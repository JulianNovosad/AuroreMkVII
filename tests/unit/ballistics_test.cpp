#include "aurore/ballistic_solver.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace aurore;

void test_profile_validation_valid() {
    BallisticProfile profile;
    profile.name = "Test";
    profile.muzzle_velocity_m_s = 900.f;
    profile.ballistic_coefficient = 0.3f;
    profile.sight_height_mm = 50.f;
    profile.zero_range_m = 100.f;
    
    assert(profile.validate());
    std::cout << "PASS: Valid profile passes validation\n";
}

void test_profile_validation_invalid_velocity() {
    BallisticProfile profile;
    profile.muzzle_velocity_m_s = 30.f;  // Too low (< 50 m/s)
    assert(!profile.validate());
    
    profile.muzzle_velocity_m_s = 1600.f;  // Too high (> 1500 m/s)
    assert(!profile.validate());
    std::cout << "PASS: Invalid velocity rejected\n";
}

void test_profile_validation_invalid_bc() {
    BallisticProfile profile;
    profile.ballistic_coefficient = 0.01f;  // Too low
    assert(!profile.validate());
    
    profile.ballistic_coefficient = 2.0f;  // Too high
    assert(!profile.validate());
    std::cout << "PASS: Invalid ballistic coefficient rejected\n";
}

void test_profile_load_from_json() {
    nlohmann::json config = nlohmann::json::parse(R"({
        "ballistics": {
            "profiles": [
                {
                    "name": "Default",
                    "muzzle_velocity_mps": 900.0,
                    "ballistic_coefficient": 0.300,
                    "sight_height_mm": 50.0,
                    "zero_range_m": 100.0
                },
                {
                    "name": "Subsonic",
                    "muzzle_velocity_mps": 300.0,
                    "ballistic_coefficient": 0.450,
                    "sight_height_mm": 45.0,
                    "zero_range_m": 50.0
                }
            ]
        }
    })");

    BallisticSolver solver;
    [[maybe_unused]] bool result = solver.loadProfiles(config);
    assert(result);

    auto profiles = solver.getAvailableProfiles();
    assert(profiles.size() == 2);
    assert(profiles[0].name == "Default");
    assert(profiles[0].muzzle_velocity_m_s == 900.f);
    assert(profiles[1].name == "Subsonic");
    assert(profiles[1].muzzle_velocity_m_s == 300.f);

    [[maybe_unused]] auto active = solver.getActiveProfile();
    assert(active != nullptr);
    assert(active->name == "Default");

    std::cout << "PASS: Profile load from JSON\n";
}

void test_profile_set_active() {
    nlohmann::json config = nlohmann::json::parse(R"({
        "ballistics": {
            "profiles": [
                {
                    "name": "Profile1",
                    "muzzle_velocity_mps": 800.0,
                    "ballistic_coefficient": 0.300,
                    "sight_height_mm": 50.0,
                    "zero_range_m": 100.0
                },
                {
                    "name": "Profile2",
                    "muzzle_velocity_mps": 950.0,
                    "ballistic_coefficient": 0.350,
                    "sight_height_mm": 55.0,
                    "zero_range_m": 150.0
                }
            ]
        }
    })");

    BallisticSolver solver;
    solver.loadProfiles(config);

    // Switch to Profile2
    [[maybe_unused]] bool result = solver.setActiveProfile("Profile2");
    assert(result);

    [[maybe_unused]] auto active = solver.getActiveProfile();
    assert(active != nullptr);
    assert(active->name == "Profile2");
    assert(active->muzzle_velocity_m_s == 950.f);

    // Try to switch to non-existent profile
    result = solver.setActiveProfile("NonExistent");
    assert(!result);

    // Active profile should remain unchanged
    active = solver.getActiveProfile();
    assert(active->name == "Profile2");

    std::cout << "PASS: Set active profile\n";
}

