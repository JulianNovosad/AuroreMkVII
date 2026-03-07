#include <cassert>
#include <vector>
#include <iostream>
#include <thread>
#include <atomic>
#include "lockfree_queue.h"
#include "pipeline_structs.h"





void test_queue_basic_ops() {
    DetectionOverlayQueue queue;

    OverlayData data1;
    data1.detections.push_back({0.1f, 0.1f, 0.3f, 0.3f, 0.95f, 0});
    data1.ballistic_point.is_valid = true;
    data1.ballistic_point.impact_px_x = 100.0f;
    data1.ballistic_point.impact_px_y = 200.0f;

    assert(queue.push(data1));
    std::cout << "[PASS] test_queue_basic_ops - push" << std::endl;

    OverlayData result;
    assert(queue.pop(result));
    assert(result.detections.size() == 1);
    assert(result.ballistic_point.is_valid);
    assert(result.ballistic_point.impact_px_x == 100.0f);
    std::cout << "[PASS] test_queue_basic_ops - pop" << std::endl;
}

void test_queue_multiple_producers() {
    DetectionOverlayQueue queue;
    std::atomic<int> success_count{0};
    const int iterations = 8;

    auto producer = [&]() {
        for (int i = 0; i < iterations; i++) {
            OverlayData data;
            data.detections.push_back({0.1f, 0.1f, 0.3f, 0.3f, 0.5f + (i * 0.05f), (int)i});
            if (queue.push(data)) {
                success_count++;
            }
        }
    };

    std::thread t1(producer);
    std::thread t2(producer);
    t1.join();
    t2.join();

    std::cout << "[PASS] test_queue_multiple_producers - " << success_count.load() << " items pushed" << std::endl;

    int pop_count = 0;
    OverlayData result;
    while (queue.pop(result)) {
        pop_count++;
    }
    assert(pop_count == success_count);
    std::cout << "[PASS] test_queue_multiple_producers - " << pop_count << " items popped" << std::endl;
}

void test_queue_empty() {
    DetectionOverlayQueue queue;
    OverlayData result;
    assert(!queue.pop(result));
    std::cout << "[PASS] test_queue_empty - pop on empty returns false" << std::endl;
}

void test_queue_full() {
    DetectionOverlayQueue queue; // Capacity is now 4

    for (std::size_t i = 0; i < DetectionOverlayQueue::capacity(); i++) {
        OverlayData data;
        data.detections.push_back({0.1f, 0.1f, 0.3f, 0.3f, 0.5f, (int)i});
        assert(queue.push(data)); // All pushes up to capacity should succeed
    }

    OverlayData overflow;
    overflow.detections.push_back({0.1f, 0.1f, 0.3f, 0.3f, 0.99f, 99});
    assert(!queue.push(overflow)); // This push should fail
    std::cout << "[PASS] test_queue_full - overflow rejected" << std::endl;
}

void test_overlay_data_methods() {
    OverlayData data;
    assert(!data.has_ballistic_point());

    data.ballistic_point.is_valid = true;
    assert(data.has_ballistic_point());

    data.clear();
    assert(!data.has_ballistic_point());
    assert(data.detections.empty());
    std::cout << "[PASS] test_overlay_data_methods" << std::endl;
}

int main() {
    std::cout << "=== Detection Overlay Queue Tests ===" << std::endl;

    test_queue_basic_ops();
    test_queue_multiple_producers();
    test_queue_empty();
    test_queue_full();
    test_overlay_data_methods();

    std::cout << "=== All queue tests passed ===" << std::endl;
    return 0;
}
