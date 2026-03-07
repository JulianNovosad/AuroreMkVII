#ifndef RELIABILITY_TRACKER_H
#define RELIABILITY_TRACKER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <array>

namespace aurore {
namespace reliability {

enum class FailureCategory {
    TPU,
    CAMERA,
    SERVO,
    MEMORY,
    THERMAL,
    NETWORK,
    UNKNOWN,
    COUNT
};

enum class FailureSeverity {
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

constexpr size_t FAILURE_CATEGORY_COUNT = static_cast<size_t>(FailureCategory::COUNT);

struct FailureEvent {
    FailureCategory category;
    FailureSeverity severity;
    std::chrono::steady_clock::time_point timestamp;
    std::string message;
    uint64_t recovery_time_us;
};

struct MTBFStats {
    FailureCategory category;
    double mtbf_hours;
    double mtbf_minutes;
    uint64_t total_failures;
    uint64_t total_uptime_seconds;
    double failure_rate_per_hour;
    double availability_percentage;
    double ema_mtbf_hours;
};

struct ReliabilitySnapshot {
    std::chrono::steady_clock::time_point timestamp;
    uint64_t system_uptime_seconds;
    uint64_t total_failures_all_categories;
    std::vector<MTBFStats> mtbf_by_category;
    double overall_mtbf_hours;
    double overall_availability;
};

class ReliabilityTracker {
public:
    static ReliabilityTracker& instance() {
        static ReliabilityTracker tracker;
        return tracker;
    }

    void record_failure(FailureCategory category, FailureSeverity severity,
                        const std::string& message, uint64_t recovery_time_us = 0) {
        FailureEvent event;
        event.category = category;
        event.severity = severity;
        event.timestamp = std::chrono::steady_clock::now();
        event.message = message;
        event.recovery_time_us = recovery_time_us;

        std::lock_guard<std::mutex> lock(mutex_);
        failure_history_.push_back(event);
        size_t idx = static_cast<size_t>(category);
        if (idx < FAILURE_CATEGORY_COUNT) {
            category_counts_[idx].fetch_add(1);
        }
        total_failures_.fetch_add(1);

        if (category < FailureCategory::COUNT) {
            update_ema_mtbf(category);
        }

        if (severity == FailureSeverity::CRITICAL) {
            critical_failures_.fetch_add(1);
        }
    }

    void record_recovery(FailureCategory category, uint64_t recovery_time_us) {
        size_t idx = static_cast<size_t>(category);
        if (idx >= FAILURE_CATEGORY_COUNT) return;

        std::lock_guard<std::mutex> lock(mutex_);
        category_recovery_time_[idx].fetch_add(recovery_time_us);
        total_recovery_time_us_.fetch_add(recovery_time_us);
    }

    void start_uptime() {
        start_time_ = std::chrono::steady_clock::now();
        is_running_.store(true);
    }

    void stop_uptime() {
        if (is_running_.load()) {
            auto now = std::chrono::steady_clock::now();
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
            total_uptime_ms_.fetch_add(duration_ms.count());
            total_uptime_seconds_.fetch_add(duration_ms.count() / 1000);
            is_running_.store(false);
        }
    }

    uint64_t get_current_uptime_ms() {
        if (is_running_.load()) {
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
            return total_uptime_ms_.load() + duration.count();
        }
        return total_uptime_ms_.load();
    }

    uint64_t get_current_uptime_seconds() {
        if (is_running_.load()) {
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
            return total_uptime_seconds_.load() + (duration.count() / 1000);
        }
        return total_uptime_seconds_.load();
    }

    ReliabilitySnapshot get_snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        ReliabilitySnapshot snapshot;
        snapshot.timestamp = std::chrono::steady_clock::now();
        snapshot.system_uptime_seconds = get_current_uptime_seconds();
        snapshot.total_failures_all_categories = total_failures_.load();

        uint64_t total_failures = 0;
        double weighted_mtbf_sum = 0.0;

        for (size_t i = 0; i < FAILURE_CATEGORY_COUNT; ++i) {
            MTBFStats stats = calculate_mtbf_for_category(static_cast<FailureCategory>(i));
            snapshot.mtbf_by_category.push_back(stats);
            total_failures += stats.total_failures;
            weighted_mtbf_sum += stats.mtbf_hours * stats.total_failures;
        }

