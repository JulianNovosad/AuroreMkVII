/**
 * @file test_coupling_control_actuation.cpp
 * @brief Unit test for Frontend <-> Robotics Coupling
 *
 * This test validates the data contract between the control layer (GimbalController)
 * and the robotics backend (FusionHat), ensuring coordinate systems and units
 * are correctly mapped according to ICD-002 and ICD-003.
 */

#include "aurore/fusion_hat.hpp"
#include "aurore/gimbal_controller.hpp"
#include "aurore/interlock_controller.hpp"
#include "aurore/timing.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    std::cout << "Running " << #name << "... "; \
    try { name(); std::cout << "PASS\n"; ++g_pass; } \
    catch (const std::exception& e) { \
        std::cout << "FAIL: " << e.what() << "\n"; ++g_fail; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        throw std::runtime_error("Assertion failed: " #x); \
    } \
} while(0)

#define ASSERT_NEAR(expected, actual, eps) do { \
    if (std::abs((expected) - (actual)) > (eps)) { \
        throw std::runtime_error("Assertion failed: " + std::to_string(expected) + " ~= " + std::to_string(actual)); \
    } \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        throw std::runtime_error("Assertion failed: " + std::to_string(expected) + " == " + std::to_string(actual)); \
    } \
} while(0)

using namespace aurore;

/**
 * @brief Test Azimuth Mapping (ICD-002)
 * Requirement: Azimuth ±90° mapped to 1000-2000µs
 */
TEST(test_azimuth_coupling) {
    FusionHatConfig config;
    config.min_angle_deg = -90.0f;
    config.max_angle_deg = 90.0f;
    config.min_pulse_width_us = 1000;
    config.max_pulse_width_us = 2000;
    
    FusionHat hat(config);
    hat.init();
    
    // Test center (0°)
    hat.set_servo_angle(0, 0.0f);
    ASSERT_EQ(1500, hat.get_pulse_width(0));
    
    // Test full left (-90°)
    hat.set_servo_angle(0, -90.0f);
    ASSERT_EQ(1000, hat.get_pulse_width(0));
    
    // Test full right (+90°)
    hat.set_servo_angle(0, 90.0f);
    ASSERT_EQ(2000, hat.get_pulse_width(0));
}

/**
 * @brief Test Elevation Mapping (ICD-002)
 * Requirement: Elevation -10° to +45° mapped to 1000-2000µs
 *
 * NOTE: This test highlights the gap in the current FusionHat implementation
 * which uses a global min/max angle for all channels.
 */
TEST(test_elevation_coupling) {
    // To support -10 to +45, we would need to configure FusionHat with those limits.
    // But if we already configured it for Azimuth ±90, we have a conflict.
    
    // For this test, we assume a FusionHat configured specifically for Elevation
    FusionHatConfig config;
    config.min_angle_deg = -10.0f;
    config.max_angle_deg = 45.0f;
    config.min_pulse_width_us = 1000;
    config.max_pulse_width_us = 2000;
    
    FusionHat hat(config);
    hat.init();
    
    // Test bottom limit (-10°)
    hat.set_servo_angle(1, -10.0f);
    ASSERT_EQ(1000, hat.get_pulse_width(1));
    
    // Test top limit (+45°)
    hat.set_servo_angle(1, 45.0f);
    ASSERT_EQ(2000, hat.get_pulse_width(1));
    
    // Test horizontal (0°)
    // ratio = (0 - (-10)) / (45 - (-10)) = 10 / 55 = 0.1818...
    // pulse = 1000 + 0.1818 * 1000 = 1181
    hat.set_servo_angle(1, 0.0f);
    ASSERT_NEAR(1181.8f, static_cast<float>(hat.get_pulse_width(1)), 1.0f);
}

/**
 * @brief Test Interlock Coupling (ICD-003)
 * Requirement: 1000µs = inhibit, 2000µs = enable on Channel 2
 */
TEST(test_interlock_coupling) {
    FusionHatConfig hat_config;
    FusionHat hat(hat_config);
    hat.init();
    
    InterlockConfig il_config;
    il_config.inhibit_channel = 2;
    
    InterlockController interlock(&hat, il_config);
    interlock.init();
    
    // Initial state should be inhibit (1000µs)
    ASSERT_EQ(1000, hat.get_pulse_width(2));
    
    // Set to enable (2000µs)
    interlock.set_inhibit(false);
    ASSERT_EQ(2000, hat.get_pulse_width(2));
    
    // Set back to inhibit (1000µs)
    interlock.set_inhibit(true);
    ASSERT_EQ(1000, hat.get_pulse_width(2));
}

/**
 * @brief Test Safety Invariance
 * Even if GimbalController requests out-of-bounds angles,
 * FusionHat should clamp them to its configured limits.
 */
TEST(test_safety_clamping_coupling) {
    FusionHatConfig config;
    config.min_angle_deg = -45.0f;
    config.max_angle_deg = 45.0f;
    config.min_pulse_width_us = 1000;
    config.max_pulse_width_us = 2000;
    
    FusionHat hat(config);
    hat.init();
    
    // Request angle outside limits
    hat.set_servo_angle(0, -100.0f);
    ASSERT_EQ(1000, hat.get_pulse_width(0));
    ASSERT_NEAR(-45.0f, *hat.get_servo_angle(0), 0.001f);
    
    hat.set_servo_angle(0, 100.0f);
    ASSERT_EQ(2000, hat.get_pulse_width(0));
    ASSERT_NEAR(45.0f, *hat.get_servo_angle(0), 0.001f);
}

int main() {
    std::cout << "=== Coupling Control-Actuation Tests ===\n" << std::endl;
    
    RUN_TEST(test_azimuth_coupling);
    RUN_TEST(test_elevation_coupling);
    RUN_TEST(test_interlock_coupling);
    RUN_TEST(test_safety_clamping_coupling);
    
    std::cout << "\n=== Results ===\n";
    std::cout << "Passed: " << g_pass << "\n";
    std::cout << "Failed: " << g_fail << "\n";
    
    return g_fail > 0 ? 1 : 0;
}
