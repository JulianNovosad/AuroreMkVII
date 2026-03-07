#ifndef GEOMETRY_VERIFICATION_H
#define GEOMETRY_VERIFICATION_H

#include <string>
#include <cmath>
#include <cstdint>
#include <atomic>

namespace aurore {
namespace geometry {

constexpr float SIGHT_HEIGHT_DEFAULT_M = 0.08f;
constexpr float X_ALIGNMENT_TOLERANCE_DEFAULT = 1.0f;
constexpr float SIGHT_OVER_BORE_TOLERANCE_DEFAULT = 3.0f;

enum class GeometryErrorCode {
    NONE = 0,
    OUTSIDE_FOV_X,
    OUTSIDE_FOV_Y,
    NEGATIVE_COORDINATE_X,
    NEGATIVE_COORDINATE_Y,
    EXCEEDS_WIDTH,
    EXCEEDS_HEIGHT,
    INVALID_DISTANCE,
    SIGHT_OVER_BORE_VIOLATION,
    X_ALIGNMENT_VIOLATION,
    INVALID_FOCAL_LENGTH
};

struct FOVBounds {
    float width_pixels;
    float height_pixels;
    float horizontal_fov_deg;
    float vertical_fov_deg;
    float focal_length_px;
    float sensor_width_mm;
    float sensor_height_mm;
    float focal_length_mm;

    FOVBounds()
        : width_pixels(0), height_pixels(0), horizontal_fov_deg(0), vertical_fov_deg(0),
          focal_length_px(0), sensor_width_mm(0), sensor_height_mm(0), focal_length_mm(0) {}

    FOVBounds(float w, float h, float fl_mm, float sw_mm, float sh_mm)
        : width_pixels(w), height_pixels(h) {
        focal_length_mm = fl_mm;
        sensor_width_mm = sw_mm;
        sensor_height_mm = sh_mm;

        focal_length_px = (w / sensor_width_mm) * focal_length_mm;

        horizontal_fov_deg = 2.0f * std::atan2(sensor_width_mm, 2.0f * focal_length_mm) * (180.0f / M_PI);
        vertical_fov_deg = 2.0f * std::atan2(sensor_height_mm, 2.0f * focal_length_mm) * (180.0f / M_PI);
    }
};

struct GeometryCheckResult {
    bool is_valid;
    GeometryErrorCode error_code;
    std::string error_message;
    float x;
    float y;
    float distance_m;
    uint64_t timestamp_ms;
    std::string source_location;

    GeometryCheckResult()
        : is_valid(true), error_code(GeometryErrorCode::NONE), x(0), y(0),
          distance_m(0), timestamp_ms(0) {}
};

class GeometryVerifier {
public:
    GeometryVerifier();
    GeometryVerifier(const FOVBounds& fov);
    ~GeometryVerifier();

    void set_fov_bounds(const FOVBounds& fov);
    const FOVBounds& get_fov_bounds() const { return fov_; }

    void set_sight_height_m(float height_m);
    float get_sight_height_m() const { return sight_height_m_; }

    void set_x_alignment_tolerance(float tolerance);
    float get_x_alignment_tolerance() const { return x_alignment_tolerance_; }

    void set_sight_over_bore_tolerance(float tolerance);
    float get_sight_over_bore_tolerance() const { return sight_over_bore_tolerance_; }

    bool is_within_fov(float x, float y) const;

    GeometryCheckResult check_coordinate(float x, float y, uint64_t timestamp_ms = 0,
                                          const std::string& source = "") const;

    GeometryCheckResult check_orange_zone(float oz_x, float oz_y,
                                          float bbox_center_x, float bbox_center_y,
                                          float distance_m,
                                          float target_velocity_x = 0.0f,
                                          uint64_t timestamp_ms = 0,
                                          const std::string& source = "") const;

    float calculate_sight_over_bore_offset_pixels(float distance_m) const;

    bool verify_sight_over_bore_y_offset(float bbox_center_y, float oz_y,
                                         float distance_m, float tolerance_px = -1.0f) const;

    bool verify_x_alignment(float bbox_center_x, float oz_x,
                            float target_velocity_x, float tolerance_px = -1.0f) const;

    std::string get_fov_description() const;

    static std::string error_code_to_string(GeometryErrorCode code);

    uint64_t get_total_geometry_errors() const { return total_errors_.load(); }
    uint64_t get_total_fov_errors() const { return fov_errors_.load(); }
    uint64_t get_total_sight_over_bore_errors() const { return sob_errors_.load(); }
    uint64_t get_total_x_alignment_errors() const { return x_align_errors_.load(); }

    void reset_counters();

private:
    FOVBounds fov_;
    float sight_height_m_;
    float x_alignment_tolerance_;
    float sight_over_bore_tolerance_;

    mutable std::atomic<uint64_t> total_errors_{0};
    mutable std::atomic<uint64_t> fov_errors_{0};
    mutable std::atomic<uint64_t> sob_errors_{0};
    mutable std::atomic<uint64_t> x_align_errors_{0};

    void record_error(GeometryErrorCode code) const;
    GeometryCheckResult create_error_result(GeometryErrorCode code, float x, float y,
                                            uint64_t timestamp_ms, const std::string& source) const;
};

class ScopedGeometryCheck {
public:
    ScopedGeometryCheck(const GeometryVerifier& verifier, float x, float y,
                        uint64_t timestamp_ms, const std::string& source)
        : verifier_(verifier), result_(verifier.check_coordinate(x, y, timestamp_ms, source)) {}

    const GeometryCheckResult& get_result() const { return result_; }
    bool is_valid() const { return result_.is_valid; }
    const std::string& get_error_message() const { return result_.error_message; }

private:
    const GeometryVerifier& verifier_;
    GeometryCheckResult result_;
};

}
}

#endif // GEOMETRY_VERIFICATION_H
