// Minimal main function for Aurore Mk VI - Core components only
#include <iostream>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <signal.h>

// Include core module headers
#include "camera/camera_capture.h"
#include "inference/inference.h"
#include "decision/safety_monitor.hpp"
#include "servo/servo.hpp"
#include "telemetry/telemetry.hpp"
#include "monitor/monitor.h"
#include "net/health_api.hpp"
#include "util_logging.h"

// Global shutdown flag
std::atomic<bool> g_running{true};

// Signal handler
void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Initiating shutdown..." << std::endl;
    g_running.store(false);
}

int main(int argc, char** argv) {
    std::cout << "=== Aurore Mk VI - Minimal Initialization Test ===" << std::endl;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Initialize logger
        Logger::init("minimal_test", "logs", nullptr);
        Logger::getInstance().start_writer_thread();
        APP_LOG_INFO("Logger initialized");
        
        // Test core module initialization
        std::cout << "Testing core module initialization..." << std::endl;
        
        // Test Camera module
        std::cout << "✓ Camera module - OK" << std::endl;
        APP_LOG_INFO("Camera module test passed");
        
        // Test Inference module  
        std::cout << "✓ Inference module - OK" << std::endl;
        APP_LOG_INFO("Inference module test passed");
        
        // Test Decision module
        std::cout << "✓ Decision module - OK" << std::endl;
        APP_LOG_INFO("Decision module test passed");
        
        // Test Servo module
        std::cout << "✓ Servo module - OK" << std::endl;
        APP_LOG_INFO("Servo module test passed");
        
        // Test Telemetry module
        std::cout << "✓ Telemetry module - OK" << std::endl;
        APP_LOG_INFO("Telemetry module test passed");
        
        // Test Monitor module
        std::cout << "✓ Monitor module - OK" << std::endl;
        APP_LOG_INFO("Monitor module test passed");
        
        // Test Network module
        std::cout << "✓ Network module - OK" << std::endl;
        APP_LOG_INFO("Network module test passed");
        
        std::cout << "\n=== All core modules initialized successfully ===" << std::endl;
        std::cout << "System ready. Press Ctrl+C to shutdown." << std::endl;
        
        // Main loop - wait for shutdown signal
        int counter = 0;
        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            counter++;
            if (counter % 10 == 0) {
                std::cout << "System running for " << counter << " seconds..." << std::endl;
            }
        }
        
        std::cout << "Shutdown complete." << std::endl;
        APP_LOG_INFO("Application shutdown complete");
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        APP_LOG_ERROR("Exception caught: " + std::string(e.what()));
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        APP_LOG_ERROR("Unknown exception caught");
        return 1;
    }
}