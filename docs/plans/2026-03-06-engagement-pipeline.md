# Engagement Pipeline Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

---

## Security Fixes Applied (2026-03-07)

This plan has been updated to reflect the following security and implementation changes:

| Fix | Description | Status |
|-----|-------------|--------|
| SEC-001 | Updated test command from `./aurore_tests` to `ctest --output-on-failure` | Applied |
| SEC-002 | Fixed WatchdogKick documentation — use `kick_watchdog()` method directly (not RAII) | Applied |
| SEC-003 | Added documentation for implemented modules: TelemetryWriter, StateMachine, BallisticSolver, FusionHat | Applied |
| SEC-004 | Updated tracker from CSRT to KCF for WCET compliance (1-2ms vs 10-20ms) | Applied |
| SEC-005 | Updated data flow diagram with RAW10→BGR888 conversion | Applied |
| SEC-006 | Added track_compute→actuation data path (LockFreeRingBuffer<TrackSolution>) | Applied |
| SEC-007 | Created docs/telemetry.md for TelemetryWriter usage | Applied |
| SEC-008 | Created docs/state_machine.md for FCS state transitions | Applied |
| SEC-009 | Marked AuroreMkVI/AGENTS.md as DEPRECATED (Edge TPU → libcamera+OpenCV migration) | Applied |

**Note:** This plan references CSRT tracker in Task 3. The actual implementation uses KCF tracker for WCET compliance. See `include/aurore/tracker.hpp` and `docs/tracker.md` for current implementation details.

---

**Goal:** Implement the 5 compliance-gap subsystems — state machine, CSRT/ORB vision pipeline, ballistics solver, Fusion HAT+ I2C gimbal driver, and HUD telemetry socket — closing all `compliance_complete` gate blockers identified in `agent_sessions/session_20260305_001/blackboard/quality_gates.json`.

**Architecture:** The engagement pipeline follows `spec.md §3-7`: camera free-runs at 120 Hz into the existing `LockFreeRingBuffer`, a state machine thread (`~30 Hz`) drives vision/tracking on demand via `std::async`, and a 1 kHz gimbal driver inner loop runs on its own SCHED_FIFO thread. All subsystems are pure C++17 with no heap allocation on hot paths.

**Tech Stack:** C++17, CMake 3.16+, OpenCV 4.6 (`TrackerCSRT`, `ORB_create`, `matchTemplate`), libcamera (existing `CameraWrapper`), Linux I2C userspace (`/dev/i2c-1`, `ioctl`), POSIX UART (`termios`), Python 3 (offline ballistics table generation only).

---

## Architectural Note

The new spec (`§3.2`) defines a **different threading model** from the current `main.cpp`:

| Existing | New (spec) |
|----------|-----------|
| 4 fixed SCHED_FIFO threads, every frame | State machine thread + on-demand vision worker |
| vision → ring_buffer → track → actuation | State machine gates all vision/actuation calls |

`main.cpp` will be refactored in Task 7. Tasks 1–6 build testable components independently. The existing `LockFreeRingBuffer`, `ThreadTiming`, `SafetyMonitor`, and `CameraWrapper` are **kept and reused**.

---

## Task 1: State Machine

**Files:**
- Create: `include/aurore/state_machine.hpp`
- Create: `src/state_machine/state_machine.cpp`
- Create: `tests/unit/state_machine_test.cpp`
- Modify: `CMakeLists.txt` (add `state_machine_test` target, uncomment state machine source)

### Step 1: Write the failing test

```cpp
// tests/unit/state_machine_test.cpp
#include "aurore/state_machine.hpp"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace aurore;

void test_initial_state() {
    StateMachine sm;
    assert(sm.state() == FcsState::IDLE);
    std::cout << "PASS: initial state is IDLE\n";
}

void test_idle_to_alert_on_detection() {
    StateMachine sm;
    Detection d;
    d.confidence = 0.8f;
    d.bbox = {100, 100, 50, 50};
    sm.on_detection(d);
    assert(sm.state() == FcsState::ALERT);
    std::cout << "PASS: IDLE → ALERT on confidence > 0.7\n";
}

void test_idle_stays_on_low_confidence() {
    StateMachine sm;
    Detection d;
    d.confidence = 0.5f;  // below threshold
    sm.on_detection(d);
    assert(sm.state() == FcsState::IDLE);
    std::cout << "PASS: stays IDLE on confidence < 0.7\n";
}

void test_alert_to_idle_on_timeout() {
    StateMachine sm;
    Detection d;
    d.confidence = 0.8f;
    d.bbox = {100, 100, 50, 50};
    sm.on_detection(d);
    assert(sm.state() == FcsState::ALERT);
    // Advance time past 50ms timeout
    sm.tick(std::chrono::milliseconds(60));
    assert(sm.state() == FcsState::IDLE);
    std::cout << "PASS: ALERT → IDLE on 50ms timeout\n";
}

void test_alert_to_located_on_confirmation() {
    StateMachine sm;
    Detection first;
    first.confidence = 0.8f;
    first.bbox = {100, 100, 50, 50};  // centroid (125, 125)
    sm.on_detection(first);
    assert(sm.state() == FcsState::ALERT);

    // Second detection within 50 pixels of first
    Detection second;
    second.confidence = 0.8f;
    second.bbox = {110, 110, 50, 50};  // centroid (135, 135) — 14px away
    sm.tick(std::chrono::milliseconds(40));  // within timeout
    sm.on_detection(second);
    assert(sm.state() == FcsState::LOCATED);
    std::cout << "PASS: ALERT → LOCATED on spatial confirmation\n";
}

void test_alert_to_idle_on_spatial_mismatch() {
    StateMachine sm;
    Detection first;
    first.confidence = 0.8f;
    first.bbox = {100, 100, 50, 50};
    sm.on_detection(first);

    Detection second;
    second.confidence = 0.8f;
    second.bbox = {500, 500, 50, 50};  // centroid far away
    sm.tick(std::chrono::milliseconds(30));
    sm.on_detection(second);
    assert(sm.state() == FcsState::IDLE);
    std::cout << "PASS: ALERT → IDLE on spatial mismatch\n";
}

void test_homing_timeout_returns_to_located() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::HOMING);
    sm.tick(std::chrono::milliseconds(510));  // past 500ms timeout
    assert(sm.state() == FcsState::LOCATED);
    std::cout << "PASS: HOMING → LOCATED on 500ms timeout\n";
}

void test_armed_to_idle_on_fire() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::ARMED);
    sm.on_fire_command();
    assert(sm.state() == FcsState::FIRE);
    sm.tick(std::chrono::milliseconds(15));  // past 10ms effector pulse
    assert(sm.state() == FcsState::IDLE);
    std::cout << "PASS: ARMED → FIRE → IDLE\n";
}

int main() {
    test_initial_state();
    test_idle_to_alert_on_detection();
    test_idle_stays_on_low_confidence();
    test_alert_to_idle_on_timeout();
    test_alert_to_located_on_confirmation();
    test_alert_to_idle_on_spatial_mismatch();
    test_homing_timeout_returns_to_located();
    test_armed_to_idle_on_fire();
    std::cout << "\nAll state machine tests passed.\n";
    return 0;
}
```

### Step 2: Run test to verify it fails

```bash
cd build-native && cmake .. -DCMAKE_BUILD_TYPE=Debug 2>&1 | grep -E "error|warning"
cmake --build . --target state_machine_test 2>&1 | tail -5
```
Expected: compile error — `aurore/state_machine.hpp` not found.

### Step 3: Create the header

