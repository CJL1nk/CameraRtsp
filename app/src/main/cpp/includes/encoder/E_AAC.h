#pragma once

#include "encoder/E_Platform.h"
#include "encoder/E_AACFrameQueue.h"
#include "mediasource/M_AudioSource.h"
#include "utils/CircularDeque.h"
#include "utils/Configs.h"
#include "utils/FrameBuffer.h"
#include "utils/Platform.h"

typedef void (*E_AACFrameCallback)(void *context, const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &);

typedef struct {
    E_AACFrameCallback callback;
    void *context;
} E_AACFrameListener;

typedef CircularDeque<byte_t, MAX_AUDIO_RECORD_SIZE * 2> RecordBuffer;

typedef struct {
    // Double buffering
    RecordBuffer buffer[2];
    a_int_t read_idx;
    a_int_t write_idx;
    cond_t condition;
    lock_t buffer_lock;

    // Frame queue
    E_AACFrameQueue queue;
    E_AACQueueCb queue_cb;

    // Frame available listeners
    lock_t listener_lock;
    E_AACFrameListener listeners[MAX_AAC_LISTENER];

    // Media codec for AAC encoding
    E_Codec *codec;
    E_Format *format;

    // Audio source
    M_AudioSource *source;

    // Threading
    // Idle: recording false, stopping false
    // Start: recording true, stopping false
    // Stop: recording true, stopping true
    // Stopped: recording false, stopping false
    // Use is_stopping to check switch from start -> stop
    a_bool_t is_recording;
    a_bool_t is_stopping;
    thread_t thread;
} E_AAC;

void E_Init(E_AAC &encoder, M_AudioSource *source);
void E_Start(E_AAC &encoder);
void E_Stop(E_AAC &encoder);
bool E_AddListener(E_AAC &encoder,
                   E_AACFrameCallback callback,
                   void *ctx);
bool E_RemoveListener(E_AAC &encoder, void *ctx);
