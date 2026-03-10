/**
 * @file telemetry_writer.hpp
 * @brief Asynchronous telemetry logging for Aurore MkVII
 *
 * Per spec.md AM7-L2-HUD-002: Provides telemetry for remote HUD rendering
 * Per spec.md AM7-L3-TIM-001: Uses CLOCK_MONOTONIC_RAW for all timestamps
 *
 * Features:
 * - Async writer thread (non-blocking for control loops)
 * - CSV output (unified.csv format compatible with MkVI)
 * - JSON summary (run.json for quick status)
 * - Log rotation (configurable size/runs)
 *
 * SEC-010: Backpressure handling with configurable drop policy
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>

#include "aurore/ring_buffer.hpp"
#include "aurore/telemetry_types.hpp"

namespace aurore {

// CsvLogEntry is queued and copied between threads — it must remain trivially copyable.
static_assert(std::is_trivially_copyable<CsvLogEntry>::value,
              "CsvLogEntry must be trivially copyable for safe async queue transfer");

// SEC-010: Backpressure policy options
enum class BackpressurePolicy : uint8_t {
    kDropOldest = 0,  // Drop oldest entries when queue full
    kDropNewest = 1,  // Drop new entries when queue full
    kBlock = 2        // Block producer (not recommended for real-time)
};

/**
 * @brief SEC-010: Queue statistics for monitoring
 */
struct TelemetryQueueStats {
    size_t current_depth = 0;
    size_t high_water_mark = 0;
    size_t max_depth = 0;
    uint64_t total_enqueued = 0;
    uint64_t total_dropped = 0;
    bool backpressure_active = false;
};

/**
 * @brief Configuration for telemetry writer
 *
 * SEC-010: Added backpressure configuration
 */
struct TelemetryConfig {
    std::string log_dir = "logs";        ///< Log directory
    std::string session_prefix = "run";  ///< Session file prefix
    size_t max_file_size_mb = 100;       ///< Rotate after N MB
    size_t max_sessions = 10;            ///< Keep N sessions max
    bool enable_csv = true;              ///< Write CSV logs
    bool enable_json = true;             ///< Write JSON summary
    bool enable_console = false;         ///< Mirror to stdout

    // SEC-010: Backpressure configuration
    size_t max_queue_size = 1024;      ///< Max entries in queue (must match ring buffer)
    size_t queue_high_water_pct = 80;  ///< High-water mark as % of max
    BackpressurePolicy backpressure_policy = BackpressurePolicy::kDropOldest;

    // SEC-003: HMAC-SHA256 key for log signing (empty = disabled)
    std::string hmac_key = "";
};

/**
 * @brief Asynchronous telemetry writer
 */
class TelemetryWriter {
   public:
    TelemetryWriter() = default;
    ~TelemetryWriter();

    bool start(const TelemetryConfig& config = TelemetryConfig());
    void stop();
    bool is_running() const { return running_.load(std::memory_order_acquire); }

    void log_frame(const DetectionData& detection, const TrackData& track,
                   const ActuationData& actuation, const SystemHealthData& health);

    void log_event(TelemetryEventId event_id, TelemetrySeverity severity,
                   const std::string& message);

    std::string get_session_path() const { return session_csv_path_; }

    uint64_t get_entries_written() const {
        return entries_written_.load(std::memory_order_acquire);
    }

    size_t get_queue_depth() const { return ring_buffer_.size(); }

    bool is_backpressure_active() const {
        return backpressure_active_.load(std::memory_order_acquire);
    }

    TelemetryQueueStats get_queue_stats() const;

    uint64_t get_entries_dropped() const {
        return entries_dropped_.load(std::memory_order_acquire);
    }

   private:
    void writer_loop();
    void write_csv_header();
    void write_csv_entry(const CsvLogEntry& entry);
    void write_json_summary();
    void rotate_logs();
    static uint64_t get_timestamp_ns();
    bool enqueue_entry(const CsvLogEntry& entry);
    void update_high_water_mark(size_t current_depth);
    void check_backpressure_state(size_t current_depth);

    TelemetryConfig config_;
    size_t queue_high_water_ = 800;

    std::thread writer_thread_;
    std::atomic<bool> running_{false};

    // SEC-010: Lock-free SPSC ring buffer for async logging
    LockFreeRingBuffer<CsvLogEntry, 1024> ring_buffer_;
    mutable std::mutex producer_mutex_;

    std::string session_csv_path_;
    std::ofstream csv_file_;
    uint32_t session_id_ = 0;
    uint64_t start_time_ns_ = 0;

    std::atomic<uint64_t> entries_written_{0};
    std::atomic<uint64_t> entries_dropped_{0};
    std::atomic<uint64_t> entries_enqueued_{0};
    std::atomic<size_t> high_water_mark_{0};
    std::atomic<bool> backpressure_active_{false};
    uint64_t first_frame_time_ns_ = 0;
    uint64_t last_frame_time_ns_ = 0;
    uint64_t frame_count_ = 0;
};

}  // namespace aurore
