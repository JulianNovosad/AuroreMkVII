#!/usr/bin/env python3
"""
Parallel Qwen Code Agent Launcher

Launches multiple Qwen Code CLI instances in parallel, each with their own
subagent task. Uses tmux to manage multiple sessions.

Usage:
    python3 scripts/parallel-agents.py
"""

import subprocess
import sys
import os
from pathlib import Path
import shlex

# Task definitions - each runs in parallel
TASKS = [
    {
        "name": "GimbalController",
        "session": "gimbal",
        "prompt": r'''## Task: Implement GimbalController (Tasks 2.1 + 2.2)

Create these files for AuroreMkVII:

1. include/aurore/gimbal_controller.hpp - Header
2. src/actuation/gimbal_controller.cpp - Implementation  
3. tests/unit/gimbal_controller_test.cpp - Unit tests

### Header (include/aurore/gimbal_controller.hpp):

```cpp
#pragma once
#include <atomic>
#include <cmath>
#include <algorithm>

namespace aurore {

enum class GimbalSource { AUTO, FREECAM };

struct GimbalCommand { float az_deg{0.f}; float el_deg{0.f}; };

struct CameraIntrinsics {
    float focal_length_px{1128.f};
    float cx{768.f};
    float cy{432.f};
};

class GimbalController {
public:
    explicit GimbalController(const CameraIntrinsics& cam = {});
    GimbalCommand command_from_pixel(float centroid_x, float centroid_y, float gain = 1.0f);
    GimbalCommand command_absolute(float az_deg, float el_deg);
    void set_source(GimbalSource s) { source_.store(s, std::memory_order_release); }
    GimbalSource source() const { return source_.load(std::memory_order_acquire); }
    float current_az() const { return az_.load(std::memory_order_relaxed); }
    float current_el() const { return el_.load(std::memory_order_relaxed); }
    void set_limits(float az_min, float az_max, float el_min, float el_max);

private:
    CameraIntrinsics cam_;
    std::atomic<GimbalSource> source_{GimbalSource::AUTO};
    std::atomic<float> az_{0.f};
    std::atomic<float> el_{0.f};
    float az_min_{-90.f}, az_max_{90.f};
    float el_min_{-10.f}, el_max_{45.f};
};

}  // namespace aurore
```

### Implementation (src/actuation/gimbal_controller.cpp):

Key formulas:
- delta_theta = atan2(offset_px, focal_length_px) * (180 / PI)
- Pixel Y increases downward, elevation increases upward (negate dy)
- AUTO mode accumulates angles, FREECAM sets absolute
- All outputs clamped to limits

### Tests (tests/unit/gimbal_controller_test.cpp):

6 tests:
1. test_pixel_at_center_gives_zero_command
2. test_pixel_offset_gives_correct_angle  
3. test_auto_source_accumulates_angle
4. test_freecam_source_sets_absolute
5. test_limits_clamp_commands
6. test_source_switch_auto_to_freecam

### CMakeLists.txt:

Add to AURORE_SOURCES: src/actuation/gimbal_controller.cpp

Add test:
```cmake
add_executable(gimbal_controller_test
    tests/unit/gimbal_controller_test.cpp
    src/actuation/gimbal_controller.cpp
)
target_include_directories(gimbal_controller_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(gimbal_controller_test PRIVATE Threads::Threads)
add_test(NAME GimbalControllerTest COMMAND gimbal_controller_test)
```

### Verify:
```bash
cd /home/laptop/AuroreMkVII
cmake --build build-native --target gimbal_controller_test -j4
cd build-native && ./gimbal_controller_test
```

Build, test, commit.
''',
    },
    {
        "name": "ConfigLoader",
        "session": "config",
        "prompt": r'''## Task: Implement ConfigLoader (Tasks 3.1 + 3.2)

Create these files for AuroreMkVII:

1. include/aurore/config_loader.hpp - Header
2. src/common/config_loader.cpp - Implementation
3. tests/unit/config_loader_test.cpp - Unit tests

### Prerequisites:
```bash
sudo apt install nlohmann-json3-dev
```

### Header (include/aurore/config_loader.hpp):

```cpp
#pragma once
#include <memory>
#include <string>

namespace aurore {

class ConfigLoader {
public:
    explicit ConfigLoader(const std::string& path = "");
    bool load(const std::string& path);
    int get_int(const std::string& key, int default_value = 0) const;
    float get_float(const std::string& key, float default_value = 0.f) const;
    bool get_bool(const std::string& key, bool default_value = false) const;
    std::string get_string(const std::string& key, const std::string& default_value = "") const;
    bool is_loaded() const { return loaded_; }

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    bool loaded_{false};
};

}  // namespace aurore
```

### Implementation (src/common/config_loader.cpp):

Use nlohmann/json for parsing. Navigate dot-separated keys like "gimbal.azimuth.min_deg" by splitting and traversing JSON tree.

### Tests (tests/unit/config_loader_test.cpp):

6 tests using this JSON fixture:
```json
{
  "system": {"frame_rate_hz": 120, "use_preempt_rt": false},
  "gimbal": {"azimuth": {"velocity_limit_dps": 60.0}}
}
```

Tests:
1. test_load_returns_true_for_valid_file
2. test_load_returns_false_for_missing_file
3. test_get_int_reads_value
4. test_get_float_reads_value
5. test_get_bool_reads_value
6. test_missing_key_returns_default

### CMakeLists.txt:

Add to AURORE_SOURCES: src/common/config_loader.cpp

Add test:
```cmake
add_executable(config_loader_test
    tests/unit/config_loader_test.cpp
    src/common/config_loader.cpp
)
target_include_directories(config_loader_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(config_loader_test PRIVATE Threads::Threads nlohmann_json::nlohmann_json)
add_test(NAME ConfigLoaderTest COMMAND config_loader_test)
```

### Verify:
```bash
cd /home/laptop/AuroreMkVII
cmake --build build-native --target config_loader_test -j4
cd build-native && ./config_loader_test
```

Build, test, commit.
''',
    },
    {
        "name": "PythonClient",
        "session": "pyclient",
        "prompt": r'''## Task: Create Python Client MVP (Tasks 6.1 + 6.2)

Create these files:

1. aurore_link/aurore_pb2.py - Generated protobuf bindings
2. aurore_link/client.py - TCP client library
3. aurore_link/viewer.py - OpenCV imshow MVP viewer
4. aurore_link/README.md - Documentation

### Step 1: Generate Python Protobuf

```bash
cd /home/laptop/AuroreMkVII
mkdir -p aurore_link
export LD_LIBRARY_PATH=/home/laptop/.local/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
/home/laptop/.local/bin/protoc --python_out=aurore_link -I proto proto/aurore.proto
```

### Step 2: Create client.py

TCP client with:
- Connect to telemetry port 9000, command port 9002
- Length-prefixed protobuf framing (4-byte big-endian length + data)
- Callback on_telemetry for received messages
- send_mode_switch("AUTO"|"FREECAM")
- send_freecam(az, el, velocity)

### Step 3: Create viewer.py

OpenCV window showing:
- Current mode (AUTO/FREECAM)
- FCS state (BOOT, IDLE_SAFE, SEARCH, TRACKING, ARMED, FAULT)
- Frame count
- Gimbal angles (AZ, EL)
- Track centroid and confidence if valid

Keys: a=AUTO, f=FREECAM, q=Quit

### Step 4: Create README.md

Setup: pip3 install protobuf opencv-python
Usage: python3 viewer.py 127.0.0.1

### Verify:

1. Start MkVII: ./build-native/aurore --dry-run
2. Run viewer: cd aurore_link && python3 viewer.py
3. Verify telemetry displays

Build, test, commit.
''',
    },
    {
        "name": "BallisticsRK4",
        "session": "ballistics",
        "prompt": r'''## Task: Port G1 Drag + RK4 Ballistics from MkVI (Task 8.1)

Read reference: /home/laptop/AuroreMkVII/AuroreMkVI/src/logic.cpp lines 47-414

### Modify these files:

1. include/aurore/ballistic_solver.hpp - Add G1 drag + RK4 declarations
2. src/actuation/ballistic_solver.cpp - Implement G1 drag + RK4

### G1 Drag Model:

4-segment piecewise drag coefficient vs Mach number:
- Subsonic (Mach 0-0.8): cd = 0.2
- Transonic (Mach 0.8-1.2): cd = 0.4
- Supersonic (Mach 1.2-2.5): cd = 0.25
- Hypersonic (Mach 2.5-10): cd = 0.18

### RK4 Integrator:

State vector: [x, y, z, vx, vy, vz]
Derivative: [vx, vy, vz, ax, ay, az]

Physics:
- Drag acceleration: a_drag = -0.5 * rho * v^2 * cd * A / m
- Gravity: az -= 9.81

RK4 step:
- k1 = f(s)
- k2 = f(s + k1*0.5*dt)
- k3 = f(s + k2*0.5*dt)
- k4 = f(s + k3*dt)
- s_next = s + (k1 + 2*k2 + 2*k3 + k4)*dt/6

### Modify solve_kinetic() and solve_drop() to use RK4 + G1 drag.

### Add tests to tests/unit/ballistics_test.cpp:

1. test_rk4_vacuum_trajectory - RK4 matches analytical in vacuum
2. test_g1_drag_drop - G1 drag produces expected bullet drop

### Verify:
```bash
cd /home/laptop/AuroreMkVII
cmake --build build-native --target ballistics_test -j4
cd build-native && ./ballistics_test
```

Build, test, commit.
''',
    },
    {
        "name": "StateMachineWiring",
        "session": "statemachine",
        "prompt": r'''## Task: Wire StateMachine into main.cpp pipeline (Task 4.1)

Read existing files:
- /home/laptop/AuroreMkVII/include/aurore/state_machine.hpp
- /home/laptop/AuroreMkVII/src/state_machine/state_machine.cpp
- /home/laptop/AuroreMkVII/src/main.cpp

### Goal:

Integrate StateMachine into the main processing loop so state transitions occur based on:
- Detection events (SEARCH -> TRACKING when confidence > 0.85)
- Tracking loss (TRACKING -> SEARCH when confidence < 0.5 for 3 frames)
- Gimbal settled + ballistics p_hit > 0.95 (TRACKING -> ARMED)
- Gimbal unsettled (ARMED -> TRACKING)

### Implementation:

In main.cpp, find the track_compute thread lambda and add:

```cpp
// After computing track solution:
bool on_target = track_solution.confidence > 0.85f;
state_machine.on_detection(on_target ? 0.9f : 0.0f);

// After gimbal command:
bool gimbal_settled = std::abs(gimbal.az_error) < 0.5f && std::abs(gimbal.el_error) < 0.5f;
if (gimbal_settled) state_machine.on_gimbal_status(true);
```

### Verify:

```bash
cd /home/laptop/AuroreMkVII
cmake --build build-native --target aurore -j4
timeout 8 ./build-native/aurore --dry-run 2>&1 | grep -E "State:|IDLE|SEARCH|TRACKING"
```

Expected: State machine cycles through IDLE_SAFE -> SEARCH -> TRACKING states.

Build, test, commit.
''',
    },
    {
        "name": "AuroreLinkWiring",
        "session": "aurolink",
        "prompt": r'''## Task: Wire AuroreLinkServer into main.cpp (Task 5.1)

The AuroreLinkServer is already implemented. Wire it into main.cpp.

### Files to modify:

- /home/laptop/AuroreMkVII/src/main.cpp

### Implementation:

1. Add include: #include "aurore/aurore_link_server.hpp"

2. After camera init, create and start server:

```cpp
aurore::AuroreLinkConfig link_cfg;
link_cfg.telemetry_port = 9000;
link_cfg.command_port = 9002;
aurore::AuroreLinkServer link_server(link_cfg);
link_server.start();

// Install callbacks
link_server.set_mode_callback([&](aurore::LinkMode mode) {
    if (mode == aurore::LinkMode::FREECAM) {
        gimbal_controller.set_source(aurore::GimbalSource::FREECAM);
    } else {
        gimbal_controller.set_source(aurore::GimbalSource::AUTO);
    }
});
```

3. In track thread, broadcast telemetry each frame:

```cpp
aurore::Telemetry tel;
tel.set_timestamp_ns(aurore::get_timestamp());
tel.mutable_health()->set_frame_count(frame_sequence.load());
tel.mutable_gimbal()->set_az_deg(gimbal_controller.current_az());
tel.mutable_gimbal()->set_el_deg(gimbal_controller.current_el());
link_server.broadcast_telemetry(tel);
```

4. In shutdown sequence, before cleanup:

```cpp
link_server.stop();
```

### CMakeLists.txt:

Ensure ${Protobuf_LIBRARIES} is linked to aurore target.

### Verify:

```bash
cd /home/laptop/AuroreMkVII
cmake --build build-native --target aurore -j4
timeout 8 ./build-native/aurore --dry-run 2>&1 | grep -E "AuroreLink|listening"
```

Expected: AuroreLink listening: telemetry=9000 command=9002

Build, test, commit.
''',
    },
]

