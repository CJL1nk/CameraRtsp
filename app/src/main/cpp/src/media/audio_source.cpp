#include "media/audio_source.h"


void NativeAudioSource::start() {
    frame_queue_.addFrameCallback(frameQueueAvailableCallback, this);
    frame_queue_.start();
}

void NativeAudioSource::onFrameAvailable(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &info) {
    if (info.flags & BUFFER_FLAG_CODEC_CONFIG) return;
    frame_queue_.enqueueFrame(info);
}

void NativeAudioSource::stop() {
    frame_queue_.stop();
    frame_queue_.removeFrameCallback(this);
}

void NativeAudioSource::processQueuedFrame(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i.callback != nullptr && i.context != nullptr) {
            i.callback(frame, i.context);
        }
    }
}

bool NativeAudioSource::addListener(FrameAvailableFunction callback, void* ctx) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i.callback == nullptr && i.context == nullptr) {
            i.callback = callback;
            i.context = ctx;
            return true;
        }
    }
    return false;
}

bool NativeAudioSource::removeListener(void* ctx) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i.context == ctx) {
            i.callback = nullptr;
            i.context = nullptr;
            return true;
        }
    }
    return false;
}