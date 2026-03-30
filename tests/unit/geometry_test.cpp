#include "aurore/gimbal_controller.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace aurore;

void test_center_target() {
    CameraIntrinsics cam;
    cam.focal_length_px = 1000.0f;
    cam.cx = 320.0f;
    cam.cy = 240.0f;
    GimbalController controller(cam);
    controller.set_limits(-90.0f, 90.0f, -45.0f, 45.0f);

    // Target exactly at center should result in 0,0 angles if starting from 0,0
    GimbalCommand cmd = controller.command_from_pixel(320.0f, 240.0f);
    assert(std::abs(cmd.az_deg) < 0.001f);
    assert(std::abs(cmd.el_deg) < 0.001f);
    (void)cmd;
    std::cout << "PASS: Center target at 0,0\n";
}

void test_quadrants() {
    CameraIntrinsics cam;
    cam.focal_length_px = 1000.0f;
    cam.cx = 320.0f;
    cam.cy = 240.0f;
    GimbalController controller(cam);
    controller.set_limits(-90.0f, 90.0f, -45.0f, 45.0f);

    // Quadrant 1: Top-Right (dx > 0, dy < 0) - pixel (420, 140)
    // dx = 100, dy = -100 -> az > 0, el > 0
    controller.reset_angles_for_test();
    GimbalCommand cmd1 = controller.command_from_pixel(420.0f, 140.0f);
    assert(cmd1.az_deg > 0.0f);
    assert(cmd1.el_deg > 0.0f);
    (void)cmd1;
    std::cout << "PASS: Quadrant 1 (Top-Right)\n";

    // Quadrant 2: Top-Left (dx < 0, dy < 0) - pixel (220, 140)
    // dx = -100, dy = -100 -> az < 0, el > 0
    controller.reset_angles_for_test();
    GimbalCommand cmd2 = controller.command_from_pixel(220.0f, 140.0f);
    assert(cmd2.az_deg < 0.0f);
    assert(cmd2.el_deg > 0.0f);
    (void)cmd2;
    std::cout << "PASS: Quadrant 2 (Top-Left)\n";

    // Quadrant 3: Bottom-Left (dx < 0, dy > 0) - pixel (220, 340)
    // dx = -100, dy = 100 -> az < 0, el < 0
    controller.reset_angles_for_test();
    GimbalCommand cmd3 = controller.command_from_pixel(220.0f, 340.0f);
    assert(cmd3.az_deg < 0.0f);
    assert(cmd3.el_deg < 0.0f);
    (void)cmd3;
    std::cout << "PASS: Quadrant 3 (Bottom-Left)\n";

    // Quadrant 4: Bottom-Right (dx > 0, dy > 0) - pixel (420, 340)
    // dx = 100, dy = 100 -> az > 0, el < 0
    controller.reset_angles_for_test();
    GimbalCommand cmd4 = controller.command_from_pixel(420.0f, 340.0f);
    assert(cmd4.az_deg > 0.0f);
    assert(cmd4.el_deg < 0.0f);
    (void)cmd4;
    std::cout << "PASS: Quadrant 4 (Bottom-Right)\n";
}

void test_limits_clamping() {
    CameraIntrinsics cam;
    cam.focal_length_px = 1000.0f;
    cam.cx = 320.0f;
    cam.cy = 240.0f;
    GimbalController controller(cam);
    controller.set_limits(-90.0f, 90.0f, -45.0f, 45.0f);

    // Request large offsets that exceed limits
    GimbalCommand cmd_max = controller.command_from_pixel(2000.0f, -2000.0f);
    assert(cmd_max.az_deg <= 90.0f);
    assert(cmd_max.el_deg <= 45.0f);
    (void)cmd_max;
    
    GimbalCommand cmd_min = controller.command_from_pixel(-2000.0f, 2000.0f);
    assert(cmd_min.az_deg >= -90.0f);
    assert(cmd_min.el_deg >= -45.0f);
    (void)cmd_min;
    std::cout << "PASS: Limits clamping\n";
}

int main() {
    test_center_target();
    test_quadrants();
    test_limits_clamping();
    std::cout << "\nAll geometry tests passed.\n";
    return 0;
}
