// test_display_only_overlay.cpp
// Test 2: GPU overlay only (crosshair, lines, synthetic elements)
#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <unistd.h>
#include <cstring>
#include <cmath> // For abs function

#include "fbdev_display.h"
#include "pipeline_structs.h"
#include "util_logging.h"
#include "config_loader.h"

std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

int main() {
    std::cout << "Test 2: GPU Overlay Only" << std::endl;
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize logger
    ::aurore::logging::Logger::init("test_overlay", "logs", nullptr);
    ::aurore::logging::Logger::getInstance().start_writer_thread();
    
    APP_LOG_INFO("Starting GPU Overlay Only Test");
    
    // Initialize configuration
    ConfigLoader config_loader;
    if (!config_loader.load("config.json")) {
        std::cerr << "Failed to load config.json" << std::endl;
        return 1;
    }
    
    // Get display dimensions from config or defaults
    unsigned int display_w = config_loader.get_camera_width();
    unsigned int display_h = config_loader.get_camera_height();
    
    std::cout << "Display resolution: " << display_w << "x" << display_h << std::endl;
    
    // Create framebuffer display
    FbdevDisplay display(nullptr); // No application reference for this test
    if (!display.initialize(display_w, display_h)) {
        std::cerr << "Failed to initialize FbdevDisplay" << std::endl;
        return 1;
    }
    
    std::cout << "Display initialized, rendering overlay elements..." << std::endl;
    std::cout << "Press Ctrl+C to exit" << std::endl;
    
    // Create a dummy frame buffer to render over
    std::vector<uint8_t> dummy_frame(display_w * display_h * 3, 0); // Black frame
    
    int frame_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (g_running) {
        // Draw synthetic overlay elements on the dummy frame
        // Crosshair at center
        int center_x = display_w / 2;
        int center_y = display_h / 2;
        
        // Horizontal line (red)
        for (int x = 0; x < display_w; x++) {
            if (std::abs(x - center_x) < 50) { // Only draw center portion
                int idx = (center_y * display_w + x) * 3;
                if (idx + 2 < dummy_frame.size()) {
                    dummy_frame[idx] = 0;     // B
                    dummy_frame[idx + 1] = 0; // G  
                    dummy_frame[idx + 2] = 255; // R (Red)
                }
            }
        }
        
        // Vertical line (red)
        for (int y = 0; y < display_h; y++) {
            if (std::abs(y - center_y) < 50) { // Only draw center portion
                int idx = (y * display_w + center_x) * 3;
                if (idx + 2 < dummy_frame.size()) {
                    dummy_frame[idx] = 0;     // B
                    dummy_frame[idx + 1] = 0; // G
                    dummy_frame[idx + 2] = 255; // R (Red)
                }
            }
        }
        
        // Circle (orange ballistic point)
        int radius = 10;
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx*dx + dy*dy <= radius*radius) {
                    int x = center_x + dx;
                    int y = center_y + dy;
                    if (x >= 0 && x < display_w && y >= 0 && y < display_h) {
                        int idx = (y * display_w + x) * 3;
                        if (idx + 2 < dummy_frame.size()) {
                            dummy_frame[idx] = 0;       // B
                            dummy_frame[idx + 1] = 165; // G (Orange)
                            dummy_frame[idx + 2] = 255; // R (Orange)
                        }
                    }
                }
            }
        }
        
        // Draw a target box (red)
        int box_x = center_x - 60;
        int box_y = center_y - 60;
        int box_w = 120;
        int box_h = 120;
        
        // Top and bottom lines
        for (int x = box_x; x < box_x + box_w && x < display_w; x++) {
            if (x >= 0) {
                // Top line
                int top_idx = (box_y * display_w + x) * 3;
                if (box_y >= 0 && box_y < display_h && top_idx + 2 < dummy_frame.size()) {
                    dummy_frame[top_idx] = 0;     // B
                    dummy_frame[top_idx + 1] = 0; // G
                    dummy_frame[top_idx + 2] = 255; // R (Red)
                }
                
                // Bottom line
                int bot_idx = ((box_y + box_h) * display_w + x) * 3;
                if (box_y + box_h >= 0 && box_y + box_h < display_h && bot_idx + 2 < dummy_frame.size()) {
                    dummy_frame[bot_idx] = 0;     // B
                    dummy_frame[bot_idx + 1] = 0; // G
                    dummy_frame[bot_idx + 2] = 255; // R (Red)
                }
            }
        }
        
        // Left and right lines
        for (int y = box_y; y < box_y + box_h && y < display_h; y++) {
            if (y >= 0) {
                // Left line
                int left_idx = (y * display_w + box_x) * 3;
                if (box_x >= 0 && box_x < display_w && left_idx + 2 < dummy_frame.size()) {
                    dummy_frame[left_idx] = 0;     // B
                    dummy_frame[left_idx + 1] = 0; // G
                    dummy_frame[left_idx + 2] = 255; // R (Red)
                }
                
                // Right line
                int right_idx = (y * display_w + box_x + box_w) * 3;
                if (box_x + box_w >= 0 && box_x + box_w < display_w && right_idx + 2 < dummy_frame.size()) {
                    dummy_frame[right_idx] = 0;     // B
                    dummy_frame[right_idx + 1] = 0; // G
                    dummy_frame[right_idx + 2] = 255; // R (Red)
                }
            }
        }
        
        // Render the frame with overlays to display
        display.render_frame(
            dummy_frame.data(),
            display_w,
            display_h
        );
        
        frame_count++;
        
        // Log frame rate periodically
        if (frame_count % 100 == 0) {
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
            double fps = (frame_count * 1000.0) / elapsed;
            std::cout << "Frame rate: " << fps << " FPS" << std::endl;
        }
        
        // Small delay to control frame rate
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
    }
    
    std::cout << "Shutting down..." << std::endl;
    
    display.cleanup();
    
    ::aurore::logging::Logger::getInstance().stop_writer_thread();
    
    std::cout << "Test completed." << std::endl;
    return 0;
}