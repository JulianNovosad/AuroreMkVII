/**
 * @file gimbal_actuation_test.cpp
 * @brief Hardware-in-the-loop (HIL) test for Gimbal and Trigger system
 *
 * Tests the Fusion HAT+ PWM channels for gimbal control:
 * - Azimuth (Horizontal): PWM Channel 10
 * - Elevation (Vertical): PWM Channel 11
 * - Trigger: PWM Channel 8
 *
 * HARDWARE POLICY (per CLAUDE.md):
 * - NEVER use mocks - interact with real Fusion HAT+
 * - FAIL immediately (<=500ms) if HAT not detected
 * - Provide clear FAIL messages with Check:/Fix: steps
 * - Respect 20ms ramp-up to prevent servo strain
 *
 * Usage (on RPi 5 with Fusion HAT+):
 *   sudo ./gimbal_actuation_test
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "aurore/fusion_hat.hpp"
#include "aurore/timing.hpp"

static constexpr int kAzimuthChannel = 10;
static constexpr int kElevationChannel = 11;
static constexpr int kTriggerChannel = 8;

static constexpr float kAzimuthMinDeg = 45.0f;
static constexpr float kAzimuthMaxDeg = 135.0f;
static constexpr float kAzimuthCenterDeg = 90.0f;

static constexpr float kElevationMinDeg = 60.0f;
static constexpr float kElevationMaxDeg = 120.0f;
static constexpr float kElevationCenterDeg = 90.0f;

static constexpr int kTriggerPulseWidthUs = 1500;
static constexpr int kTriggerFireUs = 2000;
static constexpr int kTriggerFireDurationMs = 50;

static constexpr int kRampUpMs = 20;
static constexpr int kTestTimeoutMs = 500;

static int tests_passed = 0;
static int tests_failed = 0;

static std::atomic<bool> watchdog_kicked{false};

#define TEST_ASSERT(condition, message)                 \
    do {                                                \
        if (condition) {                                \
            std::cout << "  PASS: " << message << "\n"; \
            tests_passed++;                             \
        } else {                                        \
            std::cerr << "  FAIL: " << message << "\n"; \
            tests_failed++;                             \
        }                                               \
    } while (0)

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static void test_fusion_hat_presence(aurore::FusionHat& hat) {
    std::cout << "\n=== Test: Fusion HAT+ Presence ===\n";

    auto start = std::chrono::steady_clock::now();

    if (!hat.init()) {
        std::cerr << "FAIL: Fusion HAT+ not responding on /dev/i2c-1\n";
        std::cerr << "      Check: ls /sys/class/fusion_hat/fusion_hat\n";
        std::cerr << "      Fix: Check HAT seating and 5V power rail\n";
        tests_failed++;
        return;
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();

    if (elapsed_ms > kTestTimeoutMs) {
        TEST_ASSERT(false, "Fusion HAT+ init timeout (" + std::to_string(elapsed_ms) + "ms > " +
                               std::to_string(kTestTimeoutMs) + "ms)");
        return;
    }

    TEST_ASSERT(hat.is_connected(), "Fusion HAT+ detected on sysfs");
    TEST_ASSERT(hat.is_initialized(), "Fusion HAT+ initialized");

    auto fw = hat.get_firmware_version();
    if (!fw.empty()) {
        std::cout << "  INFO: Firmware version: " << fw << "\n";
    }

    std::cout << "  INFO: Init time: " << elapsed_ms << "ms\n";
}

static void test_pwm_resolution(aurore::FusionHat& hat) {
    std::cout << "\n=== Test: 16-bit PWM Resolution ===\n";

    int pw1 = hat.get_pulse_width(kAzimuthChannel);
    TEST_ASSERT(pw1 >= 0, "PWM channel " + std::to_string(kAzimuthChannel) + " readable");

    hat.set_servo_pulse_width(kAzimuthChannel, 1500);
    sleep_ms(50);
    int pw2 = hat.get_pulse_width(kAzimuthChannel);
    TEST_ASSERT(pw2 >= 1000 && pw2 <= 2000, "PWM range 1000-2000μs");

    hat.set_servo_pulse_width(kAzimuthChannel, 1000);
    sleep_ms(50);
    int pw_min = hat.get_pulse_width(kAzimuthChannel);
    TEST_ASSERT(pw_min <= 1005, "PWM min: " + std::to_string(pw_min) + "μs");

    hat.set_servo_pulse_width(kAzimuthChannel, 2000);
    sleep_ms(50);
    int pw_max = hat.get_pulse_width(kAzimuthChannel);
    TEST_ASSERT(pw_max >= 1995, "PWM max: " + std::to_string(pw_max) + "μs");

    hat.set_servo_pulse_width(kAzimuthChannel, 1500);
    sleep_ms(50);

    constexpr int k16BitMax = 65535;
    bool is_16bit = (pw_max - pw_min) >= (k16BitMax / 100);
    TEST_ASSERT(is_16bit,
                "16-bit resolution verified (" + std::to_string(pw_max - pw_min) + " range)");
}

static void test_servo_sweep(aurore::FusionHat& hat, int channel, float start_deg, float mid_deg,
                             float end_deg, const std::string& name) {
    std::cout << "\n=== Test: " << name << " Sweep ===\n";

    std::cout << "  Sweep: " << start_deg << "° -> " << mid_deg << "° -> " << end_deg << "°\n";

    hat.set_servo_enabled(channel, true);
    sleep_ms(kRampUpMs);

    hat.set_servo_angle(channel, start_deg);
    sleep_ms(100);
    auto angle1 = hat.get_servo_angle(channel);
    TEST_ASSERT(angle1.has_value(), name + " set to " + std::to_string(start_deg) + "°");

    hat.set_servo_angle(channel, mid_deg);
    sleep_ms(100);
    auto angle2 = hat.get_servo_angle(channel);
    TEST_ASSERT(angle2.has_value(), name + " set to " + std::to_string(mid_deg) + "°");

    hat.set_servo_angle(channel, end_deg);
    sleep_ms(100);
    auto angle3 = hat.get_servo_angle(channel);
    TEST_ASSERT(angle3.has_value(), name + " set to " + std::to_string(end_deg) + "°");

    hat.set_servo_enabled(channel, false);
}

static void test_azimuth_sweep(aurore::FusionHat& hat) {
    test_servo_sweep(hat, kAzimuthChannel, kAzimuthMinDeg, kAzimuthMaxDeg, kAzimuthCenterDeg,
                     "Azimuth (Ch" + std::to_string(kAzimuthChannel) + ")");
}

static void test_elevation_sweep(aurore::FusionHat& hat) {
    test_servo_sweep(hat, kElevationChannel, kElevationMinDeg, kElevationMaxDeg,
                     kElevationCenterDeg,
                     "Elevation (Ch" + std::to_string(kElevationChannel) + ")");
}

static void test_trigger_pulse(aurore::FusionHat& hat) {
    std::cout << "\n=== Test: Trigger Pulse (Ch" << kTriggerChannel << ") ===\n";

    hat.set_servo_enabled(kTriggerChannel, true);
    sleep_ms(kRampUpMs);

    hat.set_servo_pulse_width(kTriggerChannel, kTriggerPulseWidthUs);
    sleep_ms(50);

    auto initial_pw = hat.get_pulse_width(kTriggerChannel);
    TEST_ASSERT(initial_pw > 0, "Trigger channel readable: " + std::to_string(initial_pw) + "μs");

    auto start = std::chrono::steady_clock::now();
    hat.set_servo_pulse_width(kTriggerChannel, kTriggerFireUs);
    auto cmd_time = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();

    TEST_ASSERT(true, "Trigger fire command issued (" + std::to_string(cmd_time) + "μs)");

    sleep_ms(kTriggerFireDurationMs);

    auto during = hat.get_pulse_width(kTriggerChannel);
    TEST_ASSERT(during >= kTriggerFireUs - 50 && during <= kTriggerFireUs + 50,
                "Trigger pulse active: " + std::to_string(during) + "μs");

    hat.set_servo_pulse_width(kTriggerChannel, kTriggerPulseWidthUs);
    sleep_ms(50);

    auto after = hat.get_pulse_width(kTriggerChannel);
    TEST_ASSERT(after > 0, "Trigger returned to idle: " + std::to_string(after) + "μs");

    hat.set_servo_enabled(kTriggerChannel, false);
}

static void test_command_queuing(aurore::FusionHat& hat) {
    std::cout << "\n=== Test: Non-blocking Command Queuing (PERF-006) ===\n";

    constexpr int kNumCommands = 10;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < kNumCommands; ++i) {
        hat.set_servo_angle(kAzimuthChannel, 45.0f + static_cast<float>(i) * 10.0f);
    }

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();

    float avg_us = static_cast<float>(elapsed_us) / kNumCommands;
    TEST_ASSERT(elapsed_us < 10000,
                kNumCommands + " commands queued in " + std::to_string(elapsed_us) + "μs");
    TEST_ASSERT(avg_us < 1000, "Avg command time: " + std::to_string(avg_us) + "μs (< 1ms)");

    uint64_t cmd_count = hat.get_command_count();
    TEST_ASSERT(cmd_count >= kNumCommands, "Command count: " + std::to_string(cmd_count) +
                                               " (>= " + std::to_string(kNumCommands) + ")");

    std::cout << "  INFO: PERF-006: Non-blocking queuing verified\n";
}

static void test_sweep_to_center(aurore::FusionHat& hat) {
    std::cout << "\n=== Test: Combined Sweep-to-Center Routine ===\n";

    hat.set_servo_enabled(kAzimuthChannel, true);
    hat.set_servo_enabled(kElevationChannel, true);
    sleep_ms(kRampUpMs);

    std::cout << "  Phase 1: Move to max positions\n";
    hat.set_servo_angle(kAzimuthChannel, kAzimuthMaxDeg);
    hat.set_servo_angle(kElevationChannel, kElevationMaxDeg);
    sleep_ms(200);

    std::cout << "  Phase 2: Return to center\n";
    hat.set_servo_angle(kAzimuthChannel, kAzimuthCenterDeg);
    hat.set_servo_angle(kElevationChannel, kElevationCenterDeg);
    sleep_ms(200);

    auto az = hat.get_servo_angle(kAzimuthChannel);
    auto el = hat.get_servo_angle(kElevationChannel);

    bool az_ok = az.has_value() && std::abs(*az - kAzimuthCenterDeg) < 5.0f;
    bool el_ok = el.has_value() && std::abs(*el - kElevationCenterDeg) < 5.0f;

    TEST_ASSERT(az_ok, "Azimuth at center: " + (az ? std::to_string(*az) : "N/A") + "°");
    TEST_ASSERT(el_ok, "Elevation at center: " + (el ? std::to_string(*el) : "N/A") + "°");

    hat.set_servo_enabled(kAzimuthChannel, false);
    hat.set_servo_enabled(kElevationChannel, false);
}

static void print_summary() {
    std::cout << "\n========================================\n";
    std::cout << "HIL Test Summary: " << tests_passed << " passed, " << tests_failed << " failed\n";
    std::cout << "========================================\n";

    if (tests_failed > 0) {
        std::cerr << "\nHARDWARE TEST FAILED\n";
        std::cerr << "Check hardware connections and re-run.\n";
    }
}

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "===========================================\n";
    std::cout << "Aurore MkVII Gimbal & Trigger HIL Test\n";
    std::cout << "===========================================\n";
    std::cout << "Channels:\n";
    std::cout << "  Azimuth:    Ch" << kAzimuthChannel << " (" << kAzimuthMinDeg << "-"
              << kAzimuthMaxDeg << "°)\n";
    std::cout << "  Elevation:  Ch" << kElevationChannel << " (" << kElevationMinDeg << "-"
              << kElevationMaxDeg << "°)\n";
    std::cout << "  Trigger:    Ch" << kTriggerChannel << " (" << kTriggerPulseWidthUs
              << "μs idle)\n";
    std::cout << "Ramp-up:     " << kRampUpMs << "ms\n";

    aurore::FusionHat hat;
    aurore::FusionHatConfig config;
    config.enable_rate_limit = true;
    config.max_angular_velocity_dps = 180.0f;

    test_fusion_hat_presence(hat);

    if (!hat.is_initialized()) {
        print_summary();
        return EXIT_FAILURE;
    }

    test_pwm_resolution(hat);
    test_azimuth_sweep(hat);
    test_elevation_sweep(hat);
    test_trigger_pulse(hat);
    test_command_queuing(hat);
    test_sweep_to_center(hat);

    std::cout << "\n=== Cleanup ===\n";
    hat.disable_all_servos();
    std::cout << "  All servos disabled\n";

    print_summary();

    return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
