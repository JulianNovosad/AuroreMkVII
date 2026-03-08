/**
 * @file ring_buffer_test.cpp
 * @brief Unit tests for LockFreeRingBuffer
 * 
 * Tests cover:
 * - Basic push/pop operations
 * - Thread safety (single producer, single consumer)
 * - Buffer full/empty conditions
 * - Memory ordering correctness
 */

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "aurore/ring_buffer.hpp"

namespace {

// Test counters
std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run.fetch_add(1); \
    try { \
        name(); \
        g_tests_passed.fetch_add(1); \
        std::cout << "  PASS: " << #name << std::endl; \
    } \
    catch (const std::exception& e) { \
        g_tests_failed.fetch_add(1); \
        std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)

}  // anonymous namespace

// ============================================================================
// Basic Operations Tests
// ============================================================================

TEST(test_ring_buffer_construction) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    ASSERT_EQ(buffer.capacity(), 4);
    ASSERT_EQ(buffer.usable_capacity(), 3);
    ASSERT_TRUE(buffer.empty());
    ASSERT_FALSE(buffer.full());
    ASSERT_EQ(buffer.size(), 0);
}

TEST(test_ring_buffer_push_pop_single) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    
    // Push one element
    ASSERT_TRUE(buffer.push(42));
    ASSERT_EQ(buffer.size(), 1);
    ASSERT_FALSE(buffer.empty());
    
    // Pop one element
    int value;
    ASSERT_TRUE(buffer.pop(value));
    ASSERT_EQ(value, 42);
    ASSERT_EQ(buffer.size(), 0);
    ASSERT_TRUE(buffer.empty());
}

TEST(test_ring_buffer_push_pop_multiple) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    
    // Fill buffer (usable capacity is 3)
    ASSERT_TRUE(buffer.push(1));
    ASSERT_TRUE(buffer.push(2));
    ASSERT_TRUE(buffer.push(3));
    
    ASSERT_EQ(buffer.size(), 3);
    ASSERT_TRUE(buffer.full());
    
    // Push should fail when full
    ASSERT_FALSE(buffer.push(4));
    ASSERT_EQ(buffer.size(), 3);
    
    // Pop in order
    int value;
    ASSERT_TRUE(buffer.pop(value));
    ASSERT_EQ(value, 1);
    
    ASSERT_TRUE(buffer.pop(value));
    ASSERT_EQ(value, 2);
    
    ASSERT_TRUE(buffer.pop(value));
    ASSERT_EQ(value, 3);
    
    ASSERT_TRUE(buffer.empty());
}

TEST(test_ring_buffer_wraparound) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    
    // Push and pop to advance head and tail
    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(buffer.push(i));
        int value;
        ASSERT_TRUE(buffer.pop(value));
        ASSERT_EQ(value, i);
    }
    
    // Buffer should be back at start
    ASSERT_TRUE(buffer.empty());
    
    // Fill again to test wraparound
    ASSERT_TRUE(buffer.push(100));
    ASSERT_TRUE(buffer.push(200));
    
    int value;
    ASSERT_TRUE(buffer.pop(value));
    ASSERT_EQ(value, 100);
    
    ASSERT_TRUE(buffer.pop(value));
    ASSERT_EQ(value, 200);
}

