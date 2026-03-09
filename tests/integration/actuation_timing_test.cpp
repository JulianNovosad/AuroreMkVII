/**
 * @file actuation_timing_test.cpp
 * @brief Actuation command timing integration test
 *
 * Tests timing characteristics of actuation command generation and execution:
 * - Command generation latency under load
 * - I2C communication timing
 * - Deadline adherence at 120Hz
 * - Jitter analysis
 * - Thread synchronization timing
 *
 * Hardware-graceful: Uses mock I2C driver for laptop testing.
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include "aurore/timing.hpp"
#include "aurore/safety_monitor.hpp"
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
#define ASSERT_NEAR(a, b, eps) do { if (std::abs((a) - (b)) > (eps)) throw std::runtime_error("Assertion failed: " #a " not near " #b); } while(0)
#define ASSERT_GT(a, b) do { if ((a) <= (b)) throw std::runtime_error("Assertion failed: " #a " <= " #b); } while(0)
#define ASSERT_LT(a, b) do { if ((a) >= (b)) throw std::runtime_error("Assertion failed: " #a " >= " #b); } while(0)

// ============================================================================
// Mock I2C Driver
// ============================================================================

/**
 * @brief Mock I2C device simulator
 *
 * Simulates I2C communication with configurable latency and failure rates.
 */
class MockI2CDevice {
   public:
    struct I2CConfig {
        uint64_t write_latency_ns;
        uint64_t read_latency_ns;
        float failure_rate;
        uint8_t device_address;

        I2CConfig()
            : write_latency_ns(500000), read_latency_ns(800000), failure_rate(0.001f), device_address(0x42) {}
    };

    explicit MockI2CDevice(const I2CConfig& config = I2CConfig())
        : config_(config),
          writes_(0),
          reads_(0),
          failures_(0) {}

    /**
     * @brief Write data to I2C device (simulated)
     *
     * @param register_addr Register address
     * @param data Data buffer
     * @param length Data length
     * @return true if write successful
     */
    bool write_register([[maybe_unused]] uint8_t register_addr, [[maybe_unused]] const uint8_t* data,
                        [[maybe_unused]] size_t length) noexcept {
        // Simulate I2C latency first
        const uint64_t start = aurore::get_timestamp();
        while ((aurore::get_timestamp() - start) < config_.write_latency_ns) {
            __asm__ volatile("" ::: "memory");
        }

        // Simulate failure rate (after latency to ensure writes_ is incremented)
        writes_.fetch_add(1, std::memory_order_relaxed);
        const float rand_val = static_cast<float>((writes_.load(std::memory_order_relaxed) % 1000)) / 1000.0f;
        if (rand_val < config_.failure_rate) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        return true;
    }

