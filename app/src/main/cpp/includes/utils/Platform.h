#pragma once

#include <android/log.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#define LOGI(tag, ...) __android_log_print(ANDROID_LOG_INFO, tag, __VA_ARGS__)
#define LOGE(tag, ...) __android_log_print(ANDROID_LOG_ERROR, tag, __VA_ARGS__)

typedef bool bool_t;
typedef char char_t;
typedef uint8_t byte_t;
typedef int16_t short_t;
typedef uint16_t ushort_t;
typedef int32_t int_t;
typedef uint32_t uint_t;
typedef int64_t long_t;
typedef size_t sz_t;
typedef uint64_t tm_t;
typedef double double_t;
typedef ssize_t ssz_t;

typedef pthread_mutex_t lock_t;
typedef pthread_cond_t cond_t;
typedef pthread_t thread_t;

typedef atomic_bool a_bool_t;
typedef atomic_int a_int_t;

typedef timespec ts_t;

static inline void Copy(void* dst, const void* src, sz_t size) {
    memcpy(dst, src, size);
}

static inline void Reset(void* dst, sz_t size) {
    memset(dst, 0, size);
}

static inline void Init(a_bool_t* value) {
    atomic_init(value, false);
}

static inline bool Load(const a_bool_t* value) {
    return atomic_load(value);
}

static inline void Store(a_bool_t* value, bool val) {
    atomic_store(value, val);
}

static inline bool GetAndSet(a_bool_t* value, bool val) {
    return atomic_exchange(value, val);
}

static inline void Reset(a_int_t* value) {
    atomic_init(value, 0);
}

static inline int SyncAndGet(a_int_t* value) {
    return atomic_load_explicit(value, memory_order_acquire);
}

static inline void SetAndSync(a_int_t* value, int val) {
    atomic_store_explicit(value, val, memory_order_release);
}

static inline void Init(thread_t* thread) {
    // Do nothing
}

static inline void Start(thread_t* thread, void* (*start_routine)(void*), void* arg) {
    pthread_create(thread, nullptr, start_routine, arg);
}

static inline void Join(const thread_t* thread) {
    pthread_join(*thread, nullptr);
}

static inline void SetThreadName(const char_t *name) {
    pthread_setname_np(pthread_self(), name);
}

static inline void Init(cond_t* cond) {
    pthread_cond_init(cond, nullptr);
}

static inline void Wait(cond_t* cond, lock_t* lock) {
    pthread_cond_wait(cond, lock);
}

static inline void Signal(cond_t* cond) {
    pthread_cond_signal(cond);
}

static inline void Init(lock_t *lock) {
    pthread_mutex_init(lock, nullptr);
}

static inline void Lock(lock_t *lock) {
    pthread_mutex_lock(lock);
}

static inline void Unlock(lock_t *lock) {
    pthread_mutex_unlock(lock);
}

static inline tm_t NowNanos() {
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static inline tm_t NowMicros() {
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static inline tm_t NowSecs() {
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec;
}

static inline int_t RandomInt() {
    return ((int_t)rand() << 16) ^ rand();  // 32-bit from two rand();
}

static inline short_t RandomShort() {
    return (short_t)(rand() & 0xFFFF);  // Mask to 16 bits
}

static inline int_t WriteStream(
        char_t* dst,
        sz_t size,
        const char_t* msg,
        ...) {
    va_list args;
    va_start(args, msg);
    int_t result = vsnprintf(dst, size, msg, args);
    va_end(args);
    return result;
}

static inline const char_t* FindSubString(const char_t* str, const char_t* sub_str) {
    return strstr(str, sub_str);
}

static inline int_t Len(const char_t* str) {
    return strlen(str);
}

static inline int_t Int(const char_t* src) {
    int_t value;
    if (sscanf(src, "%d", &value) == 1) {
        return value;
    }
    return -1;
}

static inline void WriteFile(int_t fd, const void* data, sz_t size) {
    write(fd, data, size);
}