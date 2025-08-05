#pragma once

#include "media/M_AudioSource.h"
#include "media/M_VideoSource.h"
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
        M_VideoSource* video_source,
        M_AudioSource* audio_source);
void S_Start(S_RtspServer& server, bool_t start_video, bool_t start_audio);
void S_Stop(S_RtspServer& server);