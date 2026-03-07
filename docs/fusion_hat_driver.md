# Fusion HAT+ Driver Documentation

## Overview

The AuroreMkVII Fusion HAT+ driver provides C++17 interface to the Sunfounder Fusion HAT+ PWM/servo controller board for Raspberry Pi.

**Key Features:**
- 12-channel PWM output (500-2500μs pulse width)
- 50Hz servo control (standard RC servos)
- Sysfs-based interface (kernel driver handles I2C)
- Thread-safe operation
- Software endstops and safety limits
- Real-time capable (no heap allocation in hot path)

---

## Hardware Requirements

- **Board:** Sunfounder Fusion HAT+
- **Host:** Raspberry Pi 4/5
- **Interface:** Sysfs (`/sys/class/fusion_hat/fusion_hat/pwm*`)
- **I2C Address:** 0x17 (handled by kernel driver)

### Installation

The Fusion HAT+ requires the Sunfounder kernel driver:

```bash
# Install Fusion HAT+ driver
curl -sSL https://raw.githubusercontent.com/sunfounder/fusion-hat/main/install.sh | sudo bash

# Reboot Raspberry Pi
sudo reboot

# Verify installation
ls /sys/class/fusion_hat/fusion_hat/
# Should show: pwm0 pwm1 ... pwm11
```

---

## API Reference

### Configuration

```cpp
#include "aurore/fusion_hat.hpp"

aurore::FusionHatConfig config;
config.servo_freq_hz = 50;              // PWM frequency (50Hz for servos)
config.min_pulse_width_us = 500;        // Minimum pulse width (-90°)
config.max_pulse_width_us = 2500;       // Maximum pulse width (+90°)
config.min_angle_deg = -90.0f;          // Minimum angle
config.max_angle_deg = 90.0f;           // Maximum angle
config.enable_endstops = true;          // Enable software limits
config.enable_rate_limit = false;       // Rate limiting (optional)
config.max_angular_velocity_dps = 100;  // Max degrees/second

if (!config.validate()) {
    std::cerr << "Invalid configuration" << std::endl;
}
```

### Basic Usage

```cpp
aurore::FusionHat hat(config);

// Initialize (checks hardware presence)
if (!hat.init()) {
    std::cerr << "Fusion HAT+ not found" << std::endl;
    return -1;
}

// Set servo angle (channel 0, 0° = center)
hat.set_servo_angle(0, 0.0f);

// Full range motion
hat.set_servo_angle(0, -90.0f);  // Full left
hat.set_servo_angle(0, 90.0f);   // Full right

// Direct pulse width control
hat.set_servo_pulse_width(0, 1500);  // 1500μs = center

// Get current state
auto angle = hat.get_servo_angle(0);
if (angle.has_value()) {
    std::cout << "Current angle: " << *angle << "°" << std::endl;
}

// Disable servo (stops PWM output)
hat.set_servo_enabled(0, false);

// Emergency stop - disable all channels
hat.disable_all_servos();
```

### Gimbal Control (Azimuth/Elevation)

```cpp
// Configure two servos for gimbal
aurore::FusionHat hat;
hat.init();

// Azimuth (channel 0): ±180° rotation
hat.set_endstop_limits(0, -180.0f, 180.0f);

// Elevation (channel 1): -10° to +45° (mechanical limits)
hat.set_endstop_limits(1, -10.0f, 45.0f);

// Point gimbal
hat.set_servo_angle(0, 45.0f);   // Azimuth: 45° right
hat.set_servo_angle(1, 15.0f);   // Elevation: 15° up

// Get status
auto status = hat.get_servo_status(0);
std::cout << "Azimuth: " << status.angle_deg << "°"
          << ", Enabled: " << status.enabled << std::endl;
```

### PWM Control (Direct)

```cpp
// Set custom PWM frequency
hat.set_pwm_freq(2, 1000);  // 1kHz for ESC/motor controller

// Set duty cycle (0-100%)
hat.set_pwm_duty_cycle(2, 50);  // 50% duty cycle

// Set period directly (microseconds)
hat.set_pwm_period(3, 20000);  // 20ms = 50Hz

// Read current values
int freq = hat.get_pwm_freq(2);
int duty = hat.get_pwm_duty_cycle(2);
int period = hat.get_pwm_period(2);
```

---

## Integration with AuroreMkVII

### Main Thread Integration

The Fusion HAT+ driver is integrated into the actuation output thread:

