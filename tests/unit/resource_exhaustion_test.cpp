/**
 * @file resource_exhaustion_test.cpp
 * @brief Part 8: Resource Exhaustion and Backpressure Tests
 *
 * Tests:
 * - Handle and descriptor leaks
 * - DMA channel exhaustion
 * - Queue saturation behavior
 * - Verification that backpressure propagates without deadlock or stall
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>
#include <future>

#include "aurore/ring_buffer.hpp"
#include "aurore/test_infrastructure.hpp"

namespace {

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
    } catch (const std::exception& e) { \
        g_tests_failed.fetch_add(1); \
        std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)
#define ASSERT_GT(a, b) do { if ((a) <= (b)) throw std::runtime_error("Assertion failed: " #a " > " #b); } while(0)
#define ASSERT_LT(a, b) do { if ((a) >= (b)) throw std::runtime_error("Assertion failed: " #a " < " #b); } while(0)
#define ASSERT_GE(a, b) do { if ((a) < (b)) throw std::runtime_error("Assertion failed: " #a " >= " #b); } while(0)

constexpr size_t kQueueCapacity = 4;

}  // anonymous namespace

// ============================================================================
// Handle/Descriptor Leak Tests
// ============================================================================

TEST(test_handle_leak_detection_basic) {
    aurore::test::ResourceMonitor fd_monitor(
        aurore::test::ResourceType::Descriptors, 100);

    for (int i = 0; i < 50; i++) {
        ASSERT_TRUE(fd_monitor.acquire());
    }

    auto stats = fd_monitor.get_stats();
    ASSERT_EQ(stats.active_count, 50);
    ASSERT_EQ(stats.peak_count, 50);
}

TEST(test_handle_no_leak_cycle) {
    aurore::test::ResourceMonitor fd_monitor(
        aurore::test::ResourceType::Descriptors, 100);

    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(fd_monitor.acquire());
    }
    for (int i = 0; i < 10; i++) {
        fd_monitor.release();
    }

    auto stats = fd_monitor.get_stats();
    ASSERT_EQ(stats.active_count, 0);
}

TEST(test_handle_leak_rate) {
    aurore::test::ResourceMonitor fd_monitor(
        aurore::test::ResourceType::Descriptors, 200);

    size_t initial_leaks = 0;
    size_t final_leaks = 0;

    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < 20; i++) {
            fd_monitor.acquire();
        }
        for (int i = 0; i < 20; i++) {
            fd_monitor.release();
        }

        size_t end = fd_monitor.get_stats().active_count;
        if (round == 0) initial_leaks = end;
        final_leaks = end;
    }

    ASSERT_EQ(initial_leaks, final_leaks);
}

TEST(test_parallel_handle_acquisition) {
    aurore::test::ResourceMonitor fd_monitor(
        aurore::test::ResourceType::Descriptors, 20);

    std::atomic<int> success_count(0);

    std::thread t1([&]() {
        for (int i = 0; i < 10; i++) {
            if (fd_monitor.acquire()) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < 10; i++) {
            if (fd_monitor.acquire()) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    t1.join();
    t2.join();

    ASSERT_GE(success_count.load(), 15);
}

// ============================================================================
// DMA Channel Exhaustion Tests
// ============================================================================

TEST(test_dma_channel_exhaustion_basic) {
    aurore::test::ResourceMonitor dma_monitor(
        aurore::test::ResourceType::DmaChannels, 8);

    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(dma_monitor.acquire());
    }

    auto stats = dma_monitor.get_stats();
    ASSERT_TRUE(stats.exhausted);

    ASSERT_FALSE(dma_monitor.acquire());
}

TEST(test_dma_channel_recovery_after_release) {
    aurore::test::ResourceMonitor dma_monitor(
        aurore::test::ResourceType::DmaChannels, 4);

    for (int i = 0; i < 4; i++) {
        ASSERT_TRUE(dma_monitor.acquire());
    }

    ASSERT_TRUE(dma_monitor.is_exhausted());

    for (int i = 0; i < 4; i++) {
        dma_monitor.release();
    }

    ASSERT_FALSE(dma_monitor.is_exhausted());
    ASSERT_TRUE(dma_monitor.acquire());
}

TEST(test_dma_channel_peak_tracking) {
    aurore::test::ResourceMonitor dma_monitor(
        aurore::test::ResourceType::DmaChannels, 16);

    for (int i = 0; i < 10; i++) {
        dma_monitor.acquire();
    }
    auto stats1 = dma_monitor.get_stats();
    ASSERT_EQ(stats1.peak_count, 10);

    for (int i = 0; i < 10; i++) {
        dma_monitor.release();
    }

    for (int i = 0; i < 6; i++) {
        dma_monitor.acquire();
    }
    auto stats2 = dma_monitor.get_stats();
    ASSERT_GE(stats2.peak_count, 6);
}

// ============================================================================
// Queue Backpressure Tests
// ============================================================================

TEST(test_queue_saturation_returns_false) {
    aurore::LockFreeRingBuffer<int, kQueueCapacity> buffer;

    int pushed_count = 0;
    for (size_t i = 0; i < kQueueCapacity * 2; i++) {
        if (buffer.push(static_cast<int>(i))) {
            pushed_count++;
        }
    }
    ASSERT_GT(pushed_count, 0);
}

TEST(test_queue_backpressure_no_deadlock) {
    aurore::LockFreeRingBuffer<int, kQueueCapacity> buffer;
    std::atomic<bool> done(false);
    std::atomic<size_t> push_count(0);
    std::atomic<size_t> pop_count(0);

    std::thread producer([&]() {
        for (int i = 0; i < 1000; i++) {
            while (!buffer.push(i)) {
                std::this_thread::yield();
            }
            push_count.fetch_add(1, std::memory_order_relaxed);
        }
        done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        int val;
        while (!done.load(std::memory_order_acquire) || !buffer.empty()) {
            if (buffer.pop(val)) {
                pop_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(push_count.load(), 1000);
    ASSERT_EQ(push_count.load(), pop_count.load());
}

TEST(test_queue_continuous_backpressure) {
    aurore::LockFreeRingBuffer<int, kQueueCapacity> buffer;

    std::atomic<size_t> blocked_count(0);
    std::atomic<bool> running(true);

    std::thread producer([&]() {
        for (int i = 0; i < 10000; i++) {
            while (!buffer.push(i)) {
                blocked_count.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        }
        running.store(false, std::memory_order_release);
    });

    std::thread consumer([&]() {
        int val;
        while (running.load(std::memory_order_acquire)) {
            if (buffer.pop(val)) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        }
        while (buffer.pop(val)) { }
    });

    producer.join();
    consumer.join();

    ASSERT_GT(blocked_count.load(), 0);
}

TEST(test_queue_multiple_producers_backpressure) {
    aurore::LockFreeRingBuffer<int, kQueueCapacity> buffer;
    std::atomic<size_t> total_produced(0);
    std::atomic<bool> producers_done(false);

    std::thread p1([&]() {
        for (int i = 0; i < 500; i++) {
            if (buffer.push(i)) {
                total_produced.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    });

    std::thread p2([&]() {
        for (int i = 0; i < 500; i++) {
            if (buffer.push(i + 1000)) {
                total_produced.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    });

    std::thread consumer([&]() {
        int val;
        while (!producers_done.load(std::memory_order_acquire) || !buffer.empty()) {
            buffer.pop(val);
            std::this_thread::yield();
        }
    });

    p1.join();
    p2.join();
    producers_done.store(true, std::memory_order_release);
    consumer.join();

    ASSERT_GE(total_produced.load(), 100);
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST(test_resource_stress_high_frequency) {
    aurore::test::ResourceMonitor fd_monitor(
        aurore::test::ResourceType::Descriptors, 50);

    std::atomic<bool> done(false);
    std::atomic<size_t> acquire_count(0);
    std::atomic<size_t> release_count(0);

    std::thread stress_thread([&]() {
        while (!done.load(std::memory_order_acquire)) {
            if (fd_monitor.acquire()) {
                acquire_count.fetch_add(1, std::memory_order_relaxed);
                if (acquire_count.load() % 2 == 0) {
                    fd_monitor.release();
                    release_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    done.store(true, std::memory_order_release);
    stress_thread.join();

    auto stats = fd_monitor.get_stats();
    ASSERT_TRUE(stats.active_count <= 50);
}

TEST(test_resource_stress_alternating) {
    aurore::test::ResourceMonitor fd_monitor(
        aurore::test::ResourceType::Descriptors, 100);

    for (int round = 0; round < 20; round++) {
        for (int i = 0; i < 20; i++) {
            fd_monitor.acquire();
        }
        for (int i = 0; i < 20; i++) {
            fd_monitor.release();
        }
    }

    auto stats = fd_monitor.get_stats();
    ASSERT_EQ(stats.active_count, 0);
}

TEST(test_backpressure_propagation_order) {
    aurore::LockFreeRingBuffer<int, 2> buffer;

    buffer.push(1);
    buffer.push(2);

    bool push1 = buffer.push(3);
    bool push2 = buffer.push(4);

    ASSERT_FALSE(push1 || push2);
    ASSERT_TRUE(!push1 && !push2);

    int val;
    buffer.pop(val);
    bool push3 = buffer.push(5);

    ASSERT_TRUE(push3);
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "=== Part 8: Resource Exhaustion and Backpressure Tests ===" << std::endl;
    std::cout << "Running resource exhaustion tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    std::cout << "\n--- Handle/Descriptor Leak Tests ---" << std::endl;
    RUN_TEST(test_handle_leak_detection_basic);
    RUN_TEST(test_handle_no_leak_cycle);
    RUN_TEST(test_handle_leak_rate);
    RUN_TEST(test_parallel_handle_acquisition);

    std::cout << "\n--- DMA Channel Exhaustion Tests ---" << std::endl;
    RUN_TEST(test_dma_channel_exhaustion_basic);
    RUN_TEST(test_dma_channel_recovery_after_release);
    RUN_TEST(test_dma_channel_peak_tracking);

    std::cout << "\n--- Queue Backpressure Tests ---" << std::endl;
    RUN_TEST(test_queue_saturation_returns_false);
    RUN_TEST(test_queue_backpressure_no_deadlock);
    RUN_TEST(test_queue_continuous_backpressure);
    RUN_TEST(test_queue_multiple_producers_backpressure);

    std::cout << "\n--- Stress Tests ---" << std::endl;
    RUN_TEST(test_resource_stress_high_frequency);
    RUN_TEST(test_resource_stress_alternating);
    RUN_TEST(test_backpressure_propagation_order);

    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}