void test_profile_load_invalid_skipped() {
    nlohmann::json config = nlohmann::json::parse(R"({
        "ballistics": {
            "profiles": [
                {
                    "name": "Valid",
                    "muzzle_velocity_mps": 900.0,
                    "ballistic_coefficient": 0.300,
                    "sight_height_mm": 50.0,
                    "zero_range_m": 100.0
                },
                {
                    "name": "Invalid",
                    "muzzle_velocity_mps": 10.0,
                    "ballistic_coefficient": 0.300,
                    "sight_height_mm": 50.0,
                    "zero_range_m": 100.0
                }
            ]
        }
    })");

    BallisticSolver solver;
    [[maybe_unused]] bool result = solver.loadProfiles(config);
    assert(result);

    auto profiles = solver.getAvailableProfiles();
    assert(profiles.size() == 1);  // Invalid profile should be skipped
    assert(profiles[0].name == "Valid");

    std::cout << "PASS: Invalid profiles skipped\n";
}

void test_profile_load_default_fallback() {
    nlohmann::json config = nlohmann::json::object();  // No ballistics section

    BallisticSolver solver;
    [[maybe_unused]] bool result = solver.loadProfiles(config);
    assert(result);

    auto profiles = solver.getAvailableProfiles();
    assert(profiles.size() == 1);
    assert(profiles[0].name == "Default");

    [[maybe_unused]] auto active = solver.getActiveProfile();
    assert(active != nullptr);
    assert(active->name == "Default");

    std::cout << "PASS: Default profile fallback\n";
}

void test_kinetic_mode_tof_formula() {
    BallisticSolver solver;
    [[maybe_unused]] auto sol = solver.solve_kinetic(1.0f, 0.f, 700.f);
    assert(sol.has_value());
    assert(std::abs(sol->el_lead_deg) < 0.5f);
    assert(sol->tof_s < 0.01f);
    std::cout << "PASS: kinetic mode tof < 10ms at 1m/700m/s\n";
}

void test_drop_mode_requires_valid_arc() {
    BallisticSolver solver;
    [[maybe_unused]] auto sol = solver.solve_drop(1.5f, -0.5f);
    assert(sol.has_value());
    assert(sol->el_lead_deg > 0.f);
    assert(sol->launch_v_m_s > 0.f);
    std::cout << "PASS: drop mode finds upward arc for 1.5m/-0.5m target\n";
}

void test_drop_mode_impossible_geometry() {
    BallisticSolver solver;
    [[maybe_unused]] auto sol = solver.solve_drop(3.0f, 2.5f);
    std::cout << "PASS: drop mode handles upward target gracefully\n";
}

void test_monte_carlo_p_hit_perfect_inputs() {
    BallisticSolver solver;
    FireControlSolution perfect;
    perfect.range_m = 1.0f;
    perfect.el_lead_deg = 0.f;
    perfect.az_lead_deg = 0.f;
    perfect.velocity_m_s = 700.f;
    perfect.kinetic_mode = true;

    float p = solver.monte_carlo_p_hit(perfect, 50);
    assert(p >= 0.f && p <= 1.f);
    std::cout << "PASS: monte carlo returns [0,1] result for " << p << "\n";
}

void test_isfinite_checks() {
    BallisticSolver solver;
    float nan_val = std::nanf("");
    float inf_val = std::numeric_limits<float>::infinity();

    // Test solve() with NaN
    try {
        solver.solve(nan_val, 0.f, 0.f, 900.f, 0.f);
        assert(false && "Should have thrown runtime_error for NaN");
    } catch (const std::runtime_error& e) {
        std::cout << "PASS: Caught expected exception for NaN in solve(): " << e.what() << "\n";
    }

    // Test solve_kinetic() with Inf
    try {
        solver.solve_kinetic(100.f, inf_val, 900.f, 0.f);
        assert(false && "Should have thrown runtime_error for Inf");
    } catch (const std::runtime_error& e) {
        std::cout << "PASS: Caught expected exception for Inf in solve_kinetic(): " << e.what() << "\n";
    }

    // Test solve_drop() with NaN
    try {
        solver.solve_drop(100.f, nan_val, 0.f);
        assert(false && "Should have thrown runtime_error for NaN");
    } catch (const std::runtime_error& e) {
        std::cout << "PASS: Caught expected exception for NaN in solve_drop(): " << e.what() << "\n";
    }
}

