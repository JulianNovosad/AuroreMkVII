#ifndef MEMORY_POOL_LIMITS_H
#define MEMORY_POOL_LIMITS_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <iostream>

namespace aurore {
namespace memory {

constexpr size_t DEFAULT_HEAP_LIMIT = 512 * 1024 * 1024;
constexpr size_t DEFAULT_INFERENCE_LIMIT = 128 * 1024 * 1024;
constexpr size_t DEFAULT_DMABUF_LIMIT = 64 * 1024 * 1024;
constexpr size_t DEFAULT_QUEUE_LIMIT = 32 * 1024 * 1024;
constexpr size_t DEFAULT_STACK_LIMIT = 8 * 1024 * 1024;
constexpr size_t DEFAULT_TOTAL_LIMIT = 1024 * 1024 * 1024;

constexpr float WARNING_THRESHOLD = 0.80f;
constexpr float CRITICAL_THRESHOLD = 0.90f;
constexpr float EMERGENCY_THRESHOLD = 0.95f;

enum class MemoryPool {
    HEAP_GENERAL,
    INFERENCE_BUFFERS,
    DMABUF_POOL,
    QUEUE_BUFFERS,
    TELEMETRY,
    UNKNOWN
};

enum class MemoryAlertLevel {
    NORMAL,
    WARNING,
    CRITICAL,
    EMERGENCY
};

struct MemoryPoolConfig {
    MemoryPool pool;
    std::string name;
    size_t limit_bytes;
    float warning_threshold;
    float critical_threshold;
    float emergency_threshold;
    bool purge_enabled;
    int purge_priority;
};

struct MemoryPoolStats {
    MemoryPool pool;
    std::string name;
    size_t current_bytes;
    size_t limit_bytes;
    size_t peak_bytes;
    float utilization_percent;
    MemoryAlertLevel alert_level;
    uint64_t allocation_count;
    uint64_t deallocation_count;
    uint64_t purge_count;
    uint64_t failed_allocations;
};

struct MemorySnapshot {
    std::chrono::steady_clock::time_point timestamp;
    uint64_t total_allocated_bytes;
    uint64_t total_limit_bytes;
    float overall_utilization_percent;
    MemoryAlertLevel overall_alert_level;
    uint64_t total_purge_count;
    uint64_t total_failed_allocations;
};

constexpr size_t POOL_COUNT = 6;

class MemoryPoolLimits {
public:
    MemoryPoolLimits() {
        configure_defaults();
    }

    static MemoryPoolLimits& instance() {
        static MemoryPoolLimits limits;
        return limits;
    }

    static std::unique_ptr<MemoryPoolLimits> create() {
        return std::make_unique<MemoryPoolLimits>();
    }

    void configure_pool(const MemoryPoolConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t idx = static_cast<size_t>(config.pool);
        if (idx >= POOL_COUNT) return;

        configs_[idx] = config;
        pool_names_[idx] = config.name;
        pool_limits_[idx].store(config.limit_bytes);
        pool_alert_levels_[idx].store(MemoryAlertLevel::NORMAL);
    }

    void configure_defaults() {
        configure_pool({MemoryPool::HEAP_GENERAL, "Heap General",
                        DEFAULT_HEAP_LIMIT, WARNING_THRESHOLD, CRITICAL_THRESHOLD,
                        EMERGENCY_THRESHOLD, true, 10});
        configure_pool({MemoryPool::INFERENCE_BUFFERS, "Inference Buffers",
                        DEFAULT_INFERENCE_LIMIT, WARNING_THRESHOLD, CRITICAL_THRESHOLD,
                        EMERGENCY_THRESHOLD, true, 5});
        configure_pool({MemoryPool::DMABUF_POOL, "DMA-BUF Pool",
                        DEFAULT_DMABUF_LIMIT, WARNING_THRESHOLD, CRITICAL_THRESHOLD,
                        EMERGENCY_THRESHOLD, true, 3});
        configure_pool({MemoryPool::QUEUE_BUFFERS, "Queue Buffers",
                        DEFAULT_QUEUE_LIMIT, WARNING_THRESHOLD, CRITICAL_THRESHOLD,
                        EMERGENCY_THRESHOLD, true, 2});
        configure_pool({MemoryPool::TELEMETRY, "Telemetry",
                        16 * 1024 * 1024, WARNING_THRESHOLD, CRITICAL_THRESHOLD,
                        EMERGENCY_THRESHOLD, false, 1});
        configure_pool({MemoryPool::UNKNOWN, "Unknown", 0, WARNING_THRESHOLD,
                        CRITICAL_THRESHOLD, EMERGENCY_THRESHOLD, false, 0});
    }

