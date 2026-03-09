#include <opencv2/imgproc.hpp>

#include "aurore/tracker.hpp"

namespace aurore {

KcfTracker::KcfTracker() = default;

void KcfTracker::set_camera(CameraWrapper* camera) { camera_ = camera; }

bool KcfTracker::init(const cv::Mat& bgr_frame, const cv::Rect2d& bbox) {
    // AM7-L2-VIS-008: KCF tracker for 120Hz continuous tracking
    // KCF is 10× faster than CSRT (1-2ms vs 10-20ms)
    // Using default KCF parameters for stability
    auto params = cv::TrackerKCF::Params();
    // Default parameters (optimized for speed):
    // - sigma: 0.2 (Gaussian kernel bandwidth)
    // - lambda: 0.0001 (regularization)
    // - interp_factor: 0.075 (linear interpolation for adaptation)
    // - output_sigma_factor: 0.1 (spatial bandwidth)
    // - resize: false (disabled for WCET compliance)

    tracker_ = cv::TrackerKCF::create(params);
    cv::Rect bbox_int(static_cast<int>(bbox.x), static_cast<int>(bbox.y),
                      static_cast<int>(bbox.width), static_cast<int>(bbox.height));
    tracker_->init(bgr_frame, bbox_int);
    valid_ = true;
    last_bbox_ = bbox;
    have_prev_ = false;
    return valid_;
}

TrackSolution KcfTracker::update(const cv::Mat& bgr_frame) {
    TrackSolution sol;
    if (!valid_ || !tracker_) return sol;

    // AM7-L2-VIS-003: Vision pipeline latency ≤3ms
    // KCF update: ~1-2ms typical
    cv::Rect bbox;
    bool ok = tracker_->update(bgr_frame, bbox);

    if (!ok) {
        valid_ = false;
        return sol;
    }

    // Area change sanity check (drift detection)
    double prev_area = last_bbox_.width * last_bbox_.height;
    double curr_area = static_cast<double>(bbox.width) * bbox.height;
    if (prev_area > 0 &&
        std::abs(curr_area - prev_area) / prev_area > static_cast<double>(kAreaChangeMaxRatio)) {
        valid_ = false;
        return sol;
    }

    cv::Point2f centroid(static_cast<float>(bbox.x) + static_cast<float>(bbox.width) * 0.5f,
                         static_cast<float>(bbox.y) + static_cast<float>(bbox.height) * 0.5f);

    sol.centroid_x = centroid.x;
    sol.centroid_y = centroid.y;

    // Velocity estimation for predictive gimbal compensation
    if (have_prev_) {
        sol.velocity_x = centroid.x - prev_centroid_.x;
        sol.velocity_y = centroid.y - prev_centroid_.y;
    }
    prev_centroid_ = centroid;
    have_prev_ = true;
    last_bbox_ = cv::Rect2d(bbox.x, bbox.y, bbox.width, bbox.height);
    sol.valid = true;
    // INT-006: KCF does not provide PSR (Peak-to-Sidelobe Ratio).
    // Set to -1.0f to indicate "not available". Do not use for track quality assessment.
    // For redetection, use capture_reference_template() + redetect() which computes
    // correlation peak ratio via matchTemplate.
    sol.psr = -1.0f;
    return sol;
}

void KcfTracker::reset() {
    tracker_.reset();
    valid_ = false;
    have_prev_ = false;

    // Zero-copy: release the stored DMA buffer back to camera pool
    if (ref_frame_.is_valid() && camera_) {
        camera_->release_frame(ref_frame_);
    }
    ref_frame_ = ZeroCopyFrame{};
    ref_roi_ = cv::Rect2d{};
}

bool KcfTracker::is_valid() const { return valid_; }

void KcfTracker::capture_reference_template(const ZeroCopyFrame& frame, const cv::Rect2d& bbox) {
    // AM7-L3-VIS-001: Zero-copy reference template capture
    // Store the ZeroCopyFrame descriptor (DMA buffer reference), NOT a pixel copy.
    // The frame is held until reset() or next capture_reference_template() call.

    if (!frame.is_valid()) {
        return;
    }

    // Release previous reference frame if still held
    if (ref_frame_.is_valid() && camera_) {
        camera_->release_frame(ref_frame_);
    }

    // Store the frame descriptor (zero-copy - no memcpy performed)
    ref_frame_ = frame;
    ref_roi_ = bbox;
}

float KcfTracker::redetect(const cv::Mat& bgr_frame) const {
    // AM7-L3-VIS-001: Zero-copy redetection
    // Wrap the stored ZeroCopyFrame as cv::Mat (no memcpy) and extract ROI

    if (!ref_frame_.is_valid() || !camera_) {
        return 0.f;
    }

    // Wrap stored DMA buffer as cv::Mat (zero-copy operation)
    cv::Mat ref_bgr = camera_->wrap_as_mat(ref_frame_, PixelFormat::BGR888);
    if (ref_bgr.empty()) {
        return 0.f;
    }

    // Extract ROI from reference frame
    cv::Rect ref_roi(static_cast<int>(ref_roi_.x), static_cast<int>(ref_roi_.y),
                     static_cast<int>(ref_roi_.width), static_cast<int>(ref_roi_.height));
    ref_roi &= cv::Rect(0, 0, ref_bgr.cols, ref_bgr.rows);
    if (ref_roi.empty()) {
        return 0.f;
    }

    // Convert reference ROI to grayscale for template matching
    cv::Mat ref_gray;
    cv::cvtColor(ref_bgr(ref_roi), ref_gray, cv::COLOR_BGR2GRAY);

    // Convert current frame to grayscale
    cv::Mat gray;
    cv::cvtColor(bgr_frame, gray, cv::COLOR_BGR2GRAY);

    // Search region with margin around last known position
    int margin = 100;
    cv::Rect search_roi(static_cast<int>(std::max(0.0, last_bbox_.x - margin)),
                        static_cast<int>(std::max(0.0, last_bbox_.y - margin)),
                        static_cast<int>(last_bbox_.width + 2 * margin),
                        static_cast<int>(last_bbox_.height + 2 * margin));
    search_roi &= cv::Rect(0, 0, gray.cols, gray.rows);
    if (search_roi.empty()) {
        return 0.f;
    }

    cv::Mat region = gray(search_roi);
    if (region.cols < ref_gray.cols || region.rows < ref_gray.rows) {
        return 0.f;
    }

    // Template matching with normalized cross-correlation
    cv::Mat result;
    cv::matchTemplate(region, ref_gray, result, cv::TM_CCOEFF_NORMED);

    double min_val, max_val;
    cv::minMaxLoc(result, &min_val, &max_val);
    return static_cast<float>(max_val);
}

}  // namespace aurore
