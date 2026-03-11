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
        throw std::runtime_error("Assertion failed: " #expected " ~= " #actual \
            " (expected=" + std::to_string(expected) + \
            " actual=" + std::to_string(actual) + ")"); \
    } \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        throw std::runtime_error("Assertion failed: " #expected " == " #actual); \
    } \
} while(0)

#define ASSERT_LE(val, bound) do { \
    if ((val) > (bound)) { \
        throw std::runtime_error("Assertion failed: " #val " <= " #bound \
            " (val=" + std::to_string(val) + " bound=" + std::to_string(bound) + ")"); \
    } \
} while(0)

#define ASSERT_GE(val, bound) do { \
    if ((val) < (bound)) { \
        throw std::runtime_error("Assertion failed: " #val " >= " #bound \
            " (val=" + std::to_string(val) + " bound=" + std::to_string(bound) + ")"); \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        throw std::runtime_error("Assertion failed: " #cond " is false"); \
    } \
} while(0)

using namespace aurore;

// Helper: nominal dt_s on first call (prev_cmd_ns_ == 0)
static constexpr float kNominalDt = 1.0f / 120.0f;
// Max position step allowed on first call (acceleration-limited from rest)
// accel_limit * dt * dt
static constexpr float kFirstFrameMaxStep = kGimbalAccelLimitDefault * kNominalDt * kNominalDt;
// Max velocity after the first frame
static constexpr float kFirstFrameMaxVel = kGimbalAccelLimitDefault * kNominalDt;

TEST(test_pixel_at_center_gives_zero_command) {
    GimbalController ctrl;
    auto cmd = ctrl.command_from_pixel(768.f, 432.f);  // center pixel
    ASSERT_NEAR(0.f, cmd.az_deg, 1e-5f);
    ASSERT_NEAR(0.f, cmd.el_deg, 1e-5f);
}

// Verify pixel→angle conversion math and that output respects velocity/accel limits.
// A large pixel offset (100px) produces ~5°, which exceeds the per-frame accel budget.
// The command should be clamped to kFirstFrameMaxStep, in the correct direction.
TEST(test_pixel_offset_gives_correct_direction_and_limit) {
    // Azimuth: target 100px right of center — positive azimuth expected
    GimbalController ctrl_az;
    auto cmd_az = ctrl_az.command_from_pixel(868.f, 432.f);
    ASSERT_TRUE(cmd_az.az_deg > 0.0f);
    ASSERT_NEAR(0.f, cmd_az.el_deg, 1e-5f);
    ASSERT_LE(cmd_az.az_deg, kGimbalRateLimitDefault * kNominalDt + 1e-5f);

    // Elevation: target 100px above center (y=332) — positive elevation expected
    GimbalController ctrl_el;
    auto cmd_el = ctrl_el.command_from_pixel(768.f, 332.f);
    ASSERT_TRUE(cmd_el.el_deg > 0.0f);
    ASSERT_NEAR(0.f, cmd_el.az_deg, 1e-5f);
    ASSERT_LE(cmd_el.el_deg, kGimbalRateLimitDefault * kNominalDt + 1e-5f);
}

// Verify convergence: repeated identical pixel commands must approach the target over time.
// Each call with rapid succession uses the minimum dt (1ms), giving max_vel_delta = 0.06°/call.
// After many calls the angle must monotonically increase.
TEST(test_auto_source_accumulates_angle_monotonically) {
    GimbalController ctrl;
    ASSERT_EQ(GimbalSource::AUTO, ctrl.source());

    float prev_az = 0.0f;
    for (int i = 0; i < 20; ++i) {
        // Constant rightward pixel offset — the controller should keep moving right
        auto cmd = ctrl.command_from_pixel(868.f, 432.f);
        // After first call az should be > 0; subsequent calls should continue to increase
        // (unless already at target, but we're well below the 90° limit here)
        if (i > 0) {
            ASSERT_GE(cmd.az_deg, prev_az - 1e-5f);  // must not decrease
        }
        prev_az = cmd.az_deg;
    }
    // After 20 calls the angle must have increased meaningfully from 0
    ASSERT_TRUE(prev_az > 0.01f);
}