void test_mode_selection_kinetic_for_shallow_target() {
    BallisticSolver solver;
    [[maybe_unused]] EngagementMode mode = solver.select_mode(1.5f, 10.f, 1.0f);
    assert(mode == EngagementMode::KINETIC);
    std::cout << "PASS: KINETIC mode selected for shallow elevation\n";
}

void test_mode_selection_drop_for_top_down() {
    BallisticSolver solver;
    [[maybe_unused]] EngagementMode mode = solver.select_mode(1.2f, 5.f, 2.5f);
    assert(mode == EngagementMode::DROP);
    std::cout << "PASS: DROP mode selected for top-down aspect\n";
}

// Test RK4 integration in vacuum (no drag) matches analytical solution
// Analytical: x(t) = v0*cos(theta)*t, z(t) = v0*sin(theta)*t - 0.5*g*t^2
void test_rk4_vacuum_trajectory() {
    BallisticSolver solver;
    
    // Test parameters: 45 degree launch, 100 m/s muzzle velocity
    const float muzzle_velocity = 100.f;
    const float launch_angle_rad = static_cast<float>(M_PI) / 4.f;  // 45 degrees
    const float air_density = 0.f;  // Vacuum: no drag
    const float cross_section = 1e-6f;  // Negligible
    const float mass = 1.f;  // Arbitrary
    const float max_distance = 2000.f;  // Allow full trajectory
    const float dt = 0.001f;  // 1ms time step
    
    auto trajectory = solver.simulate_trajectory(
        muzzle_velocity, launch_angle_rad,
        air_density, cross_section, mass,
        max_distance, dt);
    
    assert(trajectory.size() > 10);
    
    // Verify against analytical solution at several points (while projectile is in flight)
    for (size_t i = 0; i < trajectory.size() && trajectory[i].z >= 0.f; ++i) {
        const auto& pt = trajectory[i];
        float t = pt.time;
        
        // Analytical vacuum trajectory
        float expected_x = muzzle_velocity * std::cos(launch_angle_rad) * t;
        float expected_z = muzzle_velocity * std::sin(launch_angle_rad) * t - 0.5f * 9.81f * t * t;
        float expected_vx = muzzle_velocity * std::cos(launch_angle_rad);
        float expected_vz = muzzle_velocity * std::sin(launch_angle_rad) - 9.81f * t;
        
        // Allow 1% tolerance for RK4 numerical integration
        float pos_tolerance = 0.01f * std::max(std::abs(expected_x), std::abs(expected_z)) + 0.01f;
        float vel_tolerance = 0.01f * std::max(std::abs(expected_vx), std::abs(expected_vz)) + 0.01f;
        
        assert(std::abs(pt.x - expected_x) < pos_tolerance);
        assert(std::abs(pt.z - expected_z) < pos_tolerance);
        assert(std::abs(pt.vx - expected_vx) < vel_tolerance);
        assert(std::abs(pt.vz - expected_vz) < vel_tolerance);
        (void)pos_tolerance;  // Suppress unused warning
        (void)vel_tolerance;  // Suppress unused warning
    }
    
    // Verify range matches analytical: R = v^2 * sin(2*theta) / g
    float expected_range = muzzle_velocity * muzzle_velocity * std::sin(2.f * launch_angle_rad) / 9.81f;
    
    // Find impact point (last point where z >= 0)
    float impact_x = 0.f;
    for (size_t i = 1; i < trajectory.size(); ++i) {
        if (trajectory[i].z < 0.f) {
            // Interpolate to find exact impact
            float t = trajectory[i].z / (trajectory[i].z - trajectory[i-1].z);
            impact_x = trajectory[i-1].x + t * (trajectory[i].x - trajectory[i-1].x);
            break;
        }
    }
    
    float range_error = std::abs(impact_x - expected_range);
    assert(range_error < expected_range * 0.02f);  // Within 2%
    
    std::cout << "PASS: RK4 vacuum trajectory matches analytical (range error: " 
              << range_error << "m, expected: " << expected_range << "m)\n";
}

