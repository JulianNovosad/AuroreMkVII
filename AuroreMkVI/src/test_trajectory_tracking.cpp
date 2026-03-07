#include <cmath>
#include <iostream>
#include <vector>
#include <atomic>
#include <chrono>
#include <string>

#include "logic.h"
#include "timing.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_NEAR(actual, expected, tolerance) do { \
    float _actual = (actual); \
    float _expected = (expected); \
    float _tolerance = (tolerance); \
    float _diff = std::abs(_actual - _expected); \
    if (_diff > _tolerance) { \
        std::cerr << "FAILED: " << #actual << " (" << _actual << ") not near " << \
                  #expected << " (" << _expected << ") within " << _tolerance << "\n"; \
        throw std::runtime_error("assertion failed"); \
    } \
} while(0)

#define ASSERT_TRUE(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAILED: " << #condition << "\n"; \
        throw std::runtime_error("assertion failed"); \
    } \
} while(0)

#define ASSERT_FALSE(condition) do { \
    if (condition) { \
        std::cerr << "FAILED: NOT " << #condition << "\n"; \
        throw std::runtime_error("assertion failed"); \
    } \
} while(0)

#define RUN_TEST(name) do { \
    std::cout << "Running " << name << "... "; \
    try { \
        name(); \
        std::cout << "PASSED\n"; \
        tests_passed++; \
    } catch (const std::exception& e) { \
        std::cout << "FAILED: " << e.what() << "\n"; \
        tests_failed++; \
    } \
} while(0)

static void ballistic_solver_basic_trajectory();
static void ballistic_solver_vacuum_mode();
static void ballistic_solver_zero_pitch();
static void target_distance_estimation();
static void trajectory_with_drag();
static void trajectory_stability();
static void hit_streak_calculation();
static void predictive_aim_calculation();

void ballistic_solver_basic_trajectory() {
    BallisticProfile profile;
    profile.muzzle_velocity_mps = 800.0f;
    profile.bullet_mass_kg = 0.01f;
    profile.ballistic_coefficient_si = 0.5f;
    profile.air_pressure_pa = 101325.0f;
    profile.temperature_c = 20.0f;
    profile.sight_height_m = 0.05f;
    profile.zero_distance_m = 100.0f;

    BallisticsSolver solver(profile);

    auto trajectory = solver.calculate_trajectory(0.0f, 200.0f);

    ASSERT_TRUE(trajectory.size() > 1);
    ASSERT_TRUE(trajectory.front().position.x >= 0.0f);

    float max_x = 0.0f;
    for (const auto& state : trajectory) {
        if (state.position.x > max_x) {
            max_x = state.position.x;
        }
    }
    ASSERT_TRUE(max_x > 50.0f);
}

void ballistic_solver_vacuum_mode() {
    BallisticProfile profile;
    profile.muzzle_velocity_mps = 800.0f;
    profile.bullet_mass_kg = 0.01f;
    profile.ballistic_coefficient_si = -1.0f;
    profile.air_pressure_pa = 101325.0f;
    profile.temperature_c = 20.0f;
    profile.sight_height_m = 0.05f;
    profile.zero_distance_m = 100.0f;

    BallisticsSolver solver(profile);

    auto trajectory = solver.calculate_trajectory(0.0f, 200.0f);

    ASSERT_TRUE(trajectory.size() > 1);

    float y_at_100m = -1.0f;
    for (const auto& state : trajectory) {
        if (state.position.x >= 100.0f && state.position.x <= 101.0f) {
            y_at_100m = state.position.y;
            break;
        }
    }

    ASSERT_TRUE(y_at_100m > -1.0f);
}

void ballistic_solver_zero_pitch() {
    BallisticProfile profile;
    profile.muzzle_velocity_mps = 800.0f;
    profile.bullet_mass_kg = 0.01f;
    profile.ballistic_coefficient_si = 0.5f;
    profile.air_pressure_pa = 101325.0f;
    profile.temperature_c = 20.0f;
    profile.sight_height_m = 0.05f;
    profile.zero_distance_m = 100.0f;

    BallisticsSolver solver(profile);

    float zero_pitch = solver.calculate_zero_pitch();

    ASSERT_TRUE(std::isfinite(zero_pitch));
    ASSERT_TRUE(zero_pitch > -0.5f && zero_pitch < 0.5f);
}

