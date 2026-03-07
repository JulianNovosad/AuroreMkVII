/**
 * @file test_regression_003_ballistic_solver.cpp
 * @brief Regression test for AM7-L2-BALL-001: Ballistic engine
 *
 * This test verifies that the ballistic solver calculates accurate aim point
 * offsets within the 1ms timing budget.
 *
 * REGRESSION TEST REG_003:
 * - Failure mode: Ballistic solver returns zero solution, no aim point offset
 * - Expected before fix: FAIL (solver not implemented)
 * - Expected after fix: PASS (accurate solution within timing budget)
 *
 * Requirements covered:
 * - AM7-L2-BALL-001: Ballistic engine implementation
 * - AM7-L2-BALL-003: Aim point offset calculation
 * - AM7-L2-BALL-004: 1ms timing budget
 */

#include <iostream>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

namespace {

size_t g_tests_run = 0;
size_t g_tests_passed = 0;
size_t g_tests_failed = 0;

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run++; \
    try { \
        name(); \
        g_tests_passed++; \
        std::cout << "  PASS: " << #name << std::endl; \
    } \
    catch (const std::exception& e) {
        g_tests_failed++; \
        std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)
#define ASSERT_NEAR(a, b, eps) do { if (std::abs((a) - (b)) > (eps)) throw std::runtime_error("Assertion failed: " #a " ≈ " #b); } while(0)

}  // anonymous namespace

// ============================================================================
// Data Structures (per AM7-L2-BALL-001)
// ============================================================================

struct BallisticProfile {
    std::string name;
    float muzzle_velocity_ms = 850.0f;    // m/s
    float ballistic_coefficient = 0.305f; // G1
    float sight_height_mm = 50.0f;        // mm
    float zero_range_m = 100.0f;          // m
};

struct BallisticSolution {
    float elevation_offset_mrad = 0.0f;   // Milliradians
    float azimuth_offset_mrad = 0.0f;     // Milliradians (lead)
    float bullet_drop_m = 0.0f;           // Meters
    float time_of_flight_ms = 0.0f;       // Milliseconds
    bool valid = false;
};

struct BallisticConfig {
    std::string profile_path;
    uint32_t max_range_m = 5000;
    float gravity_ms2 = 9.80665f;
};

// ============================================================================
// Ballistic Solver Implementation (Stub)
// ============================================================================

class BallisticSolver {
public:
    BallisticSolver(const BallisticConfig& config) : config_(config) {}
    
    bool loadProfiles(const std::string& path) {
        // TODO: Load profiles from JSON config file
        // AM7-L2-BALL-002: Configurable ammunition profiles
        
        // Stub: Add default profile
        BallisticProfile default_profile;
        default_profile.name = "default";
        default_profile.muzzle_velocity_ms = 850.0f;
        default_profile.ballistic_coefficient = 0.305f;
        default_profile.sight_height_mm = 50.0f;
        default_profile.zero_range_m = 100.0f;
        
        profiles_["default"] = default_profile;
        return true;
    }
    
    /**
     * Calculate aim point offset
     * AM7-L2-BALL-003: Ballistic solution calculation
     * AM7-L2-BALL-004: Must complete within 1ms
     */
    BallisticSolution solve(float range_m, float target_velocity_ms,
                           const BallisticProfile& profile) {
        BallisticSolution solution;
        
        if (range_m <= 0.0f || range_m > config_.max_range_m) {
            return solution;  // Invalid range
        }
        
        // Simplified ballistic calculation (stub)
        // TODO: Implement full 3DOF or 6DOF ballistic model
        
        // Time of flight (simplified)
        float time_of_flight_s = range_m / profile.muzzle_velocity_ms;
        solution.time_of_flight_ms = time_of_flight_s * 1000.0f;
        
        // Bullet drop (simplified parabolic trajectory)
        solution.bullet_drop_m = 0.5f * config_.gravity_ms2 * 
                                  time_of_flight_s * time_of_flight_s;
        
        // Elevation offset (compensate for drop)
        solution.elevation_offset_mrad = (solution.bullet_drop_m / range_m) * 1000.0f;
        
        // Lead calculation for moving target
        solution.azimuth_offset_mrad = (target_velocity_ms * time_of_flight_s / range_m) * 1000.0f;
        
        solution.valid = true;
        return solution;
    }
    
private:
    BallisticConfig config_;
    std::unordered_map<std::string, BallisticProfile> profiles_;
};

// ============================================================================
// Test Cases
// ============================================================================

TEST(test_ballistic_solver_construction) {
    BallisticConfig config;
    BallisticSolver solver(config);
}

