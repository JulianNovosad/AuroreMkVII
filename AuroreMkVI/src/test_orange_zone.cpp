#include <cmath>
#include <limits>
#include <iostream>
#include <atomic>
#include <chrono>
#include <string>

#include "geometry_verification.h"
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

static void orange_zone_valid_stationary_target();
static void orange_zone_outside_fov();
static void orange_zone_invalid_distance();
static void orange_zone_sight_over_bore_violation();
static void orange_zone_x_alignment_violation();
static void orange_zone_moving_target();
static void orange_zone_edge_cases();
static void orange_zone_multiple_distances();
static void check_coordinate_outside_bounds();
static void check_coordinate_on_boundary();
static void check_coordinate_inside();
static void check_coordinate_nan_inf();

void orange_zone_valid_stationary_target() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    float distance_m = 10.0f;
    float bbox_center_x = 320.0f;
    float bbox_center_y = 240.0f;
    float oz_x = 320.0f;

    float expected_offset = verifier.calculate_sight_over_bore_offset_pixels(distance_m);
    float oz_y = bbox_center_y - expected_offset;

    auto result = verifier.check_orange_zone(oz_x, oz_y, bbox_center_x, bbox_center_y, distance_m, 0.0f);

    ASSERT_TRUE(result.is_valid);
    ASSERT_TRUE(result.error_code == aurore::geometry::GeometryErrorCode::NONE);
}

void orange_zone_outside_fov() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    auto result = verifier.check_orange_zone(-10.0f, 240.0f, 320.0f, 240.0f, 10.0f, 0.0f);

    ASSERT_FALSE(result.is_valid);
    ASSERT_TRUE(result.error_code == aurore::geometry::GeometryErrorCode::NEGATIVE_COORDINATE_X);
    ASSERT_TRUE(result.error_message.find("CRITICAL_GEOMETRY_ERROR") != std::string::npos);
}

void orange_zone_invalid_distance() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    auto result = verifier.check_orange_zone(320.0f, 240.0f, 320.0f, 240.0f, 0.0f, 0.0f);

    ASSERT_FALSE(result.is_valid);
    ASSERT_TRUE(result.error_code == aurore::geometry::GeometryErrorCode::INVALID_DISTANCE);
}

void orange_zone_sight_over_bore_violation() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    float distance_m = 10.0f;
    float bbox_center_x = 320.0f;
    float bbox_center_y = 240.0f;
    float oz_x = 320.0f;

    float expected_offset = verifier.calculate_sight_over_bore_offset_pixels(distance_m);
    float oz_y = bbox_center_y - expected_offset - 10.0f;

    auto result = verifier.check_orange_zone(oz_x, oz_y, bbox_center_x, bbox_center_y, distance_m, 0.0f);

    ASSERT_FALSE(result.is_valid);
    ASSERT_TRUE(result.error_code == aurore::geometry::GeometryErrorCode::SIGHT_OVER_BORE_VIOLATION);
    ASSERT_TRUE(result.error_message.find("CRITICAL_GEOMETRY_ERROR") != std::string::npos);
}

void orange_zone_x_alignment_violation() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    float distance_m = 10.0f;
    float bbox_center_x = 320.0f;
    float bbox_center_y = 240.0f;

    float expected_offset = verifier.calculate_sight_over_bore_offset_pixels(distance_m);
    float oz_y = bbox_center_y - expected_offset;

    float oz_x = 330.0f;

    auto result = verifier.check_orange_zone(oz_x, oz_y, bbox_center_x, bbox_center_y, distance_m, 0.0f);

    ASSERT_FALSE(result.is_valid);
    ASSERT_TRUE(result.error_code == aurore::geometry::GeometryErrorCode::X_ALIGNMENT_VIOLATION);
}

void orange_zone_moving_target() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    float distance_m = 10.0f;
    float bbox_center_x = 320.0f;
    float bbox_center_y = 240.0f;
    float oz_x = 320.0f;
    float velocity_x = 5.0f;

    float expected_offset = verifier.calculate_sight_over_bore_offset_pixels(distance_m);
    float oz_y = bbox_center_y - expected_offset;

    auto result = verifier.check_orange_zone(oz_x, oz_y, bbox_center_x, bbox_center_y, distance_m, velocity_x);

    ASSERT_TRUE(result.is_valid);
}

void orange_zone_edge_cases() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    float offset_10m = verifier.calculate_sight_over_bore_offset_pixels(10.0f);

    float bbox_center_y = 100.0f;
    float oz_y = bbox_center_y - offset_10m;
    auto result1 = verifier.check_orange_zone(100.0f, oz_y, 100.0f, bbox_center_y, 10.0f, 0.0f);
    ASSERT_TRUE(result1.is_valid);

    bbox_center_y = 400.0f;
    oz_y = bbox_center_y - offset_10m;
    auto result2 = verifier.check_orange_zone(500.0f, oz_y, 500.0f, bbox_center_y, 10.0f, 0.0f);
    ASSERT_TRUE(result2.is_valid);
}

