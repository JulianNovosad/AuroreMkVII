#ifndef AURORE_FIRE_OVERRIDE_SAFETY_H
#define AURORE_FIRE_OVERRIDE_SAFETY_H

#include <atomic>
#include <chrono>
#include <string>
#include <functional>
#include <mutex>
#include <thread>
#include <queue>
#include <cstdint>

namespace Aurore {

enum class FireOverrideState {
    IDLE,
    CONFIRM_WAITING,
    CONFIRMED,
    ACTIVE,
    TIMEOUT,
    ERROR
};

struct FireOverrideEvent {
    std::chrono::steady_clock::time_point timestamp;
    FireOverrideState from_state;
    FireOverrideState to_state;
    std::string details;
};

class FireOverrideSafety {
public:
    static constexpr int CONFIRM_STEPS = 2;
    static constexpr std::chrono::seconds CONFIRM_TIMEOUT = std::chrono::seconds(5);
    static constexpr std::chrono::seconds ACTIVE_TIMEOUT = std::chrono::seconds(10);

private:
    std::atomic<FireOverrideState> state_;
    std::atomic<int> confirm_step_;
    std::atomic<std::chrono::steady_clock::time_point> state_timestamp_;
    std::atomic<std::chrono::steady_clock::time_point> active_until_;
    
    std::mutex event_mutex_;
    std::queue<FireOverrideEvent> event_queue_;
    std::thread timeout_thread_;
    std::atomic<bool> running_;
    
    std::function<void(const FireOverrideEvent&)> audit_callback_;
    std::function<void(FireOverrideState, FireOverrideState)> state_callback_;

public:
    FireOverrideSafety() 
        : state_(FireOverrideState::IDLE),
          confirm_step_(0),
          state_timestamp_(std::chrono::steady_clock::now()),
          active_until_(std::chrono::steady_clock::time_point::min()),
          running_(true) {
        timeout_thread_ = std::thread(&FireOverrideSafety::timeout_loop, this);
    }

    ~FireOverrideSafety() {
        running_ = false;
        if (timeout_thread_.joinable()) {
            timeout_thread_.join();
        }
    }

    FireOverrideSafety(const FireOverrideSafety&) = delete;
    FireOverrideSafety& operator=(const FireOverrideSafety&) = delete;

    bool initiate_fire_sequence() {
        FireOverrideState expected = FireOverrideState::IDLE;
        if (state_.compare_exchange_strong(expected, FireOverrideState::CONFIRM_WAITING)) {
            confirm_step_ = 1;
            record_event(FireOverrideState::IDLE, FireOverrideState::CONFIRM_WAITING, "Fire sequence initiated");
            return true;
        }
        return false;
    }

    bool confirm_step(int step) {
        FireOverrideState current = state_.load();
        if (current != FireOverrideState::CONFIRM_WAITING) {
            return false;
        }
        
        int expected_step = confirm_step_.load();
        if (step != expected_step) {
            return false;
        }
        
        if (step == CONFIRM_STEPS) {
            if (state_.compare_exchange_strong(current, FireOverrideState::CONFIRMED)) {
                record_event(FireOverrideState::CONFIRM_WAITING, FireOverrideState::CONFIRMED, "All confirmations received");
                activate_fire();
                return true;
            }
        } else {
            confirm_step_ = step + 1;
            record_event(FireOverrideState::CONFIRM_WAITING, FireOverrideState::CONFIRM_WAITING, 
                        "Confirmation step " + std::to_string(step) + " received");
            return true;
        }
        return false;
    }

    bool is_fire_authorized() const {
        FireOverrideState current = state_.load();
        return current == FireOverrideState::ACTIVE;
    }

    FireOverrideState get_state() const {
        return state_.load();
    }

    int get_confirm_step() const {
        return confirm_step_.load();
    }

    void cancel() {
        FireOverrideState current = state_.load();
        if (current == FireOverrideState::ACTIVE ||
            current == FireOverrideState::CONFIRM_WAITING ||
            current == FireOverrideState::CONFIRMED) {
            record_event(current, FireOverrideState::IDLE, "Fire sequence cancelled");
            state_.store(FireOverrideState::IDLE);
            confirm_step_ = 0;
        }
    }

