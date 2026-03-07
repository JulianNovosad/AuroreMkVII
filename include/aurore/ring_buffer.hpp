/**
 * @file ring_buffer.hpp
 * @brief Lock-free Single-Producer Single-Consumer (SPSC) ring buffer
 * 
 * Designed for zero-copy frame transfer between control loop threads.
 * Uses atomic operations with proper memory ordering for lock-free operation.
 * 
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <stdexcept>
#include <optional>

namespace aurore {

/**
 * @brief Cache line size for alignment (prevents false sharing)
 * 
 * Most modern CPUs use 64-byte cache lines. Aligning atomic variables
 * to cache line boundaries prevents false sharing where unrelated
 * variables share the same cache line.
 */
constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * @brief Lock-free SPSC ring buffer
 * 
 * Thread-safe ring buffer optimized for single-producer single-consumer
 * scenarios. Uses atomic operations with proper memory ordering to
 * achieve lock-free operation.
 * 
 * @tparam T Element type (must be trivially copyable for best performance)
 * 
 * Usage:
 * @code
 *     LockFreeRingBuffer<FrameDescriptor, 4> buffer;
 *     
 *     // Producer thread
 *     FrameDescriptor frame{...};
 *     buffer.push(frame);  // Returns false if full
 *     
 *     // Consumer thread
 *     FrameDescriptor frame;
 *     if (buffer.pop(frame)) {
 *         // Process frame...
 *     }
 * @endcode
 * 
 * Memory layout:
 * - Head and tail are cache-line aligned to prevent false sharing
 * - Buffer size must be power of 2 for efficient modulo operation
 * - Elements are stored contiguously for cache efficiency
 */
template<typename T, size_t Size>
class LockFreeRingBuffer {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    static_assert(Size > 0, "Size must be greater than 0");
    static_assert(std::is_trivially_copyable_v<T>, 
                  "T must be trivially copyable for lock-free operation");

public:
    /**
     * @brief Construct ring buffer with default-initialized elements
     */
    LockFreeRingBuffer() noexcept
        : head_(0)
        , tail_(0)
        , mask_(Size - 1) {
        // Elements are default-initialized
    }
    
    /**
     * @brief Push element to buffer (producer operation)
     * 
     * @param item Item to push
     * @return true if successful, false if buffer is full
     * 
     * Thread-safety: Safe for single producer thread only
     * Memory ordering: Release semantics ensure visibility to consumer
     */
    bool push(const T& item) noexcept {
        const uint32_t current_head = head_.load(std::memory_order_relaxed);
        const uint32_t next_head = (current_head + 1) & mask_;
        
        // Check if buffer is full
        // Acquire fence ensures we see the most recent tail value
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false;  // Buffer full - caller should drop frame
        }
        
        // Write element to buffer
        buffer_[current_head] = item;
        
