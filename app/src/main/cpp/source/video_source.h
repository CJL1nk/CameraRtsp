#pragma once

#include <jni.h>
#include <mutex>
#include "utils/constant.h"
#include "utils/frame_buffer.h"

class NativeVideoSource {
public:
    using FrameAvailableFunction = void (*)(const FrameBuffer<MAX_VIDEO_FRAME_SIZE>&, void* context);

    NativeVideoSource() = default;
    ~NativeVideoSource() = default;
    void start() { }
    void onFrameAvailable(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame);
    void stop() { }
    bool addListener(FrameAvailableFunction callback, void* ctx);
    bool removeListener(void* ctx);
    char vps[64] {};
    char sps[64] {};
    char pps[64] {};
    bool areParamsInitialized() const { return params_initialized_.load(); }

private:
    struct FrameListener {
        FrameAvailableFunction callback;
        void* context;
    };

private:
    static constexpr size_t MAX_VIDEO_LISTENER = 2;
    std::mutex listener_mutex_;
    std::atomic_bool params_initialized_ { false };
    FrameListener listeners_[MAX_VIDEO_LISTENER] {};
    void parseParameterSets(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame);
};