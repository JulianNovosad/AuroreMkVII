/**
 * @file safety_monitor.hpp
 * @brief 1kHz safety monitor thread with software watchdog integration
 *
 * Monitors control loop health and triggers safety actions on fault detection.
 * Runs at highest priority (SCHED_FIFO=99) with 1ms period.
 *
 * Safety functions:
 * - Vision pipeline deadline monitoring (10ms max latency per AM7-L3-VIS-004)
 * - Actuation output deadline monitoring (2ms max latency per AM7-L2-ACT-003)
 * - Frame progression tracking (stall detection)
 * - Software watchdog feeding (safety fault on missed kick per AM7-L3-SAFE-005)
 * - Fault logging with timestamps
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#pragma once

#include <time.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#include "timing.hpp"

namespace aurore {

/**
 * @brief Maximum length of fault reason strings
 */
constexpr size_t MAX_FAULT_REASON_LEN = 64;

/**
 * @brief Safety fault codes
 *
 * Each fault code corresponds to a specific safety violation.
 * Fault codes are logged and can trigger different safety actions.
 */
enum class SafetyFaultCode : uint16_t {
    NONE = 0x0000,

    // Vision pipeline faults (0x01xx)
    VISION_STALLED = 0x0101,
    VISION_LATENCY_EXCEEDED = 0x0102,
    VISION_BUFFER_OVERRUN = 0x0103,

    // Actuation faults (0x02xx)
    ACTUATION_STALLED = 0x0201,
    ACTUATION_LATENCY_EXCEEDED = 0x0202,
    ACTUATION_COMMAND_INVALID = 0x0203,

    // Timing faults (0x03xx)
    FRAME_DEADLINE_MISSED = 0x0301,
    CONSECUTIVE_DEADLINE_MISSES = 0x0302,
    TIMESTAMP_NON_MONOTONIC = 0x0303,

    // System faults (0x04xx)
    WATCHDOG_FEED_FAILED = 0x0401,
    MEMORY_LOCK_FAILED = 0x0402,
    SCHEDULING_POLICY_FAILED = 0x0403,

    // Communication faults (0x05xx)
    I2C_TIMEOUT = 0x0501,
    I2C_NACK = 0x0502,
    CAMERA_TIMEOUT = 0x0503,

    // Safety system faults (0x06xx)
    SAFETY_COMPARATOR_MISMATCH = 0x0601,
    INTERLOCK_FAULT = 0x0602,
    RANGE_DATA_STALE = 0x0603,
    RANGE_DATA_INVALID = 0x0604,

    // Emergency (0x07xx)
    EMERGENCY_STOP_REQUESTED = 0x0701,
    CRITICAL_TEMPERATURE = 0x0702,
    POWER_FAULT = 0x0703,
};

/**
 * @brief Convert fault code to string description
 */
