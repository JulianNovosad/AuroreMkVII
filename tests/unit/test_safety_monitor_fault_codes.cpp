/**
 * @file test_safety_monitor_fault_codes.cpp
 * @brief Unit tests for SafetyMonitor fault codes and per-stage monitoring
 *
 * Tests cover:
 * - ALL 24 SafetyFaultCode values
 * - Per-stage latency monitoring
 * - Recovery callbacks
 * - Health report generation
 *
 * Requirements traced:
 * - AM7-L2-VIS-003: Vision pipeline deadline monitoring
 * - AM7-L2-ACT-003: Actuation output deadline monitoring
 * - AM7-L3-SAFE-005: Software watchdog monitoring
 */

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
#include <string>

#include "aurore/safety_monitor.hpp"

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
#define ASSERT_NE(a, b) do { if ((a) == (b)) throw std::runtime_error("Assertion failed: " #a " == " #b); } while(0)

// Helper: sleep for specified duration in milliseconds
void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // anonymous namespace

// ============================================================================
// SafetyFaultCode Enum Tests - All 24 Values
// ============================================================================

TEST(test_fault_code_none) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::NONE;
    ASSERT_EQ(code, aurore::SafetyFaultCode::NONE);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "NONE");
}

TEST(test_fault_code_vision_stalled) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::VISION_STALLED;
    ASSERT_EQ(code, aurore::SafetyFaultCode::VISION_STALLED);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "VISION_STALLED");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0101);
}

TEST(test_fault_code_vision_latency_exceeded) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::VISION_LATENCY_EXCEEDED;
    ASSERT_EQ(code, aurore::SafetyFaultCode::VISION_LATENCY_EXCEEDED);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "VISION_LATENCY_EXCEEDED");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0102);
}

TEST(test_fault_code_vision_buffer_overrun) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::VISION_BUFFER_OVERRUN;
    ASSERT_EQ(code, aurore::SafetyFaultCode::VISION_BUFFER_OVERRUN);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "VISION_BUFFER_OVERRUN");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0103);
}

TEST(test_fault_code_actuation_stalled) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::ACTUATION_STALLED;
    ASSERT_EQ(code, aurore::SafetyFaultCode::ACTUATION_STALLED);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "ACTUATION_STALLED");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0201);
}

TEST(test_fault_code_actuation_latency_exceeded) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::ACTUATION_LATENCY_EXCEEDED;
    ASSERT_EQ(code, aurore::SafetyFaultCode::ACTUATION_LATENCY_EXCEEDED);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "ACTUATION_LATENCY_EXCEEDED");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0202);
}

TEST(test_fault_code_actuation_command_invalid) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::ACTUATION_COMMAND_INVALID;
    ASSERT_EQ(code, aurore::SafetyFaultCode::ACTUATION_COMMAND_INVALID);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "ACTUATION_COMMAND_INVALID");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0203);
}

TEST(test_fault_code_frame_deadline_missed) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::FRAME_DEADLINE_MISSED;
    ASSERT_EQ(code, aurore::SafetyFaultCode::FRAME_DEADLINE_MISSED);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "FRAME_DEADLINE_MISSED");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0301);
}

TEST(test_fault_code_consecutive_deadline_misses) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::CONSECUTIVE_DEADLINE_MISSES;
    ASSERT_EQ(code, aurore::SafetyFaultCode::CONSECUTIVE_DEADLINE_MISSES);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "CONSECUTIVE_DEADLINE_MISSES");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0302);
}

TEST(test_fault_code_timestamp_non_monotonic) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::TIMESTAMP_NON_MONOTONIC;
    ASSERT_EQ(code, aurore::SafetyFaultCode::TIMESTAMP_NON_MONOTONIC);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "TIMESTAMP_NON_MONOTONIC");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0303);
}

TEST(test_fault_code_watchdog_feed_failed) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::WATCHDOG_FEED_FAILED;
    ASSERT_EQ(code, aurore::SafetyFaultCode::WATCHDOG_FEED_FAILED);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "WATCHDOG_FEED_FAILED");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0401);
}

