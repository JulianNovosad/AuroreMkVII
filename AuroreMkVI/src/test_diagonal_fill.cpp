#include <cassert>
#include <vector>
#include <iostream>
#include <cmath>
#include <cstring>

struct DiagonalFillBox {
    float xmin, ymin, xmax, ymax;
    float r, g, b;
    float stripe_spacing;
    bool use_fragment_shader;

    DiagonalFillBox(float xm, float ym, float xM, float yM, float red, float green, float blue,
                    float spacing = 0.05f, bool frag_shader = true)
        : xmin(xm), ymin(ym), xmax(xM), ymax(yM), r(red), g(green), b(blue),
          stripe_spacing(spacing), use_fragment_shader(frag_shader) {}

    float width() const { return xmax - xmin; }
    float height() const { return ymax - ymin; }
    bool is_valid() const { return xmax > xmin && ymax > ymin; }
};

static const char* diagonal_fill_fragment_shader = R"(#version 300 es
precision mediump float;

in vec2 vTexCoord;
in vec4 vColor;
in vec4 vFillColor;

out vec4 fragColor;

uniform float uStripeSpacing;

void main() {
    vec2 pos = vTexCoord;
    float diagonal = (pos.x + pos.y) / uStripeSpacing;
    float stripe = mod(floor(diagonal), 2.0);

    if (stripe > 0.5) {
        fragColor = vFillColor;
    } else {
        fragColor = vColor;
    }
}
)";

void test_diagonal_box_creation() {
    DiagonalFillBox box(0.4f, 0.4f, 0.6f, 0.6f, 1.0f, 0.65f, 0.0f);
    assert(box.is_valid());
    assert(fabsf(box.width() - 0.2f) < 0.001f);
    assert(fabsf(box.height() - 0.2f) < 0.001f);
    assert(box.r == 1.0f && box.g == 0.65f && box.b == 0.0f);
    std::cout << "[PASS] test_diagonal_box_creation" << std::endl;
}

void test_diagonal_box_invalid() {
    DiagonalFillBox box(0.5f, 0.5f, 0.4f, 0.4f, 1.0f, 0.65f, 0.0f);
    assert(!box.is_valid());
    std::cout << "[PASS] test_diagonal_box_invalid" << std::endl;
}

void test_diagonal_pattern_logic() {
    float spacing = 0.1f;
    int diagonal_count = 0;
    int solid_count = 0;

    for (float x = 0.0f; x < 1.0f; x += 0.01f) {
        for (float y = 0.0f; y < 1.0f; y += 0.01f) {
            float diagonal = (x + y) / spacing;
            float stripe = fmod(floor(diagonal), 2.0);
            if (stripe > 0.5f) {
                diagonal_count++;
            } else {
                solid_count++;
            }
        }
    }

    assert(diagonal_count > 0);
    assert(solid_count > 0);
    float ratio = static_cast<float>(diagonal_count) / (diagonal_count + solid_count);
    assert(ratio > 0.45f && ratio < 0.55f);
    std::cout << "[PASS] test_diagonal_pattern_logic (ratio: " << ratio << ")" << std::endl;
}

void test_fragment_shader_source() {
    assert(diagonal_fill_fragment_shader != nullptr);
    assert(strlen(diagonal_fill_fragment_shader) > 100);
    assert(strstr(diagonal_fill_fragment_shader, "#version 300 es") != nullptr);
    assert(strstr(diagonal_fill_fragment_shader, "uStripeSpacing") != nullptr);
    (void)diagonal_fill_fragment_shader;
    std::cout << "[PASS] test_fragment_shader_source" << std::endl;
}

int main() {
    std::cout << "=== Diagonal Fill Box Tests ===" << std::endl;

    test_diagonal_box_creation();
    test_diagonal_box_invalid();
    test_diagonal_pattern_logic();
    test_fragment_shader_source();

    std::cout << "=== All diagonal fill tests passed ===" << std::endl;
    return 0;
}
