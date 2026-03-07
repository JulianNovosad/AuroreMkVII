# GPIO Interlock Controller

## Overview

The GPIO Interlock Controller provides hardware-based safety interlock for the AuroreMkVII fire control system. It monitors a physical safety circuit and controls an inhibit output that prevents actuation when the interlock is open.

**Safety Level:** SIL-2 capable (with proper external circuitry)

---

## Hardware Requirements

### Raspberry Pi GPIO Connections

| Signal | GPIO Pin (BCM) | Physical Pin | Direction | Description |
|--------|----------------|--------------|-----------|-------------|
| INTERLOCK_IN | GPIO 17 | Pin 11 | Input | Safety circuit input (active-low) |
| INHIBIT_OUT | GPIO 27 | Pin 13 | Output | Actuation inhibit output |
| STATUS_LED | GPIO 22 | Pin 15 | Output | Status indicator |

### External Circuit

```
+3.3V ──┬──[10kΩ]──┬── GPIO 17 (INTERLOCK_IN)
        │          │
     [Interlock]   │
        │          │
 GND ───┴──────────┴──

GPIO 27 (INHIBIT_OUT) ──[1kΩ]──┬── Relay/Transistor ── Actuation Power
                               │
                            [Pull-down]
                               │
                              GND
```

**Interlock Switch:** Normally-closed (NC) safety switch that opens when:
- Enclosure is opened
- Emergency stop is pressed
- Safety key is removed

---

## API Reference

### Configuration

```cpp
#include "aurore/interlock_controller.hpp"

aurore::InterlockConfig config;
config.input_pin = 17;           // BCM GPIO for interlock input
config.inhibit_pin = 27;         // BCM GPIO for inhibit output
config.status_led_pin = 22;      // BCM GPIO for status LED
config.debounce_ms = 50;         // Input debounce time
config.poll_interval_ms = 10;    // Monitoring thread interval
config.active_low = true;        // Input grounded when closed
config.enable_watchdog = true;   // Enable hardware watchdog
config.watchdog_timeout_ms = 1000; // Watchdog timeout
```

### Basic Usage

```cpp
aurore::InterlockController interlock(config);

// Initialize GPIO hardware
if (!interlock.init()) {
    std::cerr << "Interlock initialization failed" << std::endl;
    return -1;
}

// Start monitoring thread
interlock.start();

// In safety monitor thread (1kHz):
interlock.watchdog_feed();  // Must call every <watchdog_timeout_ms

// Check if actuation is allowed
if (interlock.is_actuation_allowed()) {
    // Safe to actuate
    enable_actuation();
} else {
    // Interlock open - inhibit actuation
    disable_actuation();
}

// Get detailed status
aurore::InterlockStatus status = interlock.get_status();
std::cout << "Interlock state: " 
          << aurore::interlock_state_to_string(status.state) << std::endl;

// Cleanup
interlock.stop();
```

### State Enumeration

```cpp
enum class InterlockState : uint8_t {
    OPEN,       // Safety circuit open - actuation inhibited
    CLOSED,     // Safety circuit closed - actuation enabled
    FAULT,      // Hardware fault or watchdog timeout
    UNKNOWN     // Initial state before first read
};
```

### Status Structure

```cpp
struct InterlockStatus {
    InterlockState state;           // Current state
    uint64_t last_change_ns;        // Timestamp of last state change
    uint64_t transition_count;      // Number of state transitions
    uint64_t fault_count;           // Number of fault events
    uint64_t watchdog_feeds;        // Watchdog feed counter
    uint64_t last_watchdog_feed_ns; // Timestamp of last watchdog feed
    bool actuation_inhibited;       // True if actuation is inhibited
};
```

---

## Integration with Safety Monitor

The interlock controller integrates with the SafetyMonitor for comprehensive safety coverage:

```cpp
// In safety monitor initialization:
aurore::InterlockController interlock;
interlock.init();
interlock.start();

// In safety monitor thread (1kHz):
interlock.watchdog_feed();

// Check interlock state in run_cycle():
if (!interlock.is_actuation_allowed()) {
    safety_monitor.trigger_fault(
        aurore::SafetyFaultCode::INTERLOCK_FAULT,
        "Hardware interlock open"
    );
}
```

