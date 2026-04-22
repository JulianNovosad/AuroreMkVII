# IMU Sensor Integration (SensaGram)

**Module:** `aurore::ImuReceiver`
**Headers:** `include/aurore/imu_receiver.hpp`, `src/sensors/imu_receiver.cpp`
**Spec:** AM7-L2-IF-009, AM7-L2-IMU-001, ICD-008

---

## Overview

The AuroreMkVII system integrates IMU (Inertial Measurement Unit) data from an Android phone (Samsung A54) running the **SensaGram** app. The phone sends sensor data over UDP to the Pi 5, which uses it for:

1. **Gimbal stabilization**: Compensate for mount movement using gyroscope angular velocity
2. **Fire control solution**: Angular velocity → lead angle calculation for moving targets

### Architecture

```
┌─────────────────┐
│   Phone (A54)   │
│   SensaGram App │
│   4 IMU sensors │
└────────┬────────┘
         │
         │ UDP @ 7070
         │ JSON to 127.0.0.1
         │
         ▼
┌────────────────────────────────────────────────┐
│         Raspberry Pi 5 (AuroreMkVII)           │
│  ┌──────────────────────────────────────────┐  │
│  │  eth0 (wired)                            │  │
│  │  - IMU data ingestion                    │  │
│  │  - UDP port 7070                         │  │
│  │  - ImuReceiver class                     │  │
│  └──────────────────────────────────────────┘  │
│                      │                         │
│                      ▼                         │
│  ┌──────────────────────────────────────────┐  │
│  │  StateMachine::on_imu_data()             │  │
│  │  - Gimbal stabilization                  │  │
│  │  - Fire control solution                 │  │
│  └──────────────────────────────────────────┘  │
└────────────────────────────────────────────────┘
```

---

## SensaGram App Setup

### Installation

Install SensaGram on your Android phone:

