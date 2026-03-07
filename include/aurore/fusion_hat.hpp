/**
 * @file fusion_hat.hpp
 * @brief Sunfounder Fusion HAT+ PWM/servo driver
 *
 * C++17 wrapper for Fusion HAT+ sysfs interface.
 * The Fusion HAT+ uses a kernel driver that exposes 12 PWM channels via sysfs,
 * NOT direct I2C communication. This wrapper provides safe, real-time capable
 * servo and PWM control.
 *
 * Hardware interface:
 * - I2C Address: 0x17 (handled by kernel driver)
 * - PWM Channels: 12 (0-11)
 * - Frequency: 50Hz for servos (500-2500μs pulse width)
 * - Sysfs path: /sys/class/fusion_hat/fusion_hat/pwm*
 *
 * Usage:
 * @code
 *     FusionHat hat;
 *     if (!hat.init()) {
 *         std::cerr << "Fusion HAT+ not found" << std::endl;
 *         return -1;
 *     }
 *     
 *     // Set servo on channel 0 to center position
 *     hat.set_servo_angle(0, 0.0f);  // 0 degrees = center
 *     
 *     // Set servo to full range
 *     hat.set_servo_angle(0, -90.0f);  // Full left
 *     hat.set_servo_angle(0, 90.0f);   // Full right
 *     
 *     // Direct PWM control
 *     hat.set_pwm_duty_cycle(1, 50);  // 50% duty cycle
 * @endcode
 *
 * @copyright AuroreMkVII Project - Educational/Personal Use Only
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <optional>

namespace aurore {

/**
 * @brief Fusion HAT+ configuration
 */
struct FusionHatConfig {
    /// Servo frequency in Hz (default 50Hz for standard servos)
    int servo_freq_hz = 50;
    
    /// Minimum pulse width in microseconds (default 500μs = -90°)
    int min_pulse_width_us = 500;
    
    /// Maximum pulse width in microseconds (default 2500μs = +90°)
    int max_pulse_width_us = 2500;
    
    /// Minimum servo angle in degrees (default -90°)
    float min_angle_deg = -90.0f;
    
    /// Maximum servo angle in degrees (default +90°)
    float max_angle_deg = 90.0f;
    
    /// Enable software endstops
    bool enable_endstops = true;
    
    /// Enable rate limiting (degrees per second)
    bool enable_rate_limit = false;
    
    /// Maximum angular velocity in degrees/second
    float max_angular_velocity_dps = 100.0f;

    /**
     * @brief Validate configuration
     */
    bool validate() const noexcept {
        if (servo_freq_hz <= 0 || servo_freq_hz > 1000) return false;
        if (min_pulse_width_us <= 0 || min_pulse_width_us > 10000) return false;
        if (max_pulse_width_us <= min_pulse_width_us) return false;
        if (min_angle_deg >= max_angle_deg) return false;
        return true;
    }
};

/**
 * @brief Servo channel status
 */
struct ServoStatus {
    /// Channel number (0-11)
    int channel;
    
    /// Current angle in degrees
    float angle_deg;
    
    /// Current pulse width in microseconds
    int pulse_width_us;
    
    /// True if servo is enabled
    bool enabled;
    
    /// True if endstop is active
    bool endstop_active;
    
    /// Timestamp of last update (nanoseconds since boot)
    uint64_t last_update_ns;
};

/**
 * @brief Fusion HAT+ PWM/servo driver
 *
 * Thread-safe wrapper for Fusion HAT+ sysfs interface.
 * Uses file-based sysfs communication (not I2C directly).
 */
class FusionHat {
public:
    /**
     * @brief Construct Fusion HAT+ driver
     *
     * @param config Configuration options
     */
    explicit FusionHat(const FusionHatConfig& config = FusionHatConfig());
    
    /**
     * @brief Destructor
     */
    ~FusionHat();
    
    // Non-copyable
    FusionHat(const FusionHat&) = delete;
    FusionHat& operator=(const FusionHat&) = delete;
    
    /**
     * @brief Initialize Fusion HAT+
     *
     * Checks for device presence and initializes PWM channels.
     *
     * @return true if Fusion HAT+ is connected and initialized
     */
    bool init();
    
    /**
     * @brief Check if Fusion HAT+ is connected
     *
     * @return true if device tree entry and sysfs path exist
     */
    bool is_connected() const noexcept;
    
