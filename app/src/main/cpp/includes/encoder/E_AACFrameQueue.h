#pragma once

#include "utils/Configs.h"
#include "utils/FrameBuffer.h"
#include "utils/HierarchyMemoryPool.h"
#include "utils/CircularDeque.h"
#include "utils/Platform.h"

typedef void (*E_AACQueueCbFnc)(void* context, const FrameBuffer<MAX_AUDIO_FRAME_SIZE>&);

typedef struct {
    void* context;
    E_AACQueueCbFnc fnc;
} E_AACQueueCb;

typedef HierarchyMemoryPool<
        MAX_AUDIO_FRAME_QUEUE_SIZE,
        NORMAL_AUDIO_FRAME_SIZE,
        MAX_AUDIO_FRAME_SIZE> QPool;

typedef HierarchyBuffer<
        MAX_AUDIO_FRAME_QUEUE_SIZE,
        NORMAL_AUDIO_FRAME_SIZE,
        MAX_AUDIO_FRAME_SIZE> QBuffer;

typedef struct {
    QBuffer data;
    tm_t presentation_time_us;
    sz_t size;
    int_t flags;
} QueueBuffer;

typedef CircularDeque<
        QueueBuffer,
        MAX_AUDIO_FRAME_QUEUE_SIZE> QDeque;

typedef struct {
    // Buffer management
    QPool pool;

    // Frame queue and synchronization
    QDeque queue;
    lock_t lock;
    cond_t condition;

    // Thread
    thread_t thread;
    a_bool_t running;
    a_bool_t stopping;

    // Callback
    E_AACQueueCb* callback;

    // Timing
    tm_t start_time_ns;
    tm_t first_frame_us;
    bool timing_initialized;
} E_AACFrameQueue;

void E_Init(E_AACFrameQueue& queue, E_AACQueueCb* callback);
void E_Start(E_AACFrameQueue& queue);
void E_Stop(E_AACFrameQueue& queue);

// This function enqueues the frame to a queue.
// That queue does 2 things:
// - If timeUs >= expected: Run the callback function
// - Else: Sleep until the expected timeUs
void E_Enqueue(
        E_AACFrameQueue& queue,
        const byte_t* data,
        sz_t size,
        tm_t timeUs,
        int_t flags);