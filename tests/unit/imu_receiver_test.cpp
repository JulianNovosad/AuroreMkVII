#include "aurore/imu_receiver.hpp"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    aurore::ImuReceiverConfig config;
    config.bind_address = "0.0.0.0";  // Listen on all interfaces
    config.udp_port = 7070;

    aurore::ImuReceiver receiver(config);

    if (!receiver.init()) {
        std::cerr << "Failed to init IMU receiver\n";
        return 1;
    }

    std::cout << "IMU Receiver initialized, starting...\n";
    receiver.start();

    // Wait for data
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto data = receiver.get_latest_data();

        std::cout << "Packets: " << data.packets_received
                  << ", Accel: " << (data.accel_valid ? "valid" : "invalid")
                  << ", Gyro: " << (data.gyro_valid ? "valid" : "invalid")
                  << ", Orient: " << (data.orientation_valid ? "valid" : "invalid") << "\n";

        if (data.accel_valid && data.gyro_valid) {
            std::cout << "SUCCESS: Got IMU data!\n";
            std::cout << "  Accel: " << data.accel_x << ", " << data.accel_y << ", " << data.accel_z
                      << "\n";
            std::cout << "  Gyro: " << data.gyro_x << ", " << data.gyro_y << ", " << data.gyro_z
                      << "\n";
            break;
        }
    }

    receiver.stop();
    return 0;
}