[<img src="https://github.com/user-attachments/assets/0f628053-199f-4587-a5b2-034cf027fb99" height="60">](https://github.com/UmerCodez/SensaGram/releases) [<img src="https://fdroid.gitlab.io/artwork/badge/get-it-on.png" alt="Get it on F-Droid" height="60">](https://f-droid.org/packages/com.github.umer0586.sensagram/)

### Configuration

1. **Open SensaGram app**

2. **Select sensors** (tap to enable):
   - ☑️ `android.sensor.accelerometer`
   - ☑️ `android.sensor.gyroscope`
   - ☑️ `android.sensor.device_orientation`
   - ☑️ `android.sensor.orientation`

3. **Configure network**:
   - **Address**: `127.0.0.1` (or Pi 5 IP if phone is on same network via Wi-Fi)
   - **Port**: `7070`

4. **Start streaming**: Tap the "Stream" button

### Phone Mounting

For best results:
- Mount phone rigidly to the gimbal or base plate
- Ensure phone orientation is consistent (portrait or landscape)
- Use a phone holder with secure grip
- Keep phone charged (IMU streaming drains battery)

---

## Usage

### Basic Initialization

```cpp
#include "aurore/imu_receiver.hpp"

// Create and configure IMU receiver
aurore::ImuReceiverConfig config;
config.udp_port = 7070;
config.bind_address = "127.0.0.1";  // Loopback (SensaGram default)
config.enable_accelerometer = true;
config.enable_gyroscope = true;
config.enable_device_orientation = true;
config.enable_orientation = true;

aurore::ImuReceiver imu_receiver(config);

// Initialize UDP socket
if (!imu_receiver.init()) {
    std::cerr << "Failed to initialize IMU receiver\n";
    return false;
}

// Start receiver thread
imu_receiver.start();
```

### Reading IMU Data in Control Loop

```cpp
// In your 120Hz control loop:
aurore::ImuData imu = imu_receiver.get_latest_data();

// Check if data is fresh (< 100ms old)
if (imu_receiver.is_data_fresh()) {
    // Use gyroscope for gimbal stabilization
    if (imu.gyro_valid) {
        float angular_velocity_yaw = imu.gyro_z;  // rad/s
        float angular_velocity_pitch = imu.gyro_y;
        
        // Compensate gimbal commands
        gimbal_cmd.az_deg += angular_velocity_yaw * kGain;
        gimbal_cmd.el_deg += angular_velocity_pitch * kGain;
    }
    
    // Use accelerometer for tilt compensation
    if (imu.accel_valid) {
        float tilt_angle = std::atan2(imu.accel_y, imu.accel_z);
        // Apply tilt compensation to fire control solution
    }
    
    // Use orientation for absolute heading
    if (imu.orientation_valid) {
        float azimuth = imu.azimuth_deg;  // 0-360 degrees
        // Use as absolute heading reference
    }
} else {
    // IMU data stale - fall back to vision-only mode
    std::cerr << "IMU data stale, using vision-only tracking\n";
}
```

### Getting Individual Sensor Data

```cpp
// Get latest accelerometer sample
std::optional<aurore::ImuSample> accel = 
    imu_receiver.get_latest_sample(aurore::ImuSensorType::kAccelerometer);

if (accel.has_value()) {
    float ax = accel->values[0];
    float ay = accel->values[1];
    float az = accel->values[2];
    uint64_t sensor_ts = accel->sensor_timestamp_ns;
    uint64_t receipt_ts = accel->receipt_timestamp_ns;
}

// Get latest gyroscope sample
std::optional<aurore::ImuSample> gyro = 
    imu_receiver.get_latest_sample(aurore::ImuSensorType::kGyroscope);
```

### Monitoring Statistics

```cpp
// Get packet statistics
uint32_t received = imu_receiver.get_packets_received();
uint32_t dropped = imu_receiver.get_packets_dropped();
float avg_latency = imu_receiver.get_average_latency_ms();

std::cout << "IMU stats: received=" << received 
          << ", dropped=" << dropped
          << ", latency=" << avg_latency << " ms\n";

// Check health
if (dropped > received * 0.05) {
    // > 5% packet loss - log warning
    telemetry.log_event(aurore::TelemetryEventId::IMU_DATA_STALE,
                        aurore::TelemetrySeverity::kWarning,
                        "High IMU packet loss");
}
```

---

## JSON Message Format

SensaGram sends JSON-formatted messages over UDP:

### Accelerometer

```json
{
  "type": "android.sensor.accelerometer",
  "timestamp": 3925657519043709,
  "values": [0.31892395, -0.97802734, 10.049896]
}
```

**Values:**
- `values[0]`: Acceleration force along X axis (including gravity), m/s²
- `values[1]`: Acceleration force along Y axis (including gravity), m/s²
- `values[2]`: Acceleration force along Z axis (including gravity), m/s²

### Gyroscope

```json
{
  "type": "android.sensor.gyroscope",
  "timestamp": 3925657529043709,
  "values": [0.00123, 0.00456, 0.00789]
}
```

**Values:**
- `values[0]`: Angular velocity around X axis, rad/s
- `values[1]`: Angular velocity around Y axis, rad/s
- `values[2]`: Angular velocity around Z axis, rad/s

### Device Orientation

```json
{
  "type": "android.sensor.device_orientation",
  "timestamp": 3925657539043709,
  "values": [90.0, 45.0, 180.0]
}
```

**Values:**
- `values[0]`: Azimuth (rotation around Z axis), 0-360 degrees
- `values[1]`: Pitch (rotation around X axis), -180 to 180 degrees
- `values[2]`: Roll (rotation around Y axis), -180 to 180 degrees

### Orientation (Quaternion)

```json
{
  "type": "android.sensor.orientation",
  "timestamp": 3925657549043709,
  "values": [0.707, 0.0, 0.707, 0.0]
}
```

**Values:**
- `values[0]`: Quaternion W component
- `values[1]`: Quaternion X component
- `values[2]`: Quaternion Y component
- `values[3]`: Quaternion Z component

---

## Integration with State Machine

The IMU data is integrated into the state machine for gimbal stabilization and fire control:

```cpp
// In main.cpp track_compute thread:

// Get IMU data
aurore::ImuData imu = imu_receiver.get_latest_data();

if (imu.gyro_valid && state == aurore::FcsState::TRACKING) {
    // Angular velocity → lead angle conversion
    float angular_velocity_rad_s = std::sqrt(
        imu.gyro_x * imu.gyro_x + 
        imu.gyro_y * imu.gyro_y + 
        imu.gyro_z * imu.gyro_z
    );
    
    // Lead angle = angular_velocity * time_of_flight
    float time_of_flight = range_m / muzzle_velocity_mps;
    float lead_angle_rad = angular_velocity_rad_s * time_of_flight;
    float lead_angle_deg = lead_angle_rad * (180.0f / M_PI);
    
    // Apply to gimbal command
    gimbal_cmd.az_deg += lead_angle_deg;
}

// Check for IMU data staleness
if (!imu_receiver.is_data_fresh()) {
    // IMU_DATA_STALE fault (AM7-L3-SAFE-002)
    state_machine.on_fault(aurore::FaultCode::IMU_DATA_STALE);
}
```

---

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| Update rate | ≥100 Hz | Sensor-dependent (phone-dependent) |
| Latency (phone → Pi) | ≤50ms | Network + processing |
| Packet loss tolerance | ≤5% | Over 1-second window |
| Max sample age | 100ms | After which data is considered stale |
| Buffer size | 100 samples | Circular buffer |

---

## Troubleshooting

### No IMU Data Received

1. **Check SensaGram is streaming**:
   - Verify "Stream" button is active
   - Check phone screen shows active streaming

2. **Check network connectivity**:
   ```bash
   # On Pi 5, listen for UDP packets
   nc -ul 7070
   
   # Should see JSON messages
   ```

3. **Check firewall**:
   ```bash
   # Allow UDP port 7070
   sudo ufw allow 7070/udp
   ```

### High Latency

1. **Reduce network congestion**:
   - Use 5 GHz Wi-Fi for phone (if connected via Wi-Fi)
   - Or use USB tethering for lower latency

2. **Check phone performance**:
   - Close background apps
   - Ensure phone isn't thermal throttling

### Packet Loss

1. **Increase buffer size**:
   ```cpp
   config.max_queue_size = 200;  // Default is 100
   ```

2. **Check Wi-Fi signal strength**:
   ```bash
   # On phone, check Wi-Fi RSSI
   # Should be > -70 dBm for reliable streaming
   ```

---

## Security Considerations

**Current configuration**: UDP on loopback (127.0.0.1) or local network only.

**If exposing to network**:
- Bind to specific interface (not 0.0.0.0)
- Consider adding simple authentication
- Use firewall rules to restrict source IPs

---

## Related Documentation

- [spec.md](../spec.md) - AM7-L2-IF-009, AM7-L2-IMU-001, ICD-008
- [wifi_ap_setup.md](./wifi_ap_setup.md) - Wi-Fi AP configuration
- [state_machine.md](./state_machine.md) - State machine integration

---

## References

- [SensaGram GitHub](https://github.com/UmerCodez/SensaGram)
- [Android SensorEvent](https://developer.android.com/reference/android/hardware/SensorEvent)
- [Android Motion Sensors](https://developer.android.com/guide/topics/sensors/sensors_motion)
- [Android Position Sensors](https://developer.android.com/guide/topics/sensors/sensors_position)
