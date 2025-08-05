#pragma once

#include "Platform.h"

template <typename T, sz_t CAPACITY>
struct CircularDeque {
    T buffer[CAPACITY];
    sz_t head;
    sz_t tail;
    sz_t size;
};

template <typename T, sz_t CAPACITY>
bool Empty(const CircularDeque<T, CAPACITY>& deque) {
    return deque.size == 0; 
}

template <typename T, sz_t CAPACITY>
bool Full(const CircularDeque<T, CAPACITY>& deque) {
    return deque.size == CAPACITY;
}

template <typename T, sz_t CAPACITY>
void Reset(CircularDeque<T, CAPACITY>& deque) {
    Reset(deque.buffer, CAPACITY * sizeof(T));
    deque.head = 0;
    deque.tail = 0;
    deque.size = 0;
}

template <typename T, sz_t CAPACITY>
void PushBack(CircularDeque<T, CAPACITY>& deque, const T& value) {
    if (Full(deque)) {
        // Overwrite oldest
        deque.head = (deque.head + 1) % CAPACITY;
        --deque.size;
    }
    deque.buffer[deque.tail] = value;
    deque.tail = (deque.tail + 1) % CAPACITY;
    ++deque.size;
}

template <typename T, sz_t CAPACITY>
void PushFront(CircularDeque<T, CAPACITY>& deque, const T& value) {
    if (Full(deque)) {
        // Overwrite newest
        deque.tail = (deque.tail + CAPACITY - 1) % CAPACITY;
        --deque.size;
    }
    deque.head = (deque.head + CAPACITY - 1) % CAPACITY;
    deque.buffer[deque.head] = value;
    ++deque.size;
}

template <typename T, sz_t CAPACITY>
bool PopFront(CircularDeque<T, CAPACITY>& deque, T& out) {
    if (Empty(deque)) return false;
    out = deque.buffer[deque.head];
    deque.head = (deque.head + 1) % CAPACITY;
    --deque.size;
    return true;
}

template <typename T, sz_t CAPACITY>
bool PopBack(CircularDeque<T, CAPACITY>& deque, T& out) {
    if (Empty(deque)) return false;
    deque.tail = (deque.tail + CAPACITY - 1) % CAPACITY;
    out = deque.buffer[deque.tail];
    --deque.size;
    return true;
}

template <typename T, sz_t CAPACITY>
void PushBack(CircularDeque<T, CAPACITY>& deque, const T* src, sz_t count) {
    sz_t firstChunk;
    sz_t remaining;

    if (count <= 0)
        return;

    if (count > CAPACITY) {
        // Keep only last CAPACITY items
        PushBack(deque, src + count - CAPACITY, CAPACITY);
        return;
    }

    if (deque.size + count > CAPACITY) {
        sz_t overflow = deque.size + count - CAPACITY;
        deque.head = (deque.head + overflow) % CAPACITY;
        deque.size -= overflow;
    }

    firstChunk = count > CAPACITY - deque.tail ?
            CAPACITY - deque.tail :
                      count;
    Copy(&deque.buffer[deque.tail], src, firstChunk * sizeof(T));
    deque.tail = (deque.tail + firstChunk) % CAPACITY;

    remaining = count - firstChunk;
    if (remaining > 0) {
        Copy(&deque.buffer[0], src + firstChunk, remaining * sizeof(T));
        deque.tail = remaining;
    }

    deque.size += count;
}

template <typename T, sz_t CAPACITY>
sz_t PopFront(CircularDeque<T, CAPACITY>& deque, T* dst, sz_t count) {
    sz_t actual;
    sz_t firstChunk;
    sz_t remaining;

    if (count <= 0)
        return 0;

    actual = count > deque.size ?
            deque.size :
            count;
    firstChunk = actual > CAPACITY - deque.head ?
            CAPACITY - deque.head :
            actual;

    Copy(dst, &deque.buffer[deque.head], firstChunk * sizeof(T));
    deque.head = (deque.head + firstChunk) % CAPACITY;

    remaining = actual - firstChunk;
    if (remaining > 0) {
        Copy(dst + firstChunk, &deque.buffer[0], remaining * sizeof(T));
        deque.head = remaining;
    }

    deque.size -= actual;
    return actual;
}
