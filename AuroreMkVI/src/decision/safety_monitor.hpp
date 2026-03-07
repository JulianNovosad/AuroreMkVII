#pragma once

#include "../telemetry/telemetry.hpp"
#include <string>
#include <fstream>
#include <vector>

namespace aurore {

class SafetyMonitor {
public:
    SafetyMonitor(Telemetry& telemetry, int kill_switch_gpio = 23);
    ~SafetyMonitor();

    // Updates all invariants from real sensors
    void update();

    bool is_actuation_allowed() const { return actuation_allowed_; }
    
    float get_cpu_temp() const { return current_temp_; }
    float get_cpu_usage() const { return current_usage_; }
    bool get_kill_switch_engaged() const { return kill_switch_engaged_; }

    // Physical boundary and temporal checks
    bool validate_actuation(int pulse_us);

private:
    Telemetry& telemetry_;
    bool actuation_allowed_;
    int kill_switch_gpio_;
    
    float current_temp_;
    float current_usage_;
    bool kill_switch_engaged_;

    uint64_t last_actuation_ms_;
    const int MIN_PULSE_US = 1000;
    const int MAX_PULSE_US = 2000;
    const int MIN_ACTUATION_INTERVAL_MS = 10; // 100Hz max actuation updates

    // CPU usage tracking
    unsigned long long last_total_user, last_total_user_low, last_total_sys, last_total_idle;

    float read_temp();
    float read_cpu_usage();
    bool read_kill_switch();

    bool init_gpio();
    void cleanup_gpio();
};

} // namespace aurore