#pragma once

#include "utils/MemoryPool.h"

enum HierarchyBufferType
{
    HIERARCHY_BUFFER_DEFAULT = 0,
    HIERARCHY_BUFFER_MAX = 1
};

template <sz_t POOL_SIZE, sz_t DEFAULT_CAP, sz_t MAX_CAP>
struct HierarchyBuffer {
    HierarchyBufferType type;
    void *ptr; // Points to actual MemoryPoolBuffer
};

template <sz_t POOL_SIZE, sz_t DEFAULT_CAP, sz_t MAX_CAP>
struct HierarchyMemoryPool {
    static const sz_t DEFAULT_POOL_SIZE = (POOL_SIZE * 80) / 100;
    static const sz_t MAX_POOL_SIZE = POOL_SIZE - DEFAULT_POOL_SIZE;

    MemoryPool<DEFAULT_POOL_SIZE, DEFAULT_CAP> defaultPool;
    MemoryPool<MAX_POOL_SIZE, MAX_CAP> maxPool;
};

template <sz_t POOL_SIZE, sz_t DEFAULT_CAP, sz_t MAX_CAP>
void Init(HierarchyMemoryPool<POOL_SIZE, DEFAULT_CAP, MAX_CAP> &pool)
{
    Init(pool.defaultPool);
    Init(pool.maxPool);
}

template <sz_t POOL_SIZE, sz_t DEFAULT_CAP, sz_t MAX_CAP>
void Reset(HierarchyMemoryPool<POOL_SIZE, DEFAULT_CAP, MAX_CAP> &pool)
{
    Reset(pool.defaultPool);
    Reset(pool.maxPool);
}

template <sz_t POOL_SIZE, sz_t DEFAULT_CAP, sz_t MAX_CAP>
HierarchyBuffer<POOL_SIZE, DEFAULT_CAP, MAX_CAP> AcquireBuffer(
        HierarchyMemoryPool<POOL_SIZE, DEFAULT_CAP, MAX_CAP>    &pool, 
        sz_t                                                    size) {

    HierarchyBuffer<POOL_SIZE, DEFAULT_CAP, MAX_CAP> result;
    result.ptr = nullptr;

    if (size <= DEFAULT_CAP) {
        auto *buf = AcquireBuffer(pool.defaultPool, size);
        if (buf) {
            result.type = HIERARCHY_BUFFER_DEFAULT;
            result.ptr = buf;
            return result;
        }
    }

    if (size <= MAX_CAP) {
        auto *buf = AcquireBuffer(pool.maxPool, size);
        if (buf) {
            result.type = HIERARCHY_BUFFER_MAX;
            result.ptr = buf;
            return result;
        }
    }

    result.type = HIERARCHY_BUFFER_DEFAULT;
    return result; // ptr is nullptr, indicating failure
}

template <sz_t POOL_SIZE, sz_t DEFAULT_CAP, sz_t MAX_CAP>
void ReleaseBuffer(HierarchyMemoryPool<POOL_SIZE, DEFAULT_CAP, MAX_CAP>     &pool,
                   const HierarchyBuffer<POOL_SIZE, DEFAULT_CAP, MAX_CAP>   &buffer) {

    if (!buffer.ptr) {
        return;
    }

    if (buffer.type == HIERARCHY_BUFFER_DEFAULT) {
        ReleaseBuffer(pool.defaultPool,
                      static_cast<MemoryPoolBuffer<DEFAULT_CAP>*>(buffer.ptr));
    }
    else {
        ReleaseBuffer(pool.maxPool,
                      static_cast<MemoryPoolBuffer<MAX_CAP>*>(buffer.ptr));
    }
}


template <sz_t POOL_SIZE, sz_t DEFAULT_CAP, sz_t MAX_CAP>
byte_t *BufferData(const HierarchyBuffer<POOL_SIZE, DEFAULT_CAP, MAX_CAP> &buffer) {

    if (!buffer.ptr) {
        return nullptr;
    }

    if (buffer.type == HIERARCHY_BUFFER_DEFAULT) {
        return static_cast<MemoryPoolBuffer<DEFAULT_CAP>*>(buffer.ptr)->data;
    }

    return static_cast<MemoryPoolBuffer<MAX_CAP>*>(buffer.ptr)->data;
}
