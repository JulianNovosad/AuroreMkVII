#include "telemetry.hpp"
#include <chrono>
#include <iostream> // Keep for potential non-debug messages, but remove debug ones
#include <filesystem>
#include <iomanip>

namespace aurore {

Telemetry::Telemetry(const std::string& log_dir, monitor::SharedMetrics* shared_metrics)
    : log_dir_(log_dir), shared_metrics_(shared_metrics) {
    t_zero_ = std::chrono::steady_clock::now();
    std::filesystem::create_directories(log_dir);
    unified_csv_.open(log_dir + "/unified.csv", std::ios::out);
    write_header();
    init_run_json();
}

Telemetry::~Telemetry() {
    if (unified_csv_.is_open()) {
        unified_csv_.flush();
        unified_csv_.close();
    }
}

void Telemetry::write_header() {
    unified_csv_ << "produced_ts_epoch_ms,monotonic_ms,module,event,thread_id,thread_cpu,thread_priority,"
                 << "cam_frame_id,cam_exposure_ms,cam_width,cam_height,cam_pixel_format,cam_buffer_fd,"
                 << "queue_depth_before,queue_depth_after,queue_overflow_count,"
                 << "inference_start_ms,inference_end_ms,inference_latency_ms,inference_delegate_type,inference_confidence,model_name,"
                 << "actuation_decision_ms,actuation_command,servo_pwm_value,servo_write_latency_ms,servo_status,"
                 << "cpu_percent,cpu_temp_c,mem_mb,swap_mb,notes\n";
    unified_csv_.flush();
}

void Telemetry::init_run_json() {
    std::ofstream run_json(log_dir_ + "/run.json");
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    run_json << "{\n"
             << "  \"session_start_utc\": \"" << std::put_time(std::gmtime(&tt), "%Y-%m-%d %H:%M:%S") << "\",\n"
             << "  \"status\": \"started\"\n"
             << "}\n";
}

uint64_t Telemetry::get_monotonic_ms() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - t_zero_).count();
}

void Telemetry::log_safety_invariant_violation(const std::string& reason) {
    log_event("safety", "invariant_violation", reason);
    if (shared_metrics_) {
        std::lock_guard<std::mutex> lock(shared_metrics_->mutex);
        shared_metrics_->safety_invariant_violated.store(true);
        shared_metrics_->safety_violation_reason = reason;
    }
}

void Telemetry::log_event(const std::string& module, const std::string& event, const std::string& notes) {
    TelemetryEntry entry;
    entry.monotonic_ms = get_monotonic_ms();
    entry.module = module;
    entry.event = event;
    entry.notes = notes;
    log_full_entry(entry);
}

void Telemetry::log_full_entry(const TelemetryEntry& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    unified_csv_ << epoch << "," << e.monotonic_ms << "," << e.module << "," << e.event << ","
                 << e.thread_id << "," << e.thread_cpu << "," << e.thread_priority << ","
                 << e.cam_frame_id << "," << e.cam_exposure_ms << "," << e.cam_width << "," << e.cam_height << "," << e.cam_pixel_format << "," << e.cam_buffer_fd << ","
                 << e.queue_depth_before << "," << e.queue_depth_after << "," << e.queue_overflow_count << ","
                 << e.inference_start_ms << "," << e.inference_end_ms << "," << e.inference_latency_ms << "," << e.inference_delegate_type << "," << e.inference_confidence << "," << e.model_name << ","
                 << e.actuation_decision_ms << "," << e.actuation_command << "," << e.servo_pwm_value << "," << e.servo_write_latency_ms << "," << e.servo_status << ","
                 << e.cpu_percent << "," << e.cpu_temp_c << "," << e.mem_mb << "," << e.swap_mb << "," << e.notes << "\n";
    unified_csv_.flush();

    // Update shared metrics for TUI if available
    if (shared_metrics_) {
        std::lock_guard<std::mutex> shared_lock(shared_metrics_->mutex); // Lock for string members
        if (e.queue_depth_before != -1) shared_metrics_->camera_queue_out_count.store(e.queue_depth_before); // This mapping needs to be accurate
        if (e.queue_depth_after != -1) shared_metrics_->inference_queue_in_count.store(e.queue_depth_after); // This mapping needs to be accurate
        if (e.queue_overflow_count != -1) shared_metrics_->queue_overflow_count.store(e.queue_overflow_count);
        if (e.inference_latency_ms != -1.0f) shared_metrics_->inference_latency_ms.store(e.inference_latency_ms);
        if (!e.inference_delegate_type.empty()) shared_metrics_->inference_delegate_type = e.inference_delegate_type;
        if (e.cpu_temp_c != -1.0f) shared_metrics_->cpu_temp_c.store(e.cpu_temp_c);
        if (e.cpu_percent != -1.0f) shared_metrics_->cpu_usage_percent.store(e.cpu_percent);
        if (e.mem_mb != -1.0f) shared_metrics_->mem_usage_mb.store(e.mem_mb);
        if (e.swap_mb != -1.0f) shared_metrics_->swap_usage_mb.store(e.swap_mb);
        
        // Safety invariant violation is handled in log_safety_invariant_violation
    }
}

} // namespace aurore
