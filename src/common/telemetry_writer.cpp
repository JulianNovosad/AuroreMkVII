/**
 * @file telemetry_writer.cpp
 * @brief Implementation of asynchronous telemetry logging
 *
 * SEC-010: Implements backpressure handling to prevent queue overflow DoS
 */

#include "aurore/telemetry_writer.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace aurore {

TelemetryWriter::~TelemetryWriter() {
    stop();
}

bool TelemetryWriter::start(const TelemetryConfig& config) {
    if (running_.load(std::memory_order_acquire)) {
        return false;  // Already running
    }

    config_ = config;

    // SEC-010: Compute high-water threshold
    queue_high_water_ = (config_.max_queue_size * config_.queue_high_water_pct) / 100;
    if (queue_high_water_ == 0) {
        queue_high_water_ = 1;  // Minimum threshold
    }

    // Create log directory
    mkdir(config_.log_dir.c_str(), 0755);

    // Rotate old logs
    rotate_logs();

    // Generate session ID and filename
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << config_.log_dir << "/" << config_.session_prefix << "_"
       << std::put_time(std::localtime(&time_t_now), "%Y%m%d_%H%M%S")
       << ".csv";
    session_csv_path_ = ss.str();

    // Open CSV file
    csv_file_.open(session_csv_path_, std::ios::out);
    if (!csv_file_.is_open()) {
        std::cerr << "Failed to open CSV file: " << session_csv_path_ << std::endl;
        return false;
    }

    // Write CSV header
    write_csv_header();

    // Record start time
    start_time_ns_ = get_timestamp_ns();
    session_id_ = 1;

    // Reset statistics
    entries_written_.store(0, std::memory_order_release);
    entries_dropped_.store(0, std::memory_order_release);
    entries_enqueued_.store(0, std::memory_order_release);
    queue_depth_.store(0, std::memory_order_release);
    high_water_mark_.store(0, std::memory_order_release);
    backpressure_active_.store(false, std::memory_order_release);

    // Start writer thread
    running_.store(true, std::memory_order_release);
    writer_thread_ = std::thread(&TelemetryWriter::writer_loop, this);

    std::cout << "TelemetryWriter started: " << session_csv_path_ << std::endl;
    return true;
}

void TelemetryWriter::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;  // Already stopped
    }

    // Wake up writer thread
    queue_cv_.notify_all();

    // Wait for writer thread
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    // Write JSON summary
    if (config_.enable_json) {
        write_json_summary();
    }

    // Close CSV file
    if (csv_file_.is_open()) {
        csv_file_.close();
    }

    std::cout << "TelemetryWriter stopped. Entries written: "
              << entries_written_.load(std::memory_order_acquire)
              << ", dropped: " << entries_dropped_.load(std::memory_order_acquire)
              << std::endl;
}

// SEC-010: Enqueue with backpressure handling
bool TelemetryWriter::enqueue_entry(const CsvLogEntry& entry) {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    size_t current_depth = queue_depth_.load(std::memory_order_acquire);

    // SEC-010: Check if queue is full
    if (current_depth >= config_.max_queue_size) {
        // Apply backpressure policy
        switch (config_.backpressure_policy) {
            case BackpressurePolicy::kDropOldest:
                // Drop oldest entry to make room
                if (!entry_queue_.empty()) {
                    entry_queue_.pop();
                    entries_dropped_.fetch_add(1, std::memory_order_relaxed);
                    current_depth = queue_depth_.fetch_sub(1, std::memory_order_acq_rel) - 1;
                }
                break;

            case BackpressurePolicy::kDropNewest:
                // Drop the new entry
                entries_dropped_.fetch_add(1, std::memory_order_relaxed);
                check_backpressure_state(current_depth);
                return false;

            case BackpressurePolicy::kBlock:
                // Wait until space available (not recommended for real-time)
                queue_cv_.wait(lock, [this] {
                    return queue_depth_.load(std::memory_order_acquire) < config_.max_queue_size;
                });
                current_depth = queue_depth_.load(std::memory_order_acquire);
                break;
        }
    }

    // Queue the entry
    entry_queue_.push(entry);
    size_t new_depth = queue_depth_.fetch_add(1, std::memory_order_acq_rel) + 1;
    entries_enqueued_.fetch_add(1, std::memory_order_relaxed);

    // SEC-010: Update monitoring
    update_high_water_mark(new_depth);
    check_backpressure_state(new_depth);

    return true;
}

