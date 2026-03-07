#include "servo.hpp"
#include <iostream>
#include <string>

namespace aurore {

ServoDriver::ServoDriver(Telemetry& telemetry, SafetyMonitor& safety) 
    : telemetry_(telemetry), safety_(safety), current_value_(1500), initialized_(false) {}

ServoDriver::~ServoDriver() {
    enter_safe_state();
}

bool ServoDriver::initialize() {
    initialized_ = true;
    telemetry_.log_event("servo", "initialized", "PWM 333Hz");
    return true;
}

void ServoDriver::set_value(int value) {
    if (!initialized_) return;

    if (!safety_.is_actuation_allowed()) {
        telemetry_.log_event("servo", "blocked", "safety_gate_engaged");
        enter_safe_state();
        return;
    }
    
    if (value < 1000) value = 1000;
    if (value > 2000) value = 2000;

    current_value_ = value;
    telemetry_.log_event("servo", "write", std::to_string(value));
}

void ServoDriver::enter_safe_state() {
    current_value_ = 1500;
}

bool ServoDriver::is_kill_switch_engaged() {
    return safety_.get_kill_switch_engaged();
}

} // namespace aurore