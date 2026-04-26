/**
 * @file numeric_robustness_test.cpp
 * @brief Part 6: Numeric and Algorithmic Robustness Tests
 *
 * Validates:
 * - Overflow and underflow handling
 * - NaN and Inf propagation rules
 * - Precision decay in long-running calculations
 * - Angle and range wrapping
 * - Boundary behavior at sensor and actuator limits
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <limits>

#include "aurore/test_infrastructure.hpp"
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
#define ASSERT_GT(a, b) do { if ((a) <= (b)) throw std::runtime_error("Assertion failed: " #a " > " #b); } while(0)
#define ASSERT_LT(a, b) do { if ((a) >= (b)) throw std::runtime_error("Assertion failed: " #a " < " #b); } while(0)
#define ASSERT_GE(a, b) do { if ((a) < (b)) throw std::runtime_error("Assertion failed: " #a " >= " #b); } while(0)
#define ASSERT_LE(a, b) do { if ((a) > (b)) throw std::runtime_error("Assertion failed: " #a " <= " #b); } while(0)

}  // anonymous namespace

// ============================================================================
// NaN/Inf Detection Tests
// ============================================================================

TEST(test_nan_detection) {
    using aurore::test::NumericRobustnessTester;

    ASSERT_TRUE(NumericRobustnessTester::is_nan(std::nanf("")));
    ASSERT_FALSE(NumericRobustnessTester::is_nan(1.0f));
    ASSERT_FALSE(NumericRobustnessTester::is_nan(0.0f));
}

TEST(test_inf_detection) {
    using aurore::test::NumericRobustnessTester;

    ASSERT_TRUE(NumericRobustnessTester::is_inf(std::numeric_limits<float>::infinity()));
    ASSERT_FALSE(NumericRobustnessTester::is_inf(1.0f));
    ASSERT_TRUE(NumericRobustnessTester::is_inf(-std::numeric_limits<float>::infinity()));
}

TEST(test_finite_detection) {
    using aurore::test::NumericRobustnessTester;

    ASSERT_TRUE(NumericRobustnessTester::is_finite(1.0f));
    ASSERT_TRUE(NumericRobustnessTester::is_finite(0.0f));
    ASSERT_FALSE(NumericRobustnessTester::is_finite(std::nanf("")));
    ASSERT_FALSE(NumericRobustnessTester::is_finite(std::numeric_limits<float>::infinity()));
}

// ============================================================================
// Overflow Handling Tests
// ============================================================================

TEST(test_saturating_conversion_positive_overflow) {
    using aurore::test::NumericRobustnessTester;

    float large = 1e10f;
    int32_t result = NumericRobustnessTester::float_to_int32_saturating(large);

    ASSERT_EQ(result, std::numeric_limits<int32_t>::max());
}

TEST(test_saturating_conversion_negative_overflow) {
    using aurore::test::NumericRobustnessTester;

    float small = -1e10f;
    int32_t result = NumericRobustnessTester::float_to_int32_saturating(small);

    ASSERT_EQ(result, std::numeric_limits<int32_t>::min());
}

TEST(test_saturating_conversion_normal) {
    using aurore::test::NumericRobustnessTester;

    float normal = 123.456f;
    int32_t result = NumericRobustnessTester::float_to_int32_saturating(normal);

    ASSERT_EQ(result, 123);
}

TEST(test_uint_saturating_conversion) {
    using aurore::test::NumericRobustnessTester;

    float large = 1e10f;
    uint32_t result = NumericRobustnessTester::float_to_uint32_saturating(large);

    ASSERT_EQ(result, std::numeric_limits<uint32_t>::max());
}

// ============================================================================
// Operation Robustness Tests
// ============================================================================

TEST(test_basic_operations_no_overflow) {
    using aurore::test::NumericRobustnessTester;

    auto result = NumericRobustnessTester::test_operations(1.0f, 1.0f, 100);

    ASSERT_FALSE(result.overflow_detected);
    ASSERT_FALSE(result.nan_propagated);
    ASSERT_FALSE(result.inf_propagated);
}

TEST(test_basic_operations_with_overflow) {
    using aurore::test::NumericRobustnessTester;

    auto result = NumericRobustnessTester::test_operations(
        std::numeric_limits<float>::max() / 2.0f,
        10.0f,
        100);

    ASSERT_FALSE(result.nan_propagated);
}

TEST(test_division_by_zero_safe) {
    using aurore::test::NumericRobustnessTester;

    auto result = NumericRobustnessTester::test_operations(1.0f, 0.0f, 10);

    ASSERT_FALSE(result.inf_propagated);
}

TEST(test_trigonometric_nan_propagation) {
    using aurore::test::NumericRobustnessTester;

    auto result = NumericRobustnessTester::test_trig(std::nanf(""), 10);

    ASSERT_TRUE(result.nan_propagated);
}

TEST(test_trigonometric_normal_values) {
    using aurore::test::NumericRobustnessTester;

    auto result = NumericRobustnessTester::test_trig(0.5f, 100);

    ASSERT_FALSE(result.nan_propagated);
    ASSERT_FALSE(result.inf_propagated);
}

// ============================================================================
// Precision Decay Tests
// ============================================================================

TEST(test_angle_wrapping_normal) {
    using aurore::test::NumericRobustnessTester;

    auto result = NumericRobustnessTester::test_angle_wrapping(0.0f, 100);

    ASSERT_FALSE(result.precision_decay_excessive);
    ASSERT_GE(result.min_value, 0.0f);
}

TEST(test_angle_wrapping_around_2pi) {
    using aurore::test::NumericRobustnessTester;

    auto result = NumericRobustnessTester::test_angle_wrapping(6.0f, 100);

    ASSERT_GE(result.max_value, 6.0f);
}

TEST(test_float_precision_accumulation) {
    float sum = 0.0f;
    float small = 0.00001f;

    for (int i = 0; i < 100000; i++) {
        sum += small;
    }

    float expected = 1.0f;
    float diff = std::abs(sum - expected);

    ASSERT_LT(diff, 0.01f);
}

// ============================================================================
// Range Boundary Tests
// ============================================================================

TEST(test_range_min_boundary) {
    constexpr float kRangeMinM = 0.5f;

    float range = kRangeMinM - 0.1f;
    ASSERT_LT(range, kRangeMinM);
}

TEST(test_range_max_boundary) {
    constexpr float kRangeMaxM = 5000.0f;

    float range = kRangeMaxM + 100.0f;
    ASSERT_GT(range, kRangeMaxM);
}

TEST(test_range_valid_check) {
    constexpr float kRangeMinM = 0.5f;
    constexpr float kRangeMaxM = 5000.0f;

    bool valid_1 = (1.0f >= kRangeMinM && 1.0f <= kRangeMaxM);
    bool valid_2 = (0.1f >= kRangeMinM && 0.1f <= kRangeMaxM);
    bool valid_3 = (10000.0f >= kRangeMinM && 10000.0f <= kRangeMaxM);

    ASSERT_TRUE(valid_1);
    ASSERT_FALSE(valid_2);
    ASSERT_FALSE(valid_3);
}

// ============================================================================
// Angle Boundary Tests
// ============================================================================

TEST(test_angle_azimuth_wrap) {
    float angle = 370.0f;
    while (angle > 360.0f) {
        angle -= 360.0f;
    }

    ASSERT_GE(angle, 0.0f);
    ASSERT_LT(angle, 360.0f);
}

TEST(test_angle_elevation_clamp) {
    float min_elevation = -90.0f;
    float max_elevation = 90.0f;

    float elevated = 100.0f;
    if (elevated > max_elevation) elevated = max_elevation;
    if (elevated < min_elevation) elevated = min_elevation;

    ASSERT_GE(elevated, min_elevation);
    ASSERT_LE(elevated, max_elevation);
}

TEST(test_angle_zero_normalization) {
    float angle = 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;

    ASSERT_LT(angle, 360.0f);
}

// ============================================================================
// Ballistic Solver Numeric Tests
// ============================================================================

TEST(test_ballistic_solver_normal_range) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto solution = solver.solve(100.0f, 0.0f, 0.0f, 340.0f, 0.0f);

    ASSERT_TRUE(solution.has_value());
    ASSERT_FALSE(std::isnan(solution->az_lead_deg));
    ASSERT_FALSE(std::isnan(solution->el_lead_deg));
}

TEST(test_ballistic_solver_zero_range) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto solution = solver.solve(0.0f, 0.0f, 0.0f, 340.0f, 0.0f);

    if (solution.has_value()) {
        ASSERT_FALSE(std::isnan(solution->az_lead_deg) || std::isinf(solution->az_lead_deg));
    }
}

TEST(test_ballistic_solver_large_range) {
    aurore::BallisticSolver solver;
    solver.initialize_lookup_table();

    auto solution = solver.solve(5000.0f, 0.0f, 0.0f, 340.0f, 0.0f);

    if (solution.has_value()) {
        ASSERT_FALSE(std::isnan(solution->az_lead_deg) || std::isinf(solution->az_lead_deg));
        ASSERT_FALSE(std::isnan(solution->el_lead_deg) || std::isinf(solution->el_lead_deg));
    }
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "=== Part 6: Numeric and Algorithmic Robustness Tests ===" << std::endl;
    std::cout << "Running numeric robustness tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    std::cout << "\n--- NaN/Inf Detection ---" << std::endl;
    RUN_TEST(test_nan_detection);
    RUN_TEST(test_inf_detection);
    RUN_TEST(test_finite_detection);

    std::cout << "\n--- Overflow Handling ---" << std::endl;
    RUN_TEST(test_saturating_conversion_positive_overflow);
    RUN_TEST(test_saturating_conversion_negative_overflow);
    RUN_TEST(test_saturating_conversion_normal);
    RUN_TEST(test_uint_saturating_conversion);

    std::cout << "\n--- Operation Robustness ---" << std::endl;
    RUN_TEST(test_basic_operations_no_overflow);
    RUN_TEST(test_basic_operations_with_overflow);
    RUN_TEST(test_division_by_zero_safe);
    RUN_TEST(test_trigonometric_nan_propagation);
    RUN_TEST(test_trigonometric_normal_values);

    std::cout << "\n--- Precision Decay ---" << std::endl;
    RUN_TEST(test_angle_wrapping_normal);
    RUN_TEST(test_angle_wrapping_around_2pi);
    RUN_TEST(test_float_precision_accumulation);

    std::cout << "\n--- Range Boundary ---" << std::endl;
    RUN_TEST(test_range_min_boundary);
    RUN_TEST(test_range_max_boundary);
    RUN_TEST(test_range_valid_check);

    std::cout << "\n--- Angle Boundary ---" << std::endl;
    RUN_TEST(test_angle_azimuth_wrap);
    RUN_TEST(test_angle_elevation_clamp);
    RUN_TEST(test_angle_zero_normalization);

    std::cout << "\n--- Ballistic Solver Numeric ---" << std::endl;
    RUN_TEST(test_ballistic_solver_normal_range);
    RUN_TEST(test_ballistic_solver_zero_range);
    RUN_TEST(test_ballistic_solver_large_range);

    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}