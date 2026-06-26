#include "aurore/yolo26_detector.hpp"

#include <algorithm>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

#ifdef AURORE_HAS_ONNXRUNTIME
#include <onnxruntime/onnxruntime_cxx_api.h>
#endif

namespace aurore {

// ---------------------------------------------------------------------------
// Pimpl — isolates ORT symbols so non-ORT builds still compile
// ---------------------------------------------------------------------------
struct Yolo26Detector::Impl {
#ifdef AURORE_HAS_ONNXRUNTIME
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "yolo26"};
    Ort::SessionOptions opts;
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    // Input/output names (owned strings)
    std::vector<std::string> input_names_str;
    std::vector<std::string> output_names_str;
    std::vector<const char*> input_names;
    std::vector<const char*> output_names;
#endif
};

// ---------------------------------------------------------------------------
Yolo26Detector::Yolo26Detector(const Config& cfg) : impl_(std::make_unique<Impl>()), cfg_(cfg) {}

Yolo26Detector::~Yolo26Detector() = default;

// ---------------------------------------------------------------------------
bool Yolo26Detector::load() {
#ifdef AURORE_HAS_ONNXRUNTIME
    try {
        impl_->opts.SetIntraOpNumThreads(cfg_.num_threads);
        impl_->opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        impl_->session =
            std::make_unique<Ort::Session>(impl_->env, cfg_.model_path.c_str(), impl_->opts);

        // Enumerate input/output names
        Ort::AllocatorWithDefaultOptions alloc;
        const size_t num_inputs = impl_->session->GetInputCount();
        const size_t num_outputs = impl_->session->GetOutputCount();

        impl_->input_names_str.clear();
        impl_->output_names_str.clear();

        for (size_t i = 0; i < num_inputs; ++i) {
            auto name = impl_->session->GetInputNameAllocated(i, alloc);
            impl_->input_names_str.emplace_back(name.get());
        }
        for (size_t i = 0; i < num_outputs; ++i) {
            auto name = impl_->session->GetOutputNameAllocated(i, alloc);
            impl_->output_names_str.emplace_back(name.get());
        }
        for (auto& s : impl_->input_names_str) impl_->input_names.push_back(s.c_str());
        for (auto& s : impl_->output_names_str) impl_->output_names.push_back(s.c_str());

        loaded_ = true;
        std::cout << "[Yolo26] Model loaded: " << cfg_.model_path << " (inputs=" << num_inputs
                  << ", outputs=" << num_outputs << ")" << std::endl;
        return true;
    } catch (const Ort::Exception& e) {
        std::cerr << "[Yolo26] Load failed: " << e.what() << std::endl;
        loaded_ = false;
        return false;
    }
#else
    std::cerr << "[Yolo26] Built without ONNX Runtime — detector disabled." << std::endl;
    return false;
#endif
}

// ---------------------------------------------------------------------------
Yolo26Detector::LetterboxResult Yolo26Detector::letterbox(const cv::Mat& bgr_frame) const {
    const int target = cfg_.input_size;
    const float scale = std::min(static_cast<float>(target) / static_cast<float>(bgr_frame.cols),
                                 static_cast<float>(target) / static_cast<float>(bgr_frame.rows));

    const int new_w = static_cast<int>(std::round(static_cast<float>(bgr_frame.cols) * scale));
    const int new_h = static_cast<int>(std::round(static_cast<float>(bgr_frame.rows) * scale));

    cv::Mat resized;
    cv::resize(bgr_frame, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

    const float pad_top = static_cast<float>(target - new_h) * 0.5f;
    const float pad_left = static_cast<float>(target - new_w) * 0.5f;
    const int top = static_cast<int>(std::round(pad_top));
    const int bottom = target - new_h - top;
    const int left = static_cast<int>(std::round(pad_left));
    const int right = target - new_w - left;

    cv::Mat padded;
    cv::copyMakeBorder(resized, padded, top, bottom, left, right, cv::BORDER_CONSTANT,
                       cv::Scalar(114, 114, 114));

    // BGR uint8 → RGB float32 normalized [0,1], HWC → CHW
    cv::Mat rgb;
    cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_32F, 1.0f / 255.0f);

    // CHW layout: split channels then write contiguous float array
    std::vector<cv::Mat> chans;
    cv::split(rgb, chans);
    cv::Mat chw;
    cv::vconcat(std::vector<cv::Mat>{chans[0].reshape(1, 1), chans[1].reshape(1, 1),
                                     chans[2].reshape(1, 1)},
                chw);

    return {chw, scale, static_cast<float>(top), static_cast<float>(left)};
}

