#include "video_source.h"

#include "utils/base64.h"
#include "utils/h265_nal_unit.h"


void NativeVideoSource::onFrameAvailable(const FrameBuffer<MAX_VIDEO_FRAME_SIZE> &info) {
    if (info.flags & BUFFER_FLAG_CODEC_CONFIG) {
        parseParameterSets(info);
        return;
    }
    for (auto & i : listeners_) {
        if (i != nullptr) {
            i->onFrameAvailable(info);
        }
    }
}

bool NativeVideoSource::addListener(NativeMediaSource::FrameListener *listener) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (auto & i : listeners_) {
        if (i == nullptr) {
            i = listener;
            return true;
        }
    }
    return false;
}

bool NativeVideoSource::removeListener(NativeMediaSource::FrameListener *listener) {
    std::lock_guard<std::mutex> lock(listener_mutex_);
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
    params_initialized_.exchange(true);
}