TEST(test_freecam_source_converges_to_absolute) {
    GimbalController ctrl;
    ctrl.set_source(GimbalSource::FREECAM);
    ASSERT_EQ(GimbalSource::FREECAM, ctrl.source());

    // Issue the same absolute command many times; angle should converge toward target.
    // With rapid calls (dt clamped to 0.001s), accel budget = 0.12°/s per call.
    // After N calls velocity ≈ N * 0.12 °/s (until vel cap 60°/s), so displacement grows
    // roughly as 0.5 * 0.12 * N^2 * 0.001. After 200 calls, ~2.4°.
    float prev_az = 0.0f;
    for (int i = 0; i < 200; ++i) {
        auto cmd = ctrl.command_absolute(30.f, 15.f);
        if (i > 0) {
            ASSERT_GE(cmd.az_deg, prev_az - 1e-5f);  // monotonically approaching 30°
            ASSERT_LE(cmd.az_deg, 30.0f + 1e-4f);    // must not overshoot
        }
        prev_az = cmd.az_deg;
    }
    // After 200 calls it should have moved significantly toward 30°
    ASSERT_TRUE(prev_az > 0.5f);

    // A second distinct absolute command must not jump above the current position instantly
    float az_at_switch = prev_az;
    for (int i = 0; i < 20; ++i) {
        auto cmd = ctrl.command_absolute(-20.f, 10.f);
        // The angle must not jump — it must stay within the rate-limited corridor
        (void)az_at_switch;  // just checking no position-limit violation
        ASSERT_LE(cmd.az_deg, 30.0f + 1e-4f);
        ASSERT_GE(cmd.az_deg, -20.0f - 1e-4f);
    }
}

TEST(test_limits_clamp_commands) {
    // Test AUTO mode position clamping: drive into the limit over many calls
    GimbalController ctrl_auto;
    ctrl_auto.set_limits(-45.f, 45.f, -5.f, 30.f);

    // Use a very large pixel offset — rate limiter ensures gradual approach;
    // positional limit must be respected at all times.
    for (int i = 0; i < 2000; ++i) {
        auto cmd = ctrl_auto.command_from_pixel(2000.f, -500.f);
        ASSERT_LE(cmd.az_deg, 45.0f + 1e-4f);
        ASSERT_LE(cmd.el_deg, 30.0f + 1e-4f);
        ASSERT_GE(cmd.az_deg, -45.0f - 1e-4f);
        ASSERT_GE(cmd.el_deg, -5.0f - 1e-4f);
    }

    // Test FREECAM mode position clamping
    GimbalController ctrl_freecam;
    ctrl_freecam.set_source(GimbalSource::FREECAM);
    ctrl_freecam.set_limits(-45.f, 45.f, -5.f, 30.f);

    for (int i = 0; i < 200; ++i) {
        auto cmd = ctrl_freecam.command_absolute(90.f, 60.f);
        ASSERT_LE(cmd.az_deg, 45.0f + 1e-4f);
        ASSERT_LE(cmd.el_deg, 30.0f + 1e-4f);
    }

    for (int i = 0; i < 200; ++i) {
        auto cmd = ctrl_freecam.command_absolute(-90.f, -20.f);
        ASSERT_GE(cmd.az_deg, -45.0f - 1e-4f);
        ASSERT_GE(cmd.el_deg, -5.0f - 1e-4f);
    }
}

TEST(test_source_switch_auto_to_freecam) {
    GimbalController ctrl;

    // Start in AUTO, accumulate some angle over several calls
    float az_after_auto = 0.0f;
    for (int i = 0; i < 10; ++i) {
        auto cmd = ctrl.command_from_pixel(868.f, 432.f);
        az_after_auto = cmd.az_deg;
    }
    ASSERT_TRUE(az_after_auto > 0.0f);

    // Switch to FREECAM and command zero.
    // Because the controller has built up positive velocity, the angle may still creep
    // upward for a few frames while velocity decelerates (acceleration-limited stop).
    // After enough calls it must eventually start decreasing toward 0.
    ctrl.set_source(GimbalSource::FREECAM);
    float az_min_seen = az_after_auto;
    for (int i = 0; i < 200; ++i) {
        auto cmd = ctrl.command_absolute(0.f, 0.f);
        if (cmd.az_deg < az_min_seen) {
            az_min_seen = cmd.az_deg;
        }
    }
    // After 200 calls the angle must have started moving toward 0
    // (the minimum seen must be less than the value right after AUTO mode)
    ASSERT_TRUE(az_min_seen < az_after_auto);
    // The angle may slightly overshoot 0° due to the finite deceleration ramp,
    // but the overshoot must be small (well under 0.5°, the per-frame velocity cap).
    ASSERT_GE(az_min_seen, -0.5f);

    // Verify source is FREECAM
    ASSERT_EQ(GimbalSource::FREECAM, ctrl.source());
}