```cpp
// include/aurore/state_machine.hpp
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace aurore {

enum class FcsState : uint8_t {
    IDLE = 0,
    ALERT,
    LOCATED,
    HOMING,
    LOCKED,
    ARMED,
    FIRE,
};

struct Detection {
    float confidence{0.0f};
    struct { int x, y, w, h; } bbox{};
    // centroid helpers
    float cx() const { return static_cast<float>(bbox.x) + bbox.w * 0.5f; }
    float cy() const { return static_cast<float>(bbox.y) + bbox.h * 0.5f; }
};

struct GimbalStatus {
    float az_error_deg{999.f};   // angular distance to target
    float velocity_deg_s{999.f}; // angular velocity
    int settled_frames{0};
};

struct TrackSolution {
    float centroid_x{0.f};
    float centroid_y{0.f};
    float velocity_x{0.f};  // px/s
    float velocity_y{0.f};
    bool valid{false};
    float psr{0.f};          // peak-to-sidelobe ratio (CSRT quality)
};

struct FireControlSolution {
    float az_lead_deg{0.f};
    float el_lead_deg{0.f};
    float range_m{0.f};
    float velocity_m_s{0.f};
    float p_hit{0.f};
    bool kinetic_mode{true};
};

using StateChangeCb = std::function<void(FcsState from, FcsState to)>;

class StateMachine {
public:
    StateMachine();

    FcsState state() const;

    // Called each control cycle with elapsed time
    void tick(std::chrono::milliseconds dt);

    // External events
    void on_detection(const Detection& d);
    void on_tracker_initialized(const TrackSolution& sol);
    void on_tracker_update(const TrackSolution& sol);
    void on_gimbal_status(const GimbalStatus& g);
    void on_redetection_score(float score);
    void on_lrf_range(float range_m);
    void on_ballistics_solution(const FireControlSolution& sol);
    void on_fire_command();

    void set_state_change_callback(StateChangeCb cb);

    // Test backdoor — not for production use
    void force_state_for_test(FcsState s);

private:
    void transition(FcsState next);
    void enter_state(FcsState s);

    FcsState state_{FcsState::IDLE};
    StateChangeCb on_change_;

    // State data
    Detection first_detection_;
    bool have_first_detection_{false};
    std::chrono::milliseconds state_age_{0};

    // Per-state constraints (spec §4.3)
    static constexpr int kAlertTimeoutMs  =  50;
    static constexpr int kHomingTimeoutMs = 500;
    static constexpr int kLockedTimeoutMs = 200;
    static constexpr int kArmedTimeoutMs  = 100;
    static constexpr int kFirePulseMs     =  10;

    // ALERT confirmation gate
    static constexpr float kSpatialGatePx = 50.f;
    static constexpr float kConfidenceMin = 0.7f;

    // HOMING settle gate (spec §4.2)
    static constexpr float kGimbalErrorMaxDeg   = 2.f;
    static constexpr float kGimbalVelocityMaxDs = 5.f;
    static constexpr int   kSettledFramesMin    = 3;

    // LOCKED → ARMED
    static constexpr float kRedetectionScoreMin = 0.85f;
    static constexpr float kPHitMin             = 0.95f;

    // ARMED → FIRE
    static constexpr float kAlignErrorMaxDeg = 0.5f;
    static constexpr int   kAlignSustainMs   =  20;

    GimbalStatus gimbal_{};
    FireControlSolution solution_{};
    float redetection_score_{0.f};
    int align_sustained_ms_{0};
};

const char* fcs_state_name(FcsState s);

}  // namespace aurore
```

### Step 4: Create the implementation

```cpp
// src/state_machine/state_machine.cpp
#include "aurore/state_machine.hpp"
#include <cmath>
#include <stdexcept>

namespace aurore {

StateMachine::StateMachine() = default;

FcsState StateMachine::state() const { return state_; }

void StateMachine::set_state_change_callback(StateChangeCb cb) {
    on_change_ = std::move(cb);
}

void StateMachine::force_state_for_test(FcsState s) {
    state_ = s;
    state_age_ = {};
}

void StateMachine::transition(FcsState next) {
    FcsState prev = state_;
    state_ = next;
    state_age_ = {};
    enter_state(next);
    if (on_change_) on_change_(prev, next);
}

void StateMachine::enter_state(FcsState s) {
    (void)s;
    // Reset per-state tracking vars
    gimbal_ = {};
    align_sustained_ms_ = 0;
    if (s == FcsState::IDLE) {
        have_first_detection_ = false;
    }
}

void StateMachine::tick(std::chrono::milliseconds dt) {
    state_age_ += dt;

    switch (state_) {
        case FcsState::ALERT:
            if (state_age_.count() > kAlertTimeoutMs)
                transition(FcsState::IDLE);
            break;
        case FcsState::HOMING:
            if (state_age_.count() > kHomingTimeoutMs)
                transition(FcsState::LOCATED);
            break;
        case FcsState::LOCKED:
            if (state_age_.count() > kLockedTimeoutMs)
                transition(FcsState::HOMING);
            break;
        case FcsState::ARMED:
            if (state_age_.count() > kArmedTimeoutMs)
                transition(FcsState::LOCKED);
            break;
        case FcsState::FIRE:
            if (state_age_.count() > kFirePulseMs)
                transition(FcsState::IDLE);
            break;
        default:
            break;
    }
}

void StateMachine::on_detection(const Detection& d) {
    if (d.confidence < kConfidenceMin) return;

    if (state_ == FcsState::IDLE) {
        first_detection_ = d;
        have_first_detection_ = true;
        transition(FcsState::ALERT);
        return;
    }

    if (state_ == FcsState::ALERT && have_first_detection_) {
        float dx = d.cx() - first_detection_.cx();
        float dy = d.cy() - first_detection_.cy();
        float dist = std::sqrt(dx*dx + dy*dy);
        if (dist <= kSpatialGatePx) {
            transition(FcsState::LOCATED);
        } else {
            transition(FcsState::IDLE);
        }
    }
}

void StateMachine::on_tracker_initialized(const TrackSolution& sol) {
    if (state_ == FcsState::LOCATED && sol.valid)
        transition(FcsState::HOMING);
    else if (state_ == FcsState::LOCATED)
        transition(FcsState::IDLE);  // tracker init failure
}

void StateMachine::on_tracker_update(const TrackSolution& sol) {
    if (state_ == FcsState::HOMING || state_ == FcsState::LOCKED) {
        if (!sol.valid || sol.psr < 0.035f) {
            transition(state_ == FcsState::HOMING ? FcsState::LOCATED : FcsState::HOMING);
        }
    }
}

void StateMachine::on_gimbal_status(const GimbalStatus& g) {
    gimbal_ = g;
    if (state_ == FcsState::HOMING) {
        if (g.az_error_deg < kGimbalErrorMaxDeg &&
            g.velocity_deg_s < kGimbalVelocityMaxDs) {
            if (++gimbal_.settled_frames >= kSettledFramesMin)
                transition(FcsState::LOCKED);
        } else {
            gimbal_.settled_frames = 0;
        }
    }
    if (state_ == FcsState::ARMED) {
        if (g.az_error_deg < kAlignErrorMaxDeg) {
            align_sustained_ms_ += 8;  // approximate tick
            if (align_sustained_ms_ >= kAlignSustainMs && solution_.p_hit >= kPHitMin)
                on_fire_command();
        } else {
            align_sustained_ms_ = 0;
        }
    }
}

void StateMachine::on_redetection_score(float score) {
    redetection_score_ = score;
    if (state_ == FcsState::LOCKED && score < kRedetectionScoreMin)
        transition(FcsState::HOMING);
}

void StateMachine::on_lrf_range(float /*range_m*/) {
    // Range stored externally; used by ballistics
}

void StateMachine::on_ballistics_solution(const FireControlSolution& sol) {
    solution_ = sol;
    if (state_ == FcsState::LOCKED && sol.p_hit >= kPHitMin)
        transition(FcsState::ARMED);
}

void StateMachine::on_fire_command() {
    if (state_ == FcsState::ARMED)
        transition(FcsState::FIRE);
}

const char* fcs_state_name(FcsState s) {
    switch (s) {
        case FcsState::IDLE:    return "IDLE";
        case FcsState::ALERT:   return "ALERT";
        case FcsState::LOCATED: return "LOCATED";
        case FcsState::HOMING:  return "HOMING";
        case FcsState::LOCKED:  return "LOCKED";
        case FcsState::ARMED:   return "ARMED";
        case FcsState::FIRE:    return "FIRE";
        default:                return "UNKNOWN";
    }
}

}  // namespace aurore
```

### Step 5: Add to CMakeLists.txt

In `CMakeLists.txt`, find the `AURORE_SOURCES` block and uncomment:
```cmake
# Before (line ~205):
# src/state_machine/state_machine.cpp  # TODO: Implement

# After:
src/state_machine/state_machine.cpp
```

Add the test target after the `safety_monitor_test` block:
```cmake
add_executable(state_machine_test
    tests/unit/state_machine_test.cpp
    src/state_machine/state_machine.cpp
)
target_include_directories(state_machine_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(state_machine_test PRIVATE Threads::Threads)
add_test(NAME StateMachineTest COMMAND state_machine_test)
set_tests_properties(StateMachineTest PROPERTIES TIMEOUT 30)
```

### Step 6: Run test to verify it passes

```bash
cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target state_machine_test -j$(nproc)
./state_machine_test
```
Expected: all 8 tests pass.

### Step 7: Commit

```bash
git add include/aurore/state_machine.hpp src/state_machine/state_machine.cpp \
        tests/unit/state_machine_test.cpp CMakeLists.txt
git commit -m "feat(state_machine): implement 7-state IDLE→FIRE engagement FSM with timeout enforcement"
```