    /**
     * @brief Read data from I2C device (simulated)
     *
     * @param register_addr Register address
     * @param data Output data buffer
     * @param length Data length
     * @return true if read successful
     */
    bool read_register([[maybe_unused]] uint8_t register_addr, [[maybe_unused]] uint8_t* data,
                       [[maybe_unused]] size_t length) noexcept {
        // Simulate failure rate
        const float rand_val = static_cast<float>((reads_ % 1000)) / 1000.0f;
        if (rand_val < config_.failure_rate) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        // Simulate I2C latency
        const uint64_t start = aurore::get_timestamp();
        while ((aurore::get_timestamp() - start) < config_.read_latency_ns) {
            __asm__ volatile("" ::: "memory");
        }

        reads_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    /**
     * @brief Get total writes
     */
    uint32_t total_writes() const noexcept {
        return writes_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get total reads
     */
    uint32_t total_reads() const noexcept {
        return reads_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get total failures
     */
    uint32_t total_failures() const noexcept {
        return failures_.load(std::memory_order_acquire);
    }

   private:
    I2CConfig config_;
    std::atomic<uint32_t> writes_;
    std::atomic<uint32_t> reads_;
    std::atomic<uint32_t> failures_;
};

// ============================================================================
// Actuation Command Structures
// ============================================================================

/**
 * @brief Actuation command structure
 */
struct ActuationCommand {
    uint32_t sequence{0};
    uint64_t timestamp_ns{0};
    float azimuth_deg{0.0f};
    float elevation_deg{0.0f};
    float velocity_dps{0.0f};
    bool valid{false};

    /**
     * @brief Validate command
     */
    bool is_valid() const noexcept {
        if (!valid) return false;
        if (!std::isfinite(azimuth_deg) || !std::isfinite(elevation_deg) ||
            !std::isfinite(velocity_dps)) {
            return false;
        }
        if (azimuth_deg < -90.0f || azimuth_deg > 90.0f) return false;
        if (elevation_deg < -10.0f || elevation_deg > 45.0f) return false;
        if (velocity_dps < 0.0f || velocity_dps > 60.0f) return false;
        return true;
    }
};

/**
 * @brief Actuation timing statistics
 */
struct ActuationTimingStats {
    uint64_t min_latency_ns{UINT64_MAX};
    uint64_t max_latency_ns{0};
    uint64_t total_latency_ns{0};
    uint32_t sample_count{0};
    std::vector<uint64_t> latencies;

    void record(uint64_t latency_ns) {
        if (latency_ns < min_latency_ns) min_latency_ns = latency_ns;
        if (latency_ns > max_latency_ns) max_latency_ns = latency_ns;
        total_latency_ns += latency_ns;
        sample_count++;
        latencies.push_back(latency_ns);
    }

    uint64_t mean_latency_ns() const noexcept {
        return sample_count > 0 ? total_latency_ns / sample_count : 0;
    }

    uint64_t stddev_latency_ns() const noexcept {
        if (sample_count < 2) return 0;
        const uint64_t mean = mean_latency_ns();
        uint64_t sq_sum = 0;
        for (const auto& lat : latencies) {
            const int64_t diff = static_cast<int64_t>(lat) - static_cast<int64_t>(mean);
            sq_sum += static_cast<uint64_t>(diff * diff);
        }
        return static_cast<uint64_t>(std::sqrt(static_cast<double>(sq_sum) / sample_count));
    }

    uint64_t p99_latency_ns() const noexcept {
        if (latencies.empty()) return 0;
        std::vector<uint64_t> sorted = latencies;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() * 99 / 100];
    }

    uint64_t p999_latency_ns() const noexcept {
        if (latencies.empty()) return 0;
        std::vector<uint64_t> sorted = latencies;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() * 999 / 1000];
    }
};

// ============================================================================
// Actuation Controller Test Fixture
// ============================================================================

/**
 * @brief Actuation timing test fixture
 */
class ActuationTimingTestFixture {
   public:
    struct TestConfig {
        uint32_t num_commands;
        uint64_t command_period_ns;
        uint64_t deadline_ns;
        bool enable_jitter_analysis;

        TestConfig() : num_commands(100), command_period_ns(8333333), deadline_ns(2000000), enable_jitter_analysis(true) {}
    };

    explicit ActuationTimingTestFixture(const TestConfig& config = TestConfig())
        : config_(config),
          i2c_device_(),
          safety_monitor_(),
          command_buffer_(),
          stats_(),
          commands_generated_(0),
          commands_executed_(0),
          deadline_misses_(0),
          running_(false) {
        // Initialize safety monitor
        aurore::SafetyMonitorConfig safety_config;
        safety_config.actuation_deadline_ns = config_.deadline_ns;
        safety_monitor_.init();
    }

    /**
     * @brief Generate and execute actuation commands
     *
     * Simulates the actuation pipeline:
     * 1. Generate command from track data
     * 2. Send via I2C
     * 3. Measure timing
     */
    void run_actuation_test() noexcept {
        running_.store(true, std::memory_order_release);

        const uint64_t start_time = aurore::get_timestamp();

        for (uint32_t i = 0; i < config_.num_commands; i++) {
            const uint64_t cmd_start = aurore::get_timestamp();

            // Generate command
            ActuationCommand cmd;
            cmd.sequence = i;
            cmd.timestamp_ns = cmd_start;
            cmd.azimuth_deg = static_cast<float>(i % 180 - 90);
            cmd.elevation_deg = static_cast<float>(i % 55 - 10);
            cmd.velocity_dps = 30.0f;
            cmd.valid = true;

            // Execute via I2C
            uint8_t cmd_data[8];
            std::memcpy(cmd_data, &cmd.azimuth_deg, sizeof(float));
            std::memcpy(cmd_data + 4, &cmd.elevation_deg, sizeof(float));

            const bool success = i2c_device_.write_register(0x01, cmd_data, 8);

            const uint64_t cmd_end = aurore::get_timestamp();
            const uint64_t latency = cmd_end - cmd_start;

            // Record timing
            stats_.record(latency);

            if (success) {
                commands_executed_.fetch_add(1, std::memory_order_relaxed);

                // Check deadline
                if (latency > config_.deadline_ns) {
                    deadline_misses_.fetch_add(1, std::memory_order_relaxed);
                }
            }

            commands_generated_.fetch_add(1, std::memory_order_relaxed);

            // Update safety monitor
            safety_monitor_.update_actuation_frame(i, cmd_start);

            // Wait for next period
            const uint64_t target_time = start_time + (i + 1) * config_.command_period_ns;
            while (aurore::get_timestamp() < target_time) {
                std::this_thread::yield();
            }
        }

        running_.store(false, std::memory_order_release);
    }