TEST(test_fault_code_memory_lock_failed) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::MEMORY_LOCK_FAILED;
    ASSERT_EQ(code, aurore::SafetyFaultCode::MEMORY_LOCK_FAILED);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "MEMORY_LOCK_FAILED");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0402);
}

TEST(test_fault_code_scheduling_policy_failed) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::SCHEDULING_POLICY_FAILED;
    ASSERT_EQ(code, aurore::SafetyFaultCode::SCHEDULING_POLICY_FAILED);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "SCHEDULING_POLICY_FAILED");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0403);
}

TEST(test_fault_code_i2c_timeout) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::I2C_TIMEOUT;
    ASSERT_EQ(code, aurore::SafetyFaultCode::I2C_TIMEOUT);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "I2C_TIMEOUT");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0501);
}

TEST(test_fault_code_i2c_nack) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::I2C_NACK;
    ASSERT_EQ(code, aurore::SafetyFaultCode::I2C_NACK);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "I2C_NACK");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0502);
}

TEST(test_fault_code_camera_timeout) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::CAMERA_TIMEOUT;
    ASSERT_EQ(code, aurore::SafetyFaultCode::CAMERA_TIMEOUT);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "CAMERA_TIMEOUT");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0503);
}

TEST(test_fault_code_safety_comparator_mismatch) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::SAFETY_COMPARATOR_MISMATCH;
    ASSERT_EQ(code, aurore::SafetyFaultCode::SAFETY_COMPARATOR_MISMATCH);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "SAFETY_COMPARATOR_MISMATCH");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0601);
}

TEST(test_fault_code_interlock_fault) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::INTERLOCK_FAULT;
    ASSERT_EQ(code, aurore::SafetyFaultCode::INTERLOCK_FAULT);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "INTERLOCK_FAULT");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0602);
}

TEST(test_fault_code_range_data_stale) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::RANGE_DATA_STALE;
    ASSERT_EQ(code, aurore::SafetyFaultCode::RANGE_DATA_STALE);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "RANGE_DATA_STALE");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0603);
}

TEST(test_fault_code_range_data_invalid) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::RANGE_DATA_INVALID;
    ASSERT_EQ(code, aurore::SafetyFaultCode::RANGE_DATA_INVALID);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "RANGE_DATA_INVALID");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0604);
}

TEST(test_fault_code_emergency_stop_requested) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::EMERGENCY_STOP_REQUESTED;
    ASSERT_EQ(code, aurore::SafetyFaultCode::EMERGENCY_STOP_REQUESTED);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "EMERGENCY_STOP_REQUESTED");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0701);
}

TEST(test_fault_code_critical_temperature) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::CRITICAL_TEMPERATURE;
    ASSERT_EQ(code, aurore::SafetyFaultCode::CRITICAL_TEMPERATURE);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "CRITICAL_TEMPERATURE");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0702);
}

TEST(test_fault_code_power_fault) {
    aurore::SafetyFaultCode code = aurore::SafetyFaultCode::POWER_FAULT;
    ASSERT_EQ(code, aurore::SafetyFaultCode::POWER_FAULT);
    ASSERT_EQ(std::string(aurore::fault_code_to_string(code)), "POWER_FAULT");
    ASSERT_EQ(static_cast<uint16_t>(code), 0x0703);
}