CWD = Path("/home/laptop/AuroreMkVII")
LOG_DIR = CWD / "agent_logs"
LOG_DIR.mkdir(exist_ok=True)


def check_prereqs():
    """Check if tmux and qwen are available."""
    try:
        subprocess.run(["tmux", "-V"], capture_output=True, check=True)
        print("tmux: OK")
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("ERROR: tmux not found. Install with: sudo apt install tmux")
        return False

    try:
        subprocess.run(["qwen", "--version"], capture_output=True, check=True)
        print("qwen CLI: OK")
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("ERROR: qwen CLI not found.")
        return False
    
    return True


def kill_existing_sessions():
    """Kill any existing agent sessions."""
    for task in TASKS:
        try:
            subprocess.run(
                ["tmux", "kill-session", "-t", task["session"]],
                capture_output=True,
                cwd=CWD,
            )
            print(f"  Killed existing session: {task['session']}")
        except subprocess.CalledProcessError:
            pass  # Session doesn't exist


def launch_agent(task: dict):
    """Launch a single agent in a tmux session."""
    # Escape the prompt for shell
    escaped_prompt = task["prompt"].replace("'", "'\"'\"'")
    
    cmd = [
        "tmux",
        "new-session",
        "-d",
        "-s",
        task["session"],
        f"cd {CWD} && qwen -y -p '{escaped_prompt}' 2>&1 | tee {LOG_DIR}/{task['session']}.log"
    ]

    print(f"  Launching {task['name']} in session '{task['session']}'...")
    subprocess.Popen(cmd, cwd=CWD)