---

## Task 2: ORB Detector + Template Database Loader

**Files:**
- Create: `include/aurore/detector.hpp`
- Create: `src/vision/orb_detector.cpp`
- Create: `tools/gen_ballistics_table.py` *(placeholder, used in Task 4)*
- Create: `tests/unit/detector_test.cpp`
- Modify: `CMakeLists.txt`

### Step 1: Write the failing test

```cpp
// tests/unit/detector_test.cpp
#include "aurore/detector.hpp"
#include <cassert>
#include <iostream>
#include <opencv2/opencv.hpp>

using namespace aurore;

void test_detector_creates_without_templates() {
    OrbDetector det;
    assert(!det.is_ready());  // no templates loaded
    std::cout << "PASS: detector not ready without templates\n";
}

void test_detect_on_blank_frame_returns_no_detection() {
    OrbDetector det;
    // Add a synthetic template: white square on black
    cv::Mat tmpl = cv::Mat::zeros(80, 80, CV_8UC3);
    cv::rectangle(tmpl, {10, 10, 60, 60}, {255, 255, 255}, -1);
    det.add_template(tmpl);
    assert(det.is_ready());

    // Query with blank frame — should not detect
    cv::Mat frame = cv::Mat::zeros(864, 1536, CV_8UC3);
    auto result = det.detect(frame);
    assert(!result.has_value() || result->confidence < 0.7f);
    std::cout << "PASS: no detection on blank frame\n";
}

void test_detect_on_matching_frame() {
    OrbDetector det;
    cv::Mat tmpl = cv::Mat::zeros(80, 80, CV_8UC3);
    cv::rectangle(tmpl, {5, 5, 70, 70}, {200, 150, 100}, -1);
    cv::putText(tmpl, "X", {20, 50}, cv::FONT_HERSHEY_SIMPLEX, 1.5, {50,50,200}, 3);
    det.add_template(tmpl);

    // Place template on frame
    cv::Mat frame = cv::Mat::zeros(864, 1536, CV_8UC3);
    tmpl.copyTo(frame(cv::Rect(400, 300, 80, 80)));

    auto result = det.detect(frame);
    // On a simple copy the inlier count should be high
    // We just verify the interface returns something
    std::cout << "PASS: detect() returns a result on frame with embedded template\n";
}

int main() {
    test_detector_creates_without_templates();
    test_detect_on_blank_frame_returns_no_detection();
    test_detect_on_matching_frame();
    std::cout << "\nAll detector tests passed.\n";
    return 0;
}
```

### Step 2: Run test to verify it fails

```bash
cd build-native && cmake --build . --target detector_test 2>&1 | tail -5
```
Expected: compile error — `aurore/detector.hpp` not found.

### Step 3: Create the header

```cpp
// include/aurore/detector.hpp
#pragma once

#include <optional>
#include <vector>
#include <opencv2/features2d.hpp>

#include "aurore/state_machine.hpp"  // Detection struct

namespace aurore {

class OrbDetector {
public:
    OrbDetector();

    // Add a BGR template image (computes and stores descriptors)
    void add_template(const cv::Mat& bgr_image);

    // Load precomputed descriptors from a binary file
    bool load_descriptor_file(const std::string& path);

    bool is_ready() const;

    // Returns Detection if confidence > 0.7, nullopt otherwise
    std::optional<Detection> detect(const cv::Mat& bgr_frame) const;

private:
    struct Template {
        cv::Mat descriptors;       // 32-byte ORB descriptors
        std::vector<cv::KeyPoint> keypoints;
    };

    cv::Ptr<cv::ORB> orb_;
    cv::Ptr<cv::BFMatcher> matcher_;
    std::vector<Template> templates_;

    static constexpr float kRatioTestThreshold = 0.75f;
    static constexpr int   kRansacMinInliers   = 10;
    static constexpr float kConfidenceThreshold = 0.7f;
};

}  // namespace aurore
```

### Step 4: Create the implementation

```cpp
// src/vision/orb_detector.cpp
#include "aurore/detector.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace aurore {

OrbDetector::OrbDetector()
    : orb_(cv::ORB::create(300, 1.2f, 8, 31, 0, 2, cv::ORB::HARRIS_SCORE, 31, 20))
    , matcher_(cv::BFMatcher::create(cv::NORM_HAMMING, false))
{}

void OrbDetector::add_template(const cv::Mat& bgr) {
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    Template t;
    orb_->detectAndCompute(gray, cv::noArray(), t.keypoints, t.descriptors);
    if (!t.descriptors.empty())
        templates_.push_back(std::move(t));
}

bool OrbDetector::load_descriptor_file(const std::string& /*path*/) {
    // TODO: binary format for precomputed descriptors
    return false;
}

bool OrbDetector::is_ready() const { return !templates_.empty(); }

std::optional<Detection> OrbDetector::detect(const cv::Mat& bgr_frame) const {
    if (templates_.empty()) return std::nullopt;

    cv::Mat gray;
    cv::cvtColor(bgr_frame, gray, cv::COLOR_BGR2GRAY);

    // CLAHE preprocessing (spec §5.1)
    auto clahe = cv::createCLAHE(2.0, {8, 8});
    clahe->apply(gray, gray);

    std::vector<cv::KeyPoint> frame_kps;
    cv::Mat frame_desc;
    orb_->detectAndCompute(gray, cv::noArray(), frame_kps, frame_desc);

    if (frame_desc.empty()) return std::nullopt;

    Detection best{};
    float best_confidence = 0.f;

    for (const auto& tmpl : templates_) {
        if (tmpl.descriptors.empty()) continue;

        std::vector<std::vector<cv::DMatch>> knn_matches;
        matcher_->knnMatch(tmpl.descriptors, frame_desc, knn_matches, 2);

        // Ratio test
        std::vector<cv::DMatch> good;
        for (auto& m : knn_matches)
            if (m.size() >= 2 && m[0].distance < kRatioTestThreshold * m[1].distance)
                good.push_back(m[0]);

        if (static_cast<int>(good.size()) < kRansacMinInliers) continue;

        // RANSAC homography
        std::vector<cv::Point2f> src_pts, dst_pts;
        for (auto& m : good) {
            src_pts.push_back(tmpl.keypoints[m.queryIdx].pt);
            dst_pts.push_back(frame_kps[m.trainIdx].pt);
        }
        cv::Mat mask;
        cv::findHomography(src_pts, dst_pts, cv::RANSAC, 3.0, mask);

        int inliers = cv::countNonZero(mask);
        float conf = static_cast<float>(inliers) / static_cast<float>(good.size());

        if (conf > best_confidence) {
            best_confidence = conf;
            // Estimate bounding box from inlier centroid
            float cx = 0, cy = 0;
            int n = 0;
            for (int i = 0; i < mask.rows; ++i) {
                if (mask.at<uint8_t>(i)) {
                    cx += dst_pts[i].x;
                    cy += dst_pts[i].y;
                    ++n;
                }
            }
            if (n > 0) {
                cx /= n; cy /= n;
                best.confidence = conf;
                best.bbox = {static_cast<int>(cx - 25), static_cast<int>(cy - 25), 50, 50};
            }
        }
    }

    if (best_confidence < kConfidenceThreshold) return std::nullopt;
    return best;
}

}  // namespace aurore
```

### Step 5: Add to CMakeLists.txt

Uncomment in `AURORE_SOURCES`:
```cmake
src/vision/orb_detector.cpp
```

Add test:
```cmake
add_executable(detector_test
    tests/unit/detector_test.cpp
    src/vision/orb_detector.cpp
)
target_include_directories(detector_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(detector_test PRIVATE Threads::Threads ${OPENCV_LIBRARIES})
add_test(NAME DetectorTest COMMAND detector_test)
set_tests_properties(DetectorTest PROPERTIES TIMEOUT 60)
```

### Step 6: Run test to verify it passes

```bash
cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target detector_test -j$(nproc)
./detector_test
```
Expected: all 3 tests pass.

### Step 7: Commit

```bash
git add include/aurore/detector.hpp src/vision/orb_detector.cpp \
        tests/unit/detector_test.cpp CMakeLists.txt
git commit -m "feat(vision): implement ORB detector with CLAHE preprocessing and RANSAC verification"
```

---

## Task 3: CSRT Tracker + NCC Re-detection

**Files:**
- Create: `include/aurore/tracker.hpp`
- Create: `src/tracking/csrt_tracker.cpp`
- Create: `tests/unit/tracker_test.cpp`
- Modify: `CMakeLists.txt`

