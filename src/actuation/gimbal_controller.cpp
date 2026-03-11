#include "aurore/gimbal_controller.hpp"
#include "aurore/timing.hpp"

#include <utility>

namespace aurore {

GimbalController::GimbalController(const CameraIntrinsics& cam) : cam_(cam) {}

std::pair<float, float> GimbalController::apply_rate_limit(float az_desired, float el_desired) {
    const uint64_t now    = get_timestamp();
    const uint64_t prev_t = prev_cmd_ns_.load(std::memory_order_relaxed);

    // dt_s: use nominal frame period for the very first call, else clamp real elapsed time
    const float dt_s = (prev_t == 0)
        ? (1.0f / 120.0f)
        : std::clamp(static_cast<float>(now - prev_t) * 1e-9f, 0.001f, 0.1f);

    const float prev_az = prev_az_cmd_.load(std::memory_order_relaxed);
    const float prev_el = prev_el_cmd_.load(std::memory_order_relaxed);

    // --- velocity clamping ---
    const float max_vel_delta = kGimbalRateLimitDefault * dt_s;
    const float az_vel_limited = prev_az + std::clamp(az_desired - prev_az, -max_vel_delta, max_vel_delta);
    const float el_vel_limited = prev_el + std::clamp(el_desired - prev_el, -max_vel_delta, max_vel_delta);

    // --- acceleration clamping ---
    // Compute velocity implied by the velocity-limited position change
    const float az_vel = (az_vel_limited - prev_az) / dt_s;
    const float el_vel = (el_vel_limited - prev_el) / dt_s;

    const float prev_az_v = prev_az_vel_.load(std::memory_order_relaxed);
    const float prev_el_v = prev_el_vel_.load(std::memory_order_relaxed);

    const float max_accel_delta = kGimbalAccelLimitDefault * dt_s;
    const float az_vel_clamped  = prev_az_v + std::clamp(az_vel - prev_az_v, -max_accel_delta, max_accel_delta);
    const float el_vel_clamped  = prev_el_v + std::clamp(el_vel - prev_el_v, -max_accel_delta, max_accel_delta);

    // Recompute final angles from the acceleration-clamped velocity
    const float az_final = prev_az + az_vel_clamped * dt_s;
    const float el_final = prev_el + el_vel_clamped * dt_s;

    // Persist state for next call
    prev_az_cmd_.store(az_final, std::memory_order_relaxed);
    prev_el_cmd_.store(el_final, std::memory_order_relaxed);
    prev_az_vel_.store(az_vel_clamped, std::memory_order_relaxed);
    prev_el_vel_.store(el_vel_clamped, std::memory_order_relaxed);
    prev_cmd_ns_.store(now, std::memory_order_relaxed);

    return {az_final, el_final};
}

GimbalCommand GimbalController::command_from_pixel(float centroid_x, float centroid_y, float gain) {
    // Compute pixel offset from image center
    const float dx = centroid_x - cam_.cx;
    const float dy = centroid_y - cam_.cy;  // positive dy = target below center

    // Convert pixel offset to angle using pinhole camera model
    // delta_theta = atan2(offset_px, focal_length_px) * (180 / PI)
    // Note: Pixel Y increases downward, but elevation increases upward
    // Therefore we negate dy to get correct elevation direction
    const float delta_az =
        std::atan2(dx, cam_.focal_length_px) * (180.0f / static_cast<float>(M_PI));
    const float delta_el =
        std::atan2(-dy, cam_.focal_length_px) * (180.0f / static_cast<float>(M_PI));

    // AUTO mode: accumulate delta onto current angle, then apply rate limiting
    const float desired_az = az_.load(std::memory_order_relaxed) + delta_az * gain;
    const float desired_el = el_.load(std::memory_order_relaxed) + delta_el * gain;

    // Apply velocity + acceleration rate limits (AM7-L2-ACT-002)
    auto [limited_az, limited_el] = apply_rate_limit(desired_az, desired_el);

    // Clamp to configured limits
    const float new_az = std::clamp(limited_az, az_min_, az_max_);
    const float new_el = std::clamp(limited_el, el_min_, el_max_);

    // Store updated angles
    az_.store(new_az, std::memory_order_relaxed);
    el_.store(new_el, std::memory_order_relaxed);

    return GimbalCommand{new_az, new_el, std::nullopt};  // No sequence number for AUTO mode
}

GimbalCommand GimbalController::command_absolute(float az_deg, float el_deg, std::optional<uint32_t> seq_num) {
    // FREECAM mode: apply rate limits before setting absolute angles
    auto [limited_az, limited_el] = apply_rate_limit(az_deg, el_deg);

    // Clamp to configured limits
    const float clamped_az = std::clamp(limited_az, az_min_, az_max_);
    const float clamped_el = std::clamp(limited_el, el_min_, el_max_);

    // Store angles
    az_.store(clamped_az, std::memory_order_relaxed);
    el_.store(clamped_el, std::memory_order_relaxed);

    return GimbalCommand{clamped_az, clamped_el, seq_num};
}

std::optional<GimbalCommand> GimbalController::process_command_with_gap_check(float az_deg, float el_deg, uint32_t seq_num) {
    // Check for sequence gap if we have a previous sequence number
    if (last_sequence_num_.has_value()) {
        if (security::is_sequence_gap(last_sequence_num_.value(), seq_num, kGimbalSequenceGapThreshold)) {
            // Sequence gap detected - reject command and flag fault
            sequence_gap_detected_.store(true, std::memory_order_release);
            return std::nullopt;  // Command rejected
        }
    }

    // Update last sequence number
    last_sequence_num_ = seq_num;

    // Process command with rate limits then positional clamp
    auto [limited_az, limited_el] = apply_rate_limit(az_deg, el_deg);

    const float clamped_az = std::clamp(limited_az, az_min_, az_max_);
    const float clamped_el = std::clamp(limited_el, el_min_, el_max_);

    // Store angles
    az_.store(clamped_az, std::memory_order_relaxed);
    el_.store(clamped_el, std::memory_order_relaxed);

    return GimbalCommand{clamped_az, clamped_el, seq_num};
}

void GimbalController::set_limits(float az_min, float az_max, float el_min, float el_max) {
    az_min_ = az_min;
    az_max_ = az_max;
    el_min_ = el_min;
    el_max_ = el_max;
}

}  // namespace aurore
