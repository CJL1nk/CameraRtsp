#pragma once

#include <jni.h>
#include <array>
#include <mutex>
#include "source/audio_frame_queue.h"
#include "source/media_source.h"
#include "utils/constant.h"


class NativeAudioSource : public NativeMediaSource<MAX_AUDIO_FRAME_SIZE> {
public:
    NativeAudioSource() : frame_queue_(frameCallback, this) {}
    ~NativeAudioSource() override = default;
    void start() override;
    void onFrameAvailable(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &info) override;
    void stop() override;
    bool addListener(NativeMediaSource::FrameListener *listener) override;
    bool removeListener(NativeMediaSource::FrameListener *listener) override;
private:
    static constexpr size_t MAX_AUDIO_LISTENER = 2;

    NativeAudioFrameQueue frame_queue_;
    std::mutex listener_mutex_;
    NativeMediaSource::FrameListener* listeners_[MAX_AUDIO_LISTENER] { nullptr };

    void processQueuedFrame(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame);
    static void frameCallback(const FrameBuffer<MAX_AUDIO_FRAME_SIZE>& frame, void* context) {
        static_cast<NativeAudioSource*>(context)->processQueuedFrame(frame);
    }
};