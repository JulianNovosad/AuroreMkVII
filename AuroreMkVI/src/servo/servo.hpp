#pragma once
#include <string>
#include "../telemetry/telemetry.hpp"
#include "../decision/safety_monitor.hpp"

namespace aurore {

class ServoDriver {
public:
    ServoDriver(Telemetry& telemetry, SafetyMonitor& safety);
    ~ServoDriver();

    bool initialize();
    void set_value(int value);
    void enter_safe_state();

    bool is_kill_switch_engaged();

private:
    Telemetry& telemetry_;
    SafetyMonitor& safety_;
    int current_value_;
    bool initialized_;
};

} // namespace aurore