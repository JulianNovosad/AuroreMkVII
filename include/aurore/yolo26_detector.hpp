#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "aurore/state_machine.hpp"  // for Detection

namespace aurore {

/**
 * @brief YOLO26n detector using ONNX Runtime.
 *
 * Accepts a BGR888 OpenCV Mat of any resolution, letterboxes it to 640×640,
 * runs YOLO26n (NMS-free end-to-end), and returns the highest-confidence
 * Detection among the target classes {0=person, 2=car, 4=airplane}.
 *
 * Inference runs in the calling thread. Designed to be called from a
 * dedicated non-RT detect thread, NOT from a SCHED_FIFO RT thread.
 */
class Yolo26Detector {
public:
    struct Config {
        std::string model_path;               ///< Path to yolo26n.onnx
        float conf_threshold  = 0.40f;        ///< Minimum detection confidence
        int   input_size      = 640;          ///< Model input resolution (square)
        int   num_threads     = 3;            ///< ORT intra-op thread count
        std::vector<int> target_classes = {0, 2, 4};  ///< person, car, airplane
    };

    explicit Yolo26Detector(const Config& cfg);
    ~Yolo26Detector();

    /// Load the ONNX model. Returns false if model file not found.
    bool load();
    bool is_loaded() const { return loaded_; }

    /// Run detection on a BGR888 frame. Returns highest-confidence target detection
    /// among target_classes, or std::nullopt if none found above threshold.
    std::optional<Detection> detect(const cv::Mat& bgr_frame);

    /// Return all detections above threshold (any target class).
    std::vector<Detection> detect_all(const cv::Mat& bgr_frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Config cfg_;
    bool loaded_{false};

    // Letterbox bgr_frame to (input_size × input_size), return scale and padding
    struct LetterboxResult {
        cv::Mat image;    // letterboxed float32 [0,1] CHW
        float scale;      // scale factor applied to original image
        float pad_top;    // top padding in pixels (after scaling)
        float pad_left;   // left padding in pixels (after scaling)
    };
    LetterboxResult letterbox(const cv::Mat& bgr_frame) const;

    // Unscale bbox from letterboxed 640×640 space back to original frame space
    Detection unscale(int x1, int y1, int x2, int y2, float conf, int cls,
                      const LetterboxResult& lb, int orig_w, int orig_h) const;
};

}  // namespace aurore
