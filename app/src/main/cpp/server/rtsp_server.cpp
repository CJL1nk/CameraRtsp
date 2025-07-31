//
// Created by thanh on 30/07/2025.
//
#include <arpa/inet.h>
#include "rtsp_server.h"
#include "source/video_source.h"
#include "source/video_source.h"
#include "packetizer/aac_latm_packetizer.h"
#include "packetizer/h265_packetizer.h"
#include "utils/android_log.h"

#define LOG_TAG "RTSPServer"

void RTSPServer::start(bool start_video, bool start_audio) {
    if (running_.exchange(true)) {
        return;
    }
    if (start_video) {
        media_type_ |= VIDEO_TYPE;
    }
    if (start_audio) {
        media_type_ |= AUDIO_TYPE;
    }
    thread_ = std::thread(&RTSPServer::startListening, this);
}

void RTSPServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    for (auto &session : sessions_) {
        session.tryClose();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    LOGD("CleanUp", "gracefully clean up rtsp server");
}

void RTSPServer::startListening() {
    server_ = setupServer();
    pthread_setname_np(pthread_self(), "RTSPServer");

    if (server_ < 0) {
        LOGE(LOG_TAG, "Failed to setup server, error code %d", server_);
        return;
    }

    while (running_.load()) {
        sockaddr_in client_address {};
        socklen_t client_addrlen = sizeof(client_address);
        auto client = accept(server_, (sockaddr *)&client_address, &client_addrlen);
        if (client < 0) {
            LOGE(LOG_TAG, "Failed to accept connection, error code %d", client);
            break;
        }

        if (isClientConnected(client)) {
            continue;
        }

        auto session_id = getAvailableSession();
        if (session_id == -1) {
            LOGE(LOG_TAG, "Limit session reached");
            close(client);
            return;
        }

        sessions_[session_id].socket = client;
        std::thread thread = std::thread(
                &RTSPServer::handleClient, this,
                session_id, client, client_address
        );
        thread.detach();
    }

    close(server_);
    LOGD(LOG_TAG, "Processing thread finished");
}

int32_t RTSPServer::setupServer() {
    int32_t server_fd;
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        close(server_fd);
        return -2;
    }

    memset(&server_address_, 0, server_addrlen_);
    server_address_.sin_family = AF_INET;
    server_address_.sin_addr.s_addr = INADDR_ANY;
    server_address_.sin_port = htons(RTSP_PORT);

    if (bind(server_fd, (sockaddr *)&server_address_, server_addrlen_) < 0) {
        close(server_fd);
        return -3;
    }

    // Listen
    if (listen(server_fd, MAX_CONNECTIONS) < 0) {
        close(server_fd);
        return -4;
    }

    return server_fd;
}

void RTSPServer::handleClient(int32_t session_id, int32_t client, sockaddr_in client_address) {
    char clientIp[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_address.sin_addr, clientIp, sizeof(clientIp));

    char* lines[MAX_LINES];
    char response_buffer[MAX_BUFFER_LEN];
    char receive_buffer[MAX_BUFFER_LEN];
    char session_id_str[MAX_SESSION_ID_LEN];
    char sdp_buffer[MAX_SDP_LEN];
    snprintf(session_id_str, MAX_SESSION_ID_LEN, "session_%d", session_id);
    session_id_str[MAX_SESSION_ID_LEN - 1] = '\0';
    pthread_setname_np(pthread_self(), session_id_str);

    while (!running_.load()) {
        auto len = recv(client, receive_buffer, sizeof(receive_buffer) - 1, 0);
        if (len <= 0) break;
        receive_buffer[len] = '\0';

        // Split into lines
        size_t lineCount = 0;
        char* token = strtok(receive_buffer, "\r\n");
        while (token && lineCount < MAX_LINES) {
            lines[lineCount++] = token;
            token = strtok(nullptr, "\r\n"); // Continue from last buffer
        }
        if (lineCount == 0) continue;

        const char* requestLine = lines[0];
        const char* cseq = findCSeq(requestLine);
        if (!cseq) continue;

        auto track_id = findTrackId(requestLine);

        if (strncmp(requestLine, "OPTIONS", 7) == 0) {
            snprintf(response_buffer, sizeof(response_buffer),
                     "RTSP/1.0 200 OK\r\n"
                     "CSeq: %s\r\n"
                     "Public: %s\r\n"
                     "\r\n",
                     cseq, public_methods);
        } else if (strncmp(requestLine, "DESCRIBE", 8) == 0) {
            auto sdp_len = prepareSdp(clientIp, sdp_buffer, sizeof(sdp_buffer));
            snprintf(response_buffer, sizeof(response_buffer),
                     "RTSP/1.0 200 OK\r\n"
                     "CSeq: %s\r\n"
                     "Content-Type: application/sdp\r\n"
                     "Content-Length: %zu\r\n"
                     "\r\n"
                     "%s",
                     cseq, sdp_len, sdp_buffer);
        } else if (strncmp(requestLine, "SETUP", 5) == 0 && track_id >= 0) {
            auto interleave = track_id == VIDEO_TRACK_ID ? VIDEO_INTERLEAVE : AUDIO_INTERLEAVE;
            snprintf(response_buffer, sizeof(response_buffer),
                     "RTSP/1.0 200 OK\r\n"
                     "CSeq: %s\r\n"
                     "Transport: RTP/AVP/TCP;interleaved=%d-%d;unicast\r\n"
                     "Session: %s\r\n"
                     "\r\n",
                     cseq, interleave, interleave + 1, session_id_str);
        } else if (strncmp(requestLine, "PLAY", 4) == 0) {
            snprintf(response_buffer, sizeof(response_buffer),
                     "RTSP/1.0 200 OK\r\n"
                     "CSeq: %s\r\n"
                     "Session: %s\r\n"
                     "\r\n",
                     cseq, session_id_str);
        } else if (strncmp(requestLine, "TEARDOWN", 8) == 0) {
            snprintf(response_buffer, sizeof(response_buffer),
                     "RTSP/1.0 200 OK\r\n"
                     "CSeq: %s\r\n"
                     "Session: %s\r\n"
                     "\r\n",
                     cseq, session_id_str);
            break;
        } else {
            snprintf(response_buffer, sizeof(response_buffer),
                     "RTSP/1.0 404 Invalid command\r\n"
                     "CSeq: %s\r\n"
                     "Public: %s\r\n"
                     "\r\n",
                     cseq, public_methods);
        }
        send(client, response_buffer, strlen(response_buffer), 0);
    }

    sessions_[session_id].tryClose();
    LOGD(LOG_TAG, "Client %s disconnected, exiting listening loop.", clientIp);
}