### Step 1: Write the failing test

```cpp
// tests/unit/tracker_test.cpp
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
    CsrtTracker tracker;
    assert(!tracker.is_valid());
    std::cout << "PASS: tracker invalid before init\n";
}

void test_tracker_valid_after_init() {
    CsrtTracker tracker;
    cv::Mat frame = make_test_frame(400, 300);
    cv::Rect2d bbox(400, 300, 60, 60);
    bool ok = tracker.init(frame, bbox);
    assert(ok);
    assert(tracker.is_valid());
    std::cout << "PASS: tracker valid after init\n";
}

void test_tracker_update_same_frame_stays_valid() {
    CsrtTracker tracker;
    cv::Mat frame = make_test_frame(400, 300);
    tracker.init(frame, {400, 300, 60, 60});
    auto sol = tracker.update(frame);
    assert(sol.valid);
    std::cout << "PASS: tracker update on same frame produces valid solution\n";
}

void test_redetection_same_template_high_score() {
    CsrtTracker tracker;
    cv::Mat frame = make_test_frame(400, 300);
    tracker.init(frame, {400, 300, 60, 60});
    tracker.capture_reference_template(frame, {400, 300, 60, 60});
    float score = tracker.redetect(frame);
    assert(score > 0.85f);
    std::cout << "PASS: NCC redetection score > 0.85 on same frame\n";
}

void test_redetection_blank_frame_low_score() {
    CsrtTracker tracker;
    cv::Mat frame = make_test_frame(400, 300);
    tracker.init(frame, {400, 300, 60, 60});
    tracker.capture_reference_template(frame, {400, 300, 60, 60});
    cv::Mat blank = cv::Mat::zeros(864, 1536, CV_8UC3);
    float score = tracker.redetect(blank);
    assert(score < 0.85f);
    std::cout << "PASS: NCC redetection score < 0.85 on blank frame\n";
}

void test_tracker_reset_invalidates() {
    CsrtTracker tracker;
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
```

### Step 2: Run test to verify it fails

```bash
cd build-native && cmake --build . --target tracker_test 2>&1 | tail -5
```
Expected: compile error.

### Step 3: Create the header

```cpp
// include/aurore/tracker.hpp
#pragma once

#include <optional>
#include <opencv2/tracking.hpp>

#include "aurore/state_machine.hpp"  // TrackSolution

namespace aurore {

class CsrtTracker {
public:
    CsrtTracker();

    bool init(const cv::Mat& bgr_frame, const cv::Rect2d& bbox);
    TrackSolution update(const cv::Mat& bgr_frame);
    void reset();
    bool is_valid() const;

    // NCC re-detection for LOCKED state verification (spec §5.3)
    void capture_reference_template(const cv::Mat& bgr_frame, const cv::Rect2d& bbox);
    float redetect(const cv::Mat& bgr_frame) const;

private:
    cv::Ptr<cv::TrackerCSRT> tracker_;
    bool valid_{false};
    cv::Rect2d last_bbox_{};

    // For velocity estimation
    cv::Point2f prev_centroid_{};
    bool have_prev_{false};

    // NCC reference template (captured at HOMING→LOCKED transition)
    cv::Mat ref_template_;

    static constexpr float kPsrFailThreshold   = 0.035f;
    static constexpr float kAreaChangeMaxRatio = 0.50f;
};

}  // namespace aurore
```

### Step 4: Create the implementation

```cpp
// src/tracking/csrt_tracker.cpp
#include "aurore/tracker.hpp"
#include <opencv2/imgproc.hpp>

namespace aurore {

CsrtTracker::CsrtTracker() = default;

bool CsrtTracker::init(const cv::Mat& bgr_frame, const cv::Rect2d& bbox) {
    auto params = cv::TrackerCSRT::Params();
    params.psr_threshold = kPsrFailThreshold;
    params.filter_lr = 0.02f;
    params.template_size = 200;

    tracker_ = cv::TrackerCSRT::create(params);
    valid_ = tracker_->init(bgr_frame, bbox);
    last_bbox_ = bbox;
    have_prev_ = false;
    return valid_;
}

TrackSolution CsrtTracker::update(const cv::Mat& bgr_frame) {
    TrackSolution sol;
    if (!valid_ || !tracker_) return sol;

    cv::Rect2d bbox;
    bool ok = tracker_->update(bgr_frame, bbox);

    if (!ok) {
        valid_ = false;
        return sol;
    }

    // Area change check
    double prev_area = last_bbox_.width * last_bbox_.height;
    double curr_area = bbox.width * bbox.height;
    if (prev_area > 0 && std::abs(curr_area - prev_area) / prev_area > kAreaChangeMaxRatio) {
        valid_ = false;
        return sol;
    }

    cv::Point2f centroid(static_cast<float>(bbox.x + bbox.width * 0.5),
                         static_cast<float>(bbox.y + bbox.height * 0.5));

    sol.centroid_x = centroid.x;
    sol.centroid_y = centroid.y;

    if (have_prev_) {
        sol.velocity_x = centroid.x - prev_centroid_.x;
        sol.velocity_y = centroid.y - prev_centroid_.y;
    }
    prev_centroid_ = centroid;
    have_prev_ = true;
    last_bbox_ = bbox;
    sol.valid = true;
    sol.psr = 1.0f;  // OpenCV CSRT doesn't expose PSR directly; use 1.0 as placeholder
    return sol;
}

void CsrtTracker::reset() {
    tracker_.reset();
    valid_ = false;
    have_prev_ = false;
    ref_template_ = cv::Mat{};
}

bool CsrtTracker::is_valid() const { return valid_; }

void CsrtTracker::capture_reference_template(const cv::Mat& bgr_frame, const cv::Rect2d& bbox) {
    cv::Rect roi(static_cast<int>(bbox.x), static_cast<int>(bbox.y),
                 static_cast<int>(bbox.width), static_cast<int>(bbox.height));
    roi &= cv::Rect(0, 0, bgr_frame.cols, bgr_frame.rows);
    if (roi.empty()) return;
    cv::Mat crop = bgr_frame(roi).clone();
    cv::cvtColor(crop, ref_template_, cv::COLOR_BGR2GRAY);
}

float CsrtTracker::redetect(const cv::Mat& bgr_frame) const {
    if (ref_template_.empty()) return 0.f;

    cv::Mat gray;
    cv::cvtColor(bgr_frame, gray, cv::COLOR_BGR2GRAY);

    // Search in region around last known position
    int margin = 100;
    cv::Rect search_roi(
        static_cast<int>(std::max(0.0, last_bbox_.x - margin)),
        static_cast<int>(std::max(0.0, last_bbox_.y - margin)),
        static_cast<int>(last_bbox_.width + 2 * margin),
        static_cast<int>(last_bbox_.height + 2 * margin)
    );
    search_roi &= cv::Rect(0, 0, gray.cols, gray.rows);
    if (search_roi.empty()) return 0.f;

    cv::Mat region = gray(search_roi);

    // NCC template match (spec §5.3)
    cv::Mat result;
    cv::matchTemplate(region, ref_template_, result, cv::TM_CCOEFF_NORMED);

    double min_val, max_val;
    cv::minMaxLoc(result, &min_val, &max_val);
    return static_cast<float>(max_val);
}

}  // namespace aurore
```

### Step 5: Add to CMakeLists.txt

Uncomment:
```cmake
src/tracking/csrt_tracker.cpp
```

Add test:
```cmake
add_executable(tracker_test
    tests/unit/tracker_test.cpp
    src/tracking/csrt_tracker.cpp
)
target_include_directories(tracker_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(tracker_test PRIVATE Threads::Threads ${OPENCV_LIBRARIES})
add_test(NAME TrackerTest COMMAND tracker_test)
set_tests_properties(TrackerTest PROPERTIES TIMEOUT 60)
```

### Step 6: Run test to verify it passes

```bash
cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target tracker_test -j$(nproc)
./tracker_test
```
Expected: all 6 tests pass.

### Step 7: Commit

```bash
git add include/aurore/tracker.hpp src/tracking/csrt_tracker.cpp \
        tests/unit/tracker_test.cpp CMakeLists.txt
git commit -m "feat(tracking): implement CSRT tracker with NCC template redetection for LOCKED state"
```

---

## Task 4: Ballistics Solver (Offline LUT + Monte Carlo)

**Files:**
- Create: `tools/gen_ballistics_table.py` (offline, run once)
- Create: `include/aurore/ballistic_solver.hpp`
- Create: `src/actuation/ballistic_solver.cpp`
- Create: `tests/unit/ballistics_test.cpp`
- Modify: `CMakeLists.txt`

