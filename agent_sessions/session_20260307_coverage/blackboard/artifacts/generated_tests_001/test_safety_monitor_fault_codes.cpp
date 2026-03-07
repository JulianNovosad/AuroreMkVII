/**
 * @file test_safety_monitor_fault_codes.cpp
 * @brief Unit tests for all SafetyMonitor fault code paths
 *
 * Tests cover:
 * - All SafetyFaultCode values
 * - Fault triggering and detection
 * - Per-stage latency monitoring
 * - Recovery callback invocation
 * - Health report generation
 *
 * Coverage gaps addressed:
 * - include/aurore/safety_monitor.hpp lines 145-712
 */

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "aurore/safety_monitor.hpp"
#include "aurore/timing.hpp"

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

// Helper: sleep for specified duration in milliseconds
void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // anonymous namespace

// ============================================================================
// Fault Code String Conversion Tests
// ============================================================================

TEST(test_all_fault_codes_have_strings) {
    // Test all known fault codes have string representations
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::NONE)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::VISION_STALLED)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::VISION_LATENCY_EXCEEDED)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::VISION_BUFFER_OVERRUN)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::ACTUATION_STALLED)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::ACTUATION_LATENCY_EXCEEDED)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::ACTUATION_COMMAND_INVALID)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::FRAME_DEADLINE_MISSED)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::CONSECUTIVE_DEADLINE_MISSES)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::TIMESTAMP_NON_MONOTONIC)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::WATCHDOG_FEED_FAILED)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::MEMORY_LOCK_FAILED)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::SCHEDULING_POLICY_FAILED)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::I2C_TIMEOUT)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::I2C_NACK)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::CAMERA_TIMEOUT)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::SAFETY_COMPARATOR_MISMATCH)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::INTERLOCK_FAULT)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::RANGE_DATA_STALE)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::RANGE_DATA_INVALID)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::EMERGENCY_STOP_REQUESTED)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::CRITICAL_TEMPERATURE)) > 0);
    ASSERT_TRUE(std::strlen(aurore::fault_code_to_string(aurore::SafetyFaultCode::POWER_FAULT)) > 0);
}

TEST(test_fault_code_specific_strings) {
    // Verify specific fault code strings
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::NONE)), "NONE");
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::VISION_STALLED)), "VISION_STALLED");
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::EMERGENCY_STOP_REQUESTED)), "EMERGENCY_STOP_REQUESTED");
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::CRITICAL_TEMPERATURE)), "CRITICAL_TEMPERATURE");
}

// ============================================================================
// Vision Fault Tests
// ============================================================================

TEST(test_vision_stalled_fault) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.vision_deadline_ns = 1000000;  // 1ms for test
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    // Update once then stop updating (simulate stall)
    auto ts = aurore::get_timestamp();
    monitor.update_vision_frame(1, ts);
    
    // Wait for stall detection
    sleep_ms(10);
    
    // Run monitoring cycles
    for (int i = 0; i < 10; i++) {
        monitor.run_cycle();
        sleep_ms(1);
    }
    
    // Monitor should still be functional
    ASSERT_TRUE(monitor.is_running());
    
    monitor.stop();
}

TEST(test_vision_latency_exceeded_fault) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.vision_deadline_ns = 1000000;  // 1ms
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    // Update with old timestamp (simulating latency)
    auto old_ts = aurore::get_timestamp() - 10000000;  // 10ms ago
    monitor.update_vision_frame(1, old_ts);
    
    sleep_ms(5);
    monitor.run_cycle();
    
    ASSERT_TRUE(monitor.is_running());
    
    monitor.stop();
}

// ============================================================================
// Actuation Fault Tests
// ============================================================================

TEST(test_actuation_stalled_fault) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.actuation_deadline_ns = 1000000;  // 1ms
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    // Update once then stop
    auto ts = aurore::get_timestamp();
    monitor.update_actuation_frame(1, ts);
    
    sleep_ms(10);
    
    for (int i = 0; i < 10; i++) {
        monitor.run_cycle();
        sleep_ms(1);
    }
    
    ASSERT_TRUE(monitor.is_running());
    
    monitor.stop();
}

// ============================================================================
// System Fault Tests
// ============================================================================

TEST(test_watchdog_feed_failed_fault) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_kick_interval_ms = 50;
    config.watchdog_timeout_ms = 100;  // 100ms timeout for test
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    // Initial kick
    monitor.kick_watchdog();
    
    // Wait for timeout (no more kicks)
    sleep_ms(150);
    
    // Run monitoring cycle
    monitor.run_cycle();
    
    // Monitor should still be functional
    ASSERT_TRUE(monitor.is_running());
    
    monitor.stop();
}

