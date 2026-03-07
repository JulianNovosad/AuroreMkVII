#pragma once
#include <string>
#include "../telemetry/telemetry.hpp"
#include "../decision/safety_monitor.hpp"

namespace aurore {

class ServoDriverHardware {
public:
    ServoDriverHardware(Telemetry& telemetry, SafetyMonitor& safety);
    ~ServoDriverHardware();

    bool initialize(int bus = 1, int address = 0x40);
    void set_pwm(int channel, int pulse_us);
    void safe_stop();
    bool is_kill_switch_engaged();

private:
    bool set_frequency(float freq_hz);
    Telemetry& telemetry_;
    SafetyMonitor& safety_;
    int i2c_fd_;
    bool initialized_;
};

} // namespace aurore
