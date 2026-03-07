#pragma once

#include <string>
#include <mutex>
#include <atomic>

namespace aurore {
namespace monitor {

struct SharedMetrics {
    std::mutex mutex;

    // Data Throughput (example, needs to be refined based on actual queues)
    std::atomic<uint64_t> camera_queue_in_count = 0;
    std::atomic<uint64_t> camera_queue_out_count = 0;
    std::atomic<uint64_t> inference_queue_in_count = 0;
    std::atomic<uint64_t> inference_queue_out_count = 0;
    
    // Invariance Check Results (simplified)
    std::atomic<bool> safety_invariant_violated = false;
    std::string safety_violation_reason = "None"; // Access protected by mutex

    // FPS Metrics
    std::atomic<float> camera_fps = 0.0f;
    std::atomic<float> inference_fps = 0.0f;
    std::atomic<float> total_system_fps = 0.0f; // Overall processing FPS

    // Other relevant telemetry data from TelemetryEntry
    std::atomic<float> cpu_temp_c = 0.0f;
    std::atomic<float> cpu_usage_percent = 0.0f;
    std::atomic<float> mem_usage_mb = 0.0f;
    std::atomic<float> swap_usage_mb = 0.0f;
    std::atomic<int> queue_overflow_count = 0;
    std::atomic<float> inference_latency_ms = 0.0f;
    std::string inference_delegate_type = "N/A"; // Access protected by mutex
};

} // namespace monitor
} // namespace aurore
