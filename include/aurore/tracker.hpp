#pragma once

#include <optional>
#include <opencv2/tracking.hpp>

#include "aurore/state_machine.hpp"

namespace aurore {

/**
 * @brief KCF Tracker - AM7-L2-VIS-003, AM7-L2-VIS-008
 * 
 * KCF (Kernelized Correlation Filter) tracker provides:
 * - 1-2ms execution time vs 10-20ms for CSRT (AM7-L2-TIM-002 WCET ≤5ms)
 * - 120Hz tracking capability
 * - Good accuracy for rigid target tracking
 * 
 * Note: KCF does not support scale change detection.
 * For scale-invariant tracking, consider SAMF or CSRT (with WCET penalty).
 */
class KcfTracker {
public:
    KcfTracker();

    bool init(const cv::Mat& bgr_frame, const cv::Rect2d& bbox);
    TrackSolution update(const cv::Mat& bgr_frame);
    void reset();
    bool is_valid() const;
    cv::Rect2d last_bbox() const { return last_bbox_; }

    void capture_reference_template(const cv::Mat& bgr_frame, const cv::Rect2d& bbox);
    float redetect(const cv::Mat& bgr_frame) const;

private:
    cv::Ptr<cv::TrackerKCF> tracker_;
    bool valid_{false};
    cv::Rect2d last_bbox_{};

    cv::Point2f prev_centroid_{};
    bool have_prev_{false};

    cv::Mat ref_template_;

    // INT-006: KCF does not provide PSR. This constant is unused.
    // PSR field in TrackSolution is set to -1.0f to indicate "not available".
    // Use correlation peak ratio from matchTemplate if redetection is needed.
    static constexpr float kPsrFailThreshold   = 0.0f;  // Unused for KCF
    static constexpr float kAreaChangeMaxRatio = 0.50f;
};

}  // namespace aurore
