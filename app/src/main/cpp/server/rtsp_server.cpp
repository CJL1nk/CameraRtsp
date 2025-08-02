//
// Created by thanh on 30/07/2025.
//
#include <arpa/inet.h>
#include "rtsp_server.h"
#include "source/video_source.h"
#include "source/video_source.h"
#include "utils/android_log.h"

#define LOG_TAG "RTSPServer"

void RTSPServer::start(bool start_video, bool start_audio) {
    if (running_.exchange(true)) {
        return;
    }
    int32_t idx = 0;
    if (start_video) {
        media_type_ |= VIDEO_TYPE;
        video_track_idx_ = idx++;
    }
    if (start_audio) {
        media_type_ |= AUDIO_TYPE;
        audio_track_idx_ = idx++;
    }
    pthread_create(&processing_thread_, nullptr, startListeningThread, this);
}

void RTSPServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    write(pipe_fd_[1], "x", 1);  // wakes up select()
    for (auto &session : sessions_) {
        if (session.isConnected()) {
            write(session.client_pipe_fd[1], "x", 1); // wakes up recv()
        }
        if (session.thread_start.load()) {
            pthread_join(session.thread, nullptr);
        }
    }
    pthread_join(processing_thread_, nullptr);
    LOGD("CleanUp", "gracefully clean up rtsp server");
}

void RTSPServer::startListening() {
    server_ = setupServer();
    pthread_setname_np(pthread_self(), "RTSPServer");

    if (server_ < 0) {
        LOGE(LOG_TAG, "Failed to setup server, error code %d", server_);
        return;
    }

    pipe(pipe_fd_);
    fd_set read_fds;
    int max_fd = (server_ > pipe_fd_[0]) ? server_ : pipe_fd_[0];

    while (running_.load()) {
        FD_ZERO(&read_fds);
        FD_SET(server_, &read_fds);
        FD_SET(pipe_fd_[0], &read_fds);

        select(max_fd  + 1, &read_fds, nullptr, nullptr, nullptr);

        if (FD_ISSET(pipe_fd_[0], &read_fds)) { // waked up by pipe_fd_[1]
            char buf[1];
            read(pipe_fd_[0], buf, 1);
            break;
        }

        if (FD_ISSET(server_, &read_fds)) {
            if (acceptClient() < 0) {
                break;
            }
        }
    }

    close(server_);
    LOGD(LOG_TAG, "Processing thread finished");
}


int32_t RTSPServer::acceptClient() {
    sockaddr_in client_address {};
    socklen_t client_addrlen = sizeof(client_address);

    auto client = accept(server_, (sockaddr *)&client_address, &client_addrlen);
    if (client < 0) {
        LOGE(LOG_TAG, "Failed to accept connection, error code %d", client);
        return client;
    }

    if (isClientConnected(client)) {
        return client;
    }

    auto session_id = getAvailableSession();
    if (session_id == -1) {
        LOGE(LOG_TAG, "Limit session reached");
        close(client);
        return -1;
    }

    auto &session = sessions_[session_id];
    session.client = client;
    session.client_address = client_address;

    if (session.thread_start.exchange(true)) {
        // Join old, no longer running session
        pthread_join(session.thread, nullptr);
    }

    pthread_create(&session.thread, nullptr, recvClientThread, &session);
    return client;
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

void RTSPServer::recvClient(Session& session) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &session.client_address.sin_addr, client_ip, sizeof(client_ip));

    char response_buffer[MAX_BUFFER_LEN];
    char receive_buffer[MAX_BUFFER_LEN];
    char session_id_str[MAX_SESSION_ID_LEN];
    snprintf(session_id_str, MAX_SESSION_ID_LEN, "session_%d", session.session_id);
    session_id_str[MAX_SESSION_ID_LEN - 1] = '\0';
    pthread_setname_np(pthread_self(), session_id_str);

    pipe(session.client_pipe_fd);
    fd_set read_fds;
    int max_fd = (session.client > session.client_pipe_fd[0]) ? session.client : session.client_pipe_fd[0];

    while (running_.load()) {
        FD_ZERO(&read_fds);
        FD_SET(session.client, &read_fds);
        FD_SET(session.client_pipe_fd[0], &read_fds);

        select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);

        if (FD_ISSET(session.client_pipe_fd[0], &read_fds)) {
            char buf[1];
            read(session.client_pipe_fd[0], buf, 1);
            break;
        }

        if (FD_ISSET(session.client, &read_fds)) {
            if (handleClient(session,
                             response_buffer, sizeof(response_buffer),
                             receive_buffer, sizeof(receive_buffer),
                             client_ip, session_id_str) < 0) {
                break;
            }
        }
    }

    session.rtp_session.stop();
    close(session.client);
    session.client = -1;
    LOGD(LOG_TAG, "Client %s disconnected, exiting listening loop.", client_ip);
}