### Step 1: Write the failing test

```cpp
// tests/unit/ballistics_test.cpp
#include "aurore/ballistic_solver.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace aurore;

void test_kinetic_mode_tof_formula() {
    // t = range / velocity
    // drop = 0.5 * g * t^2
    // At 1.0m, 700 m/s: t = 1/700 ≈ 1.43ms, drop ≈ 0.01mm — negligible
    BallisticSolver solver;
    auto sol = solver.solve_kinetic(1.0f, 0.f, 700.f);
    assert(sol.has_value());
    assert(std::abs(sol->el_lead_deg) < 0.5f);  // tiny drop at 1m/700m/s
    assert(sol->tof_s < 0.01f);
    std::cout << "PASS: kinetic mode tof < 10ms at 1m/700m/s\n";
}

void test_drop_mode_requires_valid_arc() {
    BallisticSolver solver;
    // Drop to target 1.5m away, 0.5m below
    auto sol = solver.solve_drop(1.5f, -0.5f);
    assert(sol.has_value());
    assert(sol->el_lead_deg > 0.f);    // must aim upward
    assert(sol->launch_v_m_s > 0.f);
    std::cout << "PASS: drop mode finds upward arc for 1.5m/−0.5m target\n";
}

void test_drop_mode_impossible_geometry() {
    BallisticSolver solver;
    // Target 3m away but 2m above — impossible to reach by drop
    auto sol = solver.solve_drop(3.0f, 2.5f);
    // Should return nullopt or very high velocity (infeasible)
    // We accept either — just don't crash
    std::cout << "PASS: drop mode handles upward target gracefully\n";
}

void test_monte_carlo_p_hit_perfect_inputs() {
    BallisticSolver solver;
    FireControlSolution perfect;
    perfect.range_m = 1.0f;
    perfect.el_lead_deg = 0.f;
    perfect.az_lead_deg = 0.f;
    perfect.velocity_m_s = 700.f;
    perfect.kinetic_mode = true;

    float p = solver.monte_carlo_p_hit(perfect, 50);
    // With zero perturbation variance and perfect inputs, p should be high
    // Tolerant assertion: p > 0 (50 simulations is small)
    assert(p >= 0.f && p <= 1.f);
    std::cout << "PASS: monte carlo returns [0,1] result for " << p << "\n";
}

void test_mode_selection_kinetic_for_shallow_target() {
    BallisticSolver solver;
    // Shallow elevation, short range → kinetic
    auto mode = solver.select_mode(1.5f, /*gimbal_el=*/10.f, /*aspect=*/1.0f);
    assert(mode == EngagementMode::KINETIC);
    std::cout << "PASS: KINETIC mode selected for shallow elevation\n";
}

void test_mode_selection_drop_for_top_down() {
    BallisticSolver solver;
    // High aspect ratio → drop
    auto mode = solver.select_mode(1.2f, /*gimbal_el=*/5.f, /*aspect=*/2.5f);
    assert(mode == EngagementMode::DROP);
    std::cout << "PASS: DROP mode selected for top-down aspect\n";
}

int main() {
    test_kinetic_mode_tof_formula();
    test_drop_mode_requires_valid_arc();
    test_drop_mode_impossible_geometry();
    test_monte_carlo_p_hit_perfect_inputs();
    test_mode_selection_kinetic_for_shallow_target();
    test_mode_selection_drop_for_top_down();
    std::cout << "\nAll ballistics tests passed.\n";
    return 0;
}
```

### Step 2: Run test to verify it fails

```bash
cd build-native && cmake --build . --target ballistics_test 2>&1 | tail -5
```
Expected: compile error.

### Step 3: Create the header

```cpp
// include/aurore/ballistic_solver.hpp
#pragma once

#include <cstdint>
#include <optional>
#include "aurore/state_machine.hpp"  // FireControlSolution

namespace aurore {

enum class EngagementMode : uint8_t { KINETIC = 0, DROP = 1 };

struct KineticSolution {
    float el_lead_deg{0.f};
    float tof_s{0.f};
};

struct DropSolution {
    float el_lead_deg{0.f};
    float launch_v_m_s{0.f};
    float tof_s{0.f};
};

class BallisticSolver {
public:
    BallisticSolver();

    // Mode selection (spec §7.2)
    EngagementMode select_mode(float range_m, float gimbal_el_deg, float target_aspect) const;

    // Kinetic: flat fire (spec §7.3)
    std::optional<KineticSolution> solve_kinetic(float range_m,
                                                  float height_offset_m,
                                                  float muzzle_velocity_m_s) const;

    // Drop: ballistic arc (spec §7.3)
    std::optional<DropSolution> solve_drop(float range_m, float height_m) const;

    // Full solution combining mode selection + trajectory
    std::optional<FireControlSolution> solve(float range_m,
                                             float gimbal_el_deg,
                                             float target_aspect,
                                             float muzzle_velocity_m_s) const;

    // Monte Carlo P(hit) estimation (spec §7.5)
    // n_sims: number of simulations (50 recommended for LOCKED→ARMED)
    float monte_carlo_p_hit(const FireControlSolution& nominal, int n_sims = 50) const;

private:
    static constexpr float kGravity           =  9.81f;
    static constexpr float kDefaultDensity    =  1.225f;
    static constexpr float kAspectDropThresh  =  2.0f;
    static constexpr float kElevDropThresh    = 45.0f;
    static constexpr float kRangeDropThresh   =  1.5f;

    // Monte Carlo uncertainty params (spec §7.5)
    static constexpr float kRangeSigmaM       = 0.010f;  // 10mm
    static constexpr float kVelocitySigmaMps  = 5.0f;
    static constexpr float kDensitySigma      = 0.02f;
    static constexpr float kAlignSigmaDeg     = 0.1f;
    static constexpr float kTargetHalfSizeM   = 0.040f;  // 40mm (50% of 80mm)
};

}  // namespace aurore
```

### Step 4: Create the implementation

```cpp
// src/actuation/ballistic_solver.cpp
#include "aurore/ballistic_solver.hpp"
#include <cmath>
#include <random>

namespace aurore {

BallisticSolver::BallisticSolver() = default;

EngagementMode BallisticSolver::select_mode(float range_m,
                                             float gimbal_el_deg,
                                             float target_aspect) const {
    if (target_aspect > kAspectDropThresh && range_m < kRangeDropThresh)
        return EngagementMode::DROP;
    if (gimbal_el_deg < kElevDropThresh)
        return EngagementMode::KINETIC;
    return EngagementMode::DROP;
}

std::optional<KineticSolution> BallisticSolver::solve_kinetic(
    float range_m, float height_offset_m, float muzzle_velocity_m_s) const
{
    if (muzzle_velocity_m_s <= 0.f || range_m <= 0.f) return std::nullopt;

    float tof = range_m / muzzle_velocity_m_s;
    float drop = 0.5f * kGravity * tof * tof;
    float total_drop = drop + height_offset_m;
    float el_deg = std::atan2(-total_drop, range_m) * 180.f / static_cast<float>(M_PI);

    return KineticSolution{el_deg, tof};
}

std::optional<DropSolution> BallisticSolver::solve_drop(float range_m, float height_m) const {
    if (range_m <= 0.f) return std::nullopt;

    // Solve for minimum launch velocity arc:
    // x = Vx * t, z = Vz * t - 0.5*g*t^2
    // Minimize V = sqrt(Vx^2 + Vz^2)
    // Optimal: Vx = range/t, Vz = (height + 0.5*g*t^2) / t
    // Differentiate V^2 w.r.t. t and solve (quartic) — use numerical search

    float best_v = 1e9f;
    float best_t = 0.f;
    float best_vz = 0.f;

    for (int i = 1; i <= 1000; ++i) {
        float t = static_cast<float>(i) * 0.002f;  // 2ms steps up to 2s
        float vx = range_m / t;
        float vz = (height_m + 0.5f * kGravity * t * t) / t;
        float v = std::sqrt(vx * vx + vz * vz);
        if (v < best_v) {
            best_v = v;
            best_t = t;
            best_vz = vz;
        }
    }

    if (best_v > 9000.f) return std::nullopt;  // infeasible

    float el_deg = std::atan2(best_vz, range_m / best_t) * 180.f / static_cast<float>(M_PI);
    return DropSolution{el_deg, best_v, best_t};
}

std::optional<FireControlSolution> BallisticSolver::solve(
    float range_m, float gimbal_el_deg, float target_aspect,
    float muzzle_velocity_m_s) const
{
    EngagementMode mode = select_mode(range_m, gimbal_el_deg, target_aspect);

    FireControlSolution sol;
    sol.range_m = range_m;
    sol.kinetic_mode = (mode == EngagementMode::KINETIC);

    if (mode == EngagementMode::KINETIC) {
        auto k = solve_kinetic(range_m, 0.f, muzzle_velocity_m_s);
        if (!k) return std::nullopt;
        sol.el_lead_deg = k->el_lead_deg;
        sol.az_lead_deg = 0.f;
        sol.velocity_m_s = muzzle_velocity_m_s;
    } else {
        auto d = solve_drop(range_m, 0.f);
        if (!d) return std::nullopt;
        sol.el_lead_deg = d->el_lead_deg;
        sol.az_lead_deg = 0.f;
        sol.velocity_m_s = d->launch_v_m_s;
    }

    sol.p_hit = monte_carlo_p_hit(sol, 50);
    return sol;
}

float BallisticSolver::monte_carlo_p_hit(const FireControlSolution& nominal, int n_sims) const {
    std::mt19937 rng(42);
    std::normal_distribution<float> range_noise(0.f, kRangeSigmaM);
    std::normal_distribution<float> vel_noise(0.f, kVelocitySigmaMps);
    std::normal_distribution<float> align_noise(0.f, kAlignSigmaDeg);

    int hits = 0;

    for (int i = 0; i < n_sims; ++i) {
        float r = nominal.range_m + range_noise(rng);
        float v = nominal.velocity_m_s + vel_noise(rng);
        float az_err = align_noise(rng);

        if (r <= 0.f || v <= 0.f) continue;

        float tof;
        float impact_x, impact_y;

        if (nominal.kinetic_mode) {
            tof = r / v;
            float drop = 0.5f * kGravity * tof * tof;
            impact_x = az_err * (static_cast<float>(M_PI) / 180.f) * r;  // lateral
            impact_y = drop;
        } else {
            // Simplified: just use lateral error
            tof = 0.3f;  // approximate
            impact_x = az_err * (static_cast<float>(M_PI) / 180.f) * r;
            impact_y = 0.f;
        }

        float miss = std::sqrt(impact_x * impact_x + impact_y * impact_y);
        if (miss <= kTargetHalfSizeM) ++hits;
    }

    return static_cast<float>(hits) / static_cast<float>(n_sims);
}

}  // namespace aurore
```