    bool allocate(MemoryPool pool, size_t size) {
        size_t idx = static_cast<size_t>(pool);
        if (idx >= POOL_COUNT) return false;

        size_t new_total = pool_current_[idx].load() + size;
        size_t limit = pool_limits_[idx].load();

        if (new_total > limit) {
            pool_failed_[idx].fetch_add(1);
            if (allocation_failed_callback_) {
                allocation_failed_callback_(pool, size, new_total - limit);
            }
            return false;
        }

        size_t peak = pool_peak_[idx].load();
        if (new_total > peak) {
            pool_peak_[idx].store(new_total);
        }

        pool_current_[idx].store(new_total);
        pool_allocations_[idx].fetch_add(1);

        update_alert_level(idx);

        return true;
    }

    void deallocate(MemoryPool pool, size_t size) {
        size_t idx = static_cast<size_t>(pool);
        if (idx >= POOL_COUNT) return;

        size_t current = pool_current_[idx].load();
        if (size > current) {
            pool_current_[idx].store(0);
        } else {
            pool_current_[idx].store(current - size);
        }
        pool_deallocations_[idx].fetch_add(1);
    }

    size_t get_current(MemoryPool pool) const {
        size_t idx = static_cast<size_t>(pool);
        if (idx >= POOL_COUNT) return 0;
        return pool_current_[idx].load();
    }

    size_t get_limit(MemoryPool pool) const {
        size_t idx = static_cast<size_t>(pool);
        if (idx >= POOL_COUNT) return 0;
        return pool_limits_[idx].load();
    }

    float get_utilization(MemoryPool pool) const {
        size_t idx = static_cast<size_t>(pool);
        if (idx >= POOL_COUNT || pool_limits_[idx].load() == 0) return 0.0f;
        return static_cast<float>(pool_current_[idx].load()) / pool_limits_[idx].load();
    }

    MemoryAlertLevel get_alert_level(MemoryPool pool) const {
        size_t idx = static_cast<size_t>(pool);
        if (idx >= POOL_COUNT) return MemoryAlertLevel::NORMAL;
        return pool_alert_levels_[idx].load();
    }

    MemoryPoolStats get_pool_stats(MemoryPool pool) const {
        size_t idx = static_cast<size_t>(pool);
        MemoryPoolStats stats;
        stats.pool = pool;
        stats.name = pool_names_[idx];
        stats.current_bytes = pool_current_[idx].load();
        stats.limit_bytes = pool_limits_[idx].load();
        stats.peak_bytes = pool_peak_[idx].load();
        stats.utilization_percent = get_utilization(pool);
        stats.alert_level = pool_alert_levels_[idx].load();
        stats.allocation_count = pool_allocations_[idx].load();
        stats.deallocation_count = pool_deallocations_[idx].load();
        stats.purge_count = pool_purge_[idx].load();
        stats.failed_allocations = pool_failed_[idx].load();
        return stats;
    }

    MemorySnapshot get_snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        MemorySnapshot snapshot;
        snapshot.timestamp = std::chrono::steady_clock::now();

        uint64_t total_allocated = 0;
        uint64_t total_limit = 0;
        uint64_t total_purges = 0;
        uint64_t total_failures = 0;
        MemoryAlertLevel highest_alert = MemoryAlertLevel::NORMAL;

        for (size_t i = 0; i < POOL_COUNT; ++i) {
            total_allocated += pool_current_[i].load();
            total_limit += pool_limits_[i].load();
            total_purges += pool_purge_[i].load();
            total_failures += pool_failed_[i].load();

            if (pool_alert_levels_[i].load() > highest_alert) {
                highest_alert = pool_alert_levels_[i].load();
            }
        }

        snapshot.total_allocated_bytes = total_allocated;
        snapshot.total_limit_bytes = total_limit;
        snapshot.total_purge_count = total_purges;
        snapshot.total_failed_allocations = total_failures;
        snapshot.overall_alert_level = highest_alert;

        if (total_limit > 0) {
            snapshot.overall_utilization_percent = static_cast<float>(total_allocated) / total_limit * 100.0f;
        } else {
            snapshot.overall_utilization_percent = 0.0f;
        }

        return snapshot;
    }

    size_t purge(MemoryPool pool, size_t target_bytes) {
        size_t idx = static_cast<size_t>(pool);
        if (idx >= POOL_COUNT) return 0;

        std::lock_guard<std::mutex> lock(mutex_);

        size_t current = pool_current_[idx].load();
        size_t purged = std::min(target_bytes, current);
        pool_current_[idx].store(current - purged);
        pool_purge_[idx].fetch_add(1);

        return purged;
    }

    size_t purge_non_critical(size_t target_bytes) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<size_t> pool_indices;
        for (size_t i = 0; i < POOL_COUNT; ++i) {
            if (pool_purge_enabled_[i]) {
                pool_indices.push_back(i);
            }
        }

