#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

namespace injector::logging {

/// Lock-free single-producer, single-consumer ring buffer.
/// Overwrites oldest entries when full (suitable for telemetry logging).
template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : capacity_(capacity), buffer_(capacity) {}

    /// Push an item. Overwrites the oldest entry if full.
    /// Must be called from the producer thread only.
    void push(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        buffer_[head % capacity_] = item;
        head_.store(head + 1, std::memory_order_release);
    }

    /// Try to pop the oldest unread item.
    /// Must be called from the consumer thread only.
    [[nodiscard]] std::optional<T> pop() {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);

        if (tail >= head) {
            return std::nullopt;
        }

        // If producer has lapped us, skip to the earliest available entry
        if (head - tail > capacity_) {
            tail_.store(head - capacity_, std::memory_order_relaxed);
            return pop();
        }

        T item = buffer_[tail % capacity_];
        tail_.store(tail + 1, std::memory_order_release);
        return item;
    }

    /// Number of unread items available to the consumer.
    [[nodiscard]] size_t available() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);

        if (head <= tail) {
            return 0;
        }

        const size_t diff = head - tail;
        return diff > capacity_ ? capacity_ : diff;
    }

    /// Total number of items pushed (may exceed capacity).
    [[nodiscard]] size_t totalPushed() const {
        return head_.load(std::memory_order_acquire);
    }

    [[nodiscard]] size_t capacity() const { return capacity_; }

    /// Snapshot all stored entries into a vector (oldest first).
    /// Intended for export — reads all entries without consuming them.
    [[nodiscard]] std::vector<T> snapshot() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t count = (head < capacity_) ? head : capacity_;
        const size_t start = (head <= capacity_) ? 0 : head - capacity_;

        std::vector<T> result;
        result.reserve(count);
        for (size_t i = start; i < head; ++i) {
            result.push_back(buffer_[i % capacity_]);
        }
        return result;
    }

private:
    size_t capacity_;
    std::vector<T> buffer_;
    std::atomic<size_t> head_{0};  // next write position (producer)
    std::atomic<size_t> tail_{0};  // next read position (consumer)
};

}  // namespace injector::logging
