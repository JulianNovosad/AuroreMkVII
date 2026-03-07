#ifndef PER_FRAME_WATCHDOG_H
#define PER_FRAME_WATCHDOG_H

#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <cstdint>

namespace aurore {
namespace watchdog {

constexpr uint64_t STALL_THRESHOLD_US = 25000;
constexpr uint64_t STAGE_DEADLINE_US = 25000;
constexpr uint64_t PIPELINE_DEADLINE_US = 30000;

enum class PipelineStage {
    CAPTURE,
    IMAGE_PROCESSOR,
    INFERENCE,
    LOGIC,
    OVERLAY,
    DISPLAY,
    NUM_STAGES
};

struct StageWatchdog {
    PipelineStage stage;
    std::string name;
    std::atomic<uint64_t> last_latency_us{0};
    std::atomic<uint64_t> stall_count{0};
    std::atomic<uint64_t> max_latency_us{0};
    std::atomic<uint64_t> total_latency_us{0};
    std::atomic<uint64_t> sample_count{0};

    StageWatchdog(PipelineStage s, const std::string& n) : stage(s), name(n) {}

    void record_latency(uint64_t latency_us) {
        last_latency_us.store(latency_us, std::memory_order_release);
        total_latency_us.fetch_add(latency_us, std::memory_order_relaxed);
        sample_count.fetch_add(1, std::memory_order_relaxed);

        uint64_t current_max = max_latency_us.load(std::memory_order_relaxed);
        while (latency_us > current_max &&
               !max_latency_us.compare_exchange_weak(current_max, latency_us,
                   std::memory_order_relaxed, std::memory_order_relaxed)) {}

        if (latency_us > STALL_THRESHOLD_US) {
            stall_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    double get_avg_latency_us() const {
        uint64_t count = sample_count.load(std::memory_order_acquire);
        if (count == 0) return 0.0;
        return static_cast<double>(total_latency_us.load(std::memory_order_acquire)) / count;
    }

    uint64_t get_stall_count() const {
        return stall_count.load(std::memory_order_acquire);
    }

    uint64_t get_max_latency() const {
        return max_latency_us.load(std::memory_order_acquire);
    }

    bool is_stalled() const {
        return last_latency_us.load(std::memory_order_acquire) > STALL_THRESHOLD_US;
    }

    void reset() {
        stall_count.store(0, std::memory_order_release);
        max_latency_us.store(0, std::memory_order_release);
        total_latency_us.store(0, std::memory_order_release);
        sample_count.store(0, std::memory_order_release);
        last_latency_us.store(0, std::memory_order_release);
    }
};

class PerFrameWatchdog {
public:
    PerFrameWatchdog();
    ~PerFrameWatchdog();

    StageWatchdog& get_stage(PipelineStage stage);
    const StageWatchdog& get_stage(PipelineStage stage) const;

    void record_stage_latency(PipelineStage stage, uint64_t latency_us);
    void record_frame_latency(uint64_t capture_to_display_us);

    bool check_pipeline_health() const;
    uint64_t get_total_stalls() const;
    uint64_t get_frame_stalls() const { return frame_stall_count_.load(); }

    void reset_all();
    void set_recovery_callback(void (*callback)());

    std::string get_health_report() const;

    static const char* stage_name(PipelineStage stage);
    static PipelineStage parse_stage(const std::string& name);

private:
    std::vector<StageWatchdog*> stages_;
    std::atomic<uint64_t> total_stall_count_{0};
    std::atomic<uint64_t> frame_stall_count_{0};
    std::atomic<uint64_t> total_frames_{0};
    std::atomic<uint64_t> healthy_frames_{0};
    void (*recovery_callback_)();

    void increment_frame_stalls();
};

class FrameTimer {
public:
    FrameTimer(PerFrameWatchdog& watchdog, PipelineStage stage)
        : watchdog_(watchdog), stage_(stage), start_time_(std::chrono::steady_clock::now()) {}

    ~FrameTimer() {
        auto end_time = std::chrono::steady_clock::now();
        uint64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time_).count();
        watchdog_.record_stage_latency(stage_, elapsed_us);
    }

    uint64_t elapsed_us() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now - start_time_).count();
    }

private:
    PerFrameWatchdog& watchdog_;
    PipelineStage stage_;
    std::chrono::steady_clock::time_point start_time_;
};

class ScopedStageTimer {
public:
    ScopedStageTimer(PerFrameWatchdog& wd, PipelineStage stage, uint64_t* storage)
        : watchdog_(wd), stage_(stage), storage_(storage), start_(std::chrono::steady_clock::now()) {}

    ~ScopedStageTimer() {
        uint64_t elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_).count();
        if (storage_) *storage_ = elapsed;
        watchdog_.record_stage_latency(stage_, elapsed);
    }

private:
    PerFrameWatchdog& watchdog_;
    PipelineStage stage_;
    uint64_t* storage_;
    std::chrono::steady_clock::time_point start_;
};

}
}

#endif // PER_FRAME_WATCHDOG_H
