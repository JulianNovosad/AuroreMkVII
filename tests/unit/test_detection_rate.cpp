/**
 * @file test_detection_rate.cpp
 * @brief Target detection rate test framework for AM7-L2-VIS-004 verification
 *
 * Requirement AM7-L2-VIS-004:
 *   Target detection shall achieve probability of detection Pd ≥ 95% at false alarm
 *   rate FAR ≤ 10⁻⁴ per frame for specified target signatures.
 *
 * This test framework provides:
 * 1. Detection statistics collection infrastructure
 * 2. Probability of detection (Pd) computation
 * 3. False alarm rate (FAR) computation
 * 4. Target signature test harness
 *
 * Test methodology:
 * - Run detector on labeled test dataset (images with ground truth)
 * - Count true positives (TP), false positives (FP), false negatives (FN)
 * - Compute Pd = TP / (TP + FN)
 * - Compute FAR = FP / (total frames or search regions)
 *
 * Pass criteria:
 * - Pd ≥ 95% on target signature set
 * - FAR ≤ 10⁻⁴ per frame
 *
 * Note: This is a framework for detection rate testing. Actual detection
 * algorithm integration (ORB, AprilTag, color segmentation) is required
 * to run full tests.
 *
 * @copyright AuroreMkVII Project - Educational/Personal Use Only
 */

#include "aurore/timing.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// Test configuration
namespace {
    constexpr double kMinPd = 0.95;        // AM7-L2-VIS-004: Pd ≥ 95%
    constexpr double kMaxFAR = 1e-4;       // AM7-L2-VIS-004: FAR ≤ 10⁻⁴ per frame
    constexpr int kMinTestFrames = 1000;   // Minimum frames for statistical significance
}

// Detection result
struct DetectionResult {
    float confidence;           // Detection confidence [0, 1]
    float centroid_x;           // Detected centroid X (pixels)
    float centroid_y;           // Detected centroid Y (pixels)
    float width;                // Bounding box width (pixels)
    float height;               // Bounding box height (pixels)
    uint64_t timestamp_ns;      // Detection timestamp
};

// Ground truth annotation
struct GroundTruth {
    bool has_target;            // True if target is present
    float true_centroid_x;      // True centroid X (pixels)
    float true_centroid_y;      // True centroid Y (pixels)
    float true_width;           // True bounding box width (pixels)
    float true_height;          // True bounding box height (pixels)
    std::string target_class;   // Target class identifier
};

// Detection evaluation result
struct DetectionEvaluation {
    int true_positives;         // Correct detections
    int false_positives;        // Incorrect detections
    int false_negatives;        // Missed targets
    int true_negatives;         // Correct non-detections
    double probability_of_detection;  // Pd = TP / (TP + FN)
    double false_alarm_rate;          // FAR = FP / total_frames
    double precision;                 // TP / (TP + FP)
    double recall;                    // TP / (TP + FN) = Pd
};

// IoU (Intersection over Union) computation for detection matching
double compute_iou(const DetectionResult& det, const GroundTruth& gt) {
    // Compute bounding box coordinates
    const float det_left = det.centroid_x - det.width / 2.0f;
    const float det_right = det.centroid_x + det.width / 2.0f;
    const float det_top = det.centroid_y - det.height / 2.0f;
    const float det_bottom = det.centroid_y + det.height / 2.0f;

    const float gt_left = gt.true_centroid_x - gt.true_width / 2.0f;
    const float gt_right = gt.true_centroid_x + gt.true_width / 2.0f;
    const float gt_top = gt.true_centroid_y - gt.true_height / 2.0f;
    const float gt_bottom = gt.true_centroid_y + gt.true_height / 2.0f;

    // Compute intersection
    const float inter_left = std::max(det_left, gt_left);
    const float inter_right = std::min(det_right, gt_right);
    const float inter_top = std::max(det_top, gt_top);
    const float inter_bottom = std::min(det_bottom, gt_bottom);

    if (inter_left >= inter_right || inter_top >= inter_bottom) {
        return 0.0;
    }

    const float inter_area = (inter_right - inter_left) * (inter_bottom - inter_top);

    // Compute union
    const float det_area = det.width * det.height;
    const float gt_area = gt.true_width * gt.true_height;
    const float union_area = det_area + gt_area - inter_area;

    return (union_area > 0.0f) ? (inter_area / union_area) : 0.0;
}