    void set_audit_callback(std::function<void(const FireOverrideEvent&)> callback) {
        audit_callback_ = callback;
    }

    void set_state_callback(std::function<void(FireOverrideState, FireOverrideState)> callback) {
        state_callback_ = callback;
    }

    bool get_audit_event(FireOverrideEvent& event) {
        std::lock_guard<std::mutex> lock(event_mutex_);
        if (!event_queue_.empty()) {
            event = event_queue_.front();
            event_queue_.pop();
            return true;
        }
        return false;
    }

    void reset() {
        cancel();
        std::lock_guard<std::mutex> lock(event_mutex_);
        while (!event_queue_.empty()) {
            event_queue_.pop();
        }
    }

private:
    void activate_fire() {
        auto now = std::chrono::steady_clock::now();
        active_until_.store(now + ACTIVE_TIMEOUT);
        state_.store(FireOverrideState::ACTIVE);
        record_event(FireOverrideState::CONFIRMED, FireOverrideState::ACTIVE, "Fire authorized - timeout active");
    }

    void timeout_loop() {
        while (running_) {
            auto now = std::chrono::steady_clock::now();
            
            // Check confirm timeout
            if (state_.load() == FireOverrideState::CONFIRM_WAITING) {
                auto state_time = state_timestamp_.load();
                if (now - state_time > CONFIRM_TIMEOUT) {
                    record_event(FireOverrideState::CONFIRM_WAITING, FireOverrideState::TIMEOUT, "Confirmation timeout");
                    state_.store(FireOverrideState::TIMEOUT);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    state_.store(FireOverrideState::IDLE);
                    confirm_step_ = 0;
                }
            }
            
            // Check active timeout
            if (state_.load() == FireOverrideState::ACTIVE) {
                auto until = active_until_.load();
                if (now > until) {
                    record_event(FireOverrideState::ACTIVE, FireOverrideState::IDLE, "Active timeout expired");
                    state_.store(FireOverrideState::IDLE);
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void record_event(FireOverrideState from, FireOverrideState to, const std::string& details) {
        FireOverrideEvent event;
        event.timestamp = std::chrono::steady_clock::now();
        event.from_state = from;
        event.to_state = to;
        event.details = details;
        
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            event_queue_.push(event);
            // Keep only last 100 events
            while (event_queue_.size() > 100) {
                event_queue_.pop();
            }
        }
        
        if (audit_callback_) {
            audit_callback_(event);
        }
        
        if (state_callback_) {
            state_callback_(from, to);
        }
    }
};

class ManualFireOverride {
public:
    struct OverrideConfig {
        int required_confirms = 2;
        std::chrono::seconds confirm_timeout = std::chrono::seconds(5);
        std::chrono::seconds active_timeout = std::chrono::seconds(10);
        bool require_kill_switch_check = true;
    };

private:
    FireOverrideSafety safety_;
    OverrideConfig config_;
    std::atomic<bool> kill_switch_verified_;

public:
    explicit ManualFireOverride(const OverrideConfig& cfg = OverrideConfig{})
        : config_(cfg), kill_switch_verified_(false) {
        safety_.set_audit_callback([this](const FireOverrideEvent& e) {
            log_audit_event(e);
        });
    }

    bool request_fire() {
        if (config_.require_kill_switch_check && !kill_switch_verified_.load()) {
            return false;
        }
        return safety_.initiate_fire_sequence();
    }

    bool confirm(int step) {
        return safety_.confirm_step(step);
    }

    bool is_authorized() const {
        return safety_.is_fire_authorized();
    }

    void cancel() {
        safety_.cancel();
    }

    void verify_kill_switch() {
        kill_switch_verified_ = true;
    }

    void invalidate_kill_switch() {
        kill_switch_verified_ = false;
        safety_.cancel();
    }

    void set_config(const OverrideConfig& cfg) {
        config_ = cfg;
    }

    FireOverrideState get_state() const {
        return safety_.get_state();
    }

private:
    void log_audit_event(const FireOverrideEvent& event) {
        // Audit logging implementation
        // Would integrate with system logging in production
    }
};

} // namespace Aurore

#endif // AURORE_FIRE_OVERRIDE_SAFETY_H