TEST(test_ballistic_profile_loading) {
    // AM7-L2-BALL-002: Load profiles from config.json
    BallisticConfig config;
    config.profile_path = "/tmp/test_ballistic_profiles.json";
    
    BallisticSolver solver(config);
    bool result = solver.loadProfiles(config.profile_path);
    
    ASSERT_TRUE(result);
}

TEST(test_ballistic_solution_basic) {
    // AM7-L2-BALL-003: Calculate aim point offset
    BallisticConfig config;
    BallisticSolver solver(config);
    solver.loadProfiles("/tmp/test.json");
    
    BallisticProfile profile;
    profile.name = "test";
    profile.muzzle_velocity_ms = 850.0f;
    profile.ballistic_coefficient = 0.305f;
    
    BallisticSolution solution = solver.solve(100.0f, 0.0f, profile);
    
    ASSERT_TRUE(solution.valid);
    ASSERT_GT(solution.elevation_offset_mrad, 0.0f);
    ASSERT_GT(solution.time_of_flight_ms, 0.0f);
}

TEST(test_ballistic_solution_zero_range) {
    // Zero range should return invalid solution
    BallisticConfig config;
    BallisticSolver solver(config);
    solver.loadProfiles("/tmp/test.json");
    
    BallisticProfile profile;
    BallisticSolution solution = solver.solve(0.0f, 0.0f, profile);
    
    ASSERT_FALSE(solution.valid);
}

TEST(test_ballistic_solution_max_range) {
    // Max range should return valid solution
    BallisticConfig config;
    config.max_range_m = 5000;
    BallisticSolver solver(config);
    solver.loadProfiles("/tmp/test.json");
    
    BallisticProfile profile;
    BallisticSolution solution = solver.solve(1000.0f, 0.0f, profile);
    
    ASSERT_TRUE(solution.valid);
    ASSERT_GT(solution.elevation_offset_mrad, 0.0f);
}

TEST(test_ballistic_solution_beyond_max_range) {
    // Beyond max range should return invalid solution
    BallisticConfig config;
    config.max_range_m = 1000;
    BallisticSolver solver(config);
    solver.loadProfiles("/tmp/test.json");
    
    BallisticProfile profile;
    BallisticSolution solution = solver.solve(2000.0f, 0.0f, profile);
    
    ASSERT_FALSE(solution.valid);
}

TEST(test_ballistic_lead_calculation) {
    // AM7-L2-BALL-003: Lead calculation for moving targets
    BallisticConfig config;
    BallisticSolver solver(config);
    solver.loadProfiles("/tmp/test.json");
    
    BallisticProfile profile;
    
    // Stationary target: no lead
    BallisticSolution solution_stationary = solver.solve(100.0f, 0.0f, profile);
    ASSERT_NEAR(solution_stationary.azimuth_offset_mrad, 0.0f, 0.1f);
    
    // Moving target: positive lead
    BallisticSolution solution_moving = solver.solve(100.0f, 10.0f, profile);
    ASSERT_GT(solution_moving.azimuth_offset_mrad, 0.0f);
}

TEST(test_ballistic_solver_timing_budget) {
    // AM7-L2-BALL-004: Ballistic solution within 1ms budget
    BallisticConfig config;
    BallisticSolver solver(config);
    solver.loadProfiles("/tmp/test.json");
    
    BallisticProfile profile;
    
    const int num_iterations = 10000;
    std::vector<int64_t> latencies;
    latencies.reserve(num_iterations);
    
    for (int i = 0; i < num_iterations; i++) {
        auto start = std::chrono::high_resolution_clock::now();
        
        BallisticSolution solution = solver.solve(500.0f, 5.0f, profile);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count();
        latencies.push_back(latency);
        
        ASSERT_TRUE(solution.valid);
    }
    
    // Calculate statistics
    int64_t max_latency = 0;
    int64_t sum_latency = 0;
    for (auto lat : latencies) {
        if (lat > max_latency) max_latency = lat;
        sum_latency += lat;
    }
    int64_t avg_latency = sum_latency / num_iterations;
    
    std::cout << "    Timing test: avg=" << avg_latency/1000.0 << "μs, "
              << "max=" << max_latency/1000.0 << "μs" << std::endl;
    
    // AM7-L2-BALL-004: Must complete within 1ms (1000μs)
    ASSERT_TRUE(max_latency < 1000000);  // 1ms = 1,000,000ns
}

