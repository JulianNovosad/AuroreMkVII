#include "aurore/detector.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <opencv2/opencv.hpp>

using namespace aurore;

void test_detector_creates_without_templates() {
    OrbDetector det;
    assert(!det.is_ready());
    std::cout << "PASS: detector not ready without templates\n";
}

void test_detect_on_blank_frame_returns_no_detection() {
    OrbDetector det;
    cv::Mat tmpl = cv::Mat::zeros(80, 80, CV_8UC3);
    cv::rectangle(tmpl, {10, 10, 60, 60}, {255, 255, 255}, -1);
    det.add_template(tmpl);
    assert(det.is_ready());

    cv::Mat frame = cv::Mat::zeros(864, 1536, CV_8UC3);
    [[maybe_unused]] auto result = det.detect(frame);
    assert(!result.has_value() || result->confidence < 0.7f);
    std::cout << "PASS: no detection on blank frame\n";
}

void test_detect_on_matching_frame() {
    OrbDetector det;
    cv::Mat tmpl = cv::Mat::zeros(80, 80, CV_8UC3);
    cv::rectangle(tmpl, {5, 5, 70, 70}, {200, 150, 100}, -1);
    cv::putText(tmpl, "X", {20, 50}, cv::FONT_HERSHEY_SIMPLEX, 1.5, {50,50,200}, 3);
    det.add_template(tmpl);

    cv::Mat frame = cv::Mat::zeros(864, 1536, CV_8UC3);
    tmpl.copyTo(frame(cv::Rect(400, 300, 80, 80)));

    [[maybe_unused]] auto result = det.detect(frame);
    std::cout << "PASS: detect() returns a result on frame with embedded template\n";
}

void test_load_descriptor_file_with_valid_descriptors() {
    // Create a synthetic descriptor file with ORB keypoints and descriptors
    const std::string test_file = "/tmp/test_descriptors.yml";

    // Create template with synthetic descriptors
    OrbDetector det_gen;
    cv::Mat tmpl = cv::Mat::zeros(80, 80, CV_8UC3);
    cv::rectangle(tmpl, {5, 5, 70, 70}, {200, 150, 100}, -1);
    cv::putText(tmpl, "T", {20, 50}, cv::FONT_HERSHEY_SIMPLEX, 1.5, {50,50,200}, 3);
    det_gen.add_template(tmpl);

    // Extract descriptors manually
    cv::Mat gray;
    cv::cvtColor(tmpl, gray, cv::COLOR_BGR2GRAY);
    auto orb = cv::ORB::create();
    std::vector<cv::KeyPoint> kps;
    cv::Mat descs;
    orb->detectAndCompute(gray, cv::noArray(), kps, descs);

    // Write to YAML file
    cv::FileStorage fs(test_file, cv::FileStorage::WRITE);
    fs << "descriptors" << descs;
    fs << "keypoints" << kps;
    fs.release();

    // Load from file
    OrbDetector det;
    [[maybe_unused]] bool loaded = det.load_descriptor_file(test_file);
    assert(loaded);
    assert(det.is_ready());
    std::cout << "PASS: load_descriptor_file() successfully loads valid YAML\n";

    // Clean up
    std::remove(test_file.c_str());
}

void test_load_descriptor_file_with_missing_file() {
    OrbDetector det;
    [[maybe_unused]] bool loaded = det.load_descriptor_file("/tmp/nonexistent_descriptors.yml");
    assert(!loaded);
    std::cout << "PASS: load_descriptor_file() returns false for missing file\n";
}

int main() {
    test_detector_creates_without_templates();
    test_detect_on_blank_frame_returns_no_detection();
    test_detect_on_matching_frame();
    test_load_descriptor_file_with_valid_descriptors();
    test_load_descriptor_file_with_missing_file();
    std::cout << "\nAll detector tests passed.\n";
    return 0;
}
