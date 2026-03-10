#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <signal.h>
#include <unistd.h>

#include "aurore/timing.hpp"
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
#define ASSERT_NEAR(a, b, tol) do { \
    auto diff = std::abs(static_cast<int64_t>(a) - static_cast<int64_t>(b)); \
    if (diff > static_cast<int64_t>(tol)) \
        throw std::runtime_error("Assertion failed: " #a " not near " #b); \
} while(0)
#define ASSERT_GE(a, b) do { if (!((a) >= (b))) throw std::runtime_error("Assertion failed: " #a " < " #b); } while(0)
#define ASSERT_GT(a, b) do { if (!((a) > (b))) throw std::runtime_error("Assertion failed: " #a " <= " #b); } while(0)

void signal_handler(int) {}

}  // anonymous namespace

using namespace aurore;

// 1. Wrap-Safe Diff
TEST(test_wrap_safe_diff) {
    TimestampNs t0 = 0xFFFFFFFFFFFFFFFFULL;
    TimestampNs t1 = 0;
    
    ASSERT_EQ(timestamp_diff_ns(t1, t0), 1);
    ASSERT_EQ(timestamp_diff_ns(t0, t1), -1);
}

// 2. Timestamp Window Wrap
TEST(test_timestamp_window_wrap) {
    TimestampNs ref = 0xFFFFFFFFFFFFFFFFULL;
    TimestampNs t_early = 0xFFFFFFFFFFFFFFFEULL;
    TimestampNs t_late = 0ULL;
    
    ASSERT_TRUE(timestamp_within_window(t_early, ref, 1));
    ASSERT_TRUE(timestamp_within_window(t_late, ref, 1));
    ASSERT_FALSE(timestamp_within_window(t_late, ref, 0));
}

// 3. Sleep Interruption (EINTR)
TEST(test_sleep_interruption) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, nullptr);
    
    ThreadTiming timing(100000000); // 100ms
    timing.init(100000000);
    
    std::thread t([&]() {
        usleep(10000); // 10ms
        kill(getpid(), SIGUSR1);
    });
    
    TimestampNs start = get_timestamp();
    timing.wait(); // Should restart on EINTR
    TimestampNs end = get_timestamp();
    
    t.join();
    
    // Should have waited at least ~100ms
    ASSERT_GE(timestamp_diff_ns(end, start), 80000000LL);
}

// 4. Drift Accumulation (Logic check)
TEST(test_drift_accumulation) {
    uint64_t period = 8333333; // 120Hz
    uint64_t count = 100000ULL;
    uint64_t total_expected_ns = period * count;
    
    uint64_t seconds = total_expected_ns / 1000000000ULL;
    uint64_t nsecs = total_expected_ns % 1000000000ULL;
    
    ASSERT_EQ(seconds * 1000000000ULL + nsecs, total_expected_ns);
}

// 5. Emplace Safety
struct ComplexStruct {
    uint64_t a_val;
    double b_val;
    char c_str[32];
    
    ComplexStruct(uint64_t a, double b, const char* s) : a_val(a), b_val(b) {
        std::strncpy(c_str, s, sizeof(c_str));
    }
    ComplexStruct() : a_val(0), b_val(0.0) {
        c_str[0] = '\0';
    }
};

TEST(test_emplace_safety) {
    LockFreeRingBuffer<ComplexStruct, 4> buffer;
    ASSERT_TRUE(buffer.emplace(123ULL, 45.67, "hello"));
    
    ComplexStruct out;
    ASSERT_TRUE(buffer.pop(out));
    ASSERT_EQ(out.a_val, 123ULL);
    ASSERT_EQ(out.b_val, 45.67);
    ASSERT_TRUE(std::strcmp(out.c_str, "hello") == 0);
}

