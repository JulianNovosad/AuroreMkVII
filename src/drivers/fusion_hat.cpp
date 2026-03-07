/**
 * @file fusion_hat.cpp
 * @brief Sunfounder Fusion HAT+ PWM/servo driver implementation
 *
 * Implementation using Linux sysfs interface.
 * The Fusion HAT+ kernel driver handles all I2C communication internally.
 */

#include "aurore/fusion_hat.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aurore/timing.hpp"

namespace aurore {

// ============================================================================
// Sysfs Helper Functions
// ============================================================================

bool FusionHat::write_sysfs(const std::string& path, int value) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << value;
    return file.good();
}

int FusionHat::read_sysfs(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return -1;
    }
    int value;
    file >> value;
    if (file.fail()) {
        return -1;
    }
    return value;
}

std::string FusionHat::read_sysfs_string(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string result = buffer.str();
    
    // Remove trailing newline/null
    while (!result.empty() && (result.back() == '\n' || result.back() == '\0')) {
        result.pop_back();
    }
    return result;
}

// ============================================================================
// Constructor/Destructor
// ============================================================================

FusionHat::FusionHat(const FusionHatConfig& config)
    : config_(config) {
    
    if (!config_.validate()) {
        std::cerr << "FusionHat: Invalid configuration" << std::endl;
    }
}

FusionHat::~FusionHat() {
    disable_all_servos();
}

// ============================================================================
// Initialization
// ============================================================================

bool FusionHat::is_connected() const noexcept {
    // Check device tree for Fusion HAT+ UUID
    // Product ID 0x0774 is encoded in the UUID
    DIR* dir = opendir(DEVICE_TREE_PATH);
    if (!dir) {
        return false;
    }
    
    bool found = false;
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find("hat") != std::string::npos) {
            std::string uuid_path = std::string(DEVICE_TREE_PATH) + "/" + name + "/uuid";
            std::string uuid = read_sysfs_string(uuid_path);
            
            // UUID format: 9daeea78-0000-0774-000a-582369ac3e02
            // Product ID is in the third segment (0774)
            if (uuid.find("0774") != std::string::npos) {
                found = true;
                break;
            }
        }
    }
    
    closedir(dir);
    
    // Also check sysfs path exists
    if (found) {
        struct stat st;
        if (stat(SYSFS_BASE, &st) != 0) {
            found = false;
        }
    }
    
    return found;
}

