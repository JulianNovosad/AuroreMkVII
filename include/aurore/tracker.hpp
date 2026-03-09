#pragma once

#include <optional>
#include <opencv2/tracking.hpp>

#include "aurore/state_machine.hpp"
#include "aurore/camera_wrapper.hpp"

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
 *
 * Zero-copy design: Reference template is stored as ZeroCopyFrame descriptor
 * (DMA buffer reference), not as pixel copy. This complies with AM7-L3-VIS-001.
 */
class KcfTracker {
public:
    KcfTracker();

    /**
     * @brief Set camera wrapper for zero-copy frame management
     * @param camera Pointer to camera wrapper (must outlive tracker)
     */
    void set_camera(CameraWrapper* camera);

    bool init(const cv::Mat& bgr_frame, const cv::Rect2d& bbox);
    TrackSolution update(const cv::Mat& bgr_frame);
    void reset();
    bool is_valid() const;
    cv::Rect2d last_bbox() const { return last_bbox_; }

    /**
     * @brief Capture reference template for redetection (zero-copy)
     *
     * Stores the ZeroCopyFrame descriptor (DMA buffer reference) and ROI.
     * No memcpy is performed - the frame is NOT copied.
     *
     * @param frame Zero-copy frame from camera (will be held until reset/release)
     * @param bbox ROI within the frame to use as template
     */
    void capture_reference_template(const ZeroCopyFrame& frame, const cv::Rect2d& bbox);
    
    /**
     * @brief Attempt redetection using stored reference template
     *
     * Wraps the stored ZeroCopyFrame as cv::Mat (zero-copy) and performs
     * template matching.
     *
     * @param bgr_frame Current frame to search in
     * @return float Normalized correlation score (1.0 = perfect match)
     */
    float redetect(const cv::Mat& bgr_frame) const;

private:
    cv::Ptr<cv::TrackerKCF> tracker_;
    bool valid_{false};
    cv::Rect2d last_bbox_{};

    cv::Point2f prev_centroid_{};
    bool have_prev_{false};

    // Zero-copy reference template storage
    ZeroCopyFrame ref_frame_;      ///< DMA buffer descriptor (no pixel copy)
    cv::Rect2d ref_roi_{};         ///< ROI within the reference frame
    CameraWrapper* camera_{nullptr};  ///< Camera for wrap_as_mat() during redetection

    // INT-006: KCF does not provide PSR. This constant is unused.
    // PSR field in TrackSolution is set to -1.0f to indicate "not available".
    // Use correlation peak ratio from matchTemplate if redetection is needed.
    static constexpr float kPsrFailThreshold   = 0.0f;  // Unused for KCF
    static constexpr float kAreaChangeMaxRatio = 0.50f;
};

}  // namespace aurore
