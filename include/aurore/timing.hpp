/**
 * @file timing.hpp
 * @brief Real-time thread timing framework using clock_nanosleep
 * 
 * Provides precise periodic thread wakeup with absolute time references
 * to prevent timer drift. Designed for SCHED_FIFO real-time threads
 * on Linux with PREEMPT_RT kernel.
 * 
 * Key features:
 * - Absolute time sleep (TIMER_ABSTIME) prevents cumulative drift
 * - CLOCK_MONOTONIC for sleep (CLOCK_MONOTONIC_RAW not supported by nanosleep)
 * - CLOCK_MONOTONIC_RAW for timestamp capture (highest precision)
 * - Deadline miss detection and counting
 * - Phase offset support for pipelined execution
 * 
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <time.h>

namespace aurore {

/**
 * @brief Clock ID enumeration for type safety
 */
enum class ClockId {
    Monotonic = CLOCK_MONOTONIC,        ///< For sleep operations
    MonotonicRaw = CLOCK_MONOTONIC_RAW, ///< For timestamp capture (highest precision)
    Realtime = CLOCK_REALTIME,          ///< Wall-clock time (not recommended for RT)
    Boottime = CLOCK_BOOTTIME           ///< Includes suspend time
};

/**
 * @brief Convert ClockId to native clockid_t
 */
inline clockid_t to_clockid(ClockId id) noexcept {
    return static_cast<clockid_t>(id);
}

/**
 * @brief High-resolution timestamp in nanoseconds
 *
 * Uses CLOCK_MONOTONIC_RAW for maximum precision and immunity
 * to NTP adjustments.
 *
 * Note: CLOCK_MONOTONIC_RAW wraps after ~584 years (2^64 nanoseconds).
 * For wrap-safe comparisons, use timestamp_diff_ns() instead of direct
 * subtraction.
 */
using TimestampNs = uint64_t;

/**
 * @brief Maximum safe timestamp difference (half of uint64_t range)
 *
 * Differences larger than this may indicate timestamp wrap or error.
 */
constexpr TimestampNs MAX_SAFE_TIMESTAMP_DIFF_NS = (1ULL << 63);

/**
 * @brief Calculate difference between two timestamps (wrap-safe)
 *
 * Uses signed arithmetic to handle timestamp wrap correctly.
 * Returns negative value if after < before.
 *
 * @param after Later timestamp
 * @param before Earlier timestamp
 * @return int64_t Difference in nanoseconds (negative if after < before)
 *
 * This function correctly handles wrap-around:
 * - If after = 0xFFFFFFFFFFFFFFFF and before = 0, returns -1
 * - If after = 0 and before = 0xFFFFFFFFFFFFFFFF, returns +1
 */
inline int64_t timestamp_diff_ns(TimestampNs after, TimestampNs before) noexcept {
    return static_cast<int64_t>(after - before);
}

/**
 * @brief Check if timestamp_a is after timestamp_b (wrap-safe)
 *
 * @param a First timestamp
 * @param b Second timestamp
 * @return true if a is after b
 */
inline bool timestamp_is_after(TimestampNs a, TimestampNs b) noexcept {
    return static_cast<int64_t>(a - b) > 0;
}

/**
 * @brief Check if timestamp is within window of reference (wrap-safe)
 *
 * @param timestamp Timestamp to check
 * @param reference Reference timestamp
 * @param window_ns Window size in nanoseconds
 * @return true if |timestamp - reference| <= window_ns
 */
inline bool timestamp_within_window(TimestampNs timestamp, TimestampNs reference,
                                    uint64_t window_ns) noexcept {
    const int64_t diff = timestamp_diff_ns(timestamp, reference);
    return diff >= 0 ? static_cast<uint64_t>(diff) <= window_ns
                     : static_cast<uint64_t>(-diff) <= window_ns;
}

/**
 * @brief Get current timestamp from specified clock
 * 
 * @param clock Clock source (default: CLOCK_MONOTONIC_RAW)
 * @return TimestampNs Current time in nanoseconds since epoch
 * 
 * @throws std::system_error if clock_gettime fails
 */