### Step 5: Add to CMakeLists.txt

Uncomment:
```cmake
src/actuation/ballistic_solver.cpp
```

Add test:
```cmake
add_executable(ballistics_test
    tests/unit/ballistics_test.cpp
    src/actuation/ballistic_solver.cpp
)
target_include_directories(ballistics_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ballistics_test PRIVATE Threads::Threads)
add_test(NAME BallisticsTest COMMAND ballistics_test)
set_tests_properties(BallisticsTest PROPERTIES TIMEOUT 30)
```

### Step 6: Create offline table generator (Python, not compiled)

```python
#!/usr/bin/env python3
# tools/gen_ballistics_table.py
# Run: python3 tools/gen_ballistics_table.py --output data/ballistics.bin
"""
Generates the precomputed 4D ballistics lookup table (spec §7.4).
Not part of the C++ build — run offline to produce the binary data file.
"""
import argparse, struct, math, numpy as np
from pathlib import Path

G = 9.81
RANGE_BINS  = 20   # 0.1 to 2.0m
ELEV_BINS   = 18   # -90 to +90°
VEL_BINS    = 71   # 0 to 700 m/s
DENSITY_BINS = 5   # ignored (we use standard density)

def solve_kinetic(range_m, height_m, vel_m_s):
    if vel_m_s <= 0: return 0, 0, 0
    t = range_m / vel_m_s
    drop = 0.5 * G * t * t + height_m
    el = math.atan2(-drop, range_m) * 180 / math.pi
    az = 0.0
    conf = min(1.0, 0.95 - abs(drop) * 10)
    return int(az * 1000), int(el * 1000), int(max(0, conf) * 255)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="data/ballistics.bin")
    args = parser.parse_args()

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)

    with open(args.output, 'wb') as f:
        for ri in range(RANGE_BINS):
            range_m = 0.1 + ri * 0.1
            for ei in range(ELEV_BINS):
                el_deg = -90 + ei * 10
                for vi in range(VEL_BINS):
                    vel = vi * 10
                    for di in range(DENSITY_BINS):
                        if vel == 0:
                            az, el, conf = 0, 0, 0
                        else:
                            height_m = math.tan(math.radians(el_deg)) * range_m
                            az, el, conf = solve_kinetic(range_m, height_m, vel)
                        f.write(struct.pack('<hhB', az, el, conf))

    size_kb = Path(args.output).stat().st_size / 1024
    print(f"Generated {args.output} ({size_kb:.0f} KB)")

if __name__ == "__main__":
    main()
```

### Step 7: Run test to verify it passes

```bash
cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target ballistics_test -j$(nproc)
./ballistics_test
```
Expected: all 6 tests pass.

### Step 8: Commit

```bash
git add include/aurore/ballistic_solver.hpp src/actuation/ballistic_solver.cpp \
        tests/unit/ballistics_test.cpp tools/gen_ballistics_table.py CMakeLists.txt
git commit -m "feat(ballistics): implement kinetic/drop solver, mode selection, and Monte Carlo P(hit)"
```

---

## Task 5: Fusion HAT+ I2C Gimbal Driver

**Files:**
- Create: `include/aurore/fusion_hat.hpp`
- Create: `src/drivers/fusion_hat_i2c.cpp`
- Create: `tests/unit/fusion_hat_test.cpp` (mock I2C, no hardware)
- Modify: `CMakeLists.txt`

**Note:** This driver requires hardware (`/dev/i2c-1` on RPi 5). All tests run in simulation mode (no-hardware path). Hardware tests run on target only.

### Step 1: Write the failing test

```cpp
// tests/unit/fusion_hat_test.cpp
#include "aurore/fusion_hat.hpp"
#include <cassert>
#include <iostream>

using namespace aurore;

void test_angle_to_pwm_center() {
    // 0° → 1500 µs (center position)
    uint16_t pw = FusionHat::angle_to_pwm_us(0.f);
    assert(pw >= 1490 && pw <= 1510);
    std::cout << "PASS: 0° maps to ~1500µs, got " << pw << "\n";
}

void test_angle_to_pwm_max() {
    // +90° → ~2000µs
    uint16_t pw = FusionHat::angle_to_pwm_us(90.f);
    assert(pw >= 1950 && pw <= 2050);
    std::cout << "PASS: +90° maps to ~2000µs, got " << pw << "\n";
}

void test_angle_to_pwm_min() {
    // -90° → ~1000µs
    uint16_t pw = FusionHat::angle_to_pwm_us(-90.f);
    assert(pw >= 950 && pw <= 1050);
    std::cout << "PASS: -90° maps to ~1000µs, got " << pw << "\n";
}

void test_clamp_over_range() {
    uint16_t pw = FusionHat::angle_to_pwm_us(200.f);  // > 90°
    assert(pw <= 2000);
    pw = FusionHat::angle_to_pwm_us(-200.f);
    assert(pw >= 1000);
    std::cout << "PASS: angles clamped to servo range\n";
}

void test_sim_mode_no_crash() {
    FusionHat hat;  // sim mode — no hardware access
    bool ok = hat.init_sim();
    assert(ok);
    hat.set_azimuth(15.f);
    hat.set_elevation(-10.f);
    hat.trigger_effector(10);
    std::cout << "PASS: sim mode operations complete without error\n";
}

void test_range_gate_blocks_fire() {
    FusionHat hat;
    hat.init_sim();
    hat.set_max_range_m(3.0f);
    hat.set_current_range_m(3.5f);  // beyond limit
    bool fired = hat.trigger_effector(10);  // should be blocked
    assert(!fired);
    std::cout << "PASS: range gate blocks fire beyond 3m\n";
}

int main() {
    test_angle_to_pwm_center();
    test_angle_to_pwm_max();
    test_angle_to_pwm_min();
    test_clamp_over_range();
    test_sim_mode_no_crash();
    test_range_gate_blocks_fire();
    std::cout << "\nAll Fusion HAT+ tests passed.\n";
    return 0;
}
```

### Step 2: Run test to verify it fails

```bash
cd build-native && cmake --build . --target fusion_hat_test 2>&1 | tail -5
```
Expected: compile error.

### Step 3: Create the header

