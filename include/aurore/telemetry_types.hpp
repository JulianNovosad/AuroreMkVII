/**
 * @file telemetry_types.hpp
 * @brief Data structures for Aurore MkVII telemetry logging
 *
 * Per spec.md AM7-L3-TIM-001: All timestamps use CLOCK_MONOTONIC_RAW
 * Per spec.md AM7-L2-HUD-002: Telemetry includes detection, tracking, and actuation data
 *
 * SEC-009: All string operations use bounds-checked safe functions
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace aurore {

// SEC-009: Explicit size constants for all fixed buffers
static constexpr size_t kModule_name_max = 32;
static constexpr size_t kEvent_name_max = 32;
static constexpr size_t kMessage_max = 256;

/**
 * @brief Telemetry event severity levels
 */
enum class TelemetrySeverity : uint8_t {
    kDebug = 0,
    kInfo = 1,
    kWarning = 2,
    kError = 3,
    kCritical = 4
};

/**
 * @brief Telemetry event IDs (per ICD-004)
 */
enum class TelemetryEventId : uint16_t {
    // System events
    SYSTEM_BOOT = 0x0001,
    SYSTEM_SHUTDOWN = 0x0002,

    // Detection events
    DETECTION_VALID = 0x0101,
    DETECTION_INVALID = 0x0102,
    DETECTION_TIMEOUT = 0x0103,

    // Tracking events
    TRACK_ACQUIRED = 0x0201,
    TRACK_LOST = 0x0202,
    TRACK_UPDATED = 0x0203,

    // Actuation events
    ACTUATION_COMMAND = 0x0301,
    ACTUATION_LIMIT = 0x0302,
    ACTUATION_FAULT = 0x0303,

    // Safety events
    SAFETY_FAULT = 0x0401,
    SAFETY_INHIBIT_ENGAGED = 0x0402,
    SAFETY_INHIBIT_RELEASED = 0x0403,
    WATCHDOG_TIMEOUT = 0x0404,

    // Hardware events
    CAMERA_TIMEOUT = 0x0501,
    GIMBAL_TIMEOUT = 0x0502,
    TEMPERATURE_WARNING = 0x0503,
    TEMPERATURE_CRITICAL = 0x0504,
    I2C_FAULT = 0x0505,
};

/**
 * @brief SEC-009: Safe string copy with explicit bounds checking
 *
 * Prevents buffer overflow by:
 * 1. Checking destination buffer size at compile time (when possible)
 * 2. Always null-terminating
 * 3. Using explicit length parameter
 */
inline void safe_string_copy(char* dest, const char* src, size_t dest_size) {
    if (dest == nullptr || src == nullptr || dest_size == 0) {
        return;
    }

    // SEC-009: Explicit bounds check - never write beyond dest_size-1
    size_t src_len = std::strlen(src);
    size_t copy_len = std::min(src_len, dest_size - 1);

    // SEC-009: Use memcpy for controlled copy (safer than strncpy)
    std::memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';  // Always null-terminate
}

/**
 * @brief SEC-009: Safe string copy from std::string
 */
inline void safe_string_copy(char* dest, const std::string& src, size_t dest_size) {
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    size_t copy_len = std::min(src.size(), dest_size - 1);
    std::memcpy(dest, src.c_str(), copy_len);
    dest[copy_len] = '\0';
}

/**
 * @brief SEC-009: Validate string fits in buffer
 */
inline bool validate_string_fits(const char* str, size_t buffer_size) {
    if (str == nullptr || buffer_size == 0) {
        return false;
    }
    return std::strlen(str) < buffer_size;
}

/**
 * @brief Detection result from vision pipeline
 *
 * Represents a detected target (calibration sheet or helicopter)
 * SEC-009: All fields validated before use
 */
struct DetectionData {
    uint32_t frame_id = 0;      ///< Frame sequence number
    uint64_t timestamp_ns = 0;  ///< Timestamp (CLOCK_MONOTONIC_RAW)

    // Bounding box (pixel coordinates)
    float x = 0.0f;       ///< Center X in pixels
    float y = 0.0f;       ///< Center Y in pixels
    float width = 0.0f;   ///< Width in pixels
    float height = 0.0f;  ///< Height in pixels

    // Detection confidence
    float confidence = 0.0f;  ///< 0.0 - 1.0

