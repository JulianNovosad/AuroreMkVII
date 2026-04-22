#include "aurore/drivers/fusion_hat_sensor.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace aurore {

static constexpr const char* kIioPrefix = "/sys/bus/iio/devices/iio:device";
static constexpr const char* kBatteryBase = "/sys/class/power_supply/fusion-hat";
static constexpr const char* kDeviceBase = "/sys/class/fusion_hat/fusion_hat";

int FusionHatSensor::find_iio_device() const {
    for (int i = 0; i < 10; ++i) {
        std::string name_path = std::string(kIioPrefix) + std::to_string(i) + "/name";
        auto name = read_sysfs_string(name_path);
        if (name == "fusion-hat") {
            return i;
        }
    }
    return -1;
}

bool FusionHatSensor::init() {
    // Find IIO device
    iio_device_index_ = find_iio_device();
    if (iio_device_index_ < 0) {
        std::cerr << "FusionHatSensor: IIO device 'fusion-hat' not found" << std::endl;
        return false;
    }

    // Read per-channel ADC scale factors
    std::string iio_base = std::string(kIioPrefix) + std::to_string(iio_device_index_);
    for (int ch = 0; ch < 4; ++ch) {
        std::string scale_path = iio_base + "/in_voltage" + std::to_string(ch) + "_scale";
        std::string scale_str = read_sysfs_string(scale_path);
        if (scale_str.empty()) {
            std::cerr << "FusionHatSensor: cannot read ADC scale for channel " << ch << std::endl;
            return false;
        }
        adc_scale_[ch] = std::stof(scale_str);
    }

    // Verify battery sysfs exists
    std::string voltage_path = std::string(kBatteryBase) + "/voltage_now";
    if (read_sysfs_string(voltage_path).empty()) {
        std::cerr << "FusionHatSensor: battery sysfs not available" << std::endl;
        return false;
    }

    ready_ = true;
    return true;
}

std::optional<float> FusionHatSensor::read_adc(uint8_t channel) const {
    if (channel > 3 || !ready_) return std::nullopt;

    std::string iio_base = std::string(kIioPrefix) + std::to_string(iio_device_index_);
    std::string raw_path = iio_base + "/in_voltage" + std::to_string(channel) + "_raw";

    auto raw = read_sysfs_int(raw_path);
    if (!raw) return std::nullopt;

    // Voltage in V = raw * scale_mV / 1000
    return static_cast<float>(*raw) * adc_scale_[channel] / 1000.0f;
}

std::optional<float> FusionHatSensor::read_battery_v() const {
    if (!ready_) return std::nullopt;

    std::string path = std::string(kBatteryBase) + "/voltage_now";
    auto uv = read_sysfs_int(path);
    if (!uv) return std::nullopt;

    // voltage_now is in microvolts
    return static_cast<float>(*uv) / 1000000.0f;
}

std::optional<bool> FusionHatSensor::read_button() const {
    if (!ready_) return std::nullopt;

    std::string path = std::string(kDeviceBase) + "/button";
    auto val = read_sysfs_int(path);
    if (!val) return std::nullopt;

    return *val != 0;
}

std::optional<bool> FusionHatSensor::read_charging() const {
    if (!ready_) return std::nullopt;

    std::string path = std::string(kBatteryBase) + "/status";
    std::string status = read_sysfs_string(path);
    if (status.empty()) return std::nullopt;

    return status == "Charging";
}

std::string FusionHatSensor::read_firmware_version() const {
    std::string path = std::string(kDeviceBase) + "/firmware_version";
    return read_sysfs_string(path);
}

std::optional<int> FusionHatSensor::read_sysfs_int(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    int value;
    if (!(file >> value)) return std::nullopt;
    return value;
}

std::string FusionHatSensor::read_sysfs_string(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";

    std::string value;
    std::getline(file, value);

    // Trim trailing whitespace/newlines
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

}  // namespace aurore
