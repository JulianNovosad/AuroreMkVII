/**
 * @file lrf_analyzer.cpp
 * @brief LRF UART Diagnostic Tool — hex-dump, stuck-sensor detection, CRC validation
 *
 * Standalone diagnostic tool for the M01 / Modbus RTU laser rangefinder.
 * Connects directly to the UART device and reports raw byte-level data.
 *
 * Usage:
 *   ./lrf_analyzer [device] [baud] [protocol] [samples]
 *   ./lrf_analyzer /dev/ttyAMA0 9600 m01 20
 *   ./lrf_analyzer /dev/ttyAMA0 9600 modbus 20
 *
 * Failure message format:
 *   FAIL: <description>
 *    Check: <what to inspect>
 *    Fix: <corrective action>
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>

// ============================================================================
// Modbus CRC-16 (same as in laser_rangefinder.cpp)
// ============================================================================
static uint16_t modbus_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x0001) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
        }
    }
    return crc;
}

// M01 checksum: sum(bytes[1..N-1]) & 0xFF
static uint8_t m01_checksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 1; i < len - 1; ++i) {
        sum += data[i];
    }
    return sum;
}

static void hex_dump(const uint8_t* data, size_t len, const char* label) {
    std::printf("  [%s] %zu bytes: ", label, len);
    for (size_t i = 0; i < len; ++i) {
        std::printf("%02X ", data[i]);
    }
    std::printf("\n");
}

static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 115200: return B115200;
        default:     return B9600;
    }
}

static int open_uart(const char* device, int baud) {
    int fd = ::open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::printf("FAIL: Cannot open %s: %s\n", device, std::strerror(errno));
        std::printf(" Check: UART device path and permissions.\n");
        std::printf(" Fix: Verify /dev/ttyAMA* exists and user is in dialout group.\n");
        return -1;
    }

    // Clear O_NONBLOCK
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty{};
    if (::tcgetattr(fd, &tty) != 0) {
        std::printf("FAIL: tcgetattr failed: %s\n", std::strerror(errno));
        std::printf(" Check: UART device is a valid serial port.\n");
        std::printf(" Fix: Verify Fusion Hat+ UART jumpers and power stability.\n");
        ::close(fd);
        return -1;
    }

    speed_t spd = baud_to_speed(baud);
    ::cfsetospeed(&tty, spd);
    ::cfsetispeed(&tty, spd);

    tty.c_cflag = (tty.c_cflag & ~static_cast<tcflag_t>(CSIZE)) | CS8;
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_lflag = 0;
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (::tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::printf("FAIL: tcsetattr failed: %s\n", std::strerror(errno));
        std::printf(" Check: UART TX/RX integrity and %d baud lock.\n", baud);
        std::printf(" Fix: Verify Fusion Hat+ UART jumpers and power stability.\n");
        ::close(fd);
        return -1;
    }

    ::tcflush(fd, TCIOFLUSH);
    return fd;
}

// ============================================================================
// Raw byte dump — shows every byte received, interprets 9-byte chunks
// Run with: ./lrf_analyzer /dev/ttyAMA0 9600 raw 10
// ============================================================================
static void analyze_raw(int fd, int duration_sec) {
    static constexpr uint8_t kLaserOnCmd[]    = {0xAA, 0x00, 0x01, 0xBE, 0x00, 0x01, 0x00, 0x01, 0xC1};
    static constexpr uint8_t kContinuousCmd[] = {0xAA, 0x00, 0x00, 0x21, 0x00, 0x01, 0x00, 0x00, 0x22};

    std::printf("\n=== Raw UART Dump (%d seconds) ===\n", duration_sec);
    std::printf("Sending Laser ON (x3) then Continuous (x3)...\n");

    ::tcflush(fd, TCIOFLUSH);
    for (int i = 0; i < 3; ++i) {
        ssize_t w = ::write(fd, kLaserOnCmd, sizeof(kLaserOnCmd));
        (void)w;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (int i = 0; i < 3; ++i) {
        ssize_t w = ::write(fd, kContinuousCmd, sizeof(kContinuousCmd));
        (void)w;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::printf("Waiting 2s then dumping for %ds...\n\n", duration_sec);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ::tcflush(fd, TCIFLUSH);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration_sec);
    auto start    = std::chrono::steady_clock::now();

    uint8_t accum[512];
    size_t  accum_len = 0;
    int     chunk_num = 0;
    int     read_count = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        struct pollfd pfd{};
        pfd.fd     = fd;
        pfd.events = POLLIN;
        if (::poll(&pfd, 1, 200) <= 0) continue;

        uint8_t tmp[64];
        ssize_t n = ::read(fd, tmp, sizeof(tmp));
        if (n <= 0) continue;

        ++read_count;
        double t = std::chrono::duration<double>(
                       std::chrono::steady_clock::now() - start).count();
        std::printf("[T+%6.3fs] read %2zd bytes: ", t, n);
        for (ssize_t j = 0; j < n; ++j) std::printf("%02X ", tmp[j]);
        std::printf("\n");
        std::fflush(stdout);

        // Append to accumulator
        for (ssize_t j = 0; j < n && accum_len < sizeof(accum); ++j)
            accum[accum_len++] = tmp[j];

        // Process complete 9-byte frames
        while (accum_len >= 9) {
            ++chunk_num;
            const uint8_t* f = accum;
            std::printf("  [Frame %d] sync=0x%02X | ", chunk_num, f[0]);
            for (int b = 0; b < 9; ++b) std::printf("%02X ", f[b]);

            // Checksum: sum(bytes[1..7]) == byte[8]
            uint8_t ck = 0;
            for (int b = 1; b <= 7; ++b) ck += f[b];
            std::printf("| ck=%s\n", (ck == f[8]) ? "OK" : "FAIL");

            if (ck == f[8]) {
                // Try every adjacent byte pair as uint16 BE (mm)
                std::printf("    uint16-BE [b:b+1] -> mm:\n");
                for (int b = 1; b <= 7; ++b) {
                    uint32_t v = (static_cast<uint32_t>(f[b]) << 8) | f[b + 1];
                    std::printf("      [%d:%d] 0x%02X 0x%02X = %4u mm = %.3f m\n",
                                b, b + 1, f[b], f[b + 1], v, v / 1000.0);
                }
                // 4-nibble BCD in centimetres (the 0xEE frame format)
                std::printf("    4-nibble BCD-cm [b:b+1] -> mm (only plausible 50-50000 mm shown):\n");
                for (int b = 1; b <= 7; ++b) {
                    uint32_t cm = ((f[b]   >> 4) & 0xF) * 1000u +
                                   (f[b]         & 0xF) * 100u  +
                                  ((f[b+1] >> 4) & 0xF) * 10u   +
                                   (f[b+1]       & 0xF);
                    uint32_t mm = cm * 10u;
                    if (mm >= 50 && mm <= 50000)
                        std::printf("      [%d:%d] 0x%02X 0x%02X = %4u cm = %5u mm = %.3f m  <--\n",
                                    b, b + 1, f[b], f[b + 1], cm, mm, mm / 1000.0);
                }
                // 3-nibble BCD (the old broken decode)
                {
                    const uint8_t dh = f[5], dl = f[6];
                    uint32_t old_cm = ((dh >> 4) & 0xF) * 100u +
                                       (dh        & 0xF) * 10u  +
                                      ((dl >> 4) & 0xF);
                    uint32_t new_cm = ((dh >> 4) & 0xF) * 1000u +
                                       (dh        & 0xF) * 100u  +
                                      ((dl >> 4) & 0xF) * 10u  +
                                       (dl        & 0xF);
                    std::printf("    bytes[5:6] BCD: OLD 3-nibble = %u mm, NEW 4-nibble = %u mm\n",
                                old_cm * 10u, new_cm * 10u);
                }
            }

            std::memmove(accum, accum + 9, accum_len - 9);
            accum_len -= 9;
        }
    }

    std::printf("\n=== Raw dump complete: %d read() calls, %d 9-byte chunks processed ===\n",
                read_count, chunk_num);
    if (chunk_num == 0)
        std::printf("FAIL: No data received — check UART device, baud rate, and ENA pin.\n");
}

// ============================================================================
// M01 Analyzer
// ============================================================================
static void analyze_m01(int fd, int num_samples) {
    static constexpr uint8_t kContinuousCmd[] = {0xAA, 0x00, 0x00, 0x21, 0x00, 0x01, 0x00, 0x00, 0x22};

    std::printf("\n=== M01 Protocol Analyzer ===\n");
    std::printf("Sending continuous-mode command...\n");

    ::tcflush(fd, TCIOFLUSH);
    for (int i = 0; i < 3; ++i) {
        ssize_t bytes_written = ::write(fd, kContinuousCmd, sizeof(kContinuousCmd));
        if (bytes_written < 0) { /* Error handling could go here, e.g., log, but not critical for diagnostic tool */ }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::printf("Waiting 2s for LRF warm-up...\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    // Do NOT flush here — data from the module may already be buffered.
    // The M01 outputs a burst after each command; flushing throws it away.

    // Track high-byte changes for stuck-sensor detection
    int last_dist_hi = -1;
    int last_dist_raw = -1;
    int hi_byte_changes = 0;
    int dist_changes = 0;
    int valid_frames = 0;
    int crc_errors = 0;
    int status_frames = 0;

    std::printf("\nCollecting %d samples...\n\n", num_samples);

    for (int sample = 0; sample < num_samples; ++sample) {
        // Re-stimulate the module before each sample.  The M01 outputs a short
        // burst after each continuous command and then goes quiet; re-sending
        // keeps data flowing between samples.
        {
            ssize_t w = ::write(fd, kContinuousCmd, sizeof(kContinuousCmd));
            (void)w;
        }

        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;

        int ready = ::poll(&pfd, 1, 2000);
        if (ready <= 0) {
            std::printf("  [%03d] TIMEOUT — no data in 2000ms\n", sample + 1);
            std::printf("FAIL: LRF not responding.\n");
            std::printf(" Check: UART TX/RX integrity and 9600 baud lock.\n");
            std::printf(" Fix: Verify Fusion Hat+ UART jumpers and power stability.\n");
            continue;
        }

        uint8_t buf[32];
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) {
            std::printf("  [%03d] READ ERROR: %s\n", sample + 1, std::strerror(errno));
            continue;
        }

        hex_dump(buf, static_cast<size_t>(n), "RAW");

        if (buf[0] == 0xEE) {
            // Don't skip — decode fully. Some M01 variants carry distance in 0xEE frames.
            status_frames++;
            if (n < 9) {
                std::printf("  [%03d] 0xEE frame too short (%zd bytes)\n", sample + 1, n);
                continue;
            }
            uint8_t ck = 0;
            for (int b = 1; b <= 7; ++b) ck += buf[b];
            bool ck_ok = (ck == buf[8]);
            std::printf("  [%03d] 0xEE frame | ck=%s |", sample + 1, ck_ok ? "OK" : "FAIL");
            for (int b = 0; b < 9; ++b) std::printf(" %02X", buf[b]);
            std::printf("\n");

            if (ck_ok) {
                // Old (broken) 3-nibble decode at bytes[5:6]
                uint32_t old_cm = ((buf[5] >> 4) & 0xF) * 100u +
                                   (buf[5]        & 0xF) * 10u  +
                                  ((buf[6] >> 4) & 0xF);
                // Fixed 4-nibble decode (all BCD digits)
                uint32_t new_cm = ((buf[5] >> 4) & 0xF) * 1000u +
                                   (buf[5]        & 0xF) * 100u  +
                                  ((buf[6] >> 4) & 0xF) * 10u   +
                                   (buf[6]        & 0xF);
                std::printf("         bytes[5:6]=0x%02X 0x%02X | BCD-3nib=%umm(%.3fm)"
                            " BCD-4nib=%umm(%.3fm)\n",
                            buf[5], buf[6],
                            old_cm * 10, old_cm * 10 / 1000.0,
                            new_cm * 10, new_cm * 10 / 1000.0);

                // Also show uint16-BE for every offset (helps spot non-BCD encoding)
                std::printf("         uint16-BE pairs:");
                for (int b = 1; b <= 7; ++b) {
                    uint32_t v = (static_cast<uint32_t>(buf[b]) << 8) | buf[b + 1];
                    if (v >= 50 && v <= 50000)
                        std::printf(" [%d:%d]=%umm", b, b + 1, v);
                }
                std::printf("\n");
            }
            continue;
        }

        if (buf[0] != 0xAA) {
            std::printf("  [%03d] UNKNOWN sync byte 0x%02X — not M01\n", sample + 1, buf[0]);
            continue;
        }

        if (n < 9) {
            std::printf("  [%03d] PARTIAL frame (%zd bytes, need 9+)\n", sample + 1, n);
            continue;
        }

        // Validate checksum (try 9-byte frame)
        size_t frame_len = 9;
        uint8_t expected_ck = m01_checksum(buf, frame_len);
        uint8_t actual_ck = buf[frame_len - 1];

        if (expected_ck != actual_ck && n >= 13) {
            // Try 13-byte frame
            frame_len = 13;
            expected_ck = m01_checksum(buf, frame_len);
            actual_ck = buf[frame_len - 1];
        }

        bool ck_ok = (expected_ck == actual_ck);

        // Skip command echoes: 9-byte frames with bytes[4:5] == 0x00 0x01 are echoes
        // of commands sent by us, not distance measurements.
        if (frame_len == 9 && buf[4] == 0x00 && buf[5] == 0x01) {
            std::printf("  [%03d] CMD-ECHO (9-byte, skipped)\n", sample + 1);
            continue;
        }

        // Distance decoding: both 9-byte and 13-byte use 4-nibble BCD in mm.
        //   9-byte data frames:  bytes [5:6]
        //   13-byte data frames: bytes [8:9]
        uint8_t dh, dl;
        if (frame_len == 13) {
            dh = buf[8]; dl = buf[9];
        } else {
            dh = buf[5]; dl = buf[6];
        }
        uint32_t dist_mm = ((dh >> 4) & 0xF) * 1000u +
                            (dh        & 0xF) * 100u  +
                           ((dl >> 4) & 0xF) * 10u   +
                            (dl        & 0xF);
        float dist_m = static_cast<float>(dist_mm) / 1000.0f;

        std::printf("  [%03d] FRAME %zu-byte | bytes[dh:dl]=0x%02X 0x%02X | "
                    "BCD=%u mm (%.3f m) | CRC %s",
                    sample + 1, frame_len, dh, dl,
                    dist_mm, static_cast<double>(dist_m),
                    ck_ok ? "OK" : "FAIL");

        if (!ck_ok) {
            std::printf(" (expected 0x%02X, got 0x%02X)", expected_ck, actual_ck);
            crc_errors++;
        } else {
            valid_frames++;
        }
        std::printf("\n");

        // Track high-byte changes
        if (last_dist_hi >= 0 && dh != static_cast<uint8_t>(last_dist_hi)) {
            hi_byte_changes++;
        }
        if (last_dist_raw >= 0 && dist_mm != static_cast<uint32_t>(last_dist_raw)) {
            dist_changes++;
        }
        last_dist_hi = dh;
        last_dist_raw = static_cast<int>(dist_mm);
    }

    // Summary
    std::printf("\n=== M01 Analysis Summary ===\n");
    std::printf("  Valid frames:  %d / %d\n", valid_frames, num_samples);
    std::printf("  CRC errors:    %d\n", crc_errors);
    std::printf("  Status frames: %d\n", status_frames);
    std::printf("  Hi-byte changes: %d (of %d valid)\n", hi_byte_changes, valid_frames);

    if (valid_frames > 0 && dist_changes == 0 && valid_frames > 1) {
        std::printf("\nWARNING: LRF distance value unchanged across %d frames.\n", valid_frames);
        std::printf(" Check: Target may be stationary or sensor may be stuck.\n");
    }

    if (crc_errors > valid_frames) {
        std::printf("\nFAIL: LRF CRC mismatch.\n");
        std::printf(" Check: UART TX/RX integrity and 9600 baud lock.\n");
        std::printf(" Fix: Verify Fusion Hat+ UART jumpers and power stability.\n");
    }

    if (valid_frames == 0 && status_frames > 0) {
        std::printf("\nFAIL: Only 0xEE status frames received — LRF may be warming up.\n");
        std::printf(" Check: Wait 5-10 seconds after power-on before polling.\n");
        std::printf(" Fix: Ensure stable 3.3V supply ≥150mA and ENA pin is HIGH.\n");
    }
}

