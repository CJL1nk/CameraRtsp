#pragma once

#include "media/M_AFrameQueue.h"
#include "media/M_Platform.h"
#include "utils/Configs.h"
#include "utils/FrameBuffer.h"
#include "utils/Platform.h"

typedef void (*M_AEncodedCallback)(void *context, const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &);

typedef struct {
    M_AEncodedCallback callback;
    void *context;
} M_AEncodedListener;

typedef CircularDeque<byte_t, MAX_AUDIO_RECORD_SIZE * 2> RecordBuffer;

typedef struct {
    // Double buffering
    RecordBuffer buffer[2];
    a_int_t read_idx;
    a_int_t write_idx;
    cond_t condition;
    lock_t buffer_lock;

    // Frame queue
    M_AFrameQueue queue;
    M_AQueueCb queue_cb;

    // Frame available listeners
    lock_t listener_lock;
    M_AEncodedListener listeners[MAX_AUDIO_LISTENER];

    // AAudio stream
    M_AStream *stream;

    // Media codec for AAC encoding
    M_Codec *encoder;
    M_Format *format;

    // Threading
    // Idle: recording false, stopping false
    // Start: recording true, stopping false
    // Stop: recording true, stopping true
    // Stopped: recording false, stopping false
    // Use is_stopping to check switch from start -> stop
    a_bool_t is_recording;
    a_bool_t is_stopping;
    thread_t thread;
} M_AudioSource;

void M_Init(M_AudioSource &source);
void M_Start(M_AudioSource &source);
void M_Stop(M_AudioSource &source);
bool M_AddListener(M_AudioSource &source, M_AEncodedCallback callback, void *ctx);
bool M_RemoveListener(M_AudioSource &source, void *ctx);
