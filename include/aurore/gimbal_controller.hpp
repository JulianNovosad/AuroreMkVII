#pragma once
#include <atomic>
#include <cmath>
#include <algorithm>

namespace aurore {

enum class GimbalSource { AUTO, FREECAM };

struct GimbalCommand {
    float az_deg{0.f};
    float el_deg{0.f};
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
    GimbalCommand command_absolute(float az_deg, float el_deg);

    void set_source(GimbalSource s) { source_.store(s, std::memory_order_release); }
    GimbalSource source() const     { return source_.load(std::memory_order_acquire); }

    // Get last commanded angles (from either source)
    float current_az() const { return az_.load(std::memory_order_relaxed); }
    float current_el() const { return el_.load(std::memory_order_relaxed); }

    // Clamp limits (set from config)
    void set_limits(float az_min, float az_max, float el_min, float el_max);

private:
    CameraIntrinsics cam_;
    std::atomic<GimbalSource> source_{GimbalSource::AUTO};
    std::atomic<float> az_{0.f};
    std::atomic<float> el_{0.f};
    float az_min_{-90.f}, az_max_{90.f};
    float el_min_{-10.f}, el_max_{45.f};
};

}  // namespace aurore