    // Target classification
    uint8_t target_class = 0;  ///< 0=unknown, 1=calibration, 2=helicopter

    // SEC-009: Validation method
    bool is_valid() const {
        // Check for NaN/Inf
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
            !std::isfinite(height) || !std::isfinite(confidence)) {
            return false;
        }

        // Bounds checks
        if (confidence < 0.0f || confidence > 1.0f) {
            return false;
        }

        return confidence > 0.5f && width > 0.0f && height > 0.0f;
    }
};

/**
 * @brief Tracked target state from CSRT tracker
 *
 * Per AM7-L2-VIS-008: CSRT tracker for continuous target tracking
 * SEC-009: All fields validated before use
 */
struct TrackData {
    uint32_t track_id = 0;      ///< Unique track identifier
    uint64_t timestamp_ns = 0;  ///< Timestamp (CLOCK_MONOTONIC_RAW)

    // 3D position estimate (meters)
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;  ///< Range (estimated from target size)

    // Velocity estimate (m/s)
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;

    // Track quality
    uint32_t hit_streak = 0;     ///< Consecutive successful updates
    uint32_t missed_frames = 0;  ///< Frames without detection
    float confidence = 0.0f;     ///< Track confidence 0.0 - 1.0

    // Bounding box (for visualization)
    float bbox_x = 0.0f;       ///< Top-left X in pixels
    float bbox_y = 0.0f;       ///< Top-left Y in pixels
    float bbox_width = 0.0f;   ///< Width in pixels
    float bbox_height = 0.0f;  ///< Height in pixels

    // SEC-009: Validation method
    bool is_valid() const {
        // Check for NaN/Inf in all float fields
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(vx) ||
            !std::isfinite(vy) || !std::isfinite(vz) || !std::isfinite(confidence) ||
            !std::isfinite(bbox_x) || !std::isfinite(bbox_y) || !std::isfinite(bbox_width) ||
            !std::isfinite(bbox_height)) {
            return false;
        }

        // Bounds checks
        if (confidence < 0.0f || confidence > 1.0f) {
            return false;
        }

        return confidence > 0.5f && hit_streak >= 2;
    }
};

/**
 * @brief Actuation command to gimbal
 *
 * Per AM7-L2-ACT-002: Elevation -10° to +45°, Azimuth ±90°
 * SEC-009: All fields validated before use
 */
struct ActuationData {
    uint32_t sequence = 0;      ///< Command sequence number
    uint64_t timestamp_ns = 0;  ///< Timestamp (CLOCK_MONOTONIC_RAW)

    // Commanded position (degrees)
    float azimuth_deg = 0.0f;    ///< -90° to +90°
    float elevation_deg = 0.0f;  ///< -10° to +45°

    // Command velocity (deg/s)
    float velocity_dps = 0.0f;  ///< Max 60°/s per AM7-L2-ACT-002

    // Execution status
    bool command_sent = false;
    bool limit_violation = false;

    // Latency tracking
    uint64_t compute_time_ns = 0;  ///< Time when command was computed
    uint64_t write_time_ns = 0;    ///< Time when I2C write completed

    // SEC-009: Validation method
    bool is_valid() const {
        // Check for NaN/Inf
        if (!std::isfinite(azimuth_deg) || !std::isfinite(elevation_deg) ||
            !std::isfinite(velocity_dps)) {
            return false;
        }

        // Bounds checks per AM7-L2-ACT-002
        if (azimuth_deg < -90.0f || azimuth_deg > 90.0f) {
            return false;
        }
        if (elevation_deg < -10.0f || elevation_deg > 45.0f) {
            return false;
        }
        if (velocity_dps < 0.0f || velocity_dps > 60.0f) {
            return false;
        }

        return true;
    }
};

/**
 * @brief System health metrics
 * SEC-009: All fields validated before use
 */
struct SystemHealthData {
    uint64_t timestamp_ns = 0;  ///< Timestamp

    // CPU metrics
    float cpu_temp_c = 0.0f;         ///< CPU temperature (°C)
    float cpu_usage_percent = 0.0f;  ///< CPU usage (0-100%)

    // Memory metrics
    uint32_t mem_used_mb = 0;   ///< Memory used (MB)
    uint32_t mem_total_mb = 0;  ///< Total memory (MB)

