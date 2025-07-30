#pragma once

#include <jni.h>
#include <mutex>
#include "source/media_source.h"

#define MAX_VIDEO_FRAME_SIZE 102400 // Rare cases
#define NORMAL_VIDEO_FRAME_SIZE 20480 // 2Mbps / 24 frames / 8 bits per byte
#define MAX_VIDEO_LISTENER 2
#define VIDEO_SAMPLE_RATE 90000 // H264/H265 standard


class NativeVideoSource : public NativeMediaSource<MAX_VIDEO_FRAME_SIZE> {
public:
    NativeVideoSource() = default;
    ~NativeVideoSource() override = default;
    void start() override { }
    void onFrameAvailable(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &info) override;
    void stop() override { }
    FrameInfo getCurrentFrame() override;
    bool addListener(NativeMediaSource::FrameListener *listener) override;
    bool removeListener(NativeMediaSource::FrameListener *listener) override;
private:
    FrameInfo current_frame_;
    std::mutex mutex_;
    NativeMediaSource::FrameListener* listeners_[MAX_VIDEO_LISTENER] { nullptr };
};