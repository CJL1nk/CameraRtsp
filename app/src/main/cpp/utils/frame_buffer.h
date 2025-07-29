#pragma once

#include <jni.h>
#include <array>

template<size_t BufferCapacity>
struct FrameBuffer {
    std::array<uint8_t, BufferCapacity> data{};
    int64_t presentation_time_us;
    size_t size;
    int32_t flags;

    FrameBuffer() : presentation_time_us(0), size(0), flags(0) {}
    FrameBuffer(int64_t t, size_t s, int32_t f)
            : presentation_time_us(t), size(s), flags(f) {}
    ~FrameBuffer() = default;
};

