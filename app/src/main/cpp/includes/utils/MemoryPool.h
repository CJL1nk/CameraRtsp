#pragma once

#include "Platform.h"

template <sz_t BUFFER_CAPACITY>
struct MemoryPoolBuffer {
    byte_t data[BUFFER_CAPACITY];
    bool_t use;
};

template <sz_t POOL_SIZE, sz_t BUFFER_CAPACITY>
struct MemoryPool {
    MemoryPoolBuffer<BUFFER_CAPACITY> buffers[POOL_SIZE];
    lock_t lock;
};

template <sz_t POOL_SIZE, sz_t BUFFER_CAPACITY>
void Init(MemoryPool<POOL_SIZE, BUFFER_CAPACITY> &pool) {
    Init(&pool.lock);
}

template <sz_t POOL_SIZE, sz_t BUFFER_CAPACITY>
void Reset(MemoryPool<POOL_SIZE, BUFFER_CAPACITY> &pool) {
    Reset(pool.buffers, sizeof(pool.buffers));
}

template <sz_t POOL_SIZE, sz_t BUFFER_CAPACITY>
MemoryPoolBuffer<BUFFER_CAPACITY> *AcquireBuffer(
        MemoryPool<POOL_SIZE, BUFFER_CAPACITY>  &pool,
        sz_t                                    size){

    if (size > BUFFER_CAPACITY) {
        return nullptr;
    }

    Lock(&pool.lock);
    for (sz_t i = 0; i < POOL_SIZE; ++i) {
        if (!pool.buffers[i].use) {
            pool.buffers[i].use = true;
            Unlock(&pool.lock);
            return &pool.buffers[i];
        }
    }
    Unlock(&pool.lock);
    return nullptr;
}

template <sz_t POOL_SIZE, sz_t BUFFER_CAPACITY>
void ReleaseBuffer(
        MemoryPool<POOL_SIZE, BUFFER_CAPACITY>  &pool,
        MemoryPoolBuffer<BUFFER_CAPACITY>       *buf) {

    if (!buf)
        return;

    Lock(&pool.lock);
    buf->use = false;
    Unlock(&pool.lock);
}