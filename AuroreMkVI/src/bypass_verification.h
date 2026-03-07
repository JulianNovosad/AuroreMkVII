#ifndef AURORE_BYPASS_VERIFICATION_H
#define AURORE_BYPASS_VERIFICATION_H

#include <atomic>
#include <chrono>
#include <string>
#include <functional>
#include <mutex>
#include <thread>
#include <queue>

// REMEDIATION 2026-02-02: Findings #7, #8 - Hardware bypass verification
// This header implements GPIO-based hardware switch verification for fire and target lock bypasses

namespace Aurore {

enum class BypassType {
    FIRE_BYPASS,
    TARGET_LOCK_BYPASS
};

enum class BypassState {
    INACTIVE,
    REQUESTED,
    GPIO_VERIFYING,
    VERIFIED,
    REJECTED,
    ACTIVE,
    EXPIRED
};

struct BypassEvent {
    std::chrono::steady_clock::time_point timestamp;
    BypassType type;
    BypassState from_state;
    BypassState to_state;
    std::string details;
};

class BypassVerification {
public:
    // Hardware configuration for bypass switches
    // GPIO pins must be physically connected to hardware switches
    static constexpr int FIRE_BYPASS_GPIO = 17;      // Physical switch for fire bypass
    static constexpr int TARGET_LOCK_BYPASS_GPIO = 18; // Physical switch for target lock bypass
    static constexpr auto VERIFICATION_TIMEOUT = std::chrono::seconds(3);
    static constexpr auto BYPASS_ACTIVE_DURATION = std::chrono::seconds(60);

private:
    std::atomic<BypassState> fire_bypass_state_{BypassState::INACTIVE};
    std::atomic<BypassState> target_lock_bypass_state_{BypassState::INACTIVE};
    
    std::atomic<std::chrono::steady_clock::time_point> fire_bypass_expiry_;
    std::atomic<std::chrono::steady_clock::time_point> target_lock_bypass_expiry_;
    
    std::mutex event_mutex_;
    std::queue<BypassEvent> event_queue_;
    std::thread monitor_thread_;
    std::atomic<bool> running_{true};
    
    std::function<void(const BypassEvent&)> audit_callback_;
    std::function<bool(int)> gpio_reader_;  // Function to read GPIO state

public:
    BypassVerification() : running_(true) {
        fire_bypass_expiry_ = std::chrono::steady_clock::now();
        target_lock_bypass_expiry_ = std::chrono::steady_clock::now();
        monitor_thread_ = std::thread(&BypassVerification::monitor_loop, this);
    }

    ~BypassVerification() {
        running_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }

    BypassVerification(const BypassVerification&) = delete;
    BypassVerification& operator=(const BypassVerification&) = delete;

    // Request bypass with hardware verification
    bool request_bypass(BypassType type) {
        if (type == BypassType::FIRE_BYPASS) {
            BypassState expected = BypassState::INACTIVE;
            if (fire_bypass_state_.compare_exchange_strong(expected, BypassState::REQUESTED)) {
                record_event(type, BypassState::INACTIVE, BypassState::REQUESTED, 
                           "Fire bypass requested, awaiting hardware verification");
                
                // Attempt hardware verification
                if (verify_hardware_switch(FIRE_BYPASS_GPIO)) {
                    fire_bypass_state_.store(BypassState::VERIFIED);
                    record_event(type, BypassState::REQUESTED, BypassState::VERIFIED,
                               "Hardware switch verified for fire bypass");
                    activate_bypass(type);
                    return true;
                } else {
                    fire_bypass_state_.store(BypassState::REJECTED);
                    record_event(type, BypassState::REQUESTED, BypassState::REJECTED,
                               "Hardware verification FAILED - fire bypass rejected");
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    fire_bypass_state_.store(BypassState::INACTIVE);
                    return false;
                }
            }
        } else if (type == BypassType::TARGET_LOCK_BYPASS) {
            BypassState expected = BypassState::INACTIVE;
            if (target_lock_bypass_state_.compare_exchange_strong(expected, BypassState::REQUESTED)) {
                record_event(type, BypassState::INACTIVE, BypassState::REQUESTED,
                           "Target lock bypass requested, awaiting hardware verification");
                
                if (verify_hardware_switch(TARGET_LOCK_BYPASS_GPIO)) {
                    target_lock_bypass_state_.store(BypassState::VERIFIED);
                    record_event(type, BypassState::REQUESTED, BypassState::VERIFIED,
                               "Hardware switch verified for target lock bypass");
                    activate_bypass(type);
                    return true;
                } else {
                    target_lock_bypass_state_.store(BypassState::REJECTED);
                    record_event(type, BypassState::REQUESTED, BypassState::REJECTED,
                               "Hardware verification FAILED - target lock bypass rejected");
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    target_lock_bypass_state_.store(BypassState::INACTIVE);
                    return false;
                }
            }
        }
        return false;
    }

