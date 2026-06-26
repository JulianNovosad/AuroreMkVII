/**
 * @file spatial_resolver.cpp
 * @brief 3D Spatial Offset Resolver — parallax compensation for LRF/camera offsets
 *
 * Converts pixel-space detections + LRF range into barrel-relative 3D target vectors,
 * compensating for the physical XYZ offset between sensors and barrel tip.
 *
 * Coordinate system (barrel-centric, right-hand):
 *   +X = right, +Y = up, +Z = forward (downrange)
 *   Origin = barrel tip (muzzle)
 *
 * No mocks. All geometry is real-world calibrated.
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include "aurore/spatial_resolver.hpp"

#include <cmath>
#include <iostream>

namespace aurore {

SpatialResolver::SpatialResolver(const SpatialResolverConfig& config) : config_(config) {}

bool SpatialResolver::init() {
    // Validate configuration ranges
    if (config_.mipi_focal_length_mm <= 0.0f || config_.usb_focal_length_mm <= 0.0f) {
        std::cerr << "FAIL: SpatialResolver invalid focal length.\n"
                  << " Check: Camera intrinsic calibration values.\n"
                  << " Fix: Set positive focal_length_mm for both MIPI and USB cameras.\n";
        return false;
    }

    if (config_.max_range_m <= config_.min_range_m) {
        std::cerr << "FAIL: SpatialResolver invalid range bounds.\n"
                  << " Check: max_range_m > min_range_m.\n"
                  << " Fix: Verify SpatialResolverConfig range limits.\n";
        return false;
    }

    // Copy initial offsets as calibrated starting point
    calibrated_mipi_offset_ = config_.mipi_offset;
    calibrated_usb_offset_ = config_.usb_offset;

    initialized_.store(true, std::memory_order_release);
    std::cout << "[SpatialResolver] Initialized — MIPI offset (" << config_.mipi_offset.x << ", "
              << config_.mipi_offset.y << ", " << config_.mipi_offset.z << ") mm, USB offset ("
              << config_.usb_offset.x << ", " << config_.usb_offset.y << ", "
              << config_.usb_offset.z << ") mm\n";
    return true;
}

// ============================================================================
// Offset accessors
// ============================================================================

void SpatialResolver::set_mipi_offset(const SensorOffset& offset) {
    calibrated_mipi_offset_ = offset;
}

void SpatialResolver::set_usb_offset(const SensorOffset& offset) {
    calibrated_usb_offset_ = offset;
}

void SpatialResolver::set_barrel_offset(const SensorOffset& offset) {
    config_.barrel_offset = offset;
}

SensorOffset SpatialResolver::get_mipi_offset() const { return calibrated_mipi_offset_; }
SensorOffset SpatialResolver::get_usb_offset() const { return calibrated_usb_offset_; }
SensorOffset SpatialResolver::get_barrel_offset() const { return config_.barrel_offset; }

// ============================================================================
// Core resolution: pixel + LRF range → barrel-relative 3D target vector
// ============================================================================

TargetVector SpatialResolver::resolve_from_mipi(float range_mm, const PixelCoord& mipi_px) const {
    TargetVector result{};
    if (!initialized_.load(std::memory_order_acquire)) return result;

    const float range_m = range_mm * kMmToM;
    if (range_m < config_.min_range_m || range_m > config_.max_range_m) return result;

    // Convert pixel offset from frame center to angular offset
    // FOV-based: angle = atan((pixel_offset / frame_dim) * sensor_size / focal_length)
    const float cx = static_cast<float>(mipi_px.width) / 2.0f;
    const float cy = static_cast<float>(mipi_px.height) / 2.0f;
    const float dx_px = mipi_px.u - cx;
    const float dy_px = mipi_px.v - cy;

    // Pixels to mm on sensor plane
    const float px_to_mm_x = config_.mipi_sensor_width_mm / static_cast<float>(mipi_px.width);
    const float px_to_mm_y = config_.mipi_sensor_height_mm / static_cast<float>(mipi_px.height);

    // Angular offset from optical axis (radians)
    const float az_rad = std::atan2(dx_px * px_to_mm_x, config_.mipi_focal_length_mm);
    const float el_rad = std::atan2(-dy_px * px_to_mm_y, config_.mipi_focal_length_mm);

    // Target position in camera frame (Z = forward)
    const float cam_x = range_m * std::tan(az_rad);
    const float cam_y = range_m * std::tan(el_rad);
    const float cam_z = range_m;

    // Transform from camera frame to barrel frame by subtracting sensor offset
    // Sensor offset is from barrel tip to sensor, so barrel-relative target =
    // camera-relative target + sensor offset (sensor sees target from its own position)
    const SensorOffset& off = calibrated_mipi_offset_;
    result.x = cam_x + off.x * kMmToM;
    result.y = cam_y + off.y * kMmToM;
    result.z = cam_z + off.z * kMmToM;

    // Apply angular offsets from boresight calibration
    if (std::abs(off.pitch_offset) > 0.001f || std::abs(off.yaw_offset) > 0.001f) {
        const float yaw_rad = off.yaw_offset * kDegToRad;
        const float pitch_rad = off.pitch_offset * kDegToRad;

        // Rotate around Y (yaw), then X (pitch)
        const float z_rot = result.z * std::cos(yaw_rad) - result.x * std::sin(yaw_rad);
        const float x_rot = result.z * std::sin(yaw_rad) + result.x * std::cos(yaw_rad);
        result.x = x_rot;

        const float z_rot2 = z_rot * std::cos(pitch_rad) + result.y * std::sin(pitch_rad);
        const float y_rot = -z_rot * std::sin(pitch_rad) + result.y * std::cos(pitch_rad);
        result.y = y_rot;
        result.z = z_rot2;
    }

    result.range_m = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z);
    result.az_deg = std::atan2(result.x, result.z) * kRadToDeg;
    result.el_deg = std::atan2(result.y, result.z) * kRadToDeg;
    result.valid = std::isfinite(result.range_m) && result.range_m > 0.0f;
    result.timestamp_ns = get_timestamp(ClockId::MonotonicRaw);

    return result;
}

TargetVector SpatialResolver::resolve_from_usb(float range_mm, const PixelCoord& usb_px) const {
    TargetVector result{};
    if (!initialized_.load(std::memory_order_acquire)) return result;

    const float range_m = range_mm * kMmToM;
    if (range_m < config_.min_range_m || range_m > config_.max_range_m) return result;

    const float cx = static_cast<float>(usb_px.width) / 2.0f;
    const float cy = static_cast<float>(usb_px.height) / 2.0f;
    const float dx_px = usb_px.u - cx;
    const float dy_px = usb_px.v - cy;

    const float px_to_mm_x = config_.usb_sensor_width_mm / static_cast<float>(usb_px.width);
    const float px_to_mm_y = config_.usb_sensor_height_mm / static_cast<float>(usb_px.height);

    const float az_rad = std::atan2(dx_px * px_to_mm_x, config_.usb_focal_length_mm);
    const float el_rad = std::atan2(-dy_px * px_to_mm_y, config_.usb_focal_length_mm);

    const float cam_x = range_m * std::tan(az_rad);
    const float cam_y = range_m * std::tan(el_rad);
    const float cam_z = range_m;

    const SensorOffset& off = calibrated_usb_offset_;
    result.x = cam_x + off.x * kMmToM;
    result.y = cam_y + off.y * kMmToM;
    result.z = cam_z + off.z * kMmToM;

    if (std::abs(off.pitch_offset) > 0.001f || std::abs(off.yaw_offset) > 0.001f) {
        const float yaw_rad = off.yaw_offset * kDegToRad;
        const float pitch_rad = off.pitch_offset * kDegToRad;

        const float z_rot = result.z * std::cos(yaw_rad) - result.x * std::sin(yaw_rad);
        const float x_rot = result.z * std::sin(yaw_rad) + result.x * std::cos(yaw_rad);
        result.x = x_rot;

        const float z_rot2 = z_rot * std::cos(pitch_rad) + result.y * std::sin(pitch_rad);
        const float y_rot = -z_rot * std::sin(pitch_rad) + result.y * std::cos(pitch_rad);
        result.y = y_rot;
        result.z = z_rot2;
    }

    result.range_m = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z);
    result.az_deg = std::atan2(result.x, result.z) * kRadToDeg;
    result.el_deg = std::atan2(result.y, result.z) * kRadToDeg;
    result.valid = std::isfinite(result.range_m) && result.range_m > 0.0f;
    result.timestamp_ns = get_timestamp(ClockId::MonotonicRaw);

    return result;
}

// ============================================================================
// Fused resolution: combine MIPI + USB for cross-validated target vector
// ============================================================================

TargetVector SpatialResolver::resolve_fused(float range_mm, const PixelCoord& mipi_px,
                                            const PixelCoord& usb_px) const {
    const TargetVector mipi_tv = resolve_from_mipi(range_mm, mipi_px);
    const TargetVector usb_tv = resolve_from_usb(range_mm, usb_px);

    if (!mipi_tv.valid && !usb_tv.valid) return TargetVector{};
    if (!usb_tv.valid) return mipi_tv;
    if (!mipi_tv.valid) return usb_tv;

    // Average the two solutions (MIPI is primary, weighted 60/40)
    TargetVector fused{};
    fused.x = mipi_tv.x * 0.6f + usb_tv.x * 0.4f;
    fused.y = mipi_tv.y * 0.6f + usb_tv.y * 0.4f;
    fused.z = mipi_tv.z * 0.6f + usb_tv.z * 0.4f;
    fused.range_m = std::sqrt(fused.x * fused.x + fused.y * fused.y + fused.z * fused.z);
    fused.az_deg = std::atan2(fused.x, fused.z) * kRadToDeg;
    fused.el_deg = std::atan2(fused.y, fused.z) * kRadToDeg;
    fused.valid = true;
    fused.timestamp_ns = get_timestamp(ClockId::MonotonicRaw);

    return fused;
}

// ============================================================================
// Convergence zone: where both cameras should see the same target at given range
// ============================================================================

ConvergenceZone SpatialResolver::calculate_convergence_zone(float range_mm) const {
    ConvergenceZone zone{};
    const float range_m = range_mm * kMmToM;
    if (range_m <= 0.0f) return zone;

    // Project barrel center (0,0,range) back into each camera's pixel space
    TargetVector barrel_target{};
    barrel_target.x = 0.0f;
    barrel_target.y = 0.0f;
    barrel_target.z = range_m;
    barrel_target.valid = true;

    const PixelCoord mipi_proj = project_to_mipi(barrel_target);
    const PixelCoord usb_proj = project_to_usb(barrel_target);

    zone.mipi_x = mipi_proj.u;
    zone.mipi_y = mipi_proj.v;
    zone.usb_x = usb_proj.u;
    zone.usb_y = usb_proj.v;

    // Tolerance shrinks with range (parallax decreases at distance)
    zone.tolerance_px = config_.convergence_tolerance_px / std::max(range_m, 0.5f);

    return zone;
}

// ============================================================================
// Parallax adjustment: how much the LRF range differs from barrel range
// ============================================================================

float SpatialResolver::calculate_parallax_adjustment(float range_mm,
                                                     const SensorOffset& sensor) const {
    // The LRF measures from its own position; the barrel fires from origin.
    // Parallax = sqrt((range + dz)^2 + dx^2 + dy^2) - range
    const float range_m = range_mm * kMmToM;
    const float dx = sensor.x * kMmToM;
    const float dy = sensor.y * kMmToM;
    const float dz = sensor.z * kMmToM;

    const float lrf_range = std::sqrt((range_m + dz) * (range_m + dz) + dx * dx + dy * dy);
    return (lrf_range - range_m) * 1000.0f;  // Return in mm
}

float SpatialResolver::lrf_to_barrel_range_m(float lrf_range_mm) const {
    if (lrf_range_mm <= 0.0f) return 0.0f;

    // The LRF measures from its own position. The barrel fires from origin.
    // Inverse parallax: barrel_range = sqrt(lrf_range^2 - dx^2 - dy^2) - dz
    const float dx = config_.lrf_offset.x * kMmToM;
    const float dy = config_.lrf_offset.y * kMmToM;
    const float dz = config_.lrf_offset.z * kMmToM;
    const float lrf_m = lrf_range_mm * kMmToM;

    const float lrf_sq = lrf_m * lrf_m;
    const float lateral_sq = dx * dx + dy * dy;

    // If LRF range is less than the lateral offset, geometry is degenerate
    if (lrf_sq <= lateral_sq) return lrf_m;  // Fallback to raw range

    const float barrel_range = std::sqrt(lrf_sq - lateral_sq) - dz;
    return barrel_range > 0.0f ? barrel_range : 0.0f;
}

// ============================================================================
// Back-projection: 3D target → pixel coordinates
// ============================================================================

PixelCoord SpatialResolver::project_to_mipi(const TargetVector& target) const {
    PixelCoord px{};
    if (!target.valid || target.z <= 0.0f) return px;

    // Transform from barrel frame to camera frame (subtract sensor offset)
    const float cam_x = target.x - calibrated_mipi_offset_.x * kMmToM;
    const float cam_y = target.y - calibrated_mipi_offset_.y * kMmToM;
    const float cam_z = target.z - calibrated_mipi_offset_.z * kMmToM;

    if (cam_z <= 0.0f) return px;

    // Project onto sensor plane
    const float sensor_x_mm = config_.mipi_focal_length_mm * cam_x / cam_z;
    const float sensor_y_mm = config_.mipi_focal_length_mm * cam_y / cam_z;

    // Assume 1536x864 if width/height not set (MIPI default)
    px.width = 1536;
    px.height = 864;

    const float px_per_mm_x = static_cast<float>(px.width) / config_.mipi_sensor_width_mm;
    const float px_per_mm_y = static_cast<float>(px.height) / config_.mipi_sensor_height_mm;

    px.u = static_cast<float>(px.width) / 2.0f + sensor_x_mm * px_per_mm_x;
    px.v = static_cast<float>(px.height) / 2.0f - sensor_y_mm * px_per_mm_y;

    return px;
}

PixelCoord SpatialResolver::project_to_usb(const TargetVector& target) const {
    PixelCoord px{};
    if (!target.valid || target.z <= 0.0f) return px;

    const float cam_x = target.x - calibrated_usb_offset_.x * kMmToM;
    const float cam_y = target.y - calibrated_usb_offset_.y * kMmToM;
    const float cam_z = target.z - calibrated_usb_offset_.z * kMmToM;

    if (cam_z <= 0.0f) return px;

    const float sensor_x_mm = config_.usb_focal_length_mm * cam_x / cam_z;
    const float sensor_y_mm = config_.usb_focal_length_mm * cam_y / cam_z;

    // USB webcam default resolution
    px.width = 640;
    px.height = 480;

    const float px_per_mm_x = static_cast<float>(px.width) / config_.usb_sensor_width_mm;
    const float px_per_mm_y = static_cast<float>(px.height) / config_.usb_sensor_height_mm;

    px.u = static_cast<float>(px.width) / 2.0f + sensor_x_mm * px_per_mm_x;
    px.v = static_cast<float>(px.height) / 2.0f - sensor_y_mm * px_per_mm_y;

    return px;
}

// ============================================================================
// Convergence zone validation
// ============================================================================

bool SpatialResolver::is_in_convergence_zone(const PixelCoord& mipi_px, const PixelCoord& usb_px,
                                             float range_mm) const {
    const ConvergenceZone zone = calculate_convergence_zone(range_mm);

    const float mipi_err = std::sqrt((mipi_px.u - zone.mipi_x) * (mipi_px.u - zone.mipi_x) +
                                     (mipi_px.v - zone.mipi_y) * (mipi_px.v - zone.mipi_y));
    const float usb_err = std::sqrt((usb_px.u - zone.usb_x) * (usb_px.u - zone.usb_x) +
                                    (usb_px.v - zone.usb_y) * (usb_px.v - zone.usb_y));

    return mipi_err <= zone.tolerance_px && usb_err <= zone.tolerance_px;
}

// ============================================================================
// Calibration with known target
// ============================================================================

void SpatialResolver::calibrate_with_known_target(float measured_range_mm,
                                                  const PixelCoord& mipi_px,
                                                  const PixelCoord& usb_px) {
    // Resolve current estimate from each camera
    const TargetVector mipi_tv = resolve_from_mipi(measured_range_mm, mipi_px);
    const TargetVector usb_tv = resolve_from_usb(measured_range_mm, usb_px);

    // The target should be at (0, 0, range) in barrel frame if perfectly boresighted.
    // Any X/Y deviation is the boresight error, which we store as angular correction.
    const float range_m = measured_range_mm * kMmToM;

    if (mipi_tv.valid && range_m > 0.0f) {
        calibrated_mipi_offset_.yaw_offset -= mipi_tv.az_deg;
        calibrated_mipi_offset_.pitch_offset -= mipi_tv.el_deg;
    }

    if (usb_tv.valid && range_m > 0.0f) {
        calibrated_usb_offset_.yaw_offset -= usb_tv.az_deg;
        calibrated_usb_offset_.pitch_offset -= usb_tv.el_deg;
    }

    std::cout << "[SpatialResolver] Calibrated at " << range_m << "m — MIPI correction ("
              << calibrated_mipi_offset_.yaw_offset << "°, " << calibrated_mipi_offset_.pitch_offset
              << "°), USB correction (" << calibrated_usb_offset_.yaw_offset << "°, "
              << calibrated_usb_offset_.pitch_offset << "°)\n";
}

}  // namespace aurore
