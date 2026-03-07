# Telemetry Writer

**Module:** `aurore::TelemetryWriter`  
**Headers:** `include/aurore/telemetry_writer.hpp`, `include/aurore/telemetry_types.hpp`  
**Implementation:** `src/common/telemetry_writer.cpp`

---

## Overview

The Telemetry Writer provides asynchronous, non-blocking telemetry logging for the Aurore MkVII fire control system. It captures frame-by-frame data from the control loop and writes it to CSV and JSON formats for post-session analysis and remote HUD rendering.

**Related Requirements:**
- AM7-L2-HUD-001: System shall provide telemetry data for remote HUD rendering
- AM7-L2-HUD-002: HUD telemetry shall include reticle, range, state machine status, target bounding box
- AM7-L2-HUD-004: HUD telemetry update rate shall be 120 Hz synchronized to frame boundary
- AM7-L3-TIM-001: System shall use CLOCK_MONOTONIC_RAW as sole time source

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Control Loop (120Hz)                         │
│                                                                 │
│  vision_pipeline → track_compute → actuation_output             │
│         │                │                    │                 │
│         └────────────────┼────────────────────┘                 │
│                          │                                      │
│                          ▼                                      │
│              ┌───────────────────────┐                          │
│              │   TelemetryWriter     │                          │
│              │   (non-blocking)      │                          │
│              └───────────┬───────────┘                          │
│                          │                                      │
│                          ▼                                      │
│              ┌───────────────────────┐                          │
│              │   Async Writer Thread │                          │
│              │   (background I/O)    │                          │
│              └───────────┬───────────┘                          │
│                          │                                      │
│                          ▼                                      │
│         ┌────────────────┼────────────────┐                     │
│         │                │                │                     │
│    logs/run_*.csv   logs/run_*.json   console (opt)            │
└─────────────────────────────────────────────────────────────────┘
```

### Key Design Decisions

1. **Async I/O**: Writer thread runs independently, preventing control loop blocking
2. **Backpressure Handling**: Configurable drop policies prevent queue overflow DoS
3. **Zero-Copy Compatible**: Telemetry data is copied once at log time (acceptable for non-RT logging)
4. **Session Rotation**: Automatic log rotation prevents disk exhaustion

---

## Usage

### Basic Initialization

```cpp
#include "aurore/telemetry_writer.hpp"

// Create and configure telemetry writer
aurore::TelemetryWriter telemetry;
aurore::TelemetryConfig config;

// Configure output directory and formats
config.log_dir = "logs";
config.session_prefix = "run";
config.enable_csv = true;      // Write CSV frame logs
config.enable_json = true;     // Write JSON session summary
config.enable_console = false; // Optional: mirror to stdout

// Configure backpressure (SEC-010)
config.max_queue_size = 100;           // Max entries in queue
config.backpressure_policy = aurore::BackpressurePolicy::kDropOldest;

// Start the writer (spawns async thread)
if (!telemetry.start(config)) {
    // Handle initialization failure
    return false;
}
```

### Logging in Control Loop

```cpp
// In your 120Hz control loop (non-blocking call)
void control_cycle() {
    // ... vision, tracking, actuation work ...
    
    // Prepare telemetry data
    aurore::DetectionData detection;
    detection.confidence = 0.95f;
    detection.bbox = {100, 50, 80, 80};
    detection.timestamp_ns = aurore::get_timestamp_ns();
    
    aurore::TrackData track;
    track.centroid_x = 140.0f;
    track.centroid_y = 90.0f;
    track.velocity_x = 2.5f;
    track.velocity_y = -1.0f;
    track.valid = true;
    track.psr = 0.05f;
    
    aurore::ActuationData actuation;
    actuation.azimuth_cmd_deg = 15.5f;
    actuation.elevation_cmd_deg = 22.3f;
    actuation.sequence = frame_count;
    
    aurore::SystemHealthData health;
    health.cpu_temp_c = 45.0f;
    health.fps = 120.0f;
    health.fault_count = 0;
    
    // Log frame (queues entry, returns immediately)
    telemetry.log_frame(detection, track, actuation, health);
}
```

### Event Logging

```cpp
// Log discrete events (state transitions, faults, etc.)
telemetry.log_event(
    aurore::TelemetryEventId::STATE_TRANSITION,
    aurore::TelemetrySeverity::INFO,
    "Transitioned from SEARCH to TRACKING"
);

telemetry.log_event(
    aurore::TelemetryEventId::FAULT,
    aurore::TelemetrySeverity::CRITICAL,
    "Camera timeout detected"
);
```

### Shutdown

```cpp
// Stop telemetry writer (blocks until queue drained)
telemetry.stop();

// Get session output paths
std::string csv_path = telemetry.get_session_path();
// e.g., "logs/run_00001.csv"

