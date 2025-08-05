#pragma once

#include "Platform.h"

template<sz_t CAPACITY>
struct FrameBuffer {
    byte_t data[CAPACITY];
    tm_t timeUs;
    sz_t size;
    int_t flags;
};

template<sz_t CAPACITY>
void Reset(FrameBuffer<CAPACITY>& buffer) {
    Reset(buffer.data, CAPACITY);
    buffer.timeUs = 0;
    buffer.size = 0;
    buffer.flags = 0;
}

template<sz_t CAPACITY>
int_t Compare(const FrameBuffer<CAPACITY>& a, const FrameBuffer<CAPACITY>& b) {
    if (a.timeUs < b.timeUs)
        return -1;

    else if (a.timeUs > b.timeUs)
        return 1;

    else
        return 0;
}


