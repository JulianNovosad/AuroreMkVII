#pragma once
#include <atomic>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <optional>

#include "aurore/security.hpp"

namespace aurore {

// Motion constraints - placed in .rodata (read-only data segment)
// Per AM7-L2-ACT-002: Elevation -10° to +45°, Azimuth ±90°
constexpr float kGimbalAzMinDefault = -90.0f;
constexpr float kGimbalAzMaxDefault = 90.0f;
constexpr float kGimbalElMinDefault = -10.0f;
constexpr float kGimbalElMaxDefault = 45.0f;

// Rate limit default (degrees per second) - per AM7-L2-ACT-002
constexpr float kGimbalRateLimitDefault = 60.0f;
// Acceleration limit default (degrees per second squared) - per AM7-L2-ACT-002
static constexpr float kGimbalAccelLimitDefault = 120.0f;

// Sequence gap threshold per AM7-L3-SEC-004
constexpr uint32_t kGimbalSequenceGapThreshold = 1000;

enum class GimbalSource { AUTO, FREECAM };

struct GimbalCommand {
    float az_deg{0.f};
    float el_deg{0.f};
    std::optional<uint32_t> sequence_num;  // Optional sequence number for gap detection
};

struct CameraIntrinsics {
    float focal_length_px{1128.f};  // (4.74mm * 1536px) / 6.45mm for RPi Cam3
    float cx{768.f};                // image center X (half of 1536)
    float cy{432.f};                // image center Y (half of 864)
};

// Converts pixel offset → servo angle, accepts commands from AUTO or FREECAM source.
class GimbalController {
public:
    explicit GimbalController(const CameraIntrinsics& cam = {});

    // AUTO mode: compute delta angle from track centroid, apply to current angle
    GimbalCommand command_from_pixel(float centroid_x, float centroid_y, float gain = 1.0f);

    // FREECAM mode: direct absolute angle command from Link
    GimbalCommand command_absolute(float az_deg, float el_deg, std::optional<uint32_t> seq_num = std::nullopt);

    // Process command with sequence gap detection (returns std::nullopt if gap detected)
    std::optional<GimbalCommand> process_command_with_gap_check(float az_deg, float el_deg, uint32_t seq_num);

    void set_source(GimbalSource s) { source_.store(s, std::memory_order_release); }
    GimbalSource source() const     { return source_.load(std::memory_order_acquire); }

    // Get last commanded angles (from either source)
    float current_az() const { return az_.load(std::memory_order_relaxed); }
    float current_el() const { return el_.load(std::memory_order_relaxed); }

    // Clamp limits (set from config)
    void set_limits(float az_min, float az_max, float el_min, float el_max);

    // Get last sequence number (for monitoring)
    std::optional<uint32_t> last_sequence() const { return last_sequence_num_; }

    // Check if sequence gap was detected
    bool has_sequence_gap() const { return sequence_gap_detected_.load(std::memory_order_acquire); }

    // Reset sequence gap flag
    void reset_sequence_gap() { sequence_gap_detected_.store(false, std::memory_order_release); }

    // Reset rate limiter state (call on mode transitions or after homing)
    void reset_rate_limiter() {
        prev_az_cmd_.store(az_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        prev_el_cmd_.store(el_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        prev_az_vel_.store(0.0f, std::memory_order_relaxed);
        prev_el_vel_.store(0.0f, std::memory_order_relaxed);
        prev_cmd_ns_.store(0, std::memory_order_relaxed);
    }

private:
    CameraIntrinsics cam_;
    std::atomic<GimbalSource> source_{GimbalSource::AUTO};
    std::atomic<float> az_{0.f};
    std::atomic<float> el_{0.f};
    // Limits are configurable at runtime but default to .rodata constants
    float az_min_{kGimbalAzMinDefault};
    float az_max_{kGimbalAzMaxDefault};
    float el_min_{kGimbalElMinDefault};
    float el_max_{kGimbalElMaxDefault};

    // Sequence number tracking for gap detection (AM7-L3-SEC-004)
    std::optional<uint32_t> last_sequence_num_;
    std::atomic<bool> sequence_gap_detected_{false};

    // Rate / acceleration limiter state (AM7-L2-ACT-002)
    // prev_az_cmd_ / prev_el_cmd_ hold the last rate-limited output angle
    // prev_az_vel_ / prev_el_vel_ hold the last velocity (°/s) for accel clamping
    // prev_cmd_ns_ holds the timestamp of the last command (0 = not yet set)
    std::atomic<float>    prev_az_cmd_{0.0f};
    std::atomic<float>    prev_el_cmd_{0.0f};
    std::atomic<float>    prev_az_vel_{0.0f};
    std::atomic<float>    prev_el_vel_{0.0f};
    std::atomic<uint64_t> prev_cmd_ns_{0};

    // Apply velocity + acceleration clamping to a desired (az, el) pair.
    // Updates all limiter state and returns the clamped (az, el).
    std::pair<float, float> apply_rate_limit(float az_desired, float el_desired);};

}  // namespace aurore
