#include "aurore/detector.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace aurore {

// PERF-001: Optimized ORB parameters for real-time performance
// - nfeatures: 500 (reduced from default 500, but with better distribution)
// - scaleFactor: 1.2f (standard)
// - nlevels: 4 (reduced from 8 for faster pyramid computation)
// - edgeThreshold: 31 (standard)
// - firstLevel: 0 (standard)
// - WTA_K: 2 (HAMMING distance)
// - scoreType: HARRIS_SCORE (better quality than FAST_SCORE)
// - patchSize: 31 (standard)
// - fastThreshold: 20 (standard)
OrbDetector::OrbDetector()
    : orb_(cv::ORB::create(500, 1.2f, 4, 31, 0, 2, cv::ORB::HARRIS_SCORE, 31, 20))
    , matcher_(cv::BFMatcher::create(cv::NORM_HAMMING, false))
    , clahe_(cv::createCLAHE(2.0, {8, 8}))  // PERF-002: Cache CLAHE object at construction
{}

void OrbDetector::add_template(const cv::Mat& bgr) {
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    Template t;
    orb_->detectAndCompute(gray, cv::noArray(), t.keypoints, t.descriptors);
    templates_.push_back(std::move(t));  // detect() skips entries with empty descriptors
}

bool OrbDetector::load_descriptor_file(const std::string& path) {
    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) return false;

    Template t;
    fs["descriptors"] >> t.descriptors;
    fs["keypoints"] >> t.keypoints;
    fs.release();

    if (t.descriptors.empty()) return false;

    templates_.push_back(std::move(t));
    return true;
}

bool OrbDetector::is_ready() const { return !templates_.empty(); }

std::optional<Detection> OrbDetector::detect(const cv::Mat& bgr_frame) const {
    if (templates_.empty()) return std::nullopt;

    // AM7-L2-TIM-002: Use 640x480 center ROI to stay within 5ms WCET
    // Input frame is 1536x864. ROI: (1536-640)/2 = 448, (864-480)/2 = 192
    const int roi_x = 448;
    const int roi_y = 192;
    const int roi_w = 640;
    const int roi_h = 480;
    
    cv::Rect roi(roi_x, roi_y, roi_w, roi_h);
    cv::Mat cropped = bgr_frame(roi);

    cv::Mat gray;
    cv::cvtColor(cropped, gray, cv::COLOR_BGR2GRAY);

    // PERF-002: Use cached CLAHE object instead of creating new one each frame
    clahe_->apply(gray, gray);

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

        std::vector<cv::DMatch> good;
        for (auto& m : knn_matches)
            if (m.size() >= 2 && m[0].distance < kRatioTestThreshold * m[1].distance)
                good.push_back(m[0]);

        if (static_cast<int>(good.size()) < kRansacMinInliers) continue;

        std::vector<cv::Point2f> src_pts, dst_pts;
        for (auto& m : good) {
            src_pts.push_back(tmpl.keypoints[static_cast<std::vector<cv::KeyPoint>::size_type>(m.queryIdx)].pt);
            dst_pts.push_back(frame_kps[static_cast<std::vector<cv::KeyPoint>::size_type>(m.trainIdx)].pt);
        }
        cv::Mat mask;
        // PERF-003: Limit RANSAC iterations to 50 (from default 2000) for faster homography
        cv::findHomography(src_pts, dst_pts, cv::RANSAC, 3.0, mask, kRansacMaxIterations);

        if (mask.empty()) continue;

        int inliers = cv::countNonZero(mask);
        float conf = static_cast<float>(inliers) / static_cast<float>(good.size());

        if (conf > best_confidence) {
            best_confidence = conf;
            float cx = 0, cy = 0;
            int n = 0;
            for (int i = 0; i < mask.rows; ++i) {
                if (mask.at<uint8_t>(i)) {
                    cx += dst_pts[static_cast<std::vector<cv::Point_<float> >::size_type>(i)].x;
                    cy += dst_pts[static_cast<std::vector<cv::Point_<float> >::size_type>(i)].y;
                    ++n;
                }
            }
            if (n > 0) {
                cx /= static_cast<float>(n); cy /= static_cast<float>(n);
                best.confidence = conf;
                // Add ROI offsets back to detection coordinates
                best.bbox = {static_cast<int>(cx + roi_x - 25), static_cast<int>(cy + roi_y - 25), 50, 50};
            }
        }
    }

    if (best_confidence < kConfidenceThreshold) return std::nullopt;
    return best;
}

}  // namespace aurore
