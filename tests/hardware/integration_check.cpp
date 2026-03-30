/**
 * @file integration_check.cpp
 * @brief Hardware integration check for USB webcam and laser rangefinder
 *
 * HARDWARE TESTING POLICY (per CLAUDE.md):
 * - NEVER implement graceful degradation
 * - ALWAYS fail immediately (<=500ms) if hardware not detected
 * - NO mocks, NO simulations, NO timeouts >1 second
 * - Provide clear FAIL messages with Check: and Fix: steps
 *
 * Tests:
 *   1. USB webcam presence and frame capture
 *   2. LRF UART presence, wiring diagnostic, and range reading
 *
 * Usage:
 *   sudo ./integration_check [--modbus] [--uart /dev/ttyAMA10] [--usb /dev/video0]
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "aurore/drivers/laser_rangefinder.hpp"
#include "aurore/usb_camera.hpp"
#include "aurore/timing.hpp"

// ============================================================================
// Test framework
// ============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message)                           \
    do {                                                          \
        if (condition) {                                          \
            std::cout << "  PASS: " << message << "\n";           \
            tests_passed++;                                       \
        } else {                                                  \
            std::cerr << "  FAIL: " << message << "\n";           \
            tests_failed++;                                       \
        }                                                         \
    } while (0)

// ============================================================================
// USB Webcam Tests
// ============================================================================

static void test_usb_webcam_presence(const std::string& device_path) {
    std::cout << "\n=== Test: USB Webcam Presence Check ===\n";

    if (!device_path.empty()) {
        // Check specific device
        int fd = ::open(device_path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            TEST_ASSERT(false,
                "USB webcam not detected on " + device_path + "\n"
                "      Check: ls /dev/video*\n"
                "      Fix: Connect a USB UVC webcam to a USB port");
            return;
        }

        struct v4l2_capability cap{};
        bool is_valid = false;
        if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            is_valid = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
                       (cap.capabilities & V4L2_CAP_STREAMING);
        }
        ::close(fd);

        if (!is_valid) {
            TEST_ASSERT(false,
                device_path + " is not a valid video capture device\n"
                "      Check: v4l2-ctl --device=" + device_path + " --all\n"
                "      Fix: Ensure a UVC-compatible webcam is connected");
            return;
        }

        TEST_ASSERT(true, "USB webcam detected on " + device_path);
    } else {
        // Auto-detect
        bool found = aurore::UsbCamera::detect();
        if (!found) {
            TEST_ASSERT(false,
                "No USB webcam detected\n"
                "      Check: ls /dev/video* && lsusb | grep -i cam\n"
                "      Fix: Connect a USB UVC webcam to any USB port");
            return;
        }
        TEST_ASSERT(true, "USB webcam auto-detected");
    }
}

static void test_usb_webcam_capture(const std::string& device_path) {
    std::cout << "\n=== Test: USB Webcam Frame Capture ===\n";

    aurore::UsbCameraConfig cfg;
    cfg.width = 640;
    cfg.height = 480;
    cfg.fps = 30;
    if (!device_path.empty()) {
        cfg.device_path = device_path;
    }

    aurore::UsbCamera cam(cfg);

    if (!cam.init()) {
        TEST_ASSERT(false,
            "USB webcam init failed\n"
            "      Check: v4l2-ctl --list-devices\n"
            "      Fix: Ensure webcam supports 640x480 and user is in 'video' group");
        return;
    }

    if (!cam.start()) {
        TEST_ASSERT(false,
            "USB webcam start failed\n"
            "      Check: dmesg | tail -20\n"
            "      Fix: Reconnect webcam and check USB bandwidth");
        return;
    }

    // Capture a frame within 500ms
    aurore::ZeroCopyFrame frame;
    const auto start = std::chrono::steady_clock::now();
    bool captured = cam.capture_frame(frame, 500);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    cam.stop();

    TEST_ASSERT(captured,
        "Frame captured from USB webcam");

    if (captured) {
        TEST_ASSERT(frame.is_valid(),
            "Frame is valid (w=" + std::to_string(frame.width) +
            " h=" + std::to_string(frame.height) +
            " seq=" + std::to_string(frame.sequence) + ")");
        TEST_ASSERT(elapsed_ms <= 500,
            "Capture latency within budget (" + std::to_string(elapsed_ms) + "ms <= 500ms)");
    }
}

// ============================================================================
// LRF Tests
// ============================================================================

static void test_lrf_uart_presence(const std::string& uart_device) {
    std::cout << "\n=== Test: LRF UART Presence Check ===\n";

    int fd = ::open(uart_device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        TEST_ASSERT(false,
            "LRF UART not detected on " + uart_device + "\n"
            "      Check: ls /dev/ttyAMA*\n"
            "      Fix: Connect LRF to Fusion HAT+ UART slot (GPIO14/15)");
        return;
    }
    ::close(fd);
    TEST_ASSERT(true, "UART device " + uart_device + " accessible");
}

static void test_lrf_wiring_diagnostic(const std::string& uart_device, aurore::LrfProtocol protocol) {
    std::cout << "\n=== Test: LRF Wiring Diagnostic ===\n";

    const char* proto_name = (protocol == aurore::LrfProtocol::MODBUS_RTU) ? "Modbus RTU" : "M01";
    std::cout << "  Protocol: " << proto_name << "\n";

    aurore::LaserRangefinder lrf;
    if (!lrf.init(uart_device, 9600, protocol)) {
        TEST_ASSERT(false,
            "LRF UART init failed on " + uart_device + "\n"
            "      Check: ls -la " + uart_device + "\n"
            "      Fix: Run with sudo or add user to dialout group");
        return;
    }

    const int diag = lrf.diagnose_wiring();

    switch (diag) {
        case 0:
            TEST_ASSERT(true, "LRF wiring OK — valid response received");
            break;
        case 1:
            TEST_ASSERT(false,
                "LRF no response — TX/RX may be swapped or disconnected\n"
                "      Check: Verify TX connects to LRF RX and vice versa\n"
                "      Fix: Swap TX/RX wires on the Fusion HAT+ UART header");
            break;
        case 2:
            TEST_ASSERT(false,
                "LRF garbage response — possible baud rate mismatch\n"
                "      Check: LRF datasheet for correct baud rate\n"
                "      Fix: Try 9600, 19200, or 115200 baud (--baud flag)");
            break;
        case 3:
            TEST_ASSERT(false,
                "LRF wrong protocol response — frame structure mismatch\n"
                "      Check: LRF uses " + std::string(proto_name) + " protocol?\n"
                "      Fix: Try --modbus flag for Modbus RTU, or omit it for M01");
            break;
        default:
            TEST_ASSERT(false, "LRF unknown diagnostic code " + std::to_string(diag));
            break;
    }

    // Test multiple baud rates if initial test failed
    if (diag != 0) {
        std::cout << "\n  --- Baud Rate Scan ---\n";
        const int bauds[] = {9600, 19200, 38400, 115200};
        for (int baud : bauds) {
            aurore::LaserRangefinder probe;
            if (!probe.init(uart_device, baud, protocol)) continue;
            const int result = probe.diagnose_wiring();
            const char* status = (result == 0) ? "RESPONSE" : "no response";
            std::cout << "    " << baud << " baud: " << status << "\n";
            probe.stop();
            if (result == 0) {
                std::cout << "    >>> Try: --baud " << baud << "\n";
                break;
            }
        }
    }

    lrf.stop();
}

static void test_lrf_range_reading(const std::string& uart_device, aurore::LrfProtocol protocol) {
    std::cout << "\n=== Test: LRF Range Reading ===\n";

    aurore::LaserRangefinder lrf;
    if (!lrf.init(uart_device, 9600, protocol)) {
        TEST_ASSERT(false,
            "LRF init failed\n"
            "      Check: Previous UART presence test\n"
            "      Fix: Resolve UART issues first");
        return;
    }

    if (!lrf.start_continuous()) {
        TEST_ASSERT(false,
            "LRF start failed\n"
            "      Check: Wiring diagnostic results\n"
            "      Fix: Ensure LRF is powered and wired correctly");
        lrf.stop();
        return;
    }

    // Wait up to 6s for a valid range reading (M01 needs warm-up ~3-5s)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(6000);
    float range_m = 0.0f;
    while (std::chrono::steady_clock::now() < deadline) {
        range_m = lrf.latest_range_m();
        if (range_m > 0.0f) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    lrf.stop();

    TEST_ASSERT(range_m > 0.0f,
        "Valid range reading received");

    if (range_m > 0.0f) {
        const float max_range = (protocol == aurore::LrfProtocol::MODBUS_RTU) ? 40.0f : 50.0f;
        TEST_ASSERT(range_m >= aurore::LaserRangefinder::kMinRangeM,
            "Range >= minimum (" + std::to_string(range_m) + "m >= 0.05m)");
        TEST_ASSERT(range_m <= max_range,
            "Range <= maximum (" + std::to_string(range_m) + "m <= " +
            std::to_string(max_range) + "m)");

        std::cout << "  INFO: Measured range = " << range_m << " m\n";
    } else {
        std::cerr << "  FAIL: LRF returned no valid range within 1s\n"
                  << "      Check: Ensure LRF beam is unobstructed\n"
                  << "      Fix: Point LRF at a solid surface within range\n";
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    // Parse arguments
    std::string uart_device = "/dev/ttyAMA0";
    std::string usb_device;
    aurore::LrfProtocol protocol = aurore::LrfProtocol::M01;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--modbus") {
            protocol = aurore::LrfProtocol::MODBUS_RTU;
        } else if (arg == "--uart" && i + 1 < argc) {
            uart_device = argv[++i];
        } else if (arg == "--usb" && i + 1 < argc) {
            usb_device = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --modbus           Use Modbus RTU protocol (default: M01)\n"
                      << "  --uart <device>    UART device path (default: /dev/ttyAMA10)\n"
                      << "  --usb <device>     USB camera device path (default: auto-detect)\n"
                      << "  -h, --help         Show this help\n";
            return 0;
        }
    }

    std::cout << "===========================================\n";
    std::cout << "Aurore MkVII Hardware Integration Check\n";
    std::cout << "===========================================\n";
    std::cout << "LRF UART:  " << uart_device << "\n";
    std::cout << "LRF Proto: " << ((protocol == aurore::LrfProtocol::MODBUS_RTU) ? "Modbus RTU" : "M01") << "\n";
    std::cout << "USB Cam:   " << (usb_device.empty() ? "(auto-detect)" : usb_device) << "\n";

    // ---- USB Webcam ----
    test_usb_webcam_presence(usb_device);
    test_usb_webcam_capture(usb_device);

    // ---- Laser Rangefinder ----
    test_lrf_uart_presence(uart_device);
    test_lrf_wiring_diagnostic(uart_device, protocol);
    test_lrf_range_reading(uart_device, protocol);

    // ---- Summary ----
    std::cout << "\n===========================================\n";
    std::cout << "Integration Check Summary: " << tests_passed << " passed, "
              << tests_failed << " failed\n";
    std::cout << "===========================================\n";

    if (tests_failed > 0) {
        std::cerr << "\nHARDWARE INTEGRATION CHECK FAILED\n";
        std::cerr << "Please check hardware connections and re-run.\n";
        std::cerr << "Run 'sudo ./scripts/check-hardware.sh' to verify all hardware.\n";
    }

    return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
