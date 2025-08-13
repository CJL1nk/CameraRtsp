#pragma once

#include "utils/Configs.h"
#include "utils/Platform.h"
#include "server/S_Platform.h"
#include "server/S_RtpSession.h"

struct S_RtspMedia {
    int_t video_idx;
    int_t audio_idx;
    int_t video_interleave;
    int_t audio_interleave;
    E_H265* video_encoder;
    E_AAC* audio_encoder;
};

struct S_RtspClient {
    S_RtpSession rtp_session;

    CancellableSocket socket;
    S_RtspMedia* media;

    int_t id;

    thread_t thread;
    a_bool_t thread_start;
};

void S_Init(S_RtspClient& client,
            S_RtspMedia* media,
            E_H265* video_encoder,
            E_AAC* audio_encoder);

int_t S_Accept(S_RtspClient& client, const CancellableSocket& server_socket);

void S_Start(S_RtspClient& client);

bool_t S_IsConnected(const S_RtspClient& client);

void S_Stop(S_RtspClient& client);