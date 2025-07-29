#pragma once

#include <vector>

class MemoryPool {
public:
    struct Buffer {
        std::vector<uint8_t> data;
        size_t size = 0;

        explicit Buffer(size_t capacity = 0) : data(capacity) {}
        void clear() { size = 0; }
    };

    explicit MemoryPool(size_t pool_size = 50, size_t buffer_capacity = 1024);
    ~MemoryPool() = default;

    std::shared_ptr<Buffer> acquire(size_t min_size);
    void release(std::shared_ptr<Buffer> buffer);

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<Buffer>> pool_;
    size_t max_pool_size_;
    size_t default_capacity_;
};
