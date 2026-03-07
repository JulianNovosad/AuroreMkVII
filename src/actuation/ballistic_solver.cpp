#include "aurore/ballistic_solver.hpp"
#include <cmath>
#include <random>
#include <algorithm>

namespace aurore {

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

    float tof = range_m / muzzle_velocity_m_s;
    float drop = 0.5f * kGravity * tof * tof;
    float total_drop = drop + height_offset_m;
    float el_deg = std::atan2(-total_drop, range_m) * 180.f / static_cast<float>(M_PI);

    return KineticSolution{el_deg, tof};
}

std::optional<DropSolution> BallisticSolver::solve_drop(float range_m, float height_m) const {
    if (range_m <= 0.f) return std::nullopt;

    float best_v = 1e9f;
    float best_t = 0.f;
    float best_vz = 0.f;

    for (int i = 1; i <= 1000; ++i) {
        float t = static_cast<float>(i) * 0.002f;
        float vx = range_m / t;
        float vz = (height_m + 0.5f * kGravity * t * t) / t;
        float v = std::sqrt(vx * vx + vz * vz);
        if (v < best_v) {
            best_v = v;
            best_t = t;
            best_vz = vz;
        }
    }

    if (best_v > 9000.f) return std::nullopt;

    float el_deg = std::atan2(best_vz, range_m / best_t) * 180.f / static_cast<float>(M_PI);
    return DropSolution{el_deg, best_v, best_t};
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