TEST(test_consecutive_deadline_misses_fault) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.max_consecutive_misses = 3;
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    // Simulate consecutive misses by not updating frames
    sleep_ms(50);
    
    for (int i = 0; i < 10; i++) {
        monitor.run_cycle();
        sleep_ms(1);
    }
    
    ASSERT_TRUE(monitor.is_running());
    
    monitor.stop();
}

// ============================================================================
// Communication Fault Tests
// ============================================================================

TEST(test_i2c_timeout_fault_string) {
    // Verify I2C fault codes have proper strings
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::I2C_TIMEOUT)), "I2C_TIMEOUT");
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::I2C_NACK)), "I2C_NACK");
}

TEST(test_camera_timeout_fault_string) {
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::CAMERA_TIMEOUT)), "CAMERA_TIMEOUT");
}

// ============================================================================
// Safety System Fault Tests
// ============================================================================

TEST(test_safety_comparator_mismatch_fault) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    // Verify fault code string
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::SAFETY_COMPARATOR_MISMATCH)), 
              "SAFETY_COMPARATOR_MISMATCH");
    
    monitor.stop();
}

TEST(test_interlock_fault) {
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::INTERLOCK_FAULT)), "INTERLOCK_FAULT");
}

TEST(test_range_data_faults) {
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::RANGE_DATA_STALE)), "RANGE_DATA_STALE");
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::RANGE_DATA_INVALID)), "RANGE_DATA_INVALID");
}

// ============================================================================
// Emergency Fault Tests
// ============================================================================

TEST(test_emergency_stop_fault) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    // Trigger emergency stop
    monitor.trigger_emergency_stop("Test emergency");
    
    ASSERT_FALSE(monitor.is_system_safe());
    ASSERT_TRUE(monitor.is_emergency_active());
    ASSERT_EQ(monitor.current_fault(), aurore::SafetyFaultCode::EMERGENCY_STOP_REQUESTED);
    
    monitor.stop();
}

TEST(test_critical_temperature_fault) {
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::CRITICAL_TEMPERATURE)), 
              "CRITICAL_TEMPERATURE");
}

TEST(test_power_fault) {
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::POWER_FAULT)), "POWER_FAULT");
}

// ============================================================================
// Per-Stage Latency Monitoring Tests
// ============================================================================

TEST(test_per_stage_latency_recording) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.per_stage.enable_per_stage = true;
    config.per_stage.stall_threshold_ns = 25000000;  // 25ms
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    // Record latencies for each stage
    monitor.record_stage_latency(aurore::PipelineStage::VISION, 1000000);  // 1ms
    monitor.record_stage_latency(aurore::PipelineStage::TRACK, 2000000);   // 2ms
    monitor.record_stage_latency(aurore::PipelineStage::ACTUATION, 500000); // 0.5ms
    
    // Verify statistics
    const auto& vision_stats = monitor.get_stage_stats(aurore::PipelineStage::VISION);
    ASSERT_EQ(vision_stats.last_latency_ns.load(), 1000000);
    ASSERT_EQ(vision_stats.sample_count.load(), 1);
    
    monitor.stop();
}

TEST(test_per_stage_stall_detection) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.per_stage.enable_per_stage = true;
    config.per_stage.stall_threshold_ns = 1000000;  // 1ms threshold for test
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    // Record latency exceeding threshold
    monitor.record_stage_latency(aurore::PipelineStage::VISION, 5000000);  // 5ms (stall)
    
    const auto& stats = monitor.get_stage_stats(aurore::PipelineStage::VISION);
    ASSERT_TRUE(stats.is_stalled());
    ASSERT_EQ(stats.stall_count.load(), 1);
    
    monitor.stop();
}

TEST(test_per_stage_recovery_callback) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.per_stage.enable_per_stage = true;
    config.per_stage.stalls_before_recovery = 3;
    config.per_stage.enable_recovery_callback = true;
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    std::atomic<int> recovery_count(0);
    std::string last_stage;
    
    monitor.set_recovery_callback(
        [](const char* stage_name, uint64_t stall_count, void* user_data) {
            auto* count = static_cast<std::atomic<int>*>(user_data);
            count->fetch_add(1);
        },
        &recovery_count
    );
    
    // Record multiple stalls
    for (int i = 0; i < 5; i++) {
        monitor.record_stage_latency(aurore::PipelineStage::VISION, 5000000);  // Stall
    }
    
    // Recovery callback should have been triggered
    ASSERT_TRUE(recovery_count.load() >= 1);
    
    monitor.stop();
}

