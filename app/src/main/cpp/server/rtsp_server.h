#pragma once

#include "jni.h"
#include <array>
#include <mutex>
#include <pthread.h>
#include <netinet/in.h>
#include <unistd.h>
#include "rtp_session.h"

class RTSPServer {
public:
    static constexpr size_t RTSP_PORT = 8554;
    static constexpr size_t MAX_CONNECTIONS = 2;
    static constexpr uint8_t VIDEO_INTERLEAVE = 0;
    static constexpr uint8_t AUDIO_INTERLEAVE = 2;
    static constexpr uint8_t AUDIO_TYPE = 0x02;
    static constexpr uint8_t VIDEO_TYPE = 0x01;
    static constexpr size_t MAX_LINES = 32;
    static constexpr size_t MAX_LINE_LEN = 256;
    static constexpr size_t MAX_BUFFER_LEN = 1024;
    static constexpr size_t MAX_SDP_LEN = 2048;
    static constexpr size_t MAX_SESSION_ID_LEN = 10;
    static constexpr uint8_t VIDEO_TRACK_ID = 0;
    static constexpr uint8_t AUDIO_TRACK_ID = 1;

    struct Session {
        RtpSession rtp_session;
        int32_t socket = -1;
        void tryClose() {
            if (socket >= 0) {
                rtp_session.stop();
                close(socket);
                socket = -1;
            }
        }
        bool isRunning() const {
            return socket >= 0 && rtp_session.isRunning();
        };
    };

    RTSPServer() = default;
    ~RTSPServer() = default;
    void start(bool start_video, bool start_audio);
    void stop();

private:
    std::atomic<bool> running_ = false;
    pthread_t processing_thread_ {};

    uint8_t media_type_ = 0x00;

    int32_t server_ = -1;
    sockaddr_in server_address_;
    socklen_t server_addrlen_ = sizeof(server_address_);
    const char* public_methods = "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN";

    std::array<Session, MAX_CONNECTIONS> sessions_ {};
private:
    struct ClientArgs {
        RTSPServer* server;
        int32_t session_id;
        int32_t client;
        sockaddr_in client_address;
    };
private:
    void startListening();
    int32_t setupServer();
    void handleClient(int32_t session_id, int32_t client, sockaddr_in client_address);
    int32_t getAvailableSession() const;
    bool isClientConnected(int32_t client) const;
    size_t prepareSdp(const char* client_ip, char* sdp_buffer, size_t buffer_size) const;

    static int32_t findTrackId(const char* request);
    static const char* findCSeq(const char* request);

    static void* startListeningThread(void* arg) {
        auto* self = reinterpret_cast<RTSPServer*>(arg);
        self->startListening();  // call the member function
        return nullptr;
    }

    static void* handleClientThread(void* arg) {
        auto args = static_cast<ClientArgs*>(arg);
        args->server->handleClient(args->session_id, args->client, args->client_address);
        delete args;  // free allocated memory
        return nullptr;
    }
};