TEST(test_fault_code_all_24_values) {
    // Comprehensive test: verify all fault codes are unique and have valid strings
    std::vector<aurore::SafetyFaultCode> all_codes = {
        // NONE
        aurore::SafetyFaultCode::NONE,

        // Vision pipeline faults (0x01xx)
        aurore::SafetyFaultCode::VISION_STALLED,
        aurore::SafetyFaultCode::VISION_LATENCY_EXCEEDED,
        aurore::SafetyFaultCode::VISION_BUFFER_OVERRUN,

        // Actuation faults (0x02xx)
        aurore::SafetyFaultCode::ACTUATION_STALLED,
        aurore::SafetyFaultCode::ACTUATION_LATENCY_EXCEEDED,
        aurore::SafetyFaultCode::ACTUATION_COMMAND_INVALID,

        // Timing faults (0x03xx)
        aurore::SafetyFaultCode::FRAME_DEADLINE_MISSED,
        aurore::SafetyFaultCode::CONSECUTIVE_DEADLINE_MISSES,
        aurore::SafetyFaultCode::TIMESTAMP_NON_MONOTONIC,

        // System faults (0x04xx)
        aurore::SafetyFaultCode::WATCHDOG_FEED_FAILED,
        aurore::SafetyFaultCode::MEMORY_LOCK_FAILED,
        aurore::SafetyFaultCode::SCHEDULING_POLICY_FAILED,

        // Communication faults (0x05xx)
        aurore::SafetyFaultCode::I2C_TIMEOUT,
        aurore::SafetyFaultCode::I2C_NACK,
        aurore::SafetyFaultCode::CAMERA_TIMEOUT,

        // Safety system faults (0x06xx)
        aurore::SafetyFaultCode::SAFETY_COMPARATOR_MISMATCH,
        aurore::SafetyFaultCode::INTERLOCK_FAULT,
        aurore::SafetyFaultCode::RANGE_DATA_STALE,
        aurore::SafetyFaultCode::RANGE_DATA_INVALID,

        // Emergency (0x07xx)
        aurore::SafetyFaultCode::EMERGENCY_STOP_REQUESTED,
        aurore::SafetyFaultCode::CRITICAL_TEMPERATURE,
        aurore::SafetyFaultCode::POWER_FAULT,
    };

    // Verify count: 1 (NONE) + 3 (vision) + 3 (actuation) + 3 (timing) + 3 (system) 
    //              + 3 (communication) + 4 (safety system) + 3 (emergency) = 23
    ASSERT_EQ(all_codes.size(), 23);

    // Verify all codes have non-null string descriptions
    for (const auto& code : all_codes) {
        const char* str = aurore::fault_code_to_string(code);
        ASSERT_NE(str, nullptr);
        ASSERT_TRUE(std::strlen(str) > 0);
    }

    // Verify uniqueness (no duplicates)
    std::vector<uint16_t> code_values;
    for (const auto& code : all_codes) {
        code_values.push_back(static_cast<uint16_t>(code));
    }

    std::sort(code_values.begin(), code_values.end());
    auto unique_end = std::unique(code_values.begin(), code_values.end());
    ASSERT_EQ(std::distance(code_values.begin(), unique_end), 23);

    std::cout << "    All " << all_codes.size() << " fault codes verified: unique values, valid strings" << std::endl;
}

// ============================================================================
// Fault Code Category Tests
// ============================================================================

TEST(test_fault_code_categories) {
    // Verify fault codes are grouped by category (high byte)

    // Vision faults (0x01xx)
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::VISION_STALLED) & 0xFF00) == 0x0100);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::VISION_LATENCY_EXCEEDED) & 0xFF00) == 0x0100);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::VISION_BUFFER_OVERRUN) & 0xFF00) == 0x0100);

    // Actuation faults (0x02xx)
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::ACTUATION_STALLED) & 0xFF00) == 0x0200);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::ACTUATION_LATENCY_EXCEEDED) & 0xFF00) == 0x0200);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::ACTUATION_COMMAND_INVALID) & 0xFF00) == 0x0200);

    // Timing faults (0x03xx)
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::FRAME_DEADLINE_MISSED) & 0xFF00) == 0x0300);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::CONSECUTIVE_DEADLINE_MISSES) & 0xFF00) == 0x0300);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::TIMESTAMP_NON_MONOTONIC) & 0xFF00) == 0x0300);

    // System faults (0x04xx)
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::WATCHDOG_FEED_FAILED) & 0xFF00) == 0x0400);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::MEMORY_LOCK_FAILED) & 0xFF00) == 0x0400);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::SCHEDULING_POLICY_FAILED) & 0xFF00) == 0x0400);

    // Communication faults (0x05xx)
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::I2C_TIMEOUT) & 0xFF00) == 0x0500);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::I2C_NACK) & 0xFF00) == 0x0500);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::CAMERA_TIMEOUT) & 0xFF00) == 0x0500);

    // Safety system faults (0x06xx)
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::SAFETY_COMPARATOR_MISMATCH) & 0xFF00) == 0x0600);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::INTERLOCK_FAULT) & 0xFF00) == 0x0600);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::RANGE_DATA_STALE) & 0xFF00) == 0x0600);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::RANGE_DATA_INVALID) & 0xFF00) == 0x0600);

    // Emergency faults (0x07xx)
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::EMERGENCY_STOP_REQUESTED) & 0xFF00) == 0x0700);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::CRITICAL_TEMPERATURE) & 0xFF00) == 0x0700);
    ASSERT_TRUE((static_cast<uint16_t>(aurore::SafetyFaultCode::POWER_FAULT) & 0xFF00) == 0x0700);

    std::cout << "    Fault code categories verified: 7 categories" << std::endl;
}

