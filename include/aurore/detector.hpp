#pragma once

#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <vector>

#include "aurore/state_machine.hpp"

namespace aurore {

// PERF-001/002/003: Optimized ORB detector with cached CLAHE and reduced complexity
class OrbDetector {
   public:
    OrbDetector();

    void add_template(const cv::Mat& bgr_image);
    bool load_descriptor_file(const std::string& path);
    bool is_ready() const;
    std::optional<Detection> detect(const cv::Mat& bgr_frame) const;

   private:
    struct Template {
        cv::Mat descriptors;
        std::vector<cv::KeyPoint> keypoints;
    };

    cv::Ptr<cv::ORB> orb_;
    cv::Ptr<cv::BFMatcher> matcher_;
    mutable cv::Ptr<cv::CLAHE> clahe_;  // PERF-002: Cache CLAHE object (don't recreate each frame)
    std::vector<Template> templates_;

    // PERF-003: Optimized parameters for real-time performance
    static constexpr float kRatioTestThreshold = 0.75f;
    static constexpr int kRansacMinInliers = 10;
    static constexpr float kConfidenceThreshold = 0.95f;  // AM7-L2-TGT-003
    static constexpr int kRansacMaxIterations =
        50;  // PERF-003: Limit RANSAC iterations (from 2000)
};

}  // namespace aurore