// ============================================================================
// Modbus RTU Analyzer
// ============================================================================
static void analyze_modbus(int fd, int num_samples) {
    static constexpr uint8_t kPollCmd[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};

    std::printf("\n=== Modbus RTU Protocol Analyzer ===\n");
    std::printf("Poll frame: ");
    hex_dump(kPollCmd, sizeof(kPollCmd), "TX");

    int last_dist_hi = -1;
    int hi_byte_changes = 0;
    int valid_frames = 0;
    int crc_errors = 0;

    std::printf("\nCollecting %d samples...\n\n", num_samples);

    for (int sample = 0; sample < num_samples; ++sample) {
        ::tcflush(fd, TCIFLUSH);

        ssize_t written = ::write(fd, kPollCmd, sizeof(kPollCmd));
        if (written != static_cast<ssize_t>(sizeof(kPollCmd))) {
            std::printf("  [%03d] TX FAIL: only wrote %zd of %zu bytes\n",
                        sample + 1, written, sizeof(kPollCmd));
            continue;
        }

        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;

        int ready = ::poll(&pfd, 1, 1000);
        if (ready <= 0) {
            std::printf("  [%03d] TIMEOUT — no response in 1000ms\n", sample + 1);
            continue;
        }

        uint8_t resp[32];
        size_t total = 0;
        auto start = std::chrono::steady_clock::now();
        while (total < 7) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::milliseconds(200)) break;

            ssize_t n_read = ::read(fd, resp + total, 7 - total);
            if (n_read > 0) {
                total += static_cast<size_t>(n_read);
            } else {
                struct pollfd rpfd{};
                rpfd.fd = fd;
                rpfd.events = POLLIN;
                ::poll(&rpfd, 1, 50);
            }
        }

        hex_dump(resp, total, "RX");

        if (total < 7) {
            std::printf("  [%03d] INCOMPLETE: got %zu of 7 expected bytes\n", sample + 1, total);
            std::printf("FAIL: LRF response truncated.\n");
            std::printf(" Check: UART TX/RX integrity and 9600 baud lock.\n");
            std::printf(" Fix: Verify Fusion Hat+ UART jumpers and power stability.\n");
            continue;
        }

        // Validate structure
        if (resp[0] != 0x01 || resp[1] != 0x03) {
            std::printf("  [%03d] BAD HEADER: addr=0x%02X func=0x%02X (expected 01 03)\n",
                        sample + 1, resp[0], resp[1]);
            continue;
        }

        if (resp[2] != 0x02) {
            std::printf("  [%03d] BAD BYTE COUNT: %u (expected 2)\n", sample + 1, resp[2]);
            continue;
        }

        // CRC validation
        uint16_t calc_crc = modbus_crc16(resp, 5);
        uint16_t recv_crc = static_cast<uint16_t>(resp[5]) | (static_cast<uint16_t>(resp[6]) << 8);
        bool crc_ok = (calc_crc == recv_crc);

        uint16_t dist_mm = (static_cast<uint16_t>(resp[3]) << 8) | resp[4];
        float dist_m = static_cast<float>(dist_mm) / 1000.0f;

        std::printf("  [%03d] dist_hi=0x%02X dist_lo=0x%02X | raw=%u mm (%.3f m) | CRC %s",
                    sample + 1, resp[3], resp[4], dist_mm, static_cast<double>(dist_m),
                    crc_ok ? "OK" : "FAIL");

        if (!crc_ok) {
            std::printf(" (calc=0x%04X recv=0x%04X)", calc_crc, recv_crc);
            crc_errors++;
        } else {
            valid_frames++;
        }
        std::printf("\n");

        // Track high-byte changes
        if (last_dist_hi >= 0 && resp[3] != static_cast<uint8_t>(last_dist_hi)) {
            hi_byte_changes++;
        }
        last_dist_hi = resp[3];

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Summary
    std::printf("\n=== Modbus RTU Analysis Summary ===\n");
    std::printf("  Valid frames:    %d / %d\n", valid_frames, num_samples);
    std::printf("  CRC errors:      %d\n", crc_errors);
    std::printf("  Hi-byte changes: %d (of %d valid)\n", hi_byte_changes, valid_frames);

    if (valid_frames > 0 && hi_byte_changes == 0) {
        std::printf("\nFAIL: LRF distance high-byte NEVER changed across %d frames.\n", valid_frames);
        std::printf(" Check: Sensor stuck or target not moving between samples.\n");
        std::printf(" Fix: Move target >256mm between samples and re-run.\n");
    }

    if (crc_errors > valid_frames) {
        std::printf("\nFAIL: LRF CRC mismatch.\n");
        std::printf(" Check: UART TX/RX integrity and 9600 baud lock.\n");
        std::printf(" Fix: Verify Fusion Hat+ UART jumpers and power stability.\n");
    }
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
    const char* device = "/dev/ttyAMA0";
    int baud = 9600;
    const char* protocol = "m01";
    int samples = 20;

    if (argc >= 2) device = argv[1];
    if (argc >= 3) baud = std::atoi(argv[2]);
    if (argc >= 4) protocol = argv[3];
    if (argc >= 5) samples = std::atoi(argv[4]);

    std::printf("LRF Analyzer — Aurore MkVII Diagnostic Tool\n");
    std::printf("Device: %s  Baud: %d  Protocol: %s  Samples: %d\n\n",
                device, baud, protocol, samples);

    int fd = open_uart(device, baud);
    if (fd < 0) return 1;

    if (std::strcmp(protocol, "modbus") == 0) {
        analyze_modbus(fd, samples);
    } else if (std::strcmp(protocol, "raw") == 0) {
        analyze_raw(fd, samples);  // samples = duration in seconds for raw mode
    } else {
        analyze_m01(fd, samples);
    }

    ::close(fd);
    return 0;
}
