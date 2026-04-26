/**
 * @file numeric_robustness_test.cpp
 * @brief Part 6: Numeric and Algorithmic Robustness Tests
 *
 * Tests BallisticSolver's handling of:
 * - Invalid inputs (NaN, Inf, negative/zero range, zero velocity)
 * - Output validity (finite, p_hit ∈ [0,1], tof > 0)
 * - G1 drag model correctness across Mach regimes
 * - BallisticProfile parameter validation
 * - solve() idempotency (no state mutation)
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>

#include "aurore/ballistic_solver.hpp"

namespace {

std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run.fetch_add(1); \
    try { \
        name(); \
        g_tests_passed.fetch_add(1); \
        std::cout << "  PASS: " << #name << std::endl; \
    } catch (const std::exception& e) { \
        g_tests_failed.fetch_add(1); \
        std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)
#define ASSERT_GT(a, b) do { if (!((a) > (b))) throw std::runtime_error("Assertion failed: " #a " > " #b); } while(0)
#define ASSERT_LT(a, b) do { if (!((a) < (b))) throw std::runtime_error("Assertion failed: " #a " < " #b); } while(0)
#define ASSERT_GE(a, b) do { if (!((a) >= (b))) throw std::runtime_error("Assertion failed: " #a " >= " #b); } while(0)
#define ASSERT_LE(a, b) do { if (!((a) <= (b))) throw std::runtime_error("Assertion failed: " #a " <= " #b); } while(0)

}  // anonymous namespace

// ============================================================================
// BallisticSolver Input Validation
// ============================================================================

TEST(test_solver_rejects_nan_range) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve(std::nanf(""), 0.0f, 0.0f, 340.0f, 0.0f);
    ASSERT_FALSE(result.has_value());
}

TEST(test_solver_rejects_inf_range) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve(std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 340.0f, 0.0f);
    ASSERT_FALSE(result.has_value());
}

TEST(test_solver_rejects_negative_range) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve(-1.0f, 0.0f, 0.0f, 340.0f, 0.0f);
    ASSERT_FALSE(result.has_value());
}

TEST(test_solver_rejects_zero_range) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve(0.0f, 0.0f, 0.0f, 340.0f, 0.0f);
    ASSERT_FALSE(result.has_value());
}

TEST(test_solver_rejects_nan_velocity) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve(100.0f, 0.0f, 0.0f, std::nanf(""), 0.0f);
    ASSERT_FALSE(result.has_value());
}

TEST(test_solver_rejects_nan_elevation) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve(100.0f, std::nanf(""), 0.0f, 340.0f, 0.0f);
    ASSERT_FALSE(result.has_value());
}

// ============================================================================
// BallisticSolver Output Validity
// ============================================================================

TEST(test_solver_result_all_finite) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve(5.0f, 0.0f, 0.0f, 340.0f, 0.0f);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::isfinite(result->az_lead_deg));
    ASSERT_TRUE(std::isfinite(result->el_lead_deg));
    ASSERT_TRUE(std::isfinite(result->p_hit));
    ASSERT_TRUE(std::isfinite(result->range_m));
}

TEST(test_solver_p_hit_bounded) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    for (float range : {0.5f, 1.0f, 3.0f, 5.0f, 8.0f, 10.0f}) {
        auto result = solver.solve(range, 0.0f, 0.0f, 340.0f, 0.0f);
        if (result.has_value()) {
            ASSERT_GE(result->p_hit, 0.0f);
            ASSERT_LE(result->p_hit, 1.0f);
        }
    }
}

TEST(test_solver_range_preserved_in_result) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    const float input_range = 5.0f;
    auto result = solver.solve(input_range, 0.0f, 0.0f, 340.0f, 0.0f);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->range_m, input_range);
}

TEST(test_solver_idempotent) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto r1 = solver.solve(5.0f, 0.0f, 0.0f, 340.0f, 0.0f);
    auto r2 = solver.solve(5.0f, 0.0f, 0.0f, 340.0f, 0.0f);

    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    ASSERT_EQ(r1->az_lead_deg, r2->az_lead_deg);
    ASSERT_EQ(r1->el_lead_deg, r2->el_lead_deg);
    ASSERT_EQ(r1->p_hit, r2->p_hit);
}

// ============================================================================
// solve_kinetic() Direct Tests
// ============================================================================

TEST(test_kinetic_tof_positive) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve_kinetic(5.0f, 0.0f, 340.0f, 0.0f);
    ASSERT_TRUE(result.has_value());
    ASSERT_GT(result->tof_s, 0.0f);
}

TEST(test_kinetic_rejects_negative_range) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve_kinetic(-1.0f, 0.0f, 340.0f, 0.0f);
    ASSERT_FALSE(result.has_value());
}

TEST(test_kinetic_rejects_zero_velocity) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve_kinetic(5.0f, 0.0f, 0.0f, 0.0f);
    ASSERT_FALSE(result.has_value());
}

TEST(test_kinetic_rejects_nan_inputs) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve_kinetic(std::nanf(""), 0.0f, 340.0f, 0.0f);
    ASSERT_FALSE(result.has_value());
}

TEST(test_kinetic_lead_angles_finite) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto result = solver.solve_kinetic(5.0f, 0.0f, 340.0f, 10.0f);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::isfinite(result->az_lead_deg));
    ASSERT_TRUE(std::isfinite(result->el_lead_deg));
}

// ============================================================================
// G1 Drag Model Correctness (public for testing per header comment)
// ============================================================================

TEST(test_drag_coefficient_subsonic) {
    aurore::BallisticSolver solver;

    // Mach 0.5 is subsonic (< 0.8) → Cd = 0.2
    float cd = solver.get_drag_coefficient(0.5f);
    ASSERT_EQ(cd, aurore::kCdSubsonic);
}

TEST(test_drag_coefficient_transonic) {
    aurore::BallisticSolver solver;

    // Mach 1.0 is transonic (0.8 < M < 1.2) → Cd = 0.4
    float cd = solver.get_drag_coefficient(1.0f);
    ASSERT_EQ(cd, aurore::kCdTransonic);
}

TEST(test_drag_coefficient_supersonic) {
    aurore::BallisticSolver solver;

    // Mach 1.5 is supersonic (1.2 < M < 2.5) → Cd = 0.25
    float cd = solver.get_drag_coefficient(1.5f);
    ASSERT_EQ(cd, aurore::kCdSupersonic);
}

TEST(test_drag_coefficient_hypersonic) {
    aurore::BallisticSolver solver;

    // Mach 3.0 is hypersonic (> 2.5) → Cd = 0.18
    float cd = solver.get_drag_coefficient(3.0f);
    ASSERT_EQ(cd, aurore::kCdHypersonic);
}

// ============================================================================
// BallisticProfile Validation
// ============================================================================

TEST(test_profile_valid_params_pass) {
    aurore::BallisticProfile profile;
    profile.muzzle_velocity_m_s  = 340.0f;
    profile.ballistic_coefficient = 0.3f;
    profile.sight_height_mm      = 50.0f;
    profile.zero_range_m         = 100.0f;

    ASSERT_TRUE(profile.validate());
}

TEST(test_profile_zero_muzzle_velocity_fails) {
    aurore::BallisticProfile profile;
    profile.muzzle_velocity_m_s  = 0.0f;
    profile.ballistic_coefficient = 0.3f;
    profile.sight_height_mm      = 50.0f;
    profile.zero_range_m         = 100.0f;

    ASSERT_FALSE(profile.validate());
}

TEST(test_profile_negative_bc_fails) {
    aurore::BallisticProfile profile;
    profile.muzzle_velocity_m_s  = 340.0f;
    profile.ballistic_coefficient = -0.1f;
    profile.sight_height_mm      = 50.0f;
    profile.zero_range_m         = 100.0f;

    ASSERT_FALSE(profile.validate());
}

TEST(test_profile_extreme_sight_height_fails) {
    aurore::BallisticProfile profile;
    profile.muzzle_velocity_m_s  = 340.0f;
    profile.ballistic_coefficient = 0.3f;
    profile.sight_height_mm      = 500.0f;  // > 200mm limit
    profile.zero_range_m         = 100.0f;

    ASSERT_FALSE(profile.validate());
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "=== Part 6: Numeric and Algorithmic Robustness Tests ===" << std::endl;
    std::cout << "Running ballistic solver robustness tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    std::cout << "\n--- Input Validation ---" << std::endl;
    RUN_TEST(test_solver_rejects_nan_range);
    RUN_TEST(test_solver_rejects_inf_range);
    RUN_TEST(test_solver_rejects_negative_range);
    RUN_TEST(test_solver_rejects_zero_range);
    RUN_TEST(test_solver_rejects_nan_velocity);
    RUN_TEST(test_solver_rejects_nan_elevation);

    std::cout << "\n--- Output Validity ---" << std::endl;
    RUN_TEST(test_solver_result_all_finite);
    RUN_TEST(test_solver_p_hit_bounded);
    RUN_TEST(test_solver_range_preserved_in_result);
    RUN_TEST(test_solver_idempotent);

    std::cout << "\n--- solve_kinetic() ---" << std::endl;
    RUN_TEST(test_kinetic_tof_positive);
    RUN_TEST(test_kinetic_rejects_negative_range);
    RUN_TEST(test_kinetic_rejects_zero_velocity);
    RUN_TEST(test_kinetic_rejects_nan_inputs);
    RUN_TEST(test_kinetic_lead_angles_finite);

    std::cout << "\n--- G1 Drag Model ---" << std::endl;
    RUN_TEST(test_drag_coefficient_subsonic);
    RUN_TEST(test_drag_coefficient_transonic);
    RUN_TEST(test_drag_coefficient_supersonic);
    RUN_TEST(test_drag_coefficient_hypersonic);

    std::cout << "\n--- BallisticProfile Validation ---" << std::endl;
    RUN_TEST(test_profile_valid_params_pass);
    RUN_TEST(test_profile_zero_muzzle_velocity_fails);
    RUN_TEST(test_profile_negative_bc_fails);
    RUN_TEST(test_profile_extreme_sight_height_fails);

    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}
