#pragma once

#include <jni.h>
#include <array>
#include <mutex>

template<size_t PoolSize, size_t BufferCapacity>
class MemoryPool {
public:
    struct Buffer {
        std::array<uint8_t, BufferCapacity> data{};
        bool in_use = false;
    };
    MemoryPool() {
        for (auto &buf: buffers_) {
            buf.in_use = false;
        }
    }
    ~MemoryPool() = default;

    Buffer *acquire(size_t size) {
        if (size > BufferCapacity) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& buf : buffers_) {
            if (!buf.in_use) {
                buf.in_use = true;
                return &buf;
            }
        }
        return nullptr;
    };

    void release(Buffer* buf) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buf) {
            buf->in_use = false;
        }
    }

private:
    std::mutex mutex_;
    std::array<Buffer, PoolSize> buffers_;
};


template<size_t TotalPoolSize, size_t DefaultCapacity, size_t MaxCapacity>
class HierarchyMemoryPool {
    static constexpr size_t DefaultPoolSize = (TotalPoolSize * 80) / 100;
    static constexpr size_t MaxPoolSize = TotalPoolSize - DefaultPoolSize;

    using DefaultPool = MemoryPool<DefaultPoolSize, DefaultCapacity>;
    using MaxPool = MemoryPool<MaxPoolSize, MaxCapacity>;

public:
    struct Buffer {
        enum class Type { Default, Max };
        Type type;
        void* ptr;  // Points to actual MemoryPool::Buffer
    };

    HierarchyMemoryPool() = default;
    ~HierarchyMemoryPool() = default;

    Buffer acquire(size_t size) {
        if (size <= DefaultCapacity) {
            if (auto* buf = default_pool_.acquire(size)) {
                return Buffer{ Buffer::Type::Default, buf };
            }
        }

        if (size <= MaxCapacity) {
            if (auto* buf = max_pool_.acquire(size)) {
                return Buffer{ Buffer::Type::Max, buf };
            }
        }

        return Buffer{ Buffer::Type::Default, nullptr };  // null means failed
    }

    void release(const Buffer& buffer) {
        if (!buffer.ptr) return;
        if (buffer.type == Buffer::Type::Default) {
            default_pool_.release(static_cast<typename DefaultPool::Buffer*>(buffer.ptr));
        } else {
            max_pool_.release(static_cast<typename MaxPool::Buffer*>(buffer.ptr));
        }
    }

    size_t capacity(const Buffer& buffer) const {
        if (buffer.type == Buffer::Type::Default) {
            return DefaultCapacity;
        }
        return MaxCapacity;
    }

    void* data(const Buffer& buffer) const {
        if (buffer.type == Buffer::Type::Default) {
            return static_cast<typename DefaultPool::Buffer*>(buffer.ptr)->data.data();
        }
        return static_cast<typename MaxPool::Buffer*>(buffer.ptr)->data.data();
    }

private:
    DefaultPool default_pool_;
    MaxPool max_pool_;
};