// Evaluate detection results against ground truth
DetectionEvaluation evaluate_detections(
    const std::vector<DetectionResult>& detections,
    const std::vector<GroundTruth>& ground_truth,
    float confidence_threshold = 0.5f,
    float iou_threshold = 0.5f
) {
    DetectionEvaluation eval{};

    if (detections.size() != ground_truth.size()) {
        std::cerr << "Warning: detections and ground truth size mismatch" << std::endl;
    }

    const size_t num_frames = std::max(detections.size(), ground_truth.size());

    for (size_t i = 0; i < num_frames; ++i) {
        const bool has_detection = (i < detections.size() && detections[i].confidence >= confidence_threshold);
        const bool has_target = (i < ground_truth.size() && ground_truth[i].has_target);

        if (has_detection && has_target) {
            // Check IoU for true positive
            const double iou = compute_iou(detections[i], ground_truth[i]);
            if (iou >= iou_threshold) {
                eval.true_positives++;
            } else {
                eval.false_positives++;
            }
        } else if (has_detection && !has_target) {
            eval.false_positives++;
        } else if (!has_detection && has_target) {
            eval.false_negatives++;
        } else {
            eval.true_negatives++;
        }
    }

    // Compute metrics
    const int tp_fn = eval.true_positives + eval.false_negatives;
    const int tp_fp = eval.true_positives + eval.false_positives;

    eval.probability_of_detection = (tp_fn > 0) ?
        static_cast<double>(eval.true_positives) / tp_fn : 0.0;
    eval.false_alarm_rate = (num_frames > 0) ?
        static_cast<double>(eval.false_positives) / num_frames : 0.0;
    eval.precision = (tp_fp > 0) ?
        static_cast<double>(eval.true_positives) / tp_fp : 0.0;
    eval.recall = eval.probability_of_detection;  // Recall = Pd

    return eval;
}

// Simulated detector for framework testing
class SimulatedDetector {
public:
    SimulatedDetector(double pd, double far_per_frame)
        : target_pd_(pd), target_far_(far_per_frame), frame_count_(0) {}