// SEC-010: Update high-water mark
void TelemetryWriter::update_high_water_mark(size_t current_depth) {
    size_t current_hwm = high_water_mark_.load(std::memory_order_acquire);
    while (current_depth > current_hwm) {
        if (high_water_mark_.compare_exchange_weak(current_hwm, current_depth,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            break;
        }
    }
}

// SEC-010: Check and update backpressure state
void TelemetryWriter::check_backpressure_state(size_t current_depth) {
    bool should_be_active = (current_depth >= queue_high_water_);
    bool currently_active = backpressure_active_.load(std::memory_order_acquire);

    if (should_be_active != currently_active) {
        backpressure_active_.store(should_be_active, std::memory_order_release);

        if (should_be_active) {
            std::cerr << "TelemetryWriter: BACKPRESSURE ACTIVE - queue depth "
                      << current_depth << " >= high-water " << queue_high_water_
                      << std::endl;
        }
    }
}

void TelemetryWriter::log_frame(
    const DetectionData& detection,
    const TrackData& track,
    const ActuationData& actuation,
    const SystemHealthData& health) {

    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    // Create log entry
    CsvLogEntry entry;

    // Timestamps
    auto now = std::chrono::system_clock::now();
    auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    entry.produced_ts_epoch_ms = epoch_ms;
    entry.call_ts_epoch_ms = epoch_ms;

    // Frame ID from detection
    entry.cam_frame_id = detection.frame_id;

    // Detection data
    if (detection.is_valid()) {
        entry.det_x = detection.x;
        entry.det_y = detection.y;
        entry.det_width = detection.width;
        entry.det_height = detection.height;
        entry.det_confidence = detection.confidence;
        entry.det_target_class = detection.target_class;
    }

    // Track data
    if (track.is_valid()) {
        entry.track_id = track.track_id;
        entry.track_x = track.x;
        entry.track_y = track.y;
        entry.track_z = track.z;
        entry.track_hit_streak = track.hit_streak;
        entry.track_confidence = track.confidence;
    }

    // Actuation data
    entry.servo_azimuth = actuation.azimuth_deg;
    entry.servo_elevation = actuation.elevation_deg;
    entry.servo_command_sent = actuation.command_sent;

    // System health
    entry.cpu_temp_c = health.cpu_temp_c;
    entry.cpu_usage_percent = health.cpu_usage_percent;

    // Module/event (empty for frame logs)
    entry.set_module("FrameLog");
    entry.set_event("frame_complete");

    // SEC-010: Queue entry with backpressure handling
    bool queued = enqueue_entry(entry);
    if (!queued) {
        // Entry was dropped due to backpressure
        return;
    }

    // Notify writer thread
    queue_cv_.notify_one();

    // Update statistics
    uint64_t now_ns = get_timestamp_ns();
    if (first_frame_time_ns_ == 0) {
        first_frame_time_ns_ = now_ns;
    }
    last_frame_time_ns_ = now_ns;
    frame_count_++;
}

void TelemetryWriter::log_event(
    TelemetryEventId event_id,
    TelemetrySeverity severity,
    const std::string& message) {

    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    // SEC-009: Bounds-check message length
    std::string truncated_message = message;
    if (message.size() > kMessage_max - 1) {
        truncated_message = message.substr(0, kMessage_max - 1);
    }

    // For MVP, just print to console
    // Full implementation would queue as CsvLogEntry

    const char* severity_str = "";
    switch (severity) {
        case TelemetrySeverity::kDebug: severity_str = "DEBUG"; break;
        case TelemetrySeverity::kInfo: severity_str = "INFO"; break;
        case TelemetrySeverity::kWarning: severity_str = "WARNING"; break;
        case TelemetrySeverity::kError: severity_str = "ERROR"; break;
        case TelemetrySeverity::kCritical: severity_str = "CRITICAL"; break;
    }

    std::cout << "[" << severity_str << "] Event "
              << static_cast<int>(event_id) << ": " << truncated_message << std::endl;
}

void TelemetryWriter::writer_loop() {
    while (running_.load(std::memory_order_acquire)) {
        CsvLogEntry entry;
        bool has_entry = false;

        // Wait for entry or timeout
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // Wait with timeout to allow checking running_ flag
            if (queue_cv_.wait_for(lock, std::chrono::milliseconds(100),
                                   [this] {
                                       return !entry_queue_.empty() ||
                                              !running_.load(std::memory_order_acquire);
                                   })) {
                if (!entry_queue_.empty()) {
                    entry = entry_queue_.front();
                    entry_queue_.pop();

                    // SEC-010: Update atomic depth counter
                    size_t new_depth = queue_depth_.fetch_sub(1, std::memory_order_acq_rel) - 1;
                    check_backpressure_state(new_depth);

                    has_entry = true;
                }
            }
        }

        // Write entry
        if (has_entry && csv_file_.is_open()) {
            write_csv_entry(entry);
            entries_written_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Drain remaining entries
    while (true) {
        CsvLogEntry entry;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (entry_queue_.empty()) break;
            entry = entry_queue_.front();
            entry_queue_.pop();

            // SEC-010: Update atomic depth counter
            size_t new_depth = queue_depth_.fetch_sub(1, std::memory_order_acq_rel) - 1;
            check_backpressure_state(new_depth);
        }
        write_csv_entry(entry);
        entries_written_.fetch_add(1, std::memory_order_relaxed);
    }

    // Reset backpressure state on shutdown
    backpressure_active_.store(false, std::memory_order_release);
    queue_depth_.store(0, std::memory_order_release);
}

TelemetryQueueStats TelemetryWriter::get_queue_stats() const {
    TelemetryQueueStats stats;

    std::lock_guard<std::mutex> lock(queue_mutex_);
    stats.current_depth = queue_depth_.load(std::memory_order_acquire);
    stats.high_water_mark = high_water_mark_.load(std::memory_order_acquire);
    stats.max_depth = config_.max_queue_size;
    stats.total_enqueued = entries_enqueued_.load(std::memory_order_acquire);
    stats.total_dropped = entries_dropped_.load(std::memory_order_acquire);
    stats.backpressure_active = backpressure_active_.load(std::memory_order_acquire);

    return stats;
}

void TelemetryWriter::write_csv_header() {
    csv_file_ << "produced_ts_epoch_ms,monotonic_ms,module,event,cam_frame_id,"
              << "det_x,det_y,det_width,det_height,det_confidence,det_target_class,"
              << "track_id,track_x,track_y,track_z,track_hit_streak,track_confidence,"
              << "servo_azimuth,servo_elevation,servo_command_sent,"
              << "cpu_temp_c,cpu_usage_percent\n";
}

void TelemetryWriter::write_csv_entry(const CsvLogEntry& entry) {
    // Calculate monotonic ms from start
    uint64_t monotonic_ms = (entry.call_ts_epoch_ms -
                            (start_time_ns_ / 1000000));

    csv_file_ << entry.produced_ts_epoch_ms << ","
              << monotonic_ms << ","
              << entry.module << ","
              << entry.event << ","
              << entry.cam_frame_id << ","
              << entry.det_x << ","
              << entry.det_y << ","
              << entry.det_width << ","
              << entry.det_height << ","
              << entry.det_confidence << ","
              << (int)entry.det_target_class << ","
              << entry.track_id << ","
              << entry.track_x << ","
              << entry.track_y << ","
              << entry.track_z << ","
              << entry.track_hit_streak << ","
              << entry.track_confidence << ","
              << entry.servo_azimuth << ","
              << entry.servo_elevation << ","
              << (entry.servo_command_sent ? "1" : "0") << ","
              << entry.cpu_temp_c << ","
              << entry.cpu_usage_percent << "\n";

    // Flush periodically (every 100 entries) for debugging
    if (entries_written_.load(std::memory_order_relaxed) % 100 == 0) {
        csv_file_.flush();
    }
}

void TelemetryWriter::write_json_summary() {
    std::string json_path = config_.log_dir + "/" +
                           config_.session_prefix + "_summary.json";

    std::ofstream json_file(json_path);
    if (!json_file.is_open()) {
        return;
    }

    // Calculate statistics
    uint64_t duration_ns = last_frame_time_ns_ - first_frame_time_ns_;
    double duration_sec = static_cast<double>(duration_ns) / 1e9;
    double avg_fps = (duration_sec > 0) ?
                     static_cast<double>(frame_count_) / duration_sec : 0.0;

    // SEC-010: Include backpressure statistics
    TelemetryQueueStats queue_stats = get_queue_stats();

    json_file << "{\n";
    json_file << "  \"session_id\": " << session_id_ << ",\n";
    json_file << "  \"session_file\": \"" << session_csv_path_ << "\",\n";
    json_file << "  \"start_time_ns\": " << start_time_ns_ << ",\n";
    json_file << "  \"duration_sec\": " << std::fixed << std::setprecision(3)
              << duration_sec << ",\n";
    json_file << "  \"frame_count\": " << frame_count_ << ",\n";
    json_file << "  \"entries_written\": " << entries_written_.load() << ",\n";
    json_file << "  \"entries_dropped\": " << entries_dropped_.load() << ",\n";
    json_file << "  \"avg_fps\": " << std::fixed << std::setprecision(2)
              << avg_fps << ",\n";
    // SEC-010: Backpressure stats
    json_file << "  \"queue_max_depth\": " << queue_stats.max_depth << ",\n";
    json_file << "  \"queue_high_water_mark\": " << queue_stats.high_water_mark << ",\n";
    json_file << "  \"backpressure_events\": "
              << (queue_stats.total_dropped > 0 ? "true" : "false") << "\n";
    json_file << "}\n";

    json_file.close();
    std::cout << "JSON summary written: " << json_path << std::endl;
}

void TelemetryWriter::rotate_logs() {
    // Simple rotation: delete oldest sessions if too many
    // Full implementation would use filesystem to list and sort

    // For MVP, just create a new session file each run
    // Rotation happens at start of next session
}

uint64_t TelemetryWriter::get_timestamp_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000UL +
           static_cast<uint64_t>(ts.tv_nsec);
}

} // namespace aurore