        // Release fence ensures write is visible before head update
        atomic_thread_fence(std::memory_order_release);
        head_.store(next_head, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief Push element to buffer with in-place construction
     * 
     * @tparam Args Constructor argument types
     * @param args Constructor arguments
     * @return true if successful, false if buffer is full
     * 
     * This avoids copying by constructing the element in-place.
     */
    template<typename... Args>
    bool emplace(Args&&... args) noexcept {
        const uint32_t current_head = head_.load(std::memory_order_relaxed);
        const uint32_t next_head = (current_head + 1) & mask_;
        
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Construct element in-place
        new (&buffer_[current_head]) T(std::forward<Args>(args)...);
        
        atomic_thread_fence(std::memory_order_release);
        head_.store(next_head, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief Pop element from buffer (consumer operation)
     * 
     * @param item Output parameter for popped item
     * @return true if successful, false if buffer is empty
     * 
     * Thread-safety: Safe for single consumer thread only
     * Memory ordering: Acquire semantics ensure visibility of producer writes
     */
    bool pop(T& item) noexcept {
        const uint32_t current_tail = tail_.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (current_tail == head_.load(std::memory_order_acquire)) {
            return false;  // Buffer empty
        }
        
        // Read element from buffer
        item = buffer_[current_tail];
        
        // Acquire fence ensures read happens before tail update
        atomic_thread_fence(std::memory_order_acquire);
        tail_.store((current_tail + 1) & mask_, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief Pop element from buffer (returns std::optional)
     * 
     * @return std::optional<T> Popped item, or std::nullopt if empty
     * 
     * More convenient than the output parameter version but may
     * involve a move operation.
     */
    std::optional<T> try_pop() noexcept {
        const uint32_t current_tail = tail_.load(std::memory_order_relaxed);
        
        if (current_tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        
        T item = std::move(buffer_[current_tail]);
        
        atomic_thread_fence(std::memory_order_acquire);
        tail_.store((current_tail + 1) & mask_, std::memory_order_release);
        
        return item;
    }
    
    /**
     * @brief Check if buffer is empty
     * 
     * @return true if empty
     * 
     * Note: Result may be stale immediately after this call.
     * Use as hint only - always check pop() return value.
     */
    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Check if buffer is full
     * 
     * @return true if full
     * 
     * Note: Result may be stale immediately after this call.
     */
    bool full() const noexcept {
        const uint32_t next_head = (head_.load(std::memory_order_acquire) + 1) & mask_;
        return next_head == tail_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get current size (approximate)
     * 
     * @return size_t Number of elements in buffer
     * 
     * Note: Size is approximate in concurrent scenarios.
     * Head and tail may be updated by other threads during calculation.
     */
    size_t size() const noexcept {
        const uint32_t head = head_.load(std::memory_order_acquire);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & mask_;
    }
    
    /**
     * @brief Get buffer capacity
     * 
     * @return constexpr size_t Maximum number of elements
     */
    static constexpr size_t capacity() noexcept {
        return Size;
    }
    
    /**
     * @brief Get maximum usable capacity (Size - 1)
     * 
     * One slot is always reserved to distinguish full from empty.
     * 
     * @return constexpr size_t Usable capacity
     */
    static constexpr size_t usable_capacity() noexcept {
        return Size - 1;
    }

private:
    // Align to cache line to prevent false sharing
    // Head is written by producer, read by consumer
    alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> head_;
    
    // Tail is written by consumer, read by producer
    alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> tail_;
    
    // Mask for efficient modulo (Size must be power of 2)
    const uint32_t mask_;
    
    // Buffer storage - aligned to cache line
    alignas(CACHE_LINE_SIZE) T buffer_[Size];
};

/**
 * @brief Multi-Producer Multi-Consumer ring buffer (mutex-protected)
 *
 * For scenarios with multiple producers or consumers. Uses separate mutexes
 * for producer and consumer operations to reduce contention.
 *
 * Security note: This implementation uses proper locking to prevent race
 * conditions. For high-performance MPMC scenarios, consider using a
 * specialized lock-free MPMC queue (e.g., boost::lockfree::mpmc_queue).
 *
 * @tparam T Element type
 * @tparam Size Buffer size (power of 2)
 */
template<typename T, size_t Size>
class MPMCRingBuffer {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");

public:
    MPMCRingBuffer() noexcept
        : head_(0)
        , tail_(0)
        , mask_(Size - 1) {}

    /**
     * @brief Push element (thread-safe for multiple producers)
     *
     * @param item Item to push
     * @return true if successful, false if buffer is full
     *
     * Thread-safety: Uses producer_mutex_ to serialize producer access.
     * Multiple consumers can pop concurrently.
     */
    bool push(const T& item) noexcept {
        std::lock_guard<std::mutex> lock(producer_mutex_);

        const uint32_t current_head = head_;
        const uint32_t next_head = (current_head + 1) & mask_;

        // Check if buffer is full
        // Note: We need to read tail_ atomically to avoid race with consumers
        const uint32_t current_tail = tail_.load(std::memory_order_acquire);
        if (next_head == current_tail) {
            return false;
        }

        buffer_[current_head] = item;

        // Memory barrier before updating head
        atomic_thread_fence(std::memory_order_release);
        head_.store(next_head, std::memory_order_release);

        return true;
    }

    /**
     * @brief Pop element (thread-safe for multiple consumers)
     *
     * @param item Output parameter
     * @return true if successful, false if buffer is empty
     *
     * Thread-safety: Uses consumer_mutex_ to serialize consumer access.
     * Multiple producers can push concurrently.
     */
    bool pop(T& item) noexcept {
        std::lock_guard<std::mutex> lock(consumer_mutex_);

        const uint32_t current_tail = tail_;
        const uint32_t current_head = head_.load(std::memory_order_acquire);

        // Check if buffer is empty
        if (current_tail == current_head) {
            return false;
        }

        // Memory barrier before reading
        atomic_thread_fence(std::memory_order_acquire);
        item = buffer_[current_tail];

        tail_.store((current_tail + 1) & mask_, std::memory_order_release);

        return true;
    }

    /**
     * @brief Check if buffer is empty (approximate)
     *
     * @return true if empty
     *
     * Note: Result may be stale immediately after this call.
     * Use as hint only - always check pop() return value.
     */
    bool empty() const noexcept {
        const uint32_t head = head_.load(std::memory_order_acquire);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        return head == tail;
    }

    /**
     * @brief Get current size (approximate)
     *
     * @return size_t Number of elements in buffer
     *
     * Note: Size is approximate in concurrent scenarios.
     */
    size_t size() const noexcept {
        const uint32_t head = head_.load(std::memory_order_acquire);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & mask_;
    }

    static constexpr size_t capacity() noexcept {
        return Size;
    }

private:
    // Separate mutexes for producers and consumers to reduce contention
    // Producer mutex protects head_, consumer mutex protects tail_
    alignas(CACHE_LINE_SIZE) mutable std::mutex producer_mutex_;
    alignas(CACHE_LINE_SIZE) mutable std::mutex consumer_mutex_;

    // Atomic head and tail for cross-thread visibility
    alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> tail_;

    const uint32_t mask_;

    // Buffer storage - aligned to cache line
    alignas(CACHE_LINE_SIZE) T buffer_[Size];
};

}  // namespace aurore
