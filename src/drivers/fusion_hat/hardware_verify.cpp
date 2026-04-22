/**
 * @file hardware_verify.cpp
 * @brief Standalone diagnostic utility for Fusion HAT+ sensor hardware
 *
 * Reads battery voltage, ADC channels, firmware version, button, and
 * charging status via sysfs/IIO and prints a human-readable report.
 *
 * Usage (on RPi 5 with HAT attached):
 *   sudo ./hardware_verify
 *
 * Exit codes:
 *   0   — all reads succeeded
 *   1   — sysfs init failed
 */

#include <cstdlib>
#include <iostream>

#include "aurore/drivers/fusion_hat_sensor.hpp"

static constexpr float kBatteryMinV = 6.4f;

int main() {
    aurore::FusionHatSensor sensor;
    if (!sensor.init()) {
        std::cerr << "FATAL: FusionHatSensor init failed — sysfs not available\n";
        return 1;
    }

    // Firmware version
    std::string fw = sensor.read_firmware_version();
    if (!fw.empty()) {
        std::cout << "FW: v" << fw << "\n";
    } else {
        std::cerr << "FW: read failed\n";
    }

    // Battery voltage
    if (auto batt = sensor.read_battery_v()) {
        const char* status = (*batt >= kBatteryMinV) ? "OK" : "LOW";
        std::printf("Battery: %.3f V [%s]\n", static_cast<double>(*batt), status);
    } else {
        std::cerr << "Battery: read failed\n";
    }

    // ADC channels 0-3
    for (uint8_t ch = 0; ch < 4; ++ch) {
        if (auto v = sensor.read_adc(ch)) {
            std::printf("ADC[%d]: %.3f V\n", ch, static_cast<double>(*v));
        } else {
            std::printf("ADC[%d]: read failed\n", ch);
        }
    }

    // Button state
    if (auto btn = sensor.read_button()) {
        std::cout << "Button: " << (*btn ? "pressed" : "released") << "\n";
    } else {
        std::cerr << "Button: read failed\n";
    }

    // Charging status
    if (auto chg = sensor.read_charging()) {
        std::cout << "Charging: " << (*chg ? "yes" : "no") << "\n";
    } else {
        std::cerr << "Charging: read failed\n";
    }

    return 0;
}
