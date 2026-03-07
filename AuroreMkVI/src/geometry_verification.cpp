#include "geometry_verification.h"
#include <sstream>
#include <iomanip>

namespace aurore {
namespace geometry {

GeometryVerifier::GeometryVerifier()
    : sight_height_m_(SIGHT_HEIGHT_DEFAULT_M),
      x_alignment_tolerance_(X_ALIGNMENT_TOLERANCE_DEFAULT),
      sight_over_bore_tolerance_(SIGHT_OVER_BORE_TOLERANCE_DEFAULT) {}

GeometryVerifier::GeometryVerifier(const FOVBounds& fov)
    : fov_(fov), sight_height_m_(SIGHT_HEIGHT_DEFAULT_M),
      x_alignment_tolerance_(X_ALIGNMENT_TOLERANCE_DEFAULT),
      sight_over_bore_tolerance_(SIGHT_OVER_BORE_TOLERANCE_DEFAULT) {}

GeometryVerifier::~GeometryVerifier() {}

void GeometryVerifier::set_fov_bounds(const FOVBounds& fov) {
    fov_ = fov;
}

void GeometryVerifier::set_sight_height_m(float height_m) {
    if (height_m > 0.0f && std::isfinite(height_m)) {
        sight_height_m_ = height_m;
    }
}

void GeometryVerifier::set_x_alignment_tolerance(float tolerance) {
    if (tolerance >= 0.0f) {
        x_alignment_tolerance_ = tolerance;
    }
}

void GeometryVerifier::set_sight_over_bore_tolerance(float tolerance) {
    if (tolerance >= 0.0f) {
        sight_over_bore_tolerance_ = tolerance;
    }
}

bool GeometryVerifier::is_within_fov(float x, float y) const {
    bool x_valid = x >= 0.0f && x <= fov_.width_pixels;
    bool y_valid = y >= 0.0f && y <= fov_.height_pixels;
    return x_valid && y_valid;
}

GeometryCheckResult GeometryVerifier::check_coordinate(float x, float y,
                                                        uint64_t timestamp_ms,
                                                        const std::string& source) const {
    GeometryCheckResult result;
    result.x = x;
    result.y = y;
    result.timestamp_ms = timestamp_ms;
    result.source_location = source;

    if (!std::isfinite(x) || !std::isfinite(y)) {
        result.is_valid = false;
        result.error_code = GeometryErrorCode::INVALID_FOCAL_LENGTH;
        result.error_message = "CRITICAL_GEOMETRY_ERROR: Non-finite coordinates [" +
                               std::to_string(x) + ", " + std::to_string(y) + "]";
        record_error(result.error_code);
        return result;
    }

    if (x < 0.0f) {
        result.is_valid = false;
        result.error_code = GeometryErrorCode::NEGATIVE_COORDINATE_X;
        result.error_message = "CRITICAL_GEOMETRY_ERROR: X coordinate " + std::to_string(x) +
                               " < 0 (outside image dimensions)";
        record_error(result.error_code);
        return result;
    }

    if (y < 0.0f) {
        result.is_valid = false;
        result.error_code = GeometryErrorCode::NEGATIVE_COORDINATE_Y;
        result.error_message = "CRITICAL_GEOMETRY_ERROR: Y coordinate " + std::to_string(y) +
                               " < 0 (outside image dimensions)";
        record_error(result.error_code);
        return result;
    }

    if (x > fov_.width_pixels) {
        result.is_valid = false;
        result.error_code = GeometryErrorCode::EXCEEDS_WIDTH;
        result.error_message = "CRITICAL_GEOMETRY_ERROR: X coordinate " + std::to_string(x) +
                               " > width " + std::to_string(fov_.width_pixels);
        record_error(result.error_code);
        return result;
    }

    if (y > fov_.height_pixels) {
        result.is_valid = false;
        result.error_code = GeometryErrorCode::EXCEEDS_HEIGHT;
        result.error_message = "CRITICAL_GEOMETRY_ERROR: Y coordinate " + std::to_string(y) +
                               " > height " + std::to_string(fov_.height_pixels);
        record_error(result.error_code);
        return result;
    }

    result.is_valid = true;
    result.error_code = GeometryErrorCode::NONE;
    result.error_message = "WITHIN_FOV";
    return result;
}

GeometryCheckResult GeometryVerifier::check_orange_zone(float oz_x, float oz_y,
                                                         float bbox_center_x, float bbox_center_y,
                                                         float distance_m,
                                                         float target_velocity_x,
                                                         uint64_t timestamp_ms,
                                                         const std::string& source) const {
    GeometryCheckResult result = check_coordinate(oz_x, oz_y, timestamp_ms, source);

    if (!result.is_valid) {
        return result;
    }

    if (!std::isfinite(distance_m) || distance_m <= 0.0f) {
        result.is_valid = false;
        result.error_code = GeometryErrorCode::INVALID_DISTANCE;
        result.error_message = "CRITICAL_GEOMETRY_ERROR: Invalid distance " +
                               std::to_string(distance_m) + "m for sight-over-bore calculation";
        record_error(result.error_code);
        return result;
    }

    if (!verify_sight_over_bore_y_offset(bbox_center_y, oz_y, distance_m)) {
        result.is_valid = false;
        result.error_code = GeometryErrorCode::SIGHT_OVER_BORE_VIOLATION;
        result.error_message = "CRITICAL_GEOMETRY_ERROR: Sight-over-bore constraint violated";
        record_error(result.error_code);
        return result;
    }

    if (!verify_x_alignment(bbox_center_x, oz_x, target_velocity_x)) {
        result.is_valid = false;
        result.error_code = GeometryErrorCode::X_ALIGNMENT_VIOLATION;
        result.error_message = "CRITICAL_GEOMETRY_ERROR: X alignment constraint violated";
        record_error(result.error_code);
        return result;
    }

    result.distance_m = distance_m;
    return result;
}

float GeometryVerifier::calculate_sight_over_bore_offset_pixels(float distance_m) const {
    if (distance_m <= 0.0f || !std::isfinite(distance_m)) {
        return 0.0f;
    }

    if (fov_.focal_length_px <= 0.0f || !std::isfinite(fov_.focal_length_px)) {
        return 0.0f;
    }

    float offset_pixels = (sight_height_m_ / distance_m) * fov_.focal_length_px;

    if (!std::isfinite(offset_pixels)) {
        return 0.0f;
    }

    return offset_pixels;
}

bool GeometryVerifier::verify_sight_over_bore_y_offset(float bbox_center_y, float oz_y,
                                                        float distance_m, float tolerance_px) const {
    float expected_offset = calculate_sight_over_bore_offset_pixels(distance_m);

    if (tolerance_px < 0.0f) {
        tolerance_px = sight_over_bore_tolerance_;
    }

    float actual_offset = bbox_center_y - oz_y;
    float deviation = std::abs(actual_offset - expected_offset);

    if (deviation > tolerance_px) {
        record_error(GeometryErrorCode::SIGHT_OVER_BORE_VIOLATION);
        return false;
    }

    return true;
}

bool GeometryVerifier::verify_x_alignment(float bbox_center_x, float oz_x,
                                           float target_velocity_x, float tolerance_px) const {
    if (tolerance_px < 0.0f) {
        tolerance_px = x_alignment_tolerance_;
    }

    if (std::abs(target_velocity_x) < 0.1f) {
        float deviation = std::abs(bbox_center_x - oz_x);
        if (deviation > tolerance_px) {
            record_error(GeometryErrorCode::X_ALIGNMENT_VIOLATION);
            return false;
        }
    }

    return true;
}

std::string GeometryVerifier::get_fov_description() const {
    std::ostringstream oss;
    oss << "FOV: " << std::fixed << std::setprecision(1)
        << fov_.horizontal_fov_deg << "°H x " << fov_.vertical_fov_deg << "°V"
        << " (" << static_cast<int>(fov_.width_pixels) << "x" 
        << static_cast<int>(fov_.height_pixels) << "px, "
        << "focal=" << fov_.focal_length_px << "px)";
    return oss.str();
}

std::string GeometryVerifier::error_code_to_string(GeometryErrorCode code) {
    switch (code) {
        case GeometryErrorCode::NONE: return "NONE";
        case GeometryErrorCode::OUTSIDE_FOV_X: return "OUTSIDE_FOV_X";
        case GeometryErrorCode::OUTSIDE_FOV_Y: return "OUTSIDE_FOV_Y";
        case GeometryErrorCode::NEGATIVE_COORDINATE_X: return "NEGATIVE_COORDINATE_X";
        case GeometryErrorCode::NEGATIVE_COORDINATE_Y: return "NEGATIVE_COORDINATE_Y";
        case GeometryErrorCode::EXCEEDS_WIDTH: return "EXCEEDS_WIDTH";
        case GeometryErrorCode::EXCEEDS_HEIGHT: return "EXCEEDS_HEIGHT";
        case GeometryErrorCode::INVALID_DISTANCE: return "INVALID_DISTANCE";
        case GeometryErrorCode::SIGHT_OVER_BORE_VIOLATION: return "SIGHT_OVER_BORE_VIOLATION";
        case GeometryErrorCode::X_ALIGNMENT_VIOLATION: return "X_ALIGNMENT_VIOLATION";
        case GeometryErrorCode::INVALID_FOCAL_LENGTH: return "INVALID_FOCAL_LENGTH";
        default: return "UNKNOWN";
    }
}

void GeometryVerifier::record_error(GeometryErrorCode code) const {
    total_errors_.fetch_add(1, std::memory_order_relaxed);

    switch (code) {
        case GeometryErrorCode::NEGATIVE_COORDINATE_X:
        case GeometryErrorCode::NEGATIVE_COORDINATE_Y:
        case GeometryErrorCode::EXCEEDS_WIDTH:
        case GeometryErrorCode::EXCEEDS_HEIGHT:
            fov_errors_.fetch_add(1, std::memory_order_relaxed);
            break;
        case GeometryErrorCode::SIGHT_OVER_BORE_VIOLATION:
            sob_errors_.fetch_add(1, std::memory_order_relaxed);
            break;
        case GeometryErrorCode::X_ALIGNMENT_VIOLATION:
            x_align_errors_.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

GeometryCheckResult GeometryVerifier::create_error_result(GeometryErrorCode code, float x, float y,
                                                           uint64_t timestamp_ms,
                                                           const std::string& source) const {
    GeometryCheckResult result;
    result.is_valid = false;
    result.error_code = code;
    result.x = x;
    result.y = y;
    result.timestamp_ms = timestamp_ms;
    result.source_location = source;
    result.error_message = "CRITICAL_GEOMETRY_ERROR: " + error_code_to_string(code);
    return result;
}

void GeometryVerifier::reset_counters() {
    total_errors_.store(0, std::memory_order_release);
    fov_errors_.store(0, std::memory_order_release);
    sob_errors_.store(0, std::memory_order_release);
    x_align_errors_.store(0, std::memory_order_release);
}

}
}
