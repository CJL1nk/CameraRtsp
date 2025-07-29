#include "video_source.h"

void NativeVideoSource::onFrameAvailable(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &info) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_frame_.current_frame = info;
        // TODO: parse vps/sps/pps
        if (info.flags & BUFFER_FLAG_KEY_FRAME) {
            current_frame_.current_key_frame = info;
        }
    }
    for (auto & i : listeners_) {
        if (i != nullptr) {
            i->onFrameAvailable(info);
        }
    }
}

NativeMediaSource<MAX_VIDEO_FRAME_SIZE>::FrameInfo NativeVideoSource::getCurrentFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_frame_;
}

bool NativeVideoSource::addListener(NativeMediaSource::FrameListener *listener) {
    for (auto & i : listeners_) {
        if (i == nullptr) {
            i = listener;
            return true;
        }
    }
    return false;
}

bool NativeVideoSource::removeListener(NativeMediaSource::FrameListener *listener) {
    for (auto & i : listeners_) {
        if (i == listener) {
            i = nullptr;
            return true;
        }
    }
    return false;
}

static NativeVideoSource *g_video_source_ = nullptr;

extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_source_CameraSource_startNative(JNIEnv *env, jobject thiz) {
    if (g_video_source_ == nullptr) {
        g_video_source_ = new NativeVideoSource();
        g_video_source_->start();
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_source_CameraSource_stopNative(JNIEnv *env, jobject thiz) {
    if (g_video_source_ != nullptr) {
        g_video_source_->stop();
        g_video_source_ = nullptr;
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_source_CameraSource_onFrameAvailableNative(JNIEnv *env, jobject thiz,
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

    if (g_video_source_ != nullptr && buffer_size <= MAX_VIDEO_FRAME_SIZE) {
        FrameBuffer<MAX_VIDEO_FRAME_SIZE> frame(time, buffer_size, flags);
        std::memcpy(frame.data.data(), bytes + offset, size);
        g_video_source_->onFrameAvailable(frame);
    }
}