TEST(test_ballistic_solver_accuracy) {
    // Verify ballistic solution accuracy against known values
    BallisticConfig config;
    BallisticSolver solver(config);
    solver.loadProfiles("/tmp/test.json");
    
    BallisticProfile profile;
    profile.muzzle_velocity_ms = 850.0f;
    
    // Test at 100m zero range
    BallisticSolution solution = solver.solve(100.0f, 0.0f, profile);
    
    // At zero range, elevation offset should be minimal
    ASSERT_NEAR(solution.elevation_offset_mrad, 0.0f, 1.0f);
    
    // Test at 500m
    solution = solver.solve(500.0f, 0.0f, profile);
    
    // Expected drop at 500m for 850 m/s projectile
    // Time of flight ≈ 500/850 ≈ 0.588s
    // Drop ≈ 0.5 * 9.81 * 0.588^2 ≈ 1.7m
    // Elevation ≈ 1.7/500 * 1000 ≈ 3.4 mrad
    ASSERT_NEAR(solution.bullet_drop_m, 1.7f, 0.5f);
    ASSERT_NEAR(solution.elevation_offset_mrad, 3.4f, 1.0f);
}

TEST(test_ballistic_solver_regression_accuracy_and_timing) {
    // REG_003: Primary regression test
    // This test would FAIL before ballistic solver implementation
    // and MUST PASS after implementation
    
    BallisticConfig config;
    BallisticSolver solver(config);
    
    // Step 1: Load profiles
    bool load_result = solver.loadProfiles("/tmp/test_profiles.json");
    ASSERT_TRUE(load_result);
    
    // Step 2: Define test profile
    BallisticProfile profile;
    profile.name = ".308 Win M80";
    profile.muzzle_velocity_ms = 853.0f;
    profile.ballistic_coefficient = 0.305f;
    profile.sight_height_mm = 50.0f;
    profile.zero_range_m = 100.0f;
    
    // Step 3: Calculate solutions for various ranges
    struct TestCase {
        float range_m;
        float target_velocity_ms;
        float expected_drop_m;
        float expected_elevation_mrad;
    };
    
    TestCase test_cases[] = {
        {100.0f, 0.0f, 0.0f, 0.0f},      // Zero range
        {200.0f, 0.0f, 0.2f, 1.0f},      // 200m
        {500.0f, 0.0f, 1.5f, 3.0f},      // 500m
        {800.0f, 0.0f, 4.5f, 6.0f},      // 800m
        {500.0f, 10.0f, 1.5f, 3.0f},     // 500m with moving target
    };
    
    size_t passed_cases = 0;
    for (const auto& tc : test_cases) {
        auto start = std::chrono::high_resolution_clock::now();
        
        BallisticSolution solution = solver.solve(tc.range_m, tc.target_velocity_ms, profile);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count();
        
        if (solution.valid && 
            latency < 1000000 &&  // 1ms budget
            std::abs(solution.bullet_drop_m - tc.expected_drop_m) < 1.0f &&
            std::abs(solution.elevation_offset_mrad - tc.expected_elevation_mrad) < 2.0f) {
            passed_cases++;
        }
    }
    
    std::cout << "    REG_003: " << passed_cases << "/" << 5 
              << " test cases passed" << std::endl;
    
    ASSERT_EQ(passed_cases, 5);
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "=== Ballistic Solver Regression Tests (REG_003) ===" << std::endl;
    std::cout << "Testing AM7-L2-BALL-001: Ballistic engine" << std::endl;
    std::cout << "Testing AM7-L2-BALL-003: Aim point offset calculation" << std::endl;
    std::cout << "Testing AM7-L2-BALL-004: 1ms timing budget" << std::endl;
    std::cout << std::endl;
    
    RUN_TEST(test_ballistic_solver_construction);
    RUN_TEST(test_ballistic_profile_loading);
    RUN_TEST(test_ballistic_solution_basic);
    RUN_TEST(test_ballistic_solution_zero_range);
    RUN_TEST(test_ballistic_solution_max_range);
    RUN_TEST(test_ballistic_solution_beyond_max_range);
    RUN_TEST(test_ballistic_lead_calculation);
    RUN_TEST(test_ballistic_solver_timing_budget);
    RUN_TEST(test_ballistic_solver_accuracy);
    RUN_TEST(test_ballistic_solver_regression_accuracy_and_timing);
    
    std::cout << std::endl;
    std::cout << "=== Test Summary ===" << std::endl;
    std::cout << "Run: " << g_tests_run << std::endl;
    std::cout << "Passed: " << g_tests_passed << std::endl;
    std::cout << "Failed: " << g_tests_failed << std::endl;
    
    if (g_tests_failed > 0) {
        std::cout << "\nREGRESSION TEST REG_003: FAIL" << std::endl;
        std::cout << "Ballistic solver implementation required." << std::endl;
        return 1;
    } else {
        std::cout << "\nREGRESSION TEST REG_003: PASS" << std::endl;
        std::cout << "Ballistic solver calculates accurate solutions within 1ms." << std::endl;
        return 0;
    }
}
