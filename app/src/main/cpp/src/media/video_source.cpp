#include "media/video_source.h"

#include "utils/base64.h"
#include "utils/h265_nal_unit.h"


void NativeVideoSource::onFrameAvailable(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame) {
    if (frame.flags & BUFFER_FLAG_CODEC_CONFIG) {
        parseParameterSets(frame);
        return;
    }
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i.callback != nullptr && i.context != nullptr) {
            i.callback(frame, i.context);
        }
    }
}

bool NativeVideoSource::addListener(FrameAvailableFunction callback, void* ctx) {
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

bool NativeVideoSource::removeListener(void* ctx) {
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

void NativeVideoSource::parseParameterSets(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &frame) {
    auto nals = extractNalUnits<3>(frame.data.data(), 0, frame.size);
    for (auto nal : nals) {
        auto nal_type = NAL_TYPE(frame.data, nal);
        char* dst = nullptr;
        if (nal_type == 32) {
            dst = vps;
        } else if (nal_type == 33) {
            dst = sps;
        } else if (nal_type == 34) {
            dst = pps;
        }
        if (dst != nullptr) {
            convertBase64(frame.data.data(), nal.start, nal.end, dst);
        }
    }
    params_initialized_.exchange(true);
}