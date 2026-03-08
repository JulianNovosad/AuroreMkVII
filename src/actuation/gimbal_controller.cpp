#include "aurore/gimbal_controller.hpp"

namespace aurore {

GimbalController::GimbalController(const CameraIntrinsics& cam)
    : cam_(cam) {}

GimbalCommand GimbalController::command_from_pixel(
    float centroid_x, float centroid_y, float gain) {
    // Compute pixel offset from image center
    const float dx = centroid_x - cam_.cx;
    const float dy = centroid_y - cam_.cy;  // positive dy = target below center

    // Convert pixel offset to angle using pinhole camera model
    // delta_theta = atan2(offset_px, focal_length_px) * (180 / PI)
    // Note: Pixel Y increases downward, but elevation increases upward
    // Therefore we negate dy to get correct elevation direction
    const float delta_az = std::atan2(dx, cam_.focal_length_px) * (180.0f / static_cast<float>(M_PI));
    const float delta_el = std::atan2(-dy, cam_.focal_length_px) * (180.0f / static_cast<float>(M_PI));

    // AUTO mode: accumulate delta onto current angle
    float new_az = az_.load(std::memory_order_relaxed) + delta_az * gain;
    float new_el = el_.load(std::memory_order_relaxed) + delta_el * gain;

    // Clamp to configured limits
    new_az = std::clamp(new_az, az_min_, az_max_);
    new_el = std::clamp(new_el, el_min_, el_max_);

    // Store updated angles
    az_.store(new_az, std::memory_order_relaxed);
    el_.store(new_el, std::memory_order_relaxed);

    return GimbalCommand{new_az, new_el};
}

GimbalCommand GimbalController::command_absolute(float az_deg, float el_deg) {
    // FREECAM mode: set absolute angles directly
    float clamped_az = std::clamp(az_deg, az_min_, az_max_);
    float clamped_el = std::clamp(el_deg, el_min_, el_max_);

    // Store absolute angles
    az_.store(clamped_az, std::memory_order_relaxed);
    el_.store(clamped_el, std::memory_order_relaxed);

    return GimbalCommand{clamped_az, clamped_el};
}

void GimbalController::set_limits(float az_min, float az_max, float el_min, float el_max) {
    az_min_ = az_min;
    az_max_ = az_max;
    el_min_ = el_min;
    el_max_ = el_max;
}

}  // namespace aurore
