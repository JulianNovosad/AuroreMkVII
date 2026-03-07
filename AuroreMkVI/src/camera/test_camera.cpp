#include "camera.hpp"
#include "../telemetry/telemetry.hpp"
#include "../monitor/shared_metrics.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <memory>

int main() {
    std::filesystem::create_directories("test_logs");
    aurore::monitor::SharedMetrics shared_metrics;
    auto telemetry = std::make_unique<aurore::Telemetry>("test_logs", &shared_metrics);

    aurore::Camera cam;
    if (!cam.initialize()) {
        std::cerr << "Failed to initialize camera" << std::endl;
        return 1;
    }

    cam.start();
    // std::cout << "Camera started. Waiting for frames..." << std::endl; // Removed stdout

    for (int i = 0; i < 100; ++i) {
        aurore::FrameData inf_frame;
        if (cam.get_inference_frame(inf_frame)) {
            // std::cout << "Captured Inference Frame: " << i << " FD: " << inf_frame.fd 
            //           << " TS: " << inf_frame.timestamp_ns << std::endl; // Removed stdout
            // Log this to telemetry instead if needed
        }

        aurore::FrameData enc_frame;
        if (cam.get_encoder_frame(enc_frame)) {
            // std::cout << "Captured Encoder Frame: " << i << " FD: " << enc_frame.fd 
            //           << " TS: " << enc_frame.timestamp_ns << std::endl; // Removed stdout
            // Log this to telemetry instead if needed
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    cam.stop();
    // std::cout << "Camera stopped." << std::endl; // Removed stdout
    return 0;
}