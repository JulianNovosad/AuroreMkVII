#include "aurore/sweep_pattern.hpp"

#include <cmath>

namespace aurore {

namespace {
constexpr float kTwoPi = 6.28318530718f;
}

SweepPattern::SweepPattern() : cfg_(Config{}) {}
SweepPattern::SweepPattern(const Config& cfg) : cfg_(cfg) {}

void SweepPattern::reset() { elapsed_sec_ = 0.0f; }

SweepPattern::Point SweepPattern::tick(float dt_sec) {
    elapsed_sec_ += dt_sec;

    const float t = elapsed_sec_;
    const float T_az = cfg_.az_period_sec;
    const float T_el = T_az * 0.5f;  // 2:1 ratio → oval

    const float az = cfg_.az_amplitude_deg * std::sin(kTwoPi * t / T_az);
    const float el =
        cfg_.el_offset_deg + cfg_.el_amplitude_deg * std::sin(kTwoPi * t / T_el + kTwoPi * 0.25f);

    return {az, el};
}

}  // namespace aurore
