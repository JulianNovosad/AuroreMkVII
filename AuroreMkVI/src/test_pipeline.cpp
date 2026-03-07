#include "camera/camera.hpp"
#include "inference/inference.hpp"
#include "decision/safety_monitor.hpp"
#include "servo/servo_hardware.hpp"
#include "telemetry/telemetry.hpp"
#include "net/health_api.hpp"
#include "net/encoder.hpp"
#include "net/streamer.hpp"
#include "monitor/shared_metrics.hpp"
#include "monitor/tui_manager.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <filesystem>
#include <memory>

int main() {
    // Initialize shared metrics and TUI manager
    aurore::monitor::SharedMetrics shared_metrics;
    aurore::monitor::TuiManager tui_manager(shared_metrics);
    tui_manager.start(); // Start the TUI display thread

    // std::cout << "AURORE Mk VI - Initializing..." << std::endl;
    std::filesystem::create_directories("run_logs");
    
    // Controlled lifetime management
    auto telemetry = std::make_unique<aurore::Telemetry>("run_logs", &shared_metrics);
    auto safety = std::make_unique<aurore::SafetyMonitor>(*telemetry);
    auto servo = std::make_unique<aurore::ServoDriverHardware>(*telemetry, *safety);
    auto engine = std::make_unique<aurore::InferenceEngine>();
    auto cam = std::make_unique<aurore::Camera>();
    auto encoder = std::make_unique<aurore::Encoder>(*telemetry);
    auto streamer = std::make_unique<aurore::Streamer>();
    auto health = std::make_unique<aurore::HealthAPI>(8080);

    if (!cam->initialize()) {
        std::cerr << "Camera initialization failed" << std::endl;
        tui_manager.stop();
        return 1;
    }

    encoder->initialize(1536, 864, 60, 4000000);
    streamer->initialize("rtsp://localhost:8554/stream", 1536, 864, 60);
    health->start();

    if (!engine->load_model("/home/pi/Aurore/models/model_edgetpu.tflite")) {
        std::cerr << "Inference Engine failed to load model" << std::endl;
        tui_manager.stop();
        return 1; 
    }

    if (!servo->initialize(1, 0x40)) {
        std::cerr << "Servo initialization failed (Check I2C-1 and PCA9685)" << std::endl;
    }

    cam->start();
    
    // std::cout << "AURORE Mk VI Full Pipeline started." << std::endl;

    // Simulate some initial metrics for TUI
    {
        std::lock_guard<std::mutex> lock(shared_metrics.mutex);
        shared_metrics.cpu_temp_c.store(safety->get_cpu_temp()); // Initial temperature
        // Add other initial metric updates here if available
    }

    // FPS calculation helpers
    uint64_t last_inference_frame_id = 0;
    uint64_t last_encoder_frame_id = 0;
    auto last_fps_update_time = std::chrono::steady_clock::now();
    uint64_t inference_frames_this_second = 0;
    uint64_t encoder_frames_this_second = 0;


    for (int i = 0; i < 100; ++i) {
        safety->update();
        
        // Update CPU temp in shared metrics
        {
            std::lock_guard<std::mutex> lock(shared_metrics.mutex);
            shared_metrics.cpu_temp_c.store(safety->get_cpu_temp());
            // safety->get_cpu_usage() is an expensive call, might need to be sampled less frequently
            // shared_metrics.cpu_usage_percent.store(safety->get_cpu_usage()); 
        }

        // Update Health API
        std::string status = "{\"temp\":" + std::to_string(safety->get_cpu_temp()) + 
                             ",\"usage\":" + std::to_string(safety->get_cpu_usage()) + 
                             ",\"kill_switch\":" + (safety->get_kill_switch_engaged() ? "true" : "false") + "}";
        health->update_status(status);

        aurore::FrameData inf_frame;
        if (cam->get_inference_frame(inf_frame)) {
            inference_frames_this_second++;
            shared_metrics.camera_queue_out_count++; // Assuming this is 'out' of camera queue
            
            std::vector<float> results;
            auto start_inf = std::chrono::steady_clock::now();
            if (engine->run_inference(inf_frame.fd, inf_frame.ptr, inf_frame.size, results)) {
                auto end_inf = std::chrono::steady_clock::now();
                auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_inf - start_inf).count();
                
                if (safety->validate_actuation(1500)) {
                    servo->set_pwm(0, 1500);
                }
                
                // if (i % 10 == 0) {
                //    std::cout << "Frame " << i << " | Latency: " << latency_ms << "ms | Temp: " << safety->get_cpu_temp() << "C" << std::endl;
                // }
                // Update latency in shared metrics
                shared_metrics.inference_latency_ms.store(static_cast<float>(latency_ms));
            }
            cam->release_frame(inf_frame.buffer);
            shared_metrics.inference_queue_in_count++; // Assuming this is 'in' for inference queue
        }

        aurore::FrameData enc_frame;
        if (cam->get_encoder_frame(enc_frame)) {
            encoder_frames_this_second++;
            shared_metrics.inference_queue_out_count++; // Assuming this is 'out' of inference queue to encoder
            
            // Encode frame
            if (enc_frame.ptr && encoder->encode_frame(static_cast<const uint8_t*>(enc_frame.ptr), enc_frame.size, enc_frame.timestamp_ns)) {
                aurore::EncodedPacket pkt;
                while (encoder->get_packet(pkt)) {
                    streamer->stream_packet(pkt);
                }
            }
            cam->release_frame(enc_frame.buffer);
            shared_metrics.camera_queue_in_count++; // Assuming this is 'in' for encoder (or end of pipeline)
        }

        // Update FPS metrics periodically
        auto current_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_fps_update_time).count();
        if (duration >= 1000) { // Every second
            {
                std::lock_guard<std::mutex> lock(shared_metrics.mutex);
                shared_metrics.inference_fps.store(static_cast<float>(inference_frames_this_second) / (duration / 1000.0f));
                shared_metrics.camera_fps.store(static_cast<float>(encoder_frames_this_second) / (duration / 1000.0f)); // Using encoder frames as camera FPS
                shared_metrics.total_system_fps.store((shared_metrics.inference_fps.load() + shared_metrics.camera_fps.load()) / 2.0f); // Simple average
            }
            inference_frames_this_second = 0;
            encoder_frames_this_second = 0;
            last_fps_update_time = current_time;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    tui_manager.stop(); // Stop the TUI display thread before other components
    // std::cout << "Stopping AURORE Mk VI..." << std::endl;
    cam->stop();
    servo->safe_stop();
    encoder->stop();
    streamer->stop();
    health->stop();
    
    // std::cout << "Pipeline loop complete. Exiting main..." << std::endl;
    return 0;
}
