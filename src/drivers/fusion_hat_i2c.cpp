#include "aurore/fusion_hat.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <chrono>
#include <time.h>

namespace aurore {

FusionHat::FusionHat() = default;

FusionHat::~FusionHat() {
    stop_async_mode();
    if (i2c_fd_ >= 0) {
        ::close(i2c_fd_);
        i2c_fd_ = -1;
    }
}

bool FusionHat::init(const std::string& i2c_device) {
    i2c_fd_ = ::open(i2c_device.c_str(), O_RDWR);
    if (i2c_fd_ < 0) {
        std::cerr << "FusionHat: failed to open " << i2c_device << ": "
                  << strerror(errno) << "\n";
        return false;
    }
    if (::ioctl(i2c_fd_, I2C_SLAVE, kFusionHatI2cAddr) < 0) {
        std::cerr << "FusionHat: ioctl I2C_SLAVE failed\n";
        ::close(i2c_fd_);
        i2c_fd_ = -1;
        return false;
    }
    sim_mode_.store(false, std::memory_order_release);
    access_level_.store(I2cAccessLevel::kNone, std::memory_order_release);
    return true;
}

bool FusionHat::init_sim() {
    sim_mode_.store(true, std::memory_order_release);
    access_level_.store(I2cAccessLevel::kNone, std::memory_order_release);
    return true;
}

// PERF-006: Start async I2C worker thread
void FusionHat::start_async_mode() {
    if (async_mode_.load(std::memory_order_acquire)) {
        return;  // Already running
    }
    async_mode_.store(true, std::memory_order_release);
    worker_running_.store(true, std::memory_order_release);
    i2c_worker_ = std::thread(&FusionHat::i2c_worker_thread, this);
}

// PERF-006: Stop async I2C worker thread
void FusionHat::stop_async_mode() {
    if (!async_mode_.load(std::memory_order_acquire)) {
        return;
    }
    worker_running_.store(false, std::memory_order_release);
    queue_cv_.notify_one();
    if (i2c_worker_.joinable()) {
        i2c_worker_.join();
    }
    async_mode_.store(false, std::memory_order_release);
}

// PERF-006: Async I2C worker thread - processes commands without blocking RT path
void FusionHat::i2c_worker_thread() {
    while (worker_running_.load(std::memory_order_acquire)) {
        I2cCommand cmd;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !command_queue_.empty() || !worker_running_.load(std::memory_order_acquire);
            });
            if (!worker_running_.load(std::memory_order_acquire) && command_queue_.empty()) {
                break;
            }
            if (command_queue_.empty()) {
                continue;
            }
            cmd = command_queue_.front();
            command_queue_.pop();
        }

        // Process effector command with proper timing
        if (cmd.is_effector) {
            // Write PWM active
            write_pwm_channel(kEffectorChannel, 2000);
            
            // Use clock_nanosleep for precise timing (PERF-007)
            struct timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ts.tv_sec += cmd.effector_pulse_ms / 1000;
            ts.tv_nsec += (cmd.effector_pulse_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
            
            // Write PWM safe
            write_pwm_channel(kEffectorChannel, 1000);
        } else {
            // Regular PWM channel write
            write_pwm_channel(cmd.channel, cmd.pulse_us);
        }
    }
}

