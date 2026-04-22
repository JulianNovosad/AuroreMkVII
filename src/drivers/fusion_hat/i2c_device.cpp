#include "aurore/drivers/i2c_device.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace aurore {

I2cDevice::~I2cDevice() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

I2cDevice::I2cDevice(I2cDevice&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

I2cDevice& I2cDevice::operator=(I2cDevice&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool I2cDevice::init(const std::string& device_path, uint8_t slave_addr) {
    fd_ = ::open(device_path.c_str(), O_RDWR);
    if (fd_ < 0) {
        std::cerr << "I2cDevice: open " << device_path << " failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }
    if (::ioctl(fd_, I2C_SLAVE, static_cast<int>(slave_addr)) < 0) {
        std::cerr << "I2cDevice: ioctl I2C_SLAVE(0x" << std::hex
                  << static_cast<int>(slave_addr) << ") failed: "
                  << std::strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

std::optional<uint8_t> I2cDevice::read_byte(uint8_t reg) const {
    if (fd_ < 0) return std::nullopt;

    // Write register address, then read one byte.
    if (::write(fd_, &reg, 1) != 1) return std::nullopt;

    uint8_t val{};
    if (::read(fd_, &val, 1) != 1) return std::nullopt;

    return val;
}

std::optional<uint16_t> I2cDevice::read_word_be(uint8_t reg_h, uint8_t reg_l) const {
    auto high = read_byte(reg_h);
    if (!high) return std::nullopt;

    auto low = read_byte(reg_l);
    if (!low) return std::nullopt;

    return static_cast<uint16_t>((static_cast<uint16_t>(*high) << 8) | *low);
}

bool I2cDevice::write_byte(uint8_t reg, uint8_t val) const {
    if (fd_ < 0) return false;

    uint8_t buf[2] = {reg, val};
    return ::write(fd_, buf, sizeof(buf)) == static_cast<ssize_t>(sizeof(buf));
}

}  // namespace aurore