void orange_zone_multiple_distances() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    float distances[] = {5.0f, 10.0f, 20.0f, 50.0f, 100.0f};

    for (float distance_m : distances) {
        float bbox_center_x = 320.0f;
        float bbox_center_y = 240.0f;
        float oz_x = 320.0f;

        float expected_offset = verifier.calculate_sight_over_bore_offset_pixels(distance_m);
        float oz_y = bbox_center_y - expected_offset;

        auto result = verifier.check_orange_zone(oz_x, oz_y, bbox_center_x, bbox_center_y, distance_m, 0.0f);

        ASSERT_TRUE(result.is_valid);
    }
}

void check_coordinate_outside_bounds() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    auto result1 = verifier.check_coordinate(-1.0f, 240.0f);
    ASSERT_FALSE(result1.is_valid);
    ASSERT_TRUE(result1.error_code == aurore::geometry::GeometryErrorCode::NEGATIVE_COORDINATE_X);

    auto result2 = verifier.check_coordinate(641.0f, 240.0f);
    ASSERT_FALSE(result2.is_valid);
    ASSERT_TRUE(result2.error_code == aurore::geometry::GeometryErrorCode::EXCEEDS_WIDTH);

    auto result3 = verifier.check_coordinate(320.0f, -1.0f);
    ASSERT_FALSE(result3.is_valid);
    ASSERT_TRUE(result3.error_code == aurore::geometry::GeometryErrorCode::NEGATIVE_COORDINATE_Y);

    auto result4 = verifier.check_coordinate(320.0f, 481.0f);
    ASSERT_FALSE(result4.is_valid);
    ASSERT_TRUE(result4.error_code == aurore::geometry::GeometryErrorCode::EXCEEDS_HEIGHT);
}

void check_coordinate_on_boundary() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    auto result1 = verifier.check_coordinate(0.0f, 240.0f);
    ASSERT_TRUE(result1.is_valid);

    auto result2 = verifier.check_coordinate(100.0f, 240.0f);
    ASSERT_TRUE(result2.is_valid);

    auto result3 = verifier.check_coordinate(320.0f, 0.0f);
    ASSERT_TRUE(result3.is_valid);

    auto result4 = verifier.check_coordinate(320.0f, 100.0f);
    ASSERT_TRUE(result4.is_valid);
}

void check_coordinate_inside() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    auto result = verifier.check_coordinate(320.0f, 240.0f);
    ASSERT_TRUE(result.is_valid);
    ASSERT_TRUE(result.error_code == aurore::geometry::GeometryErrorCode::NONE);
    ASSERT_TRUE(result.error_message == "WITHIN_FOV");
}

void check_coordinate_nan_inf() {
    aurore::geometry::FOVBounds fov(640, 480, 3.04f, 4.8f, 3.6f);
    aurore::geometry::GeometryVerifier verifier(fov);

    auto result1 = verifier.check_coordinate(std::nanf(""), 240.0f);
    ASSERT_FALSE(result1.is_valid);
    ASSERT_TRUE(result1.error_code == aurore::geometry::GeometryErrorCode::INVALID_FOCAL_LENGTH);

    auto result2 = verifier.check_coordinate(320.0f, std::numeric_limits<float>::infinity());
    ASSERT_FALSE(result2.is_valid);
    ASSERT_TRUE(result2.error_code == aurore::geometry::GeometryErrorCode::INVALID_FOCAL_LENGTH);
}

int main() {
    std::cout << "=== Orange Zone Calculation Tests ===\n\n";

    std::cout << "[Stationary Target Tests]\n";
    RUN_TEST(orange_zone_valid_stationary_target);

    std::cout << "\n[Boundary Error Tests]\n";
    RUN_TEST(orange_zone_outside_fov);
    RUN_TEST(orange_zone_invalid_distance);

    std::cout << "\n[Constraint Violation Tests]\n";
    RUN_TEST(orange_zone_sight_over_bore_violation);
    RUN_TEST(orange_zone_x_alignment_violation);

    std::cout << "\n[Moving Target Tests]\n";
    RUN_TEST(orange_zone_moving_target);

    std::cout << "\n[Edge Case Tests]\n";
    RUN_TEST(orange_zone_edge_cases);
    RUN_TEST(orange_zone_multiple_distances);

    std::cout << "\n[Coordinate Check Tests]\n";
    RUN_TEST(check_coordinate_outside_bounds);
    RUN_TEST(check_coordinate_on_boundary);
    RUN_TEST(check_coordinate_inside);
    RUN_TEST(check_coordinate_nan_inf);

    std::cout << "\n=== Test Results ===\n";
    std::cout << "Passed: " << tests_passed << "\n";
    std::cout << "Failed: " << tests_failed << "\n";

    return tests_failed > 0 ? 1 : 0;
}
