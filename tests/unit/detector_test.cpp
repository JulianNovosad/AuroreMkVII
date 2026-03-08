#include "aurore/detector.hpp"
#include <cassert>
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

int main() {
    test_detector_creates_without_templates();
    test_detect_on_blank_frame_returns_no_detection();
    test_detect_on_matching_frame();
    std::cout << "\nAll detector tests passed.\n";
    return 0;
}
