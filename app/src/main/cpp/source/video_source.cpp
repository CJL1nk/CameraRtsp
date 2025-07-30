#include "video_source.h"

#include "utils/base64.h"
#include "utils/h265_nal_unit.h"

#define NAL_TYPE(data, nal) ((data[nal.start + nal.codeSize] >> 1) & 0x3F)

void NativeVideoSource::onFrameAvailable(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &info) {
    if (info.flags & BUFFER_FLAG_CODEC_CONFIG) {
        parseParameterSets(info);
    }
    for (auto & i : listeners_) {
        if (i != nullptr) {
            i->onFrameAvailable(info);
        }
    }
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

void NativeVideoSource::parseParameterSets(const FrameBuffer<102400> &info) {
    auto nals = extractNalUnits<3>(info.data.data(), 0, info.size);
    for (auto nal : nals) {
        auto nal_type = NAL_TYPE(info.data, nal);
        char* dst = nullptr;
        if (nal_type == 32) {
            dst = vps;
        } else if (nal_type == 33) {
            dst = sps;
        } else if (nal_type == 34) {
            dst = pps;
        }
        if (dst != nullptr) {
            convertBase64(info.data.data(), nal.start, nal.end, dst);
        }
    }
}


extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_source_CameraSource_startNative(JNIEnv *env, jobject thiz) {
    if (g_video_source == nullptr) {
        g_video_source = new NativeVideoSource();
        g_video_source->start();
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_com_pntt3011_cameraserver_source_CameraSource_stopNative(JNIEnv *env, jobject thiz) {
    if (g_video_source != nullptr) {
        g_video_source->stop();
        g_video_source = nullptr;
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

    if (g_video_source != nullptr && buffer_size <= MAX_VIDEO_FRAME_SIZE) {
        FrameBuffer<MAX_VIDEO_FRAME_SIZE> frame(time, buffer_size, flags);
        std::memcpy(frame.data.data(), bytes + offset, size);
        g_video_source->onFrameAvailable(frame);
    }
}