inline TimestampNs get_timestamp(ClockId clock = ClockId::MonotonicRaw) {
    struct timespec ts;
    if (clock_gettime(to_clockid(clock), &ts) != 0) {
        throw std::system_error(errno, std::system_category(), 
                               "clock_gettime failed");
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000UL + 
           static_cast<uint64_t>(ts.tv_nsec);
}

/**
 * @brief Get current timestamp (noexcept version)
 * 
 * @param clock Clock source
 * @param error Output parameter for error code (0 on success)
 * @return TimestampNs Current time, or 0 on error
 */
inline TimestampNs get_timestamp_safe(ClockId clock, int& error) noexcept {
    struct timespec ts;
    error = clock_gettime(to_clockid(clock), &ts);
    if (error != 0) {
        return 0;
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000UL + 
           static_cast<uint64_t>(ts.tv_nsec);
}

/**
 * @brief Real-time thread timing controller
 * 
 * Manages periodic wakeup for real-time threads using absolute time
 * sleep to prevent drift. Supports phase offsets for pipelined
 * execution.
 * 
 * Usage:
 * @code
 *     // Initialize for 120Hz (8.333ms) with 2ms phase offset
 *     ThreadTiming timing(8333333, 2000000);
 *     
 *     while (running) {
 *         timing.wait();  // Sleep until next period
 *         
 *         if (timing.missed_deadline()) {
 *             // Handle deadline miss
 *         }
 *         
 *         // Process frame...
 *     }
 * @endcode
 */
class ThreadTiming {
public:
    /**
     * @brief Construct timing controller
     * 
     * @param period_ns Period in nanoseconds (e.g., 8333333 for 120Hz)
     * @param phase_ns Phase offset in nanoseconds (default: 0)
     * @param clock Clock source for sleep (default: CLOCK_MONOTONIC)
     * 
     * @throws std::system_error if clock initialization fails
     */
    explicit ThreadTiming(
        uint64_t period_ns,
        uint64_t phase_ns = 0,
        ClockId clock = ClockId::Monotonic
    ) : period_ns_(period_ns)
      , phase_ns_(phase_ns)
      , clock_(clock)
      , cycle_count_(0)
      , deadline_misses_(0)
      , consecutive_misses_(0)
      , initialized_(false)
      , last_expected_wakeup_(0)
      , last_actual_wakeup_(0) {
        
        init(period_ns, phase_ns);
    }
    
    /**
     * @brief Default constructor (requires manual init)
     */
    ThreadTiming() noexcept
        : period_ns_(0)
        , phase_ns_(0)
        , clock_(ClockId::Monotonic)
        , cycle_count_(0)
        , deadline_misses_(0)
        , consecutive_misses_(0)
        , initialized_(false) {}
    
    /**
     * @brief Initialize timing controller
     * 
     * @param period_ns Period in nanoseconds
     * @param phase_ns Phase offset in nanoseconds
     * 
     * @throws std::system_error if clock initialization fails
     */
    void init(uint64_t period_ns, uint64_t phase_ns = 0) {
        period_ns_ = period_ns;
        phase_ns_ = phase_ns;
        
        struct timespec now;
        if (clock_gettime(to_clockid(clock_), &now) != 0) {
            throw std::system_error(errno, std::system_category(),
                                   "clock_gettime failed in init");
        }
        
        // Calculate first wakeup time (aligned to period + phase)
        next_wakeup_ = now;
        next_wakeup_.tv_nsec += static_cast<long>(phase_ns_);
        normalize_timespec(next_wakeup_);
        
        // Add one period to ensure we're in the future
        add_period(next_wakeup_);
        
        initialized_ = true;
    }
    
    /**
     * @brief Wait until next period
     *
     * Blocks until the next scheduled wakeup time using absolute
     * time sleep. This prevents cumulative timer drift.
     *
     * @return true if wakeup was on time, false if deadline was missed
     *
     * @throws std::system_error if clock_nanosleep fails
     */
    bool wait() {
        if (!initialized_) {
            throw std::logic_error("ThreadTiming not initialized");
        }

        // Save the expected wakeup time before sleeping (for jitter calculation)
        const TimestampNs expected_wakeup = next_wakeup_ns();

        const int ret = clock_nanosleep(
            to_clockid(clock_),
            TIMER_ABSTIME,
            &next_wakeup_,
            nullptr
        );

        if (ret == 0) {
            // Save expected wakeup for jitter calculation (before advancing)
            last_expected_wakeup_ = expected_wakeup;

            // Success - get actual wakeup time
            struct timespec now;
            clock_gettime(to_clockid(clock_), &now);

            // Store actual wakeup for jitter calculation
            last_actual_wakeup_ = static_cast<uint64_t>(now.tv_sec) * 1000000000UL +
                                  static_cast<uint64_t>(now.tv_nsec);

            // Advance to next period AFTER waking up
            add_period(next_wakeup_);
            cycle_count_++;

            // Check if we actually woke up on time
            // Use wrap-safe comparison to handle timestamp wrap correctly
            // Compare against the expected wakeup time (before we advanced it)
            const int64_t jitter = timestamp_diff_ns(last_actual_wakeup_, expected_wakeup);
            if (jitter > static_cast<int64_t>(period_ns_)) {
                // We woke up after the scheduled time + one period = missed deadline
                deadline_misses_++;
                consecutive_misses_++;

                // Resynchronize to current time
                next_wakeup_ = now;
                add_period(next_wakeup_);

                return false;
            }

            consecutive_misses_ = 0;
            return true;
        }
        else if (ret == EINTR) {
            // Interrupted by signal - restart with same absolute time
            // No need to recalculate - absolute time is still valid
            return wait();
        }
        else {
            throw std::system_error(ret, std::system_category(),
                                   "clock_nanosleep failed");
        }
    }
    
    /**
     * @brief Check if last wait missed deadline
     * 
     * @return true if deadline was missed
     */
    bool missed_deadline() const noexcept {
        return consecutive_misses_ > 0;
    }
    
    /**
     * @brief Get total deadline miss count
     * 
     * @return uint64_t Total number of deadline misses
     */
    uint64_t deadline_misses() const noexcept {
        return deadline_misses_;
    }
    
    /**
     * @brief Get consecutive deadline miss count
     * 
     * @return uint64_t Consecutive misses (reset on successful wait)
     */
    uint64_t consecutive_misses() const noexcept {
        return consecutive_misses_;
    }
    
    /**
     * @brief Get cycle count
     * 
     * @return uint64_t Number of successful wait cycles
     */
    uint64_t cycle_count() const noexcept {
        return cycle_count_;
    }
    
    /**
     * @brief Get period in nanoseconds
     * 
     * @return uint64_t Period
     */
    uint64_t period_ns() const noexcept {
        return period_ns_;
    }
    
    /**
     * @brief Get next scheduled wakeup time
     * 
     * @return TimestampNs Next wakeup time in nanoseconds
     */
    TimestampNs next_wakeup_ns() const noexcept {
        return static_cast<uint64_t>(next_wakeup_.tv_sec) * 1000000000UL +
               static_cast<uint64_t>(next_wakeup_.tv_nsec);
    }
    
    /**
     * @brief Calculate jitter (timing variation)
     *
     * @param actual_wakeup_ns Actual wakeup time from caller
     * @return int64_t Jitter in nanoseconds (positive = late, negative = early)
     */
    int64_t calculate_jitter(TimestampNs /*actual_wakeup_ns*/) const noexcept {
        // Use the last expected wakeup time saved by wait()
        // This is called after wait() returns, so use last_expected_wakeup_
        const int64_t expected = static_cast<int64_t>(last_expected_wakeup_);
        const int64_t actual = static_cast<int64_t>(last_actual_wakeup_);
        return actual - expected;
    }

private:
    /**
     * @brief Normalize timespec (handle nanosecond overflow)
     */
    static void normalize_timespec(struct timespec& ts) {
        while (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        while (ts.tv_nsec < 0) {
            ts.tv_sec--;
            ts.tv_nsec += 1000000000L;
        }
    }
    
    /**
     * @brief Add one period to timespec
     */
    void add_period(struct timespec& ts) {
        ts.tv_sec += period_ns_ / 1000000000L;
        ts.tv_nsec += period_ns_ % 1000000000L;
        normalize_timespec(ts);
    }
    
    uint64_t period_ns_;
    uint64_t phase_ns_;
    ClockId clock_;
    struct timespec next_wakeup_;
    uint64_t cycle_count_;
    uint64_t deadline_misses_;
    uint64_t consecutive_misses_;
    bool initialized_;
    
    // Last wakeup tracking for jitter calculation
    TimestampNs last_expected_wakeup_;
    TimestampNs last_actual_wakeup_;
};

/**
 * @brief Deadline monitor for tracking execution time bounds
 * 
 * Usage:
 * @code
 *     DeadlineMonitor deadline(2000000);  // 2ms budget
 *     
 *     deadline.start();
 *     process_frame();
 *     deadline.stop();
 *     
 *     if (deadline.exceeded()) {
 *         // Handle overrun
 *     }
 * @endcode
 */
class DeadlineMonitor {
public:
    /**
     * @brief Construct deadline monitor
     * 
     * @param budget_ns Execution time budget in nanoseconds
     */
    explicit DeadlineMonitor(uint64_t budget_ns) noexcept
        : budget_ns_(budget_ns)
        , start_ns_(0)
        , end_ns_(0)
        , running_(false) {}
    
    /**
     * @brief Start timing
     */
    void start() noexcept {
        start_ns_ = get_timestamp();
        running_ = true;
    }
    
    /**
     * @brief Stop timing and check deadline
     * 
     * @return true if deadline was met, false if exceeded
     */
    bool stop() noexcept {
        end_ns_ = get_timestamp();
        running_ = false;
        return (end_ns_ - start_ns_) <= budget_ns_;
    }
    
    /**
     * @brief Check if deadline was exceeded
     * 
     * @return true if execution time exceeded budget
     */
    bool exceeded() const noexcept {
        if (!running_ && end_ns_ == 0) return false;
        
        const uint64_t end = running_ ? get_timestamp() : end_ns_;
        return (end - start_ns_) > budget_ns_;
    }
    
    /**
     * @brief Get elapsed time
     * 
     * @return uint64_t Elapsed time in nanoseconds
     */
    uint64_t elapsed_ns() const noexcept {
        const uint64_t end = running_ ? get_timestamp() : end_ns_;
        return end - start_ns_;
    }
    
    /**
     * @brief Get remaining time in budget
     * 
     * @return uint64_t Remaining nanoseconds (0 if exceeded)
     */
    uint64_t remaining_ns() const noexcept {
        const uint64_t elapsed = elapsed_ns();
        return elapsed >= budget_ns_ ? 0 : budget_ns_ - elapsed;
    }
    
    /**
     * @brief Check if still running
     * 
     * @return true if start() called without matching stop()
     */
    bool is_running() const noexcept {
        return running_;
    }

private:
    uint64_t budget_ns_;
    uint64_t start_ns_;
    uint64_t end_ns_;
    bool running_;
};

/**
 * @brief Frame rate calculator
 * 
 * Tracks actual frame rate over a sliding window.
 */
class FrameRateCalculator {
public:
    /**
     * @brief Construct calculator
     * 
     * @param window_size Number of frames to average over
     */
    explicit FrameRateCalculator(size_t window_size = 120) noexcept
        : window_size_(window_size)
        , count_(0)
        , first_timestamp_(0)
        , last_timestamp_(0) {}
    
    /**
     * @brief Record frame timestamp
     * 
     * @param timestamp_ns Frame timestamp in nanoseconds
     */
    void record_frame(TimestampNs timestamp_ns) noexcept {
        if (count_ == 0) {
            first_timestamp_ = timestamp_ns;
        }
        last_timestamp_ = timestamp_ns;
        count_++;
    }
    
    /**
     * @brief Get current frame rate
     * 
     * @return double Frames per second
     */
    double fps() const noexcept {
        if (count_ < 2) return 0.0;
        
        const uint64_t delta = last_timestamp_ - first_timestamp_;
        if (delta == 0) return 0.0;
        
        return static_cast<double>(count_ - 1) * 1000000000.0 / 
               static_cast<double>(delta);
    }
    
    /**
     * @brief Reset calculator
     */
    void reset() noexcept {
        count_ = 0;
        first_timestamp_ = 0;
        last_timestamp_ = 0;
    }

private:
    size_t window_size_;
    size_t count_;
    TimestampNs first_timestamp_;
    TimestampNs last_timestamp_;
};

}  // namespace aurore