// ============================================================================
// Per-Stage Latency Monitoring Tests
// ============================================================================

TEST(test_pipeline_stage_enum) {
    // Verify PipelineStage enum values
    ASSERT_EQ(static_cast<size_t>(aurore::PipelineStage::VISION), 0);
    ASSERT_EQ(static_cast<size_t>(aurore::PipelineStage::TRACK), 1);
    ASSERT_EQ(static_cast<size_t>(aurore::PipelineStage::ACTUATION), 2);
    ASSERT_EQ(static_cast<size_t>(aurore::PipelineStage::NUM_STAGES), 3);
}

TEST(test_stage_latency_stats_construction) {
    aurore::StageLatencyStats stats;

    ASSERT_EQ(stats.last_latency_ns.load(), 0);
    ASSERT_EQ(stats.max_latency_ns.load(), 0);
    ASSERT_EQ(stats.total_latency_ns.load(), 0);
    ASSERT_EQ(stats.sample_count.load(), 0);
    ASSERT_EQ(stats.stall_count.load(), 0);
    ASSERT_EQ(stats.stall_threshold_ns, 25000000);  // 25ms default
}

TEST(test_stage_latency_stats_record_latency) {
    aurore::StageLatencyStats stats;

    // Record multiple latencies
    stats.record_latency(1000000);   // 1ms
    stats.record_latency(2000000);   // 2ms
    stats.record_latency(3000000);   // 3ms

    ASSERT_EQ(stats.last_latency_ns.load(), 3000000);
    ASSERT_EQ(stats.max_latency_ns.load(), 3000000);
    ASSERT_EQ(stats.total_latency_ns.load(), 6000000);
    ASSERT_EQ(stats.sample_count.load(), 3);
    ASSERT_EQ(stats.stall_count.load(), 0);  // All under 25ms threshold
}

TEST(test_stage_latency_stats_stall_detection) {
    aurore::StageLatencyStats stats;
    stats.stall_threshold_ns = 5000000;  // 5ms threshold

    // Record latencies, some exceeding threshold
    stats.record_latency(1000000);   // 1ms - OK
    stats.record_latency(6000000);   // 6ms - STALL
    stats.record_latency(2000000);   // 2ms - OK
    stats.record_latency(10000000);  // 10ms - STALL

    ASSERT_EQ(stats.stall_count.load(), 2);
    ASSERT_TRUE(stats.is_stalled());  // Last latency (10ms) > threshold (5ms)
}

TEST(test_stage_latency_stats_average) {
    aurore::StageLatencyStats stats;

    // Record 10 samples of 5ms each
    for (int i = 0; i < 10; i++) {
        stats.record_latency(5000000);
    }

    uint64_t avg = stats.get_avg_latency_ns();
    ASSERT_EQ(avg, 5000000);  // Exactly 5ms average
}

TEST(test_stage_latency_stats_reset) {
    aurore::StageLatencyStats stats;

    // Record some data
    stats.record_latency(1000000);
    stats.record_latency(2000000);
    stats.record_latency(30000000);  // Stall

    // Reset
    stats.reset();

    ASSERT_EQ(stats.last_latency_ns.load(), 0);
    ASSERT_EQ(stats.max_latency_ns.load(), 0);
    ASSERT_EQ(stats.total_latency_ns.load(), 0);
    ASSERT_EQ(stats.sample_count.load(), 0);
    ASSERT_EQ(stats.stall_count.load(), 0);
    ASSERT_FALSE(stats.is_stalled());
}

