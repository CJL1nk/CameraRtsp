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

NativeMediaSource<MAX_AUDIO_FRAME_SIZE>::FrameInfo NativeAudioSource::getCurrentFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_frame_;
}

void NativeAudioSource::processQueuedFrame(const FrameBuffer<MAX_AUDIO_FRAME_SIZE> &frame) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_frame_.current_frame = frame;
        current_frame_.current_key_frame = frame;
    }
    for (auto & i : listeners_) {
        if (i != nullptr) {
            i->onFrameAvailable(frame);
        }
    }
}

bool NativeAudioSource::addListener(NativeMediaSource::FrameListener *listener) {
    for (auto & i : listeners_) {
        if (i == nullptr) {
            i = listener;
            return true;
        }
    }
    return false;
}

bool NativeAudioSource::removeListener(NativeMediaSource::FrameListener *listener) {
    for (auto & i : listeners_) {
        if (i == listener) {
            i = nullptr;
            return true;
        }
    }
    return false;
}


static NativeAudioSource *g_audio_source_ = nullptr;

extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_source_AudioSource_startNative(JNIEnv *env, jobject thiz) {
    if (g_audio_source_ == nullptr) {
        g_audio_source_ = new NativeAudioSource();
        g_audio_source_->start();
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_source_AudioSource_stopNative(JNIEnv *env, jobject thiz) {
    if (g_audio_source_ != nullptr) {
        g_audio_source_->stop();
        g_audio_source_ = nullptr;
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_source_AudioSource_onFrameAvailableNative(JNIEnv *env, jobject thiz,
                                                                         jobject buffer, jint offset,
                                                                         jint size, jlong time,
                                                                         jint flags) {
    if (buffer == nullptr) {
        return;
    }

    void* data = env->GetDirectBufferAddress(buffer);
    if (data == nullptr) {
        return;
    }

    // Access the buffer as needed
    auto* bytes = reinterpret_cast<uint8_t*>(data);
    auto buffer_size = static_cast<size_t>(size);

    if (g_audio_source_ != nullptr && buffer_size <= MAX_AUDIO_FRAME_SIZE) {
        FrameBuffer<MAX_AUDIO_FRAME_SIZE> frame(time, buffer_size, flags);
        std::memcpy(frame.data.data(), bytes + offset, size);
        g_audio_source_->onFrameAvailable(frame);
    }
}