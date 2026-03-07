// test_display_only_feed.cpp
// Test 1: Live feed from camera only
#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <unistd.h>

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
    std::cout << "Test 1: Camera Feed Only" << std::endl;
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize logger
    ::aurore::logging::Logger::init("test_display", "logs", nullptr);
    ::aurore::logging::Logger::getInstance().start_writer_thread();
    
    APP_LOG_INFO("Starting Camera Feed Only Test");
    
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
    
    std::cout << "Camera started, displaying feed..." << std::endl;
    std::cout << "Press Ctrl+C to exit" << std::endl;
    
    // Main loop - get frames from camera and display them
    int frame_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (g_running) {
        // Try to get a frame from the camera
        ImageData* frame_data = nullptr;
        if (output_queue->try_pop(frame_data)) {
            if (frame_data && frame_data->buffer && !frame_data->buffer->data.empty()) {
                // Render the frame to display
                display.render_frame(
                    frame_data->buffer->data.data(),
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