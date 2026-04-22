# Hardware Testing Policy

## Core Principle

**NEVER allow graceful degradation in hardware tests.**

All hardware tests for Aurore MkVII must use real hardware and fail immediately if hardware is not connected. This policy ensures:
- Tests accurately reflect deployment conditions
- Missing hardware is detected immediately, not hidden by timeouts
- Developers are notified to connect hardware before running tests
- No false positives from mocked or simulated hardware

## Test Requirements

### 1. Real Hardware Only
- **NO** mocks, simulations, or virtual devices
- **NO** "test pattern mode" for camera tests
- **NO** loopback modes for UART/I2C/SPI
- **NO** software fallbacks that hide missing hardware

### 2. Immediate Failure
- Tests must fail within **500ms** if hardware not detected
- **NO** timeouts >1 second waiting for hardware response
- **NO** retry loops that delay failure notification
- **NO** "SKIP:" messages - tests either PASS or FAIL

### 3. Clear Error Messages
Failure messages must identify:
- **What** hardware is missing
- **Where** it should be connected
- **How** to verify the connection

Examples:
```
FAIL: M01 laser rangefinder not detected on /dev/ttyAMA10
      Check: ls /dev/ttyAMA*
      Fix: Connect M01 LRF to UART pins GPIO14/15

FAIL: IMX708 camera not detected - check MIPI cable connection
      Check: rpicam-hello --list-cameras
      Fix: Reseat MIPI CSI cable, ensure camera is powered

FAIL: Fusion HAT not detected at I2C 0x17
      Check: sudo i2cdetect -y 1
      Fix: Ensure HAT is properly seated on GPIO header

FAIL: Servo not responding on PWM channel 2
      Check: ls /sys/class/fusion_hat/fusion_hat/pwm/
      Fix: Connect servo to PWM2, verify 5V power
```

### 4. User Notification
Tests must inform the user:
- Which hardware is required
- Where to connect it
- How to verify the connection
- Command to re-run after connecting

## Required Hardware Per Test

### Laser Rangefinder Tests
| Test | Hardware | Connection | Verification |
|------|----------|------------|--------------|
| `LaserRangefinderTest` | M01 50m LRF | UART /dev/ttyAMA10 | `ls /dev/ttyAMA*` |
| `LaserValidationTest` | M01 50m LRF | UART /dev/ttyAMA10 | `sudo ./laser_verify` |

**Connection:**
- TX (LRF) → GPIO14 (UART0_TXD)
- RX (LRF) → GPIO15 (UART0_RXD)
- GND (LRF) → GND (RPi)
- VCC (LRF) → 5V (RPi)

### Camera Tests
| Test | Hardware | Connection | Verification |
|------|----------|------------|--------------|
| `FrameAuthenticationTest` | IMX708 camera | MIPI CSI | `rpicam-hello --list-cameras` |
| `VisionPipelineLatencyTest` | IMX708 camera | MIPI CSI | `rpicam-hello --list-cameras` |
| `CameraWrapperTest` | IMX708 camera | MIPI CSI | `rpicam-hello --list-cameras` |

**Connection:**
- Camera module → MIPI CSI port (J4)
- Cable fully inserted, locking mechanism engaged

### Fusion HAT Tests
| Test | Hardware | Connection | Verification |
|------|----------|------------|--------------|
| `FusionHatTest` | Fusion HAT+ | GPIO header | `ls /sys/class/fusion_hat/` |
| `GimbalCommandRateTest` | Fusion HAT+ | GPIO header | `ls /sys/class/fusion_hat/` |
| `ActuationTimingTest` | Fusion HAT+ + servos | GPIO header + PWM | `ls /sys/class/fusion_hat/fusion_hat/pwm/` |

**Connection:**
- HAT → 40-pin GPIO header (J8)
- Ensure all pins aligned, no bent pins
- Servos → PWM channels 0-3 (JST connectors)

## Pre-flight Hardware Check

Run before executing hardware tests:

```bash
sudo ./scripts/check-hardware.sh
```

This script verifies:
1. Camera detected via `rpicam-hello --list-cameras`
2. UART devices present (`ls /dev/ttyAMA*`)
3. Fusion HAT sysfs entries (`ls /sys/class/fusion_hat/`)
4. I2C devices (`sudo i2cdetect -y 1`)
5. PWM channels (`ls /sys/class/fusion_hat/fusion_hat/pwm/`)

## Test Template

All new hardware tests must follow this template:

```cpp
void test_hardware_present() {
    // 1. Attempt to initialize hardware (500ms max)
    if (!hardware.init(device_path)) {
        TEST_ASSERT(false, 
            "HARDWARE NOT CONNECTED: <device> on <port>\n"
            "Check: <verification command>\n"
            "Fix: <connection instructions>");
    }
    
    // 2. Verify hardware responds (500ms max)
    if (!hardware.is_responding()) {
        TEST_ASSERT(false,
            "HARDWARE NOT RESPONDING: <device>\n"
            "Check: <verification command>\n"
            "Fix: <troubleshooting steps>");
    }
    
    // 3. Proceed with actual test
    ...
}
```

## Enforcement

### CMake Configuration
```cmake
set_tests_properties(HardwareTest PROPERTIES
    TIMEOUT 10  # Short timeout - fail fast
    FAIL_REGULAR_EXPRESSION "SKIP|timeout|not connected|graceful"
)
```

### Code Review Checklist
- [ ] No `sleep_for()` > 500ms waiting for hardware
- [ ] No `if (!hardware) return;` or early returns
- [ ] No `std::cout << "SKIP:"` messages
- [ ] Clear error message with verification command
- [ ] User notification to connect hardware

## Violations

If a test violates this policy:
1. **Immediate fix required** - Remove graceful degradation
2. **Add to violation log** - Track recurring violations
3. **Block merge** - PRs with graceful degradation rejected

## References

- `QWEN.md` - Hardware Testing Policy section
- `scripts/check-hardware.sh` - Pre-flight verification
- `tests/hardware_test_template.cpp` - Test template
- `AGENTS.md` - Agent instructions (no graceful degradation)
