#include "aurore/tracker.hpp"
#include <opencv2/imgproc.hpp>

namespace aurore {

KcfTracker::KcfTracker() = default;

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
    if (prev_area > 0 && std::abs(curr_area - prev_area) / prev_area > kAreaChangeMaxRatio) {
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
    ref_template_ = cv::Mat{};
}

bool KcfTracker::is_valid() const { return valid_; }

void KcfTracker::capture_reference_template(const cv::Mat& bgr_frame, const cv::Rect2d& bbox) {
    cv::Rect roi(static_cast<int>(bbox.x), static_cast<int>(bbox.y),
                 static_cast<int>(bbox.width), static_cast<int>(bbox.height));
    roi &= cv::Rect(0, 0, bgr_frame.cols, bgr_frame.rows);
    if (roi.empty()) return;
    // Note: clone() performs memcpy - for zero-copy, use DMA buffer wrapping
    cv::Mat crop = bgr_frame(roi).clone();
    cv::cvtColor(crop, ref_template_, cv::COLOR_BGR2GRAY);
}

float KcfTracker::redetect(const cv::Mat& bgr_frame) const {
    if (ref_template_.empty()) return 0.f;

    cv::Mat gray;
    cv::cvtColor(bgr_frame, gray, cv::COLOR_BGR2GRAY);

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
    if (region.cols < ref_template_.cols || region.rows < ref_template_.rows) return 0.f;

    cv::Mat result;
    cv::matchTemplate(region, ref_template_, result, cv::TM_CCOEFF_NORMED);

    double min_val, max_val;
    cv::minMaxLoc(result, &min_val, &max_val);
    return static_cast<float>(max_val);
}

}  // namespace aurore
