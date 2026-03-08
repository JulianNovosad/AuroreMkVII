#include "aurore/gimbal_controller.hpp"
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

#define ASSERT_NEAR(expected, actual, eps) do { \
    if (std::fabs((expected) - (actual)) > (eps)) { \
        throw std::runtime_error("Assertion failed: " #expected " ~= " #actual); \
    } \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        throw std::runtime_error("Assertion failed: " #expected " == " #actual); \
    } \
} while(0)

using namespace aurore;

TEST(test_pixel_at_center_gives_zero_command) {
    GimbalController ctrl;
    auto cmd = ctrl.command_from_pixel(768.f, 432.f);  // center pixel
    ASSERT_NEAR(0.f, cmd.az_deg, 1e-5f);
    ASSERT_NEAR(0.f, cmd.el_deg, 1e-5f);
}

TEST(test_pixel_offset_gives_correct_angle) {
    GimbalController ctrl;
    // Target 100px right of center: should give positive azimuth
    auto cmd = ctrl.command_from_pixel(868.f, 432.f);
    float expected_az = std::atan2(100.f, 1128.f) * (180.f / static_cast<float>(M_PI));
    ASSERT_NEAR(expected_az, cmd.az_deg, 0.01f);
    ASSERT_NEAR(0.f, cmd.el_deg, 1e-5f);
    
    // Create fresh controller for elevation test to avoid accumulation
    GimbalController ctrl2;
    // Target 100px above center (y=332): should give positive elevation (negated dy)
    cmd = ctrl2.command_from_pixel(768.f, 332.f);
    float expected_el = std::atan2(100.f, 1128.f) * (180.f / static_cast<float>(M_PI));
    ASSERT_NEAR(0.f, cmd.az_deg, 0.01f);
    ASSERT_NEAR(expected_el, cmd.el_deg, 0.01f);
}

TEST(test_auto_source_accumulates_angle) {
    GimbalController ctrl;
    ASSERT_EQ(GimbalSource::AUTO, ctrl.source());
    
    // First command: target 100px right
    auto cmd1 = ctrl.command_from_pixel(868.f, 432.f);
    float expected1 = std::atan2(100.f, 1128.f) * (180.f / static_cast<float>(M_PI));
    ASSERT_NEAR(expected1, cmd1.az_deg, 0.01f);
    
    // Second command: same offset should accumulate
    auto cmd2 = ctrl.command_from_pixel(868.f, 432.f);
    ASSERT_NEAR(2.f * expected1, cmd2.az_deg, 0.01f);
}

TEST(test_freecam_source_sets_absolute) {
    GimbalController ctrl;
    ctrl.set_source(GimbalSource::FREECAM);
    ASSERT_EQ(GimbalSource::FREECAM, ctrl.source());
    
    // Absolute command should set exact angles
    auto cmd = ctrl.command_absolute(30.f, 15.f);
    ASSERT_EQ(30.f, cmd.az_deg);
    ASSERT_EQ(15.f, cmd.el_deg);
    
    // Second absolute command replaces, not accumulates
    cmd = ctrl.command_absolute(-20.f, 10.f);
    ASSERT_EQ(-20.f, cmd.az_deg);
    ASSERT_EQ(10.f, cmd.el_deg);
}

TEST(test_limits_clamp_commands) {
    // Test AUTO mode clamping
    GimbalController ctrl_auto;
    ctrl_auto.set_limits(-45.f, 45.f, -5.f, 30.f);
    
    // AUTO mode: very large offset to exceed limits
    // dx = 2000-768 = 1232px -> delta_az = atan2(1232, 1128) * 180/PI = 47.5 degrees (exceeds 45)
    // dy = -500-432 = -932px -> delta_el = atan2(932, 1128) * 180/PI = 39.5 degrees (exceeds 30)
    auto cmd = ctrl_auto.command_from_pixel(2000.f, -500.f);
    ASSERT_NEAR(45.f, cmd.az_deg, 0.1f);  // should clamp to max az
    ASSERT_NEAR(30.f, cmd.el_deg, 0.1f);  // should clamp to max el
    
    // Test FREECAM mode clamping
    GimbalController ctrl_freecam;
    ctrl_freecam.set_source(GimbalSource::FREECAM);
    ctrl_freecam.set_limits(-45.f, 45.f, -5.f, 30.f);
    
    cmd = ctrl_freecam.command_absolute(90.f, 60.f);
    ASSERT_EQ(45.f, cmd.az_deg);
    ASSERT_EQ(30.f, cmd.el_deg);
    
    cmd = ctrl_freecam.command_absolute(-90.f, -20.f);
    ASSERT_EQ(-45.f, cmd.az_deg);
    ASSERT_EQ(-5.f, cmd.el_deg);
}

TEST(test_source_switch_auto_to_freecam) {
    GimbalController ctrl;
    
    // Start in AUTO, accumulate some angle
    auto cmd1 = ctrl.command_from_pixel(868.f, 432.f);
    float delta = std::atan2(100.f, 1128.f) * (180.f / static_cast<float>(M_PI));
    ASSERT_NEAR(delta, cmd1.az_deg, 0.01f);
    
    // Switch to FREECAM and set absolute
    ctrl.set_source(GimbalSource::FREECAM);
    auto cmd2 = ctrl.command_absolute(0.f, 0.f);
    ASSERT_EQ(0.f, cmd2.az_deg);
    ASSERT_EQ(0.f, cmd2.el_deg);
    
    // Verify source is FREECAM
    ASSERT_EQ(GimbalSource::FREECAM, ctrl.source());
}

int main() {
    std::cout << "=== GimbalController Unit Tests ===\n\n";
    
    RUN_TEST(test_pixel_at_center_gives_zero_command);
    RUN_TEST(test_pixel_offset_gives_correct_angle);
    RUN_TEST(test_auto_source_accumulates_angle);
    RUN_TEST(test_freecam_source_sets_absolute);
    RUN_TEST(test_limits_clamp_commands);
    RUN_TEST(test_source_switch_auto_to_freecam);
    
    std::cout << "\n=== Results ===\n";
    std::cout << "Passed: " << g_pass << "\n";
    std::cout << "Failed: " << g_fail << "\n";
    
    return g_fail > 0 ? 1 : 0;
}
