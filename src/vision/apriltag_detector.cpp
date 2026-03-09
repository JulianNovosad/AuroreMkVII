#include "aurore/apriltag_detector.hpp"

#include <opencv2/imgproc.hpp>

namespace aurore {

// WCET-001: Optimized ArUco detector parameters for real-time performance
// - cornerRefinementMethod: SUBPIX for accuracy without significant overhead
// - errorCorrectionRate: 0.6 for robustness against partial occlusion
// - minMarkerPerimeterRate: 0.03 for 1536×864 resolution (filters very small noise)
// - polygonalApproxAccuracyRate: 0.05 for fast contour approximation
// - minCornerDistanceRate: 0.05 to prevent merged detections
// - minDistanceToBorder: 3 to avoid edge artifacts
AprilTagDetector::AprilTagDetector()
    : params_(cv::aruco::DetectorParameters::create()),
      dictionary_(cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250)) {
    // WCET-001: Tune parameters for speed and robustness
    params_->cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
    params_->errorCorrectionRate = 0.6f;      // Allow 40% bit errors (robust to partial occlusion)
    params_->minMarkerPerimeterRate = 0.03f;  // Filter very small noise
    params_->maxMarkerPerimeterRate = 0.9f;   // Allow large markers
    params_->polygonalApproxAccuracyRate = 0.05f;  // Fast contour approximation
    params_->minCornerDistanceRate = 0.05f;
    params_->minDistanceToBorder = 3;
    params_->adaptiveThreshWinSizeMin = 3;
    params_->adaptiveThreshWinSizeMax = 23;
    params_->adaptiveThreshWinSizeStep = 10;
    params_->adaptiveThreshConstant = 7;
    params_->maxErroneousBitsInBorderRate = kMaxErrorRate;
    params_->minOtsuStdDev = 5.0f;
    params_->perspectiveRemovePixelPerCell = 4;
    params_->perspectiveRemoveIgnoredMarginPerCell = 0.13f;
}

void AprilTagDetector::set_dictionary(cv::aruco::PREDEFINED_DICTIONARY_NAME dict_name) {
    dictionary_ = cv::aruco::getPredefinedDictionary(dict_name);
}

void AprilTagDetector::set_known_tag_ids(const std::vector<int>& tag_ids) {
    known_tag_ids_ = tag_ids;
}

bool AprilTagDetector::is_ready() const { return dictionary_ != nullptr; }

std::optional<Detection> AprilTagDetector::detect(const cv::Mat& bgr_frame) const {
    auto detections = detect_all(bgr_frame);
    if (detections.empty()) {
        return std::nullopt;
    }
    // Return highest confidence detection
    return detections[0];
}

std::vector<Detection> AprilTagDetector::detect_all(const cv::Mat& bgr_frame) const {
    if (!is_ready()) {
        return {};
    }

    // Convert to grayscale (required for ArUco detection)
    cv::Mat gray;
    if (bgr_frame.channels() == 1) {
        gray = bgr_frame;  // Already grayscale
    } else {
        cv::cvtColor(bgr_frame, gray, cv::COLOR_BGR2GRAY);
    }

    // Detect markers (OpenCV 4.6.0 API uses free function)
    std::vector<int> marker_ids;
    std::vector<std::vector<cv::Point2f>> marker_corners;
    std::vector<std::vector<cv::Point2f>> rejected_candidates;

    cv::aruco::detectMarkers(gray, dictionary_, marker_corners, marker_ids, params_,
                             rejected_candidates);

    // Clear last detected IDs
    last_detected_ids_.clear();

    if (marker_ids.empty()) {
        return {};
    }

    std::vector<Detection> results;
    results.reserve(marker_ids.size());

    for (std::size_t i = 0; i < marker_ids.size(); ++i) {
        const int tag_id = marker_ids[i];
        const auto& corners = marker_corners[i];

        // Filter by known tag IDs if configured
        if (!known_tag_ids_.empty()) {
            bool found = false;
            for (int known_id : known_tag_ids_) {
                if (known_id == tag_id) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                continue;  // Skip unknown tags
            }
        }

        // Compute bounding box from corners
        float min_x = corners[0].x, max_x = corners[0].x;
        float min_y = corners[0].y, max_y = corners[0].y;
        for (const auto& pt : corners) {
            if (pt.x < min_x) min_x = pt.x;
            if (pt.x > max_x) max_x = pt.x;
            if (pt.y < min_y) min_y = pt.y;
            if (pt.y > max_y) max_y = pt.y;
        }

        const int width = static_cast<int>(max_x - min_x);
        const int height = static_cast<int>(max_y - min_y);

        // Compute confidence based on marker size relative to frame
        // ArUco provides inherent reliability via error correction
        // Larger, well-defined markers get higher confidence
        const float marker_area = static_cast<float>(width * height);
        const float frame_area = static_cast<float>(bgr_frame.cols * bgr_frame.rows);
        const float area_ratio = marker_area / frame_area;
        // Confidence: 0.85 base + up to 0.15 for large markers (ratio > 0.01 = 1% of frame)
        const float confidence = std::min(1.0f, 0.85f + area_ratio * 15.0f);

        if (confidence < kConfidenceThreshold) {
            continue;  // Skip low-confidence detections
        }

        Detection det;
        det.confidence = confidence;
        det.bbox.x = static_cast<int>(min_x);
        det.bbox.y = static_cast<int>(min_y);
        det.bbox.w = width;
        det.bbox.h = height;

        results.push_back(det);
        last_detected_ids_.push_back(tag_id);
    }

    // Sort by confidence (highest first)
    std::sort(results.begin(), results.end(),
              [](const Detection& a, const Detection& b) { return a.confidence > b.confidence; });

    return results;
}

std::vector<int> AprilTagDetector::get_last_detected_ids() const { return last_detected_ids_; }

}  // namespace aurore