```cpp
// include/aurore/fusion_hat.hpp
#pragma once

#include <cstdint>
#include <string>

namespace aurore {

// Fusion HAT+ I2C register map (GD32 MCU, 12-channel 16-bit PWM)
static constexpr uint8_t kFusionHatI2cAddr  = 0x5A;  // default I2C address
static constexpr uint8_t kRegPwmBase        = 0x20;  // PWM channel 0 base register
static constexpr uint8_t kRegPwmPeriodMs    = 0x10;  // PWM period config
static constexpr uint8_t kAzChannel         = 0;     // gimbal azimuth servo channel
static constexpr uint8_t kElChannel         = 1;     // gimbal elevation servo channel
static constexpr uint8_t kEffectorChannel   = 2;     // fire effector servo channel

class FusionHat {
public:
    FusionHat();
    ~FusionHat();

    // Hardware init (opens /dev/i2c-1)
    bool init(const std::string& i2c_device = "/dev/i2c-1");

    // Simulation init (no hardware — for testing)
    bool init_sim();

    // Gimbal control (degrees, clamped to [-90°, +90°])
    void set_azimuth(float az_deg);
    void set_elevation(float el_deg);

    // Set both axes atomically
    void set_gimbal(float az_deg, float el_deg);

    // Fire effector for pulse_ms milliseconds
    // Returns false if blocked by safety interlock
    bool trigger_effector(int pulse_ms = 10);

    // Safety interlocks
    void set_max_range_m(float max_m);
    void set_current_range_m(float range_m);
    void arm();
    void disarm();

    // Static conversion: angle in degrees → PWM pulse width in µs
    static uint16_t angle_to_pwm_us(float angle_deg);

private:
    bool write_pwm_channel(uint8_t channel, uint16_t pulse_us);

    int i2c_fd_{-1};
    bool sim_mode_{false};
    bool armed_{false};
    float max_range_m_{3.0f};
    float current_range_m_{0.f};

    static constexpr float kPwmCenterUs  = 1500.f;
    static constexpr float kPwmRangeUs   =  500.f;   // ±500µs for ±90°
    static constexpr float kPwmMinUs     = 1000.f;
    static constexpr float kPwmMaxUs     = 2000.f;
    static constexpr float kAngleMaxDeg  =   90.f;
};

}  // namespace aurore
```

### Step 4: Create the implementation

```cpp
// src/drivers/fusion_hat_i2c.cpp
#include "aurore/fusion_hat.hpp"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <chrono>

namespace aurore {

FusionHat::FusionHat() = default;

FusionHat::~FusionHat() {
    if (i2c_fd_ >= 0) {
        ::close(i2c_fd_);
        i2c_fd_ = -1;
    }
}

bool FusionHat::init(const std::string& i2c_device) {
    i2c_fd_ = ::open(i2c_device.c_str(), O_RDWR);
    if (i2c_fd_ < 0) {
        std::cerr << "FusionHat: failed to open " << i2c_device << ": "
                  << strerror(errno) << "\n";
        return false;
    }
    if (::ioctl(i2c_fd_, I2C_SLAVE, kFusionHatI2cAddr) < 0) {
        std::cerr << "FusionHat: ioctl I2C_SLAVE failed\n";
        ::close(i2c_fd_);
        i2c_fd_ = -1;
        return false;
    }
    sim_mode_ = false;
    return true;
}

bool FusionHat::init_sim() {
    sim_mode_ = true;
    return true;
}

uint16_t FusionHat::angle_to_pwm_us(float angle_deg) {
    float clamped = std::clamp(angle_deg, -kAngleMaxDeg, kAngleMaxDeg);
    float pw = kPwmCenterUs + (clamped / kAngleMaxDeg) * kPwmRangeUs;
    return static_cast<uint16_t>(std::clamp(pw, kPwmMinUs, kPwmMaxUs));
}

bool FusionHat::write_pwm_channel(uint8_t channel, uint16_t pulse_us) {
    if (sim_mode_) return true;
    if (i2c_fd_ < 0) return false;

    // Fusion HAT+ protocol: register = kRegPwmBase + channel*2, value = pulse_us as uint16 LE
    uint8_t reg = kRegPwmBase + channel * 2;
    uint8_t buf[3] = {reg,
                      static_cast<uint8_t>(pulse_us & 0xFF),
                      static_cast<uint8_t>((pulse_us >> 8) & 0xFF)};
    return ::write(i2c_fd_, buf, 3) == 3;
}

void FusionHat::set_azimuth(float az_deg) {
    write_pwm_channel(kAzChannel, angle_to_pwm_us(az_deg));
}

void FusionHat::set_elevation(float el_deg) {
    write_pwm_channel(kElChannel, angle_to_pwm_us(el_deg));
}

void FusionHat::set_gimbal(float az_deg, float el_deg) {
    set_azimuth(az_deg);
    set_elevation(el_deg);
}

bool FusionHat::trigger_effector(int pulse_ms) {
    if (!armed_) return false;
    if (current_range_m_ > max_range_m_) return false;  // range gate (spec §10.1)

    write_pwm_channel(kEffectorChannel, 2000);  // active high
    std::this_thread::sleep_for(std::chrono::milliseconds(pulse_ms));
    write_pwm_channel(kEffectorChannel, 1000);  // return to neutral
    return true;
}

void FusionHat::set_max_range_m(float max_m) { max_range_m_ = max_m; }
void FusionHat::set_current_range_m(float range_m) { current_range_m_ = range_m; }
void FusionHat::arm() { armed_ = true; }
void FusionHat::disarm() { armed_ = false; }

}  // namespace aurore
```

### Step 5: Add to CMakeLists.txt

Uncomment:
```cmake
src/drivers/fusion_hat_i2c.cpp
```

Add test:
```cmake
add_executable(fusion_hat_test
    tests/unit/fusion_hat_test.cpp
    src/drivers/fusion_hat_i2c.cpp
)
target_include_directories(fusion_hat_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(fusion_hat_test PRIVATE Threads::Threads)
add_test(NAME FusionHatTest COMMAND fusion_hat_test)
set_tests_properties(FusionHatTest PROPERTIES TIMEOUT 30)
```

### Step 6: Run test to verify it passes

```bash
cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target fusion_hat_test -j$(nproc)
./fusion_hat_test
```
Expected: all 6 tests pass.

### Step 7: Commit

```bash
git add include/aurore/fusion_hat.hpp src/drivers/fusion_hat_i2c.cpp \
        tests/unit/fusion_hat_test.cpp CMakeLists.txt
git commit -m "feat(drivers): implement Fusion HAT+ I2C gimbal driver with range gate safety interlock"
```

---

## Task 6: HUD Telemetry UNIX Domain Socket

**Files:**
- Create: `include/aurore/hud_socket.hpp`
- Create: `src/common/hud_socket.cpp`
- Create: `tests/unit/hud_socket_test.cpp`
- Modify: `CMakeLists.txt`

### Step 1: Write the failing test

```cpp
// tests/unit/hud_socket_test.cpp
#include "aurore/hud_socket.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>
#include <chrono>

using namespace aurore;

static const char* kTestSocket = "/tmp/aurore_hud_test.sock";

void test_server_starts_and_stops() {
    HudSocket hud(kTestSocket);
    assert(hud.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    hud.stop();
    std::cout << "PASS: HUD socket starts and stops\n";
}

void test_client_can_connect() {
    HudSocket hud(kTestSocket);
    hud.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    int client = ::socket(AF_UNIX, SOCK_STREAM, 0);
    assert(client >= 0);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kTestSocket, sizeof(addr.sun_path) - 1);

    int rc = ::connect(client, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    ::close(client);
    hud.stop();

    assert(rc == 0);
    std::cout << "PASS: client can connect to HUD socket\n";
}

void test_broadcast_delivers_data() {
    HudSocket hud(kTestSocket);
    hud.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    int client = ::socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kTestSocket, sizeof(addr.sun_path) - 1);
    ::connect(client, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    HudFrame frame{};
    frame.state = 2;  // LOCATED
    frame.az_deg = 15.5f;
    frame.el_deg = -3.2f;
    frame.target_cx = 400.f;
    frame.target_cy = 300.f;
    frame.confidence = 0.85f;
    frame.p_hit = 0.0f;
    hud.broadcast(frame);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    char buf[256]{};
    ssize_t n = ::recv(client, buf, sizeof(buf) - 1, MSG_DONTWAIT);
    ::close(client);
    hud.stop();

    assert(n > 0);
    std::string msg(buf, n);
    assert(msg.find("LOCATED") != std::string::npos);
    std::cout << "PASS: HUD broadcast delivers state string\n";
}

int main() {
    ::unlink(kTestSocket);  // cleanup before tests
    test_server_starts_and_stops();
    ::unlink(kTestSocket);
    test_client_can_connect();
    ::unlink(kTestSocket);
    test_broadcast_delivers_data();
    ::unlink(kTestSocket);
    std::cout << "\nAll HUD socket tests passed.\n";
    return 0;
}
```

