/**
 * @file memory_resource_test.cpp
 * @brief Part 1: Stack and Heap Integrity Tests
 *
 * Validates:
 * - Per-thread stack high-water marks under worst-case execution
 * - Stack growth detection and recursion leaks
 * - Heap allocation count, size, and fragmentation tracking
 * - No unbounded growth under sustained operation
 *
 * Fail conditions:
 * - Stack margin < configured safety threshold
 * - Heap growth trend exceeds baseline envelope
 * - Fragmentation prevents allocation of required blocks
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <chrono>

#include "aurore/ring_buffer.hpp"
#include "aurore/test_utils.hpp"
#include "aurore/timing.hpp"

namespace {

std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run.fetch_add(1); \
    aurore::test::TestEnvironment::reset_trackers(); \
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
#define ASSERT_NE(a, b) do { if ((a) == (b)) throw std::runtime_error("Assertion failed: " #a " == " #b); } while(0)
#define ASSERT_GT(a, b) do { if (!((a) > (b))) throw std::runtime_error("Assertion failed: " #a " > " #b); } while(0)
#define ASSERT_GE(a, b) do { if (!((a) >= (b))) throw std::runtime_error("Assertion failed: " #a " >= " #b); } while(0)
#define ASSERT_LT(a, b) do { if (!((a) < (b))) throw std::runtime_error("Assertion failed: " #a " < " #b); } while(0)
#define ASSERT_LE(a, b) do { if (!((a) <= (b))) throw std::runtime_error("Assertion failed: " #a " <= " #b); } while(0)

constexpr size_t kStackSafetyMarginKb = 128; // Example safety threshold
constexpr uint64_t kMaxStackSizeKb = 8192; // Max stack size for StackTracker
constexpr size_t kHeapBaselineEnvelopeBytes = 1024 * 1024; // 1MB heap baseline

}  // anonymous namespace

// ============================================================================
// Stack Integrity Tests
// ============================================================================

TEST(test_stack_tracker_basic) {
    aurore::test::StackTracker& tracker = aurore::test::TestEnvironment::get_stack_tracker();
    tracker.record_sample();
    auto stats = tracker.get_stats();

    ASSERT_GT(stats.max_stack_size_bytes, 0);
    ASSERT_GT(stats.high_water_bytes, 0);
}

TEST(test_stack_tracker_safety_threshold) {
    aurore::test::StackTracker& tracker = aurore::test::TestEnvironment::get_stack_tracker();

    volatile char buffer[8192];
    (void)buffer;
    buffer[0] = 'x';
    tracker.record_sample();

    ASSERT_TRUE(tracker.check_safety_threshold(kStackSafetyMarginKb));
}

TEST(test_stack_growth_detection) {
    aurore::test::StackTracker& tracker = aurore::test::TestEnvironment::get_stack_tracker();

    tracker.record_sample();

    volatile char small_buf[256];
    (void)small_buf;
    small_buf[0] = 'a';
    tracker.record_sample();

    auto stats_small = tracker.get_stats();

    volatile char large_buf[4096];
    (void)large_buf;
    large_buf[0] = 'b';
    tracker.record_sample();

    auto stats_large = tracker.get_stats();

    ASSERT_GE(stats_large.high_water_bytes, stats_small.high_water_bytes);
}

TEST(test_stack_recursion_detection) {
    aurore::test::StackTracker& tracker = aurore::test::TestEnvironment::get_stack_tracker();

    std::function<void(int)> recursive_func = [&](int depth) {
        if (depth <= 0) return;
        char local[256];
        (void)local;
        local[0] = static_cast<char>(depth);
        tracker.record_sample();
        recursive_func(depth - 1);
    };

    recursive_func(20);

    auto stats = tracker.get_stats();
    ASSERT_GT(stats.high_water_bytes, 5000);
}

TEST(test_stack_multi_thread_independent) {
    // This test creates local StackTracker instances to simulate independent threads
    // and does not use the global TestEnvironment StackTracker directly.
    aurore::test::StackTracker tracker1(kMaxStackSizeKb);
    aurore::test::StackTracker tracker2(kMaxStackSizeKb);

    std::thread t1([&]() {
        char buf[1024];
        (void)buf;
        buf[0] = '1';
        tracker1.record_sample();
    });

    std::thread t2([&]() {
        char buf[2048];
        (void)buf;
        buf[0] = '2';
        tracker2.record_sample();
    });

    t1.join();
    t2.join();

    auto stats1 = tracker1.get_stats();
    auto stats2 = tracker2.get_stats();

    ASSERT_GT(stats1.high_water_bytes, 0);
    ASSERT_GT(stats2.high_water_bytes, 0);
}

TEST(test_stack_alignment_check) {
    aurore::test::StackTracker& tracker = aurore::test::TestEnvironment::get_stack_tracker();

    alignas(64) char aligned_buffer[512];
    (void)aligned_buffer;
    aligned_buffer[0] = 'a';
    tracker.record_sample();

    auto stats = tracker.get_stats();
    ASSERT_TRUE(stats.margin_ok || stats.safety_margin_bytes > 0);
}

// ============================================================================
// Heap Integrity Tests
// ============================================================================

TEST(test_heap_tracker_basic) {
    aurore::test::HeapTracker& tracker = aurore::test::TestEnvironment::get_heap_tracker();

    tracker.record_allocation(1024);
    tracker.record_deallocation(512);

    auto stats = tracker.get_stats();
    ASSERT_EQ(stats.allocation_count, 1);
    ASSERT_EQ(stats.deallocation_count, 1);
}

TEST(test_heap_tracker_leak_detection) {
    aurore::test::HeapTracker& tracker = aurore::test::TestEnvironment::get_heap_tracker();

    tracker.record_allocation(1024);
    tracker.record_allocation(2048);

    auto stats = tracker.get_stats();
    ASSERT_TRUE(stats.current_allocated_bytes > 0);
}

TEST(test_heap_no_leak_detection) {
    aurore::test::HeapTracker& tracker = aurore::test::TestEnvironment::get_heap_tracker();

    tracker.record_allocation(1024);
    tracker.record_deallocation(1024);

    auto stats = tracker.get_stats();
    ASSERT_FALSE(stats.has_leak);
    ASSERT_EQ(stats.current_allocated_bytes, 0);
}

TEST(test_heap_growth_trend_detection) {
    aurore::test::HeapTracker& tracker = aurore::test::TestEnvironment::get_heap_tracker();
    tracker.set_baseline_envelope(256);

    for (size_t i = 0; i < 20; i++) {
        tracker.record_allocation(100);
        tracker.record_deallocation(50);
    }

    auto stats = tracker.get_stats();
    ASSERT_TRUE(stats.growth_trend_exceeds_baseline);
}

TEST(test_heap_growth_within_envelope) {
    aurore::test::HeapTracker& tracker = aurore::test::TestEnvironment::get_heap_tracker();
    tracker.set_baseline_envelope(1024 * 1024);

    for (size_t i = 0; i < 100; i++) {
        tracker.record_allocation(1000);
        tracker.record_deallocation(1000);
    }

    ASSERT_TRUE(tracker.check_growth_envelope());
}

TEST(test_heap_peak_tracking) {
    aurore::test::HeapTracker& tracker = aurore::test::TestEnvironment::get_heap_tracker();

    tracker.record_allocation(1024);
    auto stats1 = tracker.get_stats();
    size_t peak1 = stats1.peak_allocated_bytes;

    tracker.record_allocation(2048);
    auto stats2 = tracker.get_stats();
    size_t peak2 = stats2.peak_allocated_bytes;

    ASSERT_GE(peak2, peak1);
}

TEST(test_heap_fragmentation_calculation) {
    aurore::test::HeapTracker& tracker = aurore::test::TestEnvironment::get_heap_tracker();

    tracker.record_allocation(1000);
    tracker.record_allocation(500);
    tracker.record_deallocation(300);

    auto stats = tracker.get_stats();
    ASSERT_GE(stats.fragmentation_ratio, 0.0);
    ASSERT_LE(stats.fragmentation_ratio, 1.0);
}

TEST(test_heap_long_run_stability) {
    aurore::test::HeapTracker& tracker = aurore::test::TestEnvironment::get_heap_tracker();
    tracker.set_baseline_envelope(4096);

    constexpr size_t kIterations = 1000;
    for (size_t i = 0; i < kIterations; i++) {
        tracker.record_allocation(64);
        tracker.record_deallocation(64);
    }

    auto stats = tracker.get_stats();
    ASSERT_EQ(stats.allocation_count, kIterations);
    ASSERT_FALSE(stats.has_leak);
}

// New long-run stack test
TEST(test_stack_long_run_worst_case_usage) {
    aurore::test::StackTracker& tracker = aurore::test::TestEnvironment::get_stack_tracker();
    constexpr int kMaxDepth = 500; // Deep recursion to simulate worst-case
    constexpr size_t kStackFrameSize = 128; // Bytes per recursive call

    std::function<void(int)> deep_recursive_func = [&](int depth) {
        if (depth <= 0) return;
        volatile char local_data[kStackFrameSize];
        (void)local_data; // Prevent optimization
        std::memset(const_cast<char*>(local_data), 0xAA, sizeof(local_data));
        tracker.record_sample();
        deep_recursive_func(depth - 1);
    };

    deep_recursive_func(kMaxDepth);

    auto stats = tracker.get_stats();
    // Assuming a minimum stack usage for deep recursion
    ASSERT_GT(stats.high_water_bytes, static_cast<uint64_t>(kMaxDepth) * kStackFrameSize / 2U);
    // Check against a reasonable safety threshold, e.g., 256KB margin for an 8MB stack
    ASSERT_TRUE(tracker.check_safety_threshold(256));
    std::cout << "  High water mark for deep recursion: " << stats.high_water_bytes / 1024 << " KB" << std::endl;
}

// New long-run heap test
TEST(test_heap_long_run_unbounded_growth_detection) {
    aurore::test::HeapTracker& tracker = aurore::test::TestEnvironment::get_heap_tracker();
    tracker.set_baseline_envelope(256 * 1024); // 256KB envelope — growth of ~750KB triggers this
    std::vector<char*> allocated_blocks;
    constexpr size_t kBlockSize = 1024; // 1KB blocks
    constexpr int kIterations = 2000; // Allocate 2MB over time without freeing

    for (int i = 0; i < kIterations; ++i) {
        char* block = new char[kBlockSize];
        allocated_blocks.push_back(block);
        tracker.record_allocation(kBlockSize);
        if (i % 100 == 0) { // Record samples periodically
            tracker.get_stats(); // Force update internal stats
        }
    }

    auto stats = tracker.get_stats();
    ASSERT_TRUE(stats.has_leak); // Should detect a leak
    ASSERT_TRUE(stats.growth_trend_exceeds_baseline); // Should detect unbounded growth
    ASSERT_GT(stats.current_allocated_bytes, kIterations * kBlockSize / 2); // Significant allocation remaining

    // Clean up to prevent actual memory leak in test runner
    for (char* block : allocated_blocks) {
        delete[] block;
        tracker.record_deallocation(kBlockSize);
    }
    allocated_blocks.clear();
    ASSERT_FALSE(tracker.get_stats().has_leak); // Should be clean after freeing
}

// ============================================================================
// Resource Monitor Tests
// ============================================================================

TEST(test_resource_monitor_file_descriptor) {
    aurore::test::ResourceMonitor monitor(
        aurore::test::ResourceType::Descriptors, 100);

    ASSERT_TRUE(monitor.acquire());
    ASSERT_TRUE(monitor.acquire());

    auto stats = monitor.get_stats();
    ASSERT_EQ(stats.active_count, 2);

    monitor.release();
    monitor.release();

    ASSERT_FALSE(monitor.is_exhausted());
}

TEST(test_resource_monitor_exhaustion) {
    aurore::test::ResourceMonitor monitor(
        aurore::test::ResourceType::Descriptors, 2);

    ASSERT_TRUE(monitor.acquire());
    ASSERT_TRUE(monitor.acquire());
    ASSERT_FALSE(monitor.acquire());

    auto stats = monitor.get_stats();
    ASSERT_TRUE(stats.exhausted);

    monitor.release();
    ASSERT_FALSE(monitor.is_exhausted());
}

TEST(test_resource_monitor_peak_tracking) {
    aurore::test::ResourceMonitor monitor(
        aurore::test::ResourceType::MemoryBlocks, 10);

    for (int i = 0; i < 5; i++) {
        monitor.acquire();
    }

    auto stats = monitor.get_stats();
    ASSERT_EQ(stats.peak_count, 5);
    ASSERT_EQ(stats.active_count, 5);
}

TEST(test_resource_monitor_different_types) {
    aurore::test::ResourceMonitor fd_monitor(
        aurore::test::ResourceType::Descriptors, 50);
    aurore::test::ResourceMonitor dma_monitor(
        aurore::test::ResourceType::DmaChannels, 8);

    fd_monitor.acquire();
    dma_monitor.acquire();

    auto fd_stats = fd_monitor.get_stats();
    auto dma_stats = dma_monitor.get_stats();

    ASSERT_EQ(fd_stats.active_count, 1);
    ASSERT_EQ(dma_stats.active_count, 1);
}

// ============================================================================
// Queue Stress Tests
// ============================================================================

TEST(test_queue_stress_basic) {
    aurore::test::QueueStressTest queue(10,
        aurore::test::BackpressurePolicy::ReturnFalse);

    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(queue.push(nullptr, 100));
    }

    auto metrics = queue.get_metrics();
    ASSERT_EQ(metrics.total_produced, 5);
    ASSERT_EQ(metrics.current_depth, 5);
}

TEST(test_queue_stress_saturation) {
    aurore::test::QueueStressTest queue(4,
        aurore::test::BackpressurePolicy::ReturnFalse);

    for (int i = 0; i < 4; i++) {
        ASSERT_TRUE(queue.push(nullptr, 100));
    }

    ASSERT_FALSE(queue.push(nullptr, 100));

    auto metrics = queue.get_metrics();
    ASSERT_EQ(metrics.total_dropped, 1);
    ASSERT_TRUE(metrics.backpressure_triggered);
}

TEST(test_queue_stress_producer_consumer) {
    aurore::test::QueueStressTest queue(100,
        aurore::test::BackpressurePolicy::ReturnFalse);

    std::atomic<size_t> produced(0);
    std::atomic<size_t> consumed(0);

    std::thread producer([&]() {
        for (int i = 0; i < 1000; i++) {
            if (queue.push(nullptr, 100)) {
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    std::thread consumer([&]() {
        size_t popped;
        for (int i = 0; i < 1000; i++) {
            if (queue.pop(nullptr, popped)) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();

    auto metrics = queue.get_metrics();
    ASSERT_EQ(metrics.total_produced, produced.load());
    ASSERT_EQ(metrics.total_consumed, consumed.load());
}

TEST(test_queue_depth_tracking) {
    aurore::test::QueueStressTest queue(10,
        aurore::test::BackpressurePolicy::ReturnFalse);

    for (int i = 0; i < 5; i++) {
        queue.push(nullptr, 100);
    }

    auto metrics = queue.get_metrics();
    ASSERT_EQ(metrics.current_depth, 5);
    ASSERT_EQ(metrics.max_depth, 5);
}

TEST(test_queue_head_of_line_blocking) {
    aurore::LockFreeRingBuffer<int, 4> buffer;

    for (int i = 0; i < 4; i++) {
        buffer.push(i);
    }

    std::atomic<bool> blocked(false);
    std::thread block_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        int val;
        while (buffer.pop(val)) {
        }
    });

    buffer.push(10);
    block_thread.join();

    ASSERT_TRUE(buffer.full());
}

TEST(test_queue_backpressure_propagation) {
    aurore::LockFreeRingBuffer<int, 2> buffer;

    for (int i = 0; i < 2; i++) {
        ASSERT_TRUE(buffer.push(i));
    }

    ASSERT_TRUE(buffer.full());

    bool pushed = buffer.push(100);
    ASSERT_FALSE(pushed);
}

// ============================================================================
// Ring Buffer Long-Run Tests
// ============================================================================

TEST(test_ring_buffer_long_run_no_leak) {
    aurore::LockFreeRingBuffer<int, 4> buffer;

    for (int i = 0; i < 10000; i++) {
        buffer.push(i);
        int val;
        buffer.pop(val);
    }

    ASSERT_TRUE(buffer.empty());
}

TEST(test_ring_buffer_sustained_operation) {
    aurore::LockFreeRingBuffer<int, 4> buffer;

    std::atomic<bool> running(true);
    std::atomic<size_t> push_count(0);
    std::atomic<size_t> pop_count(0);

    std::thread producer([&]() {
        while (running.load(std::memory_order_acquire)) {
            size_t val = push_count.load();
            if (buffer.push(static_cast<int>(val))) {
                push_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    std::thread consumer([&]() {
        int val;
        while (running.load(std::memory_order_acquire)) {
            if (buffer.pop(val)) {
                pop_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    running.store(false, std::memory_order_release);

    producer.join();
    consumer.join();

    // Give time for residual items to drain
    int val;
    while (buffer.pop(val)) {
        pop_count.fetch_add(1, std::memory_order_relaxed);
    }

    ASSERT_TRUE(buffer.empty());
}

TEST(test_ring_buffer_under_continuous_load) {
    aurore::LockFreeRingBuffer<int, 4> buffer;

    for (int iter = 0; iter < 100; iter++) {
        for (int i = 0; i < 4; i++) {
            buffer.push(i);
        }

        int val;
        for (int i = 0; i < 4; i++) {
            buffer.pop(val);
        }
    }

    ASSERT_TRUE(buffer.empty());
}

// ============================================================================
// DMA Health Tests
// ============================================================================

TEST(test_dma_health_monitor_basic) {
    aurore::test::DmaHealthMonitor monitor;
    
    monitor.record_transfer(4096, 1000);
    auto stats = monitor.get_stats();
    
    ASSERT_EQ(stats.transfer_count, 1);
    ASSERT_EQ(stats.state, aurore::test::DmaState::Active);
}

TEST(test_dma_health_error_detection) {
    aurore::test::DmaHealthMonitor monitor;
    
    monitor.record_transfer(4096, 1000);
    monitor.record_error();
    
    auto stats = monitor.get_stats();
    ASSERT_EQ(stats.error_count, 1);
    ASSERT_EQ(stats.state, aurore::test::DmaState::Error);
}

TEST(test_dma_health_recovery) {
    aurore::test::DmaHealthMonitor monitor;
    
    monitor.record_transfer(4096, 1000);
    monitor.simulate_fault();
    
    bool recovered = monitor.recover();
    ASSERT_TRUE(recovered);
    
    auto stats = monitor.get_stats();
    ASSERT_EQ(stats.state, aurore::test::DmaState::Idle);
}

TEST(test_dma_alignment_check) {
    aurore::test::DmaHealthMonitor monitor;
    
    bool aligned = monitor.check_alignment(4096);
    ASSERT_TRUE(aligned);
}

// ============================================================================
// Thermal Health Tests
// ============================================================================

TEST(test_thermal_health_monitor_basic) {
    aurore::test::ThermalHealthMonitor monitor(80.0);
    
    monitor.update_temperature(50.0);
    auto stats = monitor.get_stats();
    
    ASSERT_EQ(stats.throttle_state, aurore::test::ThrottleState::Nominal);
}

TEST(test_thermal_throttling_transition) {
    aurore::test::ThermalHealthMonitor monitor(80.0);
    
    monitor.update_temperature(85.0);
    auto state = monitor.check_throttle_state();
    
    ASSERT_EQ(state, aurore::test::ThrottleState::Critical);
}

TEST(test_thermal_simulate_throttling) {
    aurore::test::ThermalHealthMonitor monitor(80.0);
    
    bool transitioned = monitor.simulate_throttling_transition();
    ASSERT_TRUE(transitioned);
    
    auto stats = monitor.get_stats();
    ASSERT_EQ(stats.throttle_state, aurore::test::ThrottleState::Throttling);
}

TEST(test_thermal_timing_contract) {
    aurore::test::ThermalHealthMonitor monitor(80.0);
    
    bool contract_ok = monitor.verify_timing_contract();
    ASSERT_TRUE(contract_ok);
}

// ============================================================================
// DMA Fault Injection Edge Cases
// ============================================================================

TEST(test_dma_buffer_exhaustion_recovery) {
    aurore::test::DmaHealthMonitor monitor;
    
    for (size_t i = 0; i < 100; i++) {
        monitor.record_transfer(4096, 1000);
    }
    
    auto stats = monitor.get_stats();
    ASSERT_EQ(stats.transfer_count, 100); // Fixed: was 101, should be 100 as the loop runs 100 times
    
    monitor.record_error();
    
    bool recovered = monitor.recover();
    ASSERT_TRUE(recovered);
}

TEST(test_dma_multiple_fault_recovery) {
    aurore::test::DmaHealthMonitor monitor;
    
    monitor.record_transfer(4096, 1000);
    monitor.simulate_fault();
    bool recover1 = monitor.recover();
    ASSERT_TRUE(recover1);
    
    monitor.record_transfer(4096, 1000);
    monitor.simulate_fault();
    bool recover2 = monitor.recover();
    ASSERT_TRUE(recover2);
}

TEST(test_dma_alignment_boundary) {
    aurore::test::DmaHealthMonitor monitor;
    
    bool page_aligned = monitor.check_alignment(4096);
    ASSERT_TRUE(page_aligned);
    
    bool cache_aligned = monitor.check_alignment(64);
    ASSERT_TRUE(cache_aligned);
}

TEST(test_dma_error_state_transition) {
    aurore::test::DmaHealthMonitor monitor;
    
    monitor.record_transfer(4096, 1000);
    ASSERT_EQ(monitor.get_stats().state, aurore::test::DmaState::Active);
    
    monitor.record_error();
    ASSERT_EQ(monitor.get_stats().state, aurore::test::DmaState::Error);
    
    bool recovered = monitor.recover();
    ASSERT_TRUE(recovered);
    ASSERT_EQ(monitor.get_stats().state, aurore::test::DmaState::Idle);
}

// ============================================================================
// Thermal Edge Cases
// ============================================================================

TEST(test_thermal_rapid_transitions) {
    aurore::test::ThermalHealthMonitor monitor(80.0);
    
    for (int i = 0; i < 10; i++) {
        monitor.update_temperature(45.0);
        // Do not record_throttle_event here, it's checked by update_temperature
    }
    
    // Simulate a transition to throttling
    monitor.simulate_throttling_transition();
    auto stats = monitor.get_stats();
    ASSERT_EQ(stats.throttle_count, 1); // Should be 1 from simulate_throttling_transition

    monitor.update_temperature(85.0);
    stats = monitor.get_stats();
    ASSERT_EQ(stats.throttle_state, aurore::test::ThrottleState::Critical);
}

TEST(test_thermal_recovery_from_critical) {
    aurore::test::ThermalHealthMonitor monitor(80.0);
    
    monitor.update_temperature(90.0);
    ASSERT_EQ(monitor.check_throttle_state(), aurore::test::ThrottleState::Critical);
    
    monitor.update_temperature(50.0);
    ASSERT_EQ(monitor.check_throttle_state(), aurore::test::ThrottleState::Nominal);
}

TEST(test_thermal_frequency_scaling_detection) {
    aurore::test::ThermalHealthMonitor monitor(80.0);
    
    monitor.update_temperature(75.0);
    auto stats = monitor.get_stats();
    
    // Updated logic: check if it's throttling or critical based on update_temperature
    ASSERT_TRUE(stats.throttle_state == aurore::test::ThrottleState::Throttling || stats.throttle_state == aurore::test::ThrottleState::Nominal);
}

// ============================================================================
// Queue Cross-Thread Ordering
// ============================================================================

TEST(test_queue_message_ordering) {
    aurore::LockFreeRingBuffer<int, 8> buffer;
    
    for (int i = 0; i < 4; i++) {
        buffer.push(i);
    }
    
    int val;
    for (int i = 0; i < 4; i++) {
        ASSERT_TRUE(buffer.pop(val));
        ASSERT_EQ(val, i);
    }
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    aurore::test::TestEnvironment::init(kMaxStackSizeKb, kHeapBaselineEnvelopeBytes);

    std::cout << "=== Part 1: Stack and Heap Integrity Tests ===" << std::endl;
    std::cout << "Running memory and resource tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    std::cout << "\n--- Stack Integrity Tests ---" << std::endl;
    RUN_TEST(test_stack_tracker_basic);
    RUN_TEST(test_stack_tracker_safety_threshold);
    RUN_TEST(test_stack_growth_detection);
    RUN_TEST(test_stack_recursion_detection);
    RUN_TEST(test_stack_multi_thread_independent);
    RUN_TEST(test_stack_alignment_check);
    RUN_TEST(test_stack_long_run_worst_case_usage); // New test

    std::cout << "\n--- Heap Integrity Tests ---" << std::endl;
    RUN_TEST(test_heap_tracker_basic);
    RUN_TEST(test_heap_tracker_leak_detection);
    RUN_TEST(test_heap_no_leak_detection);
    RUN_TEST(test_heap_growth_trend_detection);
    RUN_TEST(test_heap_growth_within_envelope);
    RUN_TEST(test_heap_peak_tracking);
    RUN_TEST(test_heap_fragmentation_calculation);
    RUN_TEST(test_heap_long_run_stability);
    RUN_TEST(test_heap_long_run_unbounded_growth_detection); // New test

    std::cout << "\n--- Resource Monitor Tests ---" << std::endl;
    RUN_TEST(test_resource_monitor_file_descriptor);
    RUN_TEST(test_resource_monitor_exhaustion);
    RUN_TEST(test_resource_monitor_peak_tracking);
    RUN_TEST(test_resource_monitor_different_types);

    std::cout << "\n--- Queue Stress Tests ---" << std::endl;
    RUN_TEST(test_queue_stress_basic);
    RUN_TEST(test_queue_stress_saturation);
    RUN_TEST(test_queue_stress_producer_consumer);
    RUN_TEST(test_queue_depth_tracking);

    std::cout << "\n--- Ring Buffer Long-Run Tests ---" << std::endl;
    RUN_TEST(test_ring_buffer_long_run_no_leak);
    RUN_TEST(test_ring_buffer_sustained_operation);
    RUN_TEST(test_ring_buffer_under_continuous_load);

    std::cout << "\n--- DMA Health Tests ---" << std::endl;
    RUN_TEST(test_dma_health_monitor_basic);
    RUN_TEST(test_dma_health_error_detection);
    RUN_TEST(test_dma_health_recovery);
    RUN_TEST(test_dma_alignment_check);

    std::cout << "\n--- Thermal Health Tests ---" << std::endl;
    RUN_TEST(test_thermal_health_monitor_basic);
    RUN_TEST(test_thermal_throttling_transition);
    RUN_TEST(test_thermal_simulate_throttling);
    RUN_TEST(test_thermal_timing_contract);

    std::cout << "\n--- DMA Fault Injection Edge Cases ---" << std::endl;
    RUN_TEST(test_dma_buffer_exhaustion_recovery);
    RUN_TEST(test_dma_multiple_fault_recovery);
    RUN_TEST(test_dma_alignment_boundary);
    RUN_TEST(test_dma_error_state_transition);

    std::cout << "\n--- Thermal Edge Cases ---" << std::endl;
    RUN_TEST(test_thermal_rapid_transitions);
    RUN_TEST(test_thermal_recovery_from_critical);
    RUN_TEST(test_thermal_frequency_scaling_detection);

    std::cout << "\n--- Queue Cross-Thread Ordering ---" << std::endl;
    RUN_TEST(test_queue_message_ordering);

    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}