// SEC-007: Authentication implementation
bool FusionHat::authenticate(I2cAccessLevel level) {
    access_level_.store(level, std::memory_order_release);
    command_sequence_.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

void FusionHat::deauthenticate() {
    access_level_.store(I2cAccessLevel::kNone, std::memory_order_release);
    command_sequence_.fetch_add(1, std::memory_order_acq_rel);
}

// SEC-007: Static validation helpers
bool FusionHat::validate_channel(uint8_t channel) {
    return channel <= kEffectorChannel;
}

bool FusionHat::validate_angle(float angle_deg) {
    return (angle_deg >= -kAngleMaxDeg && angle_deg <= kAngleMaxDeg);
}

bool FusionHat::validate_pwm_value(uint16_t pulse_us) {
    return (pulse_us >= kPwmMinUs && pulse_us <= kPwmMaxUs);
}

uint16_t FusionHat::angle_to_pwm_us(float angle_deg) {
    float clamped = std::clamp(angle_deg, -kAngleMaxDeg, kAngleMaxDeg);
    float pw = kPwmCenterUs + (clamped / kAngleMaxDeg) * kPwmRangeUs;
    return static_cast<uint16_t>(std::clamp(pw, kPwmMinUs, kPwmMaxUs));
}

// SEC-007: Angle command validation with elevation-specific bounds
I2cCommandStatus FusionHat::validate_angle_command(float angle_deg, bool is_elevation) {
    // Check NaN/Inf
    if (!std::isfinite(angle_deg)) {
        return I2cCommandStatus::kInvalidValue;
    }

    // Elevation has tighter bounds than azimuth
    if (is_elevation) {
        if (angle_deg < kElevationMinDeg || angle_deg > kElevationMaxDeg) {
            return I2cCommandStatus::kInvalidRange;
        }
    } else {
        if (std::abs(angle_deg) > kAzimuthMaxDeg) {
            return I2cCommandStatus::kInvalidRange;
        }
    }

    return I2cCommandStatus::kOk;
}

// SEC-007: Access-controlled azimuth command
I2cCommandStatus FusionHat::set_azimuth(float az_deg) {
    // Check access level
    if (access_level_.load(std::memory_order_acquire) < I2cAccessLevel::kGimbal) {
        auth_failures_.fetch_add(1, std::memory_order_relaxed);
        return I2cCommandStatus::kUnauthorized;
    }

    // Validate angle
    I2cCommandStatus status = validate_angle_command(az_deg, false);
    if (status != I2cCommandStatus::kOk) {
        return status;
    }

    // Convert to PWM and write
    uint16_t pwm_us = angle_to_pwm_us(az_deg);
    return write_pwm_channel(kAzChannel, pwm_us);
}

// SEC-007: Access-controlled elevation command
I2cCommandStatus FusionHat::set_elevation(float el_deg) {
    // Check access level
    if (access_level_.load(std::memory_order_acquire) < I2cAccessLevel::kGimbal) {
        auth_failures_.fetch_add(1, std::memory_order_relaxed);
        return I2cCommandStatus::kUnauthorized;
    }

    // Validate angle (elevation has tighter bounds)
    I2cCommandStatus status = validate_angle_command(el_deg, true);
    if (status != I2cCommandStatus::kOk) {
        return status;
    }

    // Convert to PWM and write
    uint16_t pwm_us = angle_to_pwm_us(el_deg);
    return write_pwm_channel(kElChannel, pwm_us);
}

// SEC-007: Access-controlled gimbal command (atomic azimuth + elevation)
I2cCommandStatus FusionHat::set_gimbal(float az_deg, float el_deg) {
    // Check access level
    if (access_level_.load(std::memory_order_acquire) < I2cAccessLevel::kGimbal) {
        auth_failures_.fetch_add(1, std::memory_order_relaxed);
        return I2cCommandStatus::kUnauthorized;
    }

    // Validate both angles
    I2cCommandStatus az_status = validate_angle_command(az_deg, false);
    if (az_status != I2cCommandStatus::kOk) {
        return az_status;
    }

    I2cCommandStatus el_status = validate_angle_command(el_deg, true);
    if (el_status != I2cCommandStatus::kOk) {
        return el_status;
    }

    // Write both channels (note: not atomic at I2C level)
    uint16_t az_pwm = angle_to_pwm_us(az_deg);
    uint16_t el_pwm = angle_to_pwm_us(el_deg);

    I2cCommandStatus write_status = write_pwm_channel(kAzChannel, az_pwm);
    if (write_status != I2cCommandStatus::kOk) {
        return write_status;
    }

    return write_pwm_channel(kElChannel, el_pwm);
}

// PERF-006: Non-blocking effector trigger - queues command to async thread
I2cCommandStatus FusionHat::trigger_effector(int pulse_ms) {
    // Check full access required
    if (access_level_.load(std::memory_order_acquire) < I2cAccessLevel::kFull) {
        auth_failures_.fetch_add(1, std::memory_order_relaxed);
        return I2cCommandStatus::kUnauthorized;
    }

    // Validate pulse duration (1ms to 1000ms)
    if (pulse_ms < 1 || pulse_ms > 1000) {
        return I2cCommandStatus::kInvalidValue;
    }

    // Safety interlock: must be armed
    if (!armed_.load(std::memory_order_acquire)) {
        return I2cCommandStatus::kSafetyInterlock;
    }

    // Safety interlock: range must be within max
    float current_range = current_range_m_.load(std::memory_order_acquire);
    float max_range = max_range_m_.load(std::memory_order_acquire);
    if (current_range > max_range) {
        return I2cCommandStatus::kSafetyInterlock;
    }

    // PERF-006: Queue command to async thread instead of blocking
    if (async_mode_.load(std::memory_order_acquire)) {
        I2cCommand cmd;
        cmd.is_effector = true;
        cmd.effector_pulse_ms = pulse_ms;
        cmd.channel = kEffectorChannel;
        cmd.pulse_us = 2000;
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            command_queue_.push(cmd);
        }
        queue_cv_.notify_one();
        command_sequence_.fetch_add(1, std::memory_order_acq_rel);
        return I2cCommandStatus::kOk;
    }

    // Fallback: synchronous mode (legacy behavior)
    if (sim_mode_.load(std::memory_order_acquire)) {
        command_sequence_.fetch_add(1, std::memory_order_acq_rel);
        return I2cCommandStatus::kOk;
    }

    if (i2c_fd_ < 0) {
        return I2cCommandStatus::kHardwareError;
    }

    // Write effector PWM (2000us = active, 1000us = safe)
    I2cCommandStatus status = write_pwm_channel(kEffectorChannel, 2000);
    if (status != I2cCommandStatus::kOk) {
        return status;
    }

    // PERF-007: Use clock_nanosleep instead of sleep_for for precise timing
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += pulse_ms / 1000;
    ts.tv_nsec += (pulse_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);

    // Return to safe state
    status = write_pwm_channel(kEffectorChannel, 1000);
    if (status != I2cCommandStatus::kOk) {
        return status;
    }

    command_sequence_.fetch_add(1, std::memory_order_acq_rel);
    return I2cCommandStatus::kOk;
}

