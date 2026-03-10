#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <cmath>

#include "aurore/ballistic_solver.hpp"

namespace {

// Test counters
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
    } \
    catch (const std::exception& e) { \
        g_tests_failed.fetch_add(1); \
        std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)
#define ASSERT_NEAR(a, b, tol) do { \
    if (std::abs((a) - (b)) > (tol)) \
        throw std::runtime_error("Assertion failed: " #a " not near " #b); \
} while(0)

#define ASSERT_THROWS(expr) do { \
    bool caught = false; \
    try { \
        expr; \
    } catch (const std::exception&) { \
        caught = true; \
    } \
    if (!caught) throw std::runtime_error("Expected exception not thrown: " #expr); \
} while(0)

}  // anonymous namespace

using namespace aurore;

// 46. LUT Initialization: Verify initialize_lookup_table populates all 4600 bins.
TEST(test_lut_initialization) {
    BallisticSolver solver;
    solver.initialize_lookup_table();
    
    float p1 = solver.get_p_hit_from_table(0.1f, 50.0f, true);
    float p2 = solver.get_p_hit_from_table(5.0f, 250.0f, true);
    float p3 = solver.get_p_hit_from_table(10.0f, 500.0f, true);
    
    ASSERT_TRUE(p1 > 0.0f);
    ASSERT_TRUE(p2 > 0.0f);
    ASSERT_TRUE(p3 > 0.0f);
}

// 47. LUT Clamping
TEST(test_lut_clamping) {
    BallisticSolver solver;
    solver.initialize_lookup_table();
    
    float p_10m = solver.get_p_hit_from_table(10.0f, 250.0f, true);
    float p_11m = solver.get_p_hit_from_table(11.0f, 250.0f, true);
    ASSERT_EQ(p_11m, p_10m);
}

// 48. Mach Transitions: Verify get_drag_coefficient at Mach 0.8, 1.2, and 2.5.
TEST(test_mach_transitions) {
    BallisticSolver solver;
    ASSERT_NEAR(solver.get_drag_coefficient(0.5f), 0.2f, 0.001f);
    // Code uses <= 0.8 for subsonic, so 0.8 is still 0.2
    ASSERT_NEAR(solver.get_drag_coefficient(0.8f), 0.2f, 0.001f);
    ASSERT_NEAR(solver.get_drag_coefficient(0.81f), 0.4f, 0.001f);
    // <= 1.2 for transonic, so 1.2 is still 0.4
    ASSERT_NEAR(solver.get_drag_coefficient(1.2f), 0.4f, 0.001f);
    ASSERT_NEAR(solver.get_drag_coefficient(1.21f), 0.25f, 0.001f);
    // <= 2.5 for supersonic, so 2.5 is still 0.25
    ASSERT_NEAR(solver.get_drag_coefficient(2.5f), 0.25f, 0.001f);
    ASSERT_NEAR(solver.get_drag_coefficient(2.51f), 0.18f, 0.001f);
}

// 49. Vacuum Match
TEST(test_vacuum_match) {
    BallisticSolver solver;
    Rk4State state = {0, 0, 0, 100.0f, 0, 100.0f}; 
    float dt = 0.01f;
    float air_density = 0.0f;
    float area = 0.01f;
    float mass = 1.0f;
    
    for (int i = 0; i < 100; ++i) {
        state = solver.rk4_step(state, dt, air_density, area, mass);
    }
    
    ASSERT_NEAR(state.x, 100.0f, 0.1f);
    ASSERT_NEAR(state.z, 95.095f, 0.1f);
}

// 50. NaN/Inf Solve: Verify solve() throws on non-finite inputs.
TEST(test_nan_inf_solve) {
    BallisticSolver solver;
    ASSERT_THROWS(solver.solve(NAN, 0, 0, 900.0f));
    ASSERT_THROWS(solver.solve(100.0f, INFINITY, 0, 900.0f));
}

// 60. Profile Fallback
TEST(test_profile_fallback) {
    BallisticSolver solver;
    nlohmann::json empty_config = nlohmann::json::array();
    solver.loadProfiles(empty_config);
    
    const auto* profile = solver.getActiveProfile();
    ASSERT_TRUE(profile != nullptr);
    ASSERT_EQ(profile->name, "Default");
}

int main() {
    std::cout << "Running Ballistics Stress tests..." << std::endl;
    RUN_TEST(test_lut_initialization);
    RUN_TEST(test_lut_clamping);
    RUN_TEST(test_mach_transitions);
    RUN_TEST(test_vacuum_match);
    RUN_TEST(test_nan_inf_solve);
    RUN_TEST(test_profile_fallback);
    
    std::cout << "Tests run: " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed: " << g_tests_passed.load() << std::endl;
    return g_tests_failed.load() > 0 ? 1 : 0;
}
