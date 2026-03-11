#pragma once

#include <opencv2/aruco.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <vector>

#include "aurore/state_machine.hpp"

namespace aurore {

class AprilTagDetector {
   public:
    AprilTagDetector();

    void set_dictionary(int dict_id);

    void set_known_tag_ids(const std::vector<int>& tag_ids);

    bool is_ready() const;

    std::optional<Detection> detect(const cv::Mat& bgr_frame) const;

    std::vector<Detection> detect_all(const cv::Mat& bgr_frame) const;

    std::vector<int> get_last_detected_ids() const;

   private:
    cv::aruco::DetectorParameters params_;
    cv::aruco::Dictionary dictionary_;
    cv::aruco::ArucoDetector detector_;
    std::vector<int> known_tag_ids_;
    mutable std::vector<int> last_detected_ids_;

    static constexpr float kConfidenceThreshold = 0.85f;
    static constexpr int kMinMarkerPerimeter = 50;
    static constexpr float kMaxErrorRate = 0.1f;
};

}  // namespace aurore
