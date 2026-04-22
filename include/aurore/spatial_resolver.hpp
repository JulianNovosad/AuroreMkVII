/**
 * @file spatial_resolver.hpp
 * @brief Zero-Mock Boresight Transformation Engine
 *
 * Manages XYZ offsets from Barrel Tip (0,0,0) and computes unified 3D target vectors.
 * Implements parallax compensation for the physical gap between cameras and barrel.
 *
 * Hardware Requirements:
 * - MIPI CSI-2 camera (primary)
 * - USB Webcam (secondary)
 * - LRF (laser rangefinder)
 *
 * No mocks - all hardware must be detected or system fails FAST.
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#pragma once

#include <atomic>
#include <cmath>
#include <optional>
#include <string>

#include "aurore/timing.hpp"

namespace aurore {

struct SensorOffset {
    float x{0.0f};           ///< X offset from barrel tip (mm)
    float y{0.0f};           ///< Y offset from barrel tip (mm) 
    float z{0.0f};           ///< Z offset from barrel tip (mm)
    float pitch_offset{0.0f}; ///< Pitch angle offset (degrees)
    float yaw_offset{0.0f};   ///< Yaw angle offset (degrees)
};

struct TargetVector {
    float x{0.0f};           ///< X coordinate relative to gimbal center (m)
    float y{0.0f};           ///< Y coordinate relative to gimbal center (m)
    float z{0.0f};           ///< Z coordinate relative to gimbal center (m)
    float range_m{0.0f};     ///< Range to target (m)
    float az_deg{0.0f};      ///< Azimuth angle (degrees)
    float el_deg{0.0f};      ///< Elevation angle (degrees)
    bool valid{false};
    uint64_t timestamp_ns{0};
};

struct PixelCoord {
    float u{0.0f};  ///< Horizontal pixel position
    float v{0.0f};  ///< Vertical pixel position
    int width{0};   ///< Frame width
    int height{0};  ///< Frame height
};

struct ConvergenceZone {
    float mipi_x{0.0f};  ///< MIPI convergence center X (pixels)
    float mipi_y{0.0f};  ///< MIPI convergence center Y (pixels)
    float usb_x{0.0f};   ///< USB convergence center X (pixels)
    float usb_y{0.0f};   ///< USB convergence center Y (pixels)
    float tolerance_px{50.0f};  ///< Convergence tolerance (pixels)
};

struct SpatialResolverConfig {
    SensorOffset mipi_offset{};
    SensorOffset usb_offset{};
    SensorOffset lrf_offset{};    ///< LRF position relative to barrel tip (mm)
    SensorOffset barrel_offset{};
    
    float mipi_focal_length_mm{4.0f};
    float mipi_sensor_width_mm{6.4f};
    float mipi_sensor_height_mm{3.6f};
    
    float usb_focal_length_mm{3.6f};
    float usb_sensor_width_mm{4.8f};
    float usb_sensor_height_mm{3.6f};
    
    float convergence_tolerance_px{50.0f};
    float max_range_m{50.0f};
    float min_range_m{0.5f};
    
    bool enable_parallax_compensation{true};
};

class SpatialResolver {
public:
    explicit SpatialResolver(const SpatialResolverConfig& config = SpatialResolverConfig());
    
    bool init();
    bool is_initialized() const { return initialized_.load(std::memory_order_acquire); }
    
    void set_mipi_offset(const SensorOffset& offset);
    void set_usb_offset(const SensorOffset& offset);
    void set_barrel_offset(const SensorOffset& offset);
    
    SensorOffset get_mipi_offset() const;
    SensorOffset get_usb_offset() const;
    SensorOffset get_barrel_offset() const;
    
    TargetVector resolve_from_mipi(float range_mm, const PixelCoord& mipi_px) const;
    
    TargetVector resolve_from_usb(float range_mm, const PixelCoord& usb_px) const;
    
    TargetVector resolve_fused(float range_mm, 
                               const PixelCoord& mipi_px, 
                               const PixelCoord& usb_px) const;
    
    ConvergenceZone calculate_convergence_zone(float range_mm) const;
    
    float calculate_parallax_adjustment(float range_mm, const SensorOffset& sensor) const;

    /// Convert raw LRF range (mm) to barrel-relative range (m) with parallax compensation.
    /// This is the range value that should be fed to BallisticSolver::solve().
    float lrf_to_barrel_range_m(float lrf_range_mm) const;
    
    PixelCoord project_to_mipi(const TargetVector& target) const;
    PixelCoord project_to_usb(const TargetVector& target) const;
    
    bool is_in_convergence_zone(const PixelCoord& mipi_px, 
                                 const PixelCoord& usb_px,
                                 float range_mm) const;
    
    void calibrate_with_known_target(float measured_range_mm,
                                       const PixelCoord& mipi_px,
                                       const PixelCoord& usb_px);

private:
    SpatialResolverConfig config_;
    std::atomic<bool> initialized_{false};
    
    SensorOffset calibrated_mipi_offset_{};
    SensorOffset calibrated_usb_offset_{};
    
    static constexpr float kMmToM = 0.001f;
    static constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;
    static constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);
};

}  // namespace aurore
