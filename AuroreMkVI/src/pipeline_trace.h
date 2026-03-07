#ifndef PIPELINE_TRACE_H
#define PIPELINE_TRACE_H

#include <cstdint>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include "timing.h"

namespace aurore {
namespace trace {

enum class TraceStage {
    MIPI_INTERRUPT = 0,
    LIBCAMERA_CAPTURE,
    IMAGE_PROCESSOR,
    TPU_INFERENCE,
    DETECTION_PARSING,
    BALLISTICS_CALC,
    FIRE_AUTHORIZATION,
    SERVO_ACTUATION,
    STAGE_COUNT
};

constexpr size_t TRACE_BUFFER_SIZE = 4096;
constexpr size_t MAX_TRACE_STAGES = 8;

struct TraceEntry {
    uint64_t timestamp_raw_ns;
    uint32_t frame_id;
    TraceStage stage;
    uint8_t data[24];
    
    uint64_t get_latency_ns() const { return timestamp_raw_ns; }
};

struct TraceStageInfo {
    const char* name;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    uint64_t total_latency_ns;
    uint64_t count;
    
    void reset() {
        min_latency_ns = UINT64_MAX;
        max_latency_ns = 0;
        total_latency_ns = 0;
        count = 0;
    }
    
    void record(uint64_t latency_ns) {
        if (latency_ns < min_latency_ns) min_latency_ns = latency_ns;
        if (latency_ns > max_latency_ns) max_latency_ns = latency_ns;
        total_latency_ns += latency_ns;
        count++;
    }
    
    double get_avg_ns() const {
        return count > 0 ? (double)total_latency_ns / count : 0.0;
    }
};

class PipelineTracer {
public:
    static PipelineTracer& instance() {
        static PipelineTracer tracer;
        return tracer;
    }
    
    void init(const char* output_path = nullptr) {
        buffer_index_.store(0, std::memory_order_relaxed);
        enabled_.store(true, std::memory_order_release);
        
        for (size_t i = 0; i < MAX_TRACE_STAGES; i++) {
            stage_info_[i].reset();
        }
        
        if (output_path) {
            output_file_ = fopen(output_path, "w");
            if (output_file_) {
                fprintf(output_file_, "frame_id,stage,timestamp_ns,latency_ns\n");
            }
        }
    }
    
    void shutdown() {
        enabled_.store(false, std::memory_order_release);
        if (output_file_) {
            fclose(output_file_);
            output_file_ = nullptr;
        }
    }
    
    void trace_data(TraceStage stage, uint32_t frame_id, const void* data, size_t data_size) {
        if (!enabled_.load(std::memory_order_acquire)) return;
        
        size_t index = buffer_index_.fetch_add(1, std::memory_order_relaxed) % TRACE_BUFFER_SIZE;
        TraceEntry& entry = buffer_[index];
        
        entry.timestamp_raw_ns = get_time_raw_ns();
        entry.frame_id = frame_id;
        entry.stage = stage;
        
        size_t copy_size = std::min(data_size, sizeof(entry.data));
        memcpy(entry.data, data, copy_size);
        
        if (output_file_) {
            fprintf(output_file_, "%u,%u,%lu\n", frame_id, (uint32_t)stage, entry.timestamp_raw_ns);
        }
    }
    
    void record_stage_latency(TraceStage stage, uint64_t latency_ns) {
        stage_info_[(size_t)stage].record(latency_ns);
    }
    
    void trace_enter(TraceStage stage, uint32_t frame_id) {
        if (!enabled_.load(std::memory_order_acquire)) return;
        
        size_t index = buffer_index_.fetch_add(1, std::memory_order_relaxed) % TRACE_BUFFER_SIZE;
        TraceEntry& entry = buffer_[index];
        
        entry.timestamp_raw_ns = get_time_raw_ns();
        entry.frame_id = frame_id;
        entry.stage = stage;
        entry.data[0] = 1; // enter marker
        
        if (output_file_) {
            fprintf(output_file_, "%u,%u,%lu,enter\n", frame_id, (uint32_t)stage, entry.timestamp_raw_ns);
        }
    }
    
    void trace_exit(TraceStage stage, uint32_t frame_id, uint64_t latency_ns) {
        if (!enabled_.load(std::memory_order_acquire)) return;
        
        size_t index = buffer_index_.fetch_add(1, std::memory_order_relaxed) % TRACE_BUFFER_SIZE;
        TraceEntry& entry = buffer_[index];
        
        entry.timestamp_raw_ns = get_time_raw_ns();
        entry.frame_id = frame_id;
        entry.stage = stage;
        entry.data[0] = 0; // exit marker
        
        memcpy(entry.data + 4, &latency_ns, sizeof(latency_ns));
        
        record_stage_latency(stage, latency_ns);
        
        if (output_file_) {
            fprintf(output_file_, "%u,%u,%lu,%lu\n", frame_id, (uint32_t)stage, entry.timestamp_raw_ns, latency_ns);
        }
    }
    
    const char* get_stage_name(TraceStage stage) const {
        static const char* names[] = {
            "MIPI_INTERRUPT",
            "LIBCAMERA_CAPTURE",
            "IMAGE_PROCESSOR",
            "TPU_INFERENCE",
            "DETECTION_PARSING",
            "BALLISTICS_CALC",
            "FIRE_AUTHORIZATION",
            "SERVO_ACTUATION"
        };
        return names[(size_t)stage];
    }
    
    TraceStageInfo* get_stage_info() { return stage_info_; }
    size_t get_buffer_size() const { return TRACE_BUFFER_SIZE; }
    const TraceEntry* get_buffer() const { return buffer_.data(); }
    size_t get_buffer_index() const { return buffer_index_.load(std::memory_order_relaxed); }
    const std::array<TraceEntry, TRACE_BUFFER_SIZE>& get_buffer_array() const { return buffer_; }
    
private:
    PipelineTracer() = default;
    ~PipelineTracer() { shutdown(); }
    
    std::array<TraceEntry, TRACE_BUFFER_SIZE> buffer_;
    std::atomic<size_t> buffer_index_{0};
    std::atomic<bool> enabled_{false};
    FILE* output_file_ = nullptr;
    TraceStageInfo stage_info_[MAX_TRACE_STAGES];
};

inline void trace_stage_enter(TraceStage stage, uint32_t frame_id) {
    PipelineTracer::instance().trace_enter(stage, frame_id);
}

inline void trace_stage_exit(TraceStage stage, uint32_t frame_id, uint64_t latency_ns) {
    PipelineTracer::instance().trace_exit(stage, frame_id, latency_ns);
}

inline void trace_stage_data(TraceStage stage, uint32_t frame_id, const void* data, size_t size) {
    PipelineTracer::instance().trace_data(stage, frame_id, data, size);
}

} // namespace trace
} // namespace aurore

#endif // PIPELINE_TRACE_H