bool FusionHat::init() {
    if (!is_connected()) {
        std::cerr << "FusionHat: Device not connected" << std::endl;
        return false;
    }
    
    // Initialize all PWM channels
    for (int ch = 0; ch < 12; ch++) {
        std::string pwm_path = get_pwm_path(ch);
        
        // Enable PWM channel
        if (!write_sysfs(pwm_path + "/enable", 1)) {
            std::cerr << "FusionHat: Failed to enable channel " << ch << std::endl;
            error_count_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        
        // Set period (20000μs = 50Hz)
        int period_us = 1000000 / config_.servo_freq_hz;
        if (!write_sysfs(pwm_path + "/period", period_us)) {
            std::cerr << "FusionHat: Failed to set period for channel " << ch << std::endl;
            error_count_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        
        // Set initial duty cycle to 0 (servo off)
        if (!write_sysfs(pwm_path + "/duty_cycle", 0)) {
            std::cerr << "FusionHat: Failed to set duty cycle for channel " << ch << std::endl;
            error_count_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        
        channels_[ch].enabled.store(false, std::memory_order_release);
        channels_[ch].current_pulse_width.store(1500, std::memory_order_release);  // Center
        channels_[ch].current_angle.store(0.0f, std::memory_order_release);
    }
    
    initialized_.store(true, std::memory_order_release);
    
    std::cout << "FusionHat: Initialized (" << get_driver_version() 
              << ", firmware " << get_firmware_version() << ")" << std::endl;
    
    return true;
}

std::string FusionHat::get_firmware_version() const {
    return read_sysfs_string(std::string(SYSFS_BASE) + "/firmware_version");
}

std::string FusionHat::get_driver_version() const {
    return read_sysfs_string(std::string(SYSFS_BASE) + "/version");
}

// ============================================================================
// Servo Control
// ============================================================================

bool FusionHat::set_servo_angle(int channel, float angle_deg) {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }
    
    if (channel < 0 || channel >= 12) {
        error_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    // Apply software endstops
    if (config_.enable_endstops) {
        const auto& ch = channels_[channel];
        float min_angle = ch.min_angle;
        float max_angle = ch.max_angle;
        
        if (angle_deg < min_angle) {
            angle_deg = min_angle;
        } else if (angle_deg > max_angle) {
            angle_deg = max_angle;
        }
    }
    
    // Apply global limits
    angle_deg = std::clamp(angle_deg, config_.min_angle_deg, config_.max_angle_deg);
    
    // Convert to pulse width
    int pulse_width = angle_to_pulse_width(angle_deg);
    
    // Set pulse width
    if (!set_servo_pulse_width(channel, pulse_width)) {
        return false;
    }
    
    // Update channel state
    channels_[channel].current_angle.store(angle_deg, std::memory_order_release);
    channels_[channel].current_pulse_width.store(pulse_width, std::memory_order_release);
    channels_[channel].last_update_ns.store(get_timestamp(), std::memory_order_release);
    channels_[channel].enabled.store(true, std::memory_order_release);
    
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
    
    std::string pwm_path = get_pwm_path(channel);
    
    if (!write_sysfs(pwm_path + "/duty_cycle", pulse_width_us)) {
        error_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    command_count_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

int FusionHat::get_pulse_width(int channel) const {
    if (channel < 0 || channel >= 12) {
        return -1;
    }
    
    std::string pwm_path = get_pwm_path(channel);
    return read_sysfs(pwm_path + "/duty_cycle");
}

bool FusionHat::set_servo_enabled(int channel, bool enable) {
    if (channel < 0 || channel >= 12) {
        return false;
    }
    
    std::string pwm_path = get_pwm_path(channel);
    
    if (!write_sysfs(pwm_path + "/enable", enable ? 1 : 0)) {
        error_count_.fetch_add(1, std::memory_order_relaxed);
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
        const auto& ch = channels_[channel];
        status.angle_deg = ch.current_angle.load(std::memory_order_acquire);
        status.pulse_width_us = ch.current_pulse_width.load(std::memory_order_acquire);
        status.enabled = ch.enabled.load(std::memory_order_acquire);
        status.last_update_ns = ch.last_update_ns.load(std::memory_order_acquire);
        status.endstop_active = (status.angle_deg <= channels_[channel].min_angle ||
                                 status.angle_deg >= channels_[channel].max_angle);
    }
    
    return status;
}

void FusionHat::disable_all_servos() {
    for (int ch = 0; ch < 12; ch++) {
        set_servo_enabled(ch, false);
    }
}

// ============================================================================
// PWM Control
// ============================================================================

bool FusionHat::set_pwm_freq(int channel, int freq_hz) {
    if (channel < 0 || channel >= 12 || freq_hz <= 0 || freq_hz > 1000) {
        return false;
    }
    
    int period_us = 1000000 / freq_hz;
    return set_pwm_period(channel, period_us);
}

int FusionHat::get_pwm_freq(int channel) const {
    int period = get_pwm_period(channel);
    if (period <= 0) {
        return -1;
    }
    return 1000000 / period;
}

bool FusionHat::set_pwm_duty_cycle(int channel, int duty_percent) {
    if (channel < 0 || channel >= 12 || duty_percent < 0 || duty_percent > 100) {
        return false;
    }
    
    int period = get_pwm_period(channel);
    if (period <= 0) {
        return false;
    }
    
    int duty_cycle = (period * duty_percent) / 100;
    return write_sysfs(get_pwm_path(channel) + "/duty_cycle", duty_cycle);
}

int FusionHat::get_pwm_duty_cycle(int channel) const {
    int period = get_pwm_period(channel);
    int duty_cycle = read_sysfs(get_pwm_path(channel) + "/duty_cycle");
    
    if (period <= 0 || duty_cycle < 0) {
        return -1;
    }
    
    return (duty_cycle * 100) / period;
}

bool FusionHat::set_pwm_period(int channel, int period_us) {
    if (channel < 0 || channel >= 12 || period_us <= 0) {
        return false;
    }
    
    return write_sysfs(get_pwm_path(channel) + "/period", period_us);
}

int FusionHat::get_pwm_period(int channel) const {
    if (channel < 0 || channel >= 12) {
        return -1;
    }
    return read_sysfs(get_pwm_path(channel) + "/period");
}

// ============================================================================
// Safety & Configuration
// ============================================================================

void FusionHat::set_endstop_limits(int channel, float min_angle_deg, float max_angle_deg) {
    if (channel >= 0 && channel < 12 && min_angle_deg < max_angle_deg) {
        channels_[channel].min_angle = min_angle_deg;
        channels_[channel].max_angle = max_angle_deg;
    }
}

void FusionHat::set_rate_limit(bool enable, float max_velocity_dps) {
    config_.enable_rate_limit = enable;
    config_.max_angular_velocity_dps = max_velocity_dps;
}

// ============================================================================
// Helper Functions
// ============================================================================

int FusionHat::angle_to_pulse_width(float angle_deg) const noexcept {
    // Linear mapping: angle -> pulse_width
    // min_angle -> min_pulse_width
    // max_angle -> max_pulse_width
    
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
    return std::string(SYSFS_BASE) + "/pwm" + std::to_string(channel);
}

}  // namespace aurore