int32_t RTSPServer::getAvailableSession() const {
    for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
        if (!sessions_[i].isRunning()) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

bool RTSPServer::isClientConnected(int32_t client) const {
    for (const auto &session : sessions_) {
        if (session.socket == client) {
            return true;
        }
    }
    return false;
}

int32_t RTSPServer::findTrackId(const char *request) {
    const char* keyword = "trackID=";
    const char* pos = strstr(request, keyword);
    if (!pos) {
        return -1;
    }

    pos += strlen(keyword);

    int trackId = atoi(pos);
    return trackId;
}

const char* RTSPServer::findCSeq(const char *request) {
    const char* keyword = "CSeq:";
    const char* pos = strstr(request, keyword);
    if (!pos) return nullptr;

    pos += strlen(keyword);
    while (*pos == ' ' || *pos == '\t') pos++;

    return pos;
}

size_t RTSPServer::prepareSdp(const char* client_ip, char* sdp_buffer, size_t buffer_size) const {
    int offset = 0;

    offset += snprintf(sdp_buffer + offset, buffer_size - offset,
                       "v=0\r\n"
                       "o=- 0 0 IN IP4 127.0.0.1\r\n"
                       "s=Camera Stream\r\n"
                       "c=IN IP4 %s\r\n"
                       "t=0 0\r\n"
                       "a=control:*\r\n",
                       client_ip
    );

    auto video_source = g_video_source;
    if ((media_type_ & VIDEO_TYPE) && video_source != nullptr) {
        offset += snprintf(sdp_buffer + offset, buffer_size - offset,
                           "\r\n"
                           "m=video 0 RTP/AVP %d\r\n"
                           "a=rtpmap:%d H265/%d\r\n"
                           "a=fmtp:%d sprop-vps=%s;sprop-sps=%s;sprop-pps=%s\r\n"
                           "a=control:trackID=%d\r\n",
                           H265_PAYLOAD_TYPE,
                           H265_PAYLOAD_TYPE, VIDEO_SAMPLE_RATE,
                           H265_PAYLOAD_TYPE, video_source->vps, video_source->sps, video_source->pps,
                           VIDEO_TRACK_ID);
    }

    if ((media_type_ & AUDIO_TYPE)) {
        offset += snprintf(sdp_buffer + offset, buffer_size - offset,
                           "\r\n"
                           "m=audio 0 RTP/AVP %d\r\n"
                           "a=rtpmap:%d MPEG4-GENERIC/%d/%d\r\n"
                           "a=fmtp:%d streamtype=5; profile-level-id=15; mode=AAC-hbr; config=0x1208; SizeLength=13; IndexLength=3; IndexDeltaLength=3;\r\n"
                           "a=control:trackID=%d\r\n",
                           AAC_PAYLOAD_TYPE,
                           AAC_PAYLOAD_TYPE, AUDIO_SAMPLE_RATE, AUDIO_CHANNEL_COUNT,
                           AAC_PAYLOAD_TYPE,
                           AUDIO_TRACK_ID
        );
    }
    sdp_buffer[offset++] = '\0';
    return offset;
}

static RTSPServer *g_rtsp_server = nullptr;

extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_server_MainServer_startNative(JNIEnv *env, jobject thiz,
                                                             jboolean start_video,
                                                             jboolean start_audio) {
    if (g_rtsp_server == nullptr) {
        g_rtsp_server = new RTSPServer();
    }
    g_rtsp_server->start(start_video, start_audio);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_server_MainServer_stopNative(JNIEnv *env, jobject thiz) {
    if (g_rtsp_server != nullptr) {
        g_rtsp_server->stop();
        g_rtsp_server = nullptr;
    }
}