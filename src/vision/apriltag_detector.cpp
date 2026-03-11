#include "aurore/apriltag_detector.hpp"

#include <opencv2/imgproc.hpp>

namespace aurore {

AprilTagDetector::AprilTagDetector()
    : params_(),
      dictionary_(cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250)),
      detector_(dictionary_, params_) {
    params_.cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
    params_.errorCorrectionRate = 0.6f;
    params_.minMarkerPerimeterRate = 0.03f;
    params_.maxMarkerPerimeterRate = 0.9f;
    params_.polygonalApproxAccuracyRate = 0.05f;
    params_.minCornerDistanceRate = 0.05f;
    params_.minDistanceToBorder = 3;
    params_.adaptiveThreshWinSizeMin = 3;
    params_.adaptiveThreshWinSizeMax = 23;
    params_.adaptiveThreshWinSizeStep = 10;
    params_.adaptiveThreshConstant = 7;
    params_.maxErroneousBitsInBorderRate = kMaxErrorRate;
    params_.minOtsuStdDev = 5.0f;
    params_.perspectiveRemovePixelPerCell = 4;
    params_.perspectiveRemoveIgnoredMarginPerCell = 0.13f;
}

void AprilTagDetector::set_dictionary(int dict_id) {
    dictionary_ = cv::aruco::getPredefinedDictionary(dict_id);
    detector_ = cv::aruco::ArucoDetector(dictionary_, params_);
}

void AprilTagDetector::set_known_tag_ids(const std::vector<int>& tag_ids) {
    known_tag_ids_ = tag_ids;
}

bool AprilTagDetector::is_ready() const { return dictionary_.markerSize > 0; }

std::optional<Detection> AprilTagDetector::detect(const cv::Mat& bgr_frame) const {
    auto detections = detect_all(bgr_frame);
    if (detections.empty()) {
        return std::nullopt;
    }
    return detections[0];
}

std::vector<Detection> AprilTagDetector::detect_all(const cv::Mat& bgr_frame) const {
    std::vector<Detection> results;

    if (bgr_frame.empty()) {
        return results;
    }

    cv::Mat gray;
    if (bgr_frame.channels() == 3) {
        cv::cvtColor(bgr_frame, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = bgr_frame;
    }

    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners, rejected;
    detector_.detectMarkers(gray, corners, ids, rejected);

    last_detected_ids_ = ids;

    for (size_t i = 0; i < corners.size(); ++i) {
        if (!known_tag_ids_.empty()) {
            bool found = false;
            for (int kid : known_tag_ids_) {
                if (ids[i] == kid) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                continue;
            }
        }

        cv::Rect bbox = cv::boundingRect(corners[i]);
        float perimeter = 2.0f * (static_cast<float>(bbox.width) + static_cast<float>(bbox.height));

        if (perimeter < kMinMarkerPerimeter) {
            continue;
        }

        float confidence = 1.0f - (kMaxErrorRate * 0.5f);
        if (confidence < kConfidenceThreshold) {
            continue;
        }

        Detection det;
        det.id = ids[i];
        det.confidence = confidence;
        det.bbox = {bbox.x, bbox.y, bbox.width, bbox.height};

        results.push_back(det);
    }

    return results;
}

std::vector<int> AprilTagDetector::get_last_detected_ids() const {
    return last_detected_ids_;
}

}  // namespace aurore
