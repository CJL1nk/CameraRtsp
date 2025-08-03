#pragma once

#include <jni.h>
#include <array>
#include <mutex>
#include "media/helper/audio_frame_queue.h"
#include "utils/constant.h"


class NativeAudioSource {
public:
    using FrameAvailableFunction = void (*)(const FrameBuffer<MAX_AUDIO_FRAME_SIZE>&, void* context);

    NativeAudioSource() = default;
    ~NativeAudioSource() = default;
    void start();
    void onFrameAvailable(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &info);
    void stop();
    bool addListener(FrameAvailableFunction callback, void* ctx);
    bool removeListener(void* ctx);

private:
    struct FrameListener {
        FrameAvailableFunction callback;
        void* context;
    };

private:
    static constexpr size_t MAX_AUDIO_LISTENER = 2;

    NativeAudioFrameQueue frame_queue_;
    std::mutex listener_mutex_;
    FrameListener listeners_[MAX_AUDIO_LISTENER] {};

    void processQueuedFrame(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame);

    static void frameQueueAvailableCallback(const FrameBuffer<MAX_AUDIO_FRAME_SIZE>& frame, void* context) {
        static_cast<NativeAudioSource*>(context)->processQueuedFrame(frame);
    }
};