def main():
    print("=" * 60)
    print("Parallel Qwen Code Agent Launcher")
    print(f"Working directory: {CWD}")
    print(f"Tasks to run: {len(TASKS)}")
    print("=" * 60)
    print()

    if not check_prereqs():
        sys.exit(1)

    print("\nCleaning up existing sessions...")
    kill_existing_sessions()

    print("\nLaunching agents in parallel...\n")
    for task in TASKS:
        launch_agent(task)

    print(f"\nLaunched {len(TASKS)} agents in parallel!")
    print("\nSession names:")
    for task in TASKS:
        print(f"  - {task['session']} ({task['name']})")

    print("\nTo monitor sessions:")
    print("  tmux list-sessions")
    print("  tmux attach -t <session-name>")
    print("\nTo view logs:")
    print(f"  tail -f {LOG_DIR}/<session>.log")
    print("\nWaiting 5 seconds for agents to initialize...")
    subprocess.run(["sleep", "5"])
    
    print("\nAgent status:")
    for task in TASKS:
        result = subprocess.run(
            ["tmux", "list-panes", "-t", task["session"], "-F", "#{pane_current_command}"],
            capture_output=True,
            text=True,
            cwd=CWD,
        )
        if result.returncode == 0:
            print(f"  [RUNNING] {task['name']} ({task['session']})")
        else:
            print(f"  [?] {task['name']} ({task['session']})")

    print("\n" + "=" * 60)
    print("Agents are running in background tmux sessions.")
    print("Monitor with: tmux attach -t <session>")
    print("=" * 60)


if __name__ == "__main__":
    main()
