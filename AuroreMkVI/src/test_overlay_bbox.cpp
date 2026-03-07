#include <cassert>
#include <vector>
#include <algorithm>
#include <iostream>

struct BoundingBoxOverlay {
    float xmin, ymin, xmax, ymax;
    float confidence;
    int class_id;
    float r, g, b;

    BoundingBoxOverlay(float xm, float ym, float xM, float yM, float conf, int cid, float red = 1.0f, float green = 0.0f, float blue = 0.0f)
        : xmin(xm), ymin(ym), xmax(xM), ymax(yM), confidence(conf), class_id(cid), r(red), g(green), b(blue) {}

    float width() const { return xmax - xmin; }
    float height() const { return ymax - ymin; }
    bool is_valid() const { return xmax > xmin && ymax > ymin && confidence > 0.0f; }
};

struct DetectionResult {
    int class_id;
    float score;
    float xmin, ymin, xmax, ymax;
};

std::vector<BoundingBoxOverlay> detection_to_overlay(const std::vector<DetectionResult>& detections, int max_count = 3) {
    std::vector<BoundingBoxOverlay> result;

    std::vector<DetectionResult> sorted = detections;
    std::sort(sorted.begin(), sorted.end(), [](const DetectionResult& a, const DetectionResult& b) {
        return a.score > b.score;
    });

    int count = 0;
    for (const auto& d : sorted) {
        if (count >= max_count) break;
        result.emplace_back(d.xmin, d.ymin, d.xmax, d.ymax, d.score, d.class_id);
        count++;
    }

    return result;
}

void test_bbox_creation() {
    BoundingBoxOverlay bbox(0.1f, 0.2f, 0.5f, 0.8f, 0.95f, 1);
    assert(bbox.xmin == 0.1f);
    assert(bbox.xmax == 0.5f);
    assert(bbox.width() == 0.4f);
    assert(bbox.height() == 0.6f);
    assert(bbox.is_valid());
    assert(bbox.r == 1.0f && bbox.g == 0.0f && bbox.b == 0.0f);
    std::cout << "[PASS] test_bbox_creation" << std::endl;
}

void test_bbox_invalid() {
    BoundingBoxOverlay bbox(0.5f, 0.5f, 0.3f, 0.3f, 0.5f, 1);
    assert(!bbox.is_valid());
    std::cout << "[PASS] test_bbox_invalid" << std::endl;
}

void test_detection_to_overlay_sorting() {
    std::vector<DetectionResult> dets = {
        {0, 0.5f, 0.0f, 0.0f, 0.1f, 0.1f},
        {1, 0.95f, 0.2f, 0.2f, 0.3f, 0.3f},
        {2, 0.7f, 0.4f, 0.4f, 0.5f, 0.5f},
        {3, 0.3f, 0.6f, 0.6f, 0.7f, 0.7f},
    };

    auto overlays = detection_to_overlay(dets, 3);

    assert(overlays.size() == 3);
    assert(overlays[0].confidence == 0.95f);
    assert(overlays[1].confidence == 0.7f);
    assert(overlays[2].confidence == 0.5f);
    std::cout << "[PASS] test_detection_to_overlay_sorting" << std::endl;
}

void test_detection_to_overlay_max_count() {
    std::vector<DetectionResult> dets = {
        {0, 0.9f, 0.0f, 0.0f, 0.1f, 0.1f},
        {1, 0.8f, 0.1f, 0.1f, 0.2f, 0.2f},
        {2, 0.7f, 0.2f, 0.2f, 0.3f, 0.3f},
        {3, 0.6f, 0.3f, 0.3f, 0.4f, 0.4f},
        {4, 0.5f, 0.4f, 0.4f, 0.5f, 0.5f},
    };

    auto overlays = detection_to_overlay(dets, 3);
    assert(overlays.size() == 3);
    std::cout << "[PASS] test_detection_to_overlay_max_count" << std::endl;
}

void test_empty_detections() {
    std::vector<DetectionResult> dets;
    auto overlays = detection_to_overlay(dets);
    assert(overlays.empty());
    std::cout << "[PASS] test_empty_detections" << std::endl;
}

int main() {
    std::cout << "=== Overlay Bounding Box Tests ===" << std::endl;

    test_bbox_creation();
    test_bbox_invalid();
    test_detection_to_overlay_sorting();
    test_detection_to_overlay_max_count();
    test_empty_detections();

    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}
