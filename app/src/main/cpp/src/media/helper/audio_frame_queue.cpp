#include <jni.h>
#include <android/log.h>
#include <pthread.h>

#include <atomic>
#include <mutex>

#include "media/helper/audio_frame_queue.h"
#include "utils/android_log.h"

#define LOG_TAG "NativeAudioFrameQueue"

NativeAudioFrameQueue::~NativeAudioFrameQueue() {
    stop();
}

void NativeAudioFrameQueue::start() {
    if (running_.exchange(true)) {
        return; // Already running
    }
    pthread_create(&processing_thread_, nullptr, runProcessingThread, this);
}

void NativeAudioFrameQueue::enqueueFrame(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame) {
    if (!running_.load()) {
        return; // Not running
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (frame_queue_.full()) {
            QueueBuffer queue_buffer {};
            frame_queue_.pop_front(queue_buffer);
            memory_pool_.release(queue_buffer.data);
        }
    }

    MemoryPool::Buffer buffer = memory_pool_.acquire(frame.size);
    if (!buffer.ptr) {
        LOGE(LOG_TAG, "Failed to acquire memory buffer for frame size %zu", frame.size);
        return;
    }
    memcpy(memory_pool_.data(buffer), frame.data.data(), frame.size);

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        frame_queue_.push_back(QueueBuffer(buffer, frame.presentation_time_us, frame.size, frame.flags));
        queue_condition_.notify_one();
    }
}

void NativeAudioFrameQueue::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    queue_condition_.notify_one();

    pthread_join(processing_thread_, nullptr);

    LOGD("CleanUp", "gracefully clean up audio frame queue");
}

void NativeAudioFrameQueue::runProcessing() {
    pthread_setname_np(pthread_self(), "AudioQueue");

    QueueBuffer current_buffer {};
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // Wait for frames or stop signal
        queue_condition_.wait(lock, [this]{
            return !frame_queue_.empty() || !running_.load();
        });

        if (!running_.load()) {
            break;
        }

        if (frame_queue_.pop_front(current_buffer)) {
            lock.unlock();
            processFrame(current_buffer);
        }
    }

    LOGD(LOG_TAG, "Processing thread finished");
}

void NativeAudioFrameQueue::processFrame(const QueueBuffer &frame) {
    if (frame.size == 0) {
        return;
    }

    if (frame.presentation_time_us > 0) {
        auto delay = calculateDelayNanos(frame.presentation_time_us);
        if (delay > 0) {
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                frame_queue_.push_front(frame);
            }
            timespec ts {};
            ts.tv_sec = 0;
            ts.tv_nsec = static_cast<long>(delay * 1'000'000L);  // 500 ms
            nanosleep(&ts, nullptr);
            return;
        }
    }

    FrameBuffer<MAX_AUDIO_FRAME_SIZE> buffer;
    buffer.presentation_time_us = frame.presentation_time_us;
    buffer.size = frame.size;
    buffer.flags = frame.flags;
    memcpy(buffer.data.data(), memory_pool_.data(frame.data), frame.size);
    memory_pool_.release(frame.data);

    std::lock_guard<std::mutex> lock(callback_mutex_);
    for (auto & i : callbacks_) {
        if (i.callback != nullptr && i.context != nullptr) {
            i.callback(buffer, i.context);
        }
    }
}

uint64_t NativeAudioFrameQueue::calculateDelayNanos(int64_t presentation_time_us) {
    if (!timing_initialized_) {
        start_time_ns_ = currentTimeNanos();
        first_frame_us_ = presentation_time_us;
        timing_initialized_ = true;
        return 0;
    }

    auto frame_delta_ms = static_cast<uint64_t>(presentation_time_us - first_frame_us_) / 1000;

    auto now = currentTimeNanos();
    auto elapsed_ms = (now - start_time_ns_) / 1'000'000;

    if (elapsed_ms < frame_delta_ms) {
        return frame_delta_ms - elapsed_ms;
    }

    return 0;
}

bool NativeAudioFrameQueue::addFrameCallback(NativeAudioFrameQueue::FrameCallbackFunction callback_fn, void* ctx) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    for (auto & i : callbacks_) {
        if (i.callback == nullptr && i.context == nullptr) {
            i.callback = callback_fn;
            i.context = ctx;
            return true;
        }
    }
    return false;
}

bool NativeAudioFrameQueue::removeFrameCallback(void* ctx) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    for (auto & i : callbacks_) {
        if (i.context == ctx) {
            i.callback = nullptr;
            i.context = nullptr;
            return true;
        }
    }
    return false;
}

