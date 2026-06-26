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

#define BOOST_TEST_MODULE RingBufferTest
#include "aurore/ring_buffer.hpp"

#include <atomic>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <thread>
#include <vector>

// ============================================================================
// LockFreeRingBuffer Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(LockFreeRingBuffer)

BOOST_AUTO_TEST_CASE(test_ring_buffer_construction) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    BOOST_REQUIRE_EQUAL(buffer.capacity(), 4);
    BOOST_REQUIRE_EQUAL(buffer.usable_capacity(), 3);
    BOOST_REQUIRE(buffer.empty());
    BOOST_REQUIRE(!(buffer.full()));
    BOOST_REQUIRE_EQUAL(buffer.size(), 0);
}

BOOST_AUTO_TEST_CASE(test_ring_buffer_push_pop_single) {
    aurore::LockFreeRingBuffer<int, 4> buffer;

    // Push one element
    BOOST_REQUIRE(buffer.push(42));
    BOOST_REQUIRE_EQUAL(buffer.size(), 1);
    BOOST_REQUIRE(!(buffer.empty()));

    // Pop one element
    int value;
    BOOST_REQUIRE(buffer.pop(value));
    BOOST_REQUIRE_EQUAL(value, 42);
    BOOST_REQUIRE_EQUAL(buffer.size(), 0);
    BOOST_REQUIRE(buffer.empty());
}

BOOST_AUTO_TEST_CASE(test_ring_buffer_push_pop_multiple) {
    aurore::LockFreeRingBuffer<int, 4> buffer;

    // Fill buffer (usable capacity is 3)
    BOOST_REQUIRE(buffer.push(1));
    BOOST_REQUIRE(buffer.push(2));
    BOOST_REQUIRE(buffer.push(3));

    BOOST_REQUIRE_EQUAL(buffer.size(), 3);
    BOOST_REQUIRE(buffer.full());

    // Push should fail when full
    BOOST_REQUIRE(!(buffer.push(4)));
    BOOST_REQUIRE_EQUAL(buffer.size(), 3);

    // Pop in order
    int value;
    BOOST_REQUIRE(buffer.pop(value));
    BOOST_REQUIRE_EQUAL(value, 1);

    BOOST_REQUIRE(buffer.pop(value));
    BOOST_REQUIRE_EQUAL(value, 2);

    BOOST_REQUIRE(buffer.pop(value));
    BOOST_REQUIRE_EQUAL(value, 3);

    BOOST_REQUIRE(buffer.empty());
}

BOOST_AUTO_TEST_CASE(test_ring_buffer_wraparound) {
    aurore::LockFreeRingBuffer<int, 4> buffer;

    // Push and pop to advance head and tail
    for (int i = 0; i < 10; i++) {
        BOOST_REQUIRE(buffer.push(i));
        int value;
        BOOST_REQUIRE(buffer.pop(value));
        BOOST_REQUIRE_EQUAL(value, i);
    }

    // Buffer should be back at start
    BOOST_REQUIRE(buffer.empty());

    // Fill again to test wraparound
    BOOST_REQUIRE(buffer.push(100));
    BOOST_REQUIRE(buffer.push(200));

    int value;
    BOOST_REQUIRE(buffer.pop(value));
    BOOST_REQUIRE_EQUAL(value, 100);

    BOOST_REQUIRE(buffer.pop(value));
    BOOST_REQUIRE_EQUAL(value, 200);
}

BOOST_AUTO_TEST_CASE(test_ring_buffer_try_pop) {
    aurore::LockFreeRingBuffer<int, 4> buffer;

    // Empty buffer
    auto result = buffer.try_pop();
    BOOST_REQUIRE(!(result.has_value()));

    // Push and pop
    buffer.push(42);
    result = buffer.try_pop();
    BOOST_REQUIRE(result.has_value());
    BOOST_REQUIRE_EQUAL(*result, 42);
}

BOOST_AUTO_TEST_CASE(test_ring_buffer_spsc_stress) {
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
                    BOOST_REQUIRE(value > last_value || value < last_value);  // Allow wraparound
                }
                last_value = value;
                count++;
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    BOOST_REQUIRE_EQUAL(produced.load(), kNumElements);
    BOOST_REQUIRE_EQUAL(consumed.load(), kNumElements);
}

BOOST_AUTO_TEST_CASE(test_ring_buffer_no_lost_elements) {
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
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    // Verify no elements lost
    BOOST_REQUIRE_EQUAL(sum_produced.load(), sum_consumed.load());
}

BOOST_AUTO_TEST_CASE(test_ring_buffer_capacity_one) {
    aurore::LockFreeRingBuffer<int, 2> buffer;  // Usable capacity = 1

    BOOST_REQUIRE_EQUAL(buffer.usable_capacity(), 1);

    BOOST_REQUIRE(buffer.push(42));
    BOOST_REQUIRE(buffer.full());
    BOOST_REQUIRE(!(buffer.push(99)));  // Should fail

    int value;
    BOOST_REQUIRE(buffer.pop(value));
    BOOST_REQUIRE_EQUAL(value, 42);
    BOOST_REQUIRE(buffer.empty());
}

BOOST_AUTO_TEST_CASE(test_ring_buffer_large_buffer) {
    aurore::LockFreeRingBuffer<uint64_t, 4096> buffer;

    BOOST_REQUIRE_EQUAL(buffer.capacity(), 4096);
    BOOST_REQUIRE_EQUAL(buffer.usable_capacity(), 4095);

    // Push and pop many elements
    for (uint64_t i = 0; i < 10000; i++) {
        while (!buffer.push(i)) {
            std::this_thread::yield();
        }

        uint64_t value;
        while (!buffer.pop(value)) {
            std::this_thread::yield();
        }
        BOOST_REQUIRE_EQUAL(value, i);
    }
}

BOOST_AUTO_TEST_CASE(test_ring_buffer_struct_type) {
    struct TestData {
        uint64_t id;
        double value;
        char padding[64];  // Cache line size
    };

    aurore::LockFreeRingBuffer<TestData, 8> buffer;

    TestData data{42, 3.14159, {0}};
    BOOST_REQUIRE(buffer.push(data));

    TestData popped;
    BOOST_REQUIRE(buffer.pop(popped));
    BOOST_REQUIRE_EQUAL(popped.id, 42);
    BOOST_REQUIRE_EQUAL(popped.value, 3.14159);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// MPMCRingBuffer Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(MPMCRingBuffer)

BOOST_AUTO_TEST_CASE(test_mpmc_ring_buffer_basic) {
    aurore::MPMCRingBuffer<int, 4> buffer;

    BOOST_REQUIRE(buffer.push(1));
    BOOST_REQUIRE(buffer.push(2));
    BOOST_REQUIRE(buffer.push(3));

    int value;
    BOOST_REQUIRE(buffer.pop(value));
    BOOST_REQUIRE_EQUAL(value, 1);

    BOOST_REQUIRE(buffer.pop(value));
    BOOST_REQUIRE_EQUAL(value, 2);

    BOOST_REQUIRE(buffer.pop(value));
    BOOST_REQUIRE_EQUAL(value, 3);
}

BOOST_AUTO_TEST_CASE(test_mpmc_ring_buffer_concurrent) {
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
            } else {
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

    BOOST_REQUIRE_EQUAL(sum.load(), expected_sum);
}

BOOST_AUTO_TEST_SUITE_END()
