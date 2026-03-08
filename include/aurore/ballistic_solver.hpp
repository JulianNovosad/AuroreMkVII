#pragma once

#include <cstdint>
#include <optional>
#include <array>
#include <atomic>
#include "aurore/state_machine.hpp"

namespace aurore {

enum class EngagementMode : uint8_t { KINETIC = 0, DROP = 1 };

struct KineticSolution {
    float el_lead_deg{0.f};
    float tof_s{0.f};
};

struct DropSolution {
    float el_lead_deg{0.f};
    float launch_v_m_s{0.f};
    float tof_s{0.f};
};

// G1 Drag Model: 4-segment piecewise Cd vs Mach number
// Subsonic (Mach 0-0.8): cd = 0.2
// Transonic (Mach 0.8-1.2): cd = 0.4
// Supersonic (Mach 1.2-2.5): cd = 0.25
// Hypersonic (Mach 2.5-10): cd = 0.18
static constexpr float kMachSubsonicMax   = 0.8f;
static constexpr float kMachTransonicMax  = 1.2f;
static constexpr float kMachSupersonicMax = 2.5f;
static constexpr float kMachHypersonicMax = 10.0f;
static constexpr float kCdSubsonic        = 0.2f;
static constexpr float kCdTransonic       = 0.4f;
static constexpr float kCdSupersonic      = 0.25f;
static constexpr float kCdHypersonic      = 0.18f;
static constexpr float kSpeedOfSound      = 343.0f;  // m/s at sea level, 15°C

// RK4 State vector: [x, y, z, vx, vy, vz]
struct Rk4State {
    float x{0.f}, y{0.f}, z{0.f};     // Position (m)
    float vx{0.f}, vy{0.f}, vz{0.f};  // Velocity (m/s)
};

// RK4 Derivative vector: [vx, vy, vz, ax, ay, az]
struct Rk4Derivative {
    float dx{0.f}, dy{0.f}, dz{0.f};   // Velocity components
    float dvx{0.f}, dvy{0.f}, dvz{0.f}; // Acceleration components
};

// PERF-005: Lookup table dimensions for pre-computed p_hit
// Range: 0.1m to 10m in 0.1m steps (100 entries)
// Velocity: 50 m/s to 500 m/s in 10 m/s steps (46 entries)
static constexpr int kLookupTableRangeBins = 100;
static constexpr int kLookupTableVelocityBins = 46;
static constexpr float kLookupTableMinRange = 0.1f;
static constexpr float kLookupTableMaxRange = 10.0f;
static constexpr float kLookupTableMinVelocity = 50.0f;
static constexpr float kLookupTableMaxVelocity = 500.0f;

class BallisticSolver {
public:
    BallisticSolver();

    // PERF-005: Initialize lookup table (call once at startup)
    void initialize_lookup_table();

    EngagementMode select_mode(float range_m, float gimbal_el_deg, float target_aspect) const;

    std::optional<KineticSolution> solve_kinetic(float range_m,
                                                  float height_offset_m,
                                                  float muzzle_velocity_m_s) const;

    std::optional<DropSolution> solve_drop(float range_m, float height_m) const;

    std::optional<FireControlSolution> solve(float range_m,
                                             float gimbal_el_deg,
                                             float target_aspect,
                                             float muzzle_velocity_m_s) const;

    // PERF-005: Fast lookup table based p_hit calculation
    float get_p_hit_from_table(float range_m, float velocity_m_s, bool kinetic_mode) const;

    // Legacy method kept for compatibility
    float monte_carlo_p_hit(const FireControlSolution& nominal, int n_sims = 50) const;

    // G1 Drag + RK4 integration methods (public for testing)
    float get_drag_coefficient(float mach_number) const;
    Rk4Derivative compute_derivative(const Rk4State& state, float air_density,
                                      float cross_section_m2, float mass_kg) const;
    Rk4State rk4_step(const Rk4State& state, float dt, float air_density,
                      float cross_section_m2, float mass_kg) const;

    // Trajectory simulation using RK4 + G1 drag
    struct TrajectoryPoint {
        float x{0.f}, y{0.f}, z{0.f};    // Position (m)
        float vx{0.f}, vy{0.f}, vz{0.f}; // Velocity (m/s)
        float time{0.f};                  // Time since launch (s)
    };
    std::vector<TrajectoryPoint> simulate_trajectory(
        float muzzle_velocity_m_s, float launch_angle_rad,
        float air_density, float cross_section_m2, float mass_kg,
        float max_distance_m, float dt = 0.0005f) const;

private:
    // PERF-005: Pre-computed lookup table [range_bin][velocity_bin]
    // Stored as 2D array for cache-friendly access
    std::array<std::array<float, kLookupTableVelocityBins>, kLookupTableRangeBins> p_hit_table_kinetic_;
    std::array<std::array<float, kLookupTableVelocityBins>, kLookupTableRangeBins> p_hit_table_drop_;
    std::atomic<bool> lookup_table_initialized_{false};

    static constexpr float kGravity           =  9.81f;
    static constexpr float kDefaultDensity    =  1.225f;
    static constexpr float kAspectDropThresh  =  2.0f;
    static constexpr float kElevDropThresh    = 45.0f;
    static constexpr float kRangeDropThresh   =  1.5f;

    static constexpr float kRangeSigmaM       = 0.010f;
    static constexpr float kVelocitySigmaMps  = 5.0f;
    static constexpr float kDensitySigma      = 0.02f;
    static constexpr float kAlignSigmaDeg     = 0.1f;
    static constexpr float kTargetHalfSizeM   = 0.040f;

    // PERF-005: Helper to convert continuous values to table indices
    int range_to_index(float range_m) const;
    int velocity_to_index(float velocity_m_s) const;
    float index_to_range(int idx) const;
    float index_to_velocity(int idx) const;
};

}  // namespace aurore
