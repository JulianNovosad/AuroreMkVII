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
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>

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
    size_t max_queue_size = 100;       ///< Max entries in queue
    size_t queue_high_water_pct = 80;  ///< High-water mark as % of max
    BackpressurePolicy backpressure_policy = BackpressurePolicy::kDropOldest;

    // SEC-003: HMAC-SHA256 key for log signing (empty = disabled)
    std::string hmac_key = "";
};

/**
 * @brief Asynchronous telemetry writer
 *
 * Usage:
 * @code
 *     TelemetryWriter telemetry;
 *     telemetry.start();
 *
 *     // In control loop (non-blocking):
 *     telemetry.log_frame(detection, track, actuation);
 *
 *     // On shutdown:
 *     telemetry.stop();
 * @endcode
 *
 * SEC-010: Implements backpressure to prevent queue overflow DoS
 */
class TelemetryWriter {
   public:
    TelemetryWriter() = default;
    ~TelemetryWriter();

    /**
     * @brief Start telemetry writer
     * @param config Configuration options
     * @return true on success
     */
    bool start(const TelemetryConfig& config = TelemetryConfig());

    /**
     * @brief Stop telemetry writer
     */
    void stop();

    /**
     * @brief Check if writer is running
     */
    bool is_running() const { return running_.load(std::memory_order_acquire); }

    /**
     * @brief Log a complete frame (detection + track + actuation + health)
     *
     * Thread-safe, non-blocking (queues entry for async write)
     * SEC-010: Implements backpressure when queue is full
     */
    void log_frame(const DetectionData& detection, const TrackData& track,
                   const ActuationData& actuation, const SystemHealthData& health);

    /**
     * @brief Log a telemetry event
     */
    void log_event(TelemetryEventId event_id, TelemetrySeverity severity,
                   const std::string& message);

    /**
     * @brief Get current session file path
     */
    std::string get_session_path() const { return session_csv_path_; }

    /**
     * @brief Get entries written count
     */
    uint64_t get_entries_written() const {
        return entries_written_.load(std::memory_order_acquire);
    }

    // SEC-010: Backpressure monitoring
    /**
     * @brief Get current queue depth
     */
    size_t get_queue_depth() const { return queue_depth_.load(std::memory_order_acquire); }

    /**
     * @brief Check if backpressure is currently active
     */
    bool is_backpressure_active() const {
        return backpressure_active_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get queue statistics
     */
    TelemetryQueueStats get_queue_stats() const;

    /**
     * @brief Get entries dropped due to backpressure
     */
    uint64_t get_entries_dropped() const {
        return entries_dropped_.load(std::memory_order_acquire);
    }

   private:
    /**
     * @brief Writer thread main loop
     */
    void writer_loop();

    /**
     * @brief Write CSV header
     */
    void write_csv_header();

    /**
     * @brief Write a single CSV entry
     */
    void write_csv_entry(const CsvLogEntry& entry);

    /**
     * @brief Write JSON summary on stop
     */
    void write_json_summary();

    /**
     * @brief Rotate old log files
     */
    void rotate_logs();

    /**
     * @brief Get current timestamp in nanoseconds (CLOCK_MONOTONIC_RAW)
     */
    static uint64_t get_timestamp_ns();

    /**
     * @brief SEC-010: Enqueue entry with backpressure handling
     * @return true if entry was queued, false if dropped
     */
    bool enqueue_entry(const CsvLogEntry& entry);

    /**
     * @brief SEC-010: Update high-water mark
     */
    void update_high_water_mark(size_t current_depth);

    /**
     * @brief SEC-010: Check and update backpressure state
     */
    void check_backpressure_state(size_t current_depth);

    // Configuration
    TelemetryConfig config_;

    // SEC-010: Computed thresholds
    size_t queue_high_water_ = 80;  // 80% of max_queue_size

    // Writer thread
    std::thread writer_thread_;
    std::atomic<bool> running_{false};

    // SEC-010: Queue for async logging with explicit depth tracking
    std::queue<CsvLogEntry> entry_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // SEC-010: Atomic queue depth for lock-free monitoring
    std::atomic<size_t> queue_depth_{0};

    // SEC-010: Backpressure state
    std::atomic<bool> backpressure_active_{false};
    std::atomic<size_t> high_water_mark_{0};

    // Session tracking
    std::string session_csv_path_;
    std::ofstream csv_file_;
    uint32_t session_id_ = 0;
    uint64_t start_time_ns_ = 0;

    // Statistics
    std::atomic<uint64_t> entries_written_{0};
    std::atomic<uint64_t> entries_dropped_{0};
    std::atomic<uint64_t> entries_enqueued_{0};
    uint64_t first_frame_time_ns_ = 0;
    uint64_t last_frame_time_ns_ = 0;
    uint64_t frame_count_ = 0;
};

}  // namespace aurore