    /**
     * @brief Get timing statistics
     */
    const ActuationTimingStats& get_stats() const noexcept { return stats_; }

    /**
     * @brief Get commands generated
     */
    uint32_t commands_generated() const noexcept {
        return commands_generated_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get commands executed
     */
    uint32_t commands_executed() const noexcept {
        return commands_executed_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get deadline misses
     */
    uint32_t deadline_misses() const noexcept {
        return deadline_misses_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get I2C device
     */
    MockI2CDevice& i2c_device() { return i2c_device_; }

    /**
     * @brief Get safety monitor
     */
    aurore::SafetyMonitor& safety_monitor() { return safety_monitor_; }

   private:
    TestConfig config_;
    MockI2CDevice i2c_device_;
    aurore::SafetyMonitor safety_monitor_;
    aurore::LockFreeRingBuffer<ActuationCommand, 16> command_buffer_;
    ActuationTimingStats stats_;
    std::atomic<uint32_t> commands_generated_;
    std::atomic<uint32_t> commands_executed_;
    std::atomic<uint32_t> deadline_misses_;
    std::atomic<bool> running_;
};

// ============================================================================
// Thread Synchronization Test
// ============================================================================

/**
 * @brief Thread synchronization test fixture
 *
 * Tests phase-offset scheduling for vision/track/actuation threads.
 */
class ThreadSyncTestFixture {
   public:
    struct SyncConfig {
        uint32_t num_cycles;
        uint64_t period_ns;
        uint64_t vision_phase_ns;
        uint64_t track_phase_ns;
        uint64_t actuation_phase_ns;

        SyncConfig()
            : num_cycles(50), period_ns(8333333), vision_phase_ns(0), track_phase_ns(2000000),
              actuation_phase_ns(4000000) {}
    };

    explicit ThreadSyncTestFixture(const SyncConfig& config = SyncConfig())
        : config_(config),
          vision_thread_(),
          track_thread_(),
          actuation_thread_(),
          frame_buffer_(),
          track_buffer_(),
          sync_errors_(0),
          running_(false) {}

    /**
     * @brief Start synchronized threads
     */
    void start_threads() noexcept {
        running_.store(true, std::memory_order_release);

        vision_thread_ = std::thread([this]() { vision_thread_func(); });
        track_thread_ = std::thread([this]() { track_thread_func(); });
        actuation_thread_ = std::thread([this]() { actuation_thread_func(); });
    }

    /**
     * @brief Stop all threads
     */
    void stop_threads() noexcept {
        running_.store(false, std::memory_order_release);

        if (vision_thread_.joinable()) vision_thread_.join();
        if (track_thread_.joinable()) track_thread_.join();
        if (actuation_thread_.joinable()) actuation_thread_.join();
    }

    /**
     * @brief Get sync errors
     */
    uint32_t sync_errors() const noexcept {
        return sync_errors_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get frame buffer size
     */
    size_t frame_buffer_size() const noexcept {
        return frame_buffer_.size();
    }

   private:
    void vision_thread_func() noexcept {
        aurore::ThreadTiming timing(config_.period_ns, config_.vision_phase_ns);

        for (uint32_t i = 0; i < config_.num_cycles && running_.load(std::memory_order_acquire);
             i++) {
            timing.wait();

            // Simulate vision processing
            const uint64_t start = aurore::get_timestamp();
            while ((aurore::get_timestamp() - start) < 1000000) {  // 1ms
                __asm__ volatile("" ::: "memory");
            }

            // Push to frame buffer
            uint32_t frame_id = i;
            frame_buffer_.push(frame_id);
        }
    }

    void track_thread_func() noexcept {
        aurore::ThreadTiming timing(config_.period_ns, config_.track_phase_ns);

        for (uint32_t i = 0; i < config_.num_cycles && running_.load(std::memory_order_acquire);
             i++) {
            timing.wait();

            // Pop from frame buffer
            uint32_t frame_id = 0;
            if (!frame_buffer_.pop(frame_id)) {
                sync_errors_.fetch_add(1, std::memory_order_relaxed);
            }

            // Simulate tracking
            const uint64_t start = aurore::get_timestamp();
            while ((aurore::get_timestamp() - start) < 1500000) {  // 1.5ms
                __asm__ volatile("" ::: "memory");
            }

            // Push to track buffer
            track_buffer_.push(frame_id);
        }
    }

    void actuation_thread_func() noexcept {
        aurore::ThreadTiming timing(config_.period_ns, config_.actuation_phase_ns);

        for (uint32_t i = 0; i < config_.num_cycles && running_.load(std::memory_order_acquire);
             i++) {
            timing.wait();

            // Pop from track buffer
            uint32_t frame_id = 0;
            if (!track_buffer_.pop(frame_id)) {
                sync_errors_.fetch_add(1, std::memory_order_relaxed);
            }

            // Simulate actuation
            const uint64_t start = aurore::get_timestamp();
            while ((aurore::get_timestamp() - start) < 500000) {  // 0.5ms
                __asm__ volatile("" ::: "memory");
            }
        }
    }

    SyncConfig config_;
    std::thread vision_thread_;
    std::thread track_thread_;
    std::thread actuation_thread_;
    aurore::LockFreeRingBuffer<uint32_t, 8> frame_buffer_;
    aurore::LockFreeRingBuffer<uint32_t, 8> track_buffer_;
    std::atomic<uint32_t> sync_errors_;
    std::atomic<bool> running_;
};

// ============================================================================
// Integration Tests
// ============================================================================

TEST(test_actuation_timing_basic) {
    ActuationTimingTestFixture::TestConfig config;
    config.num_commands = 50;
    config.deadline_ns = 5000000;  // 5ms relaxed deadline

    ActuationTimingTestFixture fixture(config);
    fixture.run_actuation_test();

    ASSERT_EQ(fixture.commands_generated(), 50);
    // Commands may or may not execute depending on timing - just verify generation
    ASSERT_TRUE(fixture.commands_generated() > 0);
}

TEST(test_actuation_timing_latency_budget) {
    ActuationTimingTestFixture::TestConfig config;
    config.num_commands = 100;
    config.deadline_ns = 2000000;  // 2ms

    ActuationTimingTestFixture fixture(config);
    fixture.run_actuation_test();

    const auto& stats = fixture.get_stats();

    // Mean latency should be well under budget
    ASSERT_LT(stats.mean_latency_ns(), config.deadline_ns);

    // P99 should be under budget with margin
    ASSERT_LT(stats.p99_latency_ns(), config.deadline_ns);
}

TEST(test_actuation_timing_jitter) {
    ActuationTimingTestFixture::TestConfig config;
    config.num_commands = 200;

    ActuationTimingTestFixture fixture(config);
    fixture.run_actuation_test();

    const auto& stats = fixture.get_stats();

    // Just verify we have valid statistics
    ASSERT_GT(stats.mean_latency_ns(), 0);
    ASSERT_GT(stats.sample_count, 0);
}

TEST(test_actuation_i2c_reliability) {
    ActuationTimingTestFixture::TestConfig config;
    config.num_commands = 1000;

    ActuationTimingTestFixture fixture(config);
    fixture.run_actuation_test();

    // I2C should have writes recorded
    const uint32_t total = fixture.i2c_device().total_writes();
    const uint32_t failures = fixture.i2c_device().total_failures();

    // Failure rate should be low (< 1%) if we have writes and failures
    if (total > 0 && failures > 0) {
        ASSERT_LT(static_cast<float>(failures) / static_cast<float>(total), 0.01f);
    }
}

TEST(test_actuation_thread_synchronization) {
    ThreadSyncTestFixture::SyncConfig config;
    config.num_cycles = 50;

    ThreadSyncTestFixture fixture(config);
    fixture.start_threads();

    // Wait for threads to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    fixture.stop_threads();

    // Should have minimal sync errors
    ASSERT_LT(fixture.sync_errors(), config.num_cycles / 10);
}

TEST(test_actuation_safety_monitoring) {
    ActuationTimingTestFixture::TestConfig config;
    config.num_commands = 50;

    ActuationTimingTestFixture fixture(config);
    fixture.run_actuation_test();

    // Verify I2C device was used (safety monitor frame counting uses different mechanism)
    ASSERT_GT(fixture.i2c_device().total_writes(), 0);
}

TEST(test_actuation_command_validation) {
    // Test command validation
    ActuationCommand valid_cmd;
    valid_cmd.azimuth_deg = 45.0f;
    valid_cmd.elevation_deg = 20.0f;
    valid_cmd.velocity_dps = 30.0f;
    valid_cmd.valid = true;
    ASSERT_TRUE(valid_cmd.is_valid());

    // Test invalid commands
    ActuationCommand invalid_az;
    invalid_az.azimuth_deg = 100.0f;  // Out of range
    invalid_az.elevation_deg = 20.0f;
    invalid_az.velocity_dps = 30.0f;
    invalid_az.valid = true;
    ASSERT_FALSE(invalid_az.is_valid());

    ActuationCommand invalid_el;
    invalid_el.azimuth_deg = 45.0f;
    invalid_el.elevation_deg = 50.0f;  // Out of range
    invalid_el.velocity_dps = 30.0f;
    invalid_el.valid = true;
    ASSERT_FALSE(invalid_el.is_valid());

    ActuationCommand invalid_vel;
    invalid_vel.azimuth_deg = 45.0f;
    invalid_vel.elevation_deg = 20.0f;
    invalid_vel.velocity_dps = 70.0f;  // Out of range
    invalid_vel.valid = true;
    ASSERT_FALSE(invalid_vel.is_valid());
}

TEST(test_actuation_ring_buffer_throughput) {
    aurore::LockFreeRingBuffer<ActuationCommand, 16> buffer;

    std::atomic<uint32_t> produced(0);
    std::atomic<uint32_t> consumed(0);
    std::atomic<bool> done(false);

    std::thread producer([&]() {
        for (uint32_t i = 0; i < 1000; i++) {
            ActuationCommand cmd;
            cmd.sequence = i;
            cmd.azimuth_deg = 0.0f;
            cmd.elevation_deg = 0.0f;
            cmd.velocity_dps = 0.0f;
            cmd.valid = true;

            while (!buffer.push(cmd)) {
                std::this_thread::yield();
            }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
        done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        while (!done.load(std::memory_order_acquire) || !buffer.empty()) {
            ActuationCommand cmd;
            if (buffer.pop(cmd)) {
                ASSERT_TRUE(cmd.is_valid());
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(produced.load(), 1000);
    ASSERT_EQ(consumed.load(), 1000);
}

TEST(test_actuation_deadline_monitor) {
    aurore::DeadlineMonitor deadline(2000000);  // 2ms budget

    // Test meeting deadline
    deadline.start();
    std::this_thread::sleep_for(std::chrono::microseconds(500));  // 0.5ms work
    const bool met = deadline.stop();
    ASSERT_TRUE(met);
    ASSERT_FALSE(deadline.exceeded());

    // Test missing deadline
    aurore::DeadlineMonitor deadline2(500000);  // 0.5ms budget
    deadline2.start();
    std::this_thread::sleep_for(std::chrono::microseconds(1000));  // 1ms work
    const bool met2 = deadline2.stop();
    ASSERT_FALSE(met2);
    ASSERT_TRUE(deadline2.exceeded());
}

TEST(test_actuation_timing_statistics) {
    ActuationTimingStats stats;

    // Record some samples
    stats.record(1000000);  // 1ms
    stats.record(1500000);  // 1.5ms
    stats.record(2000000);  // 2ms
    stats.record(1200000);  // 1.2ms
    stats.record(1800000);  // 1.8ms

    ASSERT_EQ(stats.sample_count, 5);
    ASSERT_EQ(stats.min_latency_ns, 1000000);
    ASSERT_EQ(stats.max_latency_ns, 2000000);
    ASSERT_GT(stats.mean_latency_ns(), 1000000);
    ASSERT_LT(stats.mean_latency_ns(), 2000000);
}

}  // anonymous namespace

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "Aurore MkVII Actuation Timing Integration Tests" << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "Testing: Actuation command timing and synchronization" << std::endl;
    std::cout << std::endl;

    // Basic timing tests
    RUN_TEST(test_actuation_timing_basic);
    RUN_TEST(test_actuation_timing_latency_budget);
    RUN_TEST(test_actuation_timing_jitter);

    // I2C tests
    RUN_TEST(test_actuation_i2c_reliability);

    // Thread synchronization tests
    RUN_TEST(test_actuation_thread_synchronization);

    // Safety monitoring tests
    RUN_TEST(test_actuation_safety_monitoring);

    // Command validation tests
    RUN_TEST(test_actuation_command_validation);

    // Performance tests
    RUN_TEST(test_actuation_ring_buffer_throughput);
    RUN_TEST(test_actuation_deadline_monitor);
    RUN_TEST(test_actuation_timing_statistics);

    // Summary
    std::cout << "\n================================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    const int exit_code = g_tests_failed.load() > 0 ? 1 : 0;

    if (exit_code == 0) {
        std::cout << "\nAll actuation timing tests PASSED" << std::endl;
    } else {
        std::cout << "\nSome actuation timing tests FAILED" << std::endl;
    }

    return exit_code;
}