    // Frame rate
    float frame_rate = 0.0f;      ///< Actual FPS
    float jitter_percent = 0.0f;  ///< Jitter as % of frame period

    // SEC-009: Validation method
    bool is_valid() const {
        // Check for NaN/Inf
        if (!std::isfinite(cpu_temp_c) || !std::isfinite(cpu_usage_percent) ||
            !std::isfinite(frame_rate) || !std::isfinite(jitter_percent)) {
            return false;
        }

        // Bounds checks
        if (cpu_usage_percent < 0.0f || cpu_usage_percent > 100.0f) {
            return false;
        }
        if (jitter_percent < 0.0f || jitter_percent > 100.0f) {
            return false;
        }
        if (frame_rate < 0.0f || frame_rate > 1000.0f) {
            return false;
        }

        return true;
    }
};

/**
 * @brief Simplified CSV log entry (MVP version)
 *
 * Per spec.md Section 8.4 ICD-004, simplified for MVP
 * Full implementation would use ring buffer with HMAC
 *
 * SEC-009: Fixed-size buffers with explicit bounds
 */
struct CsvLogEntry {
    // Timestamps
    uint64_t produced_ts_epoch_ms = 0;  ///< Epoch timestamp (for log correlation)
    uint64_t call_ts_epoch_ms = 0;      ///< When log was written

    // Frame info
    uint32_t cam_frame_id = 0;

    // Detection data
    float det_x = 0.0f;
    float det_y = 0.0f;
    float det_width = 0.0f;
    float det_height = 0.0f;
    float det_confidence = 0.0f;
    uint8_t det_target_class = 0;

    // Track data
    uint32_t track_id = 0;
    float track_x = 0.0f;
    float track_y = 0.0f;
    float track_z = 0.0f;
    uint32_t track_hit_streak = 0;
    float track_confidence = 0.0f;

    // Actuation data
    float servo_azimuth = 0.0f;
    float servo_elevation = 0.0f;
    bool servo_command_sent = false;

    // System health
    float cpu_temp_c = 0.0f;
    float cpu_usage_percent = 0.0f;

    // SEC-009: Fixed-size buffers with explicit constants
    char module[kModule_name_max] = {};
    char event[kEvent_name_max] = {};

    // ICD-004: HMAC-SHA256 for entry integrity
    uint8_t hmac[32] = {};

    /**
     * @brief SEC-009: Safe string copy for module name
     */
    void set_module(const char* name) { safe_string_copy(module, name, kModule_name_max); }

    /**
     * @brief SEC-009: Safe string copy for event name
     */
    void set_event(const char* name) { safe_string_copy(event, name, kEvent_name_max); }

    /**
     * @brief SEC-009: Safe string copy from std::string for module
     */
    void set_module(const std::string& name) { safe_string_copy(module, name, kModule_name_max); }

    /**
     * @brief SEC-009: Safe string copy from std::string for event
     */
    void set_event(const std::string& name) { safe_string_copy(event, name, kEvent_name_max); }

    /**
     * @brief SEC-009: Validate all float fields are finite
     */
    bool is_valid() const {
        // Check all float fields for NaN/Inf
        if (!std::isfinite(det_x) || !std::isfinite(det_y) || !std::isfinite(det_width) ||
            !std::isfinite(det_height) || !std::isfinite(det_confidence) ||
            !std::isfinite(track_x) || !std::isfinite(track_y) || !std::isfinite(track_z) ||
            !std::isfinite(track_confidence) || !std::isfinite(servo_azimuth) ||
            !std::isfinite(servo_elevation) || !std::isfinite(cpu_temp_c) ||
            !std::isfinite(cpu_usage_percent)) {
            return false;
        }

        // Bounds checks
        if (det_confidence < 0.0f || det_confidence > 1.0f) {
            return false;
        }
        if (track_confidence < 0.0f || track_confidence > 1.0f) {
            return false;
        }
        if (cpu_usage_percent < 0.0f || cpu_usage_percent > 100.0f) {
            return false;
        }

        return true;
    }
};

// SEC-009: Compile-time size checks
static_assert(sizeof(CsvLogEntry::module) == kModule_name_max, "module buffer size mismatch");
static_assert(sizeof(CsvLogEntry::event) == kEvent_name_max, "event buffer size mismatch");

}  // namespace aurore
