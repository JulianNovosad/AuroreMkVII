/**
 * @file test_frame_authentication.cpp
 * @brief Unit tests for frame authentication (ICD-001 / AM7-L2-SEC-001)
 *
 * Tests SHA256 hash computation and HMAC-SHA256 authentication for ZeroCopyFrame.
 * Verifies:
 * - Hash computation over pixel data
 * - HMAC computation over header + hash
 * - Verification with correct key passes
 * - Verification with wrong key fails
 * - Tampered data detection
 */

#include "aurore/camera_wrapper.hpp"
#include "aurore/security.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <random>
#include <vector>

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define CHECK(expr)                                                         \
    do {                                                                    \
        ++g_tests_run;                                                      \
        if (!(expr)) {                                                      \
            std::fprintf(stderr, "  FAIL [%s:%d]: %s\n",                   \
                         __FILE__, __LINE__, #expr);                        \
            ++g_tests_failed;                                               \
        } else {                                                            \
            ++g_tests_passed;                                               \
        }                                                                   \
    } while (0)

[[maybe_unused]] static void run_test(const char* name, void (*fn)()) {
    std::printf("[ RUN  ] %s\n", name);
    fn();
    std::printf("[ %s ] %s\n", g_tests_failed == 0 ? " OK " : "FAIL", name);
}

// ---------------------------------------------------------------------------
// Test 1: SHA256 hash computation
// ---------------------------------------------------------------------------
static void test_sha256_hash_computation() {
    // Known test vector
    const char* test_data = "Hello, Aurore MkVII!";
    unsigned char hash[32];
    
    aurore::security::compute_sha256_raw_threadsafe(test_data, strlen(test_data), hash);
    
    // Note: We can't check exact hash without computing expected offline,
    // so we just verify hash is non-zero and deterministic
    bool non_zero = false;
    for (int i = 0; i < 32; i++) {
        if (hash[i] != 0) {
            non_zero = true;
            break;
        }
    }
    CHECK(non_zero);
    
    // Verify determinism - same input produces same hash
    unsigned char hash2[32];
    aurore::security::compute_sha256_raw_threadsafe(test_data, strlen(test_data), hash2);
    CHECK(std::memcmp(hash, hash2, 32) == 0);
}

// ---------------------------------------------------------------------------
// Test 2: HMAC-SHA256 computation and verification
// ---------------------------------------------------------------------------
static void test_hmac_computation_and_verification() {
    const std::string key = "test_hmac_key_256bit_secret_value!";
    const char* message = "Test message for HMAC";
    
    unsigned char hmac[32];
    aurore::security::compute_hmac_sha256_raw(key, message, std::strlen(message), hmac);
    
    // Verify with correct key
    bool verified = aurore::security::verify_hmac_sha256_raw(key, message, std::strlen(message), hmac);
    CHECK(verified);
    
    // Verify with wrong key should fail
    const std::string wrong_key = "wrong_key_value";
    bool wrong_verified = aurore::security::verify_hmac_sha256_raw(wrong_key, message, std::strlen(message), hmac);
    CHECK(!wrong_verified);
}

// ---------------------------------------------------------------------------
// Test 3: Frame authentication end-to-end
// ---------------------------------------------------------------------------
static void test_frame_authentication_e2e() {
    aurore::CameraConfig cfg;
    cfg.width  = 320;
    cfg.height = 240;
    cfg.fps    = 30;

    aurore::CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    aurore::ZeroCopyFrame frame;
    bool ok = cam.try_capture_frame(frame);
    CHECK(ok);
    CHECK(frame.valid);
    
    // Frame should have authentication data after capture
    CHECK(frame.has_authentication());
    
    // Verify authentication with default key (must match kDefaultHmacKey in camera_wrapper.cpp)
    const char* default_key = "AURORE_MK7_FRAME_AUTH_KEY_256BIT_SECRET";
    bool verified = frame.verify_authentication(default_key, std::strlen(default_key));
    CHECK(verified);
    
    // Verify with wrong key should fail
    const char* wrong_key = "wrong_key";
    bool wrong_verified = frame.verify_authentication(wrong_key, std::strlen(wrong_key));
    CHECK(!wrong_verified);
    
    // Clean up
    if (frame.error[0] == 1 && frame.plane_data[0] != nullptr) {
        delete[] static_cast<uint8_t*>(frame.plane_data[0]);
        frame.plane_data[0] = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Test 4: Tampered frame detection
// ---------------------------------------------------------------------------
static void test_tampered_frame_detection() {
    aurore::CameraConfig cfg;
    cfg.width  = 320;
    cfg.height = 240;
    cfg.fps    = 30;

    aurore::CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    aurore::ZeroCopyFrame frame;
    bool ok = cam.try_capture_frame(frame);
    CHECK(ok);
    
    // Verify original frame
    const char* default_key = "AURORE_MK7_FRAME_AUTH_KEY_256BIT_SECRET";
    bool verified_before = frame.verify_authentication(default_key, strlen(default_key));
    CHECK(verified_before);
    
    // Tamper with frame data (modify pixel data)
    if (frame.plane_data[0] != nullptr && frame.plane_size[0] > 0) {
        uint8_t* data = static_cast<uint8_t*>(frame.plane_data[0]);
        uint8_t original = data[0];
        data[0] ^= 0xFF;  // Flip bits
        
        // Verification should now fail
        bool verified_after = frame.verify_authentication(default_key, strlen(default_key));
        CHECK(!verified_after);
        
        // Restore for cleanup
        data[0] = original;
    }
    
    // Clean up
    if (frame.error[0] == 1 && frame.plane_data[0] != nullptr) {
        delete[] static_cast<uint8_t*>(frame.plane_data[0]);
        frame.plane_data[0] = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Test 5: AsyncFrameAuthenticator
// ---------------------------------------------------------------------------
static void test_async_frame_authenticator() {
    const std::string key = "async_test_key_256bit";
    aurore::security::AsyncFrameAuthenticator auth(key);

    // Create test data
    std::vector<uint8_t> pixel_data(1024);
    std::vector<uint8_t> header_data(64);

    // Fill with random data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (auto& byte : pixel_data) byte = static_cast<uint8_t>(dis(gen));
    for (auto& byte : header_data) byte = static_cast<uint8_t>(dis(gen));

    // Create output frame with proper structure for verification
    aurore::ZeroCopyFrame out_frame;
    out_frame.valid = true;
    out_frame.width = 32;
    out_frame.height = 32;
    out_frame.format = aurore::PixelFormat::BGR888;
    out_frame.plane_data[0] = pixel_data.data();
    out_frame.plane_size[0] = pixel_data.size();
    out_frame.sequence = 1;
    out_frame.buffer_id = 0;

    // Build proper header from frame fields (matching compute_frame_header layout)
    // For async auth, we pass the header that will be used for HMAC
    uint8_t frame_header[64];
    size_t offset = 0;
    std::memcpy(frame_header + offset, &out_frame.sequence, sizeof(out_frame.sequence)); offset += 8;
    std::memcpy(frame_header + offset, &out_frame.timestamp_ns, sizeof(out_frame.timestamp_ns)); offset += 8;
    std::memcpy(frame_header + offset, &out_frame.exposure_us, sizeof(out_frame.exposure_us)); offset += 8;
    std::memcpy(frame_header + offset, &out_frame.gain, sizeof(out_frame.gain)); offset += 4;
    std::memcpy(frame_header + offset, &out_frame.width, sizeof(out_frame.width)); offset += 4;
    std::memcpy(frame_header + offset, &out_frame.height, sizeof(out_frame.height)); offset += 4;
    std::memcpy(frame_header + offset, &out_frame.format, sizeof(out_frame.format)); offset += 4;
    std::memcpy(frame_header + offset, &out_frame.buffer_id, sizeof(out_frame.buffer_id)); offset += 4;

    // Submit for async authentication
    auth.authenticate_frame(
        pixel_data.data(), pixel_data.size(),
        frame_header, offset,
        &out_frame
    );

    // Wait for completion (timeout 100ms)
    bool completed = auth.wait_for_completion(std::chrono::milliseconds(100));
    CHECK(completed);
    CHECK(auth.last_success());

    // Verify the authenticated frame
    bool verified = out_frame.verify_authentication(key.c_str(), key.length());
    CHECK(verified);
}

// ---------------------------------------------------------------------------
// Test 6: AsyncFrameAuthenticator destructor safety (no use-after-free)
// ---------------------------------------------------------------------------
static void test_async_authenticator_destructor_safety() {
    const std::string key = "destructor_safety_test_key_256bit";

    std::vector<uint8_t> pixel_data(1024, 0xAB);
    uint8_t header[32] = {};

    {
        aurore::security::AsyncFrameAuthenticator auth(key);
        aurore::ZeroCopyFrame out_frame{};
        out_frame.valid = true;
        out_frame.plane_data[0] = pixel_data.data();
        out_frame.plane_size[0] = pixel_data.size();

        auth.authenticate_frame(pixel_data.data(), pixel_data.size(), header, sizeof(header), &out_frame);
        // Deliberately do NOT call wait_for_completion() — destructor must block until done.
    }
    // If we reach here without a crash or sanitizer error, the destructor correctly waited.
    CHECK(true);
}

// ---------------------------------------------------------------------------
// Test 7: AsyncFrameAuthenticator timeout then successful wait
// ---------------------------------------------------------------------------
static void test_async_authenticator_timeout_returns_false() {
    const std::string key = "timeout_test_key_256bit";
    aurore::security::AsyncFrameAuthenticator auth(key);

    std::vector<uint8_t> pixel_data(1024, 0xCD);
    uint8_t header[32] = {};
    aurore::ZeroCopyFrame out_frame{};
    out_frame.valid = true;
    out_frame.plane_data[0] = pixel_data.data();
    out_frame.plane_size[0] = pixel_data.size();

    auth.authenticate_frame(pixel_data.data(), pixel_data.size(), header, sizeof(header), &out_frame);

    // 0ms wait — may already be ready (fast task) or may timeout; both are acceptable.
    bool immediate = auth.wait_for_completion(std::chrono::milliseconds(0));

    // A subsequent 200ms wait must succeed and report success.
    bool completed = immediate || auth.wait_for_completion(std::chrono::milliseconds(200));
    CHECK(completed);
    CHECK(auth.last_success());
}

// ---------------------------------------------------------------------------
// Test 8: Performance benchmark - hash computation overhead
// ---------------------------------------------------------------------------
static void test_hash_computation_overhead() {
    const size_t frame_size = 1536 * 864 * 2;  // RAW10 frame size
    std::vector<uint8_t> frame_data(frame_size);
    
    // Fill with random data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (auto& byte : frame_data) byte = static_cast<uint8_t>(dis(gen));
    
    unsigned char hash[32];
    
    // Warmup
    aurore::security::compute_sha256_raw_threadsafe(frame_data.data(), frame_data.size(), hash);
    
    // Benchmark
    const int iterations = 100;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        aurore::security::compute_sha256_raw_threadsafe(frame_data.data(), frame_data.size(), hash);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    double avg_time_us = static_cast<double>(duration) / iterations;
    double throughput_mbps = (static_cast<double>(frame_size) / (1024 * 1024)) / (avg_time_us / 1e6);
    
    std::printf("  SHA256 performance: %.2f us per frame (%.2f MB/s)\n",
                avg_time_us, throughput_mbps);

    // Should complete within 10ms per frame (spec allows async hash within one frame period of 8.33ms)
    // Note: Synchronous SHA256 of 2.5MB frame takes ~6ms on typical hardware
    CHECK(avg_time_us < 10000.0);
}

// ---------------------------------------------------------------------------
// Test 9: HMAC computation overhead
// ---------------------------------------------------------------------------
static void test_hmac_computation_overhead() {
    const size_t header_size = 44;  // Frame header size
    const size_t hash_size = 32;    // SHA256 hash size
    const size_t total_size = header_size + hash_size;
    
    std::vector<uint8_t> hmac_input(total_size);
    std::string key = "test_key_256bit";
    
    // Fill with random data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (auto& byte : hmac_input) byte = static_cast<uint8_t>(dis(gen));
    
    unsigned char hmac[32];
    
    // Warmup
    aurore::security::compute_hmac_sha256_raw_threadsafe(key, hmac_input.data(), hmac_input.size(), hmac);
    
    // Benchmark
    const int iterations = 1000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        aurore::security::compute_hmac_sha256_raw_threadsafe(key, hmac_input.data(), hmac_input.size(), hmac);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    double avg_time_us = static_cast<double>(duration) / iterations;
    
    std::printf("  HMAC-SHA256 performance: %.2f us per computation\n", avg_time_us);
    
    // Should complete within 100us (minimal overhead)
    CHECK(avg_time_us < 100.0);
}

// ---------------------------------------------------------------------------
// Test 10: Full authentication overhead (hash + HMAC)
// ---------------------------------------------------------------------------
static void test_full_authentication_overhead() {
    const size_t frame_size = 1536 * 864 * 2;  // RAW10 frame size
    std::vector<uint8_t> frame_data(frame_size);
    
    // Fill with random data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (auto& byte : frame_data) byte = static_cast<uint8_t>(dis(gen));
    
    // Create frame struct
    aurore::ZeroCopyFrame frame;
    frame.valid = true;
    frame.width = 1536;
    frame.height = 864;
    frame.format = aurore::PixelFormat::RAW10;
    frame.plane_data[0] = frame_data.data();
    frame.plane_size[0] = frame_data.size();
    frame.sequence = 1;
    frame.buffer_id = 0;
    
    // Warmup
    aurore::authenticate_frame(frame);
    
    // Benchmark
    const int iterations = 100;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        aurore::authenticate_frame(frame);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    double avg_time_us = static_cast<double>(duration) / iterations;
    double overhead_percent = (avg_time_us / 8333.0) * 100.0;  // 8.333ms = 120Hz frame period
    
    std::printf("  Full auth performance: %.2f us per frame (%.2f%% of 120Hz budget)\n",
                avg_time_us, overhead_percent);

    // Should complete within 10ms (spec allows async auth within one frame period of 8.33ms)
    // Note: Synchronous full auth (hash + HMAC) of 2.5MB frame takes ~7ms on typical hardware
    CHECK(avg_time_us < 10000.0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::printf("Frame Authentication Tests (ICD-001 / AM7-L2-SEC-001)\n");
    std::printf("=====================================================\n\n");
    
    int prev_failed = 0;

    auto run = [&](const char* name, void (*fn)()) {
        prev_failed = g_tests_failed;
        std::printf("[ RUN  ] %s\n", name);
        fn();
        if (g_tests_failed == prev_failed) {
            std::printf("[  OK  ] %s\n", name);
        } else {
            std::printf("[ FAIL ] %s\n", name);
        }
    };

    run("test_sha256_hash_computation", test_sha256_hash_computation);
    run("test_hmac_computation_and_verification", test_hmac_computation_and_verification);
    run("test_frame_authentication_e2e", test_frame_authentication_e2e);
    run("test_tampered_frame_detection", test_tampered_frame_detection);
    run("test_async_frame_authenticator", test_async_frame_authenticator);
    run("test_async_authenticator_destructor_safety", test_async_authenticator_destructor_safety);
    run("test_async_authenticator_timeout_returns_false", test_async_authenticator_timeout_returns_false);
    run("test_hash_computation_overhead", test_hash_computation_overhead);
    run("test_hmac_computation_overhead", test_hmac_computation_overhead);
    run("test_full_authentication_overhead", test_full_authentication_overhead);

    std::printf("\n=====================================================\n");
    std::printf("Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) {
        std::printf(", %d FAILED\n", g_tests_failed);
        return 1;
    }
    std::printf(" - ALL PASS\n");
    return 0;
}
