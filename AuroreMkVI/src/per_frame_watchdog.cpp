#include "per_frame_watchdog.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace aurore {
namespace watchdog {

PerFrameWatchdog::PerFrameWatchdog()
    : recovery_callback_(nullptr)
{
    stages_.resize(static_cast<size_t>(PipelineStage::NUM_STAGES), nullptr);

    stages_[static_cast<size_t>(PipelineStage::CAPTURE)] =
        new StageWatchdog(PipelineStage::CAPTURE, "Capture");

    stages_[static_cast<size_t>(PipelineStage::IMAGE_PROCESSOR)] =
        new StageWatchdog(PipelineStage::IMAGE_PROCESSOR, "ImageProcessor");

    stages_[static_cast<size_t>(PipelineStage::INFERENCE)] =
        new StageWatchdog(PipelineStage::INFERENCE, "Inference");

    stages_[static_cast<size_t>(PipelineStage::LOGIC)] =
        new StageWatchdog(PipelineStage::LOGIC, "Logic");

    stages_[static_cast<size_t>(PipelineStage::OVERLAY)] =
        new StageWatchdog(PipelineStage::OVERLAY, "Overlay");

    stages_[static_cast<size_t>(PipelineStage::DISPLAY)] =
        new StageWatchdog(PipelineStage::DISPLAY, "Display");
}

PerFrameWatchdog::~PerFrameWatchdog() {
    for (auto* stage : stages_) {
        delete stage;
    }
    stages_.clear();
}

StageWatchdog& PerFrameWatchdog::get_stage(PipelineStage stage) {
    return *stages_[static_cast<size_t>(stage)];
}

const StageWatchdog& PerFrameWatchdog::get_stage(PipelineStage stage) const {
    return *stages_[static_cast<size_t>(stage)];
}

void PerFrameWatchdog::record_stage_latency(PipelineStage stage, uint64_t latency_us) {
    if (stage == PipelineStage::NUM_STAGES) return;

    StageWatchdog* watchdog = stages_[static_cast<size_t>(stage)];
    if (watchdog) {
        watchdog->record_latency(latency_us);

        if (latency_us > STALL_THRESHOLD_US) {
            total_stall_count_.fetch_add(1, std::memory_order_relaxed);
            increment_frame_stalls();
        }
    }
}

void PerFrameWatchdog::record_frame_latency(uint64_t capture_to_display_us) {
    total_frames_.fetch_add(1, std::memory_order_relaxed);

    if (capture_to_display_us <= PIPELINE_DEADLINE_US) {
        healthy_frames_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PerFrameWatchdog::increment_frame_stalls() {
    frame_stall_count_.fetch_add(1, std::memory_order_relaxed);

    uint64_t current_stalls = frame_stall_count_.load(std::memory_order_acquire);

    if (current_stalls >= 3 && recovery_callback_) {
        recovery_callback_();
    }
}

bool PerFrameWatchdog::check_pipeline_health() const {
    for (size_t i = 0; i < static_cast<size_t>(PipelineStage::NUM_STAGES); ++i) {
        if (stages_[i] && stages_[i]->is_stalled()) {
            return false;
        }
    }
    return true;
}

uint64_t PerFrameWatchdog::get_total_stalls() const {
    return total_stall_count_.load(std::memory_order_acquire);
}

void PerFrameWatchdog::reset_all() {
    for (auto* stage : stages_) {
        if (stage) {
            stage->reset();
        }
    }
    total_stall_count_.store(0, std::memory_order_release);
    frame_stall_count_.store(0, std::memory_order_release);
    total_frames_.store(0, std::memory_order_release);
    healthy_frames_.store(0, std::memory_order_release);
}

void PerFrameWatchdog::set_recovery_callback(void (*callback)()) {
    recovery_callback_ = callback;
}

std::string PerFrameWatchdog::get_health_report() const {
    std::ostringstream oss;
    oss << "=== Per-Frame Watchdog Health Report ===" << std::endl;
    oss << "Total Frames: " << total_frames_.load(std::memory_order_acquire) << std::endl;
    oss << "Healthy Frames: " << healthy_frames_.load(std::memory_order_acquire) << std::endl;
    oss << "Total Stalls: " << total_stall_count_.load(std::memory_order_acquire) << std::endl;
    oss << std::endl;

    double health_pct = 0.0;
    uint64_t total = total_frames_.load(std::memory_order_acquire);
    if (total > 0) {
        health_pct = (100.0 * healthy_frames_.load(std::memory_order_acquire)) / total;
    }
    oss << "Pipeline Health: " << std::fixed << std::setprecision(2) << health_pct << "%" << std::endl;
    oss << std::endl;

    oss << "Stage Details:" << std::endl;
    for (size_t i = 0; i < static_cast<size_t>(PipelineStage::NUM_STAGES); ++i) {
        if (stages_[i]) {
            const StageWatchdog& sw = *stages_[i];
            oss << "  " << sw.name << ":" << std::endl;
            oss << "    Avg: " << std::fixed << std::setprecision(2)
                << sw.get_avg_latency_us() / 1000.0 << " ms" << std::endl;
            oss << "    Max: " << sw.get_max_latency() / 1000.0 << " ms" << std::endl;
            oss << "    Stalls: " << sw.get_stall_count() << std::endl;

            if (sw.is_stalled()) {
                oss << "    Status: STALLED" << std::endl;
            } else {
                oss << "    Status: OK" << std::endl;
            }
        }
    }

    return oss.str();
}

const char* PerFrameWatchdog::stage_name(PipelineStage stage) {
    switch (stage) {
        case PipelineStage::CAPTURE: return "Capture";
        case PipelineStage::IMAGE_PROCESSOR: return "ImageProcessor";
        case PipelineStage::INFERENCE: return "Inference";
        case PipelineStage::LOGIC: return "Logic";
        case PipelineStage::OVERLAY: return "Overlay";
        case PipelineStage::DISPLAY: return "Display";
        default: return "Unknown";
    }
}

PipelineStage PerFrameWatchdog::parse_stage(const std::string& name) {
    if (name == "Capture") return PipelineStage::CAPTURE;
    if (name == "ImageProcessor") return PipelineStage::IMAGE_PROCESSOR;
    if (name == "Inference") return PipelineStage::INFERENCE;
    if (name == "Logic") return PipelineStage::LOGIC;
    if (name == "Overlay") return PipelineStage::OVERLAY;
    if (name == "Display") return PipelineStage::DISPLAY;
    return PipelineStage::NUM_STAGES;
}

}
}