int32_t RTSPServer::handleClient(RTSPServer::Session &session,
                                 char* response_buffer, size_t response_buffer_size,
                                 char* receive_buffer, size_t receive_buffer_size,
                                 char* clientIp, char* session_id_str) {
    auto len = recv(session.client, receive_buffer,  receive_buffer_size - 1, 0);
    if (len <= 0) {
        LOGE(LOG_TAG, "Failed to receive data from client");
        return -1;
    }
    receive_buffer[len] = '\0';

    auto cseq = findCSeq(receive_buffer);
    if (cseq < 0) return 0;

    auto track_id = findTrackId(receive_buffer);

    if (strncmp(receive_buffer, "OPTIONS", 7) == 0) {
        snprintf(response_buffer, response_buffer_size,
                 "RTSP/1.0 200 OK\r\n"
                 "CSeq: %d\r\n"
                 "Public: %s\r\n"
                 "\r\n",
                 cseq, public_methods);
    } else if (strncmp(receive_buffer, "DESCRIBE", 8) == 0) {
        char sdp_buffer[MAX_SDP_LEN];
        auto sdp_len = prepareSdp(clientIp, sdp_buffer, sizeof(sdp_buffer));
        snprintf(response_buffer, response_buffer_size,
                 "RTSP/1.0 200 OK\r\n"
                 "CSeq: %d\r\n"
                 "Content-Type: application/sdp\r\n"
                 "Content-Length: %zu\r\n"
                 "\r\n"
                 "%s",
                 cseq, sdp_len, sdp_buffer);
    } else if (strncmp(receive_buffer, "SETUP", 5) == 0 && track_id >= 0) {
        const char* transport_tcp = strstr(receive_buffer, "Transport: RTP/AVP/TCP");
        auto interleave = track_id == video_track_idx_ ? VIDEO_INTERLEAVE : AUDIO_INTERLEAVE;
        if (transport_tcp) {
            snprintf(response_buffer, response_buffer_size,
                     "RTSP/1.0 200 OK\r\n"
                     "CSeq: %d\r\n"
                     "Transport: RTP/AVP/TCP;interleaved=%d-%d;unicast\r\n"
                     "Session: %s\r\n"
                     "\r\n",
                     cseq, interleave, interleave + 1, session_id_str);
        } else {
            snprintf(response_buffer, response_buffer_size,
                     "RTSP/1.0 461 Unsupported Transport\r\n"
                     "CSeq: %d\r\n"
                     "Public: %s\r\n"
                     "Supported: Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n"
                     "\r\n",
                     cseq,
                     public_methods,
                     interleave, interleave + 1);
        }

    } else if (strncmp(receive_buffer, "PLAY", 4) == 0) {
        session.rtp_session.start(
                session.client,
                media_type_ & VIDEO_TYPE ? VIDEO_INTERLEAVE : -1,
                media_type_ & AUDIO_TYPE ? AUDIO_INTERLEAVE : -1
        );
        snprintf(response_buffer, response_buffer_size,
                 "RTSP/1.0 200 OK\r\n"
                 "CSeq: %d\r\n"
                 "Session: %s\r\n"
                 "\r\n",
                 cseq, session_id_str);
    } else if (strncmp(receive_buffer, "TEARDOWN", 8) == 0) {
        snprintf(response_buffer, response_buffer_size,
                 "RTSP/1.0 200 OK\r\n"
                 "CSeq: %d\r\n"
                 "Session: %s\r\n"
                 "\r\n",
                 cseq, session_id_str);
        return -1;
    } else {
        snprintf(response_buffer, response_buffer_size,
                 "RTSP/1.0 404 Invalid command\r\n"
                 "CSeq: %d\r\n"
                 "Public: %s\r\n"
                 "\r\n",
                 cseq, public_methods);
    }
    send(session.client, response_buffer, strlen(response_buffer), 0);
    return 0;
}

int32_t RTSPServer::getAvailableSession() const {
    for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
        if (!sessions_[i].isConnected()) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

bool RTSPServer::isClientConnected(int32_t client) const {
    for (const auto &session : sessions_) {
        if (session.client == client) {
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

int32_t RTSPServer::findCSeq(const char *request) {
    const char* keyword = "CSeq:";
    const char* pos = strstr(request, keyword);
    if (!pos) return -1;

    pos += strlen(keyword);
    while (*pos == ' ' || *pos == '\t') pos++;
    int32_t value;
    if (sscanf(pos, "%d", &value) == 1) {
        return value;
    }
    return -1;
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

    if ((media_type_ & VIDEO_TYPE) && video_source_ != nullptr && video_source_->areParamsInitialized()) {
        offset += snprintf(sdp_buffer + offset, buffer_size - offset,
                           "\r\n"
                           "m=video 0 RTP/AVP %d\r\n"
                           "a=rtpmap:%d H265/%d\r\n"
                           "a=fmtp:%d sprop-vps=%s;sprop-sps=%s;sprop-pps=%s\r\n"
                           "a=control:trackID=%d\r\n",
                           H265_PAYLOAD_TYPE,
                           H265_PAYLOAD_TYPE, VIDEO_SAMPLE_RATE,
                           H265_PAYLOAD_TYPE, video_source_->vps, video_source_->sps, video_source_->pps,
                           video_track_idx_);
    }

    if ((media_type_ & AUDIO_TYPE)) {
        offset += snprintf(sdp_buffer + offset, buffer_size - offset,
                           "\r\n"
                           "m=audio 0 RTP/AVP %d\r\n"
                           "a=rtpmap:%d MPEG4-GENERIC/%d/%d\r\n"
                           "a=fmtp:%d streamtype=5; profile-level-id=15; mode=AAC-hbr; config=1208; SizeLength=13; IndexLength=3; IndexDeltaLength=3;\r\n"
                           "a=control:trackID=%d\r\n",
                           AAC_PAYLOAD_TYPE,
                           AAC_PAYLOAD_TYPE, AUDIO_SAMPLE_RATE, AUDIO_CHANNEL_COUNT,
                           AAC_PAYLOAD_TYPE,
                           audio_track_idx_
        );
    }
    sdp_buffer[offset] = '\0';
    return offset;
}