---

## LED Status Indicators

| LED Pattern | State | Description |
|-------------|-------|-------------|
| OFF | OPEN | Safety circuit open (safe state) |
| ON (steady) | CLOSED | Safety circuit closed (armed) |
| Blinking (5Hz) | FAULT | Fault condition (watchdog timeout, hardware fault) |

---

## Timing Requirements

| Parameter | Value | Description |
|-----------|-------|-------------|
| Polling interval | 10ms | Interlock input sampling rate |
| Debounce time | 50ms | Input debounce filter |
| Watchdog timeout | 1000ms | Maximum time between watchdog feeds |
| Inhibit response | <1ms | Time from fault to inhibit activation |

---

## Error Handling

### Initialization Failures

```cpp
if (!interlock.init()) {
    // Possible causes:
    // - /dev/gpiomem not accessible (run as root or add user to gpio group)
    // - Invalid GPIO pin configuration
    // - Memory mapping failed
}
```

### Runtime Faults

| Fault | Cause | Recovery |
|-------|-------|----------|
| WATCHDOG_TIMEOUT | watchdog_feed() not called within timeout | Call watchdog_feed() regularly |
| INPUT_STUCK | Interlock input stuck high/low | Check wiring, replace switch |
| OUTPUT_FAULT | Inhibit output not responding | Check GPIO, replace Pi |

---

## Testing

### Unit Test Example

```cpp
TEST(test_interlock_basic) {
    aurore::InterlockConfig config;
    config.input_pin = 17;
    config.inhibit_pin = 27;
    
    aurore::InterlockController interlock(config);
    
    // Test initialization
    ASSERT_TRUE(interlock.init());
    
    // Test initial state (should be UNKNOWN or OPEN)
    auto state = interlock.get_state();
    ASSERT_TRUE(state == aurore::InterlockState::UNKNOWN || 
                state == aurore::InterlockState::OPEN);
    
    // Test inhibit
    interlock.set_inhibit(true);
    ASSERT_TRUE(!interlock.is_actuation_allowed());
    
    // Test force state
    interlock.force_state(aurore::InterlockState::CLOSED);
    ASSERT_EQ(interlock.get_state(), aurore::InterlockState::CLOSED);
    
    interlock.stop();
}
```

### Hardware Test

```bash
# Run interlock test (requires root for GPIO access)
sudo ./interlock_test --input-pin=17 --inhibit-pin=27

# Monitor status
watch -n 0.1 'cat /sys/kernel/debug/gpio'
```

---

## Security Considerations

1. **Physical Security:** Interlock switch must be tamper-resistant
2. **Fail-Safe Design:** Inhibit output defaults to active (inhibiting) on power loss
3. **Watchdog:** Hardware watchdog prevents software runaway
4. **Debounce:** Input debounce prevents false triggering from switch bounce
5. **Monitoring:** Transition count and fault count enable predictive maintenance

---

## Troubleshooting

| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| Always OPEN | Interlock switch open | Check switch wiring, close enclosure |
| Always FAULT | Watchdog not fed | Ensure watchdog_feed() called regularly |
| LED not lit | GPIO not configured | Check pin numbers, run as root |
| Intermittent | Loose wiring | Check connections, add pull-up resistor |

---

## Regulatory Compliance

**Note:** This interlock controller is for educational/personal use only. It is NOT certified for industrial safety applications (SIL-3, PL-e).

For commercial deployment:
- Use certified safety relay module
- Implement redundant interlock circuits
- Perform FMEA (Failure Mode and Effects Analysis)
- Obtain appropriate safety certifications

---

**See Also:**
- `include/aurore/interlock_controller.hpp` - Header file
- `src/safety/interlock_controller.cpp` - Implementation
- `include/aurore/safety_monitor.hpp` - Safety monitor integration
- `spec.md` - System requirements (AM7-L3-MODE-007)