```cpp
// In src/main.cpp actuation_thread:

aurore::FusionHat hat;
if (!hat.init()) {
    std::cerr << "Fusion HAT+ not available" << std::endl;
}

// In actuation loop:
while (running) {
    timing.wait();  // 120Hz
    
    // Read track solution from buffer
    TrackSolution solution;
    if (track_buffer.pop(solution)) {
        if (solution.valid) {
            // Convert centroid to gimbal angles
            float az = (solution.centroid_x - width/2) * pixels_per_degree;
            float el = (solution.centroid_y - height/2) * pixels_per_degree;
            
            // Command gimbal
            hat.set_servo_angle(0, az);  // Azimuth
            hat.set_servo_angle(1, el);  // Elevation
            
            // Update safety monitor
            safety_monitor.update_actuation_frame(sequence, timestamp);
        }
    }
}
```

### Safety Monitor Integration

```cpp
// In safety_monitor thread:
if (!hat.is_connected()) {
    safety_monitor.trigger_fault(
        aurore::SafetyFaultCode::GIMBAL_TIMEOUT,
        "Fusion HAT+ not connected"
    );
}

// Monitor servo status
for (int ch = 0; ch < 2; ch++) {
    auto status = hat.get_servo_status(ch);
    if (status.endstop_active) {
        // Servo at limit - log warning
        telemetry.log_warning("Servo " + std::to_string(ch) + " at endstop");
    }
}
```

---

## Technical Details

### Pulse Width to Angle Mapping

The driver uses linear interpolation:

```
pulse_width = min_pw + (angle - min_angle) / (max_angle - min_angle) * (max_pw - min_pw)

Example (standard servo):
- min_angle = -90°, max_angle = 90°
- min_pw = 500μs, max_pw = 2500μs
- angle = 0° → pulse_width = 500 + (90/180) * 2000 = 1500μs
```

### Sysfs Interface

The Fusion HAT+ kernel driver exposes PWM channels via sysfs:

```
/sys/class/fusion_hat/fusion_hat/
├── pwm0/
│   ├── enable          # 1=enable, 0=disable
│   ├── period          # Period in μs (20000 for 50Hz)
│   └── duty_cycle      # Duty cycle in μs (500-2500 for servos)
├── pwm1/
├── ...
├── pwm11/
├── firmware_version    # MCU firmware version
└── version             # Driver version
```

### Timing Characteristics

| Operation | Typical Latency | WCET |
|-----------|-----------------|------|
| `set_servo_angle()` | ~100μs | ~500μs |
| `set_pwm_duty_cycle()` | ~50μs | ~200μs |
| `get_servo_status()` | ~10μs | ~50μs |

**Note:** Sysfs file I/O involves kernel context switches. For hard real-time requirements, consider using the pigpio library or direct GPIO memory mapping.

---

## Troubleshooting

### "Fusion HAT+ not connected"

```bash
# Check driver installation
ls /sys/class/fusion_hat/fusion_hat/

# Check device tree
cat /proc/device-tree/*/uuid | grep 0774

# Reinstall driver if needed
curl -sSL https://raw.githubusercontent.com/sunfounder/fusion-hat/main/install.sh | sudo bash
```

### Servos jittering

- Ensure adequate power supply (5V/3A minimum)
- Check ground connections
- Reduce servo speed with rate limiting:
  ```cpp
  hat.set_rate_limit(true, 50.0f);  // 50°/second max
  ```

### PWM channel not responding

```bash
# Check channel enable state
cat /sys/class/fusion_hat/fusion_hat/pwm0/enable

# Manually enable
echo 1 | sudo tee /sys/class/fusion_hat/fusion_hat/pwm0/enable
```

---

## Testing

### Unit Tests

```bash
cd build-native
ctest -R FusionHatTest --output-on-failure
```

### Hardware Test

```cpp
// Sweep all servos
for (int ch = 0; ch < 12; ch++) {
    for (float angle = -90.0f; angle <= 90.0f; angle += 5.0f) {
        hat.set_servo_angle(ch, angle);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
```

---

## Safety Considerations

1. **Power:** Servos can draw significant current. Use separate 5V/3A+ supply.
2. **Mechanical Limits:** Always configure software endstops to match mechanical limits.
3. **Emergency Stop:** Use `disable_all_servos()` for immediate shutdown.
4. **Rate Limiting:** Enable for smooth motion and to prevent mechanical damage.

---

## References

- [Sunfounder Fusion HAT+ Repository](https://github.com/sunfounder/fusion-hat)
- [Fusion HAT+ Documentation](https://docs.sunfounder.com/projects/fusion-hat/)
- `include/aurore/fusion_hat.hpp` - Header file
- `src/drivers/fusion_hat.cpp` - Implementation
- `spec.md` - AM7-L2-ACT-001 (Actuation output requirements)