// ---------------------------------------------------------------------------
Detection Yolo26Detector::unscale(int x1, int y1, int x2, int y2, float conf, int cls,
                                  const LetterboxResult& lb, int orig_w, int orig_h) const {
    // Remove padding, then undo scale
    const float rx1 = std::max(0.f, (static_cast<float>(x1) - lb.pad_left) / lb.scale);
    const float ry1 = std::max(0.f, (static_cast<float>(y1) - lb.pad_top) / lb.scale);
    const float rx2 =
        std::min(static_cast<float>(orig_w - 1), (static_cast<float>(x2) - lb.pad_left) / lb.scale);
    const float ry2 =
        std::min(static_cast<float>(orig_h - 1), (static_cast<float>(y2) - lb.pad_top) / lb.scale);

    Detection d;
    d.id = cls;
    d.confidence = conf;
    d.bbox.x = static_cast<int>(rx1);
    d.bbox.y = static_cast<int>(ry1);
    d.bbox.w = static_cast<int>(rx2 - rx1);
    d.bbox.h = static_cast<int>(ry2 - ry1);
    return d;
}

// ---------------------------------------------------------------------------
std::vector<Detection> Yolo26Detector::detect_all(const cv::Mat& bgr_frame) {
    std::vector<Detection> results;

#ifdef AURORE_HAS_ONNXRUNTIME
    if (!loaded_ || bgr_frame.empty()) return results;

    const int orig_w = bgr_frame.cols;
    const int orig_h = bgr_frame.rows;

    // Preprocess
    auto lb = letterbox(bgr_frame);

    // Input tensor: [1, 3, 640, 640]
    const std::array<int64_t, 4> input_shape{1, 3, cfg_.input_size, cfg_.input_size};
    const size_t input_elements =
        1 * 3 * static_cast<size_t>(cfg_.input_size) * static_cast<size_t>(cfg_.input_size);

    const float* data_ptr = reinterpret_cast<const float*>(lb.image.data);
    auto input_tensor =
        Ort::Value::CreateTensor<float>(impl_->mem_info, const_cast<float*>(data_ptr),
                                        input_elements, input_shape.data(), input_shape.size());

    // Run inference
    auto outputs =
        impl_->session->Run(Ort::RunOptions{nullptr}, impl_->input_names.data(), &input_tensor, 1,
                            impl_->output_names.data(), impl_->output_names.size());

    // Output: [1, 300, 6] → [x1, y1, x2, y2, conf, class_id]
    auto& out_tensor = outputs[0];
    const auto out_shape = out_tensor.GetTensorTypeAndShapeInfo().GetShape();
    if (out_shape.size() < 3) return results;

    const int64_t num_det = out_shape[1];
    const float* out_ptr = out_tensor.GetTensorData<float>();

    for (int64_t i = 0; i < num_det; ++i) {
        const float* row = out_ptr + i * 6;
        const float conf = row[4];
        const int cls = static_cast<int>(row[5]);

        if (conf < cfg_.conf_threshold) continue;

        // Check if class is in target list
        bool is_target = false;
        for (int tc : cfg_.target_classes) {
            if (tc == cls) {
                is_target = true;
                break;
            }
        }
        if (!is_target) continue;

        const int x1 = static_cast<int>(row[0]);
        const int y1 = static_cast<int>(row[1]);
        const int x2 = static_cast<int>(row[2]);
        const int y2 = static_cast<int>(row[3]);

        if (x2 <= x1 || y2 <= y1) continue;

        results.push_back(unscale(x1, y1, x2, y2, conf, cls, lb, orig_w, orig_h));
    }
#endif

    return results;
}

// ---------------------------------------------------------------------------
std::optional<Detection> Yolo26Detector::detect(const cv::Mat& bgr_frame) {
    auto all = detect_all(bgr_frame);
    if (all.empty()) return std::nullopt;

    // Return highest-confidence detection
    auto best = std::max_element(
        all.begin(), all.end(),
        [](const Detection& a, const Detection& b) { return a.confidence < b.confidence; });
    return *best;
}

}  // namespace aurore
