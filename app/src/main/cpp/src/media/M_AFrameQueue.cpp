
#include "media/M_AFrameQueue.h"
#include "utils/CircularDeque.h"
#include "utils/HierarchyMemoryPool.h"

#define LOG_TAG "AudioFrameQueue"

static tm_t CalculateDelayNs(M_AFrameQueue &queue, tm_t presentation_time_us) {
    // Init on first value
    if (!queue.timing_initialized) {
        queue.start_time_ns = NowNanos();
        queue.first_frame_us = presentation_time_us;
        queue.timing_initialized = true;
        return 0;
    }

    // Delay = expected frame time (derived from first frame time)
    //          - actual frame time
    tm_t expected_ns = (presentation_time_us - queue.first_frame_us) * 1000;
    tm_t actual_ns = NowNanos() - queue.start_time_ns;

    if (actual_ns < expected_ns) {
        return expected_ns - actual_ns;
    }
    return 0;
}

static void ProcessFrame(
        M_AFrameQueue &queue,
        const QueueBuffer &frame,
        FrameBuffer<MAX_AUDIO_FRAME_SIZE> &buffer) {

    if (frame.size == 0) {
        return;
    }

    ts_t ts;
    tm_t delay;

    // Sleep if frame comes before expected
    // Also push front to process it in the next loop
    if (frame.presentation_time_us > 0) {
        delay = CalculateDelayNs(queue, frame.presentation_time_us);
        if (delay > 0) {
            {
                Lock(&queue.lock);
                PushFront(queue.queue, frame);
                Unlock(&queue.lock);
            }
            ts.tv_sec = 0;
            ts.tv_nsec = delay;
            nanosleep(&ts, nullptr);
            return;
        }
    }

    Reset(buffer);
    buffer.size = frame.size;
    buffer.flags = frame.flags;
    buffer.timeUs = frame.presentation_time_us;

    Copy(buffer.data, BufferData(frame.data), frame.size);
    ReleaseBuffer(queue.pool, frame.data);

    if (queue.callback != nullptr) {
        queue.callback->fnc(queue.callback->context, buffer);
    }
}

// Main loop for queue
static void Run(M_AFrameQueue& queue) {
    SetThreadName("AudioQueue");

    QueueBuffer frame;
    FrameBuffer<MAX_AUDIO_FRAME_SIZE> buffer;
    bool_t stopping;
    bool_t empty;

    while (true) {
        Lock(&queue.lock);

        // Wait until queue is not empty or stopped
        while (true) {
            stopping = Load(&queue.stopping);
            empty = Empty(queue.queue);
            if (stopping || !empty) {
                break;
            }
            Wait(&queue.condition, &queue.lock);
        }

        if (stopping) {
            Unlock(&queue.lock);
            break;
        }

        if (PopFront(queue.queue, frame)) {
            Unlock(&queue.lock);
            ProcessFrame(queue, frame, buffer);
        } else {
            // Remember to release the lock
            Unlock(&queue.lock);
        }
    }
    LOGI("CleanUp", "gracefully clean up audio frame queue");
}

static void* StartThread(void* arg) {
    auto* obj = static_cast<M_AFrameQueue*>(arg);
    if (obj) {
        Run(*obj);
    }
    return nullptr;
}

static void Reset(M_AFrameQueue& queue) {
    Reset(queue.pool);
    Reset(queue.queue);

    queue.start_time_ns = 0;
    queue.first_frame_us = 0;
    queue.timing_initialized = false;
}

void M_Init(M_AFrameQueue& queue, M_AQueueCb* callback) {
    Init(&queue.lock);
    Init(&queue.condition);
    Init(&queue.thread);
    Init(&queue.running);
    Init(&queue.stopping);
    Init(queue.pool);
    queue.callback = callback;
}

void M_Start(M_AFrameQueue& queue) {
    if (Load(&queue.stopping)) {
        return;
    }
    if (GetAndSet(&queue.running, true)) {
        return; // Already running
    }
    Reset(queue);
    Start(&queue.thread, StartThread, &queue);
}

void M_Enqueue(
        M_AFrameQueue& queue,
        const byte_t* data,
        sz_t size,
        tm_t timeUs,
        int_t flags) {

    QueueBuffer buffer;

    if (Load(&queue.stopping)) {
        return;
    }

    {
        // If the queue is full, drop the oldest frame
        // (also release its buffer to pool)
        Lock(&queue.lock);
        if (Full(queue.queue)) {
            PopFront(queue.queue, buffer);
            ReleaseBuffer(queue.pool, buffer.data);
        }
        Unlock(&queue.lock);
    }

    // Acquire buffer from pool
    buffer.data = AcquireBuffer(queue.pool, size);
    if (!buffer.data.ptr) {
        LOGE(LOG_TAG, "Failed to acquire memory buffer for frame size %zu", size);
        return;
    }

    // Copy the data
    Copy(BufferData(buffer.data), data, size);
    buffer.presentation_time_us = timeUs;
    buffer.size = size;
    buffer.flags = flags;

    {
        Lock(&queue.lock);
        PushBack(queue.queue, buffer);
        Unlock(&queue.lock);
    }
    Signal(&queue.condition);
}

static void MarkStopped(M_AFrameQueue& queue) {
    Store(&queue.running, false);
    Store(&queue.stopping, false);
}

void M_Stop(M_AFrameQueue& queue) {
    if (!Load(&queue.running)) {
        return;
    }
    if (GetAndSet(&queue.stopping, true)) {
        return;
    }
    Signal(&queue.condition);
    Join(&queue.thread);
    MarkStopped(queue);
}