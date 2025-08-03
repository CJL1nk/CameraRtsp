#pragma once

#include <deque>
#include <pthread.h>

#include "utils/circular_deque.h"
#include "utils/frame_buffer.h"
#include "utils/memory_pool.h"
#include "utils/constant.h"

class NativeAudioFrameQueue {
public:
    using FrameCallbackFunction = void (*)(const FrameBuffer<MAX_AUDIO_FRAME_SIZE>&, void* context);

    NativeAudioFrameQueue() = default;
    ~NativeAudioFrameQueue();
    void start();
    void stop();
    void enqueueFrame(const uint8_t *data,
                      size_t size,
                      int64_t presentation_time_us,
                      int32_t flags);
    bool addFrameCallback(FrameCallbackFunction callback_fn, void* ctx);
    bool removeFrameCallback(void* ctx);

private:
    struct FrameCallback {
        FrameCallbackFunction callback;
        void* context;
    };

private:
    static constexpr size_t MAX_AUDIO_FRAME_QUEUE_SIZE = 30; // media codec batches 15 frames per encode
    static constexpr size_t MAX_CALLBACK = 1;

    // State
    std::atomic<bool> running_ = false;

    // Thread
    pthread_t processing_thread_ {};

    // Buffer pool
    using MemoryPool = HierarchyMemoryPool<MAX_AUDIO_FRAME_QUEUE_SIZE, NORMAL_AUDIO_FRAME_SIZE, MAX_AUDIO_FRAME_SIZE>;
    MemoryPool memory_pool_;

    struct QueueBuffer {
        MemoryPool::Buffer data;
        int64_t presentation_time_us;
        size_t size;
        int32_t flags;

        QueueBuffer() : data(MemoryPool::Buffer{ MemoryPool::Buffer::Type::Default, nullptr }), presentation_time_us(0), size(0), flags(0) {}
        QueueBuffer(const MemoryPool::Buffer &d, int64_t t, size_t s, int32_t f)
        : data(d), presentation_time_us(t), size(s), flags(f) {}
        ~QueueBuffer() = default;
    };

    // Frame queue
    std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    CircularDeque<QueueBuffer, MAX_AUDIO_FRAME_QUEUE_SIZE> frame_queue_;

    // Timing
    uint64_t start_time_ns_ = 0;
    int64_t first_frame_us_ = 0;
    bool timing_initialized_ = false;

    // Callback
    std::mutex callback_mutex_;
    FrameCallback callbacks_[MAX_CALLBACK] {};

    // Methods
    void runProcessing();
    void processFrame(const QueueBuffer &frame);
    uint64_t calculateDelayNanos(int64_t presentation_time_us);

private:
    static void* runProcessingThread(void* arg) {
        auto* self = static_cast<NativeAudioFrameQueue*>(arg);
        self->runProcessing();  // call the member function
        return nullptr;
    }
};