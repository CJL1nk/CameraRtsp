#include "audio_source.h"


void NativeAudioSource::start() {
    frame_queue_.start();
}

void NativeAudioSource::onFrameAvailable(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &info) {
    if (info.flags & BUFFER_FLAG_CODEC_CONFIG) return;
    frame_queue_.enqueueFrame(info);
}

void NativeAudioSource::stop() {
    frame_queue_.stop();
}

void NativeAudioSource::processQueuedFrame(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame) {
    for (auto & i : listeners_) {
        if (i != nullptr) {
            i->onFrameAvailable(frame);
        }
    }
}

bool NativeAudioSource::addListener(NativeMediaSource::FrameListener *listener) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i == nullptr) {
            i = listener;
            return true;
        }
    }
    return false;
}

bool NativeAudioSource::removeListener(NativeMediaSource::FrameListener *listener) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i == listener) {
            i = nullptr;
            return true;
        }
    }
    return false;
}