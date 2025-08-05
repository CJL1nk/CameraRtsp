#pragma once

#include "media/M_AudioSource.h"
#include "media/M_VideoSource.h"
#include "server/S_VideoStream.h"
#include "server/S_AudioStream.h"
#include "utils/Platform.h"

typedef struct {
    S_AudioStream audio_stream;
    S_VideoStream video_stream;
} S_RtpSession;

void S_Init(S_RtpSession& session, M_AudioSource* audio_source, M_VideoSource* video_source);
void S_Start(
        S_RtpSession& session,
        CancellableSocket* socket,
        int_t video_interleave,
        int_t audio_interleave);
void S_Stop(S_RtpSession& session);
bool_t S_IsRunning(const S_RtpSession& session);
