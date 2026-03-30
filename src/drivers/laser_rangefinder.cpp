/**
 * @file laser_rangefinder.cpp
 * @brief Laser rangefinder UART driver — M01 and Modbus RTU protocols
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include "aurore/drivers/laser_rangefinder.hpp"
#include "aurore/timing.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace aurore {

namespace {

// M01 protocol frames (little-endian checksum = sum(bytes[1..7]) & 0xFF)
constexpr uint8_t kContinuousCmd[] = {0xAA, 0x00, 0x00, 0x21, 0x00, 0x01, 0x00, 0x00, 0x22};
constexpr uint8_t kSingleShotCmd[] = {0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x21};

// Modbus RTU: Read 1 Holding Register at address 0x0000 from slave 0x01
// Frame: [addr=01] [func=03] [start_hi=00] [start_lo=00] [count_hi=00] [count_lo=01] [CRC_lo] [CRC_hi]
constexpr uint8_t kModbusPollCmd[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};

speed_t baud_to_speed(int baud) noexcept {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 115200: return B115200;
        default:     return B9600;
    }
}

}  // namespace

// ============================================================================
// Modbus CRC-16 (polynomial 0xA001, init 0xFFFF)
// ============================================================================

uint16_t LaserRangefinder::modbus_crc16(const uint8_t* data, size_t len) noexcept {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;  // Low byte first in Modbus wire format
}

// ============================================================================
// Init
// ============================================================================

bool LaserRangefinder::init(const std::string& uart_device, int baud, LrfProtocol protocol) {
    protocol_ = protocol;

    fd_ = ::open(uart_device.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd_ < 0) {
        std::cerr << "[LaserRangefinder] open(" << uart_device << ") failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }

    struct termios tty{};
    if (::tcgetattr(fd_, &tty) != 0) {
        std::cerr << "[LaserRangefinder] tcgetattr failed: " << std::strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    const speed_t spd = baud_to_speed(baud);
    ::cfsetospeed(&tty, spd);
    ::cfsetispeed(&tty, spd);

    // 8N1, no flow control, raw mode
    tty.c_cflag = (tty.c_cflag & ~static_cast<tcflag_t>(CSIZE)) | CS8;
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_lflag = 0;
    tty.c_iflag = 0;
    tty.c_oflag = 0;

    if (protocol_ == LrfProtocol::MODBUS_RTU) {
        // Modbus RTU: non-blocking reads with poll()-based timeout
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 5;   // 500ms inter-character timeout
    } else {
        // M01: return as soon as data is available (handle partial frames)
        tty.c_cc[VMIN]  = 1;
        tty.c_cc[VTIME] = 10;  // 1 second max wait for first byte
    }

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        std::cerr << "[LaserRangefinder] tcsetattr failed: " << std::strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Flush any stale data in the UART buffer
    ::tcflush(fd_, TCIOFLUSH);

    const char* proto_name = (protocol_ == LrfProtocol::MODBUS_RTU) ? "Modbus RTU" : "M01";
    std::cout << "[LaserRangefinder] UART " << uart_device << " open OK ("
              << baud << " baud, " << proto_name << ")\n";
    return true;
}

// ============================================================================
// Start / Stop
// ============================================================================

bool LaserRangefinder::start_continuous() {
    if (fd_ < 0) return false;

    if (protocol_ == LrfProtocol::MODBUS_RTU) {
        // Modbus RTU: no init command needed; polling starts in the thread
        running_.store(true, std::memory_order_release);
        reader_thread_ = std::thread(&LaserRangefinder::reader_loop_modbus, this);
        std::cout << "[LaserRangefinder] Modbus RTU polling started\n";
    } else {
        // M01: send continuous-mode command
        if (::write(fd_, kContinuousCmd, sizeof(kContinuousCmd)) !=
            static_cast<ssize_t>(sizeof(kContinuousCmd))) {
            std::cerr << "[LaserRangefinder] failed to send continuous command: "
                      << std::strerror(errno) << "\n";
            return false;
        }
        running_.store(true, std::memory_order_release);
        reader_thread_ = std::thread(&LaserRangefinder::reader_loop_m01, this);
        std::cout << "[LaserRangefinder] M01 continuous mode started\n";
    }

    return true;
}

void LaserRangefinder::stop() {
    if (!running_.load(std::memory_order_acquire)) return;

    running_.store(false, std::memory_order_release);
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }

    if (fd_ >= 0) {
        if (protocol_ == LrfProtocol::M01) {
            // Send single-shot command to halt continuous output (best-effort)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
            ::write(fd_, kSingleShotCmd, sizeof(kSingleShotCmd));
#pragma GCC diagnostic pop
        }
        ::close(fd_);
        fd_ = -1;
    }
}

float LaserRangefinder::latest_range_m() const noexcept {
    const uint32_t mm = range_mm_.load(std::memory_order_acquire);
    return mm == 0u ? 0.0f : static_cast<float>(mm) / 1000.0f;
}

// ============================================================================
// M01 reader loop (passive continuous mode)
// ============================================================================

void LaserRangefinder::reader_loop_m01() {
    uint8_t buf[64];  // Buffer large enough for max frame

    while (running_.load(std::memory_order_acquire)) {
        // Read available data (VMIN=1 means return as soon as at least 1 byte)
        ssize_t n = ::read(fd_, buf, sizeof(buf));
        if (n <= 0) continue;

        // Check frame type
        if (buf[0] == 0xEE) {
            // Status/warm-up frame (8 bytes) - ignore but don't block
            continue;
        }

        if (buf[0] != 0xAA) continue;  // Unknown frame type - skip

        // Need at least 10 bytes for distance data (bytes 8-9)
        if (n < 10) continue;

        // M01 13-byte frame layout (verified against real hardware):
        //   [0]    = 0xAA sync
        //   [1-3]  = command echo (0x00 0x00 0x21 for continuous)
        //   [4-5]  = data length (0x00 0x04 = 4 bytes)
        //   [6-7]  = status/flags
        //   [8-9]  = distance in mm, big-endian
        //   [10-11]= signal quality
        //   [12]   = checksum
        const uint32_t raw_mm = (static_cast<uint32_t>(buf[kM01DistOffset]) << 8) |
                                buf[kM01DistOffset + 1];
        static constexpr float kM01CalibrationFactor = 0.87f;
        const uint32_t mm = static_cast<uint32_t>(static_cast<float>(raw_mm) * kM01CalibrationFactor / 2.0f);
        // Sanity: 50 mm (5 cm) to 50 000 mm (50 m)
        if (mm >= 50u && mm <= 50000u) {
            range_mm_.store(mm, std::memory_order_release);
            last_ts_ns_.store(get_timestamp(ClockId::MonotonicRaw), std::memory_order_release);
        }
    }
}

// ============================================================================
// Modbus RTU reader loop (active poll/response)
// ============================================================================

void LaserRangefinder::reader_loop_modbus() {
    uint8_t resp[kModbusResponseLen];

    while (running_.load(std::memory_order_acquire)) {
        // Send poll command
        ::tcflush(fd_, TCIFLUSH);  // Discard stale RX data before polling
        const ssize_t written = ::write(fd_, kModbusPollCmd, sizeof(kModbusPollCmd));
        if (written != static_cast<ssize_t>(sizeof(kModbusPollCmd))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kModbusPollIntervalMs));
            continue;
        }

        // Wait for response with poll() — strict 500ms timeout
        struct pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;

        int ready = ::poll(&pfd, 1, 500);
        if (ready <= 0) {
            // Timeout or error — no response from LRF
            std::this_thread::sleep_for(std::chrono::milliseconds(kModbusPollIntervalMs));
            continue;
        }

        // Read response: expect exactly 7 bytes
        // [addr=01] [func=03] [byte_count=02] [data_hi] [data_lo] [crc_lo] [crc_hi]
        size_t total_read = 0;
        while (total_read < kModbusResponseLen) {
            const ssize_t n = ::read(fd_, resp + total_read,
                                     static_cast<size_t>(kModbusResponseLen) - total_read);
            if (n <= 0) break;
            total_read += static_cast<size_t>(n);
        }

        if (total_read != kModbusResponseLen) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kModbusPollIntervalMs));
            continue;
        }

        // Validate address and function code
        if (resp[0] != kModbusAddr || resp[1] != kModbusFunc) continue;

        // Validate byte count (should be 2 for one register)
        if (resp[2] != 0x02) continue;

        // Validate CRC-16 over first 5 bytes (addr + func + byte_count + 2 data bytes)
        const uint16_t calc_crc = modbus_crc16(resp, 5);
        const uint16_t recv_crc = static_cast<uint16_t>(resp[5]) |
                                  (static_cast<uint16_t>(resp[6]) << 8);
        if (calc_crc != recv_crc) continue;

        // Extract distance: bytes 3-4, big-endian, in millimetres
        const uint32_t mm = (static_cast<uint32_t>(resp[3]) << 8) | resp[4];

        // Sanity: 50 mm (5 cm) to 40 000 mm (40 m) for Modbus LRF
        if (mm >= 50u && mm <= 40000u) {
            range_mm_.store(mm, std::memory_order_release);
            last_ts_ns_.store(get_timestamp(ClockId::MonotonicRaw), std::memory_order_release);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kModbusPollIntervalMs));
    }
}

// ============================================================================
// Wiring diagnostic
// ============================================================================

int LaserRangefinder::diagnose_wiring() {
    if (fd_ < 0) return 1;

    // Flush stale UART buffers
    ::tcflush(fd_, TCIOFLUSH);

    // Send a probe command to trigger a response
    // For M01: send continuous mode command which keeps LRF active
    // For Modbus: send poll command
    const uint8_t* cmd;
    size_t cmd_len;
    if (protocol_ == LrfProtocol::MODBUS_RTU) {
        cmd = kModbusPollCmd;
        cmd_len = sizeof(kModbusPollCmd);
    } else {
        // M01: send continuous mode command to keep LRF streaming
        cmd = kContinuousCmd;
        cmd_len = sizeof(kContinuousCmd);
    }

    // Write command and check for response
    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    // Wait for any pending data to clear after flush
    (void)poll(&pfd, 1, 50);

    ssize_t written = ::write(fd_, cmd, cmd_len);
    if (written != static_cast<ssize_t>(cmd_len)) {
        // TX failure - try reading anyway in case LRF is in continuous mode
    }

    // Give LRF time to process command and start responding
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait up to 2000ms for response (M01 needs time to start streaming)
    int ready = ::poll(&pfd, 1, 2000);
    if (ready <= 0) {
        return 1;  // No response — TX/RX swap or disconnected
    }

    // Read whatever came back
    uint8_t buf[32];
    const ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n <= 0) {
        return 1;  // Read failed
    }

    // Check if response looks valid for the protocol
    if (protocol_ == LrfProtocol::MODBUS_RTU) {
        if (n < kModbusResponseLen) return 2;  // Too short — baud mismatch
        if (buf[0] != kModbusAddr || buf[1] != kModbusFunc) return 3;  // Wrong protocol
        return 0;  // Looks valid
    } else {
        // 0xAA = data frame (13 bytes), 0xEE = status/warm-up frame (8 bytes)
        // Both are valid M01 responses
        if (n < 8) return 2;     // Too short — baud mismatch
        if (buf[0] != 0xAA && buf[0] != 0xEE) return 3;  // Wrong sync — wrong protocol
        return 0;  // Looks valid
    }
}

}  // namespace aurore
