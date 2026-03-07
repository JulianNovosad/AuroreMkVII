#include "aurore/tracker.hpp"
#include <cassert>
#include <iostream>
#include <opencv2/opencv.hpp>

using namespace aurore;

cv::Mat make_test_frame(int target_x, int target_y) {
    cv::Mat frame = cv::Mat::zeros(864, 1536, CV_8UC3);
    cv::rectangle(frame, {target_x, target_y, 60, 60}, {0, 200, 100}, -1);
    cv::putText(frame, "T", {target_x+10, target_y+45},
                cv::FONT_HERSHEY_SIMPLEX, 1.5, {255,255,0}, 3);
    return frame;
}

void test_tracker_not_valid_before_init() {
    KcfTracker tracker;
    assert(!tracker.is_valid());
    std::cout << "PASS: tracker invalid before init\n";
}

void test_tracker_valid_after_init() {
    KcfTracker tracker;
    cv::Mat frame = make_test_frame(400, 300);
    cv::Rect2d bbox(400, 300, 60, 60);
    bool ok = tracker.init(frame, bbox);
    assert(ok);
    assert(tracker.is_valid());
    std::cout << "PASS: tracker valid after init\n";
}

void test_tracker_update_same_frame_stays_valid() {
    KcfTracker tracker;
    cv::Mat frame = make_test_frame(400, 300);
    tracker.init(frame, {400, 300, 60, 60});
    auto sol = tracker.update(frame);
    assert(sol.valid);
    std::cout << "PASS: tracker update on same frame produces valid solution\n";
}

void test_redetection_same_template_high_score() {
    KcfTracker tracker;
    cv::Mat frame = make_test_frame(400, 300);
    tracker.init(frame, {400, 300, 60, 60});
    tracker.capture_reference_template(frame, {400, 300, 60, 60});
    float score = tracker.redetect(frame);
    assert(score > 0.85f);
    std::cout << "PASS: NCC redetection score > 0.85 on same frame\n";
}

void test_redetection_blank_frame_low_score() {
    KcfTracker tracker;
    cv::Mat frame = make_test_frame(400, 300);
    tracker.init(frame, {400, 300, 60, 60});
    tracker.capture_reference_template(frame, {400, 300, 60, 60});
    cv::Mat blank = cv::Mat::zeros(864, 1536, CV_8UC3);
    float score = tracker.redetect(blank);
    assert(score < 0.85f);
    std::cout << "PASS: NCC redetection score < 0.85 on blank frame\n";
}

void test_tracker_reset_invalidates() {
    KcfTracker tracker;
    cv::Mat frame = make_test_frame(400, 300);
    tracker.init(frame, {400, 300, 60, 60});
    assert(tracker.is_valid());
    tracker.reset();
    assert(!tracker.is_valid());
    std::cout << "PASS: reset invalidates tracker\n";
}

int main() {
    test_tracker_not_valid_before_init();
    test_tracker_valid_after_init();
    test_tracker_update_same_frame_stays_valid();
    test_redetection_same_template_high_score();
    test_redetection_blank_frame_low_score();
    test_tracker_reset_invalidates();
    std::cout << "\nAll tracker tests passed.\n";
    return 0;
}
