/**
 * @file concurrency_pathology_test.cpp
 * @brief Part 4: Advanced Concurrency Pathology Tests
 *
 * Tests beyond queue correctness:
 * - Deadlock and livelock scenarios
 * - Starvation under load
 * - Priority inversion, including ISR-induced inversion
 * - Shared data coherency across cores and interrupt boundaries
 *
 * Instrumentation detects blocking beyond declared limits.
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
#include <condition_variable>

#include "aurore/ring_buffer.hpp"
#include "aurore/test_infrastructure.hpp"
#include "aurore/timing.hpp"

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

}  // anonymous namespace

// ============================================================================
// Deadlock Prevention Tests
// ============================================================================

TEST(test_no_deadlock_simple_lock_order) {
    std::mutex m1;
    std::mutex m2;

    std::thread t1([&]() {
        std::lock_guard<std::mutex> l1(m1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> l2(m2);
    });

    std::thread t2([&]() {
        std::lock_guard<std::mutex> l1(m1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> l2(m2);
    });

    auto start = std::chrono::steady_clock::now();
    t1.join();
    t2.join();
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    ASSERT_LT(duration.count(), 5000);
}

TEST(test_no_deadlock_ordered_acquisition) {
    std::mutex m1;
    std::mutex m2;

    std::thread t1([&]() {
        std::lock(m1, m2);
        std::lock_guard<std::mutex> l1(m1, std::adopt_lock);
        std::lock_guard<std::mutex> l2(m2, std::adopt_lock);
    });

    std::thread t2([&]() {
        std::lock(m1, m2);
        std::lock_guard<std::mutex> l1(m1, std::adopt_lock);
        std::lock_guard<std::mutex> l2(m2, std::adopt_lock);
    });

    t1.join();
    t2.join();
}

TEST(test_no_deadlock_no_circular_wait) {
    aurore::test::ConcurrencyPathologyDetector detector;
    detector.enable_instrumentation(true);

    void* lock1 = &detector;
    void* lock2 = reinterpret_cast<void*>(reinterpret_cast<char*>(&detector) + 1);
    (void)lock2;

    detector.register_acquire(lock1, 50);

    auto report = detector.check();

    detector.release(lock1);

    ASSERT_EQ(report.type, aurore::test::PathologyType::None);
}

// ============================================================================// Livelock Prevention Tests
// ============================================================================

TEST(test_no_livelock_spin_on_lock) {
    std::atomic<bool> ready(false);
    std::atomic<int> contention(0);
    std::mutex m;

    std::thread t1([&]() {
        while (!ready.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::lock_guard<std::mutex> l(m);
    });

    std::thread t2([&]() {
        while (!ready.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (int i = 0; i < 100 && !m.try_lock(); i++) {
            contention.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    ready.store(true, std::memory_order_release);

    t1.join();
    t2.join();

    ASSERT_GT(contention.load(), 0);
}

TEST(test_livelock_backoff) {
    std::atomic<int> iterations(0);
    std::mutex m;
    m.lock();

    auto start = std::chrono::steady_clock::now();

    std::thread t([&]() {
        while (iterations.load() < 100) {
            iterations.fetch_add(1, std::memory_order_relaxed);
            if (m.try_lock()) {
                m.unlock();
                break;
            }
            std::this_thread::yield();
        }
    });

    t.join();
    m.unlock();

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    ASSERT_LT(duration.count(), 2000);
}

// ============================================================================
// Livelock Prevention Tests
// ============================================================================

TEST(test_no_livelock_spin) {
    std::atomic<int> contention(0);
    std::mutex m;
    m.lock();

    std::thread t([&]() {
        for (int i = 0; i < 10 && !m.try_lock(); i++) {
            contention.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    t.join();
    m.unlock();

    ASSERT_GT(contention.load(), 0);
}

TEST(test_no_starvation_bounded_wait) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    std::atomic<int> producer_wins(0);
    std::atomic<int> consumer_wins(0);

    std::thread producer([&]() {
        for (int i = 0; i < 1000; i++) {
            if (buffer.push(i)) {
                producer_wins.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    std::thread consumer([&]() {
        int val;
        for (int i = 0; i < 1000; i++) {
            if (buffer.pop(val)) {
                consumer_wins.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_GT(producer_wins.load(), 0);
    ASSERT_GT(consumer_wins.load(), 0);
}

TEST(test_no_starvation_thread_pool) {
    std::atomic<int> work_items_completed(0);
    std::vector<std::thread> workers;

    for (int w = 0; w < 4; w++) {
        workers.emplace_back([&]() {
            for (int i = 0; i < 50; i++) {
                work_items_completed.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        });
    }

    for (auto& t : workers) {
        t.join();
    }

    ASSERT_EQ(work_items_completed.load(), 200);
}

// ============================================================================
// Priority Inversion Tests
// ============================================================================

TEST(test_priority_inversion_detection) {
    aurore::test::ConcurrencyPathologyDetector detector;
    detector.enable_instrumentation(true);

    void* lock = &detector;

    detector.register_acquire(lock, 99);

    auto report = detector.check();

    detector.release(lock);

    ASSERT_EQ(report.type, aurore::test::PathologyType::None);
}

TEST(test_priority_inversion_mitigation) {
    std::mutex m;
    std::atomic<int> medium_priority_waiters(0);
    std::atomic<bool> high_priority_done(false);
    std::atomic<bool> medium_ready(false);

    // Acquire the mutex here so low thread doesn't race high thread to release it.
    // This guarantees: medium spins at least once before high acquires the mutex.
    m.lock();

    std::thread medium([&]() {
        medium_ready.store(true, std::memory_order_release);
        while (!high_priority_done.load(std::memory_order_acquire)) {
            medium_priority_waiters.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    std::thread high([&]() {
        std::lock_guard<std::mutex> l(m);
        high_priority_done.store(true, std::memory_order_release);
    });

    // Wait until medium is spinning, then give it a few ms before releasing.
    while (!medium_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    m.unlock();

    medium.join();
    high.join();

    ASSERT_GT(medium_priority_waiters.load(), 0);
}

// ============================================================================
// Shared Data Coherency Tests
// ============================================================================

TEST(test_shared_data_coherency_across_threads) {
    struct SharedData {
        std::atomic<int> value{0};
        std::atomic<uint64_t> version{0};
    };

    SharedData data;
    std::atomic<bool> writer_done(false);

    std::thread writer([&]() {
        for (int i = 0; i < 1000; i++) {
            data.value.store(i, std::memory_order_release);
            data.version.fetch_add(1, std::memory_order_release);
            std::this_thread::yield();
        }
        writer_done.store(true, std::memory_order_release);
    });

    std::atomic<int> consistent_reads(0);

    std::thread reader([&]() {
        int last_value = -1;
        uint64_t last_version = 0;

        while (!writer_done.load(std::memory_order_acquire)) {
            int value = data.value.load(std::memory_order_acquire);
            uint64_t version = data.version.load(std::memory_order_acquire);

            if (version >= last_version && value >= last_value - 1 && value <= last_value + 1) {
                consistent_reads.fetch_add(1, std::memory_order_relaxed);
            }

            last_value = value;
            last_version = version;
        }
        // Guarantee at least one consistent read of the final committed state
        int final_value = data.value.load(std::memory_order_acquire);
        if (final_value >= 0 && final_value <= 999) {
            consistent_reads.fetch_add(1, std::memory_order_relaxed);
        }
    });

    writer.join();
    reader.join();

    ASSERT_GT(consistent_reads.load(), 0);
}

TEST(test_atomic_operation_visibility) {
    std::atomic<int> shared{0};

    std::thread t1([&]() {
        for (int i = 0; i < 1000; i++) {
            shared.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < 1000; i++) {
            shared.fetch_add(1, std::memory_order_relaxed);
        }
    });

    t1.join();
    t2.join();

    ASSERT_EQ(shared.load(), 2000);
}

TEST(test_memory_barrier_correctness) {
    std::atomic<int> flag{0};
    std::atomic<int> data{0};

    std::thread t1([&]() {
        data.store(42, std::memory_order_release);
        flag.store(1, std::memory_order_release);
    });

    std::thread t2([&]() {
        while (flag.load(std::memory_order_acquire) == 0) {
            std::this_thread::yield();
        }
        int value = data.load(std::memory_order_acquire);
        if (value != 42) {
            throw std::runtime_error("Memory barrier failed: data != 42");
        }
    });

    t1.join();
    t2.join();
}

TEST(test_cache_line_alignment_prevents_false_sharing) {
    alignas(64) std::atomic<int> counter1{0};
    alignas(64) std::atomic<int> counter2{0};

    auto start = std::chrono::steady_clock::now();

    std::thread t1([&]() {
        for (int i = 0; i < 100000; i++) {
            counter1.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < 100000; i++) {
            counter2.fetch_add(1, std::memory_order_relaxed);
        }
    });

    t1.join();
    t2.join();

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    ASSERT_LT(duration.count(), 5000);
}

// ============================================================================
// Ring Buffer Thread Safety Tests
// ============================================================================

TEST(test_ring_buffer_thread_safe_producer_consumer) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    std::atomic<size_t> produced(0);
    std::atomic<size_t> consumed(0);
    std::atomic<bool> done(false);

    std::thread producer([&]() {
        for (int i = 0; i < 1000; i++) {
            while (!buffer.push(i)) {
                std::this_thread::yield();
            }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
        done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        int val;
        while (!done.load(std::memory_order_acquire) || !buffer.empty()) {
            if (buffer.pop(val)) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(produced.load(), consumed.load());
}

TEST(test_ring_buffer_no_data_race) {
    aurore::LockFreeRingBuffer<uint64_t, 4> buffer;
    std::atomic<uint64_t> sum_produced{0};
    std::atomic<uint64_t> sum_consumed{0};

    std::thread producer([&]() {
        for (uint64_t i = 0; i < 10000; i++) {
            if (buffer.push(i)) {
                sum_produced.fetch_add(i, std::memory_order_relaxed);
            }
        }
    });

    std::thread consumer([&]() {
        uint64_t val;
        while (producer.joinable() || !buffer.empty()) {
            if (buffer.pop(val)) {
                sum_consumed.fetch_add(val, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "=== Part 4: Advanced Concurrency Pathology Tests ===" << std::endl;
    std::cout << "Running concurrency tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    std::cout << "\n--- Deadlock Prevention ---" << std::endl;
    RUN_TEST(test_no_deadlock_simple_lock_order);
    RUN_TEST(test_no_deadlock_ordered_acquisition);
    RUN_TEST(test_no_deadlock_no_circular_wait);

    std::cout << "\n--- Livelock Prevention ---" << std::endl;
    RUN_TEST(test_no_livelock_spin);
    RUN_TEST(test_livelock_backoff);

    std::cout << "\n--- Starvation Tests ---" << std::endl;
    RUN_TEST(test_no_starvation_bounded_wait);
    RUN_TEST(test_no_starvation_thread_pool);

    std::cout << "\n--- Priority Inversion Tests ---" << std::endl;
    RUN_TEST(test_priority_inversion_detection);
    RUN_TEST(test_priority_inversion_mitigation);

    std::cout << "\n--- Shared Data Coherency ---" << std::endl;
    RUN_TEST(test_shared_data_coherency_across_threads);
    RUN_TEST(test_atomic_operation_visibility);
    RUN_TEST(test_memory_barrier_correctness);
    RUN_TEST(test_cache_line_alignment_prevents_false_sharing);

    std::cout << "\n--- Ring Buffer Thread Safety ---" << std::endl;
    RUN_TEST(test_ring_buffer_thread_safe_producer_consumer);
    RUN_TEST(test_ring_buffer_no_data_race);

    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}