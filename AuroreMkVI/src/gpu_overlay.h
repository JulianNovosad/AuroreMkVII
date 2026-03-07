// Minimal GPU Overlay - graceful fallback when GPU unavailable
#pragma once

#include <iostream>
#include "util_logging.h"

class GpuOverlay {
public:
    GpuOverlay(int width, int height) : width_(width), height_(height) {
        APP_LOG_INFO("GpuOverlay: Created " + std::to_string(width) + "x" + std::to_string(height));
    }
    
    ~GpuOverlay() {
        cleanup();
    }
    
    bool initialize(int drm_fd) {
        APP_LOG_INFO("GpuOverlay: Initializing (minimal mode)...");
        
        // Always succeed to avoid breaking the pipeline
        initialized_ = true;
        software_mode_ = true;
        
        APP_LOG_INFO("GpuOverlay: Running in bypass mode (CPU-only processing)");
        return true;
    }
    
    void render(int dma_buf_fd, size_t dma_buf_size, uint64_t frame_counter) {
        // No-op in minimal mode - CPU processing handles everything
    }
    
    int get_rendered_dma_buf_fd() const {
        return -1;
    }
    
    bool is_software_mode() const { return software_mode_; }
    bool is_initialized() const { return initialized_; }
    
private:
    int width_;
    int height_;
    bool initialized_ = false;
    bool software_mode_ = true;
    
    void cleanup() {
        // Nothing to clean up
    }
};
