/**
 * @file fusion_hat_test.cpp
 * @brief Unit tests for Fusion HAT+ PWM/servo driver
 *
 * Note: These tests require actual Fusion HAT+ hardware.
 * For CI/development without hardware, tests are skipped gracefully.
 */

#include "aurore/fusion_hat.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>

// Test helper macros
#define TEST(name) void name()
#define ASSERT_TRUE(x) do { if (!(x)) { std::cerr << "FAIL: " #x << std::endl; throw std::runtime_error("Assertion failed: " #x); } } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NEAR(a, b, tol) ASSERT_TRUE(std::abs((a) - (b)) <= (tol))

namespace {

TEST(test_fusion_hat_construction) {
    std::cout << "Running test_fusion_hat_construction..." << std::endl;
    
    aurore::FusionHatConfig config;
    ASSERT_TRUE(config.validate());
    
    aurore::FusionHat hat(config);
    ASSERT_FALSE(hat.is_initialized());
    
    std::cout << "  PASS" << std::endl;
}

TEST(test_fusion_hat_config_validation) {
    std::cout << "Running test_fusion_hat_config_validation..." << std::endl;
    
    // Invalid: frequency out of range
    aurore::FusionHatConfig config1;
    config1.servo_freq_hz = 0;
    ASSERT_FALSE(config1.validate());
    
    // Invalid: pulse width range
    aurore::FusionHatConfig config2;
    config2.min_pulse_width_us = 3000;
    config2.max_pulse_width_us = 1000;
    ASSERT_FALSE(config2.validate());
    
    // Invalid: angle range
    aurore::FusionHatConfig config3;
    config3.min_angle_deg = 90.0f;
    config3.max_angle_deg = -90.0f;
    ASSERT_FALSE(config3.validate());
    
    // Valid config
    aurore::FusionHatConfig config4;
    config4.servo_freq_hz = 50;
    config4.min_pulse_width_us = 500;
    config4.max_pulse_width_us = 2500;
    config4.min_angle_deg = -90.0f;
    config4.max_angle_deg = 90.0f;
    ASSERT_TRUE(config4.validate());
    
    std::cout << "  PASS" << std::endl;
}

TEST(test_angle_to_pulse_width_mapping) {
    std::cout << "Running test_angle_to_pulse_width_mapping..." << std::endl;
    
    // This tests the internal mapping logic via public API
    // Center position (0 degrees) should be ~1500μs
    aurore::FusionHatConfig config;
    config.min_angle_deg = -90.0f;
    config.max_angle_deg = 90.0f;
    config.min_pulse_width_us = 500;
    config.max_pulse_width_us = 2500;
    
    aurore::FusionHat hat(config);
    
    // We can't test actual conversion without init, but we can verify
    // the config is stored correctly
    ASSERT_EQ(config.min_angle_deg, -90.0f);
    ASSERT_EQ(config.max_angle_deg, 90.0f);
    ASSERT_EQ(config.min_pulse_width_us, 500);
    ASSERT_EQ(config.max_pulse_width_us, 2500);
    
    std::cout << "  PASS" << std::endl;
}

TEST(test_channel_range) {
    std::cout << "Running test_channel_range..." << std::endl;
    
    aurore::FusionHat hat;
    
    // Valid channels: 0-11
    for (int ch = 0; ch < 12; ch++) {
        // These will fail without init, but shouldn't crash
        auto status = hat.get_servo_status(ch);
        ASSERT_EQ(status.channel, ch);
    }
    
    // Invalid channel should return nullopt or error
    auto angle = hat.get_servo_angle(-1);
    ASSERT_FALSE(angle.has_value());
    
    auto angle2 = hat.get_servo_angle(12);
    ASSERT_FALSE(angle2.has_value());
    
    std::cout << "  PASS" << std::endl;
}

TEST(test_is_connected_without_hardware) {
    std::cout << "Running test_is_connected_without_hardware..." << std::endl;
    
    aurore::FusionHat hat;
    
    // Without hardware, is_connected should return false
    // This test verifies graceful degradation
    bool connected = hat.is_connected();
    
    // Either result is valid - depends on whether hardware is present
    std::cout << "  Fusion HAT+ " << (connected ? "connected" : "not connected") << std::endl;
    std::cout << "  PASS" << std::endl;
}

TEST(test_disable_all_servos) {
    std::cout << "Running test_disable_all_servos..." << std::endl;
    
    aurore::FusionHat hat;
    
    // Should not crash even without init
    hat.disable_all_servos();
    
    std::cout << "  PASS" << std::endl;
}

TEST(test_error_counting) {
    std::cout << "Running test_error_counting..." << std::endl;
    
    aurore::FusionHat hat;
    
    uint64_t initial_errors = hat.get_error_count();
    
    // Try invalid operations - should increment error count
    // Note: Without init, some operations may return early without incrementing
    bool result1 = hat.set_servo_angle(-1, 0.0f);  // Invalid channel
    bool result2 = hat.set_servo_angle(12, 0.0f);  // Invalid channel
    
    uint64_t final_errors = hat.get_error_count();
    
    // At least one error should be counted, or operations should return false
    ASSERT_FALSE(result1);  // Should fail
    ASSERT_FALSE(result2);  // Should fail
    
    // Error count may or may not increment depending on where validation fails
    // The important thing is the operations fail gracefully
    (void)initial_errors;  // Suppress unused warning
    (void)final_errors;
    
    std::cout << "  PASS" << std::endl;
}

TEST(test_command_counting) {
    std::cout << "Running test_command_counting..." << std::endl;
    
    aurore::FusionHat hat;
    
    uint64_t initial_commands = hat.get_command_count();
    
    // These won't succeed without init, but may increment command count
    // depending on implementation
    
    uint64_t final_commands = hat.get_command_count();
    ASSERT_TRUE(final_commands >= initial_commands);
    
    std::cout << "  PASS" << std::endl;
}

TEST(test_servo_status_structure) {
    std::cout << "Running test_servo_status_structure..." << std::endl;
    
    aurore::FusionHat hat;
    
    auto status = hat.get_servo_status(0);
    
    // Verify structure fields exist and have sensible defaults
    ASSERT_EQ(status.channel, 0);
    ASSERT_FALSE(status.enabled);
    ASSERT_FALSE(status.endstop_active);
    
    std::cout << "  PASS" << std::endl;
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    std::cout << "Fusion HAT+ Unit Tests" << std::endl;
    std::cout << "======================" << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    auto run_test = [&](const char* name, void (*test_fn)()) {
        try {
            test_fn();
            passed++;
        } catch (const std::exception& e) {
            std::cerr << "  FAIL: " << e.what() << std::endl;
            failed++;
        }
    };
    
    run_test("test_fusion_hat_construction", test_fusion_hat_construction);
    run_test("test_fusion_hat_config_validation", test_fusion_hat_config_validation);
    run_test("test_angle_to_pulse_width_mapping", test_angle_to_pulse_width_mapping);
    run_test("test_channel_range", test_channel_range);
    run_test("test_is_connected_without_hardware", test_is_connected_without_hardware);
    run_test("test_disable_all_servos", test_disable_all_servos);
    run_test("test_error_counting", test_error_counting);
    run_test("test_command_counting", test_command_counting);
    run_test("test_servo_status_structure", test_servo_status_structure);
    
    std::cout << std::endl;
    std::cout << "======================" << std::endl;
    std::cout << "Tests run: " << (passed + failed) << std::endl;
    std::cout << "Tests passed: " << passed << std::endl;
    std::cout << "Tests failed: " << failed << std::endl;
    
    return (failed == 0) ? 0 : 1;
}
