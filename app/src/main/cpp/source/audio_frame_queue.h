#pragma once

#include <thread>
#include <deque>

#include "source/audio_source.h"
#include "utils/circular_deque.h"
#include "utils/memory_pool.h"

#define MAX_AUDIO_FRAME_QUEUE_SIZE 10 // 200ms delay is enough

class NativeAudioFrameQueue {
public:
    typedef void (*FrameCallback)(const FrameBuffer<MAX_AUDIO_FRAME_SIZE>&, void* context);
    explicit NativeAudioFrameQueue(FrameCallback callback, void* ctx) : callback_func_(callback), context_(ctx) {}
    ~NativeAudioFrameQueue();
    void start();
    void stop();
    void enqueueFrame(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame);

private:
    // State
    std::atomic<bool> running_ = false;

    // Thread
    std::thread processing_thread_;

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
    std::chrono::steady_clock::time_point start_time_;
    int64_t first_frame_us_ = 0;
    bool timing_initialized_ = false;

    // Callback
    FrameCallback callback_func_;
    void* context_;

    // Methods
    void runProcessing();
    void processFrame(const QueueBuffer &frame);
    std::chrono::milliseconds calculateDelay(int64_t presentation_time_us);
};