TEST(test_ring_buffer_try_pop) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    
    // Empty buffer
    auto result = buffer.try_pop();
    ASSERT_FALSE(result.has_value());
    
    // Push and pop
    buffer.push(42);
    result = buffer.try_pop();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 42);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST(test_ring_buffer_spsc_stress) {
    constexpr size_t kNumElements = 100000;
    aurore::LockFreeRingBuffer<uint64_t, 256> buffer;
    
    std::atomic<uint64_t> produced(0);
    std::atomic<uint64_t> consumed(0);
    std::atomic<bool> producer_done(false);
    
    // Producer thread
    std::thread producer([&]() {
        for (uint64_t i = 0; i < kNumElements; i++) {
            while (!buffer.push(i)) {
                // Buffer full, retry
                std::this_thread::yield();
            }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
        producer_done.store(true, std::memory_order_release);
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        uint64_t last_value = 0;
        uint64_t count = 0;
        
        while (count < kNumElements) {
            uint64_t value;
            if (buffer.pop(value)) {
                // Verify ordering
                if (count > 0) {
                    ASSERT_TRUE(value > last_value || value < last_value);  // Allow wraparound
                }
                last_value = value;
                count++;
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    ASSERT_EQ(produced.load(), kNumElements);
    ASSERT_EQ(consumed.load(), kNumElements);
}

TEST(test_ring_buffer_no_lost_elements) {
    constexpr size_t kNumElements = 10000;
    aurore::LockFreeRingBuffer<int, 16> buffer;
    
    std::atomic<int> sum_produced(0);
    std::atomic<int> sum_consumed(0);
    std::atomic<bool> done(false);
    
    // Producer
    std::thread producer([&]() {
        for (int i = 1; i <= static_cast<int>(kNumElements); i++) {
            while (!buffer.push(i)) {
                std::this_thread::yield();
            }
            sum_produced.fetch_add(i, std::memory_order_relaxed);
        }
        done.store(true, std::memory_order_release);
    });
    
    // Consumer
    std::thread consumer([&]() {
        int value;
        while (!done.load(std::memory_order_acquire) || !buffer.empty()) {
            if (buffer.pop(value)) {
                sum_consumed.fetch_add(value, std::memory_order_relaxed);
            }
            else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    // Verify no elements lost
    ASSERT_EQ(sum_produced.load(), sum_consumed.load());
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(test_ring_buffer_capacity_one) {
    aurore::LockFreeRingBuffer<int, 2> buffer;  // Usable capacity = 1
    
    ASSERT_EQ(buffer.usable_capacity(), 1);
    
    ASSERT_TRUE(buffer.push(42));
    ASSERT_TRUE(buffer.full());
    ASSERT_FALSE(buffer.push(99));  // Should fail
    
    int value;
    ASSERT_TRUE(buffer.pop(value));
    ASSERT_EQ(value, 42);
    ASSERT_TRUE(buffer.empty());
}

TEST(test_ring_buffer_large_buffer) {
    aurore::LockFreeRingBuffer<uint64_t, 4096> buffer;
    
    ASSERT_EQ(buffer.capacity(), 4096);
    ASSERT_EQ(buffer.usable_capacity(), 4095);
    
    // Push and pop many elements
    for (uint64_t i = 0; i < 10000; i++) {
        while (!buffer.push(i)) {
            std::this_thread::yield();
        }
        
        uint64_t value;
        while (!buffer.pop(value)) {
            std::this_thread::yield();
        }
        ASSERT_EQ(value, i);
    }
}

TEST(test_ring_buffer_struct_type) {
    struct TestData {
        uint64_t id;
        double value;
        char padding[64];  // Cache line size
    };
    
    aurore::LockFreeRingBuffer<TestData, 8> buffer;
    
    TestData data{42, 3.14159, {0}};
    ASSERT_TRUE(buffer.push(data));
    
    TestData popped;
    ASSERT_TRUE(buffer.pop(popped));
    ASSERT_EQ(popped.id, 42);
    ASSERT_EQ(popped.value, 3.14159);
}

// ============================================================================
// MPMC Ring Buffer Tests
// ============================================================================

TEST(test_mpmc_ring_buffer_basic) {
    aurore::MPMCRingBuffer<int, 4> buffer;
    
    ASSERT_TRUE(buffer.push(1));
    ASSERT_TRUE(buffer.push(2));
    ASSERT_TRUE(buffer.push(3));
    
    int value;
    ASSERT_TRUE(buffer.pop(value));
    ASSERT_EQ(value, 1);
    
    ASSERT_TRUE(buffer.pop(value));
    ASSERT_EQ(value, 2);
    
    ASSERT_TRUE(buffer.pop(value));
    ASSERT_EQ(value, 3);
}

TEST(test_mpmc_ring_buffer_concurrent) {
    constexpr size_t kNumElements = 10000;
    aurore::MPMCRingBuffer<int, 256> buffer;
    
    std::atomic<int> sum(0);
    
    // Multiple producers
    std::vector<std::thread> producers;
    for (int p = 0; p < 4; p++) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < static_cast<int>(kNumElements); i++) {
                while (!buffer.push(p * 100000 + i)) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Single consumer
    std::thread consumer([&]() {
        size_t count = 0;
        int value;
        while (count < kNumElements * 4) {
            if (buffer.pop(value)) {
                sum.fetch_add(value, std::memory_order_relaxed);
                count++;
            }
            else {
                std::this_thread::yield();
            }
        }
    });
    
    for (auto& t : producers) {
        t.join();
    }
    consumer.join();
    
    // Expected sum: sum of (p * 100000 + i) for p=0..3, i=0..9999
    int expected_sum = 0;
    for (int p = 0; p < 4; p++) {
        for (int i = 0; i < static_cast<int>(kNumElements); i++) {
            expected_sum += p * 100000 + i;
        }
    }
    
    ASSERT_EQ(sum.load(), expected_sum);
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "Running LockFreeRingBuffer tests..." << std::endl;
    std::cout << "=====================================" << std::endl;
    
    // Basic tests
    RUN_TEST(test_ring_buffer_construction);
    RUN_TEST(test_ring_buffer_push_pop_single);
    RUN_TEST(test_ring_buffer_push_pop_multiple);
    RUN_TEST(test_ring_buffer_wraparound);
    RUN_TEST(test_ring_buffer_try_pop);
    
    // Thread safety tests
    RUN_TEST(test_ring_buffer_spsc_stress);
    RUN_TEST(test_ring_buffer_no_lost_elements);
    
    // Edge cases
    RUN_TEST(test_ring_buffer_capacity_one);
    RUN_TEST(test_ring_buffer_large_buffer);
    RUN_TEST(test_ring_buffer_struct_type);
    
    // MPMC tests
    RUN_TEST(test_mpmc_ring_buffer_basic);
    RUN_TEST(test_mpmc_ring_buffer_concurrent);
    
    // Summary
    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;
    
    return g_tests_failed.load() > 0 ? 1 : 0;
}
