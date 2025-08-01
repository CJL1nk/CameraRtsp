#pragma once

#include <jni.h>
#include <mutex>
#include "source/media_source.h"
#include "utils/constant.h"

class NativeVideoSource : public NativeMediaSource<MAX_VIDEO_FRAME_SIZE> {
public:
    NativeVideoSource() = default;
    ~NativeVideoSource() override = default;
    void start() override { }
    void onFrameAvailable(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &info) override;
    void stop() override { }
    bool addListener(NativeMediaSource::FrameListener *listener) override;
    bool removeListener(NativeMediaSource::FrameListener *listener) override;
    char vps[64] {};
    char sps[64] {};
    char pps[64] {};
    bool areParamsInitialized() const { return params_initialized_.load(); }
private:
    static constexpr size_t MAX_VIDEO_LISTENER = 2;
    std::mutex listener_mutex_;
    std::atomic_bool params_initialized_ { false };
    NativeMediaSource::FrameListener* listeners_[MAX_VIDEO_LISTENER] { nullptr };
    void parseParameterSets(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &info);
};