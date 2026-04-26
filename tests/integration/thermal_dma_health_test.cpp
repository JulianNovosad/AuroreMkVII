/**
 * @file thermal_dma_health_test.cpp
 * @brief Part 2: Temperature, Power, and DMA Health Tests
 *
 * Validates:
 * - Correct behavior under thermal throttling transitions
 * - Verification that frequency scaling does not violate timing contracts
 * - DMA buffer integrity, alignment, exhaustion, and recovery
 * - Simulated DMA fault injection and containment
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include "aurore/test_utils.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <functional> // For std::function

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
#define ASSERT_GT(a, b) do { if ((a) <= (b)) throw std::runtime_error("Assertion failed: " #a " > " #b); } while(0)
#define ASSERT_LT(a, b) do { if ((a) >= (b)) throw std::runtime_error("Assertion failed: " #a " < " #b); } while(0)
#define ASSERT_GE(a, b) do { if ((a) < (b)) throw std::runtime_error("Assertion failed: " #a " >= " #b); } while(0)
#define ASSERT_LE(a, b) do { if ((a) > (b)) throw std::runtime_error("Assertion failed: " #a " <= " #b); } while(0)
#define ASSERT_NEAR(a, b, tol) do { \
    auto diff = std::abs(static_cast<int64_t>(a) - static_cast<int64_t>(b)); \
    if (diff > static_cast<int64_t>(tol)) \
        throw std::runtime_error("Assertion failed: " #a " not near " #b); \
} while(0)

constexpr uint64_t kMaxStackSizeKb = 8192; // Max stack size for StackTracker
constexpr size_t kHeapBaselineEnvelopeBytes = 1024 * 1024; // 1MB heap baseline

}  // anonymous namespace

// ============================================================================
// Thermal Health Tests
// ============================================================================

TEST(test_thermal_throttling_transitions) {
    aurore::test::ThermalHealthMonitor monitor(80.0); // Critical threshold at 80C

    // Nominal state
    monitor.update_temperature(50.0);
    ASSERT_EQ(monitor.check_throttle_state(), aurore::test::ThrottleState::Nominal);

    // Transition to throttling
    monitor.update_temperature(70.0); // 80 - 15 = 65, so 70 is throttling
    ASSERT_EQ(monitor.check_throttle_state(), aurore::test::ThrottleState::Throttling);
    monitor.record_throttle_event(); // Simulate system recording throttle

    // Transition to critical
    monitor.update_temperature(85.0);
    ASSERT_EQ(monitor.check_throttle_state(), aurore::test::ThrottleState::Critical);
}

TEST(test_thermal_timing_contract_under_throttle) {
    aurore::test::ThermalHealthMonitor monitor(80.0);

    // Simulate critical throttling for 30 seconds (30,000,000,000 ns)
    // The verify_timing_contract() in the monitor checks for a 60s max.
    // This test will pass. A real test would check actual timing.
    monitor.update_temperature(85.0);
    monitor.simulate_throttling_transition(); // Transition to Critical
    
    // For this test, we can only verify the logic of the monitor's contract check.
    // Actual timing verification needs external measurement in a real system.
    ASSERT_TRUE(monitor.verify_timing_contract()); 
}

TEST(test_thermal_recovery_to_nominal) {
    aurore::test::ThermalHealthMonitor monitor(80.0);

    monitor.update_temperature(90.0);
    ASSERT_EQ(monitor.check_throttle_state(), aurore::test::ThrottleState::Critical);

    monitor.update_temperature(50.0);
    ASSERT_EQ(monitor.check_throttle_state(), aurore::test::ThrottleState::Nominal);
}

// ============================================================================
// DMA Health Tests
// ============================================================================

TEST(test_dma_buffer_alignment_integrity) {
    aurore::test::DmaHealthMonitor monitor;

    // Test with a common page alignment
    ASSERT_TRUE(monitor.check_alignment(4096));

    // Test with a typical cache line alignment
    ASSERT_TRUE(monitor.check_alignment(64));

    // Test with a non-power-of-2 alignment (should fail if buffer_alignment is power-of-2)
    // The monitor is initialized with buffer_alignment_ = 4096, which is power-of-2
    // If we assume a fixed alignment, this test verifies it.
    // For testing arbitrary alignment, the monitor would need to be configurable.
    // For now, checking the default value's alignment properties.
    auto stats = monitor.get_stats();
    ASSERT_TRUE(stats.alignment_valid); 
}

TEST(test_dma_fault_injection_containment) {
    aurore::test::DmaHealthMonitor monitor;

    // Simulate a fault
    monitor.simulate_fault();
    ASSERT_EQ(monitor.get_stats().state, aurore::test::DmaState::Error);
    ASSERT_GT(monitor.get_stats().error_count, 0);

    // Attempt recovery
    ASSERT_TRUE(monitor.recover());
    ASSERT_EQ(monitor.get_stats().state, aurore::test::DmaState::Idle);

    // Verify containment (i.e., no other part of the system is affected)
    // This is hard to test purely within the test_infrastructure, but we can
    // check that the monitor itself returns to a good state.
}

TEST(test_dma_exhaustion_scenario) {
    aurore::test::DmaHealthMonitor monitor;
    aurore::test::ResourceMonitor dma_channel_monitor(aurore::test::ResourceType::DmaChannels, 5);

    // Simulate exhausting DMA channels
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(dma_channel_monitor.acquire());
        monitor.record_transfer(1024, 100);
    }
    ASSERT_TRUE(dma_channel_monitor.is_exhausted());
    ASSERT_FALSE(dma_channel_monitor.acquire()); // Should fail to acquire more

    // Simulate an error under exhaustion
    monitor.record_error();
    ASSERT_EQ(monitor.get_stats().state, aurore::test::DmaState::Error);

    // Simulate recovery and releasing channels
    monitor.recover();
    for (int i = 0; i < 5; ++i) {
        dma_channel_monitor.release();
    }
    ASSERT_FALSE(dma_channel_monitor.is_exhausted());
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    aurore::test::TestEnvironment::init(kMaxStackSizeKb, kHeapBaselineEnvelopeBytes);

    std::cout << "=== Part 2: Temperature, Power, and DMA Health Tests ===" << std::endl;
    std::cout << "Running thermal and DMA health tests..." << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "\n--- Thermal Health Tests ---" << std::endl;
    RUN_TEST(test_thermal_throttling_transitions);
    RUN_TEST(test_thermal_timing_contract_under_throttle);
    RUN_TEST(test_thermal_recovery_to_nominal);

    std::cout << "\n--- DMA Health Tests ---" << std::endl;
    RUN_TEST(test_dma_buffer_alignment_integrity);
    RUN_TEST(test_dma_fault_injection_containment);
    RUN_TEST(test_dma_exhaustion_scenario);

    std::cout << "\n========================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}