#include "memory_pool.h"

MemoryPool::MemoryPool(size_t pool_size, size_t buffer_capacity)
        : max_pool_size_(pool_size), default_capacity_(buffer_capacity) {
    pool_.reserve(pool_size);
}

std::shared_ptr<MemoryPool::Buffer> MemoryPool::acquire(size_t min_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = pool_.begin(); it != pool_.end(); ++it) {
        if ((*it)->data.capacity() >= min_size) {
            auto buffer = *it;
            pool_.erase(it);
            buffer->clear();
            return buffer;
        }
    }

    size_t capacity = std::max(min_size, default_capacity_);
    return std::make_shared<Buffer>(capacity);
}

void MemoryPool::release(std::shared_ptr<MemoryPool::Buffer> buffer) {
    if (!buffer) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (pool_.size() < max_pool_size_) {
        buffer->clear();
        pool_.push_back(std::move(buffer));
    }
}

