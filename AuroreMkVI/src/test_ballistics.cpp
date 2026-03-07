#include <cmath>
#include <iostream>
#include <vector>
#include <atomic>
#include <chrono>
#include <string>

#include "timing.h"

static int tests_passed = 0;
static int tests_failed = 0;

constexpr float GRAVITY_CONST = 9.81f;
constexpr float PI = 3.14159265358979323846f;

struct Vec3 {
    float x, y, z;
    Vec3(float x_=0, float y_=0, float z_=0) : x(x_), y(y_), z(z_) {}
    float magnitude() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3 operator+(const Vec3& other) const { return Vec3(x + other.x, y + other.y, z + other.z); }
    Vec3 operator-(const Vec3& other) const { return Vec3(x - other.x, y - other.y, z - other.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
};

struct BallisticProfile {
    float muzzle_velocity_mps;
    float bullet_mass_kg;
    float ballistic_coefficient_si;
    float air_pressure_pa;
    float temperature_c;
    float sight_height_m;
    float zero_distance_m;
};

struct BallisticState {
    Vec3 position;
    Vec3 velocity;
};

class BallisticsSolver {
public:
    explicit BallisticsSolver(const BallisticProfile& profile) : profile_(profile) {
        zero_pitch_rad_ = calculate_zero_pitch();
    }

    float get_air_density() const {
        constexpr float R_DRY_AIR = 287.058f;
        float temp_kelvin = profile_.temperature_c + 273.15f;
        return profile_.air_pressure_pa / (R_DRY_AIR * temp_kelvin);
    }

    Vec3 drag_force(const Vec3& velocity, float air_density) {
        float v = velocity.magnitude();
        if (v < 1e-6) return Vec3(0, 0, 0);

        float cd = 0.0f;
        if (v <= 200.0f) {
            cd = 0.25f + (0.35f - 0.25f) * (v / 200.0f);
        } else if (v <= 400.0f) {
            cd = 0.35f + (0.28f - 0.35f) * ((v - 200.0f) / 200.0f);
        } else if (v <= 800.0f) {
            cd = 0.28f + (0.20f - 0.28f) * ((v - 400.0f) / 400.0f);
        } else {
            cd = 0.20f;
        }

        if (profile_.ballistic_coefficient_si <= 0.0f) {
            return Vec3(0, 0, 0);
        }

        float drag_magnitude = 0.5f * air_density * v * v * profile_.ballistic_coefficient_si * cd;
        return velocity * (-drag_magnitude / v);
    }

    BallisticState derivatives(const BallisticState& state, float air_density) {
        Vec3 gravitational_force = Vec3(0, -GRAVITY_CONST * profile_.bullet_mass_kg, 0);
        Vec3 drag = drag_force(state.velocity, air_density);
        Vec3 total_force = Vec3(gravitational_force.x + drag.x,
                                 gravitational_force.y + drag.y,
                                 gravitational_force.z + drag.z);
        Vec3 acceleration = total_force * (1.0f / profile_.bullet_mass_kg);

        return {{state.velocity}, {acceleration}};
    }

    BallisticState rk4_step(const BallisticState& state, float dt, float air_density) {
        BallisticState k1 = derivatives(state, air_density);
        BallisticState k2 = derivatives({state.position + k1.position * (dt / 2.0f), state.velocity + k1.velocity * (dt / 2.0f)}, air_density);
        BallisticState k3 = derivatives({state.position + k2.position * (dt / 2.0f), state.velocity + k2.velocity * (dt / 2.0f)}, air_density);
        BallisticState k4 = derivatives({state.position + k3.position * dt, state.velocity + k3.velocity * dt}, air_density);

        Vec3 pos_next = state.position + (k1.position + k2.position * 2.0f + k3.position * 2.0f + k4.position) * (dt / 6.0f);
        Vec3 vel_next = state.velocity + (k1.velocity + k2.velocity * 2.0f + k3.velocity * 2.0f + k4.velocity) * (dt / 6.0f);

        return {pos_next, vel_next};
    }

    std::vector<BallisticState> calculate_trajectory(float initial_pitch, float max_distance, float time_step_override = 0.0f) {
        std::vector<BallisticState> trajectory;
        float air_density = get_air_density();

        float actual_time_step = time_step_override;
        if (actual_time_step == 0.0f) {
            float target_steps = 500.0f;
            actual_time_step = max_distance / (profile_.muzzle_velocity_mps * target_steps);
            actual_time_step = std::max(0.0001f, std::min(0.1f, actual_time_step));
        }

        BallisticState current_state;
        current_state.position = Vec3(0, -profile_.sight_height_m, 0);
        current_state.velocity = Vec3(
            profile_.muzzle_velocity_mps * std::cos(initial_pitch),
            profile_.muzzle_velocity_mps * std::sin(initial_pitch),
            0.0f
        );
        trajectory.push_back(current_state);

        while (current_state.position.x < max_distance && current_state.position.y >= -0.5f) {
            current_state = rk4_step(current_state, actual_time_step, air_density);
            trajectory.push_back(current_state);

            if (current_state.position.x > max_distance || current_state.velocity.x <= 0) break;
            if (trajectory.size() > 5000) break;
            if (!std::isfinite(current_state.position.x) || !std::isfinite(current_state.position.y)) break;
        }
        return trajectory;
    }

    float calculate_zero_pitch() {
        float time_of_flight = profile_.zero_distance_m / profile_.muzzle_velocity_mps;
        float drop_at_zero = 0.5f * GRAVITY_CONST * time_of_flight * time_of_flight;
        float initial_angle_estimate = std::atan2(drop_at_zero - profile_.sight_height_m, profile_.zero_distance_m);

        float low_angle_rad = initial_angle_estimate - 0.1f;
        float high_angle_rad = initial_angle_estimate + 0.1f;

        if (high_angle_rad <= low_angle_rad) {
            low_angle_rad = initial_angle_estimate - 0.2f;
            high_angle_rad = initial_angle_estimate + 0.2f;
        }

        low_angle_rad = std::max(-0.5f, low_angle_rad);
        high_angle_rad = std::min(0.5f, high_angle_rad);

        constexpr int max_iterations = 50;
        constexpr float tolerance_m = 0.01f;

        for (int i = 0; i < max_iterations; ++i) {
            float mid_angle_rad = (low_angle_rad + high_angle_rad) / 2.0f;
            auto trajectory = calculate_trajectory(mid_angle_rad, profile_.zero_distance_m);

            if (trajectory.empty()) break;

            float y_at_zero = -999.0f;
            for (const auto& state : trajectory) {
                if (state.position.x >= profile_.zero_distance_m - 1.0f) {
                    y_at_zero = state.position.y;
                    break;
                }
            }

            if (y_at_zero > -profile_.sight_height_m + tolerance_m) {
                high_angle_rad = mid_angle_rad;
            } else {
                low_angle_rad = mid_angle_rad;
            }
        }

        return (low_angle_rad + high_angle_rad) / 2.0f;
    }

private:
    BallisticProfile profile_;
    float zero_pitch_rad_;
};

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
static void trajectory_stability();
static void target_distance_estimation();
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

    float air_density = solver.get_air_density();
    ASSERT_TRUE(air_density > 1.0f && air_density < 1.3f);

    BallisticState state;
    state.position = Vec3(0, 0, 0);
    state.velocity = Vec3(800, 80, 0);

    Vec3 drag = solver.drag_force(state.velocity, air_density);
    ASSERT_TRUE(std::isfinite(drag.x));
    ASSERT_TRUE(std::isfinite(drag.y));

    ASSERT_TRUE(solver.calculate_trajectory(0.0f, 100.0f).size() > 0);
    ASSERT_TRUE(solver.calculate_trajectory(0.1f, 200.0f).size() > 0);
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
    auto trajectory = solver.calculate_trajectory(0.1f, 200.0f);

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
    auto trajectory = solver.calculate_trajectory(0.1f, 500.0f);

    ASSERT_TRUE(trajectory.size() < 5000);
    ASSERT_TRUE(trajectory.size() > 1);

    for (const auto& state : trajectory) {
        ASSERT_TRUE(std::isfinite(state.position.x));
        ASSERT_TRUE(std::isfinite(state.position.y));
        ASSERT_TRUE(std::isfinite(state.velocity.x));
        ASSERT_TRUE(std::isfinite(state.velocity.y));
    }
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

void hit_streak_calculation() {
    constexpr int MIN_STABLE_HIT_STREAK = 5;

    auto is_track_stable = [&](int consecutive_hits) -> bool {
        return consecutive_hits >= MIN_STABLE_HIT_STREAK;
    };

    ASSERT_FALSE(is_track_stable(4));

    ASSERT_TRUE(is_track_stable(5));
    ASSERT_TRUE(is_track_stable(10));
}

void predictive_aim_calculation() {
    float target_x = 100.0f;
    float target_velocity_x = 5.0f;
    float flight_time_s = 0.2f;

    auto predict_aim = [&](float tx, float vx, float t) -> float {
        return tx + vx * t;
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
    RUN_TEST(hit_streak_calculation);
    RUN_TEST(predictive_aim_calculation);

    std::cout << "\n=== Test Results ===\n";
    std::cout << "Passed: " << tests_passed << "\n";
    std::cout << "Failed: " << tests_failed << "\n";

    return tests_failed > 0 ? 1 : 0;
}