// SEC-007: Validated PWM channel write
I2cCommandStatus FusionHat::write_pwm_channel(uint8_t channel, uint16_t pulse_us) {
    // Validate channel
    if (!validate_channel(channel)) {
        return I2cCommandStatus::kInvalidChannel;
    }

    // Validate PWM value
    if (!validate_pwm_value(pulse_us)) {
        return I2cCommandStatus::kInvalidValue;
    }

    // Simulation mode bypass
    if (sim_mode_.load(std::memory_order_acquire)) {
        command_sequence_.fetch_add(1, std::memory_order_acq_rel);
        return I2cCommandStatus::kOk;
    }

    // Hardware check
    if (i2c_fd_ < 0) {
        return I2cCommandStatus::kHardwareError;
    }

    // Write to I2C
    uint8_t reg = kRegPwmBase + channel * 2;
    uint8_t buf[3] = {
        reg,
        static_cast<uint8_t>(pulse_us & 0xFF),
        static_cast<uint8_t>((pulse_us >> 8) & 0xFF)
    };

    ssize_t written = ::write(i2c_fd_, buf, sizeof(buf));
    if (written != static_cast<ssize_t>(sizeof(buf))) {
        return I2cCommandStatus::kHardwareError;
    }

    command_sequence_.fetch_add(1, std::memory_order_acq_rel);
    return I2cCommandStatus::kOk;
}

void FusionHat::set_max_range_m(float max_m) {
    max_range_m_.store(max_m, std::memory_order_release);
}

void FusionHat::set_current_range_m(float range_m) {
    current_range_m_.store(range_m, std::memory_order_release);
}

void FusionHat::arm() {
    armed_.store(true, std::memory_order_release);
}

void FusionHat::disarm() {
    armed_.store(false, std::memory_order_release);
}

}  // namespace aurore