// 6. MPMC Stress
TEST(test_mpmc_stress) {
    MPMCRingBuffer<uint64_t, 1024> buffer;
    const int num_producers = 2; // Reduced for CI speed
    const int num_consumers = 2;
    const uint64_t items_per_producer = 10000;
    const uint64_t total_items = num_producers * items_per_producer;
    
    std::atomic<uint64_t> consumed_count{0};
    std::atomic<bool> producers_done{false};
    
    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&buffer, items_per_producer]() {
            for (uint64_t j = 0; j < items_per_producer; ++j) {
                while (!buffer.push(j)) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    std::vector<std::thread> consumers;
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&buffer, &consumed_count, &producers_done]() {
            uint64_t val;
            while (!producers_done || !buffer.empty()) {
                if (buffer.pop(val)) {
                    consumed_count++;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    for (auto& t : producers) t.join();
    producers_done = true;
    for (auto& t : consumers) t.join();
    
    ASSERT_EQ(consumed_count.load(), total_items);
}

// 7. Cache Alignment
TEST(test_cache_alignment) {
    LockFreeRingBuffer<uint64_t, 4> buffer;
    ASSERT_GE(sizeof(buffer), 2 * CACHE_LINE_SIZE);
    ASSERT_EQ(alignof(LockFreeRingBuffer<uint64_t, 4>), CACHE_LINE_SIZE);
}

// 8. Deadline Overflow
TEST(test_deadline_overflow) {
    DeadlineMonitor deadline(1000);
    deadline.start();
    usleep(2000);
    deadline.stop();
    ASSERT_TRUE(deadline.exceeded());
    ASSERT_GE(deadline.elapsed_ns(), 1000000ULL);
}

// 9. Budget Zero
TEST(test_budget_zero) {
    DeadlineMonitor deadline(0);
    deadline.start();
    ASSERT_EQ(deadline.remaining_ns(), 0);
    ASSERT_TRUE(deadline.exceeded());
}

// 10. FPS Extremes
TEST(test_fps_extremes) {
    FrameRateCalculator calc(120);
    TimestampNs now = 1000000000ULL;
    for (int i = 0; i < 11; ++i) {
        calc.record_frame(now);
        now += 1000000ULL; // 1000Hz
    }
    ASSERT_NEAR(calc.fps(), 1000.0, 10.0);
}

// 11. Clock Jumps
TEST(test_clock_jumps) {
    TimestampNs now = 1000ULL;
    TimestampNs earlier = 2000ULL; 
    ASSERT_EQ(timestamp_diff_ns(earlier, now), 1000LL);
}

// 12. Init Re-entry
TEST(test_init_reentry) {
    ThreadTiming timing;
    timing.init(1000000);
    ASSERT_EQ(timing.period_ns(), 1000000ULL);
    timing.init(2000000);
    ASSERT_EQ(timing.period_ns(), 2000000ULL);
}

// 13. Wait Uninitialized
TEST(test_wait_uninitialized) {
    ThreadTiming timing;
    bool caught = false;
    try {
        timing.wait();
    } catch (const std::logic_error&) {
        caught = true;
    }
    ASSERT_TRUE(caught);
}

// 14. Memory Barrier Stress
TEST(test_memory_barrier_stress) {
    LockFreeRingBuffer<uint64_t, 1024> buffer;
    const uint64_t count = 100000;
    
    std::thread producer([&]() {
        for (uint64_t i = 0; i < count; ++i) {
            while (!buffer.push(i)) std::this_thread::yield();
        }
    });
    
    std::thread consumer([&]() {
        uint64_t val;
        for (uint64_t i = 0; i < count; ++i) {
            while (!buffer.pop(val)) std::this_thread::yield();
            if (val != i) throw std::runtime_error("Ordering failure");
        }
    });
    
    producer.join();
    consumer.join();
}

// 15. Alignment Assert
TEST(test_alignment_assert) {
    ASSERT_TRUE(std::is_trivially_copyable_v<uint64_t>);
}

int main() {
    std::cout << "Running Core Stress tests..." << std::endl;
    RUN_TEST(test_wrap_safe_diff);
    RUN_TEST(test_timestamp_window_wrap);
    RUN_TEST(test_sleep_interruption);
    RUN_TEST(test_drift_accumulation);
    RUN_TEST(test_emplace_safety);
    RUN_TEST(test_mpmc_stress);
    RUN_TEST(test_cache_alignment);
    RUN_TEST(test_deadline_overflow);
    RUN_TEST(test_budget_zero);
    RUN_TEST(test_fps_extremes);
    RUN_TEST(test_clock_jumps);
    RUN_TEST(test_init_reentry);
    RUN_TEST(test_wait_uninitialized);
    RUN_TEST(test_memory_barrier_stress);
    RUN_TEST(test_alignment_assert);
    
    std::cout << "Tests run: " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed: " << g_tests_passed.load() << std::endl;
    return g_tests_failed.load() > 0 ? 1 : 0;
}