    std::optional<DetectionResult> detect(const GroundTruth& gt) {
        frame_count_++;

        if (gt.has_target) {
            // Simulate detection with target present
            if (static_cast<double>(rand()) / RAND_MAX < target_pd_) {
                // True positive - add some noise to position
                DetectionResult result{};
                result.confidence = 0.7f + static_cast<float>(rand()) / RAND_MAX * 0.3f;
                result.centroid_x = gt.true_centroid_x + (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f;
                result.centroid_y = gt.true_centroid_y + (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f;
                result.width = gt.true_width * (0.9f + static_cast<float>(rand()) / RAND_MAX * 0.2f);
                result.height = gt.true_height * (0.9f + static_cast<float>(rand()) / RAND_MAX * 0.2f);
                result.timestamp_ns = aurore::get_timestamp();
                return result;
            }
            // False negative
            return std::nullopt;
        } else {
            // Simulate false alarm
            if (static_cast<double>(rand()) / RAND_MAX < target_far_) {
                DetectionResult result{};
                result.confidence = 0.5f + static_cast<float>(rand()) / RAND_MAX * 0.5f;
                result.centroid_x = static_cast<float>(rand()) % 1536;
                result.centroid_y = static_cast<float>(rand()) % 864;
                result.width = 50.0f + static_cast<float>(rand()) % 100;
                result.height = 50.0f + static_cast<float>(rand()) % 100;
                result.timestamp_ns = aurore::get_timestamp();
                return result;
            }
            // True negative
            return std::nullopt;
        }
    }

    uint64_t frame_count() const { return frame_count_; }

private:
    double target_pd_;
    double target_far_;
    uint64_t frame_count_;
};

// Generate test dataset with specified target signature
std::vector<GroundTruth> generate_test_dataset(int num_frames, double target_frequency = 0.3) {
    std::vector<GroundTruth> dataset;
    dataset.reserve(num_frames);

    for (int i = 0; i < num_frames; ++i) {
        GroundTruth gt{};
        gt.has_target = (static_cast<double>(rand()) / RAND_MAX < target_frequency);

        if (gt.has_target) {
            // Random target position within image bounds (1536x864)
            gt.true_centroid_x = 100.0f + static_cast<float>(rand() % 1336);
            gt.true_centroid_y = 100.0f + static_cast<float>(rand() % 664);
            gt.true_width = 50.0f + static_cast<float>(rand() % 100);
            gt.true_height = 50.0f + static_cast<float>(rand() % 100);
            gt.target_class = "helicopter";
        }

        dataset.push_back(gt);
    }

    return dataset;
}

void test_detection_rate_framework() {
    std::cout << "=== AM7-L2-VIS-004: Detection Rate Framework Test ===" << std::endl;
    std::cout << "Requirement: Pd ≥ 95%, FAR ≤ 10⁻⁴ per frame" << std::endl;
    std::cout << std::endl;

    // Initialize random seed for reproducibility
    srand(42);

    // Generate test dataset
    const int num_frames = kMinTestFrames;
    auto ground_truth = generate_test_dataset(num_frames, 0.3);  // 30% frames have targets

    // Test with detector that meets requirements
    SimulatedDetector detector(0.96, 5e-5);  // Pd=96%, FAR=5e-5 (meets requirements)

    std::vector<DetectionResult> detections;
    detections.reserve(num_frames);

    for (const auto& gt : ground_truth) {
        auto result = detector.detect(gt);
        if (result.has_value()) {
            detections.push_back(result.value());
        } else {
            // No detection - add placeholder
            detections.push_back(DetectionResult{0.0f, 0, 0, 0, 0, 0});
        }
    }

    // Evaluate results
    auto eval = evaluate_detections(detections, ground_truth, 0.5f, 0.5f);

    std::cout << "Results:" << std::endl;
    std::cout << "  True Positives:  " << eval.true_positives << std::endl;
    std::cout << "  False Positives: " << eval.false_positives << std::endl;
    std::cout << "  False Negatives: " << eval.false_negatives << std::endl;
    std::cout << "  True Negatives:  " << eval.true_negatives << std::endl;
    std::cout << std::endl;
    std::cout << "  Probability of Detection (Pd): " << (eval.probability_of_detection * 100) << " %" << std::endl;
    std::cout << "  False Alarm Rate (FAR):        " << eval.false_alarm_rate << " per frame" << std::endl;
    std::cout << "  Precision:                     " << (eval.precision * 100) << " %" << std::endl;
    std::cout << "  Recall:                        " << (eval.recall * 100) << " %" << std::endl;
    std::cout << std::endl;

    // Verify requirements
    const bool pd_pass = (eval.probability_of_detection >= kMinPd);
    const bool far_pass = (eval.false_alarm_rate <= kMaxFAR);

    std::cout << "Verification:" << std::endl;
    std::cout << "  AM7-L2-VIS-004 Pd ≥ 95%:     " << (pd_pass ? "PASS" : "FAIL")
              << " (measured: " << (eval.probability_of_detection * 100) << "%)" << std::endl;
    std::cout << "  AM7-L2-VIS-004 FAR ≤ 10⁻⁴:   " << (far_pass ? "PASS" : "FAIL")
              << " (measured: " << eval.false_alarm_rate << ")" << std::endl;

    assert(pd_pass && "AM7-L2-VIS-004: Probability of detection below 95%");
    assert(far_pass && "AM7-L2-VIS-004: False alarm rate exceeds 10⁻⁴");

    std::cout << "  PASS" << std::endl;
}

void test_iou_computation() {
    std::cout << "=== IoU Computation Test ===" << std::endl;

    // Test case 1: Perfect overlap
    DetectionResult det1{1.0f, 100.0f, 100.0f, 50.0f, 50.0f, 0};
    GroundTruth gt1{true, 100.0f, 100.0f, 50.0f, 50.0f, "target"};
    double iou1 = compute_iou(det1, gt1);
    std::cout << "  Perfect overlap IoU: " << iou1 << " (expected: 1.0)" << std::endl;
    assert(std::abs(iou1 - 1.0) < 1e-6 && "Perfect overlap should have IoU=1.0");

    // Test case 2: No overlap
    DetectionResult det2{1.0f, 100.0f, 100.0f, 50.0f, 50.0f, 0};
    GroundTruth gt2{true, 300.0f, 300.0f, 50.0f, 50.0f, "target"};
    double iou2 = compute_iou(det2, gt2);
    std::cout << "  No overlap IoU: " << iou2 << " (expected: 0.0)" << std::endl;
    assert(std::abs(iou2 - 0.0) < 1e-6 && "No overlap should have IoU=0.0");

    // Test case 3: Partial overlap (50%)
    DetectionResult det3{1.0f, 125.0f, 100.0f, 50.0f, 50.0f, 0};
    GroundTruth gt3{true, 150.0f, 100.0f, 50.0f, 50.0f, "target"};
    double iou3 = compute_iou(det3, gt3);
    std::cout << "  Partial overlap IoU: " << iou3 << " (expected: ~0.43)" << std::endl;
    assert(iou3 > 0.4 && iou3 < 0.5 && "Partial overlap should have IoU around 0.43");

    std::cout << "  PASS" << std::endl;
}

void test_detection_evaluation_metrics() {
    std::cout << "=== Detection Evaluation Metrics Test ===" << std::endl;

    // Create known detection/ground truth pairs
    std::vector<DetectionResult> detections = {
        {0.9f, 100.0f, 100.0f, 50.0f, 50.0f, 0},  // TP
        {0.8f, 200.0f, 200.0f, 50.0f, 50.0f, 0},  // FP (no target)
        {0.0f, 0, 0, 0, 0, 0},                      // FN (no detection)
        {0.0f, 0, 0, 0, 0, 0},                      // TN (no detection, no target)
    };

    std::vector<GroundTruth> ground_truth = {
        {true, 100.0f, 100.0f, 50.0f, 50.0f, "target"},   // Frame 0: target present
        {false, 0, 0, 0, 0, ""},                           // Frame 1: no target
        {true, 300.0f, 300.0f, 50.0f, 50.0f, "target"},   // Frame 2: target present (missed)
        {false, 0, 0, 0, 0, ""},                           // Frame 3: no target
    };

    auto eval = evaluate_detections(detections, ground_truth, 0.5f, 0.5f);

    std::cout << "  TP: " << eval.true_positives << " (expected: 1)" << std::endl;
    std::cout << "  FP: " << eval.false_positives << " (expected: 1)" << std::endl;
    std::cout << "  FN: " << eval.false_negatives << " (expected: 1)" << std::endl;
    std::cout << "  TN: " << eval.true_negatives << " (expected: 1)" << std::endl;

    // Pd = TP / (TP + FN) = 1 / (1 + 1) = 0.5
    std::cout << "  Pd: " << eval.probability_of_detection << " (expected: 0.5)" << std::endl;
    assert(std::abs(eval.probability_of_detection - 0.5) < 1e-6 && "Pd should be 0.5");

    // FAR = FP / total_frames = 1 / 4 = 0.25
    std::cout << "  FAR: " << eval.false_alarm_rate << " (expected: 0.25)" << std::endl;
    assert(std::abs(eval.false_alarm_rate - 0.25) < 1e-6 && "FAR should be 0.25");

    // Precision = TP / (TP + FP) = 1 / (1 + 1) = 0.5
    std::cout << "  Precision: " << eval.precision << " (expected: 0.5)" << std::endl;
    assert(std::abs(eval.precision - 0.5) < 1e-6 && "Precision should be 0.5");

    std::cout << "  PASS" << std::endl;
}

void test_confidence_threshold_sweep() {
    std::cout << "=== Confidence Threshold Sweep Test ===" << std::endl;
    std::cout << "Evaluating Pd/FAR at different confidence thresholds" << std::endl;
    std::cout << std::endl;

    srand(42);

    const int num_frames = 500;
    auto ground_truth = generate_test_dataset(num_frames, 0.3);

    // Detector with moderate performance
    SimulatedDetector detector(0.90, 1e-3);

    std::vector<DetectionResult> detections;
    detections.reserve(num_frames);

    for (const auto& gt : ground_truth) {
        auto result = detector.detect(gt);
        if (result.has_value()) {
            detections.push_back(result.value());
        } else {
            detections.push_back(DetectionResult{0.0f, 0, 0, 0, 0, 0});
        }
    }

    std::cout << "  Threshold |   Pd    |   FAR    | Precision" << std::endl;
    std::cout << "  ----------+---------+----------+----------" << std::endl;

    for (double threshold = 0.3; threshold <= 0.9; threshold += 0.1) {
        auto eval = evaluate_detections(detections, ground_truth, static_cast<float>(threshold), 0.5f);
        std::cout << "    " << threshold << "     | "
                  << (eval.probability_of_detection * 100) << " %  | "
                  << eval.false_alarm_rate << "   | "
                  << (eval.precision * 100) << " %" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "  PASS (framework demonstration)" << std::endl;
}

}  // anonymous namespace

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "Target Detection Rate Test Framework" << std::endl;
    std::cout << "AM7-L2-VIS-004 Verification" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    int passed = 0;
    int failed = 0;

    auto run_test = [&](const char* /*name*/, void (*test_fn)()) {
        try {
            test_fn();
            passed++;
        } catch (const std::exception& e) {
            std::cerr << "  FAIL: " << e.what() << std::endl;
            failed++;
        }
    };

    run_test("test_detection_rate_framework", test_detection_rate_framework);
    run_test("test_iou_computation", test_iou_computation);
    run_test("test_detection_evaluation_metrics", test_detection_evaluation_metrics);
    run_test("test_confidence_threshold_sweep", test_confidence_threshold_sweep);

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Tests run: " << (passed + failed) << std::endl;
    std::cout << "Tests passed: " << passed << std::endl;
    std::cout << "Tests failed: " << failed << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Note: This is a test framework. For actual detector" << std::endl;
    std::cout << "evaluation, integrate with ORB/AprilTag detector and" << std::endl;
    std::cout << "run on labeled target signature dataset." << std::endl;

    return (failed == 0) ? 0 : 1;
}
