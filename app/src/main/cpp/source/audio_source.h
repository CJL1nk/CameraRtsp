#pragma once

#include <jni.h>
#include <array>
#include <mutex>
#include "source/audio_frame_queue.h"
#include "source/media_source.h"

#define AUDIO_SAMPLE_RATE 44100
#define MAX_AUDIO_FRAME_SIZE 512 // Rare cases
#define NORMAL_AUDIO_FRAME_SIZE 256 // 64kbps / (44100Hz / 1024 samples per frame) frames / 8 bits per byte
#define MAX_AUDIO_LISTENER 2

class NativeAudioSource : public NativeMediaSource<MAX_AUDIO_FRAME_SIZE> {
public:
    NativeAudioSource() : frame_queue_(frameCallback, this) {}
    ~NativeAudioSource() override = default;
    void start() override;
    void onFrameAvailable(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &info) override;
    void stop() override;
    FrameInfo getCurrentFrame() override;
    bool addListener(NativeMediaSource::FrameListener *listener) override;
    bool removeListener(NativeMediaSource::FrameListener *listener) override;
private:
    FrameInfo current_frame_;
    std::mutex mutex_;
    NativeAudioFrameQueue frame_queue_;
    NativeMediaSource::FrameListener* listeners_[MAX_AUDIO_LISTENER] { nullptr };

    void processQueuedFrame(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame);
    static void frameCallback(const FrameBuffer<MAX_AUDIO_FRAME_SIZE>& frame, void* context) {
        static_cast<NativeAudioSource*>(context)->processQueuedFrame(frame);
    }
};