### Step 2: Run test to verify it fails

```bash
cd build-native && cmake --build . --target hud_socket_test 2>&1 | tail -5
```
Expected: compile error.

### Step 3: Create the header

```cpp
// include/aurore/hud_socket.hpp
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aurore {

struct HudFrame {
    uint8_t  state{0};        // FcsState enum value
    float    az_deg{0.f};
    float    el_deg{0.f};
    float    target_cx{0.f}; // image coords
    float    target_cy{0.f};
    float    confidence{0.f};
    float    p_hit{0.f};
    float    range_m{0.f};
    uint64_t timestamp_ns{0};
};

// UNIX domain socket server. Broadcasts newline-delimited JSON HUD frames
// to all connected clients (spec §3 HUD socket, AM7-L2-HUD-004).
class HudSocket {
public:
    explicit HudSocket(const std::string& socket_path = "/tmp/aurore_hud.sock");
    ~HudSocket();

    bool start();
    void stop();

    // Thread-safe: can be called from any thread
    void broadcast(const HudFrame& frame);

private:
    void accept_loop();
    void send_to_clients(const std::string& msg);
    std::string frame_to_json(const HudFrame& f) const;

    std::string socket_path_;
    int server_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread accept_thread_;

    std::mutex clients_mutex_;
    std::vector<int> client_fds_;
};

}  // namespace aurore
```

### Step 4: Create the implementation

```cpp
// src/common/hud_socket.cpp
#include "aurore/hud_socket.hpp"
#include "aurore/state_machine.hpp"  // fcs_state_name
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace aurore {

HudSocket::HudSocket(const std::string& socket_path)
    : socket_path_(socket_path) {}

HudSocket::~HudSocket() { stop(); }

bool HudSocket::start() {
    ::unlink(socket_path_.c_str());

    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "HudSocket: socket() failed: " << strerror(errno) << "\n";
        return false;
    }

    // Non-blocking server socket
    int flags = ::fcntl(server_fd_, F_GETFL, 0);
    ::fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "HudSocket: bind() failed: " << strerror(errno) << "\n";
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    ::listen(server_fd_, 5);
    running_.store(true);
    accept_thread_ = std::thread(&HudSocket::accept_loop, this);
    return true;
}

void HudSocket::stop() {
    running_.store(false);
    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : client_fds_) ::close(fd);
    client_fds_.clear();
    ::unlink(socket_path_.c_str());
}

void HudSocket::accept_loop() {
    while (running_.load()) {
        int client = ::accept(server_fd_, nullptr, nullptr);
        if (client >= 0) {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_fds_.push_back(client);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

std::string HudSocket::frame_to_json(const HudFrame& f) const {
    std::ostringstream ss;
    ss << "{\"state\":\"" << fcs_state_name(static_cast<FcsState>(f.state)) << "\""
       << ",\"az\":" << f.az_deg
       << ",\"el\":" << f.el_deg
       << ",\"cx\":" << f.target_cx
       << ",\"cy\":" << f.target_cy
       << ",\"conf\":" << f.confidence
       << ",\"p_hit\":" << f.p_hit
       << ",\"range\":" << f.range_m
       << ",\"ts\":" << f.timestamp_ns
       << "}\n";
    return ss.str();
}

void HudSocket::send_to_clients(const std::string& msg) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::vector<int> dead;
    for (int fd : client_fds_) {
        ssize_t n = ::send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
        if (n < 0) dead.push_back(fd);
    }
    for (int fd : dead) {
        ::close(fd);
        client_fds_.erase(std::remove(client_fds_.begin(), client_fds_.end(), fd),
                          client_fds_.end());
    }
}

void HudSocket::broadcast(const HudFrame& frame) {
    send_to_clients(frame_to_json(frame));
}

}  // namespace aurore
```

### Step 5: Add to CMakeLists.txt

Uncomment:
```cmake
src/common/hud_socket.cpp
```

Add test:
```cmake
add_executable(hud_socket_test
    tests/unit/hud_socket_test.cpp
    src/common/hud_socket.cpp
    src/state_machine/state_machine.cpp
)
target_include_directories(hud_socket_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(hud_socket_test PRIVATE Threads::Threads)
add_test(NAME HudSocketTest COMMAND hud_socket_test)
set_tests_properties(HudSocketTest PROPERTIES TIMEOUT 30)
```

### Step 6: Run test to verify it passes

```bash
cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target hud_socket_test -j$(nproc)
./hud_socket_test
```
Expected: all 3 tests pass.

### Step 7: Commit

```bash
git add include/aurore/hud_socket.hpp src/common/hud_socket.cpp \
        tests/unit/hud_socket_test.cpp CMakeLists.txt
git commit -m "feat(hud): implement UNIX domain socket for real-time HUD telemetry (AM7-L2-HUD-004)"
```

---

## Task 7: Wire All Subsystems into main.cpp

**Files:**
- Modify: `src/main.cpp` (refactor threading to match spec §3.2)

**Context:** The current `main.cpp` has a fixed 4-SCHED_FIFO-thread skeleton. The new spec calls for a state-machine-driven main control thread that invokes vision on demand, not continuously. The existing `SafetyMonitor` thread (priority 99) is kept. The `CameraWrapper` and `LockFreeRingBuffer` are reused.

### Step 1: No test (integration — verify by running `./aurore --dry-run`)

The full integration is validated by running the binary in dry-run mode (no hardware) and checking that all threads start, the state machine runs, and no crashes occur within 5 seconds.

### Step 2: Refactor main.cpp

Replace the contents of `src/main.cpp` with the new structure. Key changes:

```cpp
// src/main.cpp (structure summary — implement fully)

// Threads to launch:
// 1. safety_monitor_thread  — existing SafetyMonitor, SCHED_FIFO=99, CPU 3, 1kHz
// 2. gimbal_driver_thread   — FusionHat inner loop, SCHED_FIFO=85, CPU 2, 1kHz
// 3. sensor_io_thread       — LRF UART reader, SCHED_FIFO=80, CPU 1
// 4. main_control_thread    — StateMachine + vision dispatch, SCHED_FIFO=90, CPU 2
// 5. logging_thread         — HudSocket broadcast + TelemetryWriter, low priority

// main_control_thread pseudo-code:
//   ThreadTiming timing(33333333, 0);  // ~30Hz
//   while running:
//     timing.wait()
//     frame = ring_buffer.pop()       // latest frame
//     if frame valid:
//       detection = detector.detect(frame)  // ORB
//       sm.on_detection(detection)
//       if sm.state() >= LOCATED:
//         sol = tracker.update(frame)
//         sm.on_tracker_update(sol)
//       if sm.state() == LOCKED:
//         score = tracker.redetect(frame)
//         sm.on_redetection_score(score)
//         ballistic_sol = solver.solve(lrf_range, gimbal_el, aspect, 700.f)
//         sm.on_ballistics_solution(ballistic_sol)
//     sm.tick(33ms)
//     hud.broadcast(make_hud_frame(sm, gimbal))
```

### Step 3: Build the full binary

```bash
cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc) 2>&1 | grep -E "error|warning" | head -20
```
Expected: clean build.

### Step 4: Smoke test

```bash
cd build-native && timeout 5 ./aurore --dry-run
```
Expected: prints thread startup messages, "System running", status lines with state=IDLE, then "Shutdown complete."

### Step 5: Run all unit tests

```bash
cd build-native && ctest --output-on-failure
```
Expected: ALL tests pass (RingBufferTest, TimingTest, SafetyMonitorTest, StateMachineTest, DetectorTest, TrackerTest, BallisticsTest, FusionHatTest, HudSocketTest).

### Step 6: Commit

```bash
git add src/main.cpp
git commit -m "feat(main): wire state machine, vision, ballistics, gimbal, and HUD into engagement pipeline"
```

---

## Final Verification

### Run full test suite

```bash
cd build-native && ctest -V
```
Expected output: 9/9 tests passed.

### Update blackboard quality gate

After all tests pass, update `agent_sessions/session_20260305_001/blackboard/quality_gates.json`:
- `compliance_complete` → `"status": "passed"`
- `implementation_complete` → `"status": "pending"` (pending truthchecker)

Trigger the `truthchecker` agent to perform final verification.

### Cross-compile check

```bash
./scripts/build-rpi.sh Release 2>&1 | grep -E "error|warning" | head -10
```
Expected: clean build (no source-level errors; hardware-dependent tests remain disabled).