        std::sort(pool_indices.begin(), pool_indices.end(),
            [&](size_t a, size_t b) {
                return pool_purge_priority_[a] < pool_purge_priority_[b];
            });

        size_t total_purged = 0;
        for (size_t idx : pool_indices) {
            if (total_purged >= target_bytes) break;

            size_t pool_purged = purge(static_cast<MemoryPool>(idx),
                                       target_bytes - total_purged);
            total_purged += pool_purged;
        }

        return total_purged;
    }

    void set_allocation_failed_callback(std::function<void(MemoryPool, size_t, size_t)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        allocation_failed_callback_ = callback;
    }

    void set_alert_callback(std::function<void(MemoryPool, MemoryAlertLevel, float)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        alert_callback_ = callback;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < POOL_COUNT; ++i) {
            pool_current_[i].store(0);
            pool_peak_[i].store(0);
            pool_allocations_[i].store(0);
            pool_deallocations_[i].store(0);
            pool_purge_[i].store(0);
            pool_failed_[i].store(0);
            pool_alert_levels_[i].store(MemoryAlertLevel::NORMAL);
        }
    }

    std::string get_alert_level_string(MemoryAlertLevel level) const {
        switch (level) {
            case MemoryAlertLevel::NORMAL: return "NORMAL";
            case MemoryAlertLevel::WARNING: return "WARNING";
            case MemoryAlertLevel::CRITICAL: return "CRITICAL";
            case MemoryAlertLevel::EMERGENCY: return "EMERGENCY";
            default: return "UNKNOWN";
        }
    }

private:
    void update_alert_level(size_t idx) {
        float utilization = static_cast<float>(pool_current_[idx].load()) / pool_limits_[idx].load();
        size_t limit = pool_limits_[idx].load();

        MemoryAlertLevel new_level;
        if (limit == 0) {
            new_level = MemoryAlertLevel::NORMAL;
        } else if (utilization >= pool_emergency_thresh_[idx]) {
            new_level = MemoryAlertLevel::EMERGENCY;
        } else if (utilization >= pool_critical_thresh_[idx]) {
            new_level = MemoryAlertLevel::CRITICAL;
        } else if (utilization >= pool_warning_thresh_[idx]) {
            new_level = MemoryAlertLevel::WARNING;
        } else {
            new_level = MemoryAlertLevel::NORMAL;
        }

        MemoryAlertLevel old_level = pool_alert_levels_[idx].load();
        if (new_level != old_level) {
            pool_alert_levels_[idx].store(new_level);
            if (alert_callback_) {
                alert_callback_(static_cast<MemoryPool>(idx), new_level, utilization);
            }
        }
    }

    std::array<MemoryPoolConfig, POOL_COUNT> configs_;
    std::array<std::string, POOL_COUNT> pool_names_;
    std::array<std::atomic<size_t>, POOL_COUNT> pool_current_;
    std::array<std::atomic<size_t>, POOL_COUNT> pool_peak_;
    std::array<std::atomic<size_t>, POOL_COUNT> pool_limits_;
    std::array<std::atomic<uint64_t>, POOL_COUNT> pool_allocations_;
    std::array<std::atomic<uint64_t>, POOL_COUNT> pool_deallocations_;
    std::array<std::atomic<uint64_t>, POOL_COUNT> pool_purge_;
    std::array<std::atomic<uint64_t>, POOL_COUNT> pool_failed_;
    std::array<std::atomic<MemoryAlertLevel>, POOL_COUNT> pool_alert_levels_;
    std::array<float, POOL_COUNT> pool_warning_thresh_;
    std::array<float, POOL_COUNT> pool_critical_thresh_;
    std::array<float, POOL_COUNT> pool_emergency_thresh_;
    std::array<bool, POOL_COUNT> pool_purge_enabled_;
    std::array<int, POOL_COUNT> pool_purge_priority_;

    std::mutex mutex_;
    std::function<void(MemoryPool, size_t, size_t)> allocation_failed_callback_;
    std::function<void(MemoryPool, MemoryAlertLevel, float)> alert_callback_;
};

inline MemoryPoolLimits& get_memory_limits() {
    return MemoryPoolLimits::instance();
}

inline bool track_allocation(MemoryPool pool, size_t size) {
    return MemoryPoolLimits::instance().allocate(pool, size);
}

inline void track_deallocation(MemoryPool pool, size_t size) {
    MemoryPoolLimits::instance().deallocate(pool, size);
}

inline bool is_memory_critical() {
    auto snapshot = MemoryPoolLimits::instance().get_snapshot();
    return snapshot.overall_alert_level >= MemoryAlertLevel::CRITICAL;
}

}  // namespace memory
}  // namespace aurore

#endif  // MEMORY_POOL_LIMITS_H