    // Check if bypass is currently active and valid
    bool is_bypass_active(BypassType type) const {
        auto now = std::chrono::steady_clock::now();
        if (type == BypassType::FIRE_BYPASS) {
            return fire_bypass_state_.load() == BypassState::ACTIVE && 
                   now < fire_bypass_expiry_.load();
        } else {
            return target_lock_bypass_state_.load() == BypassState::ACTIVE &&
                   now < target_lock_bypass_expiry_.load();
        }
    }

    // Cancel an active bypass
    void cancel_bypass(BypassType type) {
        if (type == BypassType::FIRE_BYPASS) {
            if (fire_bypass_state_.load() == BypassState::ACTIVE) {
                record_event(type, BypassState::ACTIVE, BypassState::INACTIVE, "Fire bypass cancelled");
                fire_bypass_state_.store(BypassState::INACTIVE);
            }
        } else {
            if (target_lock_bypass_state_.load() == BypassState::ACTIVE) {
                record_event(type, BypassState::ACTIVE, BypassState::INACTIVE, "Target lock bypass cancelled");
                target_lock_bypass_state_.store(BypassState::INACTIVE);
            }
        }
    }

    BypassState get_bypass_state(BypassType type) const {
        if (type == BypassType::FIRE_BYPASS) {
            return fire_bypass_state_.load();
        } else {
            return target_lock_bypass_state_.load();
        }
    }

    void set_audit_callback(std::function<void(const BypassEvent&)> callback) {
        audit_callback_ = callback;
    }

    void set_gpio_reader(std::function<bool(int)> reader) {
        gpio_reader_ = reader;
    }

    bool get_audit_event(BypassEvent& event) {
        std::lock_guard<std::mutex> lock(event_mutex_);
        if (!event_queue_.empty()) {
            event = event_queue_.front();
            event_queue_.pop();
            return true;
        }
        return false;
    }

    void reset() {
        cancel_bypass(BypassType::FIRE_BYPASS);
        cancel_bypass(BypassType::TARGET_LOCK_BYPASS);
        std::lock_guard<std::mutex> lock(event_mutex_);
        while (!event_queue_.empty()) {
            event_queue_.pop();
        }
    }

private:
    bool verify_hardware_switch(int gpio_pin) {
        // If custom GPIO reader is set, use it
        if (gpio_reader_) {
            return gpio_reader_(gpio_pin);
        }
        
        // Default: Try to read from sysfs GPIO interface
        std::string gpio_path = "/sys/class/gpio/gpio" + std::to_string(gpio_pin) + "/value";
        std::ifstream gpio_file(gpio_path);
        if (!gpio_file.is_open()) {
            // GPIO not configured - reject bypass for safety
            return false;
        }
        int value;
        gpio_file >> value;
        gpio_file.close();
        
        // Active low: value == 0 means switch is engaged
        return (value == 0);
    }

    void activate_bypass(BypassType type) {
        auto now = std::chrono::steady_clock::now();
        auto expiry = now + BYPASS_ACTIVE_DURATION;
        
        if (type == BypassType::FIRE_BYPASS) {
            fire_bypass_expiry_ = expiry;
            fire_bypass_state_.store(BypassState::ACTIVE);
            record_event(type, BypassState::VERIFIED, BypassState::ACTIVE,
                       "Fire bypass activated - expires in 60 seconds");
        } else {
            target_lock_bypass_expiry_ = expiry;
            target_lock_bypass_state_.store(BypassState::ACTIVE);
            record_event(type, BypassState::VERIFIED, BypassState::ACTIVE,
                       "Target lock bypass activated - expires in 60 seconds");
        }
    }

    void monitor_loop() {
        while (running_) {
            auto now = std::chrono::steady_clock::now();
            
            // Check fire bypass expiry
            if (fire_bypass_state_.load() == BypassState::ACTIVE) {
                if (now > fire_bypass_expiry_.load()) {
                    record_event(BypassType::FIRE_BYPASS, BypassState::ACTIVE, BypassState::EXPIRED,
                               "Fire bypass expired after 60 seconds");
                    fire_bypass_state_.store(BypassState::INACTIVE);
                }
            }
            
            // Check target lock bypass expiry
            if (target_lock_bypass_state_.load() == BypassState::ACTIVE) {
                if (now > target_lock_bypass_expiry_.load()) {
                    record_event(BypassType::TARGET_LOCK_BYPASS, BypassState::ACTIVE, BypassState::EXPIRED,
                               "Target lock bypass expired after 60 seconds");
                    target_lock_bypass_state_.store(BypassState::INACTIVE);
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void record_event(BypassType type, BypassState from, BypassState to, const std::string& details) {
        BypassEvent event;
        event.timestamp = std::chrono::steady_clock::now();
        event.type = type;
        event.from_state = from;
        event.to_state = to;
        event.details = details;
        
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            event_queue_.push(event);
            while (event_queue_.size() > 100) {
                event_queue_.pop();
            }
        }
        
        if (audit_callback_) {
            audit_callback_(event);
        }
    }
};

} // namespace Aurore

#endif // AURORE_BYPASS_VERIFICATION_H
