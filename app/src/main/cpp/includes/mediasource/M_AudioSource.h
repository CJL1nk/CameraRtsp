#pragma once

#include "mediasource/M_Platform.h"
#include "utils/Configs.h"
#include "utils/Platform.h"

typedef void (*M_AFrameCallback)(void *context, const byte_t* data, sz_t size);

typedef struct {
    M_AFrameCallback callback;
    void *context;
} M_AFrameListener;

typedef struct {
    // AAudio stream
    M_AStream *stream;

    // Status
    a_bool_t running;

    // Frame available listeners
    lock_t listener_lock;
    M_AFrameListener listeners[MAX_AUDIO_LISTENER];
} M_AudioSource;

void M_Init(M_AudioSource &source);
void M_Start(M_AudioSource &source);
void M_Stop(M_AudioSource &source);
bool M_AddListener(M_AudioSource &source, M_AFrameCallback callback, void *ctx);
bool M_RemoveListener(M_AudioSource &source, void *ctx);