// Get statistics
uint64_t entries = telemetry.get_entries_written();
size_t dropped = telemetry.get_entries_dropped();
```

---

## Configuration Options

### TelemetryConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `log_dir` | `std::string` | `"logs"` | Output directory for log files |
| `session_prefix` | `std::string` | `"run"` | File prefix for session logs |
| `max_file_size_mb` | `size_t` | `100` | Rotate log after N MB |
| `max_sessions` | `size_t` | `10` | Maximum sessions to retain |
| `enable_csv` | `bool` | `true` | Write CSV frame logs |
| `enable_json` | `bool` | `true` | Write JSON session summary |
| `enable_console` | `bool` | `false` | Mirror telemetry to stdout |
| `max_queue_size` | `size_t` | `100` | Max entries in async queue |
| `queue_high_water_pct` | `size_t` | `80` | High-water mark percentage |
| `backpressure_policy` | `BackpressurePolicy` | `kDropOldest` | Drop policy when full |

### BackpressurePolicy

| Policy | Behavior | Use Case |
|--------|----------|----------|
| `kDropOldest` | Drop oldest entries when queue full | Real-time systems (prefer recent data) |
| `kDropNewest` | Drop new entries when queue full | Debug logging (preserve history) |
| `kBlock` | Block producer until space available | Not recommended for RT threads |

---

## Output Formats

### CSV Format (logs/run_*.csv)

```csv
timestamp_ns,frame_num,event_id,severity,confidence,bbox_x,bbox_y,bbox_w,bbox_h,track_x,track_y,track_vx,track_vy,track_valid,track_psr,az_cmd,el_cmd,effector_cmd,cpu_temp,fps,fault_count
1234567890,1,FRAME,0,0.95,100,50,80,80,140.0,90.0,2.5,-1.0,1,0.05,15.5,22.3,0,45.0,120.0,0
1234567990,2,FRAME,0,0.93,102,52,78,82,142.0,92.0,2.3,-0.8,1,0.06,15.7,22.5,0,45.1,120.0,0
```

### JSON Summary (logs/run_*.json)

```json
{
  "session_id": 1,
  "start_time_ns": 1234567890000,
  "end_time_ns": 1234567890500000,
  "duration_ms": 5000,
  "frame_count": 600,
  "entries_written": 600,
  "entries_dropped": 0,
  "queue_high_water_mark": 5,
  "backpressure_active": false,
  "events": [
    {"timestamp_ns": 1234567890000, "event_id": "SESSION_START", "severity": "INFO"},
    {"timestamp_ns": 1234567892500, "event_id": "STATE_TRANSITION", "severity": "INFO", "message": "SEARCH→TRACKING"}
  ]
}
```

---

## Backpressure Monitoring (SEC-010)

The TelemetryWriter implements backpressure handling to prevent queue overflow during high-load scenarios:

```cpp
// Monitor queue depth in real-time
size_t depth = telemetry.get_queue_depth();
bool backpressure = telemetry.is_backpressure_active();

// Get detailed statistics
aurore::TelemetryQueueStats stats = telemetry.get_queue_stats();
// stats.current_depth
// stats.high_water_mark
// stats.max_depth
// stats.total_enqueued
// stats.total_dropped
// stats.backpressure_active
```

### Recommended Monitoring

```cpp
// In safety monitor or health check thread
void check_telemetry_health() {
    auto stats = telemetry.get_queue_stats();
    
    if (stats.backpressure_active) {
        // Log warning - telemetry falling behind
        log_warn("Telemetry backpressure active: depth=%zu, dropped=%lu",
                 stats.current_depth, stats.total_dropped);
    }
    
    if (stats.total_dropped > 100) {
        // Consider increasing max_queue_size or reducing log frequency
        log_error("Excessive telemetry drops: %lu", stats.total_dropped);
    }
}
```

---

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| `log_frame()` latency | <1μs | Queue push only, no I/O |
| Writer thread CPU | ~2-5% | Depends on log volume |
| CSV write throughput | ~1000 entries/sec | Sustained |
| Queue capacity | 100 entries (configurable) | ~833ms buffer at 120Hz |
| Memory per entry | ~200 bytes | CSV + JSON serialization |

---

## Troubleshooting

### Issue: Telemetry queue filling up

**Symptoms:** `get_queue_depth()` consistently high, `backpressure_active=true`

**Solutions:**
1. Increase `max_queue_size` in config
2. Reduce log frequency (log every N frames instead of every frame)
3. Use `kDropNewest` policy to preserve older data
4. Check if writer thread is blocked on I/O (slow disk?)

### Issue: Missing log files

**Symptoms:** No files in `logs/` directory after session

**Check:**
1. `telemetry.is_running()` returns `true`
2. `log_dir` path is writable
3. `enable_csv` and/or `enable_json` are `true`
4. `telemetry.stop()` was called (JSON summary written on shutdown)

### Issue: Timestamps incorrect

**Symptoms:** Timestamps don't match expected CLOCK_MONOTONIC_RAW values

**Solution:** Verify system clock source:
```bash
cat /sys/devices/system/clocksource/clocksource0/current_clocksource
# Should show: mmio-pmmr (RPi 5) or equivalent high-res clock
```

---

## Security Considerations (SEC-010)

1. **Audit Trail**: Telemetry logs provide tamper-evident audit trail of system operation
2. **Sequence Tracking**: Frame numbers provide monotonic sequence for gap detection
3. **Log Rotation**: Prevents disk exhaustion DoS
4. **Backpressure**: Prevents unbounded memory growth under load

---

## Related Documentation

- [State Machine](state_machine.md) - FCS state transitions logged via telemetry
- [spec.md](../spec.md) - Requirements AM7-L2-HUD-* and AM7-L3-TIM-001
- [telemetry_types.hpp](../include/aurore/telemetry_types.hpp) - Event ID and data type definitions