    /**
     * @brief Check if driver is initialized
     */
    bool is_initialized() const noexcept {
        return initialized_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get firmware version
     *
     * @return Firmware version string, or empty if not available
     */
    std::string get_firmware_version() const;
    
    /**
     * @brief Get driver version
     *
     * @return Driver version string, or empty if not available
     */
    std::string get_driver_version() const;
    
    // =========================================================================
    // Servo Control API
    // =========================================================================
    
    /**
     * @brief Set servo angle
     *
     * Maps angle to pulse width and writes to PWM channel.
     *
     * @param channel PWM channel (0-11)
     * @param angle_deg Angle in degrees (-90 to +90 by default)
     * @return true on success, false on error
     */
    bool set_servo_angle(int channel, float angle_deg);
    
    /**
     * @brief Get current servo angle
     *
     * @param channel PWM channel
     * @return Current angle, or std::nullopt if error
     */
    std::optional<float> get_servo_angle(int channel) const;
    
    /**
     * @brief Set servo pulse width directly
     *
     * @param channel PWM channel (0-11)
     * @param pulse_width_us Pulse width in microseconds (500-2500)
     * @return true on success, false on error
     */
    bool set_servo_pulse_width(int channel, int pulse_width_us);
    
    /**
     * @brief Get current pulse width
     *
     * @param channel PWM channel
     * @return Pulse width in microseconds, or -1 on error
     */
    int get_pulse_width(int channel) const;
    
    /**
     * @brief Enable/disable servo channel
     *
     * @param channel PWM channel (0-11)
     * @param enable true to enable, false to disable
     * @return true on success
     */
    bool set_servo_enabled(int channel, bool enable);
    
    /**
     * @brief Check if servo is enabled
     */
    bool is_servo_enabled(int channel) const;
    
    /**
     * @brief Get servo status
     */
    ServoStatus get_servo_status(int channel) const;
    
    /**
     * @brief Disable all servos (safety function)
     */
    void disable_all_servos();
    
    // =========================================================================
    // PWM Control API
    // =========================================================================
    
    /**
     * @brief Set PWM frequency
     *
     * @param channel PWM channel (0-11)
     * @param freq_hz Frequency in Hz (1-1000)
     * @return true on success
     */
    bool set_pwm_freq(int channel, int freq_hz);
    
    /**
     * @brief Get PWM frequency
     */
    int get_pwm_freq(int channel) const;
    
    /**
     * @brief Set PWM duty cycle percentage
     *
     * @param channel PWM channel (0-11)
     * @param duty_percent Duty cycle (0-100)
     * @return true on success
     */
    bool set_pwm_duty_cycle(int channel, int duty_percent);
    
    /**
     * @brief Get PWM duty cycle
     */
    int get_pwm_duty_cycle(int channel) const;
    
    /**
     * @brief Set PWM period in microseconds
     *
     * @param channel PWM channel
     * @param period_us Period in microseconds
     * @return true on success
     */
    bool set_pwm_period(int channel, int period_us);
    
    /**
     * @brief Get PWM period
     */
    int get_pwm_period(int channel) const;
    
    // =========================================================================
    // Safety & Monitoring
    // =========================================================================
    
    /**
     * @brief Set software endstop limits
     *
     * @param channel PWM channel
     * @param min_angle_deg Minimum angle
     * @param max_angle_deg Maximum angle
     */
    void set_endstop_limits(int channel, float min_angle_deg, float max_angle_deg);
    
    /**
     * @brief Enable/disable rate limiting
     *
     * @param enable true to enable rate limiting
     * @param max_velocity_dps Maximum angular velocity in degrees/second
     */
    void set_rate_limit(bool enable, float max_velocity_dps = 100.0f);
    
    /**
     * @brief Get error count
     */
    uint64_t get_error_count() const noexcept {
        return error_count_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get command count
     */
    uint64_t get_command_count() const noexcept {
        return command_count_.load(std::memory_order_acquire);
    }

private:
    /**
     * @brief Write integer to sysfs file
     *
     * @param path Sysfs file path
     * @param value Value to write
     * @return true on success
     */
    static bool write_sysfs(const std::string& path, int value);
    
    /**
     * @brief Read integer from sysfs file
     *
     * @param path Sysfs file path
     * @return Value read, or -1 on error
     */
    static int read_sysfs(const std::string& path);
    
    /**
     * @brief Read string from sysfs file
     *
     * @param path Sysfs file path
     * @return String value, or empty on error
     */
    static std::string read_sysfs_string(const std::string& path);
    
    /**
     * @brief Map angle to pulse width
     *
     * @param angle_deg Angle in degrees
     * @return Pulse width in microseconds
     */
    int angle_to_pulse_width(float angle_deg) const noexcept;
    
    /**
     * @brief Map pulse width to angle
     *
     * @param pulse_width_us Pulse width in microseconds
     * @return Angle in degrees
     */
    float pulse_width_to_angle(int pulse_width_us) const noexcept;
    
    /**
     * @brief Get PWM channel sysfs path
     */
    std::string get_pwm_path(int channel) const;
    
    FusionHatConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<uint64_t> error_count_{0};
    std::atomic<uint64_t> command_count_{0};
    
    // Per-channel state
    struct ChannelState {
        std::atomic<bool> enabled{false};
        std::atomic<float> current_angle{0.0f};
        std::atomic<int> current_pulse_width{1500};
        std::atomic<uint64_t> last_update_ns{0};
        float min_angle{-90.0f};
        float max_angle{90.0f};
    };
    
    std::array<ChannelState, 12> channels_;
    
    // Sysfs base path
    static constexpr const char* SYSFS_BASE = "/sys/class/fusion_hat/fusion_hat";
    static constexpr const char* DEVICE_TREE_PATH = "/proc/device-tree";
    static constexpr uint16_t PRODUCT_ID = 0x0774;
};

}  // namespace aurore
