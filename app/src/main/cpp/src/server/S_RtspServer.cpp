#include "server/S_RtspServer.h"

#define LOG_TAG "RtspServer"

static void Reset(S_RtspServer& server) {
    server.media.video_idx = -1;
    server.media.video_interleave = -1;
    server.media.audio_idx = -1;
    server.media.audio_interleave = -1;
}

void S_Init(S_RtspServer& server,
            E_H265* video_encoder,
            E_AAC* audio_encoder) {

    // Initialize clients
    for (auto& client: server.clients) {
        S_Init(client, &server.media, video_encoder, audio_encoder);
    }

    // Initialize media
    server.media.video_encoder = video_encoder;
    server.media.audio_encoder = audio_encoder;

    // Initialize threading
    Init(&server.thread);
    Init(&server.is_running);
    Init(&server.is_stopping);
}

static int_t AvailableSession(const S_RtspServer& server) {
    for (int_t i = 0; i < RTSP_MAX_CONNECTIONS; i++) {
        if (!S_IsConnected(server.clients[i])) {
            return i;
        }
    }
    return -1;
}

static void MarkStopped(S_RtspServer& server) {
    Store(&server.is_running, false);
    Store(&server.is_stopping, false);
}

static void StartListen(S_RtspServer& server) {
    int_t client_idx;
    int_t result = InitServer(
            server.server_socket,
            RTSP_PORT,
            RTSP_MAX_CONNECTIONS);

    if (result < 0) {
        LOGE(LOG_TAG, "Failed to setup server, error code %d", result);
        return;
    }

    LOGI(LOG_TAG, "RTSP server listening on port %d", RTSP_PORT);
    while (true) {

        // Stopped by flag
        if (Load(&server.is_stopping)) {
            break;
        }

        // Stopped by interrupted
        if (Wait(server.server_socket) < 0) {
            break;
        }

        // Due to limited resources, we only open maximum 2 connections.
        // In reality, I only connect to 1 client anyway.
        client_idx = AvailableSession(server);
        if (client_idx < 0) {
            // I believe there will be bug in this case, but don't bother trying that.
            LOGE(LOG_TAG, "No available client slots, please wait for client disconnected");
            continue;
        }

        result = S_Accept(server.clients[client_idx], server.server_socket);
        if (result < 0) {
            LOGE(LOG_TAG, "Failed to accept client, error code %d", result);
            continue;
        }

        S_Start(server.clients[client_idx]);
    }

    Destroy(server.server_socket);
    LOGI("CleanUp", "gracefully clean up rtsp server");
}

static void* StartServerThread(void* arg) {
    S_RtspServer* server = static_cast<S_RtspServer*>(arg);
    if (server) {
        StartListen(*server);
    }
    return nullptr;
}

void S_Start(S_RtspServer& server, bool_t start_video, bool_t start_audio) {
    if (Load(&server.is_stopping)) {
        return;
    }

    if (GetAndSet(&server.is_running, true)) {
        return; // Already running
    }

    Reset(server);

    int_t idx = 0;
    if (start_video) {
        server.media.video_idx = idx++;
        server.media.video_interleave = RTSP_VIDEO_INTERLEAVE;
    }
    if (start_audio) {
        server.media.audio_idx = idx++;
        server.media.audio_interleave = RTSP_AUDIO_INTERLEAVE;
    }

    Start(&server.thread, StartServerThread, &server);
}

static void Join(S_RtspServer& server) {
    Interrupt(server.server_socket);
    Join(&server.thread);
}

void S_Stop(S_RtspServer& server) {
    if (!Load(&server.is_running)) {
        return;
    }
    if (GetAndSet(&server.is_stopping, true)) {
        return;
    }

    for (auto& client: server.clients) {
        S_Stop(client);
    }
    Join(server);
    MarkStopped(server);
}