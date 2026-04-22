/**
 * @file laser_rangefinder.cpp
 * @brief Laser rangefinder UART driver — M01 and Modbus RTU protocols
 *
 * M01 Protocol (Liancheng Electronics 50m module):
 *   - Continuous mode response: 0xAA header, distance at bytes 7-8 (big-endian, mm)
 *   - Checksum: sum(bytes[1..N-1]) & 0xFF (last byte)
 *   - Reference: github.com/Andres-ros/laser-m01-esp32
 *
 * Modbus RTU:
 *   - Poll: 01 03 00 00 00 01 84 0A (Read Holding Register 0x0000)
 *   - Response: [addr][func][byte_count][data_hi][data_lo][crc_lo][crc_hi]
 *   - Distance in bytes 3-4, big-endian, millimeters
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

// M01 protocol frames (checksum = sum(bytes[1..N-1]) & 0xFF)
// Per official M01 FAQ: must send Laser ON before continuous mode will return distance data
// Binary commands (work with some M01 variants)
constexpr uint8_t kLaserOnCmd[] = {0xAA, 0x00, 0x01, 0xBE, 0x00, 0x01, 0x00, 0x01, 0xC1};
constexpr uint8_t kContinuousCmd[] = {0xAA, 0x00, 0x00, 0x21, 0x00, 0x01, 0x00, 0x00, 0x22};
constexpr uint8_t kSingleShotCmd[] = {0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x21};

// ASCII commands per M01 spec (required for most modules)
// Sequence: CR → "L\r" → "D\r" → continuous data frames
constexpr char kLaserOnAscii[] = "L\r";
constexpr char kContinuousAscii[] = "D\r";
constexpr char kSingleShotAscii[] = "Q\r";
constexpr char kWakeupAscii[] = "\r";

// Modbus RTU: Read 1 Holding Register at address 0x0000 from slave 0x01
// Frame: [addr=01] [func=03] [start_hi=00] [start_lo=00] [count_hi=00] [count_lo=01] [CRC_lo] [CRC_hi]
constexpr uint8_t kModbusPollCmd[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};

// M01 frame reassembly ring buffer size (holds ~4 frames worth of data)
constexpr size_t kM01RingBufSize = 64;

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
// M01 checksum: sum(bytes[1..N-1]) & 0xFF
// ============================================================================

uint8_t LaserRangefinder::m01_checksum(const uint8_t* data, size_t len) noexcept {
    uint8_t sum = 0;
    for (size_t i = 1; i < len - 1; ++i) {
        sum += data[i];
    }
    return sum;
}

// ============================================================================
// M01 BCD distance decoder: 4 bytes = 8 decimal digits, result in millimeters
// Example: 0x00 0x00 0x12 0x34 -> "00001234" -> 12340 mm (12.34m)
// ============================================================================

uint32_t LaserRangefinder::m01_bcd_to_mm(const uint8_t* bcd) noexcept {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = value * 100 + ((bcd[i] >> 4) & 0x0F) * 10 + (bcd[i] & 0x0F);
    }
    // BCD value is in centimeters (2 decimal places), convert to millimeters
    return value * 10;
}

// ============================================================================
// M01 frame parser — find and validate a complete frame in the buffer
// ============================================================================

size_t LaserRangefinder::parse_m01_frame(const uint8_t* buf, size_t len, uint32_t& out_mm) {
    out_mm = 0;
    
    // Scan for sync byte
    for (size_t i = 0; i < len; ++i) {
        const uint8_t sync = buf[i];
        
        // Show every sync byte found
        if (sync != 0xAA && sync != 0xEE) {
            continue;
        }

        // Found 0xAA sync — check if we have enough bytes for minimum frame (9 bytes)
        const size_t remaining = len - i;
        if (remaining < static_cast<size_t>(kM01MinFrameLen)) {
            return i;  // Partial frame; consume everything before the sync byte
        }

        const uint8_t* frame = buf + i;

        // Handle 0xEE frames - some M01 variants use this as data frame sync
        if (sync == 0xEE) {
            // Validate checksum
            const uint8_t expected_ck = m01_checksum(frame, kM01MinFrameLen);
            const uint8_t actual_ck = frame[kM01MinFrameLen - 1];

            if (expected_ck == actual_ck) {
                // Bytes 5-6 are 4-nibble BCD in centimetres.
                // Example: 0x01 0x00 → nibbles 0,1,0,0 → 100 cm → 1000 mm (1.0 m)
                // Bug was: only 3 nibbles were read (missing d_lo & 0x0F),
                //          turning 100 cm into 10 cm (off by 10×).
                const uint8_t d_hi = frame[5];
                const uint8_t d_lo = frame[6];
                const uint32_t dist_cm =
                    (static_cast<uint32_t>((d_hi >> 4) & 0x0F) * 1000u) +
                    (static_cast<uint32_t>( d_hi        & 0x0F) * 100u)  +
                    (static_cast<uint32_t>((d_lo >> 4) & 0x0F) * 10u)   +
                    (static_cast<uint32_t>( d_lo        & 0x0F));
                out_mm = dist_cm * 10u;  // cm → mm

                frames_received_.fetch_add(1, std::memory_order_relaxed);
                return i + kM01MinFrameLen;
            }
            // Invalid 0xEE frame - skip
            return i + 1;
        }

        // For 0xAA frames, try 13-byte format FIRST (contains actual distance data)
        if (remaining >= static_cast<size_t>(kM01MaxFrameLen)) {
            const uint8_t expected_ck_13 = m01_checksum(frame, kM01MaxFrameLen);
            const uint8_t actual_ck_13 = frame[kM01MaxFrameLen - 1];

            if (expected_ck_13 == actual_ck_13 && frame[4] == 0x00 && frame[5] == 0x04) {
                // Valid 13-byte frame - extract distance from BCD bytes 8-9
                const uint8_t dist_bcd_hi = frame[8];
                const uint8_t dist_bcd_lo = frame[9];

                uint32_t dist_mm_raw = 0;
                dist_mm_raw = (static_cast<uint32_t>((dist_bcd_hi >> 4) & 0x0F) * 1000u) +
                              (static_cast<uint32_t>(dist_bcd_hi & 0x0F) * 100u) +
                              (static_cast<uint32_t>((dist_bcd_lo >> 4) & 0x0F) * 10u) +
                              (static_cast<uint32_t>(dist_bcd_lo & 0x0F));
                out_mm = dist_mm_raw;

                frames_received_.fetch_add(1, std::memory_order_relaxed);
                return i + kM01MaxFrameLen;
            }
        }

        // Skip 9-byte command echoes (bytes 4-5 = 0x00 0x01)
        if (frame[4] == 0x00 && frame[5] == 0x01) {
            return i + kM01MinFrameLen;
        }

        // Try 9-byte format for non-echo frames
        const uint8_t expected_ck_9 = m01_checksum(frame, kM01MinFrameLen);
        const uint8_t actual_ck_9 = frame[kM01MinFrameLen - 1];

        if (expected_ck_9 == actual_ck_9 && frame[4] == 0x00 && frame[5] != 0x00) {
            // Valid 9-byte frame - extract distance from BCD bytes 5-6
            const uint8_t dist_bcd_hi = frame[5];
            const uint8_t dist_bcd_lo = frame[6];

            uint32_t dist_mm_raw = 0;
            dist_mm_raw = (static_cast<uint32_t>((dist_bcd_hi >> 4) & 0x0F) * 1000u) +
                          (static_cast<uint32_t>(dist_bcd_hi & 0x0F) * 100u) +
                          (static_cast<uint32_t>((dist_bcd_lo >> 4) & 0x0F) * 10u) +
                          (static_cast<uint32_t>(dist_bcd_lo & 0x0F));
            out_mm = dist_mm_raw;  // Already in millimeters

            frames_received_.fetch_add(1, std::memory_order_relaxed);
            return i + kM01MinFrameLen;
        }

        // Invalid frame - skip this sync byte
        frame_errors_.fetch_add(1, std::memory_order_relaxed);
        return i + 1;
    }

    // No valid sync byte found — discard entire buffer
    return len;
}

// ============================================================================
// Init
// ============================================================================

bool LaserRangefinder::init(const std::string& uart_device, int baud, LrfProtocol protocol) {
    protocol_ = protocol;

    fd_ = ::open(uart_device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        std::cerr << "FAIL: LRF UART open(" << uart_device << ") failed: "
                  << std::strerror(errno) << "\n"
                  << " Check: UART device path and permissions.\n"
                  << " Fix: Verify /dev/ttyAMA* exists and user is in dialout group.\n";
        return false;
    }

    // Clear O_NONBLOCK after open (we use poll() for timeout control)
    int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);
    }

    struct termios tty{};
    if (::tcgetattr(fd_, &tty) != 0) {
        std::cerr << "FAIL: LRF tcgetattr failed: " << std::strerror(errno) << "\n"
                  << " Check: UART device is a valid serial port.\n"
                  << " Fix: Verify Fusion Hat+ UART jumpers and power stability.\n";
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

    // Fully raw mode — no signal processing, no echo, no canonical
    tty.c_lflag = 0;
    tty.c_iflag = 0;
    tty.c_oflag = 0;

    // Non-blocking reads: VMIN=0, VTIME=1 (100ms timeout)
    // Actual timeout control is via poll() in the reader loops
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        std::cerr << "FAIL: LRF tcsetattr failed: " << std::strerror(errno) << "\n"
                  << " Check: UART TX/RX integrity and " << baud << " baud lock.\n"
                  << " Fix: Verify Fusion Hat+ UART jumpers and power stability.\n";
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Flush any stale data in both TX and RX buffers
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

    // Reset diagnostic counters
    frames_received_.store(0, std::memory_order_relaxed);
    status_frames_.store(0, std::memory_order_relaxed);
    crc_errors_.store(0, std::memory_order_relaxed);
    frame_errors_.store(0, std::memory_order_relaxed);

    if (protocol_ == LrfProtocol::MODBUS_RTU) {
        // Modbus RTU: no init command needed; polling starts in the thread
        running_.store(true, std::memory_order_release);
        reader_thread_ = std::thread(&LaserRangefinder::reader_loop_modbus, this);
        std::cout << "[LaserRangefinder] Modbus RTU polling started\n";
    } else {
        // M01: use ASCII commands per M01 specification
        // Required sequence: wake up → L → D → continuous data
        ::tcflush(fd_, TCIOFLUSH);

        // Wake up the module with CR
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
        ::write(fd_, kWakeupAscii, sizeof(kWakeupAscii) - 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Enable laser emitter
        ::write(fd_, kLaserOnAscii, sizeof(kLaserOnAscii) - 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Start continuous measurement mode (MUST send after L)
        ::write(fd_, kContinuousAscii, sizeof(kContinuousAscii) - 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
#pragma GCC diagnostic pop

        running_.store(true, std::memory_order_release);
        reader_thread_ = std::thread(&LaserRangefinder::reader_loop_m01, this);
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
// M01 reader loop (passive continuous mode with frame reassembly)
// ============================================================================

void LaserRangefinder::reader_loop_m01() {
    // Ring buffer for frame reassembly across partial reads
    uint8_t ring[kM01RingBufSize];
    size_t ring_len = 0;

    // Consecutive poll timeouts before re-sending the continuous command.
    // The M01 only streams for a short burst after each command; re-stimulate
    // when the module goes quiet so data keeps flowing.
    static constexpr int kMaxIdlePolls = 3;  // 3 × 500ms = 1.5s idle → re-send
    int idle_polls = 0;

    while (running_.load(std::memory_order_acquire)) {
        // Wait for data with poll() — 500ms timeout prevents thread hang
        struct pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;

        const int ready = ::poll(&pfd, 1, 500);
        if (ready <= 0) {
            // Re-send Continuous command after prolonged silence so the module
            // keeps streaming rather than falling silent between bursts.
            if (++idle_polls >= kMaxIdlePolls) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
                ::write(fd_, kContinuousCmd, sizeof(kContinuousCmd));
#pragma GCC diagnostic pop
                idle_polls = 0;
            }
            continue;
        }
        idle_polls = 0;  // Data arrived — reset idle counter
        if (!(pfd.revents & POLLIN)) continue;

        // Read into the ring buffer at the current tail position
        const size_t space = kM01RingBufSize - ring_len;
        if (space == 0) {
            // Buffer full with no valid frame found — discard and resync
            frame_errors_.fetch_add(1, std::memory_order_relaxed);
            ring_len = 0;
            ::tcflush(fd_, TCIFLUSH);
            continue;
        }

        const ssize_t n = ::read(fd_, ring + ring_len, space);
        if (n <= 0) {
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "LRF M01 read error: " << std::strerror(errno) << std::endl;
            }
            continue;
        }

        ring_len += static_cast<size_t>(n);

        // Parse all complete frames in the buffer
        while (ring_len >= static_cast<size_t>(kM01MinFrameLen)) {
            uint32_t mm = 0;

            const size_t consumed = parse_m01_frame(ring, ring_len, mm);

            if (consumed == 0) break;  // Need more data

            // If a valid distance was extracted, update the atomic
            if (mm >= 50u && mm <= 50000u) {
                range_mm_.store(mm, std::memory_order_release);
                last_ts_ns_.store(get_timestamp(ClockId::MonotonicRaw),
                                  std::memory_order_release);
            }

            // Shift unconsumed data to the front of the ring buffer
            if (consumed < ring_len) {
                std::memmove(ring, ring + consumed, ring_len - consumed);
            }
            ring_len -= consumed;
        }
        }
        }
// ============================================================================
// Modbus RTU reader loop (active poll/response)
// ============================================================================

void LaserRangefinder::reader_loop_modbus() {
    uint8_t resp[kModbusResponseLen];

    while (running_.load(std::memory_order_acquire)) {
        // Flush stale RX data before sending poll command
        ::tcflush(fd_, TCIFLUSH);

        const ssize_t written = ::write(fd_, kModbusPollCmd, sizeof(kModbusPollCmd));
        if (written != static_cast<ssize_t>(sizeof(kModbusPollCmd))) {
            frame_errors_.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(kModbusPollIntervalMs));
            continue;
        }

        // Wait for response with poll() — 500ms timeout
        struct pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;

        const int ready = ::poll(&pfd, 1, 500);
        if (ready <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kModbusPollIntervalMs));
            continue;
        }

        // Read response: expect exactly 7 bytes
        // [addr=01] [func=03] [byte_count=02] [data_hi] [data_lo] [crc_lo] [crc_hi]
        size_t total_read = 0;
        auto read_start = std::chrono::steady_clock::now();
        while (total_read < kModbusResponseLen) {
            // Guard against hanging if bytes trickle in forever
            auto elapsed = std::chrono::steady_clock::now() - read_start;
            if (elapsed > std::chrono::milliseconds(200)) break;

            const ssize_t n = ::read(fd_, resp + total_read,
                                     static_cast<size_t>(kModbusResponseLen) - total_read);
            if (n <= 0) {
                // Brief yield before retrying partial read
                struct pollfd rpfd{};
                rpfd.fd = fd_;
                rpfd.events = POLLIN;
                if (::poll(&rpfd, 1, 50) <= 0) break;
                continue;
            }
            total_read += static_cast<size_t>(n);
        }

        if (total_read != kModbusResponseLen) {
            frame_errors_.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(kModbusPollIntervalMs));
            continue;
        }

        // Validate address and function code
        if (resp[0] != kModbusAddr || resp[1] != kModbusFunc) {
            frame_errors_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // Validate byte count (should be 2 for one register)
        if (resp[2] != 0x02) {
            frame_errors_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // Validate CRC-16 over first 5 bytes (addr + func + byte_count + 2 data bytes)
        const uint16_t calc_crc = modbus_crc16(resp, 5);
        const uint16_t recv_crc = static_cast<uint16_t>(resp[5]) |
                                  (static_cast<uint16_t>(resp[6]) << 8);
        if (calc_crc != recv_crc) {
            crc_errors_.fetch_add(1, std::memory_order_relaxed);
            std::cerr << "FAIL: LRF CRC mismatch.\n"
                      << " Check: UART TX/RX integrity and 9600 baud lock.\n"
                      << " Fix: Verify Fusion Hat+ UART jumpers and power stability.\n";
            continue;
        }

        // Extract distance: bytes 3-4, big-endian, in millimetres
        const uint32_t mm = (static_cast<uint32_t>(resp[3]) << 8) | resp[4];
        frames_received_.fetch_add(1, std::memory_order_relaxed);

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

    // For M01 protocol, use ASCII commands per spec
    if (protocol_ == LrfProtocol::M01) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
        // Wake up with CR first
        ::write(fd_, kWakeupAscii, sizeof(kWakeupAscii) - 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Then L to enable laser
        ::write(fd_, kLaserOnAscii, sizeof(kLaserOnAscii) - 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // D for continuous mode
        ::write(fd_, kContinuousAscii, sizeof(kContinuousAscii) - 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
#pragma GCC diagnostic pop
    }

    const char* cmd;
    size_t cmd_len;
    if (protocol_ == LrfProtocol::MODBUS_RTU) {
        cmd = reinterpret_cast<const char*>(kModbusPollCmd);
        cmd_len = sizeof(kModbusPollCmd);
    } else {
        cmd = kContinuousAscii;
        cmd_len = sizeof(kContinuousAscii) - 1;
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
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

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
        // 0xAA = data frame, 0xEE = status/warm-up frame — both are valid M01 responses
        if (n < 8) return 2;     // Too short — baud mismatch
        if (buf[0] != 0xAA && buf[0] != 0xEE) return 3;  // Wrong sync — wrong protocol
        return 0;  // Looks valid
    }
}

}  // namespace aurore