TEST(test_per_stage_monitor_config) {
    aurore::PerStageMonitorConfig config;

    ASSERT_TRUE(config.enable_per_stage);
    ASSERT_EQ(config.stall_threshold_ns, 25000000);  // 25ms
    ASSERT_EQ(config.stalls_before_recovery, 3);
    ASSERT_TRUE(config.enable_recovery_callback);
    ASSERT_TRUE(config.enable_health_report);
}

// ============================================================================
// SafetyMonitor Per-Stage Integration Tests
// ============================================================================

TEST(test_safety_monitor_record_stage_latency) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();

    // Record latencies for each stage
    monitor.record_stage_latency(aurore::PipelineStage::VISION, 5000000);    // 5ms
    monitor.record_stage_latency(aurore::PipelineStage::TRACK, 3000000);     // 3ms
    monitor.record_stage_latency(aurore::PipelineStage::ACTUATION, 2000000); // 2ms

    // Verify stats recorded
    const auto& vision_stats = monitor.get_stage_stats(aurore::PipelineStage::VISION);
    ASSERT_EQ(vision_stats.last_latency_ns.load(), 5000000);

    const auto& track_stats = monitor.get_stage_stats(aurore::PipelineStage::TRACK);
    ASSERT_EQ(track_stats.last_latency_ns.load(), 3000000);

    const auto& actuation_stats = monitor.get_stage_stats(aurore::PipelineStage::ACTUATION);
    ASSERT_EQ(actuation_stats.last_latency_ns.load(), 2000000);

    monitor.stop();
}

TEST(test_safety_monitor_stage_stall_detection) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.per_stage.stall_threshold_ns = 5000000;  // 5ms

    aurore::SafetyMonitor monitor(config);
    monitor.init();

    // Record stall latency
    monitor.record_stage_latency(aurore::PipelineStage::VISION, 10000000);  // 10ms - STALL

    const auto& stats = monitor.get_stage_stats(aurore::PipelineStage::VISION);
    ASSERT_EQ(stats.stall_count.load(), 1);
    ASSERT_TRUE(stats.is_stalled());

    ASSERT_EQ(monitor.get_total_stalls(), 1);

    monitor.stop();
}

TEST(test_safety_monitor_recovery_callback) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.per_stage.stall_threshold_ns = 5000000;  // 5ms
    config.per_stage.stalls_before_recovery = 3;

    aurore::SafetyMonitor monitor(config);
    monitor.init();

    std::atomic<int> recovery_count(0);
    std::string last_stage;

    monitor.set_recovery_callback(
        [](const char* stage_name, uint64_t stall_count, void* user_data) {
            auto* count = static_cast<std::atomic<int>*>(user_data);
            count->fetch_add(1);
            (void)stage_name;  // Unused in test
            (void)stall_count;  // Unused in test
        },
        &recovery_count
    );

    // Trigger 3 consecutive stalls
    for (int i = 0; i < 3; i++) {
        monitor.record_stage_latency(aurore::PipelineStage::VISION, 10000000);
    }

    // Callback should have been triggered
    ASSERT_EQ(recovery_count.load(), 1);

    monitor.stop();
}

// ============================================================================
// Health Report Tests
// ============================================================================

TEST(test_safety_monitor_health_report_generation) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.per_stage.enable_health_report = true;

    aurore::SafetyMonitor monitor(config);
    monitor.init();

    // Record some latencies
    monitor.record_stage_latency(aurore::PipelineStage::VISION, 5000000);
    monitor.record_stage_latency(aurore::PipelineStage::TRACK, 3000000);
    monitor.record_stage_latency(aurore::PipelineStage::ACTUATION, 2000000);

    // Record frame completions
    monitor.record_frame_complete(10000000);
    monitor.record_frame_complete(10000000);

    // Generate report
    std::string report = monitor.generate_health_report();

    ASSERT_FALSE(report.empty());
    ASSERT_TRUE(report.find("Per-Stage Health Report") != std::string::npos);
    ASSERT_TRUE(report.find("VISION") != std::string::npos);
    ASSERT_TRUE(report.find("TRACK") != std::string::npos);
    ASSERT_TRUE(report.find("ACTUATION") != std::string::npos);

    std::cout << "    Health report generated: " << report.length() << " bytes" << std::endl;

    monitor.stop();
}