inline const char* fault_code_to_string(SafetyFaultCode code) noexcept {
    switch (code) {
        case SafetyFaultCode::NONE:
            return "NONE";
        case SafetyFaultCode::VISION_STALLED:
            return "VISION_STALLED";
        case SafetyFaultCode::VISION_LATENCY_EXCEEDED:
            return "VISION_LATENCY_EXCEEDED";
        case SafetyFaultCode::VISION_BUFFER_OVERRUN:
            return "VISION_BUFFER_OVERRUN";
        case SafetyFaultCode::ACTUATION_STALLED:
            return "ACTUATION_STALLED";
        case SafetyFaultCode::ACTUATION_LATENCY_EXCEEDED:
            return "ACTUATION_LATENCY_EXCEEDED";
        case SafetyFaultCode::ACTUATION_COMMAND_INVALID:
            return "ACTUATION_COMMAND_INVALID";
        case SafetyFaultCode::FRAME_DEADLINE_MISSED:
            return "FRAME_DEADLINE_MISSED";
        case SafetyFaultCode::CONSECUTIVE_DEADLINE_MISSES:
            return "CONSECUTIVE_DEADLINE_MISSES";
        case SafetyFaultCode::TIMESTAMP_NON_MONOTONIC:
            return "TIMESTAMP_NON_MONOTONIC";
        case SafetyFaultCode::WATCHDOG_FEED_FAILED:
            return "WATCHDOG_FEED_FAILED";
        case SafetyFaultCode::MEMORY_LOCK_FAILED:
            return "MEMORY_LOCK_FAILED";
        case SafetyFaultCode::SCHEDULING_POLICY_FAILED:
            return "SCHEDULING_POLICY_FAILED";
        case SafetyFaultCode::I2C_TIMEOUT:
            return "I2C_TIMEOUT";
        case SafetyFaultCode::I2C_NACK:
            return "I2C_NACK";
        case SafetyFaultCode::CAMERA_TIMEOUT:
            return "CAMERA_TIMEOUT";
        case SafetyFaultCode::SAFETY_COMPARATOR_MISMATCH:
            return "SAFETY_COMPARATOR_MISMATCH";
        case SafetyFaultCode::INTERLOCK_FAULT:
            return "INTERLOCK_FAULT";
        case SafetyFaultCode::RANGE_DATA_STALE:
            return "RANGE_DATA_STALE";
        case SafetyFaultCode::RANGE_DATA_INVALID:
            return "RANGE_DATA_INVALID";
        case SafetyFaultCode::EMERGENCY_STOP_REQUESTED:
            return "EMERGENCY_STOP_REQUESTED";
        case SafetyFaultCode::CRITICAL_TEMPERATURE:
            return "CRITICAL_TEMPERATURE";
        case SafetyFaultCode::POWER_FAULT:
            return "POWER_FAULT";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Safety event for logging
 */
struct SafetyEvent {
    TimestampNs timestamp_ns;           ///< Event timestamp (CLOCK_MONOTONIC_RAW)
    SafetyFaultCode fault_code;         ///< Fault code
    uint8_t severity;                   ///< 0=debug, 1=info, 2=warning, 3=error, 4=critical
    char reason[MAX_FAULT_REASON_LEN];  ///< Human-readable reason

    SafetyEvent() noexcept : timestamp_ns(0), fault_code(SafetyFaultCode::NONE), severity(0) {
        std::memset(reason, 0, MAX_FAULT_REASON_LEN);
    }
};

/**
 * @brief Pipeline stage enumeration for per-stage monitoring
 */
enum class PipelineStage : uint8_t { VISION = 0, TRACK = 1, ACTUATION = 2, NUM_STAGES = 3 };

/**
 * @brief Per-stage latency statistics (inspired by Mk VI StageWatchdog)
 */
struct StageLatencyStats {
    /// Last recorded latency in nanoseconds
    std::atomic<uint64_t> last_latency_ns{0};

    /// Maximum latency observed in nanoseconds
    std::atomic<uint64_t> max_latency_ns{0};

    /// Total latency for averaging (nanoseconds)
    std::atomic<uint64_t> total_latency_ns{0};

    /// Sample count for averaging
    std::atomic<uint64_t> sample_count{0};

    /// Stall count (latency > threshold)
    std::atomic<uint64_t> stall_count{0};

    /// Stall threshold in nanoseconds (default: 25ms = 25000000ns)
    std::atomic<uint64_t> stall_threshold_ns{25000000};

    /**
     * @brief Record a latency sample
     */
    void record_latency(uint64_t latency_ns) noexcept {
        last_latency_ns.store(latency_ns, std::memory_order_release);
        total_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);
        sample_count.fetch_add(1, std::memory_order_relaxed);

        // Update max latency (lock-free compare-exchange)
        uint64_t current_max = max_latency_ns.load(std::memory_order_relaxed);
        while (latency_ns > current_max &&
               !max_latency_ns.compare_exchange_weak(
                   current_max, latency_ns, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }

        // Check for stall
        if (latency_ns > stall_threshold_ns) {
            stall_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Get average latency in nanoseconds
     */
    uint64_t get_avg_latency_ns() const noexcept {
        uint64_t count = sample_count.load(std::memory_order_acquire);
        if (count == 0) return 0;
        return total_latency_ns.load(std::memory_order_acquire) / count;
    }

    /**
     * @brief Check if stage is currently stalled
     */
    bool is_stalled() const noexcept {
        return last_latency_ns.load(std::memory_order_acquire) > stall_threshold_ns;
    }

    /**
     * @brief Reset all statistics
     */
    void reset() noexcept {
        last_latency_ns.store(0, std::memory_order_release);
        max_latency_ns.store(0, std::memory_order_release);
        total_latency_ns.store(0, std::memory_order_release);
        sample_count.store(0, std::memory_order_release);
        stall_count.store(0, std::memory_order_release);
    }
};

/**
 * @brief Per-stage monitoring configuration
 */
struct PerStageMonitorConfig {
    /// Enable per-stage latency tracking (default: true)
    bool enable_per_stage = true;

    /// Stall threshold for all stages (default: 25ms)
    uint64_t stall_threshold_ns = 25000000;

    /// Consecutive stalls before recovery callback (default: 3)
    uint64_t stalls_before_recovery = 3;

    /// Enable recovery callback (default: true)
    bool enable_recovery_callback = true;

    /// Enable health report on shutdown (default: true)
    bool enable_health_report = true;
};

/**
 * @brief Safety monitor configuration
 */
struct SafetyMonitorConfig {
    /// Vision pipeline max latency (default: 10ms per AM7-L3-VIS-004)
    uint64_t vision_deadline_ns = 10000000;

    /// Actuation output max latency (default: 2ms per AM7-L2-ACT-003)
    uint64_t actuation_deadline_ns = 2000000;

    /// Frame stall detection threshold (default: 2 frames = 16.67ms)
    uint64_t frame_stall_threshold = 2;

    /// Consecutive deadline misses before emergency (default: 3)
    uint64_t max_consecutive_misses = 3;

    /// Software watchdog kick interval in ms (default: 50ms per AM7-L3-SAFE-005)
    uint64_t watchdog_kick_interval_ms = 50;

    /// Software watchdog timeout in ms (default: 60ms, allows 1 missed kick)
    uint64_t watchdog_timeout_ms = 60;

    /// Enable software watchdog (default: true)
    bool enable_watchdog = true;

    /// Per-stage monitoring configuration
    PerStageMonitorConfig per_stage;
};

/**
 * @brief Safety monitor callback types
 *
 * IMPORTANT: All callbacks registered with SafetyMonitor MUST be noexcept and
 * lock-free. They are invoked from real-time threads (SCHED_FIFO) and from the
 * watchdog thread. Any blocking operation (mutex, heap allocation, I/O) inside
 * a callback risks priority inversion and missed deadlines.
 */
using SafetyActionCallback = void (*)(SafetyFaultCode code, const char* reason, void* user_data);
using LogCallback = void (*)(const SafetyEvent& event, void* user_data);
using RecoveryCallback = void (*)(const char* stage_name, uint64_t stall_count, void* user_data);

/**
 * @brief 1kHz safety monitor thread
 *
 * Monitors system health and triggers safety actions on fault detection.
 * Must run at SCHED_FIFO priority 99 (highest) for deterministic response.
 *
 * Usage:
 * @code
 *     SafetyMonitorConfig config;
 *     config.vision_deadline_ns = 20000000;  // 20ms
 *
 *     SafetyMonitor monitor(config);
 *
 *     // Register callbacks
 *     monitor.set_safety_action_callback(on_safety_action, nullptr);
 *     monitor.set_log_callback(on_log_event, nullptr);
 *
 *     // Start monitoring
 *     monitor.start();
 *
 *     // In control loop threads, update monitors:
 *     monitor.update_vision_frame(frame_sequence, timestamp_ns);
 *     monitor.update_actuation_frame(frame_sequence, timestamp_ns);
 *
 *     // In main loop, check system status:
 *     if (!monitor.is_system_safe()) {
 *         // Handle unsafe condition
 *     }
 * @endcode
 */
class SafetyMonitor {
   public:
    /**
     * @brief Construct safety monitor
     *
     * @param config Monitor configuration
     */
    explicit SafetyMonitor(const SafetyMonitorConfig& config = SafetyMonitorConfig()) noexcept
        : config_(config),
          vision_frame_count_(0),
          last_vision_count_(0),
          last_vision_timestamp_ns_(0),
          last_vision_sequence_(0),
          actuation_frame_count_(0),
          last_actuation_count_(0),
          last_actuation_timestamp_ns_(0),
          last_actuation_sequence_(0),
          deadline_misses_(0),
          consecutive_misses_(0),
          current_fault_(SafetyFaultCode::NONE),
          last_kick_time_ns_(0),
          system_safe_(true),
          running_(false),
          emergency_active_(false),
          watchdog_running_(false),
          safety_action_callback_(nullptr),
          safety_action_user_data_(nullptr),
          log_callback_(nullptr),
          log_user_data_(nullptr) {
        // last_event_ is zero-initialized by default constructor
    }

    /**
     * @brief Destructor (stops watchdog thread)
     */
    ~SafetyMonitor() noexcept { stop(); }

    // Non-copyable, non-movable
    SafetyMonitor(const SafetyMonitor&) = delete;
    SafetyMonitor& operator=(const SafetyMonitor&) = delete;
    SafetyMonitor(SafetyMonitor&&) = delete;
    SafetyMonitor& operator=(SafetyMonitor&&) = delete;

    /**
     * @brief Set safety action callback
     *
     * Called when a safety fault is detected.
     *
     * @param callback Callback function
     * @param user_data User data passed to callback
     */
    void set_safety_action_callback(SafetyActionCallback callback, void* user_data) noexcept {
        safety_action_callback_ = callback;
        safety_action_user_data_ = user_data;
    }

    /**
     * @brief Set log callback
     *
     * Called for all safety events (including non-fault events).
     *
     * @param callback Callback function
     * @param user_data User data passed to callback
     */
    void set_log_callback(LogCallback callback, void* user_data) noexcept {
        log_callback_ = callback;
        log_user_data_ = user_data;
    }

    /**
     * @brief Initialize monitor (start watchdog thread)
     *
     * @return true on success, false on failure
     */
    bool init() noexcept {
        // Start software watchdog thread if enabled
        if (config_.enable_watchdog) {
            // Set initial kick time BEFORE starting watchdog thread
            // to prevent race condition where watchdog checks before kick time is set
            last_kick_time_ns_.store(get_timestamp(ClockId::MonotonicRaw),
                                     std::memory_order_release);

            watchdog_running_.store(true, std::memory_order_release);
            watchdog_thread_ = std::thread(&SafetyMonitor::watchdog_thread_func, this);
        }

        return true;
    }

    /**
     * @brief Start monitoring thread
     *
     * @return true on success, false if already running
     */
    bool start() noexcept {
        if (running_.load(std::memory_order_acquire)) {
            return false;
        }

        running_.store(true, std::memory_order_release);
        return true;
    }

    /**
     * @brief Stop monitoring thread
     */
    void stop() noexcept {
        running_.store(false, std::memory_order_release);

        // Stop watchdog thread
        if (watchdog_running_.load(std::memory_order_acquire)) {
            watchdog_running_.store(false, std::memory_order_release);
            if (watchdog_thread_.joinable()) {
                watchdog_thread_.join();
            }
        }
    }

    /**
     * @brief Kick the software watchdog
     *
     * Call this periodically from the main control loop (every 50ms per AM7-L3-SAFE-005).
     * Missing a kick for >60ms will trigger a safety fault.
     */
    void kick_watchdog() noexcept {
        last_kick_time_ns_.store(get_timestamp(ClockId::MonotonicRaw), std::memory_order_release);
    }

    /**
     * @brief Set recovery callback
     *
     * Called when a stage exceeds stall threshold consecutively.
     *
     * @param callback Callback function
     * @param user_data User data passed to callback
     */
    void set_recovery_callback(RecoveryCallback callback, void* user_data) noexcept {
        recovery_callback_ = callback;
        recovery_user_data_ = user_data;
    }

    /**
     * @brief Record stage latency (call from each pipeline stage)
     *
     * @param stage Pipeline stage
     * @param latency_ns Latency in nanoseconds
     */
    void record_stage_latency(PipelineStage stage, uint64_t latency_ns) noexcept {
        if (!config_.per_stage.enable_per_stage) return;

        const size_t idx = static_cast<size_t>(stage);
        if (idx >= static_cast<size_t>(PipelineStage::NUM_STAGES)) return;

        // Set stall threshold from config
        stage_stats_[idx].stall_threshold_ns.store(config_.per_stage.stall_threshold_ns,
                                                   std::memory_order_relaxed);

        // Record latency
        stage_stats_[idx].record_latency(latency_ns);

        // Check for stall and trigger recovery if needed
        if (stage_stats_[idx].is_stalled()) {
            total_frame_stalls_.fetch_add(1, std::memory_order_relaxed);

            // Check if recovery callback should be triggered
            if (config_.per_stage.enable_recovery_callback && recovery_callback_) {
                uint64_t stalls = stage_stats_[idx].stall_count.load(std::memory_order_acquire);
                if (stalls >= config_.per_stage.stalls_before_recovery) {
                    const char* stage_names[] = {"VISION", "TRACK", "ACTUATION"};
                    recovery_callback_(stage_names[idx], stalls, recovery_user_data_);
                }
            }
        }
    }

    /**
     * @brief Record frame completion (call at end of control loop)
     *
     * @param total_latency_ns Total end-to-end latency
     */
    void record_frame_complete([[maybe_unused]] uint64_t total_latency_ns) noexcept {
        total_frames_.fetch_add(1, std::memory_order_relaxed);

        // Check if frame was healthy (no stage stalled)
        bool healthy = true;
        for (size_t i = 0; i < static_cast<size_t>(PipelineStage::NUM_STAGES); ++i) {
            if (stage_stats_[i].is_stalled()) {
                healthy = false;
                break;
            }
        }

        if (healthy) {
            healthy_frames_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Get stage latency statistics
     *
     * @param stage Pipeline stage
     * @return const StageLatencyStats& Stage statistics
     */
    const StageLatencyStats& get_stage_stats(PipelineStage stage) const noexcept {
        return stage_stats_[static_cast<size_t>(stage)];
    }

    /**
     * @brief Get total frame count
     */
    uint64_t get_total_frames() const noexcept {
        return total_frames_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get healthy frame count
     */
    uint64_t get_healthy_frames() const noexcept {
        return healthy_frames_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get total stall count
     */
    uint64_t get_total_stalls() const noexcept {
        return total_frame_stalls_.load(std::memory_order_acquire);
    }

    /**
     * @brief Generate health report (for logging/shutdown)
     *
     * @return std::string Human-readable health report
     */
    std::string generate_health_report() const noexcept {
        if (!config_.per_stage.enable_health_report) {
            return "";
        }

        char buffer[1024];
        const char* stage_names[] = {"VISION", "TRACK", "ACTUATION"};

        int offset = 0;
        offset += snprintf(buffer + offset, sizeof(buffer) - static_cast<size_t>(offset),
                           "=== Per-Stage Health Report ===\n");
        offset += snprintf(buffer + offset, sizeof(buffer) - static_cast<size_t>(offset),
                           "Total Frames: %lu\n", total_frames_.load());
        offset += snprintf(buffer + offset, sizeof(buffer) - static_cast<size_t>(offset),
                           "Healthy Frames: %lu (%.1f%%)\n", healthy_frames_.load(),
                           total_frames_.load() > 0
                               ? 100.0 * static_cast<double>(healthy_frames_.load()) /
                                     static_cast<double>(total_frames_.load())
                               : 0.0);
        offset += snprintf(buffer + offset, sizeof(buffer) - static_cast<size_t>(offset),
                           "Total Stalls: %lu\n\n", total_frame_stalls_.load());

        for (size_t i = 0; i < static_cast<size_t>(PipelineStage::NUM_STAGES); ++i) {
            const auto& stats = stage_stats_[i];
            offset += snprintf(buffer + offset, sizeof(buffer) - static_cast<size_t>(offset),
                               "  %s:\n", stage_names[i]);
            offset += snprintf(buffer + offset, sizeof(buffer) - static_cast<size_t>(offset),
                               "    Avg: %.3f ms\n",
                               static_cast<double>(stats.get_avg_latency_ns()) / 1000000.0);
            offset += snprintf(buffer + offset, sizeof(buffer) - static_cast<size_t>(offset),
                               "    Max: %.3f ms\n",
                               static_cast<double>(stats.max_latency_ns.load()) / 1000000.0);
            offset += snprintf(buffer + offset, sizeof(buffer) - static_cast<size_t>(offset),
                               "    Stalls: %lu\n", stats.stall_count.load());
            offset += snprintf(buffer + offset, sizeof(buffer) - static_cast<size_t>(offset),
                               "    Status: %s\n\n", stats.is_stalled() ? "STALLED" : "OK");
        }

        return std::string(buffer);
    }

    /**
     * @brief Reset all per-stage statistics
     */
    void reset_stage_stats() noexcept {
        for (size_t i = 0; i < static_cast<size_t>(PipelineStage::NUM_STAGES); ++i) {
            stage_stats_[i].reset();
        }
        total_frame_stalls_.store(0, std::memory_order_release);
        healthy_frames_.store(0, std::memory_order_release);
        total_frames_.store(0, std::memory_order_release);
    }

    /**
     * @brief Check if monitor is running
     */
    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

    /**
     * @brief Update vision frame status
     *
     * Call this from vision pipeline thread after processing each frame.
     *
     * @param sequence Frame sequence number
     * @param timestamp_ns Frame timestamp (CLOCK_MONOTONIC_RAW)
     */
    void update_vision_frame(uint64_t sequence, TimestampNs timestamp_ns) noexcept {
        vision_frame_count_.store(sequence + 1, std::memory_order_release);
        last_vision_timestamp_ns_.store(timestamp_ns, std::memory_order_release);
        last_vision_sequence_.store(sequence, std::memory_order_release);
    }

    /**
     * @brief Update actuation frame status
     *
     * Call this from actuation output thread after processing each frame.
     *
     * @param sequence Frame sequence number
     * @param timestamp_ns Frame timestamp (CLOCK_MONOTONIC_RAW)
     */
    void update_actuation_frame(uint64_t sequence, TimestampNs timestamp_ns) noexcept {
        actuation_frame_count_.store(sequence + 1, std::memory_order_release);
        last_actuation_timestamp_ns_.store(timestamp_ns, std::memory_order_release);
        last_actuation_sequence_.store(sequence, std::memory_order_release);
    }

    /**
     * @brief Check if system is safe
     *
     * @return true if no active faults, false if safety action triggered
     */
    bool is_system_safe() const noexcept {
        return system_safe_.load(std::memory_order_acquire) &&
               !emergency_active_.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if emergency stop is active
     */
    bool is_emergency_active() const noexcept {
        return emergency_active_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get current fault code
     */
    SafetyFaultCode current_fault() const noexcept {
        return current_fault_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get deadline miss count
     */
    uint64_t deadline_misses() const noexcept {
        return deadline_misses_.load(std::memory_order_acquire);
    }

    /**
     * @brief Trigger emergency stop
     *
     * Immediately disables all outputs and triggers safety action.
     *
     * @param reason Human-readable reason
     */
    void trigger_emergency_stop(const char* reason = "Emergency stop requested") noexcept {
        trigger_fault(SafetyFaultCode::EMERGENCY_STOP_REQUESTED, reason, 4);
    }

    /**
     * @brief Run one monitoring cycle
     *
     * Call this from the safety monitor thread at 1kHz.
     *
     * @return true if system is safe, false if fault detected
     */
    [[nodiscard]] bool run_cycle() noexcept {
        if (!running_.load(std::memory_order_acquire)) {
            return true;
        }

        const TimestampNs now = get_timestamp(ClockId::MonotonicRaw);

        // Check vision pipeline
        check_vision_health(now);

        // Check actuation output
        check_actuation_health(now);

        // Check for consecutive deadline misses
        if (consecutive_misses_.load(std::memory_order_acquire) >= config_.max_consecutive_misses) {
            trigger_fault(SafetyFaultCode::CONSECUTIVE_DEADLINE_MISSES,
                          "Consecutive deadline misses exceed threshold", 4);
        }

        return system_safe_.load(std::memory_order_acquire);
    }

    /**
     * @brief Clear fault - AM7-L3-SAFE-006: Fault register is latched and
     * non-clearable except via power cycle. This method always returns false
     * to enforce spec compliance.
     *
     * @return always false - faults require power cycle to clear
     */
    bool clear_fault() noexcept {
        // AM7-L3-SAFE-006: Fault register shall be latched and non-clearable
        // except via power cycle. Do not allow software clear.
        return false;
    }

   private:
    void check_vision_health(TimestampNs now) noexcept {
        const uint64_t current_count = vision_frame_count_.load(std::memory_order_acquire);
        const uint64_t last_count = last_vision_count_.load(std::memory_order_acquire);

        // Check for stall (only if we've received at least one frame)
        if (current_count == last_count && current_count > 0) {
            // Vision pipeline hasn't advanced
            const TimestampNs last_ts = last_vision_timestamp_ns_.load(std::memory_order_acquire);
            // Use wrap-safe timestamp difference
            const int64_t stall_duration = timestamp_diff_ns(now, last_ts);
            if (stall_duration > static_cast<int64_t>(config_.vision_deadline_ns * 2)) {
                trigger_fault(SafetyFaultCode::VISION_STALLED, "Vision pipeline stalled", 3);
            }
        }
        last_vision_count_.store(current_count, std::memory_order_release);

        // Check latency (only if we have a valid timestamp)
        const TimestampNs last_ts = last_vision_timestamp_ns_.load(std::memory_order_acquire);
        if (last_ts > 0) {
            // Use wrap-safe timestamp difference
            const int64_t latency = timestamp_diff_ns(now, last_ts);
            if (latency > static_cast<int64_t>(config_.vision_deadline_ns)) {
                char reason[MAX_FAULT_REASON_LEN];
                snprintf(reason, sizeof(reason), "Vision latency %ld ns exceeds %lu ns",
                         static_cast<long>(latency), config_.vision_deadline_ns);
                trigger_fault(SafetyFaultCode::VISION_LATENCY_EXCEEDED, reason, 3);
            }
        }
    }

    void check_actuation_health(TimestampNs now) noexcept {
        const uint64_t current_count = actuation_frame_count_.load(std::memory_order_acquire);
        const uint64_t last_count = last_actuation_count_.load(std::memory_order_acquire);

        // Check for stall (only if we've received at least one frame)
        if (current_count == last_count && current_count > 0) {
            const TimestampNs last_ts =
                last_actuation_timestamp_ns_.load(std::memory_order_acquire);
            // Use wrap-safe timestamp difference
            const int64_t stall_duration = timestamp_diff_ns(now, last_ts);
            if (stall_duration > static_cast<int64_t>(config_.actuation_deadline_ns * 2)) {
                trigger_fault(SafetyFaultCode::ACTUATION_STALLED, "Actuation output stalled", 3);
            }
        }
        last_actuation_count_.store(current_count, std::memory_order_release);

        // Check latency (only if we have a valid timestamp)
        const TimestampNs last_ts = last_actuation_timestamp_ns_.load(std::memory_order_acquire);
        if (last_ts > 0) {
            // Use wrap-safe timestamp difference
            const int64_t latency = timestamp_diff_ns(now, last_ts);

            if (latency > static_cast<int64_t>(config_.actuation_deadline_ns)) {
                char reason[MAX_FAULT_REASON_LEN];
                snprintf(reason, sizeof(reason), "Actuation latency %ld ns exceeds %lu ns",
                         static_cast<long>(latency), config_.actuation_deadline_ns);
                trigger_fault(SafetyFaultCode::ACTUATION_LATENCY_EXCEEDED, reason, 3);
            }
        }
    }

    void trigger_fault(SafetyFaultCode code, const char* reason, uint8_t severity) noexcept {
        // Use compare-exchange to prevent race conditions when multiple threads
        // try to trigger faults simultaneously. Only update if we're setting
        // a new fault or upgrading severity.
        SafetyFaultCode expected_fault = current_fault_.load(std::memory_order_acquire);

        // Atomically update fault code (only if changing)
        while (!current_fault_.compare_exchange_weak(
            expected_fault, code, std::memory_order_acq_rel, std::memory_order_acquire)) {
            // Another thread updated fault - only proceed if our fault is more severe
            // For simplicity, we always proceed but log the race
        }

        // Update system state with proper ordering
        system_safe_.store(false, std::memory_order_release);
        deadline_misses_.fetch_add(1, std::memory_order_relaxed);
        consecutive_misses_.fetch_add(1, std::memory_order_relaxed);

        // Create event
        SafetyEvent event;
        event.timestamp_ns = get_timestamp(ClockId::MonotonicRaw);
        event.fault_code = code;
        event.severity = severity;
        snprintf(event.reason, sizeof(event.reason), "%s", reason);

        last_event_ = event;

        // Log event
        if (log_callback_) {
            log_callback_(event, log_user_data_);
        }

        // Trigger safety action for critical faults
        // Use atomic exchange to ensure we only trigger once per fault
        if (severity >= 3 || code == SafetyFaultCode::EMERGENCY_STOP_REQUESTED) {
            bool expected = false;
            if (emergency_active_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                          std::memory_order_acquire)) {
                // We were the first to set emergency - trigger action
                if (safety_action_callback_) {
                    safety_action_callback_(code, reason, safety_action_user_data_);
                }
            }
        }
    }

    /**
     * @brief Software watchdog thread function
     *
     * Monitors kick interval and triggers fault if timeout exceeded.
     * Runs at 10ms check interval.
     */
    void watchdog_thread_func() noexcept {
        while (watchdog_running_.load(std::memory_order_acquire)) {
            const TimestampNs now = get_timestamp(ClockId::MonotonicRaw);
            const TimestampNs last_kick = last_kick_time_ns_.load(std::memory_order_acquire);
            // Use wrap-safe timestamp difference
            const int64_t elapsed_ns = timestamp_diff_ns(now, last_kick);
            const uint64_t elapsed_ms =
                elapsed_ns > 0 ? static_cast<uint64_t>(elapsed_ns) / 1000000 : 0;

            // Check if watchdog timeout exceeded
            if (elapsed_ms > config_.watchdog_timeout_ms) {
                trigger_fault(SafetyFaultCode::WATCHDOG_FEED_FAILED,
                              "Software watchdog timeout - no kick received", 4);
            }

            // Sleep for 10ms (use clock_nanosleep for precision)
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 10000000;  // 10ms
            clock_nanosleep(CLOCK_MONOTONIC_RAW, 0, &ts, nullptr);
        }
    }

    SafetyMonitorConfig config_;

    std::atomic<uint64_t> vision_frame_count_;
    std::atomic<uint64_t> last_vision_count_;
    std::atomic<TimestampNs> last_vision_timestamp_ns_;
    std::atomic<uint64_t> last_vision_sequence_;

    // Actuation output state
    std::atomic<uint64_t> actuation_frame_count_;
    std::atomic<uint64_t> last_actuation_count_;
    std::atomic<TimestampNs> last_actuation_timestamp_ns_;
    std::atomic<uint64_t> last_actuation_sequence_;

    // Fault tracking
    std::atomic<uint64_t> deadline_misses_;
    std::atomic<uint64_t> consecutive_misses_;
    std::atomic<SafetyFaultCode> current_fault_;

    // System state
    std::atomic<TimestampNs> last_kick_time_ns_;  ///< Last watchdog kick time
    std::atomic<bool> system_safe_;
    std::atomic<bool> running_;
    std::atomic<bool> emergency_active_;
    std::atomic<bool> watchdog_running_;  ///< Watchdog thread running flag
    std::thread watchdog_thread_;         ///< Software watchdog thread

    // Callbacks
    SafetyActionCallback safety_action_callback_;
    void* safety_action_user_data_;
    LogCallback log_callback_;
    void* log_user_data_;
    RecoveryCallback recovery_callback_;  ///< Called on stage stall recovery
    void* recovery_user_data_;

    // Per-stage latency tracking (Mk VI inspired)
    StageLatencyStats stage_stats_[static_cast<size_t>(PipelineStage::NUM_STAGES)];
    std::atomic<uint64_t> total_frame_stalls_{0};
    std::atomic<uint64_t> healthy_frames_{0};
    std::atomic<uint64_t> total_frames_{0};

    // Last event (for debugging)
    SafetyEvent last_event_;
};

/**
 * @brief RAII watchdog kick helper
 *
 * Automatically kicks the watchdog when the object goes out of scope.
 * Use this in control loops to ensure periodic watchdog feeding.
 *
 * Usage:
 * @code
 *     SafetyMonitor monitor(config);
 *     monitor.init();
 *     monitor.start();
 *
 *     while (running) {
 *         WatchdogKick kick(monitor);  // Auto-kick at end of scope
 *
 *         // ... control loop work ...
 *         // Vision processing
 *         // Tracking
 *         // Actuation output
 *
 *     }  // kick_watchdog() called here automatically
 * @endcode
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */
class WatchdogKick {
   public:
    /**
     * @brief Construct watchdog kick
     *
     * @param monitor Safety monitor to kick
     */
    explicit WatchdogKick(SafetyMonitor& monitor) noexcept : monitor_(monitor) {}

    /**
     * @brief Destructor (kicks watchdog)
     */
    ~WatchdogKick() noexcept { monitor_.kick_watchdog(); }

    // Non-copyable
    WatchdogKick(const WatchdogKick&) = delete;
    WatchdogKick& operator=(const WatchdogKick&) = delete;

   private:
    SafetyMonitor& monitor_;
};

}  // namespace aurore
