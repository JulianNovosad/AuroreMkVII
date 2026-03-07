// Verified headers: [queue, mutex, condition_variable, atomic, chrono...]
// Verification timestamp: 2026-01-06 17:08:04
// REMEDIATION 2026-02-02: Using PriorityMutex with PTHREAD_PRIO_INHERIT for priority inheritance
#ifndef LOCKFREE_QUEUE_H
#define LOCKFREE_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include <stdexcept>
#include "priority_mutex.h"


namespace aurore {
namespace utils {

template<typename T, ::std::size_t Capacity = 1024>
class LockFreeQueue {
private:
    ::std::queue<T> queue_;
    mutable Aurore::PriorityMutex mutex_;
    ::std::condition_variable_any cv_;
    ::std::atomic<bool> destruction_started_{false};
    ::std::atomic<bool> valid_{true};
    
public:
    LockFreeQueue() : mutex_("LockFreeQueue") {}
    ~LockFreeQueue() {
        valid_.store(false, ::std::memory_order_release);
        destruction_started_.store(true, ::std::memory_order_release);
    }

    // Disable copy and move to prevent issues
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&) = delete;
    LockFreeQueue& operator=(LockFreeQueue&&) = delete;

    bool push(const T& data) {
        // Basic sanity check on this pointer
        if (reinterpret_cast<::std::uintptr_t>(this) < 0x1000) {
            valid_.store(false, ::std::memory_order_release);
            return false;
        }
        // Early exit if not valid - don't even try to lock mutex
        if (!valid_.load(::std::memory_order_acquire)) {
            valid_.store(false, ::std::memory_order_release);
            return false;
        }
        try {
            ::std::lock_guard<Aurore::PriorityMutex> lock(mutex_);
            // Re-check validity after acquiring lock
            if (!valid_.load(::std::memory_order_acquire)) {
                throw std::runtime_error("LockFreeQueue::push called on invalid queue (post-lock).");
            }
            if (queue_.size() >= Capacity) {
                // Log the overflow
                ::std::fprintf(stderr, "QUEUE_OVERFLOW_WARNING: Queue of type %s (capacity %zu) is full, push failed.\n", typeid(T).name(), Capacity);
                return false; // Queue is full
            }
            queue_.push(data);
            cv_.notify_one();
            return true;
        } catch (const ::std::exception& e) {
            valid_.store(false, ::std::memory_order_release);
            throw std::runtime_error("Exception in LockFreeQueue::push: " + ::std::string(e.what()));
        } catch (...) {
            valid_.store(false, ::std::memory_order_release);
            throw std::runtime_error("Unknown exception in LockFreeQueue::push.");
        }
    }

    bool pop(T& data) {
        if (!valid_.load(::std::memory_order_acquire)) {
            return false;
        }
        try {
            ::std::lock_guard<Aurore::PriorityMutex> lock(mutex_);
            if (!valid_.load(::std::memory_order_acquire)) {
                return false;
            }
            if (queue_.empty()) {
                return false;
            }
            data = queue_.front();
            queue_.pop();
            return true;
        } catch (const ::std::exception& e) {
            valid_.store(false, ::std::memory_order_release);
            throw std::runtime_error("Exception in LockFreeQueue::pop: " + ::std::string(e.what()));
        } catch (...) {
            valid_.store(false, ::std::memory_order_release);
            throw std::runtime_error("Unknown exception in LockFreeQueue::pop.");
        }
    }

