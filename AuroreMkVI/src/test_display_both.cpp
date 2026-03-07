// test_display_both.cpp
// Test 3: GPU overlay and live feed
#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <unistd.h>
#include <cmath> // For abs function

#include "fbdev_display.h"
#include "camera/camera_capture.h"
#include "pipeline_structs.h"
#include "util_logging.h"
#include "config_loader.h"
#include "buffer_pool.h"

std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

int main() {
    std::cout << "Test 3: GPU Overlay and Live Feed" << std::endl;
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize logger
    ::aurore::logging::Logger::init("test_both", "logs", nullptr);
    ::aurore::logging::Logger::getInstance().start_writer_thread();
    
    APP_LOG_INFO("Starting GPU Overlay and Live Feed Test");
    
    // Initialize configuration
    ConfigLoader config_loader;
    if (!config_loader.load("config.json")) {
        std::cerr << "Failed to load config.json" << std::endl;
        return 1;
    }
    
    // Get camera dimensions from config
    unsigned int cam_w = config_loader.get_camera_width();
    unsigned int cam_h = config_loader.get_camera_height();
    unsigned int cam_fps = config_loader.get_camera_fps();
    
    std::cout << "Camera resolution: " << cam_w << "x" << cam_h << "@" << cam_fps << "fps" << std::endl;
    
    // Create framebuffer display
    FbdevDisplay display(nullptr); // No application reference for this test
    if (!display.initialize(cam_w, cam_h)) {
        std::cerr << "Failed to initialize FbdevDisplay" << std::endl;
        return 1;
    }
    
    // Create buffer pools
    auto buffer_pool = std::make_shared<BufferPool<uint8_t>>(50, cam_w * cam_h * 3, "TestImagePool");
    auto image_data_pool = std::make_shared<ObjectPool<ImageData>>(50, "TestDataPool");
    
    // Create queues for camera output
    auto output_queue = std::make_shared<ImageQueue>();
    
    // Create camera capture
    CameraCapture camera(
        cam_w, cam_h, // Main stream width/height
        cam_w, cam_h, // TPU stream width/height (same as main for this test)
        cam_fps,
        cam_w, cam_h, // Target TPU width/height (same as main for this test)
        buffer_pool, image_data_pool,
        *output_queue,
        config_loader.get_camera_watchdog_timeout()
    );
    
    if (!camera.start()) {
        std::cerr << "Failed to start camera" << std::endl;
        return 1;
    }
    
    std::cout << "Camera started, displaying feed with overlays..." << std::endl;
    std::cout << "Press Ctrl+C to exit" << std::endl;
    
    // Main loop - get frames from camera and display them with overlays
    int frame_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (g_running) {
        // Try to get a frame from the camera
        ImageData* frame_data = nullptr;
        if (output_queue->try_pop(frame_data)) {
            if (frame_data && frame_data->buffer && !frame_data->buffer->data.empty()) {
                // Create a copy of the frame data to add overlays to
                std::vector<uint8_t> frame_with_overlays = frame_data->buffer->data;
                
                // Add synthetic overlay elements to the frame
                // Crosshair at center
                int center_x = cam_w / 2;
                int center_y = cam_h / 2;
                
                // Horizontal line (red)
                for (int x = 0; x < cam_w; x++) {
                    if (std::abs(x - center_x) < 50) { // Only draw center portion
                        int idx = (center_y * cam_w + x) * 3;
                        if (idx + 2 < frame_with_overlays.size()) {
                            frame_with_overlays[idx] = 0;     // B
                            frame_with_overlays[idx + 1] = 0; // G  
                            frame_with_overlays[idx + 2] = 255; // R (Red)
                        }
                    }
                }
                
                // Vertical line (red)
                for (int y = 0; y < cam_h; y++) {
                    if (std::abs(y - center_y) < 50) { // Only draw center portion
                        int idx = (y * cam_w + center_x) * 3;
                        if (idx + 2 < frame_with_overlays.size()) {
                            frame_with_overlays[idx] = 0;     // B
                            frame_with_overlays[idx + 1] = 0; // G
                            frame_with_overlays[idx + 2] = 255; // R (Red)
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
                            if (x >= 0 && x < cam_w && y >= 0 && y < cam_h) {
                                int idx = (y * cam_w + x) * 3;
                                if (idx + 2 < frame_with_overlays.size()) {
                                    frame_with_overlays[idx] = 0;       // B
                                    frame_with_overlays[idx + 1] = 165; // G (Orange)
                                    frame_with_overlays[idx + 2] = 255; // R (Orange)
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
                for (int x = box_x; x < box_x + box_w && x < cam_w; x++) {
                    if (x >= 0) {
                        // Top line
                        int top_idx = (box_y * cam_w + x) * 3;
                        if (box_y >= 0 && box_y < cam_h && top_idx + 2 < frame_with_overlays.size()) {
                            frame_with_overlays[top_idx] = 0;     // B
                            frame_with_overlays[top_idx + 1] = 0; // G
                            frame_with_overlays[top_idx + 2] = 255; // R (Red)
                        }
                        
                        // Bottom line
                        int bot_idx = ((box_y + box_h) * cam_w + x) * 3;
                        if (box_y + box_h >= 0 && box_y + box_h < cam_h && bot_idx + 2 < frame_with_overlays.size()) {
                            frame_with_overlays[bot_idx] = 0;     // B
                            frame_with_overlays[bot_idx + 1] = 0; // G
                            frame_with_overlays[bot_idx + 2] = 255; // R (Red)
                        }
                    }
                }
                
                // Left and right lines
                for (int y = box_y; y < box_y + box_h && y < cam_h; y++) {
                    if (y >= 0) {
                        // Left line
                        int left_idx = (y * cam_w + box_x) * 3;
                        if (box_x >= 0 && box_x < cam_w && left_idx + 2 < frame_with_overlays.size()) {
                            frame_with_overlays[left_idx] = 0;     // B
                            frame_with_overlays[left_idx + 1] = 0; // G
                            frame_with_overlays[left_idx + 2] = 255; // R (Red)
                        }
                        
                        // Right line
                        int right_idx = (y * cam_w + box_x + box_w) * 3;
                        if (box_x + box_w >= 0 && box_x + box_w < cam_w && right_idx + 2 < frame_with_overlays.size()) {
                            frame_with_overlays[right_idx] = 0;     // B
                            frame_with_overlays[right_idx + 1] = 0; // G
                            frame_with_overlays[right_idx + 2] = 255; // R (Red)
                        }
                    }
                }
                
                // Render the frame with overlays to display
                display.render_frame(
                    frame_with_overlays.data(),
                    frame_data->width,
                    frame_data->height
                );
                
                frame_count++;
                
                // Log frame rate periodically
                if (frame_count % 100 == 0) {
                    auto current_time = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
                    double fps = (frame_count * 1000.0) / elapsed;
                    std::cout << "Frame rate: " << fps << " FPS" << std::endl;
                }
                
                // Release the frame data
                if (frame_data) {
                    if (frame_data->buffer) {
                        frame_data->buffer.reset();
                    }
                    image_data_pool->release(frame_data);
                }
            }
        } else {
            // Small delay to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    std::cout << "Shutting down..." << std::endl;
    
    camera.stop();
    display.cleanup();
    
    ::aurore::logging::Logger::getInstance().stop_writer_thread();
    
    std::cout << "Test completed." << std::endl;
    return 0;
}