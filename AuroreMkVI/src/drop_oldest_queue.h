#ifndef DROP_OLDEST_QUEUE_H
#define DROP_OLDEST_QUEUE_H

#include <queue>
#include <mutex>
#include <atomic>
#include <cstddef>

namespace aurore {
namespace utils {

template<typename T>
class DropOldestQueue {
private:
    static constexpr std::size_t CAPACITY = 4;
    static constexpr std::size_t DROP_THRESHOLD = 3;

    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::atomic<bool> valid_{true};

public:
    DropOldestQueue() = default;
    ~DropOldestQueue() {
        valid_.store(false, std::memory_order_release);
    }

    DropOldestQueue(const DropOldestQueue&) = delete;
    DropOldestQueue& operator=(const DropOldestQueue&) = delete;
    DropOldestQueue(DropOldestQueue&&) = delete;
    DropOldestQueue& operator=(DropOldestQueue&&) = delete;

    bool push(T item) {
        if (!valid_.load(std::memory_order_acquire)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (!valid_.load(std::memory_order_acquire)) {
            return false;
        }

        if (queue_.size() >= DROP_THRESHOLD) {
            queue_.pop();
        }

        if (queue_.size() >= CAPACITY) {
            queue_.pop();
        }

        queue_.push(std::move(item));
        return true;
    }

    bool pop(T& item) {
        if (!valid_.load(std::memory_order_acquire)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (!valid_.load(std::memory_order_acquire)) {
            return false;
        }

        if (queue_.empty()) {
            return false;
        }

        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    std::size_t capacity() const {
        return CAPACITY;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            queue_.pop();
        }
    }

    void invalidate() {
        valid_.store(false, std::memory_order_release);
    }

    bool is_valid() const {
        return valid_.load(std::memory_order_acquire);
    }
};

}
}

#endif // DROP_OLDEST_QUEUE_H