    bool wait_pop(T& data, ::std::chrono::milliseconds timeout) {
        if (!valid_.load(::std::memory_order_acquire)) {
            return false;
        }
        try {
            Aurore::UniqueLock<Aurore::PriorityMutex> lock(mutex_);
            if (!valid_.load(::std::memory_order_acquire)) {
                return false;
            }
            if (cv_.wait_for(lock, timeout, [this] { return !queue_.empty() || !valid_.load(::std::memory_order_acquire); })) {
                if (!valid_.load(::std::memory_order_acquire)) {
                    return false;
                }
                data = queue_.front();
                queue_.pop();
                return true;
            }
            return false;
        } catch (const ::std::exception& e) {
            valid_.store(false, ::std::memory_order_release);
            throw std::runtime_error("Exception in LockFreeQueue::wait_pop: " + ::std::string(e.what()));
        } catch (...) {
            valid_.store(false, ::std::memory_order_release);
            throw std::runtime_error("Unknown exception in LockFreeQueue::wait_pop.");
        }
    }

    ::std::size_t write_available() const {
        if (!valid_.load(::std::memory_order_acquire)) {
            return 0;
        }
        try {
            ::std::lock_guard<Aurore::PriorityMutex> lock(mutex_);
            if (!valid_.load(::std::memory_order_acquire)) {
                return 0;
            }
            return Capacity - queue_.size();
        } catch (const ::std::exception& e) {
            throw std::runtime_error("Exception in LockFreeQueue::write_available: " + ::std::string(e.what()));
        } catch (...) {
            throw std::runtime_error("Unknown exception in LockFreeQueue::write_available.");
        }
    }

    bool empty() const {
        if (!valid_.load(::std::memory_order_acquire)) {
            return true;
        }
        try {
            ::std::lock_guard<Aurore::PriorityMutex> lock(mutex_);
            return queue_.empty();
        } catch (const ::std::exception& e) {
            throw std::runtime_error("Exception in LockFreeQueue::empty: " + ::std::string(e.what()));
        } catch (...) {
            throw std::runtime_error("Unknown exception in LockFreeQueue::empty.");
        }
    }

    bool full() const {
        if (!valid_.load(::std::memory_order_acquire)) {
            return true;
        }
        try {
            ::std::lock_guard<Aurore::PriorityMutex> lock(mutex_);
            return queue_.size() >= Capacity;
        } catch (const ::std::exception& e) {
            throw std::runtime_error("Exception in LockFreeQueue::full: " + ::std::string(e.what()));
        } catch (...) {
            throw std::runtime_error("Unknown exception in LockFreeQueue::full.");
        }
    }

    ::std::size_t size_approx() const {
        if (!valid_.load(::std::memory_order_acquire)) {
            return 0;
        }
        try {
            ::std::lock_guard<Aurore::PriorityMutex> lock(mutex_);
            return queue_.size();
        } catch (const ::std::exception& e) {
            throw std::runtime_error("Exception in LockFreeQueue::size_approx: " + ::std::string(e.what()));
        } catch (...) {
            throw std::runtime_error("Unknown exception in LockFreeQueue::size_approx.");
        }
    }

    static constexpr ::std::size_t capacity() {
        return Capacity;
    }

    bool try_pop(T& data) {
        return pop(data);
    }

    void clear(::std::function<void(T&)> callback = nullptr) {
        if (!valid_.load(::std::memory_order_acquire)) {
            return;
        }
        try {
            ::std::lock_guard<Aurore::PriorityMutex> lock(mutex_);
            if (!valid_.load(::std::memory_order_acquire)) {
                return;
            }
            while (!queue_.empty()) {
                T data = queue_.front();
                queue_.pop();
                if (callback) callback(data);
            }
        } catch (const ::std::exception& e) {
            valid_.store(false, ::std::memory_order_release);
            throw std::runtime_error("Exception in LockFreeQueue::clear: " + ::std::string(e.what()));
        } catch (...) {
            valid_.store(false, ::std::memory_order_release);
            throw std::runtime_error("Unknown exception in LockFreeQueue::clear.");
        }
    }
    
    bool is_valid() const {
        return valid_.load(::std::memory_order_acquire);
    }
    
    void invalidate() {
        valid_.store(false, ::std::memory_order_release);
        destruction_started_.store(true, ::std::memory_order_release);
    }
};

} // namespace utils
} // namespace aurore

#endif // LOCKFREE_QUEUE_H