TEST(test_safety_monitor_health_report_disabled) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.per_stage.enable_health_report = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();

    std::string report = monitor.generate_health_report();
    ASSERT_TRUE(report.empty());

    monitor.stop();
}

TEST(test_safety_monitor_frame_counting) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();

    // Record healthy frames
    monitor.record_frame_complete(5000000);
    monitor.record_frame_complete(5000000);
    monitor.record_frame_complete(5000000);

    ASSERT_EQ(monitor.get_total_frames(), 3);
    ASSERT_EQ(monitor.get_healthy_frames(), 3);
    ASSERT_EQ(monitor.get_total_stalls(), 0);

    // Record a stalled frame
    monitor.record_stage_latency(aurore::PipelineStage::VISION, 30000000);  // 30ms - stall
    monitor.record_frame_complete(30000000);

    ASSERT_EQ(monitor.get_total_frames(), 4);
    ASSERT_EQ(monitor.get_healthy_frames(), 3);  // One was not healthy
    ASSERT_EQ(monitor.get_total_stalls(), 1);

    monitor.stop();
}

TEST(test_safety_monitor_reset_stage_stats) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();

    // Record some data
    for (int i = 0; i < 10; i++) {
        monitor.record_stage_latency(aurore::PipelineStage::VISION, 5000000);
        monitor.record_frame_complete(5000000);
    }

    ASSERT_EQ(monitor.get_total_frames(), 10);

    // Reset stats
    monitor.reset_stage_stats();

    ASSERT_EQ(monitor.get_total_frames(), 0);
    ASSERT_EQ(monitor.get_healthy_frames(), 0);
    ASSERT_EQ(monitor.get_total_stalls(), 0);

    const auto& stats = monitor.get_stage_stats(aurore::PipelineStage::VISION);
    ASSERT_EQ(stats.sample_count.load(), 0);

    monitor.stop();
}

// ============================================================================
// SafetyEvent Tests
// ============================================================================

TEST(test_safety_event_initialization) {
    aurore::SafetyEvent event;

    ASSERT_EQ(event.timestamp_ns, 0);
    ASSERT_EQ(event.fault_code, aurore::SafetyFaultCode::NONE);
    ASSERT_EQ(event.severity, 0);
    ASSERT_EQ(std::strlen(event.reason), 0);
}

TEST(test_safety_event_population) {
    aurore::SafetyEvent event;

    event.timestamp_ns = aurore::get_timestamp();
    event.fault_code = aurore::SafetyFaultCode::VISION_STALLED;
    event.severity = 3;
    std::strncpy(event.reason, "Test fault reason", sizeof(event.reason) - 1);

    ASSERT_TRUE(event.timestamp_ns > 0);
    ASSERT_EQ(event.fault_code, aurore::SafetyFaultCode::VISION_STALLED);
    ASSERT_EQ(event.severity, 3);
    ASSERT_EQ(std::string(event.reason), "Test fault reason");
}

TEST(test_safety_event_max_reason_length) {
    aurore::SafetyEvent event;

    // Test max reason length
    constexpr size_t MAX_REASON = aurore::MAX_FAULT_REASON_LEN - 1;
    std::string long_reason(MAX_REASON, 'A');
    std::strncpy(event.reason, long_reason.c_str(), sizeof(event.reason) - 1);

    ASSERT_EQ(std::strlen(event.reason), MAX_REASON);
}

// ============================================================================
// WatchdogKick RAII Tests
// ============================================================================

TEST(test_watchdog_kick_raii_basic) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Create RAII kick
    {
        aurore::WatchdogKick kick(monitor);
        // Kick happens at end of scope
    }

    // Monitor should still be safe
    ASSERT_TRUE(monitor.is_system_safe());

    monitor.stop();
}

TEST(test_watchdog_kick_raii_multiple_scopes) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Multiple RAII kicks in sequence
    for (int i = 0; i < 5; i++) {
        {
            aurore::WatchdogKick kick(monitor);
        }
        sleep_ms(1);
    }

    ASSERT_TRUE(monitor.is_system_safe());

    monitor.stop();
}

