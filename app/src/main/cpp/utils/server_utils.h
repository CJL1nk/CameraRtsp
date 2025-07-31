#pragma once

#include "jni.h"
#include <random>

static inline uint8_t genSSRC() {
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 255);
    return static_cast<uint8_t>(dist(rd));
}

static inline uint32_t genRtpTimestamp() {
    std::random_device rd;
    return rd();
}

static inline uint16_t genSequenceNumber() {
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 65536);
    return static_cast<uint16_t>(dist(rd));
}