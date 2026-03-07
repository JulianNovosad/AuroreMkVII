#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <optional>
#include "../monitor/shared_metrics.hpp" // Include SharedMetrics

namespace aurore {

struct TelemetryEntry {
    uint64_t monotonic_ms;
    std::string module;
    std::string event;
    std::string thread_id = "";
    int thread_cpu = -1;
    int thread_priority = -1;
    int64_t cam_frame_id = -1;
    float cam_exposure_ms = -1.0f;
    uint32_t cam_width = 0;
    uint32_t cam_height = 0;
    std::string cam_pixel_format = "";
    int cam_buffer_fd = -1;
    int queue_depth_before = -1;
    int queue_depth_after = -1;
    int queue_overflow_count = -1;
    uint64_t inference_start_ms = 0;
    uint64_t inference_end_ms = 0;
    float inference_latency_ms = -1.0f;
    std::string inference_delegate_type = "";
    float inference_confidence = -1.0f;
    std::string model_name = "";
    float actuation_decision_ms = -1.0f;
    std::string actuation_command = "";
    int servo_pwm_value = -1;
    float servo_write_latency_ms = -1.0f;
    std::string servo_status = "";
    float cpu_percent = -1.0f;
    float cpu_temp_c = -1.0f;
    float mem_mb = -1.0f;
    float swap_mb = -1.0f;
    std::string notes = "";
};

class Telemetry {
public:
    Telemetry(const std::string& log_dir, monitor::SharedMetrics* shared_metrics = nullptr);
    ~Telemetry();

    void log_safety_invariant_violation(const std::string& reason);
    void log_event(const std::string& module, const std::string& event, const std::string& notes = "");
    void log_full_entry(const TelemetryEntry& entry);
    uint64_t get_monotonic_ms();

private:
    void write_header();
    void init_run_json();

    std::string log_dir_;
    std::ofstream unified_csv_;
    std::mutex mutex_;
    std::chrono::steady_clock::time_point t_zero_;
    monitor::SharedMetrics* shared_metrics_; // Added member
};

} // namespace aurore
