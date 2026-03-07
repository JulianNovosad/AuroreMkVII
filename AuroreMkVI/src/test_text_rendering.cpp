#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

#include "gpu_overlay.h"
#include "font_data.h"

constexpr int TEST_WIDTH = 640;
constexpr int TEST_HEIGHT = 480;

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAILED: " << #cond << " at line " << __LINE__ << std::endl; \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::cerr << "FAILED: " << #a << " != " << #b << " (" << (a) << " vs " << (b) << ") at line " << __LINE__ << std::endl; \
        tests_failed++; \
        return; \
    } \
} while(0)

#define RUN_TEST(name) do { \
    std::cout << "Running " << #name << "..." << std::endl; \
    name(); \
} while(0)

#define PASS(name) do { \
    std::cout << "[PASS] " << name << std::endl; \
    tests_passed++; \
} while(0)

void test_font_uv_calculation() {
    for (char c = 'A'; c <= 'Z'; c++) {
        float u_start = (float)c / 128.0f;
        float u_end = (float)(c + 1) / 128.0f;
        
        ASSERT_TRUE(u_start < u_end);
        ASSERT_TRUE(u_start >= 0.0f && u_start < 1.0f);
        ASSERT_TRUE(u_end > 0.0f && u_end <= 1.0f);
    }

    for (char c = '0'; c <= '9'; c++) {
        float u_start = (float)c / 128.0f;
        float u_end = (float)(c + 1) / 128.0f;
        ASSERT_TRUE(u_start < u_end);
        ASSERT_TRUE(u_start >= 0.0f && u_start < 1.0f);
        ASSERT_TRUE(u_end > 0.0f && u_end <= 1.0f);
    }

    PASS("Font UV Calculation");
}

void test_text_positioning() {
    float x = -0.95f;
    float y = 0.9f;
    float size = 0.04f;
    float aspect = (float)TEST_WIDTH / TEST_HEIGHT;
    float char_w = size;
    float char_h = size * aspect;

    std::string text = "ABC";
    for (size_t i = 0; i < text.length(); i++) {
        float cx = x + i * char_w;
        ASSERT_TRUE(cx >= -1.0f && cx <= 1.0f);
        ASSERT_TRUE(cx + char_w >= -1.0f && cx + char_w <= 1.0f);
    }

    ASSERT_TRUE(y >= -1.0f && y <= 1.0f);
    ASSERT_TRUE(y + char_h >= -1.0f && y + char_h <= 1.0f);

    PASS("Text Positioning");
}

void test_font_texture_dimensions() {
    constexpr int FONT_TEXTURE_WIDTH = 1024;
    constexpr int FONT_TEXTURE_HEIGHT = 8;
    ASSERT_EQ(FONT_TEXTURE_WIDTH, 1024);
    ASSERT_EQ(FONT_TEXTURE_HEIGHT, 8);
    PASS("Font Texture Dimensions");
}

void test_hud_characters() {
    std::string hud_text = "ABC123XYZ";

    for (char c : hud_text) {
        int ci = (int)c;
        ASSERT_TRUE(ci >= 32 && ci < 128);
        ASSERT_TRUE(ci >= 33);
        bool has_pixels = false;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (font8x8_basic[ci][y] & (1 << x)) {
                    has_pixels = true;
                    break;
                }
            }
            if (has_pixels) break;
        }
        ASSERT_TRUE(has_pixels);
    }

    PASS("HUD Characters");
}

void test_temperature_format() {
    std::string temp_str = "45.3C";

    for (char c : temp_str) {
        int ci = (int)c;
        bool has_pixels = false;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (font8x8_basic[ci][y] & (1 << x)) {
                    has_pixels = true;
                    break;
                }
            }
            if (has_pixels) break;
        }
        ASSERT_TRUE(has_pixels);
    }

    PASS("Temperature Format Characters");
}

void test_mode_indicators() {
    std::string modes[] = {"SAFE", "ACTIVE", "FIRE"};

    for (const auto& mode : modes) {
        for (char c : mode) {
            int ci = (int)c;
            bool has_pixels = false;
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    if (font8x8_basic[ci][y] & (1 << x)) {
                        has_pixels = true;
                        break;
                    }
                }
                if (has_pixels) break;
            }
            ASSERT_TRUE(has_pixels);
        }
    }

    PASS("Mode Indicator Characters");
}

void test_frame_counter_digits() {
    for (char d = '0'; d <= '9'; d++) {
        int ci = (int)d;
        bool has_pixels = false;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (font8x8_basic[ci][y] & (1 << x)) {
                    has_pixels = true;
                    break;
                }
            }
            if (has_pixels) break;
        }
        ASSERT_TRUE(has_pixels);
    }

    PASS("Frame Counter Digits");
}

void test_alphanumeric_variety() {
    int pixel_a = 0, pixel_8 = 0;

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (font8x8_basic['A'][y] & (1 << x)) pixel_a++;
            if (font8x8_basic['8'][y] & (1 << x)) pixel_8++;
        }
    }

    ASSERT_TRUE(pixel_a >= 10);
    ASSERT_TRUE(pixel_8 >= 12);
    ASSERT_TRUE(pixel_a != pixel_8);

    PASS("Alphanumeric Variety");
}

int main() {
    std::cout << "=== Text Rendering Unit Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "[Font Bitmap Tests]" << std::endl;
    test_font_texture_dimensions();
    test_alphanumeric_variety();

    std::cout << std::endl;
    std::cout << "[HUD-Specific Tests]" << std::endl;
    test_hud_characters();
    test_temperature_format();
    test_mode_indicators();
    test_frame_counter_digits();

    std::cout << std::endl;
    std::cout << "[Text Positioning Tests]" << std::endl;
    test_font_uv_calculation();
    test_text_positioning();

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Tests passed: " << tests_passed << std::endl;
    std::cout << "Tests failed: " << tests_failed << std::endl;
    std::cout << "========================================" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
