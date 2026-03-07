#ifndef THERMAL_SHUTDOWN_H
#define THERMAL_SHUTDOWN_H

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <iostream>

namespace aurore {
namespace thermal {

constexpr float DEFAULT_CPU_CRITICAL_TEMP = 90.0f;
constexpr float DEFAULT_CPU_WARNING_TEMP = 80.0f;
constexpr float DEFAULT_TPU_CRITICAL_TEMP = 85.0f;
constexpr float DEFAULT_TPU_WARNING_TEMP = 75.0f;
constexpr int DEFAULT_HOLD_TIME_SECONDS = 30;

enum class ThermalState {
    NORMAL,
    WARNING,
    CRITICAL,
    EMERGENCY
};

struct ThermalConfig {
    float cpu_warning_temp;
    float cpu_critical_temp;
    float tpu_warning_temp;
    float tpu_critical_temp;
    int hold_time_seconds;
    bool gpio_shutdown_enabled;
    int gpio_pin;
};

struct ThermalStatus {
    ThermalState state;
    float cpu_temperature;
    float tpu_temperature;
    std::chrono::steady_clock::time_point critical_time;
    int seconds_in_critical;
    bool shutdown_triggered;
};

class ThermalShutdownController {
public:
    static ThermalShutdownController& instance() {
        static ThermalShutdownController controller;
        return controller;
    }

    void configure(const ThermalConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
    }

    void configure_defaults() {
        ThermalConfig default_config;
        default_config.cpu_warning_temp = DEFAULT_CPU_WARNING_TEMP;
        default_config.cpu_critical_temp = DEFAULT_CPU_CRITICAL_TEMP;
        default_config.tpu_warning_temp = DEFAULT_TPU_WARNING_TEMP;
        default_config.tpu_critical_temp = DEFAULT_TPU_CRITICAL_TEMP;
        default_config.hold_time_seconds = DEFAULT_HOLD_TIME_SECONDS;
        default_config.gpio_shutdown_enabled = false;
        default_config.gpio_pin = -1;
        configure(default_config);
    }

    void update_temperatures(float cpu_temp, float tpu_temp) {
        std::lock_guard<std::mutex> lock(mutex_);

        cpu_temperature_.store(cpu_temp);
        tpu_temperature_.store(tpu_temp);

        ThermalState new_state = calculate_state(cpu_temp, tpu_temp);

        if (new_state == ThermalState::EMERGENCY && state_ != ThermalState::EMERGENCY) {
            critical_start_time_ = std::chrono::steady_clock::now();
        }

        state_ = new_state;

        if (new_state == ThermalState::EMERGENCY) {
            auto elapsed = std::chrono::steady_clock::now() - critical_start_time_;
            seconds_in_critical_ = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

            if (seconds_in_critical_ >= config_.hold_time_seconds) {
                trigger_shutdown();
            }
        } else {
            seconds_in_critical_ = 0;
            critical_start_time_ = std::chrono::steady_clock::time_point{};
        }
    }

    ThermalStatus get_status() const {
        ThermalStatus status;
        status.state = state_.load();
        status.cpu_temperature = cpu_temperature_.load();
        status.tpu_temperature = tpu_temperature_.load();
        status.critical_time = critical_start_time_;
        status.seconds_in_critical = seconds_in_critical_;
        status.shutdown_triggered = shutdown_triggered_.load();
        return status;
    }

    ThermalState get_state() const { return state_; }

    float get_cpu_temperature() const { return cpu_temperature_.load(); }
    float get_tpu_temperature() const { return tpu_temperature_.load(); }

    bool is_critical() const {
        return state_ == ThermalState::EMERGENCY ||
               (state_ == ThermalState::CRITICAL && seconds_in_critical_ > 0);
    }

    bool should_reduce_power() const {
        return state_ == ThermalState::CRITICAL || state_ == ThermalState::EMERGENCY;
    }

    void set_shutdown_callback(std::function<void()> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_callback_ = callback;
    }

    void set_gpio_shutdown_callback(std::function<void(int pin)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        gpio_shutdown_callback_ = callback;
    }

    void trigger_shutdown() {
        if (shutdown_triggered_.load()) return;

        shutdown_triggered_.store(true);

        if (shutdown_callback_) {
            shutdown_callback_();
        }

        if (config_.gpio_shutdown_enabled && gpio_shutdown_callback_) {
            gpio_shutdown_callback_(config_.gpio_pin);
        }

        std::cerr << "[THERMAL] Emergency shutdown triggered! CPU: " << cpu_temperature_.load()
                  << "°C, TPU: " << tpu_temperature_.load() << "°C" << std::endl;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ThermalState::NORMAL;
        cpu_temperature_.store(0.0f);
        tpu_temperature_.store(0.0f);
        critical_start_time_ = {};
        seconds_in_critical_ = 0;
        shutdown_triggered_.store(false);
    }

    std::string get_state_string() const {
        switch (state_) {
            case ThermalState::NORMAL: return "NORMAL";
            case ThermalState::WARNING: return "WARNING";
            case ThermalState::CRITICAL: return "CRITICAL";
            case ThermalState::EMERGENCY: return "EMERGENCY";
            default: return "UNKNOWN";
        }
    }

private:
    ThermalShutdownController() {
        configure_defaults();
        state_ = ThermalState::NORMAL;
        seconds_in_critical_ = 0;
        shutdown_triggered_.store(false);
    }

    ThermalState calculate_state(float cpu_temp, float tpu_temp) {
        if (cpu_temp >= config_.cpu_critical_temp || tpu_temp >= config_.tpu_critical_temp) {
            return ThermalState::EMERGENCY;
        }
        if (cpu_temp >= config_.cpu_warning_temp || tpu_temp >= config_.tpu_warning_temp) {
            return ThermalState::CRITICAL;
        }
        if (cpu_temp >= config_.cpu_warning_temp - 10.0f ||
            tpu_temp >= config_.tpu_warning_temp - 10.0f) {
            return ThermalState::WARNING;
        }
        return ThermalState::NORMAL;
    }

    ThermalConfig config_;
    std::atomic<float> cpu_temperature_{0.0f};
    std::atomic<float> tpu_temperature_{0.0f};
    std::atomic<ThermalState> state_{ThermalState::NORMAL};
    std::chrono::steady_clock::time_point critical_start_time_;
    int seconds_in_critical_;
    std::atomic<bool> shutdown_triggered_{false};
    std::function<void()> shutdown_callback_;
    std::function<void(int pin)> gpio_shutdown_callback_;
    std::mutex mutex_;
};

inline ThermalShutdownController& get_thermal_controller() {
    return ThermalShutdownController::instance();
}

inline void update_thermal_status(float cpu_temp, float tpu_temp) {
    ThermalShutdownController::instance().update_temperatures(cpu_temp, tpu_temp);
}

inline bool is_thermal_critical() {
    return ThermalShutdownController::instance().is_critical();
}

}  // namespace thermal
}  // namespace aurore

#endif  // THERMAL_SHUTDOWN_H
