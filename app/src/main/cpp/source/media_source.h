#pragma once

#include <jni.h>
#include "utils/frame_buffer.h"
#include "utils/constant.h"

template<size_t BufferCapacity>
class NativeMediaSource {
public:
    class FrameListener {
    public:
        virtual void onFrameAvailable(const FrameBuffer<BufferCapacity> &info) = 0;
    };
    virtual void start() = 0;
    virtual void onFrameAvailable(const FrameBuffer<BufferCapacity> &info) = 0;
    virtual void stop() = 0;
    virtual bool addListener(FrameListener *listener) = 0;
    virtual bool removeListener(FrameListener *listener) = 0;

    virtual ~NativeMediaSource() = default;
};