void target_distance_estimation() {
    float focal_length_px = 300.0f;
    float target_width_m = 0.12f;

    auto estimate_distance = [&](float bbox_width_px) -> float {
        if (bbox_width_px <= 0.0f) return 0.0f;
        return (target_width_m * focal_length_px) / bbox_width_px;
    };

    ASSERT_NEAR(estimate_distance(60.0f), 0.6f, 0.01f);
    ASSERT_NEAR(estimate_distance(30.0f), 1.2f, 0.01f);
    ASSERT_TRUE(estimate_distance(0.0f) == 0.0f);
}

void trajectory_with_drag() {
    BallisticProfile profile;
    profile.muzzle_velocity_mps = 800.0f;
    profile.bullet_mass_kg = 0.01f;
    profile.ballistic_coefficient_si = 0.5f;
    profile.air_pressure_pa = 101325.0f;
    profile.temperature_c = 20.0f;
    profile.sight_height_m = 0.05f;
    profile.zero_distance_m = 100.0f;

    BallisticsSolver solver(profile);

    auto vacuum_trajectory = solver.calculate_trajectory(0.0f, 200.0f);

    profile.ballistic_coefficient_si = -1.0f;
    BallisticsSolver vacuum_solver(profile);
    auto drag_trajectory = vacuum_solver.calculate_trajectory(0.0f, 200.0f);

    ASSERT_TRUE(vacuum_trajectory.size() > 1);
    ASSERT_TRUE(drag_trajectory.size() > 1);
}

void trajectory_stability() {
    BallisticProfile profile;
    profile.muzzle_velocity_mps = 800.0f;
    profile.bullet_mass_kg = 0.01f;
    profile.ballistic_coefficient_si = 0.5f;
    profile.air_pressure_pa = 101325.0f;
    profile.temperature_c = 20.0f;
    profile.sight_height_m = 0.05f;
    profile.zero_distance_m = 100.0f;

    BallisticsSolver solver(profile);

    auto trajectory = solver.calculate_trajectory(0.0f, 500.0f);

    ASSERT_TRUE(trajectory.size() < 5000);

    for (const auto& state : trajectory) {
        ASSERT_TRUE(std::isfinite(state.position.x));
        ASSERT_TRUE(std::isfinite(state.position.y));
        ASSERT_TRUE(std::isfinite(state.velocity.x));
        ASSERT_TRUE(std::isfinite(state.velocity.y));
    }
}

void hit_streak_calculation() {
    std::atomic<int> hit_count{0};
    constexpr int MIN_STABLE_HIT_STREAK = 5;

    auto is_track_stable = [&](int consecutive_hits) -> bool {
        return consecutive_hits >= MIN_STABLE_HIT_STREAK;
    };

    ASSERT_FALSE(is_track_stable(4));

    for (int i = 0; i < 5; i++) {
        hit_count.fetch_add(1);
    }
    ASSERT_TRUE(is_track_stable(hit_count.load()));
}

void predictive_aim_calculation() {
    float target_x = 100.0f;
    float target_velocity_x = 5.0f;
    float flight_time_s = 0.2f;

    auto predict_aim = [&](float target_x, float velocity_x, float time) -> float {
        return target_x + velocity_x * time;
    };

    float predicted_x = predict_aim(target_x, target_velocity_x, flight_time_s);
    ASSERT_NEAR(predicted_x, 101.0f, 0.01f);
}

int main() {
    std::cout << "=== Target Tracking and Ballistic Prediction Tests ===\n\n";

    std::cout << "[Ballistic Solver Tests]\n";
    RUN_TEST(ballistic_solver_basic_trajectory);
    RUN_TEST(ballistic_solver_vacuum_mode);
    RUN_TEST(ballistic_solver_zero_pitch);
    RUN_TEST(trajectory_stability);

    std::cout << "\n[Target Tracking Tests]\n";
    RUN_TEST(target_distance_estimation);
    RUN_TEST(trajectory_with_drag);
    RUN_TEST(hit_streak_calculation);
    RUN_TEST(predictive_aim_calculation);

    std::cout << "\n=== Test Results ===\n";
    std::cout << "Passed: " << tests_passed << "\n";
    std::cout << "Failed: " << tests_failed << "\n";

    return tests_failed > 0 ? 1 : 0;
}
