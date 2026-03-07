#include "buffer_pool.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cassert>
#include <vector>
#include <memory>
#include <cstddef> // For size_t
#include <ostream> // For std::endl and std::flush

namespace {

constexpr size_t POOL_SIZE = 4;
constexpr size_t BUFFER_SIZE = 1024;

bool test_basic_allocation() {
    std::cout << "Test: Basic allocation and release... " << std::flush;

    BufferPool<uint8_t> pool(POOL_SIZE, BUFFER_SIZE, "test_pool");

    auto buf = pool.acquire();
    if (!buf) {
        std::cerr << "FAILED: Could not acquire buffer" << std::endl;
        return false;
    }

            if (buf->data.size() != BUFFER_SIZE) {
                std::cerr << "FAILED: Buffer size mismatch" << std::endl;
                return false;
            }
    if (pool.get_available_buffers() != POOL_SIZE - 1) { // One buffer is in use
        std::cerr << "FAILED: Buffer not acquired (pool size incorrect)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_fd_tracking() {
    std::cout << "Test: DMA-BUF FD tracking... " << std::flush;

    BufferPool<uint8_t> pool(POOL_SIZE, BUFFER_SIZE, "test_pool");

    auto buf = pool.acquire_with_fd(42, 4096, 2048);
    if (!buf) {
        std::cerr << "FAILED: Could not acquire buffer" << std::endl;
        return false;
    }

            if (buf->fd != 42 || buf->offset != 4096 || buf->length != 2048) {
                std::cerr << "FAILED: DMA-BUF fields not set correctly" << std::endl;
                return false;
            }
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_concurrent_acquire_release() {
    std::cout << "Test: Concurrent acquire/release... " << std::flush;

    BufferPool<uint8_t> pool(POOL_SIZE, BUFFER_SIZE, "test_pool");

    std::atomic<int> success_count{0};
    std::atomic<bool> start{false};

    auto producer = [&]() {
        for (int i = 0; i < 10; ++i) {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            auto buf = pool.acquire();
            if (buf) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                buf->fd = i;

                success_count.fetch_add(1, std::memory_order_release);
            }
        }
    };

    std::thread t1(producer);
    std::thread t2(producer);
    std::thread t3(producer);
    std::thread t4(producer);

    start.store(true, std::memory_order_release);

    t1.join();
    t2.join();
    t3.join();
    t4.join();

    if (success_count.load() != 40) {
        std::cerr << "FAILED: Expected 40 operations, got " << success_count.load() << std::endl;
        return false;
    }

    if (pool.get_available_buffers() != POOL_SIZE) {
        std::cerr << "FAILED: Pool not restored to full capacity" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_slot_invariants() {
    std::cout << "Test: Slot count invariants... " << std::flush;

    BufferPool<uint8_t> pool(POOL_SIZE, BUFFER_SIZE, "test_pool");

    std::vector<std::shared_ptr<PooledBuffer<uint8_t>>> acquired;

    for (size_t i = 0; i < POOL_SIZE; ++i) {
        auto buf = pool.acquire();
        if (!buf) {
            std::cerr << "FAILED: Could not acquire buffer " << i << std::endl;
            return false;
        }
        buf->fd = static_cast<int>(i);
        acquired.push_back(buf);
    }

    if (pool.get_available_buffers() != 0) {
        std::cerr << "FAILED: Pool should be empty" << std::endl;
        return false;
    }

    // Test that acquire returns nullptr when pool is empty (non-blocking behavior via timeout)
    auto buf = pool.acquire();
    if (buf != nullptr) {
        std::cerr << "FAILED: Acquire should return nullptr on full pool" << std::endl;
        return false;
    }

            // For testing, just clear the vector to release shared_ptrs and return buffers to pool
            acquired.clear();
    if (pool.get_available_buffers() != POOL_SIZE) {
        std::cerr << "FAILED: Pool should be full after releasing all" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_reference_counting() {
    std::cout << "Test: Reference counting safety... " << std::flush;

    BufferPool<uint8_t> pool(POOL_SIZE, BUFFER_SIZE, "test_pool");

    auto buf1 = pool.acquire();
    auto buf2 = buf1;
    // pool.release(buf1); // Removed: Handled by shared_ptr custom deleter

    if (pool.get_available_buffers() != POOL_SIZE - 1) {
        std::cerr << "FAILED: Buffer should still be in use (shared_ptr refcount > 0)" << std::endl;
        return false;
    }

    buf2.reset();

    if (pool.get_available_buffers() != POOL_SIZE - 1) { // buf1 is still holding a reference
        std::cerr << "FAILED: Incorrect pool size after buf2.reset()" << std::endl;
        return false;
    }
    buf1.reset(); // Explicitly release the last reference
    if (pool.get_available_buffers() != POOL_SIZE) {
        std::cerr << "FAILED: Buffer not returned after all refs released" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_zero_copy_semantics() {
    std::cout << "Test: Zero-copy semantics... " << std::flush;

    constexpr size_t ZC_POOL_SIZE = 4; // Using a different name to avoid redefinition
    constexpr size_t ZC_BUFFER_SIZE = 1920 * 1080 * 3;

    BufferPool<uint8_t> pool(ZC_POOL_SIZE, ZC_BUFFER_SIZE, "test_pool");

    auto buf = pool.acquire_with_fd(10, 0, ZC_BUFFER_SIZE);
    if (!buf) {
        std::cerr << "FAILED: Could not acquire buffer" << std::endl;
        return false;
    }

    if (buf->fd < 0 || buf->length == 0) {
        std::cerr << "FAILED: Invalid DMA-BUF handle" << std::endl;
        return false;
    }

            // pool.release(buf); // Removed: Handled by shared_ptr custom deleter
    auto buf2 = pool.acquire_with_fd(20, 8192, ZC_BUFFER_SIZE);
    if (!buf2) {
        std::cerr << "FAILED: Could not reuse pool slot" << std::endl;
        return false;
    }

    if (buf2->fd != 20) {
        std::cerr << "FAILED: Pool slot not properly reused" << std::endl;
        return false;
    }

            // pool.release(buf2); // Removed: Handled by shared_ptr custom deleter
    std::cout << "PASSED" << std::endl;
    return true;
}

} // namespace for test functions

int main() {
    std::cout << "=== BufferPool Tests ===" << std::endl;

    int passed = 0;
    int failed = 0;

    if (test_basic_allocation()) passed++; else failed++;
    if (test_fd_tracking()) passed++; else failed++;
    if (test_concurrent_acquire_release()) passed++; else failed++;
    if (test_slot_invariants()) passed++; else failed++;
    if (test_reference_counting()) passed++; else failed++;
    if (test_zero_copy_semantics()) passed++; else failed++;

    std::cout << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;

    return failed > 0 ? 1 : 0;
}
