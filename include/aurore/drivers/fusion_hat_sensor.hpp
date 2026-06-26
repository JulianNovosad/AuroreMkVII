#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace aurore {

// Reads sensor data from the Fusion HAT+ via kernel-exported sysfs/IIO interfaces.
//
// Data sources:
//   ADC channels 0-3  → /sys/bus/iio/devices/iio:deviceN/ (IIO subsystem)
//   Battery voltage    → /sys/class/power_supply/fusion-hat/voltage_now
//   Charging status    → /sys/class/power_supply/fusion-hat/status
//   Button state       → /sys/class/fusion_hat/fusion_hat/button
//   Firmware version   → /sys/class/fusion_hat/fusion_hat/firmware_version
//
// The kernel driver claims I2C address 0x17, so userspace cannot access
// /dev/i2c-1 directly — all reads go through sysfs.
//
// Thread-safe: all reads are stateless file operations (no shared fd).
class FusionHatSensor {
   public:
    FusionHatSensor() = default;
    ~FusionHatSensor() = default;

    FusionHatSensor(const FusionHatSensor&) = delete;
    FusionHatSensor& operator=(const FusionHatSensor&) = delete;

    // Discovers IIO device index and verifies sysfs paths exist.
    // Returns true on success.
    bool init();

    bool is_ready() const { return ready_; }

    // Read ADC channel 0-3 as voltage (V). Returns nullopt on error.
    std::optional<float> read_adc(uint8_t channel) const;

    // Read battery voltage (V). Returns nullopt on error.
    std::optional<float> read_battery_v() const;

    // Read button state: true = pressed.
    std::optional<bool> read_button() const;

    // Read charging status: true = charging.
    std::optional<bool> read_charging() const;

    // Read firmware version string (e.g. "1.0.2").
    std::string read_firmware_version() const;

   private:
    // Find the IIO device index whose name == "fusion-hat".
    int find_iio_device() const;

    // Read an integer from a sysfs file. Returns nullopt on error.
    static std::optional<int> read_sysfs_int(const std::string& path);

    // Read a string from a sysfs file. Returns "" on error.
    static std::string read_sysfs_string(const std::string& path);

    bool ready_ = false;
    int iio_device_index_ = -1;
    float adc_scale_[4] = {};  // mV per LSB, per channel
};

}  // namespace aurore
