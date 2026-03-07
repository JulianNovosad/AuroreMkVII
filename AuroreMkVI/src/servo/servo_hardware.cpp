#include "servo_hardware.hpp"
#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <unistd.h>
#include <cmath>

#define PCA9685_MODE1 0x00
#define PCA9685_PRESCALE 0xFE
#define LED0_ON_L 0x06

namespace aurore {

ServoDriverHardware::ServoDriverHardware(Telemetry& telemetry, SafetyMonitor& safety) 
    : telemetry_(telemetry), safety_(safety), i2c_fd_(-1), initialized_(false) {}

ServoDriverHardware::~ServoDriverHardware() {
    std::cout << "DEBUG: ServoDriverHardware destructor enter" << std::endl;
    safe_stop();
    if (i2c_fd_ >= 0) close(i2c_fd_);
    std::cout << "DEBUG: ServoDriverHardware destructor exit" << std::endl;
}

bool ServoDriverHardware::initialize(int bus, int address) {
    std::string device = "/dev/i2c-" + std::to_string(bus);
    i2c_fd_ = open(device.c_str(), O_RDWR);
    if (i2c_fd_ < 0) {
        telemetry_.log_event("servo_hw", "error", "failed_to_open_i2c");
        return false;
    }

    if (ioctl(i2c_fd_, I2C_SLAVE, address) < 0) {
        telemetry_.log_event("servo_hw", "error", "failed_to_set_address");
        return false;
    }

    // Reset PCA9685
    uint8_t buf[2] = {PCA9685_MODE1, 0x00};
    write(i2c_fd_, buf, 2);

    if (!set_frequency(333.0f)) return false;

    initialized_ = true;
    telemetry_.log_event("servo_hw", "initialized", "Bus " + std::to_string(bus) + " Ch 0 @ 333Hz");
    return true;
}

bool ServoDriverHardware::set_frequency(float freq_hz) {
    float prescaleval = 25000000.0f; // 25MHz
    prescaleval /= 4096.0f;
    prescaleval /= freq_hz;
    prescaleval -= 1.0f;
    uint8_t prescale = static_cast<uint8_t>(std::floor(prescaleval + 0.5f));

    uint8_t oldmode[1];
    uint8_t reg = PCA9685_MODE1;
    write(i2c_fd_, &reg, 1);
    read(i2c_fd_, oldmode, 1);

    uint8_t newmode = (oldmode[0] & 0x7F) | 0x10; // sleep
    uint8_t buf[2] = {PCA9685_MODE1, newmode};
    write(i2c_fd_, buf, 2);
    
    buf[0] = PCA9685_PRESCALE;
    buf[1] = prescale;
    write(i2c_fd_, buf, 2);

    buf[0] = PCA9685_MODE1;
    buf[1] = oldmode[0];
    write(i2c_fd_, buf, 2);
    
    usleep(5000);
    
    buf[1] = oldmode[0] | 0xa1; // auto-increment
    write(i2c_fd_, buf, 2);
    
    return true;
}

void ServoDriverHardware::set_pwm(int channel, int pulse_us) {
    if (!initialized_) return;

    if (!safety_.validate_actuation(pulse_us)) {
        // If it's a boundary or temporal violation, we don't necessarily safe_stop() 
        // unless it's a general safety_gate_engaged.
        // But for REQ-010 compliance, we MUST block it.
        if (!safety_.is_actuation_allowed()) {
            telemetry_.log_event("servo_hw", "blocked", "safety_gate_engaged");
            // Do not call safe_stop() here to avoid recursion if safe_stop() uses set_pwm()
        }
        return;
    }

    // Convert pulse_us to 12-bit duty cycle (4096 steps)
    // 333Hz -> period = 3003 us
    uint32_t off = (pulse_us * 4096) / 3003;
    if (off > 4095) off = 4095;

    uint8_t buf[5];
    buf[0] = LED0_ON_L + 4 * channel;
    buf[1] = 0; // ON_L
    buf[2] = 0; // ON_H
    buf[3] = off & 0xFF; // OFF_L
    buf[4] = (off >> 8) & 0xFF; // OFF_H

    write(i2c_fd_, buf, 5);
    telemetry_.log_event("servo_hw", "write", "ch" + std::to_string(channel) + ":" + std::to_string(pulse_us));
}

void ServoDriverHardware::safe_stop() {
    set_pwm(0, 1500); // Neutral
}

bool ServoDriverHardware::is_kill_switch_engaged() {
    return safety_.get_kill_switch_engaged();
}

} // namespace aurore