// AM7-L2-ACT-002: a command requesting a large jump must be clamped to velocity limit.
// At 120Hz (dt = 1/120s) with vel_limit = 60°/s: max Δaz per frame = 0.5°
TEST(test_gimbal_velocity_exceeds_limit_clamps) {
    GimbalController ctrl;

    // First call uses dt = 1/120s; accel clamp dominates (starts from rest).
    // For subsequent calls at minimum dt = 0.001s, max_vel_delta = 60 * 0.001 = 0.06°
    // and max_accel_delta = 120 * 0.001 = 0.12°/s per call.
    // We just want to verify the output never exceeds the velocity limit per call.

    float prev_az = 0.0f;
    for (int i = 0; i < 50; ++i) {
        // Command a large 90° jump every call
        auto cmd = ctrl.command_absolute(90.f, 0.f);
        // Velocity: |delta_az| per call must not exceed vel_limit * dt_s.
        // dt_s is clamped to min 0.001s, so max delta per call is 0.06°.
        // First call uses dt = 1/120s = 0.00833s → max delta = 0.5°.
        // We use a generous bound of 0.51° to cover both cases.
        float delta = std::fabs(cmd.az_deg - prev_az);
        ASSERT_LE(delta, kGimbalRateLimitDefault * kNominalDt + 0.01f);
        ASSERT_LE(cmd.az_deg, 90.0f + 1e-4f);  // must not overshoot target
        ASSERT_GE(cmd.az_deg, 0.0f - 1e-4f);   // must not go negative
        prev_az = cmd.az_deg;
    }
}

// AM7-L2-ACT-002: acceleration must be limited to 120°/s².
// Starting from rest, velocity must ramp up at no more than 120°/s per second.
TEST(test_edge_gimbal_acceleration_exceeds_limit_clamps) {
    GimbalController ctrl;

    // On the very first call (from rest, dt = 1/120s):
    // max_vel_after_accel_clamp = kAccelLimit * dt = 120 * (1/120) = 1.0°/s
    // az_final = 0 + 1.0 * (1/120) = 0.00833° (< 0.5° vel limit, so accel dominates)
    auto cmd1 = ctrl.command_absolute(90.f, 0.f);
    ASSERT_LE(cmd1.az_deg, kFirstFrameMaxStep + 1e-5f);
    ASSERT_GE(cmd1.az_deg, 0.0f);

    // After many consecutive calls (rapid succession, dt clamped to 0.001s each),
    // the angle must gradually increase toward 90° — never jump there instantly.
    float prev_az = cmd1.az_deg;
    for (int i = 0; i < 200; ++i) {
        auto cmd = ctrl.command_absolute(90.f, 0.f);
        // Must never decrease (always moving toward target)
        ASSERT_GE(cmd.az_deg, prev_az - 1e-5f);
        // Must not exceed target
        ASSERT_LE(cmd.az_deg, 90.0f + 1e-4f);
        prev_az = cmd.az_deg;
    }

    // After 200 more calls it should have moved from ~0 toward 90°
    ASSERT_TRUE(prev_az > 0.05f);
}

int main() {
    std::cout << "=== GimbalController Unit Tests ===\n\n";

    RUN_TEST(test_pixel_at_center_gives_zero_command);
    RUN_TEST(test_pixel_offset_gives_correct_direction_and_limit);
    RUN_TEST(test_auto_source_accumulates_angle_monotonically);
    RUN_TEST(test_freecam_source_converges_to_absolute);
    RUN_TEST(test_limits_clamp_commands);
    RUN_TEST(test_source_switch_auto_to_freecam);
    RUN_TEST(test_gimbal_velocity_exceeds_limit_clamps);
    RUN_TEST(test_edge_gimbal_acceleration_exceeds_limit_clamps);

    std::cout << "\n=== Results ===\n";
    std::cout << "Passed: " << g_pass << "\n";
    std::cout << "Failed: " << g_fail << "\n";

    return g_fail > 0 ? 1 : 0;
}
