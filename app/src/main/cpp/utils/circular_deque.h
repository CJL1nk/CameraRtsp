#pragma once

#include <jni.h>
#include <array>

template <typename T, size_t Capacity>
class CircularDeque {
public:
    CircularDeque() : head_(0), tail_(0), size_(0) {}

    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == Capacity; }
    size_t size() const { return size_; }
    size_t capacity() const { return Capacity; }

    void clear() {
        head_ = tail_ = size_ = 0;
    }

    void push_back(const T& value) {
        if (full()) {
            // Overwrite oldest (advance head)
            head_ = (head_ + 1) % Capacity;
            --size_;
        }
        buffer_[tail_] = value;
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
    }

    void push_front(const T& value) {
        if (full()) {
            // Overwrite newest (move tail backward)
            tail_ = (tail_ + Capacity - 1) % Capacity;
            --size_;
        }
        head_ = (head_ + Capacity - 1) % Capacity;
        buffer_[head_] = value;
        ++size_;
    }

    bool pop_front(T& out) {
        if (empty()) return false;
        out = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        --size_;
        return true;
    }

    bool pop_back(T& out) {
        if (empty()) return false;
        tail_ = (tail_ + Capacity - 1) % Capacity;
        out = buffer_[tail_];
        --size_;
        return true;
    }

private:
    std::array<T, Capacity> buffer_;
    size_t head_;  // points to oldest element
    size_t tail_;  // points to next write position at back
    size_t size_;
};
