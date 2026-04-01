#pragma once

namespace aurore {

/**
 * @brief Lissajous oval sweep pattern for autonomous target acquisition.
 *
 * Generates smooth gimbal angles for the SEARCH state:
 *   az(t) = az_amplitude * sin(2π * t / T_az)
 *   el(t) = el_offset + el_amplitude * sin(2π * t / T_el + π/2)
 *
 * With T_el = T_az / 2 this traces an oval covering the full FOV.
 */
class SweepPattern {
public:
    struct Config {
        float az_amplitude_deg = 80.0f;  ///< ±80° azimuth sweep (within ±90° gimbal limit)
        float el_amplitude_deg = 15.0f;  ///< ±15° elevation swing
        float el_offset_deg    = 10.0f;  ///< center elevation (look slightly upward)
        float az_period_sec    = 10.0f;  ///< azimuth full cycle period
        // el period is az_period / 2 — produces oval (2:1 Lissajous)
    };

    struct Point {
        float az_deg;
        float el_deg;
    };

    SweepPattern();
    explicit SweepPattern(const Config& cfg);

    /// Advance time by dt_sec and return the current target (az, el).
    Point tick(float dt_sec);

    /// Reset sweep back to origin (call when entering SEARCH state).
    void reset();

    float elapsed_sec() const { return elapsed_sec_; }

private:
    Config cfg_;
    float elapsed_sec_{0.0f};
};

}  // namespace aurore
