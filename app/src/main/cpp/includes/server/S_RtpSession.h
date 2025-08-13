#pragma once

#include "server/S_VideoStream.h"
#include "server/S_AudioStream.h"
#include "utils/Platform.h"

typedef struct {
    S_AACStream audio_stream;
    S_VideoStream video_stream;
} S_RtpSession;

void S_Init(S_RtpSession& session,
            E_H265* video_encoder,
            E_AAC* audio_encoder);
void S_Prepare(S_RtpSession& session,
               bool_t video,
               bool_t audio);
void S_Start(
        S_RtpSession& session,
        CancellableSocket* socket,
        int_t video_interleave,
        int_t audio_interleave);
void S_Stop(S_RtpSession& session);
bool_t S_IsRunning(const S_RtpSession& session);
