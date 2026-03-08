#include "aurore/ballistic_solver.hpp"
#include <cmath>
#include <random>
#include <algorithm>
#include <vector>

namespace aurore {

// ============================================================================
// G1 Drag Model + RK4 Integration Implementation
// ============================================================================

// G1 Drag Model: 4-segment piecewise Cd vs Mach number
float BallisticSolver::get_drag_coefficient(float mach_number) const {
    if (mach_number < 0.f) mach_number = 0.f;
    
    if (mach_number <= kMachSubsonicMax) {
        // Subsonic (Mach 0-0.8): cd = 0.2
        return kCdSubsonic;
    } else if (mach_number <= kMachTransonicMax) {
        // Transonic (Mach 0.8-1.2): cd = 0.4
        return kCdTransonic;
    } else if (mach_number <= kMachSupersonicMax) {
        // Supersonic (Mach 1.2-2.5): cd = 0.25
        return kCdSupersonic;
    } else {
        // Hypersonic (Mach 2.5-10): cd = 0.18
        return kCdHypersonic;
    }
}

// Compute derivative for RK4: d(state)/dt = [vx, vy, vz, ax, ay, az]
// Physics:
//   Drag acceleration: a_drag = -0.5 * rho * v^2 * cd * A / m
//   Gravity: az -= 9.81
Rk4Derivative BallisticSolver::compute_derivative(
    const Rk4State& state, float air_density, 
    float cross_section_m2, float mass_kg) const 
{
    Rk4Derivative deriv;
    
    // Velocity components (dx/dt = vx, dy/dt = vy, dz/dt = vz)
    deriv.dx = state.vx;
    deriv.dy = state.vy;
    deriv.dz = state.vz;
    
    // Compute speed and Mach number
    float speed = std::sqrt(state.vx * state.vx + state.vy * state.vy + state.vz * state.vz);
    float mach = speed / kSpeedOfSound;
    
    // Get drag coefficient from G1 model
    float cd = get_drag_coefficient(mach);
    
    // Drag acceleration magnitude: a_drag = 0.5 * rho * v^2 * cd * A / m
    // Direction: opposite to velocity vector
    float drag_accel_mag = 0.f;
    if (speed > 1e-6f) {
        drag_accel_mag = 0.5f * air_density * speed * speed * cd * cross_section_m2 / mass_kg;
        // Acceleration components (drag + gravity)
        // Drag acts opposite to velocity
        deriv.dvx = -drag_accel_mag * (state.vx / speed);
        deriv.dvy = -drag_accel_mag * (state.vy / speed);
        deriv.dvz = -drag_accel_mag * (state.vz / speed) - kGravity;  // Gravity acts downward (-z)
    } else {
        // Zero velocity - only gravity acts
        deriv.dvx = 0.f;
        deriv.dvy = 0.f;
        deriv.dvz = -kGravity;
    }

    return deriv;
}

// RK4 step: s_next = s + (k1 + 2*k2 + 2*k3 + k4) * dt / 6
Rk4State BallisticSolver::rk4_step(
    const Rk4State& state, float dt, float air_density,
    float cross_section_m2, float mass_kg) const 
{
    // k1 = f(s)
    Rk4Derivative k1 = compute_derivative(state, air_density, cross_section_m2, mass_kg);
    
    // k2 = f(s + k1 * 0.5 * dt)
    Rk4State s2;
    s2.x = state.x + k1.dx * 0.5f * dt;
    s2.y = state.y + k1.dy * 0.5f * dt;
    s2.z = state.z + k1.dz * 0.5f * dt;
    s2.vx = state.vx + k1.dvx * 0.5f * dt;
    s2.vy = state.vy + k1.dvy * 0.5f * dt;
    s2.vz = state.vz + k1.dvz * 0.5f * dt;
    Rk4Derivative k2 = compute_derivative(s2, air_density, cross_section_m2, mass_kg);
    
    // k3 = f(s + k2 * 0.5 * dt)
    Rk4State s3;
    s3.x = state.x + k2.dx * 0.5f * dt;
    s3.y = state.y + k2.dy * 0.5f * dt;
    s3.z = state.z + k2.dz * 0.5f * dt;
    s3.vx = state.vx + k2.dvx * 0.5f * dt;
    s3.vy = state.vy + k2.dvy * 0.5f * dt;
    s3.vz = state.vz + k2.dvz * 0.5f * dt;
    Rk4Derivative k3 = compute_derivative(s3, air_density, cross_section_m2, mass_kg);
    
    // k4 = f(s + k3 * dt)
    Rk4State s4;
    s4.x = state.x + k3.dx * dt;
    s4.y = state.y + k3.dy * dt;
    s4.z = state.z + k3.dz * dt;
    s4.vx = state.vx + k3.dvx * dt;
    s4.vy = state.vy + k3.dvy * dt;
    s4.vz = state.vz + k3.dvz * dt;
    Rk4Derivative k4 = compute_derivative(s4, air_density, cross_section_m2, mass_kg);
    
    // s_next = s + (k1 + 2*k2 + 2*k3 + k4) * dt / 6
    Rk4State next;
    float dt_over_6 = dt / 6.0f;
    next.x = state.x + (k1.dx + 2.f * k2.dx + 2.f * k3.dx + k4.dx) * dt_over_6;
    next.y = state.y + (k1.dy + 2.f * k2.dy + 2.f * k3.dy + k4.dy) * dt_over_6;
    next.z = state.z + (k1.dz + 2.f * k2.dz + 2.f * k3.dz + k4.dz) * dt_over_6;
    next.vx = state.vx + (k1.dvx + 2.f * k2.dvx + 2.f * k3.dvx + k4.dvx) * dt_over_6;
    next.vy = state.vy + (k1.dvy + 2.f * k2.dvy + 2.f * k3.dvy + k4.dvy) * dt_over_6;
    next.vz = state.vz + (k1.dvz + 2.f * k2.dvz + 2.f * k3.dvz + k4.dvz) * dt_over_6;
    
    return next;
}

// Full trajectory simulation using RK4 + G1 drag
std::vector<BallisticSolver::TrajectoryPoint> BallisticSolver::simulate_trajectory(
    float muzzle_velocity_m_s, float launch_angle_rad,
    float air_density, float cross_section_m2, float mass_kg,
    float max_distance_m, float dt) const 
{
    std::vector<TrajectoryPoint> trajectory;
    
    // Initial state: launch from origin, angled upward
    Rk4State state;
    state.x = 0.f;
    state.y = 0.f;
    state.z = 0.f;
    state.vx = muzzle_velocity_m_s * std::cos(launch_angle_rad);
    state.vy = 0.f;  // No lateral velocity
    state.vz = muzzle_velocity_m_s * std::sin(launch_angle_rad);
    
    float time = 0.f;
    
    // Add initial point
    trajectory.push_back({state.x, state.y, state.z, state.vx, state.vy, state.vz, time});
    
    // Integrate until we reach max_distance or hit the ground (z <= 0)
    constexpr int max_steps = 100000;  // Allow up to 100 seconds at 1ms step
    int steps = 0;
    
    while (state.x < max_distance_m && state.z >= 0.f && steps < max_steps) {
        state = rk4_step(state, dt, air_density, cross_section_m2, mass_kg);
        time += dt;
        steps++;
        
        trajectory.push_back({state.x, state.y, state.z, state.vx, state.vy, state.vz, time});
        
        // Safety check for NaN/Inf
        if (!std::isfinite(state.x) || !std::isfinite(state.y) || !std::isfinite(state.z) ||
            !std::isfinite(state.vx) || !std::isfinite(state.vy) || !std::isfinite(state.vz)) {
            break;
        }
    }
    
    return trajectory;
}

// ============================================================================
// Original Implementation (with modifications to use RK4 + G1 drag)
// ============================================================================

BallisticSolver::BallisticSolver() {
    // PERF-005: Initialize lookup table at construction
    initialize_lookup_table();
}

// PERF-005: Pre-compute p_hit lookup table for fast runtime access
void BallisticSolver::initialize_lookup_table() {
    // Use a fixed seed for reproducible table generation
    std::mt19937 rng(42);
    std::normal_distribution<float> range_noise(0.f, kRangeSigmaM);
    std::normal_distribution<float> vel_noise(0.f, kVelocitySigmaMps);
    std::normal_distribution<float> align_noise(0.f, kAlignSigmaDeg);

    const int n_sims = 500;  // Higher sample count for accurate table generation

    // Pre-compute for KINETIC mode
    for (int ri = 0; ri < kLookupTableRangeBins; ++ri) {
        float range = index_to_range(ri);
        for (int vi = 0; vi < kLookupTableVelocityBins; ++vi) {
            float velocity = index_to_velocity(vi);
            
            // Compute nominal solution
            float tof = range / velocity;
            float drop = 0.5f * kGravity * tof * tof;
            (void)drop;  // Reserved for future use

            int hits = 0;
            for (int i = 0; i < n_sims; ++i) {
                float r = range + range_noise(rng);
                float v = velocity + vel_noise(rng);
                float az_err = align_noise(rng);

                if (r <= 0.f || v <= 0.f) continue;

                float tof_sim = r / v;
                float drop_sim = 0.5f * kGravity * tof_sim * tof_sim;
                float impact_x = az_err * (static_cast<float>(M_PI) / 180.f) * r;
                float impact_y = drop_sim;

                float miss = std::sqrt(impact_x * impact_x + impact_y * impact_y);
                if (miss <= kTargetHalfSizeM) ++hits;
            }
            p_hit_table_kinetic_[ri][vi] = static_cast<float>(hits) / static_cast<float>(n_sims);
        }
    }

    // Pre-compute for DROP mode
    for (int ri = 0; ri < kLookupTableRangeBins; ++ri) {
        float range = index_to_range(ri);
        for (int vi = 0; vi < kLookupTableVelocityBins; ++vi) {
            float velocity = index_to_velocity(vi);
            
            int hits = 0;
            for (int i = 0; i < n_sims; ++i) {
                float r = range + range_noise(rng);
                float v = velocity + vel_noise(rng);
                float az_err = align_noise(rng);

                if (r <= 0.f || v <= 0.f) continue;

                // DROP mode: impact_y is approximately 0 (optimized trajectory)
                float impact_x = az_err * (static_cast<float>(M_PI) / 180.f) * r;
                float impact_y = 0.f;

                float miss = std::sqrt(impact_x * impact_x + impact_y * impact_y);
                if (miss <= kTargetHalfSizeM) ++hits;
            }
            p_hit_table_drop_[ri][vi] = static_cast<float>(hits) / static_cast<float>(n_sims);
        }
    }

    lookup_table_initialized_.store(true, std::memory_order_release);
}

EngagementMode BallisticSolver::select_mode(float range_m,
                                             float gimbal_el_deg,
                                             float target_aspect) const {
    if (target_aspect > kAspectDropThresh && range_m < kRangeDropThresh)
        return EngagementMode::DROP;
    if (gimbal_el_deg < kElevDropThresh)
        return EngagementMode::KINETIC;
    return EngagementMode::DROP;
}

std::optional<KineticSolution> BallisticSolver::solve_kinetic(
    float range_m, float height_offset_m, float muzzle_velocity_m_s) const
{
    if (muzzle_velocity_m_s <= 0.f || range_m <= 0.f) return std::nullopt;

    // Use RK4 + G1 drag for trajectory simulation
    // Default projectile parameters (typical for small arms)
    constexpr float kDefaultMass = 0.004f;        // 4 grams
    constexpr float kDefaultDiameter = 0.0055f;   // 5.5mm
    constexpr float kDefaultAirDensity = 1.225f;  // kg/m^3 at sea level
    
    float cross_section = static_cast<float>(M_PI) * (kDefaultDiameter / 2.f) * (kDefaultDiameter / 2.f);
    
    // Binary search for the launch angle that hits the target
    // Target is at (range_m, 0, -height_offset_m) in our coordinate system
    // We need to find angle such that trajectory passes through target
    
    float low_angle = -0.1f;   // -5.7 degrees
    float high_angle = 0.1f;   // +5.7 degrees
    constexpr int max_iterations = 50;
    constexpr float tolerance = 0.001f;  // 1mm
    
    float best_angle = 0.f;
    float best_error = 1e9f;
    
    for (int i = 0; i < max_iterations; ++i) {
        float mid_angle = (low_angle + high_angle) / 2.f;
        
        auto trajectory = simulate_trajectory(
            muzzle_velocity_m_s, mid_angle,
            kDefaultAirDensity, cross_section, kDefaultMass,
            range_m * 1.5f);  // Simulate beyond target range
        
        if (trajectory.empty()) {
            low_angle = mid_angle;
            continue;
        }
        
        // Find the point where x = range_m (interpolate)
        float impact_z = 0.f;
        bool found = false;
        
        for (size_t j = 1; j < trajectory.size(); ++j) {
            if (trajectory[j].x >= range_m) {
                // Linear interpolation
                float t = (range_m - trajectory[j-1].x) / 
                          (trajectory[j].x - trajectory[j-1].x);
                if (t >= 0.f && t <= 1.f) {
                    impact_z = trajectory[j-1].z + t * (trajectory[j].z - trajectory[j-1].z);
                    found = true;
                }
                break;
            }
        }
        
        if (!found) {
            low_angle = mid_angle;
            continue;
        }
        
        // Target is at z = -height_offset_m (below launch point)
        float target_z = -height_offset_m;
        float error = impact_z - target_z;
        
        if (std::abs(error) < std::abs(best_error)) {
            best_error = error;
            best_angle = mid_angle;
        }
        
        if (std::abs(error) < tolerance) {
            break;  // Found good solution
        }
        
        // Adjust search bounds
        if (error < 0.f) {
            // Hit below target, need higher angle
            low_angle = mid_angle;
        } else {
            // Hit above target, need lower angle
            high_angle = mid_angle;
        }
    }
    
    // Calculate time of flight from the trajectory
    auto final_trajectory = simulate_trajectory(
        muzzle_velocity_m_s, best_angle,
        kDefaultAirDensity, cross_section, kDefaultMass,
        range_m * 1.5f);
    
    float tof = 0.f;
    for (size_t j = 1; j < final_trajectory.size(); ++j) {
        if (final_trajectory[j].x >= range_m) {
            float t = (range_m - final_trajectory[j-1].x) / 
                      (final_trajectory[j].x - final_trajectory[j-1].x);
            if (t >= 0.f && t <= 1.f) {
                tof = final_trajectory[j-1].time + t * (final_trajectory[j].time - final_trajectory[j-1].time);
            }
            break;
        }
    }
    
    float el_deg = best_angle * 180.f / static_cast<float>(M_PI);
    return KineticSolution{el_deg, tof};
}

std::optional<DropSolution> BallisticSolver::solve_drop(float range_m, float height_m) const {
    if (range_m <= 0.f) return std::nullopt;

    // DROP mode always uses an upward arc (positive elevation angle)
    // Use a 45 degree launch angle as the base (optimal for maximum range)
    float launch_angle = static_cast<float>(M_PI) / 4.f;  // 45 degrees

    // Use kinematic equations to find velocity
    float cos_angle = std::cos(launch_angle);
    float tan_angle = std::tan(launch_angle);
    float denominator = range_m * tan_angle - height_m;

    float launch_v;
    if (std::abs(denominator) < 0.001f) {
        launch_v = 100.f;
    } else if (denominator > 0.f) {
        launch_v = std::sqrt(0.5f * kGravity * range_m * range_m /
                            (denominator * cos_angle * cos_angle));
    } else {
        // Target is above the 45-degree trajectory - need steeper angle
        launch_angle = 1.2f;  // ~69 degrees (steep drop)
        cos_angle = std::cos(launch_angle);
        tan_angle = std::tan(launch_angle);
        denominator = range_m * tan_angle - height_m;
        if (denominator > 0.001f) {
            launch_v = std::sqrt(0.5f * kGravity * range_m * range_m /
                                (denominator * cos_angle * cos_angle));
        } else {
            launch_v = 200.f;
        }
    }

    // Sanity check on velocity
    if (launch_v > 500.f) launch_v = 500.f;
    if (launch_v < 10.f) launch_v = 10.f;

    // Calculate time of flight
    float tof = range_m / (launch_v * cos_angle);

    float el_deg = launch_angle * 180.f / static_cast<float>(M_PI);
    return DropSolution{el_deg, launch_v, tof};
}

std::optional<FireControlSolution> BallisticSolver::solve(
    float range_m, float gimbal_el_deg, float target_aspect,
    float muzzle_velocity_m_s) const
{
    EngagementMode mode = select_mode(range_m, gimbal_el_deg, target_aspect);

    FireControlSolution sol;
    sol.range_m = range_m;
    sol.kinetic_mode = (mode == EngagementMode::KINETIC);

    if (mode == EngagementMode::KINETIC) {
        auto k = solve_kinetic(range_m, 0.f, muzzle_velocity_m_s);
        if (!k) return std::nullopt;
        sol.el_lead_deg = k->el_lead_deg;
        sol.az_lead_deg = 0.f;
        sol.velocity_m_s = muzzle_velocity_m_s;
    } else {
        auto d = solve_drop(range_m, 0.f);
        if (!d) return std::nullopt;
        sol.el_lead_deg = d->el_lead_deg;
        sol.az_lead_deg = 0.f;
        sol.velocity_m_s = d->launch_v_m_s;
    }

    // PERF-005: Use lookup table instead of Monte Carlo simulation (10x faster)
    sol.p_hit = get_p_hit_from_table(range_m, sol.velocity_m_s, sol.kinetic_mode);
    return sol;
}

// PERF-005: Fast lookup table based p_hit calculation
float BallisticSolver::get_p_hit_from_table(float range_m, float velocity_m_s, bool kinetic_mode) const {
    if (!lookup_table_initialized_.load(std::memory_order_acquire)) {
        // Fallback to Monte Carlo if table not initialized
        FireControlSolution nominal;
        nominal.range_m = range_m;
        nominal.velocity_m_s = velocity_m_s;
        nominal.kinetic_mode = kinetic_mode;
        return monte_carlo_p_hit(nominal, 50);
    }

    // Clamp inputs to table bounds
    float clamped_range = std::clamp(range_m, kLookupTableMinRange, kLookupTableMaxRange);
    float clamped_velocity = std::clamp(velocity_m_s, kLookupTableMinVelocity, kLookupTableMaxVelocity);

    int ri = range_to_index(clamped_range);
    int vi = velocity_to_index(clamped_velocity);

    // Bounds check (should never fail due to clamping)
    if (ri < 0 || ri >= kLookupTableRangeBins || vi < 0 || vi >= kLookupTableVelocityBins) {
        return 0.5f;  // Default fallback
    }

    // Return pre-computed value
    if (kinetic_mode) {
        return p_hit_table_kinetic_[ri][vi];
    } else {
        return p_hit_table_drop_[ri][vi];
    }
}

// PERF-005: Helper functions for table index conversion
int BallisticSolver::range_to_index(float range_m) const {
    float normalized = (range_m - kLookupTableMinRange) / (kLookupTableMaxRange - kLookupTableMinRange);
    return static_cast<int>(std::round(normalized * (kLookupTableRangeBins - 1)));
}

int BallisticSolver::velocity_to_index(float velocity_m_s) const {
    float normalized = (velocity_m_s - kLookupTableMinVelocity) / (kLookupTableMaxVelocity - kLookupTableMinVelocity);
    return static_cast<int>(std::round(normalized * (kLookupTableVelocityBins - 1)));
}

float BallisticSolver::index_to_range(int idx) const {
    return kLookupTableMinRange + (static_cast<float>(idx) / static_cast<float>(kLookupTableRangeBins - 1)) 
           * (kLookupTableMaxRange - kLookupTableMinRange);
}

float BallisticSolver::index_to_velocity(int idx) const {
    return kLookupTableMinVelocity + (static_cast<float>(idx) / static_cast<float>(kLookupTableVelocityBins - 1))
           * (kLookupTableMaxVelocity - kLookupTableMinVelocity);
}

// Legacy Monte Carlo method - kept for compatibility and offline table generation
float BallisticSolver::monte_carlo_p_hit(const FireControlSolution& nominal, int n_sims) const {
    std::mt19937 rng(42);
    std::normal_distribution<float> range_noise(0.f, kRangeSigmaM);
    std::normal_distribution<float> vel_noise(0.f, kVelocitySigmaMps);
    std::normal_distribution<float> align_noise(0.f, kAlignSigmaDeg);

    int hits = 0;

    for (int i = 0; i < n_sims; ++i) {
        float r = nominal.range_m + range_noise(rng);
        float v = nominal.velocity_m_s + vel_noise(rng);
        float az_err = align_noise(rng);

        if (r <= 0.f || v <= 0.f) continue;

        float impact_x, impact_y;

        if (nominal.kinetic_mode) {
            float tof = r / v;
            float drop = 0.5f * kGravity * tof * tof;
            impact_x = az_err * (static_cast<float>(M_PI) / 180.f) * r;
            impact_y = drop;
        } else {
            impact_x = az_err * (static_cast<float>(M_PI) / 180.f) * r;
            impact_y = 0.f;
        }

        float miss = std::sqrt(impact_x * impact_x + impact_y * impact_y);
        if (miss <= kTargetHalfSizeM) ++hits;
    }

    return static_cast<float>(hits) / static_cast<float>(n_sims);
}

}  // namespace aurore
