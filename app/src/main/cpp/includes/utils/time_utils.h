#pragma once

#include <ctime>
#include "jni.h"

static uint64_t currentTimeNanos() {
    timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

static uint64_t currentTimeMicros() {
    timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)(ts.tv_sec) * 1'000'000 + ts.tv_nsec / 1000;
}
