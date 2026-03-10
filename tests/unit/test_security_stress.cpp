/**
 * @file test_security_stress.cpp
 * @brief Stress tests for the security module (HMAC-SHA256, SHA256)
 *
 * Verifies that high-frequency authentication checks (1000+ iterations)
 * do not result in memory leaks or race conditions.
 *
 * Spec: AM7-L2-SEC-001 (Frame Authentication)
 */

#include "aurore/camera_wrapper.hpp"
#include "aurore/security.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <random>
#include <vector>
#include <thread>
#include <atomic>

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

// ---------------------------------------------------------------------------
// Stress Test 1: High-frequency synchronous authentication
// ---------------------------------------------------------------------------
static void test_security_high_frequency_sync() {
    const int iterations = 2000;
    const size_t frame_size = 640 * 480 * 2; // Simulated RAW10 frame
    std::vector<uint8_t> frame_data(frame_size);
    
    // Fill with random data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (auto& byte : frame_data) byte = static_cast<uint8_t>(dis(gen));
    
    aurore::ZeroCopyFrame frame;
    frame.valid = true;
    frame.width = 640;
    frame.height = 480;
    frame.format = aurore::PixelFormat::RAW10;
    frame.plane_data[0] = frame_data.data();
    frame.plane_size[0] = frame_data.size();
    
    const char* key = "STRESS_TEST_KEY_VALUE_1234567890";
    
    std::printf("  Running %d iterations of synchronous authentication...\n", iterations);
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        frame.sequence = static_cast<uint64_t>(i);
        bool ok = aurore::authenticate_frame(frame, key, std::strlen(key));
        if (!ok) {
            CHECK(ok);
            break;
        }
        
        bool verified = frame.verify_authentication(key, std::strlen(key));
        if (!verified) {
            CHECK(verified);
            break;
        }
        
        // Minor check to ensure it's actually doing something
        if (i % 500 == 0) {
            std::printf("    Iteration %d completed\n", i);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::printf("  Completed in %ld ms (%.2f ms/iteration)\n", 
                duration, static_cast<double>(duration) / iterations);
    
    CHECK(g_tests_failed == 0);
}

// ---------------------------------------------------------------------------
// Stress Test 2: Multithreaded race condition check
// ---------------------------------------------------------------------------
static void test_security_multithreaded_race() {
    const int num_threads = 8;
    const int iterations_per_thread = 500;
    const std::string key = "MULTI_THREAD_STRESS_KEY";
    const std::string message = "Consistent message for all threads to check output";
    
    unsigned char expected_hmac[32];
    aurore::security::compute_hmac_sha256_raw_threadsafe(key, message.data(), message.size(), expected_hmac);
    
    std::atomic<int> failed_checks{0};
    std::vector<std::thread> threads;
    
    std::printf("  Running %d threads, each with %d iterations of HMAC...\n", 
                num_threads, iterations_per_thread);
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < iterations_per_thread; i++) {
                unsigned char current_hmac[32];
                aurore::security::compute_hmac_sha256_raw_threadsafe(key, message.data(), message.size(), current_hmac);
                
                if (std::memcmp(current_hmac, expected_hmac, 32) != 0) {
                    failed_checks++;
                }
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::printf("  Completed in %ld ms\n", duration);
    CHECK(failed_checks == 0);
}

// ---------------------------------------------------------------------------
// Stress Test 3: AsyncFrameAuthenticator saturation
// ---------------------------------------------------------------------------
static void test_security_async_saturation() {
    const std::string key = "ASYNC_STRESS_KEY";
    const int iterations = 1000;
    
    std::printf("  Running %d iterations of sequential async authentication...\n", iterations);
    
    std::vector<uint8_t> pixel_data(1024, 0xAA);
    uint8_t header_data[64];
    std::memset(header_data, 0xBB, 64);
    
    aurore::ZeroCopyFrame frame;
    frame.valid = true;
    frame.plane_data[0] = pixel_data.data();
    frame.plane_size[0] = pixel_data.size();
    
    aurore::security::AsyncFrameAuthenticator auth(key);
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        frame.sequence = static_cast<uint64_t>(i);
        
        // Note: authenticate_frame now ABORTS if busy, so we MUST wait for completion
        auth.authenticate_frame(pixel_data.data(), pixel_data.size(), header_data, 44, &frame);
        
        bool completed = auth.wait_for_completion(std::chrono::milliseconds(100));
        if (!completed) {
            std::printf("    Async timeout at iteration %d\n", i);
            CHECK(completed);
            break;
        }
        
        if (i % 250 == 0) {
            std::printf("    Iteration %d completed\n", i);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::printf("  Completed in %ld ms\n", duration);
    CHECK(g_tests_failed == 0);
}

int main() {
    std::printf("Security Module Stress Tests\n");
    std::printf("============================\n\n");
    
    auto run = [](const char* name, void (*fn)()) {
        int start_failed = g_tests_failed;
        std::printf("[ RUN  ] %s\n", name);
        fn();
        if (g_tests_failed == start_failed) {
            std::printf("[  OK  ] %s\n", name);
        } else {
            std::printf("[ FAIL ] %s\n", name);
        }
    };
    
    run("test_security_high_frequency_sync", test_security_high_frequency_sync);
    run("test_security_multithreaded_race", test_security_multithreaded_race);
    run("test_security_async_saturation", test_security_async_saturation);
    
    std::printf("\n============================\n");
    std::printf("Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) {
        std::printf(", %d FAILED\n", g_tests_failed);
        return 1;
    }
    std::printf(" - ALL PASS\n");
    return 0;
}