        if (total_failures > 0) {
            snapshot.overall_mtbf_hours = weighted_mtbf_sum / total_failures;
        } else {
            snapshot.overall_mtbf_hours = static_cast<double>(snapshot.system_uptime_seconds) / 3600.0;
        }

        uint64_t total_downtime_us = total_recovery_time_us_.load();
        double total_downtime_seconds = static_cast<double>(total_downtime_us) / 1000000.0;
        if (snapshot.system_uptime_seconds > 0) {
            snapshot.overall_availability = 100.0 * (1.0 - (total_downtime_seconds / snapshot.system_uptime_seconds));
        } else {
            snapshot.overall_availability = 100.0;
        }

        return snapshot;
    }

    MTBFStats calculate_mtbf_for_category(FailureCategory category) {
        MTBFStats stats;
        stats.category = category;
        size_t idx = static_cast<size_t>(category);

        if (idx >= FAILURE_CATEGORY_COUNT) {
            stats.total_failures = 0;
            stats.total_uptime_seconds = get_current_uptime_seconds();
            stats.mtbf_hours = static_cast<double>(stats.total_uptime_seconds) / 3600.0;
            stats.mtbf_minutes = stats.mtbf_hours * 60.0;
            stats.failure_rate_per_hour = 0.0;
            stats.availability_percentage = 100.0;
            stats.ema_mtbf_hours = 0.0;
            return stats;
        }

        stats.total_failures = category_counts_[idx].load();
        stats.total_uptime_seconds = get_current_uptime_seconds();

        if (stats.total_failures > 0) {
            double uptime_hours = static_cast<double>(stats.total_uptime_seconds) / 3600.0;
            stats.mtbf_hours = uptime_hours / stats.total_failures;
            stats.mtbf_minutes = stats.mtbf_hours * 60.0;
            stats.failure_rate_per_hour = 1.0 / stats.mtbf_hours;
        } else {
            stats.mtbf_hours = stats.total_uptime_seconds > 0 ? static_cast<double>(stats.total_uptime_seconds) / 3600.0 : 0.0;
            stats.mtbf_minutes = stats.mtbf_hours * 60.0;
            stats.failure_rate_per_hour = 0.0;
        }

        uint64_t category_downtime_us = category_recovery_time_[idx].load();
        double downtime_seconds = static_cast<double>(category_downtime_us) / 1000000.0;
        if (stats.total_uptime_seconds > 0) {
            stats.availability_percentage = 100.0 * (1.0 - (downtime_seconds / stats.total_uptime_seconds));
        } else {
            stats.availability_percentage = 100.0;
        }

        stats.ema_mtbf_hours = ema_mtbf_[idx].load();

        return stats;
    }

    bool meets_mtbf_target(FailureCategory category, double target_hours) {
        MTBFStats stats = calculate_mtbf_for_category(category);
        return stats.mtbf_hours >= target_hours || stats.total_failures == 0;
    }

    std::string get_category_name(FailureCategory category) const {
        switch (category) {
            case FailureCategory::TPU: return "TPU";
            case FailureCategory::CAMERA: return "Camera";
            case FailureCategory::SERVO: return "Servo";
            case FailureCategory::MEMORY: return "Memory";
            case FailureCategory::THERMAL: return "Thermal";
            case FailureCategory::NETWORK: return "Network";
            case FailureCategory::UNKNOWN: return "Unknown";
            default: return "Unknown";
        }
    }

