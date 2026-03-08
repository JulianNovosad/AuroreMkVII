/**
 * @file mock_fusion_hat.cpp
 * @brief Mock Fusion HAT+ implementation for laptop development (no I2C hardware)
 *
 * Provides state storage and logging for servo commands without
 * actual I2C communication.
 */

#include "aurore/fusion_hat.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

#include "aurore/timing.hpp"

namespace aurore {

// ============================================================================
// FusionHat Implementation (Mock for laptop)
// ============================================================================

FusionHat::FusionHat(const FusionHatConfig& config)
    : config_(config) {

    if (!config_.validate()) {
        std::cerr << "FusionHat: Invalid configuration (mock)" << std::endl;
    }
}

FusionHat::~FusionHat() {
    disable_all_servos();
}

bool FusionHat::is_connected() const noexcept {
    // Mock: always "connected" in laptop mode
    return true;
}

bool FusionHat::init() {
    // Mock initialization
    std::cout << "MockFusionHat: Initializing (no I2C hardware)" << std::endl;

    // Initialize all channels to center position
    for (int ch = 0; ch < 12; ch++) {
        channels_[ch].current_angle.store(0.0f, std::memory_order_release);
        channels_[ch].enabled.store(false, std::memory_order_release);
        channels_[ch].current_pulse_width.store(1500, std::memory_order_release);
    }

    initialized_.store(true, std::memory_order_release);
    std::cout << "MockFusionHat: Initialized (firmware: mock, driver: "
              << get_driver_version() << ")" << std::endl;

    return true;
}

std::string FusionHat::get_firmware_version() const {
    return "mock-0.0.0";
}

std::string FusionHat::get_driver_version() const {
    return "mock-1.0.0";
}

bool FusionHat::set_servo_angle(int channel, float angle_deg) {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }

    if (channel < 0 || channel >= 12) {
        error_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Apply software endstops if enabled
    if (config_.enable_endstops) {
        float min_angle = -90.0f;  // Default limits for mock
        float max_angle = 90.0f;
        
        if (angle_deg < min_angle) angle_deg = min_angle;
        if (angle_deg > max_angle) angle_deg = max_angle;
    }

    // Apply global limits
    float clamped_angle = angle_deg;
    if (clamped_angle < config_.min_angle_deg) {
        clamped_angle = config_.min_angle_deg;
    } else if (clamped_angle > config_.max_angle_deg) {
        clamped_angle = config_.max_angle_deg;
    }

    // Convert to pulse width
    int pulse_width = angle_to_pulse_width(clamped_angle);

    // Store commanded angle
    channels_[channel].current_angle.store(clamped_angle, std::memory_order_release);
    channels_[channel].current_pulse_width.store(pulse_width, std::memory_order_release);
    channels_[channel].enabled.store(true, std::memory_order_release);
    channels_[channel].last_update_ns.store(get_timestamp(), std::memory_order_release);
    command_count_.fetch_add(1, std::memory_order_relaxed);

    return true;
}

std::optional<float> FusionHat::get_servo_angle(int channel) const {
    if (channel < 0 || channel >= 12) {
        return std::nullopt;
    }
    return channels_[channel].current_angle.load(std::memory_order_acquire);
}

bool FusionHat::set_servo_pulse_width(int channel, int pulse_width_us) {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }

    if (channel < 0 || channel >= 12) {
        error_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Validate pulse width
    if (pulse_width_us < config_.min_pulse_width_us ||
        pulse_width_us > config_.max_pulse_width_us) {
        error_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Convert pulse width to angle for mock
    float angle = pulse_width_to_angle(pulse_width_us);
    return set_servo_angle(channel, angle);
}

int FusionHat::get_pulse_width(int channel) const {
    if (channel < 0 || channel >= 12) {
        return -1;
    }

    return channels_[channel].current_pulse_width.load(std::memory_order_acquire);
}

bool FusionHat::set_servo_enabled(int channel, bool enable) {
    if (channel < 0 || channel >= 12) {
        return false;
    }

    channels_[channel].enabled.store(enable, std::memory_order_release);
    return true;
}

bool FusionHat::is_servo_enabled(int channel) const {
    if (channel < 0 || channel >= 12) {
        return false;
    }
    return channels_[channel].enabled.load(std::memory_order_acquire);
}

ServoStatus FusionHat::get_servo_status(int channel) const {
    ServoStatus status{};
    status.channel = channel;

    if (channel >= 0 && channel < 12) {
        status.angle_deg = channels_[channel].current_angle.load(std::memory_order_acquire);
        status.pulse_width_us = channels_[channel].current_pulse_width.load(std::memory_order_acquire);
        status.enabled = channels_[channel].enabled.load(std::memory_order_acquire);
        status.last_update_ns = channels_[channel].last_update_ns.load(std::memory_order_acquire);
        status.endstop_active = false;  // Mock: no endstops
    }

    return status;
}

void FusionHat::disable_all_servos() {
    for (int ch = 0; ch < 12; ch++) {
        channels_[ch].current_angle.store(0.0f, std::memory_order_release);
        channels_[ch].enabled.store(false, std::memory_order_release);
    }
    std::cout << "MockFusionHat: All servos disabled" << std::endl;
}

bool FusionHat::set_pwm_freq(int channel, int freq_hz) {
    (void)channel;
    (void)freq_hz;
    // Mock: no-op
    return true;
}

int FusionHat::get_pwm_freq(int channel) const {
    (void)channel;
    return 50;  // Mock: default 50Hz
}

bool FusionHat::set_pwm_duty_cycle(int channel, int duty_percent) {
    if (channel < 0 || channel >= 12 || duty_percent < 0 || duty_percent > 100) {
        return false;
    }
    // Mock: no-op (angle-based control used instead)
    return true;
}

int FusionHat::get_pwm_duty_cycle(int channel) const {
    if (channel < 0 || channel >= 12) {
        return -1;
    }
    // Mock: calculate from angle
    float angle = channels_[channel].current_angle.load(std::memory_order_acquire);
    float ratio = (angle - config_.min_angle_deg) / 
                  (config_.max_angle_deg - config_.min_angle_deg);
    return static_cast<int>(ratio * 100);
}

bool FusionHat::set_pwm_period(int channel, int period_us) {
    (void)channel;
    (void)period_us;
    // Mock: no-op
    return true;
}

int FusionHat::get_pwm_period(int channel) const {
    (void)channel;
    return 20000;  // Mock: default 20ms (50Hz)
}

void FusionHat::set_endstop_limits(int channel, float min_angle_deg, float max_angle_deg) {
    // Mock: limits stored in config, not per-channel
    (void)channel;
    (void)min_angle_deg;
    (void)max_angle_deg;
}

void FusionHat::set_rate_limit(bool enable, float max_velocity_dps) {
    config_.enable_rate_limit = enable;
    config_.max_angular_velocity_dps = max_velocity_dps;
}

int FusionHat::angle_to_pulse_width(float angle_deg) const noexcept {
    // Linear mapping: angle -> pulse_width
    float ratio = (angle_deg - config_.min_angle_deg) /
                  (config_.max_angle_deg - config_.min_angle_deg);

    ratio = std::clamp(ratio, 0.0f, 1.0f);

    int pulse_width = static_cast<int>(
        config_.min_pulse_width_us +
        ratio * (config_.max_pulse_width_us - config_.min_pulse_width_us)
    );

    return std::clamp(pulse_width, config_.min_pulse_width_us, config_.max_pulse_width_us);
}

float FusionHat::pulse_width_to_angle(int pulse_width_us) const noexcept {
    // Linear mapping: pulse_width -> angle

    float ratio = static_cast<float>(pulse_width_us - config_.min_pulse_width_us) /
                  static_cast<float>(config_.max_pulse_width_us - config_.min_pulse_width_us);

    ratio = std::clamp(ratio, 0.0f, 1.0f);

    return config_.min_angle_deg + ratio * (config_.max_angle_deg - config_.min_angle_deg);
}

std::string FusionHat::get_pwm_path(int channel) const {
    std::ostringstream ss;
    ss << "/mock/pwm" << channel;
    return ss.str();
}

}  // namespace aurore