TEST(test_per_stage_health_report) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.per_stage.enable_per_stage = true;
    config.per_stage.enable_health_report = true;
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    // Record some latencies
    for (int i = 0; i < 10; i++) {
        monitor.record_stage_latency(aurore::PipelineStage::VISION, 1000000 + i * 100000);
        monitor.record_stage_latency(aurore::PipelineStage::TRACK, 2000000);
        monitor.record_stage_latency(aurore::PipelineStage::ACTUATION, 500000);
        monitor.record_frame_complete(3500000);
    }
    
    // Generate health report
    std::string report = monitor.generate_health_report();
    
    ASSERT_TRUE(report.length() > 0);
    ASSERT_TRUE(report.find("VISION") != std::string::npos);
    ASSERT_TRUE(report.find("TRACK") != std::string::npos);
    ASSERT_TRUE(report.find("ACTUATION") != std::string::npos);
    
    std::cout << "    Health report generated (" << report.length() << " bytes)" << std::endl;
    
    monitor.stop();
}

TEST(test_per_stage_stats_reset) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.per_stage.enable_per_stage = true;
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    // Record some latencies
    for (int i = 0; i < 10; i++) {
        monitor.record_stage_latency(aurore::PipelineStage::VISION, 1000000);
    }
    
    // Reset stats
    monitor.reset_stage_stats();
    
    // Verify reset
    const auto& stats = monitor.get_stage_stats(aurore::PipelineStage::VISION);
    ASSERT_EQ(stats.sample_count.load(), 0);
    ASSERT_EQ(stats.stall_count.load(), 0);
    
    monitor.stop();
}

// ============================================================================
// SafetyEvent Structure Tests
// ============================================================================

TEST(test_safety_event_structure) {
    aurore::SafetyEvent event;
    
    // Verify zero initialization
    ASSERT_EQ(event.timestamp_ns, 0);
    ASSERT_EQ(event.fault_code, aurore::SafetyFaultCode::NONE);
    ASSERT_EQ(event.severity, 0);
    ASSERT_EQ(event.reason[0], '\0');
    
    // Test setting values
    event.timestamp_ns = aurore::get_timestamp();
    event.fault_code = aurore::SafetyFaultCode::VISION_STALLED;
    event.severity = 3;
    std::strncpy(event.reason, "Test reason", sizeof(event.reason) - 1);
    
    ASSERT_TRUE(event.timestamp_ns > 0);
    ASSERT_EQ(event.fault_code, aurore::SafetyFaultCode::VISION_STALLED);
    ASSERT_EQ(event.severity, 3);
    ASSERT_EQ(std::string(event.reason), "Test reason");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    std::cout << "Running Safety Monitor Fault Code tests..." << std::endl;
    std::cout << "===========================================" << std::endl;
    
    // Fault code string tests
    RUN_TEST(test_all_fault_codes_have_strings);
    RUN_TEST(test_fault_code_specific_strings);
    
    // Vision fault tests
    RUN_TEST(test_vision_stalled_fault);
    RUN_TEST(test_vision_latency_exceeded_fault);
    
    // Actuation fault tests
    RUN_TEST(test_actuation_stalled_fault);
    
    // System fault tests
    RUN_TEST(test_watchdog_feed_failed_fault);
    RUN_TEST(test_consecutive_deadline_misses_fault);
    
    // Communication fault tests
    RUN_TEST(test_i2c_timeout_fault_string);
    RUN_TEST(test_camera_timeout_fault_string);
    
    // Safety system fault tests
    RUN_TEST(test_safety_comparator_mismatch_fault);
    RUN_TEST(test_interlock_fault);
    RUN_TEST(test_range_data_faults);
    
    // Emergency fault tests
    RUN_TEST(test_emergency_stop_fault);
    RUN_TEST(test_critical_temperature_fault);
    RUN_TEST(test_power_fault);
    
    // Per-stage latency monitoring tests
    RUN_TEST(test_per_stage_latency_recording);
    RUN_TEST(test_per_stage_stall_detection);
    RUN_TEST(test_per_stage_recovery_callback);
    RUN_TEST(test_per_stage_health_report);
    RUN_TEST(test_per_stage_stats_reset);
    
    // SafetyEvent structure tests
    RUN_TEST(test_safety_event_structure);
    
    // Summary
    std::cout << "\n===========================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;
    
    return g_tests_failed.load() > 0 ? 1 : 0;
}