    void export_to_csv(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ofstream file(filename);
        if (!file.is_open()) return;

        ReliabilitySnapshot snapshot = get_snapshot();

        file << "Reliability Report\n";
        file << "Generated," << std::chrono::duration_cast<std::chrono::milliseconds>(
            snapshot.timestamp.time_since_epoch()).count() << "\n\n";

        file << "System Uptime (seconds)," << snapshot.system_uptime_seconds << "\n";
        file << "Total Failures," << snapshot.total_failures_all_categories << "\n";
        file << "Overall MTBF (hours)," << snapshot.overall_mtbf_hours << "\n";
        file << "Overall Availability (%)," << snapshot.overall_availability << "\n\n";

        file << "Category,MTBF (hours),MTBF (minutes),Failures,Uptime (hours),Failure Rate (/hr),Availability (%),EMA MTBF (hours)\n";
        for (const auto& stats : snapshot.mtbf_by_category) {
            file << get_category_name(stats.category) << ","
                 << std::fixed << std::setprecision(4) << stats.mtbf_hours << ","
                 << stats.mtbf_minutes << ","
                 << stats.total_failures << ","
                 << (stats.total_uptime_seconds / 3600.0) << ","
                 << stats.failure_rate_per_hour << ","
                 << stats.availability_percentage << ","
                 << stats.ema_mtbf_hours << "\n";
        }

        file << "\nFailure History\n";
        file << "Timestamp,Category,Severity,Message,Recovery Time (us)\n";
        for (const auto& event : failure_history_) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                event.timestamp.time_since_epoch()).count();
            file << ms << ","
                 << get_category_name(event.category) << ","
                 << static_cast<int>(event.severity) << ","
                 << "\"" << event.message << "\""
                 << "," << event.recovery_time_us << "\n";
        }

        file.close();
    }

    uint64_t get_total_failures() const { return total_failures_.load(); }
    uint64_t get_critical_failures() const { return critical_failures_.load(); }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        failure_history_.clear();
        for (size_t i = 0; i < FAILURE_CATEGORY_COUNT; ++i) {
            category_counts_[i].store(0);
            category_recovery_time_[i].store(0);
            ema_mtbf_[i].store(0.0);
        }
        total_failures_.store(0);
        critical_failures_.store(0);
        total_uptime_seconds_.store(0);
        total_uptime_ms_.store(0);
        total_recovery_time_us_.store(0);
        is_running_.store(false);
    }

private:
    ReliabilityTracker() {
        for (size_t i = 0; i < FAILURE_CATEGORY_COUNT; ++i) {
            category_counts_[i].store(0);
            category_recovery_time_[i].store(0);
            ema_mtbf_[i].store(0.0);
            ema_alpha_[i] = 0.3;
        }
        total_failures_.store(0);
        critical_failures_.store(0);
        total_uptime_seconds_.store(0);
        total_uptime_ms_.store(0);
        total_recovery_time_us_.store(0);
        is_running_.store(false);
    }

    void update_ema_mtbf(FailureCategory category) {
        size_t idx = static_cast<size_t>(category);
        if (idx >= FAILURE_CATEGORY_COUNT) return;

        MTBFStats current = calculate_mtbf_for_category(category);
        double current_mtbf = current.mtbf_hours;
        double ema = ema_mtbf_[idx].load();

        if (ema == 0.0) {
            ema_mtbf_[idx].store(current_mtbf);
        } else {
            double new_ema = ema_alpha_[idx] * current_mtbf + (1.0 - ema_alpha_[idx]) * ema;
            ema_mtbf_[idx].store(new_ema);
        }
    }

    std::vector<FailureEvent> failure_history_;
    std::mutex mutex_;

    std::array<std::atomic<uint64_t>, FAILURE_CATEGORY_COUNT> category_counts_;
    std::array<std::atomic<uint64_t>, FAILURE_CATEGORY_COUNT> category_recovery_time_;
    std::array<std::atomic<double>, FAILURE_CATEGORY_COUNT> ema_mtbf_;
    std::array<double, FAILURE_CATEGORY_COUNT> ema_alpha_;

    std::atomic<uint64_t> total_failures_{0};
    std::atomic<uint64_t> critical_failures_{0};
    std::atomic<uint64_t> total_uptime_seconds_{0};
    std::atomic<uint64_t> total_uptime_ms_{0};
    std::atomic<uint64_t> total_recovery_time_us_{0};

    std::chrono::steady_clock::time_point start_time_;
    std::atomic<bool> is_running_{false};
};

inline ReliabilityTracker& get_reliability_tracker() {
    return ReliabilityTracker::instance();
}

inline void record_failure(FailureCategory category, FailureSeverity severity,
                           const std::string& message, uint64_t recovery_time_us = 0) {
    ReliabilityTracker::instance().record_failure(category, severity, message, recovery_time_us);
}

inline bool check_mtbf_target(FailureCategory category, double target_hours) {
    return ReliabilityTracker::instance().meets_mtbf_target(category, target_hours);
}

}  // namespace reliability
}  // namespace aurore

#endif  // RELIABILITY_TRACKER_H
