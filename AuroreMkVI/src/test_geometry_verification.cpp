#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include "geometry_verification.h"

using namespace aurore::geometry;

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "  FAIL: " << msg << std::endl; \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(cond, msg) do { \
    if (cond) { \
        std::cerr << "  FAIL: " << msg << std::endl; \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ_GEOM(a, b, msg) do { \
    if ((a) != (b)) { \
        std::cerr << "  FAIL: " << msg << " (expected " << static_cast<int>(b) << ", got " << static_cast<int>(a) << ")" << std::endl; \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        std::cerr << "  FAIL: " << msg << " (expected " << (b) << ", got " << (a) << ")" << std::endl; \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, tol, msg) do { \
    if (std::abs((a) - (b)) > (tol)) { \
        std::cerr << "  FAIL: " << msg << " (expected " << (b) << "±" << (tol) << ", got " << (a) << ")" << std::endl; \
        tests_failed++; \
        return; \
    } \
} while(0)

void test_fov_bounds_determination() {
    std::cout << "\n=== Test: FOV Bounds Determination ===" << std::endl;

    FOVBounds fov(320, 320, 4.74f, 6.45f, 3.63f);

    ASSERT_EQ(fov.width_pixels, 320, "FOV width");
    ASSERT_EQ(fov.height_pixels, 320, "FOV height");
    ASSERT_TRUE(fov.focal_length_px > 0, "Focal length in pixels should be positive");
    ASSERT_TRUE(fov.horizontal_fov_deg > 0, "Horizontal FOV should be positive");
    ASSERT_TRUE(fov.vertical_fov_deg > 0, "Vertical FOV should be positive");

    std::cout << "  FOV: " << fov.horizontal_fov_deg << "°H x " << fov.vertical_fov_deg << "°V" << std::endl;
    std::cout << "  Focal length: " << fov.focal_length_px << " px" << std::endl;
    std::cout << "  PASS: FOV bounds determination" << std::endl;
    tests_passed++;
}

void test_is_within_fov_basic() {
    std::cout << "\n=== Test: isWithinFOV Basic ===" << std::endl;

    FOVBounds fov(320, 320, 4.74f, 6.45f, 3.63f);
    GeometryVerifier verifier(fov);

    ASSERT_TRUE(verifier.is_within_fov(0, 0), "Origin should be within FOV");
    ASSERT_TRUE(verifier.is_within_fov(160, 160), "Center should be within FOV");
    ASSERT_TRUE(verifier.is_within_fov(320, 320), "Corner should be within FOV");
    ASSERT_TRUE(verifier.is_within_fov(100, 200), "Arbitrary point should be within FOV");

    ASSERT_FALSE(verifier.is_within_fov(-1, 160), "Negative X should be outside");
    ASSERT_FALSE(verifier.is_within_fov(160, -1), "Negative Y should be outside");
    ASSERT_FALSE(verifier.is_within_fov(321, 160), "X > width should be outside");
    ASSERT_FALSE(verifier.is_within_fov(160, 321), "Y > height should be outside");

    std::cout << "  PASS: isWithinFOV basic tests" << std::endl;
    tests_passed++;
}

void test_check_coordinate_zero_invisibility() {
    std::cout << "\n=== Test: Zero-Invisibility Protocol ===" << std::endl;

    FOVBounds fov(320, 320, 4.74f, 6.45f, 3.63f);
    GeometryVerifier verifier(fov);
    verifier.reset_counters();

    GeometryCheckResult result;

    result = verifier.check_coordinate(-1, 100, 1000, "test_source");
    ASSERT_FALSE(result.is_valid, "Negative X should fail");
    ASSERT_EQ_GEOM(result.error_code, GeometryErrorCode::NEGATIVE_COORDINATE_X, "Error code");
    ASSERT_TRUE(result.error_message.find("CRITICAL_GEOMETRY_ERROR") != std::string::npos,
                "Error message should contain CRITICAL_GEOMETRY_ERROR");

    result = verifier.check_coordinate(100, -1, 1001, "test_source");
    ASSERT_FALSE(result.is_valid, "Negative Y should fail");
    ASSERT_EQ_GEOM(result.error_code, GeometryErrorCode::NEGATIVE_COORDINATE_Y, "Error code");

    result = verifier.check_coordinate(400, 100, 1002, "test_source");
    ASSERT_FALSE(result.is_valid, "X > width should fail");
    ASSERT_EQ_GEOM(result.error_code, GeometryErrorCode::EXCEEDS_WIDTH, "Error code");

    result = verifier.check_coordinate(100, 400, 1003, "test_source");
    ASSERT_FALSE(result.is_valid, "Y > height should fail");
    ASSERT_EQ_GEOM(result.error_code, GeometryErrorCode::EXCEEDS_HEIGHT, "Error code");

    ASSERT_EQ(verifier.get_total_fov_errors(), 4, "Should record 4 FOV errors");
    ASSERT_EQ(verifier.get_total_geometry_errors(), 4, "Should record 4 total errors");

    std::cout << "  PASS: Zero-Invisibility Protocol" << std::endl;
    tests_passed++;
}

void test_check_coordinate_valid() {
    std::cout << "\n=== Test: Valid Coordinate Check ===" << std::endl;

    FOVBounds fov(320, 320, 4.74f, 6.45f, 3.63f);
    GeometryVerifier verifier(fov);

    GeometryCheckResult result = verifier.check_coordinate(160, 160, 1000, "test_source");

    ASSERT_TRUE(result.is_valid, "Valid point should pass");
    ASSERT_EQ_GEOM(result.error_code, GeometryErrorCode::NONE, "Error code should be NONE");
    ASSERT_EQ(result.x, 160, "X coordinate");
    ASSERT_EQ(result.y, 160, "Y coordinate");
    ASSERT_EQ(result.timestamp_ms, 1000, "Timestamp");
    ASSERT_EQ(result.source_location, "test_source", "Source location");

    std::cout << "  PASS: Valid coordinate check" << std::endl;
    tests_passed++;
}

void test_sight_over_bore_offset_calculation() {
    std::cout << "\n=== Test: Sight-over-bore Offset Calculation ===" << std::endl;

    FOVBounds fov(320, 320, 4.74f, 6.45f, 3.63f);
    GeometryVerifier verifier(fov);
    verifier.set_sight_height_m(0.08f);

    float offset_10m = verifier.calculate_sight_over_bore_offset_pixels(10.0f);
    float offset_50m = verifier.calculate_sight_over_bore_offset_pixels(50.0f);
    float offset_100m = verifier.calculate_sight_over_bore_offset_pixels(100.0f);

    ASSERT_TRUE(offset_10m > offset_50m, "Offset at 10m should be larger than at 50m");
    ASSERT_TRUE(offset_50m > offset_100m, "Offset at 50m should be larger than at 100m");
    ASSERT_TRUE(offset_10m > 0, "Offset should be positive");

    float expected_10m = (0.08f / 10.0f) * fov.focal_length_px;
    ASSERT_NEAR(offset_10m, expected_10m, 0.001f, "Offset formula");

    float invalid_offset = verifier.calculate_sight_over_bore_offset_pixels(-10.0f);
    ASSERT_EQ(invalid_offset, 0.0f, "Invalid distance should return 0");

    float zero_offset = verifier.calculate_sight_over_bore_offset_pixels(0.0f);
    ASSERT_EQ(zero_offset, 0.0f, "Zero distance should return 0");

    std::cout << "  Offset at 10m: " << offset_10m << " px" << std::endl;
    std::cout << "  Offset at 50m: " << offset_50m << " px" << std::endl;
    std::cout << "  Offset at 100m: " << offset_100m << " px" << std::endl;
    std::cout << "  PASS: Sight-over-bore offset calculation" << std::endl;
    tests_passed++;
}

void test_sight_over_bore_y_offset_verification() {
    std::cout << "\n=== Test: Sight-over-bore Y Offset Verification ===" << std::endl;

    FOVBounds fov(320, 320, 4.74f, 6.45f, 3.63f);
    GeometryVerifier verifier(fov);
    verifier.set_sight_height_m(0.08f);
    verifier.set_sight_over_bore_tolerance(3.0f);

    float bbox_center_y = 200.0f;
    float distance_m = 50.0f;
    float offset_px = verifier.calculate_sight_over_bore_offset_pixels(distance_m);
    float oz_y = bbox_center_y - offset_px;

    bool valid = verifier.verify_sight_over_bore_y_offset(bbox_center_y, oz_y, distance_m);
    ASSERT_TRUE(valid, "Correct offset should be valid");

    float wrong_oz_y = bbox_center_y - offset_px - 10.0f;
    bool invalid = verifier.verify_sight_over_bore_y_offset(bbox_center_y, wrong_oz_y, distance_m);
    ASSERT_FALSE(invalid, "Wrong offset should be invalid");

    std::cout << "  Expected offset: " << offset_px << " px" << std::endl;
    std::cout << "  PASS: Sight-over-bore Y offset verification" << std::endl;
    tests_passed++;
}

void test_x_alignment_verification() {
    std::cout << "\n=== Test: X Alignment Verification ===" << std::endl;

    FOVBounds fov(320, 320, 4.74f, 6.45f, 3.63f);
    GeometryVerifier verifier(fov);
    verifier.set_x_alignment_tolerance(1.0f);

    bool valid = verifier.verify_x_alignment(160.0f, 160.0f, 0.0f);
    ASSERT_TRUE(valid, "Exact alignment should be valid");

    valid = verifier.verify_x_alignment(160.0f, 160.5f, 0.0f);
    ASSERT_TRUE(valid, "Within tolerance should be valid");

    valid = verifier.verify_x_alignment(160.0f, 162.0f, 0.0f);
    ASSERT_FALSE(valid, "Outside tolerance should be invalid for stationary target");

    valid = verifier.verify_x_alignment(160.0f, 162.0f, 5.0f);
    ASSERT_TRUE(valid, "Outside tolerance should be valid for moving target");

    std::cout << "  PASS: X alignment verification" << std::endl;
    tests_passed++;
}

void test_check_orange_zone_complete() {
    std::cout << "\n=== Test: Complete Orange Zone Check ===" << std::endl;

    FOVBounds fov(320, 320, 4.74f, 6.45f, 3.63f);
    GeometryVerifier verifier(fov);
    verifier.set_sight_height_m(0.08f);
    verifier.set_x_alignment_tolerance(1.0f);
    verifier.set_sight_over_bore_tolerance(3.0f);
    verifier.reset_counters();

    float bbox_center_x = 160.0f;
    float bbox_center_y = 200.0f;
    float distance_m = 50.0f;

    float offset_px = verifier.calculate_sight_over_bore_offset_pixels(distance_m);
    float oz_x = bbox_center_x;
    float oz_y = bbox_center_y - offset_px;

    GeometryCheckResult result = verifier.check_orange_zone(
        oz_x, oz_y, bbox_center_x, bbox_center_y, distance_m, 0.0f, 1000, "test");

    ASSERT_TRUE(result.is_valid, "Valid Orange Zone should pass");
    ASSERT_EQ_GEOM(result.error_code, GeometryErrorCode::NONE, "Error code should be NONE");

    float bad_oz_x = 400.0f;
    result = verifier.check_orange_zone(
        bad_oz_x, oz_y, bbox_center_x, bbox_center_y, distance_m, 0.0f, 1001, "test");

    ASSERT_FALSE(result.is_valid, "Out-of-FOV Orange Zone should fail");
    ASSERT_EQ_GEOM(result.error_code, GeometryErrorCode::EXCEEDS_WIDTH, "Error code");

    float bad_distance = -10.0f;
    result = verifier.check_orange_zone(
        oz_x, oz_y, bbox_center_x, bbox_center_y, bad_distance, 0.0f, 1002, "test");

    ASSERT_FALSE(result.is_valid, "Invalid distance should fail");
    ASSERT_EQ_GEOM(result.error_code, GeometryErrorCode::INVALID_DISTANCE, "Error code");

    std::cout << "  PASS: Complete Orange Zone check" << std::endl;
    tests_passed++;
}

void test_fov_description() {
    std::cout << "\n=== Test: FOV Description ===" << std::endl;

    FOVBounds fov(320, 320, 4.74f, 6.45f, 3.63f);
    GeometryVerifier verifier(fov);

    std::string desc = verifier.get_fov_description();

    ASSERT_TRUE(desc.find("FOV:") != std::string::npos, "Should contain FOV label");
    ASSERT_TRUE(desc.find("°H") != std::string::npos, "Should contain horizontal FOV");
    ASSERT_TRUE(desc.find("°V") != std::string::npos, "Should contain vertical FOV");
    ASSERT_TRUE(desc.find("320x320") != std::string::npos, "Should contain dimensions");

    std::cout << "  Description: " << desc << std::endl;
    std::cout << "  PASS: FOV description" << std::endl;
    tests_passed++;
}

void test_error_counters() {
    std::cout << "\n=== Test: Error Counters ===" << std::endl;

    FOVBounds fov(320, 320, 4.74f, 6.45f, 3.63f);
    GeometryVerifier verifier(fov);
    verifier.reset_counters();

    verifier.check_coordinate(-1, 0, 0);
    verifier.check_coordinate(0, -1, 0);
    verifier.check_coordinate(400, 0, 0);
    verifier.check_coordinate(0, 400, 0);

    ASSERT_EQ(verifier.get_total_geometry_errors(), 4, "Total errors");
    ASSERT_EQ(verifier.get_total_fov_errors(), 4, "FOV errors");

    verifier.check_coordinate(160, 160, 0);

    ASSERT_EQ(verifier.get_total_geometry_errors(), 4, "Total errors unchanged");

    verifier.reset_counters();

    ASSERT_EQ(verifier.get_total_geometry_errors(), 0, "After reset");
    ASSERT_EQ(verifier.get_total_fov_errors(), 0, "FOV errors after reset");

    std::cout << "  PASS: Error counters" << std::endl;
    tests_passed++;
}

void test_nan_inf_handling() {
    std::cout << "\n=== Test: NaN/Inf Handling ===" << std::endl;

    FOVBounds fov(320, 320, 4.74f, 6.45f, 3.63f);
    GeometryVerifier verifier(fov);

    GeometryCheckResult result = verifier.check_coordinate(NAN, 160, 0);
    ASSERT_FALSE(result.is_valid, "NaN X should fail");
    ASSERT_EQ_GEOM(result.error_code, GeometryErrorCode::INVALID_FOCAL_LENGTH, "Error code");

    result = verifier.check_coordinate(160, NAN, 0);
    ASSERT_FALSE(result.is_valid, "NaN Y should fail");
    ASSERT_EQ_GEOM(result.error_code, GeometryErrorCode::INVALID_FOCAL_LENGTH, "Error code");

    result = verifier.check_coordinate(INFINITY, 160, 0);
    ASSERT_FALSE(result.is_valid, "Inf X should fail");

    float offset = verifier.calculate_sight_over_bore_offset_pixels(INFINITY);
    ASSERT_EQ(offset, 0.0f, "Inf distance should return 0");

    std::cout << "  PASS: NaN/Inf handling" << std::endl;
    tests_passed++;
}

void test_default_parameters() {
    std::cout << "\n=== Test: Default Parameters ===" << std::endl;

    GeometryVerifier verifier;

    ASSERT_EQ(verifier.get_sight_height_m(), SIGHT_HEIGHT_DEFAULT_M, "Default sight height");
    ASSERT_EQ(verifier.get_x_alignment_tolerance(), X_ALIGNMENT_TOLERANCE_DEFAULT, "Default X tolerance");
    ASSERT_EQ(verifier.get_sight_over_bore_tolerance(), SIGHT_OVER_BORE_TOLERANCE_DEFAULT, "Default SOB tolerance");

    std::cout << "  PASS: Default parameters" << std::endl;
    tests_passed++;
}

int main() {
    std::cout << "================================================" << std::endl;
    std::cout << "  Geometry Verification Test Suite" << std::endl;
    std::cout << "  Spec: Phase 3 - Geometry and FOV Verification" << std::endl;
    std::cout << "================================================" << std::endl;

    test_default_parameters();
    test_fov_bounds_determination();
    test_is_within_fov_basic();
    test_check_coordinate_valid();
    test_check_coordinate_zero_invisibility();
    test_sight_over_bore_offset_calculation();
    test_sight_over_bore_y_offset_verification();
    test_x_alignment_verification();
    test_check_orange_zone_complete();
    test_fov_description();
    test_error_counters();
    test_nan_inf_handling();

    std::cout << "\n================================================" << std::endl;
    std::cout << "  TEST SUMMARY" << std::endl;
    std::cout << "  Passed: " << tests_passed << std::endl;
    std::cout << "  Failed: " << tests_failed << std::endl;
    std::cout << "  Total:  " << (tests_passed + tests_failed) << std::endl;
    std::cout << "================================================" << std::endl;

    if (tests_failed == 0) {
        std::cout << "  ALL TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "  SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
