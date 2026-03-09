#pragma once

#include <opencv2/aruco.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <vector>

#include "aurore/state_machine.hpp"

namespace aurore {

// WCET-001: AprilTag/ArUco detector for sub-millisecond detection
// - Execution time: 0.1-0.5ms at 1536×864 (vs 3-10ms for ORB+RANSAC)
// - Deterministic execution time (no RANSAC iterations)
// - More robust for known targets with fiducial markers
class AprilTagDetector {
   public:
    AprilTagDetector();

    // Configure detector for specific ArUco dictionary
    void set_dictionary(cv::aruco::PREDEFINED_DICTIONARY_NAME dict_name);

    // Load known tag IDs to detect (empty = detect all)
    void set_known_tag_ids(const std::vector<int>& tag_ids);

    bool is_ready() const;

    // Detect all markers in frame
    // Returns std::nullopt if no markers found or below confidence threshold
    std::optional<Detection> detect(const cv::Mat& bgr_frame) const;

    // Detect multiple markers, returns all detections above threshold
    std::vector<Detection> detect_all(const cv::Mat& bgr_frame) const;

    // Get last detected tag IDs (for telemetry)
    std::vector<int> get_last_detected_ids() const;

   private:
    cv::Ptr<cv::aruco::DetectorParameters> params_;
    cv::Ptr<cv::aruco::Dictionary> dictionary_;
    std::vector<int> known_tag_ids_;  // Empty = detect all tags
    mutable std::vector<int> last_detected_ids_;  // For telemetry

    // WCET-001: Detection parameters tuned for speed and robustness
    static constexpr float kConfidenceThreshold = 0.85f;  // Slightly lower than ORB (0.95) for ArUco reliability
    static constexpr int kMinMarkerPerimeter = 50;        // Filter noise
    static constexpr float kMaxErrorRate = 0.1f;          // Max allowed bit error rate
};

}  // namespace aurore