// Test G1 drag produces expected bullet drop
// At subsonic speeds (Mach < 0.8), Cd = 0.2
// Drag should cause additional drop compared to vacuum
void test_g1_drag_drop() {
    BallisticSolver solver;
    
    // Test parameters: 300 m/s muzzle velocity, slight upward angle
    // At 300 m/s, Mach = 300/343 = 0.87 (transonic, Cd = 0.4)
    const float muzzle_velocity = 300.f;
    const float launch_angle_rad = 0.05f;  // ~2.8 degrees upward
    const float air_density = 1.225f;  // Sea level
    const float cross_section = static_cast<float>(M_PI) * 0.00275f * 0.00275f;  // 5.5mm diameter
    const float mass = 0.004f;  // 4 grams
    const float max_distance = 1000.f;
    const float dt = 0.0005f;  // 0.5ms time step
    
    auto trajectory = solver.simulate_trajectory(
        muzzle_velocity, launch_angle_rad,
        air_density, cross_section, mass,
        max_distance, dt);
    
    assert(trajectory.size() > 10);
    
    // Find impact point (where z crosses 0)
    float impact_x = 0.f;
    float impact_vx = muzzle_velocity;
    
    for (size_t i = 1; i < trajectory.size(); ++i) {
        if (trajectory[i].z <= 0.f) {
            float t = trajectory[i].z / (trajectory[i].z - trajectory[i-1].z);
            impact_x = trajectory[i-1].x + t * (trajectory[i].x - trajectory[i-1].x);
            impact_vx = trajectory[i-1].vx + t * (trajectory[i].vx - trajectory[i-1].vx);
            break;
        }
    }
    
    assert(impact_x > 0.f);
    
    // Verify G1 drag is active (Cd should be in transonic range ~0.4)
    float mach_at_launch = muzzle_velocity / 343.f;
    float cd_at_launch = solver.get_drag_coefficient(mach_at_launch);
    assert(cd_at_launch == 0.4f);  // Transonic
    
    // Drag should reduce horizontal velocity significantly over the flight
    assert(impact_vx < muzzle_velocity);
    assert(impact_vx > muzzle_velocity * 0.5f);  // Should retain at least 50%
    
    std::cout << "PASS: G1 drag produces expected bullet drop\n";
    std::cout << "  Mach at launch: " << mach_at_launch << ", Cd: " << cd_at_launch << "\n";
    std::cout << "  Impact distance with drag: " << impact_x << "m\n";
    std::cout << "  Impact horizontal velocity: " << impact_vx << " m/s (initial: " << muzzle_velocity << " m/s)\n";
}

int main() {
    // Ballistic profile tests (AM7-L2-BALL-002)
    test_profile_validation_valid();
    test_profile_validation_invalid_velocity();
    test_profile_validation_invalid_bc();
    test_profile_load_from_json();
    test_profile_set_active();
    test_profile_load_invalid_skipped();
    test_profile_load_default_fallback();
    
    // Original ballistics tests
    test_kinetic_mode_tof_formula();
    test_drop_mode_requires_valid_arc();
    test_drop_mode_impossible_geometry();
    test_monte_carlo_p_hit_perfect_inputs();
    test_isfinite_checks();
    test_mode_selection_kinetic_for_shallow_target();
    test_mode_selection_drop_for_top_down();
    test_rk4_vacuum_trajectory();
    test_g1_drag_drop();
    std::cout << "\nAll ballistics tests passed.\n";
    return 0;
}
