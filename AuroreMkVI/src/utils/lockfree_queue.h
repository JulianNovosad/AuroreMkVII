#ifndef LOCKFREE_QUEUE_H
#define LOCKFREE_QUEUE_H

#include <atomic>
#include <memory>
#include <thread>
#include <chrono>

namespace aurore {
namespace utils {

/**
 * @brief A truly lock-free, single-producer, single-consumer queue for high-performance applications.
 * 
 * This implementation uses atomic operations exclusively and does not rely on mutexes,
 * making it suitable for real-time systems where blocking is unacceptable.
 * 
 * @tparam T The type of elements stored in the queue
 * @tparam Capacity The maximum number of elements in the queue (must be a power of 2)
 */
template<typename T, size_t Capacity = 1024>
class LockFreeSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

private:
    // Align to cache line boundaries to avoid false sharing
    alignas(64) std::atomic<size_t> write_index_{0};
    alignas(64) std::atomic<size_t> read_index_{0};
    
    T buffer_[Capacity];
    static constexpr size_t mask_ = Capacity - 1;

public:
    /**
     * @brief Try to push an element to the queue without blocking
     * @param item The element to push
     * @return true if successful, false if queue is full
     */
    bool try_push(const T& item) {
        const size_t current_write = write_index_.load(std::memory_order_relaxed);
        const size_t next_write = (current_write + 1) & mask_;
        const size_t current_read = read_index_.load(std::memory_order_acquire);
        
        // Check if queue is full
        if (next_write == current_read) {
            return false;
        }
        
        // Write the item
        buffer_[current_write] = item;
        
        // Publish the write index
        write_index_.store(next_write, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Try to pop an element from the queue without blocking
     * @param[out] item Reference to store the popped element
     * @return true if successful, false if queue is empty
     */
    bool try_pop(T& item) {
        const size_t current_read = read_index_.load(std::memory_order_relaxed);
        const size_t current_write = write_index_.load(std::memory_order_acquire);
        
        // Check if queue is empty
        if (current_read == current_write) {
            return false;
        }
        
        // Read the item
        item = buffer_[current_read];
        
        // Publish the read index
        read_index_.store((current_read + 1) & mask_, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Check if the queue is empty
     * @return true if empty, false otherwise
     */
    bool empty() const {
        const size_t current_read = read_index_.load(std::memory_order_acquire);
        const size_t current_write = write_index_.load(std::memory_order_acquire);
        return current_read == current_write;
    }
    
    /**
     * @brief Check if the queue is full
     * @return true if full, false otherwise
     */
    bool full() const {
        const size_t current_write = write_index_.load(std::memory_order_acquire);
        const size_t next_write = (current_write + 1) & mask_;
        const size_t current_read = read_index_.load(std::memory_order_acquire);
        return next_write == current_read;
    }
    
    /**
     * @brief Get the current number of elements in the queue
     * @return Number of elements in the queue
     */
    size_t size() const {
        const size_t current_write = write_index_.load(std::memory_order_acquire);
        const size_t current_read = read_index_.load(std::memory_order_acquire);
        return (current_write - current_read) & mask_;
    }
    
    /**
     * @brief Get the maximum capacity of the queue
     * @return Maximum number of elements the queue can hold
     */
    size_t capacity() const {
        return Capacity - 1; // Leave one slot empty to distinguish full/empty
    }
};

} // namespace utils
} // namespace aurore

#endif // LOCKFREE_QUEUE_H