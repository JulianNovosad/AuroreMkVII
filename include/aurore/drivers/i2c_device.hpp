#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace aurore {

// RAII I2C device handle. Opens /dev/i2c-X and sets slave address.
// Non-copyable, movable. All reads/writes return std::optional<T>;
// nullopt indicates a bus or OS error.
class I2cDevice {
   public:
    I2cDevice() = default;
    ~I2cDevice();

    I2cDevice(const I2cDevice&) = delete;
    I2cDevice& operator=(const I2cDevice&) = delete;

    I2cDevice(I2cDevice&& other) noexcept;
    I2cDevice& operator=(I2cDevice&& other) noexcept;

    // Opens device_path (e.g. "/dev/i2c-1") and selects slave_addr.
    // Returns true on success.
    bool init(const std::string& device_path, uint8_t slave_addr);

    bool is_open() const { return fd_ >= 0; }

    // Read one byte from the given register.
    std::optional<uint8_t> read_byte(uint8_t reg) const;

    // Read two bytes from consecutive registers (reg_h, reg_l) and combine
    // as big-endian 16-bit value: (high << 8) | low.
    std::optional<uint16_t> read_word_be(uint8_t reg_h, uint8_t reg_l) const;

    // Write one byte to the given register.
    bool write_byte(uint8_t reg, uint8_t val) const;

   private:
    int fd_{-1};
};

}  // namespace aurore
