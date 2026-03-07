#ifndef AURORE_TARGET_OVERRIDE_SAFETY_H
#define AURORE_TARGET_OVERRIDE_SAFETY_H

#include <atomic>
#include <chrono>
#include <string>
#include <functional>
#include <mutex>
#include <thread>
#include <queue>

namespace Aurore {

enum class TargetOverrideState {
    IDLE,
    PENDING,
    CONFIRMED,
    ACTIVE,
    TIMEOUT,
    CANCELLED
};

struct TargetOverrideEvent {
    std::chrono::steady_clock::time_point timestamp;
    TargetOverrideState from_state;
    TargetOverrideState to_state;
    int target_id;
    std::string details;
};

class TargetOverrideSafety {
public:
    // REMEDIATION 2026-02-02: Finding #6 - Added 5-second timeout protection
    static constexpr std::chrono::seconds OVERRIDE_TIMEOUT = std::chrono::seconds(5);
    static constexpr std::chrono::seconds ACTIVE_TIMEOUT = std::chrono::seconds(30);

private:
    std::atomic<TargetOverrideState> state_{TargetOverrideState::IDLE};
    std::atomic<int> target_id_{-1};
    std::atomic<std::chrono::steady_clock::time_point> state_timestamp_{std::chrono::steady_clock::now()};
    std::atomic<std::chrono::steady_clock::time_point> active_until_{std::chrono::steady_clock::time_point::min()};
    
    std::mutex event_mutex_;
    std::queue<TargetOverrideEvent> event_queue_;
    std::thread timeout_thread_;
    std::atomic<bool> running_{true};
    
    std::function<void(const TargetOverrideEvent&)> audit_callback_;
    std::function<void(TargetOverrideState, TargetOverrideState)> state_callback_;

public:
    TargetOverrideSafety() : running_(true) {
        timeout_thread_ = std::thread(&TargetOverrideSafety::timeout_loop, this);
    }

    ~TargetOverrideSafety() {
        running_ = false;
        if (timeout_thread_.joinable()) {
            timeout_thread_.join();
        }
    }

    TargetOverrideSafety(const TargetOverrideSafety&) = delete;
    TargetOverrideSafety& operator=(const TargetOverrideSafety&) = delete;

    // Request target override with confirmation
    bool request_override(int target_id) {
        TargetOverrideState expected = TargetOverrideState::IDLE;
        if (state_.compare_exchange_strong(expected, TargetOverrideState::PENDING)) {
            target_id_ = target_id;
            record_event(TargetOverrideState::IDLE, TargetOverrideState::PENDING, 
                        target_id, "Target override requested for target " + std::to_string(target_id));
            return true;
        }
        return false;
    }

    // Confirm the override (must be called within 5 seconds)
    bool confirm_override() {
        TargetOverrideState current = state_.load();
        if (current != TargetOverrideState::PENDING) {
            return false;
        }
        
        // Check if within timeout window
        auto now = std::chrono::steady_clock::now();
        auto state_time = state_timestamp_.load();
        if (now - state_time > OVERRIDE_TIMEOUT) {
            record_event(current, TargetOverrideState::TIMEOUT, target_id_.load(), 
                        "Confirmation timeout - override expired");
            state_.store(TargetOverrideState::TIMEOUT);
            return false;
        }
        
        if (state_.compare_exchange_strong(current, TargetOverrideState::CONFIRMED)) {
            activate_override();
            return true;
        }
        return false;
    }

    // Cancel the override
    void cancel() {
        TargetOverrideState current = state_.load();
        if (current != TargetOverrideState::IDLE) {
            record_event(current, TargetOverrideState::CANCELLED, target_id_.load(), "Override cancelled by operator");
            state_.store(TargetOverrideState::IDLE);
            target_id_ = -1;
        }
    }

    // Check if override is active
    bool is_override_active() const {
        return state_.load() == TargetOverrideState::ACTIVE;
    }

    int get_overridden_target_id() const {
        return target_id_.load();
    }

    TargetOverrideState get_state() const {
        return state_.load();
    }

    void set_audit_callback(std::function<void(const TargetOverrideEvent&)> callback) {
        audit_callback_ = callback;
    }

    void set_state_callback(std::function<void(TargetOverrideState, TargetOverrideState)> callback) {
        state_callback_ = callback;
    }

    bool get_audit_event(TargetOverrideEvent& event) {
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
    void activate_override() {
        auto now = std::chrono::steady_clock::now();
        active_until_ = now + ACTIVE_TIMEOUT;
        state_.store(TargetOverrideState::ACTIVE);
        record_event(TargetOverrideState::CONFIRMED, TargetOverrideState::ACTIVE, 
                    target_id_.load(), "Target override activated - timeout active");
    }

    void timeout_loop() {
        while (running_) {
            auto now = std::chrono::steady_clock::now();
            
            // Check pending timeout
            if (state_.load() == TargetOverrideState::PENDING) {
                auto state_time = state_timestamp_.load();
                if (now - state_time > OVERRIDE_TIMEOUT) {
                    record_event(TargetOverrideState::PENDING, TargetOverrideState::TIMEOUT, 
                                target_id_.load(), "Pending override timed out");
                    state_.store(TargetOverrideState::TIMEOUT);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    state_.store(TargetOverrideState::IDLE);
                    target_id_ = -1;
                }
            }
            
            // Check active timeout
            if (state_.load() == TargetOverrideState::ACTIVE) {
                auto until = active_until_.load();
                if (now > until) {
                    record_event(TargetOverrideState::ACTIVE, TargetOverrideState::IDLE, 
                                target_id_.load(), "Active override timed out");
                    state_.store(TargetOverrideState::IDLE);
                    target_id_ = -1;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void record_event(TargetOverrideState from, TargetOverrideState to, int target_id, 
                     const std::string& details) {
        TargetOverrideEvent event;
        event.timestamp = std::chrono::steady_clock::now();
        event.from_state = from;
        event.to_state = to;
        event.target_id = target_id;
        event.details = details;
        
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            event_queue_.push(event);
            while (event_queue_.size() > 100) {
                event_queue_.pop();
            }
        }
        
        state_timestamp_ = event.timestamp;
        
        if (audit_callback_) {
            audit_callback_(event);
        }
        
        if (state_callback_) {
            state_callback_(from, to);
        }
    }
};

} // namespace Aurore

#endif // AURORE_TARGET_OVERRIDE_SAFETY_H
