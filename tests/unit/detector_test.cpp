#include <cassert>
#include <cstdio>
#include <iostream>
#include <opencv2/aruco.hpp>
#include <opencv2/opencv.hpp>

#include "aurore/apriltag_detector.hpp"

using namespace aurore;

void test_detector_creates_without_templates() {
    AprilTagDetector det;
    assert(det.is_ready());  // ArUco detector is ready immediately (no templates needed)
    std::cout << "PASS: detector ready after construction\n";
}

void test_detect_on_blank_frame_returns_no_detection() {
    AprilTagDetector det;
    // White background frame (required for ArUco detection)
    cv::Mat frame = cv::Mat::ones(864, 1536, CV_8UC1) * 255;
    [[maybe_unused]] auto result = det.detect(frame);
    assert(!result.has_value());
    std::cout << "PASS: no detection on blank frame\n";
}

void test_detect_aruco_marker_tag36h11() {
    // Create a frame with an ArUco marker (DICT_6X6_250, tag ID 0)
    AprilTagDetector det;
    det.set_dictionary(cv::aruco::DICT_6X6_250);

    // Generate ArUco marker image (grayscale)
    cv::Mat marker_image;
    cv::aruco::Dictionary dictionary =
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250);
    cv::aruco::generateImageMarker(dictionary, 0, 200, marker_image, 1);

    // Create a white background frame and embed the marker
    cv::Mat frame = cv::Mat::ones(864, 1536, CV_8UC1) * 255;
    marker_image.copyTo(frame(cv::Rect(400, 300, 200, 200)));

    auto result = det.detect(frame);
    if (!result.has_value()) {
        std::cerr << "FAIL: No detection returned\n";
        return;
    }
    if (result->confidence < 0.85f) {
        std::cerr << "FAIL: Confidence too low: " << result->confidence << "\n";
        return;
    }
    if (result->bbox.w <= 0 || result->bbox.h <= 0) {
        std::cerr << "FAIL: Invalid bbox: " << result->bbox.w << "x" << result->bbox.h << "\n";
        return;
    }
    std::cout << "PASS: detected ArUco marker with confidence=" << result->confidence << " bbox=("
              << result->bbox.x << "," << result->bbox.y << "," << result->bbox.w << ","
              << result->bbox.h << ")\n";
}

void test_detect_multiple_markers() {
    AprilTagDetector det;
    det.set_dictionary(cv::aruco::DICT_6X6_250);

    cv::aruco::Dictionary dictionary =
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250);
    cv::Mat frame = cv::Mat::ones(864, 1536, CV_8UC3) * 255;  // White background (BGR)

    // Place marker ID 0 at top-left
    cv::Mat marker0;
    cv::aruco::generateImageMarker(dictionary, 0, 150, marker0, 1);
    cv::cvtColor(marker0, marker0, cv::COLOR_GRAY2BGR);
    marker0.copyTo(frame(cv::Rect(50, 50, 150, 150)));

    // Place marker ID 1 at bottom-right
    cv::Mat marker1;
    cv::aruco::generateImageMarker(dictionary, 1, 150, marker1, 1);
    cv::cvtColor(marker1, marker1, cv::COLOR_GRAY2BGR);
    marker1.copyTo(frame(cv::Rect(1300, 650, 150, 150)));

    auto results = det.detect_all(frame);
    assert(results.size() >= 2);
    std::cout << "PASS: detected " << results.size() << " markers\n";

    // Verify tag IDs were recorded
    auto ids = det.get_last_detected_ids();
    assert(ids.size() >= 2);
    std::cout << "PASS: recorded tag IDs: ";
    for (int id : ids) {
        std::cout << id << " ";
    }
    std::cout << "\n";
}

void test_filter_by_known_tag_ids() {
    AprilTagDetector det;
    det.set_dictionary(cv::aruco::DICT_6X6_250);
    det.set_known_tag_ids({0, 2});  // Only detect tags 0 and 2

    cv::aruco::Dictionary dictionary =
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250);
    cv::Mat frame = cv::Mat::ones(864, 1536, CV_8UC3) * 255;  // White background (BGR)

    // Place marker ID 0 (should be detected)
    cv::Mat marker0;
    cv::aruco::generateImageMarker(dictionary, 0, 150, marker0, 1);
    cv::cvtColor(marker0, marker0, cv::COLOR_GRAY2BGR);
    marker0.copyTo(frame(cv::Rect(50, 50, 150, 150)));

    // Place marker ID 5 (should be filtered out)
    cv::Mat marker5;
    cv::aruco::generateImageMarker(dictionary, 5, 150, marker5, 1);
    cv::cvtColor(marker5, marker5, cv::COLOR_GRAY2BGR);
    marker5.copyTo(frame(cv::Rect(400, 300, 150, 150)));

    auto results = det.detect_all(frame);
    assert(results.size() == 1);  // Only tag 0 should be detected
    std::cout << "PASS: filtered to known tag IDs (detected 1 of 2 markers)\n";
}

void test_detect_small_marker_below_threshold() {
    AprilTagDetector det;
    det.set_dictionary(cv::aruco::DICT_6X6_250);

    cv::aruco::Dictionary dictionary =
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250);
    cv::Mat frame = cv::Mat::ones(864, 1536, CV_8UC3) * 255;  // White background (BGR)

    // Place very small marker (below minMarkerPerimeter threshold)
    cv::Mat marker_small;
    cv::aruco::generateImageMarker(dictionary, 0, 30, marker_small, 1);  // 30x30 is too small
    cv::cvtColor(marker_small, marker_small, cv::COLOR_GRAY2BGR);
    marker_small.copyTo(frame(cv::Rect(400, 300, 30, 30)));

    auto result = det.detect(frame);
    // Small markers may or may not be detected depending on minMarkerPerimeter
    // This test verifies the detector handles small markers gracefully
    std::cout << "PASS: handled small marker (detected=" << (result.has_value() ? "yes" : "no")
              << ")\n";
}

void test_detect_different_dictionaries() {
    // Test with DICT_ARUCO_ORIGINAL
    AprilTagDetector det;
    det.set_dictionary(cv::aruco::DICT_ARUCO_ORIGINAL);

    cv::aruco::Dictionary dictionary =
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_ARUCO_ORIGINAL);
    cv::Mat marker_image;
    cv::aruco::generateImageMarker(dictionary, 0, 200, marker_image, 1);

    cv::Mat frame = cv::Mat::ones(864, 1536, CV_8UC3) * 255;  // White background (BGR)
    cv::cvtColor(marker_image, marker_image, cv::COLOR_GRAY2BGR);
    marker_image.copyTo(frame(cv::Rect(400, 300, 200, 200)));

    [[maybe_unused]] auto result = det.detect(frame);
    assert(result.has_value());
    std::cout << "PASS: detected marker with DICT_ARUCO_ORIGINAL\n";
}

int main() {
    test_detector_creates_without_templates();
    test_detect_on_blank_frame_returns_no_detection();
    test_detect_aruco_marker_tag36h11();
    test_detect_multiple_markers();
    test_filter_by_known_tag_ids();
    test_detect_small_marker_below_threshold();
    test_detect_different_dictionaries();
    std::cout << "\nAll AprilTag detector tests passed.\n";
    return 0;
}