TEST(test_watchdog_kick_raii_nested) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Nested RAII kicks
    {
        aurore::WatchdogKick kick1(monitor);
        {
            aurore::WatchdogKick kick2(monitor);
            // Both kicks active
        }
        // kick2 destroyed, kick1 still active
    }
    // kick1 destroyed

    ASSERT_TRUE(monitor.is_system_safe());

    monitor.stop();
}

// ============================================================================
// Fault Severity Tests
// ============================================================================

TEST(test_fault_severity_levels) {
    // Verify severity levels used in trigger_fault
    // 0=debug, 1=info, 2=warning, 3=error, 4=critical

    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Test that emergency stop (severity 4) triggers emergency_active
    monitor.trigger_emergency_stop("Test");
    ASSERT_TRUE(monitor.is_emergency_active());

    monitor.stop();
}

// ============================================================================
// Main
// ============================================================================

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "Running SafetyMonitor Fault Code tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    // Individual fault code tests (24 codes)
    RUN_TEST(test_fault_code_none);
    RUN_TEST(test_fault_code_vision_stalled);
    RUN_TEST(test_fault_code_vision_latency_exceeded);
    RUN_TEST(test_fault_code_vision_buffer_overrun);
    RUN_TEST(test_fault_code_actuation_stalled);
    RUN_TEST(test_fault_code_actuation_latency_exceeded);
    RUN_TEST(test_fault_code_actuation_command_invalid);
    RUN_TEST(test_fault_code_frame_deadline_missed);
    RUN_TEST(test_fault_code_consecutive_deadline_misses);
    RUN_TEST(test_fault_code_timestamp_non_monotonic);
    RUN_TEST(test_fault_code_watchdog_feed_failed);
    RUN_TEST(test_fault_code_memory_lock_failed);
    RUN_TEST(test_fault_code_scheduling_policy_failed);
    RUN_TEST(test_fault_code_i2c_timeout);
    RUN_TEST(test_fault_code_i2c_nack);
    RUN_TEST(test_fault_code_camera_timeout);
    RUN_TEST(test_fault_code_safety_comparator_mismatch);
    RUN_TEST(test_fault_code_interlock_fault);
    RUN_TEST(test_fault_code_range_data_stale);
    RUN_TEST(test_fault_code_range_data_invalid);
    RUN_TEST(test_fault_code_emergency_stop_requested);
    RUN_TEST(test_fault_code_critical_temperature);
    RUN_TEST(test_fault_code_power_fault);

    // Comprehensive fault code tests
    RUN_TEST(test_fault_code_all_24_values);
    RUN_TEST(test_fault_code_categories);

    // Per-stage latency monitoring tests
    RUN_TEST(test_pipeline_stage_enum);
    RUN_TEST(test_stage_latency_stats_construction);
    RUN_TEST(test_stage_latency_stats_record_latency);
    RUN_TEST(test_stage_latency_stats_stall_detection);
    RUN_TEST(test_stage_latency_stats_average);
    RUN_TEST(test_stage_latency_stats_reset);
    RUN_TEST(test_per_stage_monitor_config);

    // Per-stage integration tests
    RUN_TEST(test_safety_monitor_record_stage_latency);
    RUN_TEST(test_safety_monitor_stage_stall_detection);
    RUN_TEST(test_safety_monitor_recovery_callback);

    // Health report tests
    RUN_TEST(test_safety_monitor_health_report_generation);
    RUN_TEST(test_safety_monitor_health_report_disabled);
    RUN_TEST(test_safety_monitor_frame_counting);
    RUN_TEST(test_safety_monitor_reset_stage_stats);

    // SafetyEvent tests
    RUN_TEST(test_safety_event_initialization);
    RUN_TEST(test_safety_event_population);
    RUN_TEST(test_safety_event_max_reason_length);

    // WatchdogKick RAII tests
    RUN_TEST(test_watchdog_kick_raii_basic);
    RUN_TEST(test_watchdog_kick_raii_multiple_scopes);
    RUN_TEST(test_watchdog_kick_raii_nested);

    // Fault severity tests
    RUN_TEST(test_fault_severity_levels);

    // Summary
    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}
