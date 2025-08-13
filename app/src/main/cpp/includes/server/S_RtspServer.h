#pragma once

#include "utils/Configs.h"
#include "utils/Platform.h"
#include "server/S_Platform.h"
#include "server/S_RtspClient.h"

struct S_RtspServer {
    S_RtspClient clients[RTSP_MAX_CONNECTIONS];

    CancellableSocket server_socket;
    S_RtspMedia media;

    a_bool_t is_running;
    a_bool_t is_stopping;
    thread_t thread;
};

void S_Init(
        S_RtspServer& server,
        E_H265* video_encoder,
        E_AAC* audio_encoder);
void S_Start(S_RtspServer& server, bool_t start_video, bool_t start_audio);
void S_Stop(S_RtspServer& server);