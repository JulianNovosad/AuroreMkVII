# Issue reports in AuroreMkVII/docs: audit, fix, and maintenance summaries

**Session ID:** ses_24451c98affeGaph2RJLTCZwRv
**Created:** 4/23/2026, 8:50:49 PM
**Updated:** 4/23/2026, 9:06:26 PM

---

## User

if you had to give claude a task seeing this repo what would it be? i am trying to develop the final product. use this:

pi@raspberry:~/AuroreMkVII/docs $ tree
.
├── agent_logs
│   ├── aurolink.log
│   ├── ballistics.log
│   ├── config.log
│   ├── gimbal.log
│   ├── pyclient.log
│   └── statemachine.log
├── agent_prompt.md
├── agent_sessions
│   ├── ses_3239
│   │   ├── audit_report.md
│   │   └── maintenance_summary.md
│   └── ses_3240
│       ├── audit_report.md
│       ├── blackboard
│       └── maintenance_summary.md
├── benchmarks
│   └── membench.txt
├── dependencies.md
├── Doxyfile.in
├── fusion_hat_driver.md
├── imu_sensor.md
├── interlock_controller.md
├── issue_report.md
├── kernel_build_guide.md
├── kernel_spec.md
├── laser_rangefinder_page.png
├── laser_rangefinder_part_1.txt
├── laser_rangefinder_part_2.txt
├── plans
│   ├── 2026-03-06-engagement-pipeline.md
│   ├── 2026-03-06-verification-design.md
│   ├── 2026-03-06-verification-plan.md
│   └── 2026-03-08-mkVII-networked-fcs.md
├── reports
│   ├── 2026-03-07-verification.md
│   ├── 2026-03-08-audit-and-fix-summary.md
│   ├── 2026-03-09-clean-build-enforcement-summary.md
│   ├── 2026-03-10-audit-and-fix-summary.md
│   ├── 2026-03-10-maintenance-eval-summary.md
│   └── lrf_calibration_issue.md
├── rt_bench
├── state_machine.md
├── superpowers
│   ├── plans
│   │   ├── 2026-03-31-calibration-web-interface.md
│   │   ├── 2026-04-01-full-system-integration.md
│   │   └── 2026-04-01-unified-launch.md
│   └── specs
│       ├── 2026-03-31-calibration-web-interface-design.md
│       ├── 2026-04-01-full-system-integration-design.md
│       └── 2026-04-01-unified-launch-design.md
├── telemetry.md
├── test_policy.md
└── wifi_ap_setup.md

12 directories, 44 files
pi@raspberry:~/AuroreMkVII/docs $ 

and see specifically the issue reports file

---

## Assistant (Build · hy3-preview-free · 6.5s)

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/pi/AuroreMkVII/docs/issue_report.md"
}
```

**Output:**
```
<path>/home/pi/AuroreMkVII/docs/issue_report.md</path>
<type>file</type>
<content>1: # AuroreMkVII Issue Report
2: 
3: Compiled issues/bugs found in source code requiring investigation.
4: 
5: ---
6: 
7: ## Codebase Statistics
8: 
9: | Metric | Count |
10: |--------|-------|
11: | Total C++ Lines | ~9,000 |
12: | stderr errors/warnings | 170 |
13: | errno checks | 55 |
14: | catch blocks | 13 |
15: | TODO/FIXME | 3 |
16: | abort() calls | 11 |
17: | throw statements | 9 |
18: 
19: **Estimated Total Issues: ~270+** (unique error paths, not all will trigger)
20: 
21: ---
22: 
23: ### 1. FATAL Aborts (10)
24: 
25: | Location | Issue |
26: |----------|-------|
27: | `src/drivers/camera_wrapper.cpp:372` | `std::abort()` - malformed control |
28: | `src/drivers/camera_wrapper.cpp:409` | `std::abort()` - invalid PISP refcount |
29: | `src/drivers/camera_wrapper.cpp:429` | `std::abort()` - DMA buffer too small |
30: | `src/drivers/camera_wrapper.cpp:637` | `std::abort()` - frame timeout |
31: | `src/drivers/camera_wrapper.cpp:646` | `std::abort()` - frame validation |
32: | `src/drivers/camera_wrapper.cpp:670` | `std::abort()` - DMA invalid |
33: | `src/drivers/camera_wrapper.cpp:820` | `std::abort()` - capture failure |
34: | `src/drivers/camera_wrapper.cpp:827` | `std::abort()` - no captured frames |
35: | `src/drivers/camera_wrapper.cpp:934` | `std::abort()` - frame not valid |
36: | `src/drivers/camera_wrapper.cpp:938` | `std::abort()` - contract violation |
37: 
38: ---
39: 
40: ### 2. Camera Errors (16)
41: 
42: | Location | Issue |
43: |----------|-------|
44: | `src/drivers/camera_wrapper.cpp:196` | GPU eglChooseConfig failed |
45: | `src/drivers/camera_wrapper.cpp:208` | GPU eglCreatePbufferSurface failed |
46: | `src/drivers/camera_wrapper.cpp:219` | GPU eglCreateContext failed |
47: | `src/drivers/camera_wrapper.cpp:226` | GPU eglMakeCurrent failed |
48: | `src/drivers/camera_wrapper.cpp:408` | FATAL: libcamera init failed - No camera found or hardware error |
49: | `src/drivers/camera_wrapper.cpp:472` | CameraManager start failed |
50: | `src/drivers/camera_wrapper.cpp:478` | No cameras found |
51: | `src/drivers/camera_wrapper.cpp:486` | Camera acquire failed |
52: | `src/drivers/camera_wrapper.cpp:495` | generateConfiguration failed |
53: | `src/drivers/camera_wrapper.cpp:510` | Configuration invalid |
54: | `src/drivers/camera_wrapper.cpp:519` | configure() failed |
55: | `src/drivers/camera_wrapper.cpp:532` | Buffer allocation failed |
56: | `src/drivers/camera_wrapper.cpp:547` | mmap failed |
57: | `src/drivers/camera_wrapper.cpp:559` | No capture requests created |
58: | `src/drivers/camera_wrapper.cpp:583` | camera->start() failed |
59: 
60: ---
61: 
62: ### 3. Main.cpp Errors/Warnings (26+)
63: 
64: | Location | Issue |
65: |----------|-------|
66: | `src/main.cpp:89` | Failed to set SCHED_FIFO for thread |
67: | `src/main.cpp:101` | Failed to set CPU affinity for thread |
68: | `src/main.cpp:116` | FATAL: Failed to lock memory (mlockall) |
69: | `src/main.cpp:140` | Warning: Failed to set memlock limit |
70: | `src/main.cpp:152` | Warning: Failed to set stack limit |
71: | `src/main.cpp:191` | Failed to initialize capabilities |
72: | `src/main.cpp:198` | Failed to set capabilities |
73: | `src/main.cpp:203` | Failed to set permitted capabilities |
74: | `src/main.cpp:209` | Failed to apply capabilities |
75: | `src/main.cpp:225` | Failed to drop GID |
76: | `src/main.cpp:231` | Failed to drop UID |
77: | `src/main.cpp:287` | FATAL: Failed to lock memory - cannot guarantee real-time |
78: | `src/main.cpp:294` | Warning: Failed to load config/config.json, using defaults |
79: | `src/main.cpp:329` | SAFETY ACTION fault code triggered |
80: | `src/main.cpp:340` | FATAL: Failed to drop privileges |
81: | `src/main.cpp:363` | Camera initialization failed |
82: | `src/main.cpp:390` | Warning: Could not create socket dir |
83: | `src/main.cpp:398` | Warning: HUD socket failed to start |
84: | `src/main.cpp:407` | Warning: MJPEG streamer failed to start |
85: | `src/main.cpp:415` | Warning: MJPEG USB streamer failed to start |
86: | `src/main.cpp:453` | Warning: Interlock initialization failed |
87: | `src/main.cpp:474` | Warning: failed to open LRF device |
88: | `src/main.cpp:499` | Warning: Failed to load descriptor file |
89: | `src/main.cpp:516` | DualCamera: WARN - USB stream failed |
90: | `src/main.cpp:563` | AuroreLink: HEARTBEAT TIMEOUT |
91: | `src/main.cpp:572` | AuroreLink: EMERGENCY_INHIBIT |
92: | `src/main.cpp:644` | Warning: YOLO26n model not loaded |
93: | `src/main.cpp:647` | Warning: ONNX Runtime not available |
94: | `src/main.cpp:731` | Vision deadline missed |
95: | `src/main.cpp:755` | Vision capture exceeded deadline |
96: | `src/main.cpp:905` | OpticalGate WARN: USB/MIPI misalignment |
97: | `src/main.cpp:987` | Track compute exceeded deadline |
98: | `src/main.cpp:1093` | FusionHat: I2C error threshold exceeded |
99: | `src/main.cpp:1249` | Actuation exceeded deadline |
100: | `src/main.cpp:1280` | Safety fault detected! |
101: | `src/main.cpp:1284` | Emergency stop active |
102: | `src/main.cpp:1348` | Thread did not terminate |
103: 
104: ---
105: 
106: ### 4. Safety/ Security Issues
107: 
108: | Location | Issue |
109: |----------|-------|
110: | `src/common/security.hpp` | Defines fault codes that trigger FAULT state |
111: | `src/state_machine/state_machine.cpp:158` | Invalid transition to ARMED |
112: 
113: ---
114: 
115: ### 5. Network Errors (8)
116: 
117: | Location | Issue |
118: |----------|-------|
119: | `src/network/mjpeg_streamer.cpp:42` | socket() failed |
120: | `src/network/mjpeg_streamer.cpp:51` | bind() failed |
121: | `src/network/mjpeg_streamer.cpp:60` | listen() failed |
122: | `src/network/mjpeg_streamer.cpp:118` | accept() error |
123: | `src/network/aurore_link_server.cpp:60` | failed to bind ports |
124: | `src/network/aurore_link_server.cpp:235` | Invalid sync word |
125: | `src/network/aurore_link_server.cpp:249` | HMAC verification failed |
126: | `src/network/aurore_link_server.cpp:278` | Replay attack detected |
127: | `src/network/aurore_link_server.cpp:393` | EMERGENCY_INHIBIT received |
128: | `src/network/aurore_link_server.cpp:518` | HEARTBEAT TIMEOUT |
129: 
130: ---
131: 
132: ### 6. I2C/ Hardware Errors (12+)
133: 
134: | Location | Issue |
135: |----------|-------|
136: | `src/drivers/fusion_hat.cpp:74` | I2C timeout on write |
137: | `src/drivers/fusion_hat.cpp:82` | I2C NACK on write |
138: | `src/drivers/fusion_hat.cpp:100` | I2C timeout on write |
139: | `src/drivers/fusion_hat.cpp:120` | I2C slow response on write |
140: | `src/drivers/fusion_hat.cpp:147` | I2C timeout on read |
141: | `src/drivers/fusion_hat.cpp:172` | I2C timeout on read |
142: | `src/drivers/fusion_hat.cpp:192` | I2C slow response on read |
143: | `src/drivers/fusion_hat.cpp:228` | Invalid configuration |
144: | `src/drivers/fusion_hat.cpp:286` | Device not connected |
145: | `src/drivers/fusion_hat.cpp:296` | Failed to enable channel |
146: | `src/drivers/fusion_hat.cpp:304` | Failed to set period |
147: | `src/safety/interlock_controller.cpp:40` | Failed to open GPIO memory device |
148: | `src/safety/interlock_controller.cpp:56` | Failed to map GPIO memory |
149: | `src/safety/interlock_controller.cpp:116` | Invalid interlock configuration |
150: | `src/safety/interlock_controller.cpp:127` | GPIO initialization failed |
151: | `src/safety/interlock_controller.cpp:374` | Interlock self-test FAILED |
152: 
153: ---
154: 
155: ### 7. LRF/Detector Errors (10)
156: 
157: | Location | Issue |
158: |----------|-------|
159: | `src/drivers/laser_rangefinder.cpp:230` | UART open failed |
160: | `src/drivers/laser_rangefinder.cpp:245` | tcgetattr failed |
161: | `src/drivers/laser_rangefinder.cpp:273` | tcsetattr failed |
162: | `src/drivers/laser_rangefinder.cpp:412` | UART read error |
163: | `src/drivers/laser_rangefinder.cpp:517` | CRC mismatch |
164: | `src/drivers/usb_camera.cpp:135` | Invalid configuration |
165: | `src/drivers/usb_camera.cpp:174` | No USB webcam detected |
166: | `src/drivers/usb_camera.cpp:196` | Failed to open device |
167: | `src/drivers/usb_camera.cpp:228` | Cannot start: not initialized |
168: | `src/drivers/usb_camera.cpp:237` | Failed to grab initial frame |
169: | `src/vision/orb_detector.cpp:35` | File size check |
170: | `src/vision/dual_camera_manager.cpp:25` | Error: NULL MIPI camera |
171: | `src/vision/dual_camera_manager.cpp:41` | USB camera init failed |
172: | `src/vision/dual_camera_manager.cpp:47` | USB camera start failed |
173: | `src/vision/dual_camera_manager.cpp:88` | USB camera timeout |
174: | `src/vision/dual_camera_manager.cpp:160` | USB camera disconnected |
175: 
176: ---
177: 
178: ### 8. Unhandled Exceptions (8)
179: 
180: | Location | Issue |
181: |----------|-------|
182: | `src/main.cpp:362` | catch CameraException |
183: | `src/drivers/camera_wrapper.cpp:880` | CameraException handler |
184: | `src/vision/orb_detector.cpp:42` | Catch cv::Exception |
185: | `src/vision/yolo26_detector.cpp:74` | Catch Ort::Exception |
186: | `src/sensors/imu_receiver.cpp:91` | Catch all |
187: | `src/sensors/imu_receiver.cpp:326` | Catch all |
188: | `src/common/config_loader.cpp:65` | JSON parse_error |
189: | `src/common/config_loader.cpp:102` | JSON type_error |
190: | `src/common/config_loader.cpp:121` | JSON type_error |
191: | `src/common/config_loader.cpp:140` | JSON type_error |
192: | `src/common/config_loader.cpp:160` | JSON type_error |
193: 
194: ---
195: 
196: ### 9. TODO Items (3)
197: 
198: | Location | Issue |
199: |----------|-------|
200: | `src/drivers/camera_wrapper.cpp:254` | GPU shader implementation |
201: | `src/drivers/camera_wrapper.cpp:308` | GPU-based RAW10→BGR888 conversion |
202: | `src/drivers/camera_wrapper.cpp:968` | Full GPU conversion |
203: 
204: ---
205: 
206: ### 10. Socket/Connection Issues
207: 
208: | Location | Issue |
209: |----------|-------|
210: | `src/common/hud_socket.cpp:40` | socket() failed |
211: | `src/common/hud_socket.cpp:47` | fcntl(F_GETFL) failed |
212: | `src/common/hud_socket.cpp:61` | bind() failed |
213: | `src/common/hud_socket.cpp:69` | chmod() failed |
214: | `src/common/hud_socket.cpp:76` | listen() failed |
215: | `src/common/hud_socket.cpp:119` | SO_PEERCRED failed |
216: | `src/common/hud_socket.cpp:160` | accept() failed |
217: | `src/common/command_socket.cpp:46` | socket() failed |
218: | `src/common/command_socket.cpp:58` | bind() failed |
219: | `src/common/command_socket.cpp:65` | listen() failed |
220: | `src/sensors/imu_receiver.cpp:120` | socket() failed |
221: | `src/sensors/imu_receiver.cpp:127` | setsockopt failed |
222: | `src/sensors/imu_receiver.cpp:148` | bind() failed |
223: | `src/sensors/imu_receiver.cpp:236` | select() error |
224: | `src/sensors/imu_receiver.cpp:252` | recvfrom() error |
225: 
226: ---
227: 
228: ## Priority Issues
229: 
230: ### Critical (FATAL - causes system crash)
231: 1. Camera aborts (10 locations) - camera_wrapper.cpp
232: 2. Memory lock failures - main.cpp:116, 287
233: 3. Privilege drop failure - main.cpp:340
234: 4. Safety fault triggers - main.cpp:1280
235: 
236: ### High (Causes degraded operation)
237: 1. Vision deadline exceeded (multiple) - main.cpp
238: 2. Track compute exceeded - main.cpp
239: 3. I2C errors - fusion_hat.cpp
240: 4. Stream connection failures - mjpeg_streamer.cpp
241: 5. USB camera failures - usb_camera.cpp, dual_camera_manager.cpp
242: 
243: ### Medium (Warnings - investigate)
244: 1. Configuration warnings - config_loader.cpp
245: 2. YOLO/ONNX not loaded - main.cpp:644, 647
246: 3. Socket creation failures - various
247: 4. TODO implementations - GPU/ shader
248: 
249: ---
250: 
251: *Generated from codebase grep analysis*
252: 
253: ---
254: 
255: ## Current State Assessment (2026-04-23)
256: 
257: ### Timeline
258: | Metric | Value |
259: |--------|-------|
260: | First commit | 2026-03-08 |
261: | Latest commit | 2026-04-23 |
262: | Total commits | 152 |
263: | Development time | ~1.5 months |
264: 
265: ### Issues Status
266: | Category | Count | Fixed |
267: |----------|-------|-------|
268: | FATAL Aborts | 11 | **11 (100%)** |
269: | Camera Errors | 16 | ~8 (50%) |
270: | Main.cpp Errors | 26+ | ~10 (38%) |
271: | Safety/Security | ~5 | ~1 (20%) |
272: | Network Errors | 8 | ~2 (25%) |
273: | I2C/Hardware | 12+ | ~3 (25%) |
274: | LRF/Detector | 10 | ~2 (20%) |
275: | Unhandled Exceptions | 8 | ~3 (37%) |
276: | TODO Items | 3 | 0 (0%) |
277: | Socket/Connection | 11 | ~3 (27%) |
278: | **Total** | **~270+** | **~32 (12%)** |
279: 
280: ### Completion by Area
281: | Area | Done | Remaining |
282: |------|------|-----------|
283: | Camera (color, streaming) | 80% | WCET optimization |
284: | Real-time performance (WCET ≤5ms) | 30% | Deadline misses persist |
285: | Safety/security (spec §3-4) | 15% | HMAC, ECDSA, watchdog |
286: | Detection/tracking (YOLO26, KCF) | 35% | Performance tuning |
287: | Streaming/web interface | 80% | Working now |
288: | Hardware (I2C, FusionHAT, LRF) | 40% | Retry logic exists |
289: | Error handling (`abort()` removal) | **100%** | Done 2026-04-23 |
290: | Config loading | **100%** | Done 2026-04-23 |
291: | Heartbeat timeout | **100%** | Done 2026-04-23 |
292: 
293: ### Overall Completion
294: - **~35-40% complete** (by functionality)
295: - **~12% of issues resolved** (by count in report.md)
296: 
297: ### Development Timeline Estimate
298: - 1.5 months → 40% = ~3.75 months total estimated
299: - **Remaining: ~2-2.5 months** at current pace (152 commits / 1.5mo = ~101 commits/month)
300: 
301: ### Critical Blockers to "Done"
302: 1. **Vision capture WCET still >10ms** (target: ≤5ms) — BGR888 hardware output helps but RGB→BGR conversion adds latency
303: 2. **Safety fault transitions triggering** — root cause not fully diagnosed (consecutive misses vs watchdog vs I2C)
304: 3. **Security requirements (AM7-L1/L2-SEC)** mostly unimplemented — HMAC, ECDSA, secure boot
305: 4. **200+ issues in report.md untouched** — many are warnings/errors in non-critical paths
306: 5. **Color swap (R↔B)** — frame extraction shows R-B diff <2 (neutral scene or still swapped?)
307: 6. **Stream freezes** — aurore process death causes stream to hang (aurore-link doesn't detect)
308: 
309: ### Is the Product Fully Done?
310: **No.** The stream works now (color, 1280×720, 0 deadline misses), but:
311: - Real-time performance not meeting spec (WCET ≤5ms)
312: - Safety compliance largely incomplete
313: - Security requirements mostly unimplemented
314: - ~260 issues remain in report.md
315: - Still in active development phase

(End of file - total 315 lines)
</content>
```

---

## Assistant (Build · hy3-preview-free · 11.5s)

Implement the 3 pending GPU-based RAW10→BGR888 conversion TODOs in `src/drivers/camera_wrapper.cpp:254,308,968` to eliminate RGB→BGR conversion latency, reduce vision pipeline WCET to ≤5ms (core spec requirement), and resolve the R-B color swap issue.

---

## User

measure the end to end latency of the MkVII. include all stages. use averages, and average jitter

---

## Assistant (Build · hy3-preview-free · 6.3s)

I'll examine the timing infrastructure and measure end-to-end latency across all pipeline stages.

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/pi/AuroreMkVII/include/aurore/timing.hpp"
}
```

**Output:**
```
<path>/home/pi/AuroreMkVII/include/aurore/timing.hpp</path>
<type>file</type>
<content>1: /**
2:  * @file timing.hpp
3:  * @brief Real-time thread timing framework using clock_nanosleep
4:  *
5:  * Provides precise periodic thread wakeup with absolute time references
6:  * to prevent timer drift. Designed for SCHED_FIFO real-time threads
7:  * on Linux with PREEMPT_RT kernel.
8:  *
9:  * Key features:
10:  * - Absolute time sleep (TIMER_ABSTIME) prevents cumulative drift
11:  * - CLOCK_MONOTONIC for sleep (CLOCK_MONOTONIC_RAW not supported by nanosleep)
12:  * - CLOCK_MONOTONIC_RAW for timestamp capture (highest precision)
13:  * - Deadline miss detection and counting
14:  * - Phase offset support for pipelined execution
15:  *
16:  * @copyright Aurore MkVII Project - Educational/Personal Use Only
17:  */
18: 
19: #pragma once
20: 
21: #include <time.h>
22: 
23: #include <chrono>
24: #include <cstdint>
25: #include <cstring>
26: #include <stdexcept>
27: #include <string>
28: #include <system_error>
29: 
30: namespace aurore {
31: 
32: /**
33:  * @brief Clock ID enumeration for type safety
34:  */
35: enum class ClockId {
36:     Monotonic = CLOCK_MONOTONIC,         ///< For sleep operations
37:     MonotonicRaw = CLOCK_MONOTONIC_RAW,  ///< For timestamp capture (highest precision)
38:     Realtime = CLOCK_REALTIME,           ///< Wall-clock time (not recommended for RT)
39:     Boottime = CLOCK_BOOTTIME            ///< Includes suspend time
40: };
41: 
42: /**
43:  * @brief Convert ClockId to native clockid_t
44:  */
45: inline clockid_t to_clockid(ClockId id) noexcept { return static_cast<clockid_t>(id); }
46: 
47: /**
48:  * @brief High-resolution timestamp in nanoseconds
49:  *
50:  * Uses CLOCK_MONOTONIC_RAW for maximum precision and immunity
51:  * to NTP adjustments.
52:  *
53:  * Note: CLOCK_MONOTONIC_RAW wraps after ~584 years (2^64 nanoseconds).
54:  * For wrap-safe comparisons, use timestamp_diff_ns() instead of direct
55:  * subtraction.
56:  */
57: using TimestampNs = uint64_t;
58: 
59: /**
60:  * @brief Maximum safe timestamp difference (half of uint64_t range)
61:  *
62:  * Differences larger than this may indicate timestamp wrap or error.
63:  */
64: constexpr TimestampNs MAX_SAFE_TIMESTAMP_DIFF_NS = (1ULL << 63);
65: 
66: /**
67:  * @brief Calculate difference between two timestamps (wrap-safe)
68:  *
69:  * Uses signed arithmetic to handle timestamp wrap correctly.
70:  * Returns negative value if after < before.
71:  *
72:  * @param after Later timestamp
73:  * @param before Earlier timestamp
74:  * @return int64_t Difference in nanoseconds (negative if after < before)
75:  *
76:  * This function correctly handles wrap-around:
77:  * - If after = 0xFFFFFFFFFFFFFFFF and before = 0, returns -1
78:  * - If after = 0 and before = 0xFFFFFFFFFFFFFFFF, returns +1
79:  */
80: inline int64_t timestamp_diff_ns(TimestampNs after, TimestampNs before) noexcept {
81:     return static_cast<int64_t>(after - before);
82: }
83: 
84: /**
85:  * @brief Check if timestamp_a is after timestamp_b (wrap-safe)
86:  *
87:  * @param a First timestamp
88:  * @param b Second timestamp
89:  * @return true if a is after b
90:  */
91: inline bool timestamp_is_after(TimestampNs a, TimestampNs b) noexcept {
92:     return static_cast<int64_t>(a - b) > 0;
93: }
94: 
95: /**
96:  * @brief Check if timestamp is within window of reference (wrap-safe)
97:  *
98:  * @param timestamp Timestamp to check
99:  * @param reference Reference timestamp
100:  * @param window_ns Window size in nanoseconds
101:  * @return true if |timestamp - reference| <= window_ns
102:  */
103: inline bool timestamp_within_window(TimestampNs timestamp, TimestampNs reference,
104:                                     uint64_t window_ns) noexcept {
105:     const int64_t diff = timestamp_diff_ns(timestamp, reference);
106:     return diff >= 0 ? static_cast<uint64_t>(diff) <= window_ns
107:                      : static_cast<uint64_t>(-diff) <= window_ns;
108: }
109: 
110: /**
111:  * @brief Get current timestamp from specified clock
112:  *
113:  * @param clock Clock source (default: CLOCK_MONOTONIC_RAW)
114:  * @return TimestampNs Current time in nanoseconds since epoch
115:  *
116:  * @throws std::system_error if clock_gettime fails
117:  */
118: inline TimestampNs get_timestamp(ClockId clock = ClockId::MonotonicRaw) {
119:     struct timespec ts;
120:     if (clock_gettime(to_clockid(clock), &ts) != 0) {
121:         throw std::system_error(errno, std::system_category(), "clock_gettime failed");
122:     }
123:     return static_cast<uint64_t>(ts.tv_sec) * 1000000000UL + static_cast<uint64_t>(ts.tv_nsec);
124: }
125: 
126: /**
127:  * @brief Get current timestamp (noexcept version)
128:  *
129:  * @param clock Clock source
130:  * @param error Output parameter for error code (0 on success)
131:  * @return TimestampNs Current time, or 0 on error
132:  */
133: inline TimestampNs get_timestamp_safe(ClockId clock, int& error) noexcept {
134:     struct timespec ts;
135:     error = clock_gettime(to_clockid(clock), &ts);
136:     if (error != 0) {
137:         return 0;
138:     }
139:     return static_cast<uint64_t>(ts.tv_sec) * 1000000000UL + static_cast<uint64_t>(ts.tv_nsec);
140: }
141: 
142: /**
143:  * @brief Real-time thread timing controller
144:  *
145:  * Manages periodic wakeup for real-time threads using absolute time
146:  * sleep to prevent drift. Supports phase offsets for pipelined
147:  * execution.
148:  *
149:  * Usage:
150:  * @code
151:  *     // Initialize for 120Hz (8.333ms) with 2ms phase offset
152:  *     ThreadTiming timing(8333333, 2000000);
153:  *
154:  *     while (running) {
155:  *         timing.wait();  // Sleep until next period
156:  *
157:  *         if (timing.missed_deadline()) {
158:  *             // Handle deadline miss
159:  *         }
160:  *
161:  *         // Process frame...
162:  *     }
163:  * @endcode
164:  */
165: class ThreadTiming {
166:    public:
167:     /**
168:      * @brief Construct timing controller
169:      *
170:      * @param period_ns Period in nanoseconds (e.g., 8333333 for 120Hz)
171:      * @param phase_ns Phase offset in nanoseconds (default: 0)
172:      * @param clock Clock source for sleep (default: CLOCK_MONOTONIC)
173:      *
174:      * @throws std::system_error if clock initialization fails
175:      */
176:     explicit ThreadTiming(uint64_t period_ns, uint64_t phase_ns = 0,
177:                           ClockId clock = ClockId::Monotonic)
178:         : period_ns_(period_ns),
179:           phase_ns_(phase_ns),
180:           clock_(clock),
181:           cycle_count_(0),
182:           deadline_misses_(0),
183:           consecutive_misses_(0),
184:           initialized_(false),
185:           last_expected_wakeup_(0),
186:           last_actual_wakeup_(0) {
187:         init(period_ns, phase_ns);
188:     }
189: 
190:     /**
191:      * @brief Default constructor (requires manual init)
192:      */
193:     ThreadTiming() noexcept
194:         : period_ns_(0),
195:           phase_ns_(0),
196:           clock_(ClockId::Monotonic),
197:           cycle_count_(0),
198:           deadline_misses_(0),
199:           consecutive_misses_(0),
200:           initialized_(false) {}
201: 
202:     /**
203:      * @brief Initialize timing controller
204:      *
205:      * @param period_ns Period in nanoseconds
206:      * @param phase_ns Phase offset in nanoseconds
207:      *
208:      * @throws std::system_error if clock initialization fails
209:      */
210:     void init(uint64_t period_ns, uint64_t phase_ns = 0) {
211:         period_ns_ = period_ns;
212:         phase_ns_ = phase_ns;
213: 
214:         struct timespec now;
215:         if (clock_gettime(to_clockid(clock_), &now) != 0) {
216:             throw std::system_error(errno, std::system_category(), "clock_gettime failed in init");
217:         }
218: 
219:         // Calculate first wakeup time (aligned to period + phase)
220:         next_wakeup_ = now;
221:         next_wakeup_.tv_nsec += static_cast<long>(phase_ns_);
222:         normalize_timespec(next_wakeup_);
223: 
224:         // Add one period to ensure we're in the future
225:         add_period(next_wakeup_);
226: 
227:         initialized_ = true;
228:     }
229: 
230:     /**
231:      * @brief Wait until next period
232:      *
233:      * Blocks until the next scheduled wakeup time using absolute
234:      * time sleep. This prevents cumulative timer drift.
235:      *
236:      * @return true if wakeup was on time, false if deadline was missed
237:      *
238:      * @throws std::system_error if clock_nanosleep fails
239:      */
240:     bool wait() {
241:         if (!initialized_) {
242:             throw std::logic_error("ThreadTiming not initialized");
243:         }
244: 
245:         // Save the expected wakeup time before sleeping (for jitter calculation)
246:         const TimestampNs expected_wakeup = next_wakeup_ns();
247: 
248:         const int ret = clock_nanosleep(to_clockid(clock_), TIMER_ABSTIME, &next_wakeup_, nullptr);
249: 
250:         if (ret == 0) {
251:             // Save expected wakeup for jitter calculation (before advancing)
252:             last_expected_wakeup_ = expected_wakeup;
253: 
254:             // Success - get actual wakeup time
255:             struct timespec now;
256:             clock_gettime(to_clockid(clock_), &now);
257: 
258:             // Store actual wakeup for jitter calculation
259:             last_actual_wakeup_ = static_cast<uint64_t>(now.tv_sec) * 1000000000UL +
260:                                   static_cast<uint64_t>(now.tv_nsec);
261: 
262:             // Advance to next period AFTER waking up
263:             add_period(next_wakeup_);
264:             cycle_count_++;
265: 
266:             // Check if we actually woke up on time
267:             // Use wrap-safe comparison to handle timestamp wrap correctly
268:             // Compare against the expected wakeup time (before we advanced it)
269:             const int64_t jitter = timestamp_diff_ns(last_actual_wakeup_, expected_wakeup);
270:             if (jitter > static_cast<int64_t>(period_ns_)) {
271:                 // We woke up after the scheduled time + one period = missed deadline
272:                 deadline_misses_++;
273:                 consecutive_misses_++;
274: 
275:                 // Resynchronize to current time
276:                 next_wakeup_ = now;
277:                 add_period(next_wakeup_);
278: 
279:                 return false;
280:             }
281: 
282:             consecutive_misses_ = 0;
283:             return true;
284:         } else if (ret == EINTR) {
285:             // Interrupted by signal - restart with same absolute time
286:             // No need to recalculate - absolute time is still valid
287:             return wait();
288:         } else {
289:             throw std::system_error(ret, std::system_category(), "clock_nanosleep failed");
290:         }
291:     }
292: 
293:     /**
294:      * @brief Check if last wait missed deadline
295:      *
296:      * @return true if deadline was missed
297:      */
298:     bool missed_deadline() const noexcept { return consecutive_misses_ > 0; }
299: 
300:     /**
301:      * @brief Get total deadline miss count
302:      *
303:      * @return uint64_t Total number of deadline misses
304:      */
305:     uint64_t deadline_misses() const noexcept { return deadline_misses_; }
306: 
307:     /**
308:      * @brief Get consecutive deadline miss count
309:      *
310:      * @return uint64_t Consecutive misses (reset on successful wait)
311:      */
312:     uint64_t consecutive_misses() const noexcept { return consecutive_misses_; }
313: 
314:     /**
315:      * @brief Get cycle count
316:      *
317:      * @return uint64_t Number of successful wait cycles
318:      */
319:     uint64_t cycle_count() const noexcept { return cycle_count_; }
320: 
321:     /**
322:      * @brief Get period in nanoseconds
323:      *
324:      * @return uint64_t Period
325:      */
326:     uint64_t period_ns() const noexcept { return period_ns_; }
327: 
328:     /**
329:      * @brief Get next scheduled wakeup time
330:      *
331:      * @return TimestampNs Next wakeup time in nanoseconds
332:      */
333:     TimestampNs next_wakeup_ns() const noexcept {
334:         return static_cast<uint64_t>(next_wakeup_.tv_sec) * 1000000000UL +
335:                static_cast<uint64_t>(next_wakeup_.tv_nsec);
336:     }
337: 
338:     /**
339:      * @brief Calculate jitter (timing variation)
340:      *
341:      * @param actual_wakeup_ns Actual wakeup time from caller
342:      * @return int64_t Jitter in nanoseconds (positive = late, negative = early)
343:      */
344:     int64_t calculate_jitter(TimestampNs /*actual_wakeup_ns*/) const noexcept {
345:         // Use the last expected wakeup time saved by wait()
346:         // This is called after wait() returns, so use last_expected_wakeup_
347:         const int64_t expected = static_cast<int64_t>(last_expected_wakeup_);
348:         const int64_t actual = static_cast<int64_t>(last_actual_wakeup_);
349:         return actual - expected;
350:     }
351: 
352:    private:
353:     /**
354:      * @brief Normalize timespec (handle nanosecond overflow)
355:      */
356:     static void normalize_timespec(struct timespec& ts) {
357:         while (ts.tv_nsec >= 1000000000L) {
358:             ts.tv_sec++;
359:             ts.tv_nsec -= 1000000000L;
360:         }
361:         while (ts.tv_nsec < 0) {
362:             ts.tv_sec--;
363:             ts.tv_nsec += 1000000000L;
364:         }
365:     }
366: 
367:     /**
368:      * @brief Add one period to timespec
369:      */
370:     void add_period(struct timespec& ts) {
371:         // Correctly handle addition of unsigned nanoseconds to signed timespec fields
372:         // Perform calculation with uint64_t and then cast the result to signed types
373:         uint64_t seconds_to_add = period_ns_ / 1000000000ULL;
374:         uint64_t nsecs_to_add = period_ns_ % 1000000000ULL;
375: 
376:         ts.tv_sec += static_cast<time_t>(seconds_to_add);
377:         ts.tv_nsec += static_cast<long>(nsecs_to_add);
378:         normalize_timespec(ts);
379:     }
380: 
381:     uint64_t period_ns_;
382:     uint64_t phase_ns_;
383:     ClockId clock_;
384:     struct timespec next_wakeup_;
385:     uint64_t cycle_count_;
386:     uint64_t deadline_misses_;
387:     uint64_t consecutive_misses_;
388:     bool initialized_;
389: 
390:     // Last wakeup tracking for jitter calculation
391:     TimestampNs last_expected_wakeup_;
392:     TimestampNs last_actual_wakeup_;
393: };
394: 
395: /**
396:  * @brief Deadline monitor for tracking execution time bounds
397:  *
398:  * Usage:
399:  * @code
400:  *     DeadlineMonitor deadline(2000000);  // 2ms budget
401:  *
402:  *     deadline.start();
403:  *     process_frame();
404:  *     deadline.stop();
405:  *
406:  *     if (deadline.exceeded()) {
407:  *         // Handle overrun
408:  *     }
409:  * @endcode
410:  */
411: class DeadlineMonitor {
412:    public:
413:     /**
414:      * @brief Construct deadline monitor
415:      *
416:      * @param budget_ns Execution time budget in nanoseconds
417:      */
418:     explicit DeadlineMonitor(uint64_t budget_ns) noexcept
419:         : budget_ns_(budget_ns), start_ns_(0), end_ns_(0), running_(false) {}
420: 
421:     /**
422:      * @brief Start timing
423:      */
424:     void start() noexcept {
425:         start_ns_ = get_timestamp();
426:         running_ = true;
427:     }
428: 
429:     /**
430:      * @brief Stop timing and check deadline
431:      *
432:      * @return true if deadline was met, false if exceeded
433:      */
434:     bool stop() noexcept {
435:         end_ns_ = get_timestamp();
436:         running_ = false;
437:         return (end_ns_ - start_ns_) <= budget_ns_;
438:     }
439: 
440:     /**
441:      * @brief Check if deadline was exceeded
442:      *
443:      * @return true if execution time exceeded budget
444:      */
445:     bool exceeded() const noexcept {
446:         if (!running_ && end_ns_ == 0) return false;
447: 
448:         const uint64_t end = running_ ? get_timestamp() : end_ns_;
449:         return (end - start_ns_) > budget_ns_;
450:     }
451: 
452:     /**
453:      * @brief Get elapsed time
454:      *
455:      * @return uint64_t Elapsed time in nanoseconds
456:      */
457:     uint64_t elapsed_ns() const noexcept {
458:         const uint64_t end = running_ ? get_timestamp() : end_ns_;
459:         return end - start_ns_;
460:     }
461: 
462:     /**
463:      * @brief Get remaining time in budget
464:      *
465:      * @return uint64_t Remaining nanoseconds (0 if exceeded)
466:      */
467:     uint64_t remaining_ns() const noexcept {
468:         const uint64_t elapsed = elapsed_ns();
469:         return elapsed >= budget_ns_ ? 0 : budget_ns_ - elapsed;
470:     }
471: 
472:     /**
473:      * @brief Check if still running
474:      *
475:      * @return true if start() called without matching stop()
476:      */
477:     bool is_running() const noexcept { return running_; }
478: 
479:    private:
480:     uint64_t budget_ns_;
481:     uint64_t start_ns_;
482:     uint64_t end_ns_;
483:     bool running_;
484: };
485: 
486: /**
487:  * @brief Frame rate calculator
488:  *
489:  * Tracks actual frame rate over a sliding window.
490:  */
491: class FrameRateCalculator {
492:    public:
493:     /**
494:      * @brief Construct calculator
495:      *
496:      * @param window_size Number of frames to average over
497:      */
498:     explicit FrameRateCalculator(size_t window_size = 120) noexcept
499:         : window_size_(window_size), count_(0), first_timestamp_(0), last_timestamp_(0) {}
500: 
501:     /**
502:      * @brief Record frame timestamp
503:      *
504:      * @param timestamp_ns Frame timestamp in nanoseconds
505:      */
506:     void record_frame(TimestampNs timestamp_ns) noexcept {
507:         if (count_ == 0) {
508:             first_timestamp_ = timestamp_ns;
509:         }
510:         last_timestamp_ = timestamp_ns;
511:         count_++;
512:     }
513: 
514:     /**
515:      * @brief Get current frame rate
516:      *
517:      * @return double Frames per second
518:      */
519:     double fps() const noexcept {
520:         if (count_ < 2) return 0.0;
521: 
522:         const uint64_t delta = last_timestamp_ - first_timestamp_;
523:         if (delta == 0) return 0.0;
524: 
525:         return static_cast<double>(count_ - 1) * 1000000000.0 / static_cast<double>(delta);
526:     }
527: 
528:     /**
529:      * @brief Reset calculator
530:      */
531:     void reset() noexcept {
532:         count_ = 0;
533:         first_timestamp_ = 0;
534:         last_timestamp_ = 0;
535:     }
536: 
537:    private:
538:     size_t window_size_;
539:     size_t count_;
540:     TimestampNs first_timestamp_;
541:     TimestampNs last_timestamp_;
542: };
543: 
544: }  // namespace aurore

(End of file - total 544 lines)
</content>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/pi/AuroreMkVII/scripts/wcet_analysis.sh"
}
```

**Output:**
```
<path>/home/pi/AuroreMkVII/scripts/wcet_analysis.sh</path>
<type>file</type>
<content>1: #!/bin/bash
2: # wcet_analysis.sh - WCET measurement and analysis script
3: #
4: # Usage: ./scripts/wcet_analysis.sh [options]
5: #
6: # Options:
7: #   --samples=N    Number of samples (default: 1000000)
8: #   --output=DIR   Output directory (default: ./wcet_results)
9: 
10: set -e
11: 
12: SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
13: PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
14: BUILD_DIR="$PROJECT_DIR/build"
15: OUTPUT_DIR="./wcet_results"
16: SAMPLES=1000000
17: 
18: # Parse arguments
19: while [[ $# -gt 0 ]]; do
20:     case $1 in
21:         --samples=*)
22:             SAMPLES="${1#*=}"
23:             shift
24:             ;;
25:         --output=*)
26:             OUTPUT_DIR="${1#*=}"
27:             shift
28:             ;;
29:         *)
30:             echo "Unknown option: $1"
31:             exit 1
32:             ;;
33:     esac
34: done
35: 
36: echo "=== Aurore MkVII WCET Analysis ==="
37: echo "Samples: $SAMPLES"
38: echo "Output:  $OUTPUT_DIR"
39: echo ""
40: 
41: # Check if build exists
42: if [[ ! -f "$BUILD_DIR/aurore_wcet_measurement" ]]; then
43:     echo "Building WCET measurement tool..."
44:     mkdir -p "$BUILD_DIR"
45:     cd "$BUILD_DIR"
46:     cmake .. -DCMAKE_BUILD_TYPE=Release
47:     cmake --build . --target aurore_wcet_measurement -j$(nproc)
48: fi
49: 
50: # Create output directory
51: mkdir -p "$OUTPUT_DIR"
52: 
53: # Run WCET measurement
54: echo ""
55: echo "Running WCET measurement..."
56: "$BUILD_DIR/aurore_wcet_measurement" \
57:     --samples=$SAMPLES \
58:     --output="$OUTPUT_DIR/wcet_samples.csv" \
59:     --verbose
60: 
61: # Generate report
62: echo ""
63: echo "Generating analysis report..."
64: 
65: # Use Python if available for plotting
66: if command -v python3 &> /dev/null; then
67:     cat > "$OUTPUT_DIR/analyze.py" << 'PYTHON_SCRIPT'
68: import pandas as pd
69: import numpy as np
70: import matplotlib.pyplot as plt
71: from scipy import stats
72: import sys
73: 
74: # Load data
75: df = pd.read_csv(sys.argv[1] if len(sys.argv) > 1 else 'wcet_samples.csv')
76: samples = df['execution_time_ns'].values
77: 
78: # Basic statistics
79: print(f"\n=== Statistical Analysis ===")
80: print(f"Samples: {len(samples):,}")
81: print(f"Min:     {samples.min():,} ns")
82: print(f"Max:     {samples.max():,} ns")
83: print(f"Mean:    {samples.mean():,.0f} ns")
84: print(f"Median:  {np.median(samples):,} ns")
85: print(f"Std Dev: {samples.std():,.0f} ns")
86: 
87: # Percentiles
88: print(f"\n=== Percentiles ===")
89: print(f"P50:     {np.percentile(samples, 50):,} ns")
90: print(f"P90:     {np.percentile(samples, 90):,} ns")
91: print(f"P99:     {np.percentile(samples, 99):,} ns")
92: print(f"P99.9:   {np.percentile(samples, 99.9):,} ns")
93: print(f"P99.99:  {np.percentile(samples, 99.99):,} ns")
94: 
95: # WCET estimate (P99.99 + 10% margin)
96: wcet = np.percentile(samples, 99.99) * 1.1
97: print(f"\n=== WCET Estimate ===")
98: print(f"P99.99 + 10% margin: {wcet:,.0f} ns ({wcet/1e6:.2f} ms)")
99: print(f"Requirement (≤5ms):  {'PASS' if wcet <= 5e6 else 'FAIL'}")
100: 
101: # Histogram
102: plt.figure(figsize=(12, 8))
103: 
104: plt.subplot(2, 2, 1)
105: plt.hist(samples, bins=100, log=True, alpha=0.7)
106: plt.xlabel('Execution Time (ns)')
107: plt.ylabel('Frequency (log scale)')
108: plt.title('Execution Time Distribution')
109: plt.grid(True, alpha=0.3)
110: 
111: plt.subplot(2, 2, 2)
112: sorted_samples = np.sort(samples)
113: percentiles = np.linspace(0, 100, len(samples))
114: plt.plot(sorted_samples, percentiles)
115: plt.xlabel('Execution Time (ns)')
116: plt.ylabel('Percentile')
117: plt.title('CDF')
118: plt.grid(True, alpha=0.3)
119: plt.xlim(sorted_samples[int(len(samples)*0.9):])
120: 
121: plt.subplot(2, 2, 3)
122: tail = samples[samples > np.percentile(samples, 99)]
123: plt.hist(tail, bins=100, log=True, alpha=0.7)
124: plt.xlabel('Execution Time (ns)')
125: plt.ylabel('Frequency (log scale)')
126: plt.title('Tail Distribution (>P99)')
127: plt.grid(True, alpha=0.3)
128: 
129: plt.subplot(2, 2, 4)
130: jitter = np.diff(samples)
131: plt.hist(jitter, bins=100, alpha=0.7)
132: plt.xlabel('Jitter (ns)')
133: plt.ylabel('Frequency')
134: plt.title('Cycle-to-Cycle Jitter')
135: plt.grid(True, alpha=0.3)
136: 
137: plt.tight_layout()
138: plt.savefig('wcet_analysis.png', dpi=150)
139: print(f"\nPlot saved to: wcet_analysis.png")
140: 
141: # Extreme value analysis
142: print(f"\n=== Extreme Value Analysis ===")
143: # Fit Generalized Extreme Value distribution
144: block_size = 1000
145: block_maxima = [samples[i:i+block_size].max() for i in range(0, len(samples), block_size)]
146: shape, loc, scale = stats.genextreme.fit(block_maxima)
147: print(f"GEV shape parameter: {shape:.4f}")
148: print(f"GEV location: {loc:.0f} ns")
149: print(f"GEV scale: {scale:.0f} ns")
150: 
151: # Return level plot
152: return_periods = [10, 100, 1000, 10000]
153: print(f"\n=== Return Levels ===")
154: for rp in return_periods:
155:     level = stats.genextreme.ppf(1 - 1/rp, shape, loc, scale)
156:     print(f"{rp}-sample return level: {level:,.0f} ns")
157: PYTHON_SCRIPT
158: 
159:     python3 "$OUTPUT_DIR/analyze.py" "$OUTPUT_DIR/wcet_samples.csv"
160: else
161:     echo "Python3 not available - skipping detailed analysis"
162: fi
163: 
164: echo ""
165: echo "=== Analysis Complete ==="
166: echo "Results saved to: $OUTPUT_DIR/"

(End of file - total 166 lines)
</content>
```

**Tool: glob**

**Input:**
```json
{
  "pattern": "**/*timing*"
}
```

**Output:**
```
/home/pi/AuroreMkVII/tests/unit/timing_test.cpp
/home/pi/AuroreMkVII/include/aurore/timing.hpp
/home/pi/AuroreMkVII/AuroreMkVI/src/timing.h
```

---

## Assistant (Build · hy3-preview-free · 7.8s)

I'll measure end-to-end latency by examining the pipeline stages and using the telemetry system. Let me first check the telemetry infrastructure and main pipeline.

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/pi/AuroreMkVII/include/aurore/telemetry_writer.hpp"
}
```

**Output:**
```
<path>/home/pi/AuroreMkVII/include/aurore/telemetry_writer.hpp</path>
<type>file</type>
<content>1: /**
2:  * @file telemetry_writer.hpp
3:  * @brief Asynchronous telemetry logging for Aurore MkVII
4:  *
5:  * Per spec.md AM7-L2-HUD-002: Provides telemetry for remote HUD rendering
6:  * Per spec.md AM7-L3-TIM-001: Uses CLOCK_MONOTONIC_RAW for all timestamps
7:  *
8:  * Features:
9:  * - Async writer thread (non-blocking for control loops)
10:  * - CSV output (unified.csv format compatible with MkVI)
11:  * - JSON summary (run.json for quick status)
12:  * - Log rotation (configurable size/runs)
13:  *
14:  * SEC-010: Backpressure handling with configurable drop policy
15:  */
16: 
17: #pragma once
18: 
19: #include <atomic>
20: #include <cstdint>
21: #include <fstream>
22: #include <mutex>
23: #include <string>
24: #include <thread>
25: #include <type_traits>
26: 
27: #include "aurore/ring_buffer.hpp"
28: #include "aurore/telemetry_types.hpp"
29: 
30: namespace aurore {
31: 
32: // CsvLogEntry is queued and copied between threads — it must remain trivially copyable.
33: static_assert(std::is_trivially_copyable<CsvLogEntry>::value,
34:               "CsvLogEntry must be trivially copyable for safe async queue transfer");
35: 
36: // SEC-010: Backpressure policy options
37: enum class BackpressurePolicy : uint8_t {
38:     kDropOldest = 0,  // Drop oldest entries when queue full
39:     kDropNewest = 1,  // Drop new entries when queue full
40:     kBlock = 2        // Block producer (not recommended for real-time)
41: };
42: 
43: /**
44:  * @brief SEC-010: Queue statistics for monitoring
45:  */
46: struct TelemetryQueueStats {
47:     size_t current_depth = 0;
48:     size_t high_water_mark = 0;
49:     size_t max_depth = 0;
50:     uint64_t total_enqueued = 0;
51:     uint64_t total_dropped = 0;
52:     bool backpressure_active = false;
53: };
54: 
55: /**
56:  * @brief Configuration for telemetry writer
57:  *
58:  * SEC-010: Added backpressure configuration
59:  */
60: struct TelemetryConfig {
61:     std::string log_dir = "logs";        ///< Log directory
62:     std::string session_prefix = "run";  ///< Session file prefix
63:     size_t max_file_size_mb = 100;       ///< Rotate after N MB
64:     size_t max_sessions = 10;            ///< Keep N sessions max
65:     bool enable_csv = true;              ///< Write CSV logs
66:     bool enable_json = true;             ///< Write JSON summary
67:     bool enable_console = false;         ///< Mirror to stdout
68: 
69:     // SEC-010: Backpressure configuration
70:     size_t max_queue_size = 1024;      ///< Max entries in queue (must match ring buffer)
71:     size_t queue_high_water_pct = 80;  ///< High-water mark as % of max
72:     BackpressurePolicy backpressure_policy = BackpressurePolicy::kDropOldest;
73: 
74:     // SEC-003: HMAC-SHA256 key for log signing (empty = disabled)
75:     std::string hmac_key = "";
76: };
77: 
78: /**
79:  * @brief Asynchronous telemetry writer
80:  */
81: class TelemetryWriter {
82:    public:
83:     TelemetryWriter() = default;
84:     ~TelemetryWriter();
85: 
86:     bool start(const TelemetryConfig& config = TelemetryConfig());
87:     void stop();
88:     bool is_running() const { return running_.load(std::memory_order_acquire); }
89: 
90:     void log_frame(const DetectionData& detection, const TrackData& track,
91:                    const ActuationData& actuation, const SystemHealthData& health);
92: 
93:     void log_event(TelemetryEventId event_id, TelemetrySeverity severity,
94:                    const std::string& message);
95: 
96:     std::string get_session_path() const { return session_csv_path_; }
97: 
98:     uint64_t get_entries_written() const {
99:         return entries_written_.load(std::memory_order_acquire);
100:     }
101: 
102:     size_t get_queue_depth() const { return ring_buffer_.size(); }
103: 
104:     bool is_backpressure_active() const {
105:         return backpressure_active_.load(std::memory_order_acquire);
106:     }
107: 
108:     TelemetryQueueStats get_queue_stats() const;
109: 
110:     uint64_t get_entries_dropped() const {
111:         return entries_dropped_.load(std::memory_order_acquire);
112:     }
113: 
114:    private:
115:     void writer_loop();
116:     void write_csv_header();
117:     void write_csv_entry(const CsvLogEntry& entry);
118:     void write_json_summary();
119:     void rotate_logs();
120:     static uint64_t get_timestamp_ns();
121:     bool enqueue_entry(const CsvLogEntry& entry);
122:     void update_high_water_mark(size_t current_depth);
123:     void check_backpressure_state(size_t current_depth);
124: 
125:     TelemetryConfig config_;
126:     size_t queue_high_water_ = 800;
127: 
128:     std::thread writer_thread_;
129:     std::atomic<bool> running_{false};
130: 
131:     // SEC-010: Lock-free SPSC ring buffer for async logging
132:     LockFreeRingBuffer<CsvLogEntry, 1024> ring_buffer_;
133:     mutable std::mutex producer_mutex_;
134: 
135:     std::string session_csv_path_;
136:     std::ofstream csv_file_;
137:     uint32_t session_id_ = 0;
138:     uint64_t start_time_ns_ = 0;
139: 
140:     std::atomic<uint64_t> entries_written_{0};
141:     std::atomic<uint64_t> entries_dropped_{0};
142:     std::atomic<uint64_t> entries_enqueued_{0};
143:     std::atomic<size_t> high_water_mark_{0};
144:     std::atomic<bool> backpressure_active_{false};
145:     uint64_t first_frame_time_ns_ = 0;
146:     uint64_t last_frame_time_ns_ = 0;
147:     uint64_t frame_count_ = 0;
148: };
149: 
150: }  // namespace aurore

(End of file - total 150 lines)
</content>
```

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/pi/AuroreMkVII/src/main.cpp",
  "limit": 150
}
```

**Output:**
```
<path>/home/pi/AuroreMkVII/src/main.cpp</path>
<type>file</type>
<content>1: /**
2:  * @file main.cpp
3:  * @brief Aurore MkVII Fire Control System - Main Entry Point
4:  *
5:  * Real-time vision-based fire control system for Raspberry Pi 5.
6:  *
7:  * Architecture:
8:  * - vision_pipeline thread (SCHED_FIFO=90): 120Hz frame processing
9:  * - track_compute thread (SCHED_FIFO=85): Target tracking and prediction
10:  * - actuation_output thread (SCHED_FIFO=95): Gimbal servo commands
11:  * - safety_monitor thread (SCHED_FIFO=99): 1kHz health monitoring
12:  *
13:  * @copyright Aurore MkVII Project - Educational/Personal Use Only
14:  */
15: 
16: #include <sched.h>
17: #include <sys/mman.h>
18: #include <sys/resource.h>
19: #include <sys/types.h>
20: #include <unistd.h>
21: 
22: #include <atomic>
23: #include <cmath>
24: #include <csignal>
25: #include <cstdlib>
26: #include <cstring>
27: #include <fstream>
28: #include <iostream>
29: #include <memory>
30: #include <optional>
31: #include <string>
32: #include <filesystem>
33: #include <thread>
34: 
35: // libcap for privilege drop (optional - requires libcap-dev)
36: #ifdef HAVE_LIBCAP
37: #include <sys/capability.h>
38: #endif
39: 
40: #include "aurore.pb.h"
41: #include "aurore/aurore_link_server.hpp"
42: #include "aurore/ballistic_solver.hpp"
43: #include "aurore/camera_wrapper.hpp"
44: #include "aurore/config_loader.hpp"
45: #include "aurore/detector.hpp"  // For OrbDetector
46: #include "aurore/fusion_hat.hpp"
47: #include "aurore/gimbal_controller.hpp"
48: #include "aurore/hud_socket.hpp"
49: #include "aurore/interlock_controller.hpp"
50: #include "aurore/ring_buffer.hpp"
51: #include "aurore/safety_monitor.hpp"
52: #include "aurore/state_machine.hpp"  // For TrackSolution
53: #include "aurore/telemetry_writer.hpp"
54: #include "aurore/timing.hpp"
55: #include "aurore/tracker.hpp"  // For KcfTracker
56: #include "aurore/dual_camera_manager.hpp"
57: #include "aurore/usb_camera.hpp"
58: #include "aurore/command_socket.hpp"
59: #include "aurore/drivers/laser_rangefinder.hpp"
60: #include "aurore/yolo26_detector.hpp"
61: #include "aurore/sweep_pattern.hpp"
62: #include "aurore/mjpeg_streamer.hpp"
63: 
64: namespace {
65: 
66: // Global shutdown flag
67: std::atomic<bool> g_shutdown_requested(false);
68: 
69: // Global dry-run flag (set from main, read by thread helpers)
70: bool g_dry_run = false;
71: 
72: // Signal handler for graceful shutdown
73: void signal_handler(int signum) {
74:     if (signum == SIGINT || signum == SIGTERM) {
75:         g_shutdown_requested.store(true, std::memory_order_release);
76:         std::cout << "\nShutdown requested, cleaning up..." << std::endl;
77:     }
78: }
79: 
80: // Configure real-time thread
81: bool configure_rt_thread(const char* name, int priority, int cpu_affinity) {
82:     pthread_t thread = pthread_self();
83: 
84:     // Set SCHED_FIFO scheduling
85:     struct sched_param param;
86:     param.sched_priority = priority;
87: 
88:     if (pthread_setschedparam(thread, SCHED_FIFO, &param) != 0) {
89:         std::cerr << "Failed to set SCHED_FIFO for " << name << ": " << strerror(errno)
90:                   << std::endl;
91:         if (!g_dry_run) return false;
92:         // In dry-run mode: continue without RT scheduling
93:     }
94: 
95:     // Set CPU affinity
96:     cpu_set_t cpuset;
97:     CPU_ZERO(&cpuset);
98:     CPU_SET(static_cast<size_t>(cpu_affinity), &cpuset);
99: 
100:     if (pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset) != 0) {
101:         std::cerr << "Failed to set CPU affinity for " << name << ": " << strerror(errno)
102:                   << std::endl;
103:         if (!g_dry_run) return false;
104:         // In dry-run mode: continue without CPU affinity
105:     }
106: 
107:     std::cout << "Thread '" << name << "' configured: priority=" << priority
108:               << ", cpu=" << cpu_affinity << std::endl;
109: 
110:     return true;
111: }
112: 
113: // Lock memory to prevent page faults
114: bool lock_memory() {
115:     if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
116:         std::cerr << "FATAL: Failed to lock memory: " << strerror(errno) << std::endl;
117:         return false;
118:     }
119:     std::cout << "Memory locked successfully" << std::endl;
120:     return true;
121: }
122: 
123: // Maximum memory lock limit (64MB - sufficient for real-time buffers)
124: // This prevents runaway memory locking attacks
125: constexpr size_t MAX_MEMLOCK_BYTES = 64 * 1024 * 1024;
126: 
127: // Set resource limits with bounds
128: bool set_resource_limits() {
129:     struct rlimit rl;
130: 
131:     // Set bounded memlock limit (64MB max)
132:     // This is sufficient for:
133:     // - 4x DMA buffers @ 1536x864 RAW10: ~10MB
134:     // - Stack allocations for RT threads: ~1MB
135:     // - Safety margin: ~5MB
136:     rl.rlim_cur = MAX_MEMLOCK_BYTES;
137:     rl.rlim_max = MAX_MEMLOCK_BYTES;
138: 
139:     if (setrlimit(RLIMIT_MEMLOCK, &rl) != 0) {
140:         std::cerr << "Warning: Failed to set memlock limit: " << strerror(errno) << std::endl;
141:         return false;
142:     }
143:     std::cout << "Memory lock limit set to " << (MAX_MEMLOCK_BYTES / (1024 * 1024)) << " MB"
144:               << std::endl;
145: 
146:     // Set stack size limit for new threads (e.g., 8MB)
147:     // Required for some real-time threads to avoid stack overflow
148:     rl.rlim_cur = 8 * 1024 * 1024; // 8MB
149:     rl.rlim_max = 8 * 1024 * 1024; // 8MB
150: 

(Showing lines 1-150 of 1390. Use offset=151 to continue.)
</content>
```

---

## Assistant (Build · hy3-preview-free · 11.2s)

Let me check the telemetry types to see what's already being logged, then create a latency measurement approach.

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/pi/AuroreMkVII/include/aurore/telemetry_types.hpp"
}
```

**Output:**
```
<path>/home/pi/AuroreMkVII/include/aurore/telemetry_types.hpp</path>
<type>file</type>
<content>1: /**
2:  * @file telemetry_types.hpp
3:  * @brief Data structures for Aurore MkVII telemetry logging
4:  *
5:  * Per spec.md AM7-L3-TIM-001: All timestamps use CLOCK_MONOTONIC_RAW
6:  * Per spec.md AM7-L2-HUD-002: Telemetry includes detection, tracking, and actuation data
7:  *
8:  * SEC-009: All string operations use bounds-checked safe functions
9:  */
10: 
11: #pragma once
12: 
13: #include <algorithm>
14: #include <atomic>
15: #include <cmath>
16: #include <cstddef>
17: #include <cstdint>
18: #include <cstring>
19: #include <limits>
20: #include <string>
21: 
22: namespace aurore {
23: 
24: // SEC-009: Explicit size constants for all fixed buffers
25: static constexpr size_t kModule_name_max = 32;
26: static constexpr size_t kEvent_name_max = 32;
27: static constexpr size_t kMessage_max = 256;
28: 
29: /**
30:  * @brief Telemetry event severity levels
31:  */
32: enum class TelemetrySeverity : uint8_t {
33:     kDebug = 0,
34:     kInfo = 1,
35:     kWarning = 2,
36:     kError = 3,
37:     kCritical = 4
38: };
39: 
40: /**
41:  * @brief Telemetry event IDs (per ICD-004)
42:  */
43: enum class TelemetryEventId : uint16_t {
44:     // System events
45:     SYSTEM_BOOT = 0x0001,
46:     SYSTEM_SHUTDOWN = 0x0002,
47: 
48:     // Detection events
49:     DETECTION_VALID = 0x0101,
50:     DETECTION_INVALID = 0x0102,
51:     DETECTION_TIMEOUT = 0x0103,
52: 
53:     // Tracking events
54:     TRACK_ACQUIRED = 0x0201,
55:     TRACK_LOST = 0x0202,
56:     TRACK_UPDATED = 0x0203,
57: 
58:     // Actuation events
59:     ACTUATION_COMMAND = 0x0301,
60:     ACTUATION_LIMIT = 0x0302,
61:     ACTUATION_FAULT = 0x0303,
62: 
63:     // Safety events
64:     SAFETY_FAULT = 0x0401,
65:     SAFETY_INHIBIT_ENGAGED = 0x0402,
66:     SAFETY_INHIBIT_RELEASED = 0x0403,
67:     WATCHDOG_TIMEOUT = 0x0404,
68: 
69:     // Hardware events
70:     CAMERA_TIMEOUT = 0x0501,
71:     GIMBAL_TIMEOUT = 0x0502,
72:     TEMPERATURE_WARNING = 0x0503,
73:     TEMPERATURE_CRITICAL = 0x0504,
74:     I2C_FAULT = 0x0505,
75: 
76:     // Dual-stream vision events (AM7-L3-VIS-002)
77:     DUAL_STREAM_MIPI_FRAME = 0x0601,
78:     DUAL_STREAM_USB_FRAME = 0x0602,
79:     DUAL_STREAM_USB_DETECTION = 0x0603,
80:     DUAL_STREAM_OPTICAL_GATE_PASS = 0x0604,
81:     DUAL_STREAM_OPTICAL_GATE_FAIL = 0x0605,
82:     DUAL_STREAM_USB_DISCONNECT = 0x0606,
83:     DUAL_STREAM_USB_RECONNECT = 0x0607,
84:     DUAL_STREAM_LATENCY_WARNING = 0x0608,
85: };
86: 
87: /**
88:  * @brief SEC-009: Safe string copy with explicit bounds checking
89:  *
90:  * Prevents buffer overflow by:
91:  * 1. Checking destination buffer size at compile time (when possible)
92:  * 2. Always null-terminating
93:  * 3. Using explicit length parameter
94:  */
95: inline void safe_string_copy(char* dest, const char* src, size_t dest_size) {
96:     if (dest == nullptr || src == nullptr || dest_size == 0) {
97:         return;
98:     }
99: 
100:     // SEC-009: Explicit bounds check - never write beyond dest_size-1
101:     size_t src_len = std::strlen(src);
102:     size_t copy_len = std::min(src_len, dest_size - 1);
103: 
104:     // SEC-009: Use memcpy for controlled copy (safer than strncpy)
105:     std::memcpy(dest, src, copy_len);
106:     dest[copy_len] = '\0';  // Always null-terminate
107: }
108: 
109: /**
110:  * @brief SEC-009: Safe string copy from std::string
111:  */
112: inline void safe_string_copy(char* dest, const std::string& src, size_t dest_size) {
113:     if (dest == nullptr || dest_size == 0) {
114:         return;
115:     }
116: 
117:     size_t copy_len = std::min(src.size(), dest_size - 1);
118:     std::memcpy(dest, src.c_str(), copy_len);
119:     dest[copy_len] = '\0';
120: }
121: 
122: /**
123:  * @brief SEC-009: Validate string fits in buffer
124:  */
125: inline bool validate_string_fits(const char* str, size_t buffer_size) {
126:     if (str == nullptr || buffer_size == 0) {
127:         return false;
128:     }
129:     return std::strlen(str) < buffer_size;
130: }
131: 
132: /**
133:  * @brief Detection result from vision pipeline
134:  *
135:  * Represents a detected target (calibration sheet or helicopter)
136:  * SEC-009: All fields validated before use
137:  */
138: struct DetectionData {
139:     uint32_t frame_id = 0;      ///< Frame sequence number
140:     uint64_t timestamp_ns = 0;  ///< Timestamp (CLOCK_MONOTONIC_RAW)
141: 
142:     // Bounding box (pixel coordinates)
143:     float x = 0.0f;       ///< Center X in pixels
144:     float y = 0.0f;       ///< Center Y in pixels
145:     float width = 0.0f;   ///< Width in pixels
146:     float height = 0.0f;  ///< Height in pixels
147: 
148:     // Detection confidence
149:     float confidence = 0.0f;  ///< 0.0 - 1.0
150: 
151:     // Target classification
152:     uint8_t target_class = 0;  ///< 0=unknown, 1=calibration, 2=helicopter
153: 
154:     // SEC-009: Validation method
155:     bool is_valid() const {
156:         // Check for NaN/Inf
157:         if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
158:             !std::isfinite(height) || !std::isfinite(confidence)) {
159:             return false;
160:         }
161: 
162:         // Bounds checks
163:         if (confidence < 0.0f || confidence > 1.0f) {
164:             return false;
165:         }
166: 
167:         return confidence > 0.5f && width > 0.0f && height > 0.0f;
168:     }
169: };
170: 
171: /**
172:  * @brief Tracked target state from CSRT tracker
173:  *
174:  * Per AM7-L2-VIS-008: CSRT tracker for continuous target tracking
175:  * SEC-009: All fields validated before use
176:  */
177: struct TrackData {
178:     uint32_t track_id = 0;      ///< Unique track identifier
179:     uint64_t timestamp_ns = 0;  ///< Timestamp (CLOCK_MONOTONIC_RAW)
180: 
181:     // 3D position estimate (meters)
182:     float x = 0.0f;
183:     float y = 0.0f;
184:     float z = 0.0f;  ///< Range (estimated from target size)
185: 
186:     // Velocity estimate (m/s)
187:     float vx = 0.0f;
188:     float vy = 0.0f;
189:     float vz = 0.0f;
190: 
191:     // Track quality
192:     uint32_t hit_streak = 0;     ///< Consecutive successful updates
193:     uint32_t missed_frames = 0;  ///< Frames without detection
194:     float confidence = 0.0f;     ///< Track confidence 0.0 - 1.0
195: 
196:     // Bounding box (for visualization)
197:     float bbox_x = 0.0f;       ///< Top-left X in pixels
198:     float bbox_y = 0.0f;       ///< Top-left Y in pixels
199:     float bbox_width = 0.0f;   ///< Width in pixels
200:     float bbox_height = 0.0f;  ///< Height in pixels
201: 
202:     // SEC-009: Validation method
203:     bool is_valid() const {
204:         // Check for NaN/Inf in all float fields
205:         if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(vx) ||
206:             !std::isfinite(vy) || !std::isfinite(vz) || !std::isfinite(confidence) ||
207:             !std::isfinite(bbox_x) || !std::isfinite(bbox_y) || !std::isfinite(bbox_width) ||
208:             !std::isfinite(bbox_height)) {
209:             return false;
210:         }
211: 
212:         // Bounds checks
213:         if (confidence < 0.0f || confidence > 1.0f) {
214:             return false;
215:         }
216: 
217:         return confidence > 0.5f && hit_streak >= 2;
218:     }
219: };
220: 
221: /**
222:  * @brief Actuation command to gimbal
223:  *
224:  * Per AM7-L2-ACT-002: Elevation -10° to +45°, Azimuth ±90°
225:  * SEC-009: All fields validated before use
226:  */
227: struct ActuationData {
228:     uint32_t sequence = 0;      ///< Command sequence number
229:     uint64_t timestamp_ns = 0;  ///< Timestamp (CLOCK_MONOTONIC_RAW)
230: 
231:     // Commanded position (degrees)
232:     float azimuth_deg = 0.0f;    ///< -90° to +90°
233:     float elevation_deg = 0.0f;  ///< -10° to +45°
234: 
235:     // Command velocity (deg/s)
236:     float velocity_dps = 0.0f;  ///< Max 60°/s per AM7-L2-ACT-002
237: 
238:     // Execution status
239:     bool command_sent = false;
240:     bool limit_violation = false;
241: 
242:     // Latency tracking
243:     uint64_t compute_time_ns = 0;  ///< Time when command was computed
244:     uint64_t write_time_ns = 0;    ///< Time when I2C write completed
245: 
246:     // SEC-009: Validation method
247:     bool is_valid() const {
248:         // Check for NaN/Inf
249:         if (!std::isfinite(azimuth_deg) || !std::isfinite(elevation_deg) ||
250:             !std::isfinite(velocity_dps)) {
251:             return false;
252:         }
253: 
254:         // Bounds checks per AM7-L2-ACT-002
255:         if (azimuth_deg < -90.0f || azimuth_deg > 90.0f) {
256:             return false;
257:         }
258:         if (elevation_deg < -10.0f || elevation_deg > 45.0f) {
259:             return false;
260:         }
261:         if (velocity_dps < 0.0f || velocity_dps > 60.0f) {
262:             return false;
263:         }
264: 
265:         return true;
266:     }
267: };
268: 
269: /**
270:  * @brief System health metrics
271:  * SEC-009: All fields validated before use
272:  */
273: struct SystemHealthData {
274:     uint64_t timestamp_ns = 0;  ///< Timestamp
275: 
276:     // CPU metrics
277:     float cpu_temp_c = 0.0f;         ///< CPU temperature (°C)
278:     float cpu_usage_percent = 0.0f;  ///< CPU usage (0-100%)
279: 
280:     // Memory metrics
281:     uint32_t mem_used_mb = 0;   ///< Memory used (MB)
282:     uint32_t mem_total_mb = 0;  ///< Total memory (MB)
283: 
284:     // Frame rate
285:     float frame_rate = 0.0f;      ///< Actual FPS
286:     float jitter_percent = 0.0f;  ///< Jitter as % of frame period
287: 
288:     // SEC-009: Validation method
289:     bool is_valid() const {
290:         // Check for NaN/Inf
291:         if (!std::isfinite(cpu_temp_c) || !std::isfinite(cpu_usage_percent) ||
292:             !std::isfinite(frame_rate) || !std::isfinite(jitter_percent)) {
293:             return false;
294:         }
295: 
296:         // Bounds checks
297:         if (cpu_usage_percent < 0.0f || cpu_usage_percent > 100.0f) {
298:             return false;
299:         }
300:         if (jitter_percent < 0.0f || jitter_percent > 100.0f) {
301:             return false;
302:         }
303:         if (frame_rate < 0.0f || frame_rate > 1000.0f) {
304:             return false;
305:         }
306: 
307:         return true;
308:     }
309: };
310: 
311: /**
312:  * @brief Binary audit log entry per AM7-L3-SEC-003
313:  *
314:  * Format: {timestamp: u64, event_id: u16, severity: u8, data: u8[], hmac: u32[8]}
315:  * Each entry is individually signed with HMAC-SHA256.
316:  *
317:  * SEC-009: Fixed-size buffers with explicit bounds
318:  */
319: struct BinaryLogEntry {
320:     // Core fields (fixed size: 8 + 2 + 1 = 11 bytes + padding)
321:     uint64_t timestamp_ns{0};       ///< Timestamp (CLOCK_MONOTONIC_RAW)
322:     uint16_t event_id{0};           ///< Event ID (TelemetryEventId value)
323:     uint8_t severity{0};            ///< Severity (TelemetrySeverity value)
324:     uint8_t data_len{0};            ///< Length of data payload (0-64 bytes)
325: 
326:     // Data payload (fixed-size buffer for binary format)
327:     static constexpr size_t kMaxDataSize = 64;
328:     uint8_t data[kMaxDataSize]{};   ///< Variable-length data (padded to 64 bytes)
329: 
330:     // HMAC-SHA256 signature (8 x u32 = 32 bytes)
331:     uint32_t hmac[8]{};             ///< HMAC-SHA256 signature
332: 
333:     /**
334:      * @brief SEC-009: Set data payload with bounds checking
335:      */
336:     void set_data(const void* src, size_t len) {
337:         if (src == nullptr || len == 0) {
338:             data_len = 0;
339:             return;
340:         }
341:         data_len = static_cast<uint8_t>(std::min(len, static_cast<size_t>(kMaxDataSize)));
342:         std::memcpy(data, src, data_len);
343:     }
344: 
345:     /**
346:      * @brief SEC-009: Set data from string with bounds checking
347:      */
348:     void set_data(const std::string& str) {
349:         set_data(str.c_str(), str.size());
350:     }
351: 
352:     /**
353:      * @brief Set event ID from TelemetryEventId enum
354:      */
355:     void set_event_id(TelemetryEventId id) {
356:         event_id = static_cast<uint16_t>(id);
357:     }
358: 
359:     /**
360:      * @brief Set severity from TelemetrySeverity enum
361:      */
362:     void set_severity(TelemetrySeverity sev) {
363:         severity = static_cast<uint8_t>(sev);
364:     }
365: 
366:     /**
367:      * @brief SEC-009: Validate entry structure
368:      */
369:     bool is_valid() const {
370:         // Check data length is within bounds
371:         if (data_len > kMaxDataSize) {
372:             return false;
373:         }
374: 
375:         // Check event_id is in valid range (non-zero)
376:         if (event_id == 0) {
377:             return false;
378:         }
379: 
380:         // Check severity is in valid range
381:         if (severity > 4) {  // kCritical = 4
382:             return false;
383:         }
384: 
385:         return true;
386:     }
387: 
388:     /**
389:      * @brief Get total entry size (for binary serialization)
390:      */
391:     static constexpr size_t entry_size() {
392:         return sizeof(BinaryLogEntry);
393:     }
394: };
395: 
396: // SEC-009: Compile-time size verification for BinaryLogEntry
397: // Note: Struct has padding after data_len (1 byte padding to align hmac to 4-byte boundary)
398: // Layout: timestamp(8) + event_id(2) + severity(1) + data_len(1) + padding(2) + data(64) + hmac(32) = 110 bytes
399: // Actual layout with alignment: timestamp(8) + event_id(2) + severity(1) + data_len(1) + pad(4) + data(64) + hmac(32) = 112 bytes
400: // hmac offset: 8 + 2 + 1 + 1 + 4 (padding) = 16, then + 64 (data) = 80... but actual is 76
401: static_assert(sizeof(BinaryLogEntry) == 112, "BinaryLogEntry size mismatch (expected 112 bytes with padding)");
402: // Note: Actual offset is 76 due to compiler packing (4 bytes padding total: 2 after data_len, 2 more for alignment)
403: 
404: /**
405:  * @brief Simplified CSV log entry (MVP version)
406:  *
407:  * Per spec.md Section 8.4 ICD-004, simplified for MVP
408:  * Full implementation would use ring buffer with HMAC
409:  *
410:  * SEC-009: Fixed-size buffers with explicit bounds
411:  */
412: struct CsvLogEntry {
413:     // Timestamps
414:     uint64_t produced_ts_epoch_ms = 0;  ///< Epoch timestamp (for log correlation)
415:     uint64_t call_ts_epoch_ms = 0;      ///< When log was written
416: 
417:     // Frame info
418:     uint32_t cam_frame_id = 0;
419: 
420:     // Detection data
421:     float det_x = 0.0f;
422:     float det_y = 0.0f;
423:     float det_width = 0.0f;
424:     float det_height = 0.0f;
425:     float det_confidence = 0.0f;
426:     uint8_t det_target_class = 0;
427: 
428:     // Track data
429:     uint32_t track_id = 0;
430:     float track_x = 0.0f;
431:     float track_y = 0.0f;
432:     float track_z = 0.0f;
433:     uint32_t track_hit_streak = 0;
434:     float track_confidence = 0.0f;
435: 
436:     // Actuation data
437:     float servo_azimuth = 0.0f;
438:     float servo_elevation = 0.0f;
439:     bool servo_command_sent = false;
440: 
441:     // System health
442:     float cpu_temp_c = 0.0f;
443:     float cpu_usage_percent = 0.0f;
444: 
445:     // Dual-stream vision data (AM7-L3-VIS-002)
446:     uint32_t mipi_frame_id = 0;      ///< MIPI CSI-2 frame ID
447:     uint32_t usb_frame_id = 0;        ///< USB webcam frame ID
448:     uint64_t mipi_latency_us = 0;     ///< MIPI frame capture to process latency
449:     uint64_t usb_latency_us = 0;       ///< USB frame capture to process latency
450:     float usb_roi_x = 0.0f;           ///< USB detection ROI center X
451:     float usb_roi_y = 0.0f;            ///< USB detection ROI center Y
452:     bool optical_gate_passed = false; ///< Optical Logic Gate validation result
453: 
454:     // SEC-009: Fixed-size buffers with explicit constants
455:     char module[kModule_name_max] = {};
456:     char event[kEvent_name_max] = {};
457: 
458:     // ICD-004: HMAC-SHA256 for entry integrity
459:     uint8_t hmac[32] = {};
460: 
461:     /**
462:      * @brief SEC-009: Safe string copy for module name
463:      */
464:     void set_module(const char* name) { safe_string_copy(module, name, kModule_name_max); }
465: 
466:     /**
467:      * @brief SEC-009: Safe string copy for event name
468:      */
469:     void set_event(const char* name) { safe_string_copy(event, name, kEvent_name_max); }
470: 
471:     /**
472:      * @brief SEC-009: Safe string copy from std::string for module
473:      */
474:     void set_module(const std::string& name) { safe_string_copy(module, name, kModule_name_max); }
475: 
476:     /**
477:      * @brief SEC-009: Safe string copy from std::string for event
478:      */
479:     void set_event(const std::string& name) { safe_string_copy(event, name, kEvent_name_max); }
480: 
481:     /**
482:      * @brief SEC-009: Validate all float fields are finite
483:      */
484:     bool is_valid() const {
485:         // Check all float fields for NaN/Inf
486:         if (!std::isfinite(det_x) || !std::isfinite(det_y) || !std::isfinite(det_width) ||
487:             !std::isfinite(det_height) || !std::isfinite(det_confidence) ||
488:             !std::isfinite(track_x) || !std::isfinite(track_y) || !std::isfinite(track_z) ||
489:             !std::isfinite(track_confidence) || !std::isfinite(servo_azimuth) ||
490:             !std::isfinite(servo_elevation) || !std::isfinite(cpu_temp_c) ||
491:             !std::isfinite(cpu_usage_percent)) {
492:             return false;
493:         }
494: 
495:         // Bounds checks
496:         if (det_confidence < 0.0f || det_confidence > 1.0f) {
497:             return false;
498:         }
499:         if (track_confidence < 0.0f || track_confidence > 1.0f) {
500:             return false;
501:         }
502:         if (cpu_usage_percent < 0.0f || cpu_usage_percent > 100.0f) {
503:             return false;
504:         }
505: 
506:         return true;
507:     }
508: };
509: 
510: // SEC-009: Compile-time size checks
511: static_assert(sizeof(CsvLogEntry::module) == kModule_name_max, "module buffer size mismatch");
512: static_assert(sizeof(CsvLogEntry::event) == kEvent_name_max, "event buffer size mismatch");
513: 
514: }  // namespace aurore

(End of file - total 514 lines)
</content>
```

**Tool: grep**

**Input:**
```json
{
  "pattern": "timestamp|latency|vision_pipeline|track_compute|actuation",
  "path": "/home/pi/AuroreMkVII/src/main.cpp",
  "output_mode": "content"
}
```

**Output:**
```
Found 65 matches
/home/pi/AuroreMkVII/src/main.cpp:
  Line 8:  * - vision_pipeline thread (SCHED_FIFO=90): 120Hz frame processing
  Line 9:  * - track_compute thread (SCHED_FIFO=85): Target tracking and prediction
  Line 10:  * - actuation_output thread (SCHED_FIFO=95): Gimbal servo commands
  Line 311:     // actuation_deadline_ns: max allowed age of last actuation update (not WCET).
  Line 313:     safety_config.actuation_deadline_ns =
  Line 314:         static_cast<uint64_t>(config.get_int("safety.actuation_deadline_ns", 16666000));
  Line 321:         safety_config.actuation_deadline_ns = 1000000000;  // 1s
  Line 376:     // Initialize HUD socket for low-latency telemetry to aurore-link frontend
  Line 443:     // InterlockController for actuation safety gating
  Line 481:     // Track solution ring buffer (track_compute -> actuation_output)
  Line 609:     std::atomic<bool> actuation_running(false);
  Line 616:     // Detect thread shared state: track_compute → detect thread (frame supply)
  Line 617:     //                             detect thread → track_compute (result)
  Line 629:         std::atomic<bool>  result_fresh{false};  // set by detect thread, cleared by track_compute
  Line 712:     // This reduces jitter from ISP interrupts and track_compute context switching
  Line 714:         if (!configure_rt_thread("vision_pipeline", 90, 3)) {
  Line 747:                     safety_monitor.update_vision_frame(frame.sequence, frame.timestamp_ns);
  Line 753:                     const uint64_t now_warn = aurore::get_timestamp();
  Line 768:         if (!configure_rt_thread("track_compute", 85, 2)) {
  Line 777:         // INT-003 Fix: Track solution for actuation output
  Line 787:         uint64_t last_tick_ns = aurore::get_timestamp();
  Line 793:         // Vision watchdog: track last frame timestamp
  Line 812:             uint64_t now_ns = aurore::get_timestamp();
  Line 818:                 const uint64_t t0_track = aurore::get_timestamp();
  Line 822:                 const uint64_t t1_wrap = aurore::get_timestamp();
  Line 829:                         uint64_t now_tick = aurore::get_timestamp();
  Line 936:                                 last_tick_ns = aurore::get_timestamp();
  Line 957:                 const uint64_t t2_state = aurore::get_timestamp();
  Line 967:                 (void)aurore::get_timestamp();  // t3_dual timing point (kept for future profiling)
  Line 971:                     if (stream_status.mipi_latency_us > 5000) {
  Line 974:                                             "MIPI latency exceeded 5ms");
  Line 980:                 const uint64_t t4_release = aurore::get_timestamp();
  Line 985:                     const uint64_t now_warn = aurore::get_timestamp();
  Line 1019:     std::thread actuation_thread([&]() {
  Line 1020:         if (!configure_rt_thread("actuation_output", 95, 2)) {
  Line 1027:         actuation_running.store(true, std::memory_order_release);
  Line 1032:         uint64_t last_actuation_sequence = 0;
  Line 1049:             const uint64_t ta0 = aurore::get_timestamp();
  Line 1053:                 last_actuation_sequence++;
  Line 1056:             // Get current FSM state to gate actuation
  Line 1058:             bool actuation_allowed =
  Line 1059:                 interlock.is_actuation_allowed() &&
  Line 1075:             // Send servo commands only if actuation is gated and we're in a command state
  Line 1077:             if (actuation_allowed &&
  Line 1087:             if (actuation_allowed) {
  Line 1104:                 // Not in an active actuation state — clear accumulated startup errors so
  Line 1126:             // Only poll I2C when actuation is active — idle polling accumulates errors
  Line 1129:             const aurore::TimestampNs gimbal_ts = aurore::get_timestamp();
  Line 1131:             const auto az_opt = actuation_allowed ? fusion_hat.get_servo_angle(10) : std::nullopt;
  Line 1132:             const auto el_opt = actuation_allowed ? fusion_hat.get_servo_angle(11) : std::nullopt;
  Line 1155:             // Update safety monitor for actuation frame
  Line 1156:             if (last_actuation_sequence > 0) {
  Line 1157:                 const aurore::TimestampNs now = aurore::get_timestamp();
  Line 1158:                 safety_monitor.update_actuation_frame(last_actuation_sequence, now);
  Line 1162:             // HUD socket broadcast (low-latency JSON to frontend)
  Line 1178:             hud_frame.timestamp_ns = aurore::get_timestamp();
  Line 1181:             hud_frame.interlock = interlock.is_actuation_allowed() ? 1 : 0;
  Line 1212:             tel.set_timestamp_ns(aurore::get_timestamp());
  Line 1243:             const uint64_t ta2 = aurore::get_timestamp();
  Line 1245:             const uint64_t ta3 = aurore::get_timestamp();
  Line 1258:         actuation_running.store(false, std::memory_order_release);
  Line 1299:     uint64_t last_status_time = aurore::get_timestamp();
  Line 1304:         uint64_t now = aurore::get_timestamp();
  Line 1334:     actuation_running.store(false);
  Line 1358:     join_with_timeout(actuation_thread, 2000);
```

---

## Assistant (Build · hy3-preview-free · 11.9s)

Looking at the code, I can see there are already some latency measurements in place. Let me create a comprehensive end-to-end latency measurement by examining the pipeline timing points and creating a measurement tool.

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/pi/AuroreMkVII/src/main.cpp",
  "offset": 700,
  "limit": 100
}
```

**Output:**
```
<path>/home/pi/AuroreMkVII/src/main.cpp</path>
<type>file</type>
<content>700: 
701:     // Pin detect_thread to CPU 0 so ORT's internal thread pool cannot spill onto
702:     // CPU 1, which libcamera's callback thread needs.  Without this the camera
703:     // event loop is starved and try_capture_frame returns false ~90% of the time.
704:     {
705:         cpu_set_t cs;
706:         CPU_ZERO(&cs);
707:         CPU_SET(0, &cs);
708:         pthread_setaffinity_np(detect_thread.native_handle(), sizeof(cs), &cs);
709:     }
710: 
711:     // Vision pipeline thread - pinned to CPU 3 for isolation from track thread (CPU 2)
712:     // This reduces jitter from ISP interrupts and track_compute context switching
713:     std::thread vision_thread([&]() {
714:         if (!configure_rt_thread("vision_pipeline", 90, 3)) {
715:             return;
716:         }
717: 
718:         aurore::ThreadTiming timing(16666667, 0);    // 60Hz, matches camera FPS
719:         aurore::DeadlineMonitor deadline(5000000);  // 5ms budget (ISP jitter can reach 5-6ms)
720: 
721:         vision_running.store(true, std::memory_order_release);
722: 
723:         while (!g_shutdown_requested.load(std::memory_order_acquire) &&
724:                !safety_monitor.is_emergency_active()) {
725:             // RAII watchdog kick - auto-kick at end of each loop iteration
726:             aurore::WatchdogKick kick(safety_monitor);
727: 
728:             timing.wait();
729: 
730:             if (timing.missed_deadline()) {
731:                 std::cerr << "Vision deadline missed (consecutive: " << timing.consecutive_misses()
732:                           << ")" << std::endl;
733:             }
734: 
735:             // Capture frame
736:             if (camera && camera->is_running()) {
737:                 deadline.start();
738: 
739:                 aurore::ZeroCopyFrame frame;
740:                 if (camera->try_capture_frame(frame)) {
741:                     frame.sequence = frame_sequence.fetch_add(1, std::memory_order_relaxed);
742: 
743:                     // Push to ring buffer (drop if full)
744:                     frame_buffer.push(frame);
745: 
746:                     // Update safety monitor
747:                     safety_monitor.update_vision_frame(frame.sequence, frame.timestamp_ns);
748:                 }
749: 
750:                 deadline.stop();
751:                 if (deadline.exceeded()) {
752:                     static uint64_t last_vision_warn_ns = 0;
753:                     const uint64_t now_warn = aurore::get_timestamp();
754:                     if (now_warn - last_vision_warn_ns > 5000000000ULL) {  // at most once per 5s
755:                         std::cerr << "Vision capture exceeded deadline: "
756:                                   << deadline.elapsed_ns() / 1000 << "us\n";
757:                         last_vision_warn_ns = now_warn;
758:                     }
759:                 }
760:             }
761:         }  // kick_watchdog() called here automatically
762: 
763:         vision_running.store(false, std::memory_order_release);
764:     });
765: 
766:     // Track compute thread
767:     std::thread track_thread([&]() {
768:         if (!configure_rt_thread("track_compute", 85, 2)) {
769:             return;
770:         }
771: 
772:         aurore::ThreadTiming timing(8333333, 2000000);  // 120Hz, 2ms phase offset
773:         aurore::DeadlineMonitor deadline(5000000);      // 5ms budget (WCET spec per AGENTS.md)
774: 
775:         track_running.store(true, std::memory_order_release);
776: 
777:         // INT-003 Fix: Track solution for actuation output
778:         aurore::TrackSolution current_solution;
779:         current_solution.valid = false;
780: 
781:         // Vision pipeline integration: KCF tracker instance
782:         aurore::KcfTracker tracker;
783:         tracker.set_camera(camera.get());  // Zero-copy: tracker holds DMA buffer references
784: 
785:         // Autonomous sweep pattern for SEARCH state
786:         aurore::SweepPattern sweep;
787:         uint64_t last_tick_ns = aurore::get_timestamp();
788: 
789:         // Frame counter for detect thread feed rate (every 4th frame → ~30fps detection)
790:         uint64_t detect_frame_count = 0;
791:         constexpr uint64_t kDetectEveryN = 10;
792: 
793:         // Vision watchdog: track last frame timestamp
794:         // Initialized to 0 so the watchdog only arms after the first frame arrives.
795:         uint64_t last_frame_ns = 0;
796:         // 25ms = 3 frame periods at 120Hz: tolerates up to 2 consecutive dropped frames.
797:         // Dry-run uses 250ms because non-RT scheduling causes irregular frame delivery.
798:         const uint64_t kVisionWatchdogNs = dry_run ? 250000000ULL : 25000000ULL;
799: 

(Showing lines 700-799 of 1390. Use offset=800 to continue.)
</content>
```

**Tool: grep**

**Input:**
```json
{
  "pattern": "frame\\.timestamp_ns|track_solution\\.timestamp_ns",
  "path": "/home/pi/AuroreMkVII/src",
  "output_mode": "content"
}
```

**Output:**
```
Found 8 matches
/home/pi/AuroreMkVII/src/drivers/camera_wrapper.cpp:
  Line 817:         frame.timestamp_ns  = static_cast<TimestampNs>(meta.timestamp);
  Line 976:         frame.timestamp_ns  = get_timestamp(ClockId::MonotonicRaw);

/home/pi/AuroreMkVII/src/main.cpp:
  Line 747:                     safety_monitor.update_vision_frame(frame.sequence, frame.timestamp_ns);
  Line 1178:             hud_frame.timestamp_ns = aurore::get_timestamp();

/home/pi/AuroreMkVII/src/drivers/usb_camera.cpp:
  Line 295:     frame.timestamp_ns = get_timestamp(ClockId::MonotonicRaw);

/home/pi/AuroreMkVII/src/common/hud_socket.cpp:
  Line 311:     if (!is_message_fresh(frame.timestamp_ns)) {

/home/pi/AuroreMkVII/src/drivers/camera_auth.cpp:
  Line 44:     std::memcpy(out_header + offset, &frame.timestamp_ns, sizeof(frame.timestamp_ns));
  Line 45:     offset += sizeof(frame.timestamp_ns);
```

---

## Assistant (Build · hy3-preview-free · 29.3s)

I can see the pipeline has timestamp instrumentation. Let me create a latency measurement tool that computes end-to-end latency from camera capture to actuation output using the existing telemetry data.

**Tool: write**

**Input:**
```json
{
  "filePath": "/home/pi/AuroreMkVII/scripts/measure_latency.sh",
  "content": "#!/bin/bash\n# measure_latency.sh - Measure end-to-end latency of Aurore MkVII\n#\n# Measures latency across all pipeline stages:\n#   Camera Capture → Vision Pipeline → Track Compute → Actuation Output\n#\n# Usage: ./scripts/measure_latency.sh [--samples=N] [--output=DIR]\n\nset -e\n\nSCRIPT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\nPROJECT_DIR=\"$(dirname \"$SCRIPT_DIR\")\"\nBUILD_DIR=\"$PROJECT_DIR/build\"\nSAMPLES=100000\nOUTPUT_DIR=\"./latency_results\"\n\n# Parse arguments\nwhile [[ $# -gt 0 ]]; do\n    case $1 in\n        --samples=*)\n            SAMPLES=\"${1#*=}\"\n            shift\n            ;;\n        --output=*)\n            OUTPUT_DIR=\"${1#*=}\"\n            shift\n            ;;\n        *)\n            echo \"Unknown option: $1\"\n            exit 1\n            ;;\n    esac\ndone\n\necho \"=== Aurore MkVII End-to-End Latency Measurement ===\"\necho \"Samples: $SAMPLES\"\necho \"Output:  $OUTPUT_DIR\"\necho \"\"\n\n# Check if build exists, build if needed\nif [[ ! -f \"$BUILD_DIR/aurore_latency_measurement\" ]]; then\n    echo \"Building latency measurement tool...\"\n    mkdir -p \"$BUILD_DIR\"\n    cd \"$BUILD_DIR\"\n    cmake .. -DCMAKE_BUILD_TYPE=Release\n    cmake --build . --target aurore_latency_measurement -j$(nproc)\nfi\n\n# Create output directory\nmkdir -p \"$OUTPUT_DIR\"\n\n# Run measurement\necho \"\"\necho \"Running latency measurement (this may take a while)...\"\n\"$BUILD_DIR/aurore_latency_measurement\" \\\n    --samples=$SAMPLES \\\n    --output=\"$OUTPUT_DIR/latency_samples.csv\" \\\n    --verbose\n\n# Generate analysis report\necho \"\"\necho \"Generating latency analysis report...\"\n\nif command -v python3 &> /dev/null; then\n    cat > \"$OUTPUT_DIR/analyze_latency.py\" << 'PYTHON_SCRIPT'\nimport pandas as pd\nimport numpy as np\nimport matplotlib.pyplot as plt\nfrom scipy import stats\nimport sys\n\n# Load data\ncsv_file = sys.argv[1] if len(sys.argv) > 1 else 'latency_samples.csv'\ndf = pd.read_csv(csv_file)\n\nprint(f\"\\n=== End-to-End Latency Analysis ===\")\nprint(f\"Samples: {len(df):,}\")\n\n# Define stages\nstages = ['camera_capture', 'vision_done', 'track_done', 'actuation_done']\nstage_labels = ['Camera Capture', 'Vision Pipeline', 'Track Compute', 'Actuation Output']\n\n# Calculate stage-to-stage latencies\nfor i in range(len(stages) - 1):\n    col_name = f'{stages[i+1]}_us'\n    if col_name in df.columns:\n        latency = df[col_name] / 1000.0  # Convert to microseconds\n        print(f\"\\n--- {stage_labels[i]} → {stage_labels[i+1]} ---\")\n        print(f\"  Min:     {latency.min():.1f} µs\")\n        print(f\"  Max:     {latency.max():.1f} µs\")\n        print(f\"  Mean:    {latency.mean():.1f} µs\")\n        print(f\"  Median:  {latency.median():.1f} µs\")\n        print(f\"  Std Dev: {latency.std():.1f} µs\")\n        print(f\"  P50:     {np.percentile(latency, 50):.1f} µs\")\n        print(f\"  P90:     {np.percentile(latency, 90):.1f} µs\")\n        print(f\"  P99:     {np.percentile(latency, 99):.1f} µs\")\n        print(f\"  P99.9:   {np.percentile(latency, 99.9):.1f} µs\")\n\n# End-to-end latency\nif 'end_to_end_us' in df.columns:\n    e2e = df['end_to_end_us'] / 1000.0  # Convert to microseconds\n    print(f\"\\n=== END-TO-END LATENCY (Camera → Actuation) ===\")\n    print(f\"  Min:     {e2e.min():.1f} µs ({e2e.min()/1000:.2f} ms)\")\n    print(f\"  Max:     {e2e.max():.1f} µs ({e2e.max()/1000:.2f} ms)\")\n    print(f\"  Mean:    {e2e.mean():.1f} µs ({e2e.mean()/1000:.2f} ms)\")\n    print(f\"  Median:  {e2e.median():.1f} µs ({e2e.median()/1000:.2f} ms)\")\n    print(f\"  Std Dev: {e2e.std():.1f} µs\")\n    print(f\"  P50:     {np.percentile(e2e, 50):.1f} µs\")\n    print(f\"  P90:     {np.percentile(e2e, 90):.1f} µs\")\n    print(f\"  P95:     {np.percentile(e2e, 95):.1f} µs\")\n    print(f\"  P99:     {np.percentile(e2e, 99):.1f} µs\")\n    print(f\"  P99.9:   {np.percentile(e2e, 99.9):.1f} µs\")\n\n    # Spec check\n    spec_ms = 5.0  # WCET spec is ≤5ms per AGENTS.md\n    p99_ms = np.percentile(e2e, 99) / 1000.0\n    print(f\"\\n  Spec (≤{spec_ms}ms): {'PASS' if p99_ms <= spec_ms else 'FAIL'}\")\n    print(f\"  P99: {p99_ms:.2f} ms\")\n\n# Jitter analysis\nif 'end_to_end_us' in df.columns:\n    e2e = df['end_to_end_us'] / 1000.0\n    jitter = e2e.diff().dropna()\n    print(f\"\\n=== JITTER ANALYSIS (Cycle-to-Cycle) ===\")\n    print(f\"  Min:     {jitter.min():.1f} µs\")\n    print(f\"  Max:     {jitter.max():.1f} µs\")\n    print(f\"  Mean:    {jitter.mean():.1f} µs\")\n    print(f\"  Std Dev: {jitter.std():.1f} µs\")\n    print(f\"  Max Abs: {jitter.abs().max():.1f} µs\")\n\n# Generate plots\nprint(f\"\\nGenerating plots...\")\nplt.figure(figsize=(16, 10))\n\n# Plot 1: End-to-end latency histogram\nplt.subplot(2, 3, 1)\nplt.hist(e2e, bins=100, log=True, alpha=0.7, color='blue')\nplt.xlabel('End-to-End Latency (µs)')\nplt.ylabel('Frequency (log scale)')\nplt.title('End-to-End Latency Distribution')\nplt.grid(True, alpha=0.3)\nplt.axvline(e2e.mean(), color='red', linestyle='--', label=f'Mean: {e2e.mean():.0f}µs')\nplt.axvline(np.percentile(e2e, 99), color='orange', linestyle='--', label=f'P99: {np.percentile(e2e, 99):.0f}µs')\nplt.legend()\n\n# Plot 2: End-to-end CDF\nplt.subplot(2, 3, 2)\nsorted_e2e = np.sort(e2e)\npercentiles = np.linspace(0, 100, len(sorted_e2e))\nplt.plot(sorted_e2e, percentiles, color='blue')\nplt.xlabel('End-to-End Latency (µs)')\nplt.ylabel('Percentile')\nplt.title('End-to-End CDF')\nplt.grid(True, alpha=0.3)\nplt.xlim(sorted_e2e[int(len(sorted_e2e)*0.9):])\n\n# Plot 3: Stage latencies over time\nplt.subplot(2, 3, 3)\nfor stage in stages[1:]:\n    col_name = f'{stage}_us'\n    if col_name in df.columns:\n        plt.plot(df.index, df[col_name] / 1000.0, label=stage.replace('_', ' ').title(), alpha=0.7)\nplt.xlabel('Sample Number')\nplt.ylabel('Latency (µs)')\nplt.title('Stage Latencies Over Time')\nplt.legend()\nplt.grid(True, alpha=0.3)\n\n# Plot 4: Jitter histogram\nplt.subplot(2, 3, 4)\nplt.hist(jitter, bins=100, alpha=0.7, color='green')\nplt.xlabel('Jitter (µs)')\nplt.ylabel('Frequency')\nplt.title('Cycle-to-Cycle Jitter')\nplt.grid(True, alpha=0.3)\n\n# Plot 5: End-to-end over time\nplt.subplot(2, 3, 5)\nplt.plot(df.index, e2e, alpha=0.7, color='blue')\nplt.xlabel('Sample Number')\nplt.ylabel('End-to-End Latency (µs)')\nplt.title('End-to-End Latency Over Time')\nplt.grid(True, alpha=0.3)\n\n# Plot 6: Box plot of all stages\nplt.subplot(2, 3, 6)\nstage_data = []\nstage_labels_short = []\nfor stage in stages[1:]:\n    col_name = f'{stage}_us'\n    if col_name in df.columns:\n        stage_data.append(df[col_name] / 1000.0)\n        stage_labels_short.append(stage.replace('_', ' ').title())\nif stage_data:\n    plt.boxplot(stage_data, labels=stage_labels_short)\n    plt.ylabel('Latency (µs)')\n    plt.title('Stage Latency Comparison')\n    plt.xticks(rotation=45)\n    plt.grid(True, alpha=0.3)\n\nplt.tight_layout()\nplt.savefig(f'{OUTPUT_DIR}/latency_analysis.png', dpi=150)\nprint(f\"Plot saved to: {OUTPUT_DIR}/latency_analysis.png\")\n\nPYTHON_SCRIPT\n\n    python3 \"$OUTPUT_DIR/analyze_latency.py\" \"$OUTPUT_DIR/latency_samples.csv\"\nelse\n    echo \"Python3 not available - skipping detailed analysis\"\n    echo \"Raw data saved to: $OUTPUT_DIR/latency_samples.csv\"\nfi\n\necho \"\"\necho \"=== Measurement Complete ===\"\necho \"Results saved to: $OUTPUT_DIR/\"\necho \"  - latency_samples.csv: Raw measurement data\"\necho \"  - latency_analysis.png: Visualization plots\"\necho \"\"\necho \"To view the CSV:\"\necho \"  head -20 $OUTPUT_DIR/latency_samples.csv\"\n"
}
```

**Output:**
```
Wrote file successfully.
```

**Tool: bash**

**Input:**
```json
{
  "command": "chmod +x /home/pi/AuroreMkVII/scripts/measure_latency.sh",
  "description": "Make latency measurement script executable"
}
```

---

## Assistant (Build · hy3-preview-free · 35.7s)

Now I need to create the actual measurement tool. Let me create a C++ program that instruments the pipeline to measure end-to-end latency.

**Tool: write**

**Input:**
```json
{
  "filePath": "/home/pi/AuroreMkVII/src/latency_measurement.cpp",
  "content": "/**\n * @file latency_measurement.cpp\n * @brief End-to-end latency measurement tool for Aurore MkVII\n *\n * Measures latency across all pipeline stages:\n *   Camera Capture → Vision Pipeline → Track Compute → Actuation Output\n *\n * Outputs CSV with timestamps at each stage for analysis.\n */\n\n#include <atomic>\n#include <chrono>\n#include <csignal>\n#include <cstddef>\n#include <cstdint>\n#include <cstdlib>\n#include <fstream>\n#include <iostream>\n#include <limits>\n#include <memory>\n#include <string>\n#include <thread>\n#include <vector>\n\n#include \"aurore/camera_wrapper.hpp\"\n#include \"aurore/config_loader.hpp\"\n#include \"aurore/detector.hpp\"\n#include \"aurore/ring_buffer.hpp\"\n#include \"aurore/safety_monitor.hpp\"\n#include \"aurore/state_machine.hpp\"\n#include \"aurore/timing.hpp\"\n#include \"aurore/tracker.hpp\"\n\nnamespace {\n\nstd::atomic<bool> g_shutdown_requested(false);\n\nvoid signal_handler(int /*signum*/) {\n    g_shutdown_requested.store(true, std::memory_order_release);\n}\n\nstruct LatencySample {\n    uint64_t sample_id;\n\n    // Timestamps at each stage (CLOCK_MONOTONIC_RAW nanoseconds)\n    uint64_t camera_capture_ns;    // When frame was captured\n    uint64_t vision_done_ns;        // When vision pipeline finished processing\n    uint64_t track_done_ns;         // When track compute finished\n    uint64_t actuation_done_ns;     // When actuation command was sent\n\n    // Computed latencies (microseconds)\n    uint64_t capture_to_vision_us;\n    uint64_t vision_to_track_us;\n    uint64_t track_to_actuation_us;\n    uint64_t end_to_end_us;\n\n    // Frame info\n    uint32_t frame_sequence;\n    bool track_valid;\n};\n\n// Ring buffer for passing timestamps between threads\nstruct FrameTimestamps {\n    uint64_t camera_capture_ns;\n    uint64_t vision_done_ns;\n    uint32_t frame_sequence;\n    bool valid;\n};\n\n}  // namespace\n\nint main(int argc, char* argv[]) {\n    uint64_t num_samples = 100000;\n    std::string output_path = \"latency_samples.csv\";\n\n    // Parse arguments\n    for (int i = 1; i < argc; i++) {\n        std::string arg = argv[i];\n        if (arg.rfind(\"--samples=\", 0) == 0) {\n            num_samples = std::stoull(arg.substr(10));\n        } else if (arg.rfind(\"--output=\", 0) == 0) {\n            output_path = arg.substr(9);\n        } else if (arg == \"--verbose\" || arg == \"-v\") {\n            // Enable verbose output\n        }\n    }\n\n    std::cout << \"=== Aurore MkVII Latency Measurement ===\" << std::endl;\n    std::cout << \"Samples: \" << num_samples << std::endl;\n    std::cout << \"Output:  \" << output_path << std::endl;\n    std::cout << std::endl;\n\n    // Install signal handler\n    std::signal(SIGINT, signal_handler);\n    std::signal(SIGTERM, signal_handler);\n\n    // Load config\n    aurore::ConfigLoader config;\n    config.load(\"config.json\");\n\n    // Initialize camera\n    auto camera = std::make_unique<aurore::CameraWrapper>();\n    if (!camera->init(config)) {\n        std::cerr << \"Failed to initialize camera\" << std::endl;\n        return 1;\n    }\n    camera->start();\n\n    // Ring buffers for pipeline\n    aurore::LockFreeRingBuffer<aurore::ZeroCopyFrame, 32> frame_buffer;\n    aurore::LockFreeRingBuffer<aurore::TrackSolution, 32> track_buffer;\n\n    // Shared state for latency measurement\n    aurore::LockFreeRingBuffer<FrameTimestamps, 128> timestamp_buffer;\n    std::atomic<uint64_t> sample_count{0};\n    std::vector<LatencySample> samples;\n    samples.reserve(num_samples);\n\n    // Initialize detectors and trackers\n    aurore::KcfTracker tracker;\n    tracker.set_camera(camera.get());\n\n    // Timing variables\n    aurore::ThreadTiming vision_timing(8333333, 0);      // 120Hz, 0ms phase\n    aurore::ThreadTiming track_timing(8333333, 2000000);  // 120Hz, 2ms phase\n    aurore::ThreadTiming actuation_timing(8333333, 4000000);  // 120Hz, 4ms phase\n\n    std::atomic<bool> vision_running{false};\n    std::atomic<bool> track_running{false};\n    std::atomic<bool> actuation_running{false};\n\n    // Vision pipeline thread\n    std::thread vision_thread([&]() {\n        vision_running.store(true, std::memory_order_release);\n        aurore::DeadlineMonitor deadline(5000000);  // 5ms budget\n\n        while (!g_shutdown_requested.load(std::memory_order_acquire) &&\n               sample_count.load(std::memory_order_acquire) < num_samples) {\n            vision_timing.wait();\n\n            if (camera->is_running()) {\n                deadline.start();\n\n                aurore::ZeroCopyFrame frame;\n                if (camera->try_capture_frame(frame)) {\n                    const uint64_t vision_start_ns = aurore::get_timestamp();\n\n                    // Store timestamp for this frame\n                    FrameTimestamps fts;\n                    fts.camera_capture_ns = frame.timestamp_ns;\n                    fts.frame_sequence = frame.sequence;\n                    fts.valid = false;  // Will be set when vision processing done\n                    timestamp_buffer.push(fts);\n\n                    // Push frame to track thread\n                    frame_buffer.push(frame);\n\n                    const uint64_t vision_done_ns = aurore::get_timestamp();\n\n                    // Update the timestamp entry\n                    FrameTimestamps updated_fts;\n                    while (timestamp_buffer.pop(updated_fts)) {\n                        if (updated_fts.frame_sequence == frame.sequence) {\n                            updated_fts.vision_done_ns = vision_done_ns;\n                            updated_fts.valid = true;\n                            // Push back - track thread will read it\n                            timestamp_buffer.push(updated_fts);\n                            break;\n                        }\n                        // Push back other entries\n                        timestamp_buffer.push(updated_fts);\n                    }\n                }\n\n                deadline.stop();\n            }\n        }\n\n        vision_running.store(false, std::memory_order_release);\n    });\n\n    // Track compute thread\n    std::thread track_thread([&]() {\n        track_running.store(true, std::memory_order_release);\n        aurore::DeadlineMonitor deadline(5000000);  // 5ms budget\n\n        aurore::TrackSolution current_solution;\n        current_solution.valid = false;\n\n        while (!g_shutdown_requested.load(std::memory_order_acquire) &&\n               sample_count.load(std::memory_order_acquire) < num_samples) {\n            track_timing.wait();\n\n            deadline.start();\n\n            aurore::ZeroCopyFrame frame;\n            if (frame_buffer.pop(frame)) {\n                const uint64_t track_start_ns = aurore::get_timestamp();\n\n                // Run tracking\n                if (tracker.is_initialized()) {\n                    auto track_result = tracker.update(frame);\n                    current_solution.valid = track_result.has_value();\n                    if (track_result) {\n                        current_solution = *track_result;\n                        current_solution.timestamp_ns = aurore::get_timestamp();\n                    }\n                }\n\n                const uint64_t track_done_ns = aurore::get_timestamp();\n\n                // Update timestamp entry\n                FrameTimestamps fts;\n                while (timestamp_buffer.pop(fts)) {\n                    if (fts.frame_sequence == frame.sequence && fts.valid) {\n                        fts.track_done_ns = track_done_ns;\n                        track_buffer.push(current_solution);\n                        // Push to actuation thread via timestamp buffer\n                        timestamp_buffer.push(fts);\n                        break;\n                    }\n                    timestamp_buffer.push(fts);\n                }\n            }\n\n            deadline.stop();\n        }\n\n        track_running.store(false, std::memory_order_release);\n    });\n\n    // Actuation output thread\n    std::thread actuation_thread([&]() {\n        actuation_running.store(true, std::memory_order_release);\n\n        while (!g_shutdown_requested.load(std::memory_order_acquire) &&\n               sample_count.load(std::memory_order_acquire) < num_samples) {\n            actuation_timing.wait();\n\n            aurore::TrackSolution solution;\n            if (track_buffer.pop(solution) && solution.valid) {\n                const uint64_t actuation_start_ns = aurore::get_timestamp();\n\n                // Simulate actuation (no real hardware needed for latency measurement)\n                // In real system, this would send I2C commands\n                const uint64_t actuation_done_ns = aurore::get_timestamp();\n\n                // Read timestamp entry and compute latencies\n                FrameTimestamps fts;\n                while (timestamp_buffer.pop(fts)) {\n                    if (fts.frame_sequence == solution.track_id && fts.valid) {\n                        LatencySample sample;\n                        sample.sample_id = sample_count.fetch_add(1) + 1;\n                        sample.camera_capture_ns = fts.camera_capture_ns;\n                        sample.vision_done_ns = fts.vision_done_ns;\n                        sample.track_done_ns = fts.track_done_ns;\n                        sample.actuation_done_ns = actuation_done_ns;\n                        sample.frame_sequence = fts.frame_sequence;\n                        sample.track_valid = true;\n\n                        // Compute latencies (convert to microseconds)\n                        if (sample.vision_done_ns >= sample.camera_capture_ns) {\n                            sample.capture_to_vision_us =\n                                (sample.vision_done_ns - sample.camera_capture_ns) / 1000;\n                        }\n                        if (sample.track_done_ns >= sample.vision_done_ns) {\n                            sample.vision_to_track_us =\n                                (sample.track_done_ns - sample.vision_done_ns) / 1000;\n                        }\n                        if (sample.actuation_done_ns >= sample.track_done_ns) {\n                            sample.track_to_actuation_us =\n                                (sample.actuation_done_ns - sample.track_done_ns) / 1000;\n                        }\n                        if (sample.actuation_done_ns >= sample.camera_capture_ns) {\n                            sample.end_to_end_us =\n                                (sample.actuation_done_ns - sample.camera_capture_ns) / 1000;\n                        }\n\n                        samples.push_back(sample);\n                        break;\n                    }\n                    timestamp_buffer.push(fts);\n                }\n            }\n        }\n\n        actuation_running.store(false, std::memory_order_release);\n    });\n\n    // Wait for completion\n    while (sample_count.load(std::memory_order_acquire) < num_samples &&\n           !g_shutdown_requested.load(std::memory_order_acquire)) {\n        std::this_thread::sleep_for(std::chrono::milliseconds(100));\n    }\n\n    // Signal shutdown\n    g_shutdown_requested.store(true, std::memory_order_release);\n\n    // Wait for threads\n    if (vision_thread.joinable()) vision_thread.join();\n    if (track_thread.joinable()) track_thread.join();\n    if (actuation_thread.joinable()) actuation_thread.join();\n\n    camera->stop();\n\n    // Write CSV output\n    std::cout << \"Writing \" << samples.size() << \" samples to \" << output_path << std::endl;\n\n    std::ofstream csv(output_path);\n    if (!csv.is_open()) {\n        std::cerr << \"Failed to open output file: \" << output_path << std::endl;\n        return 1;\n    }\n\n    // CSV header\n    csv << \"sample_id,camera_capture_ns,vision_done_ns,track_done_ns,actuation_done_ns,\"\n        << \"capture_to_vision_us,vision_to_track_us,track_to_actuation_us,end_to_end_us,\"\n        << \"frame_sequence,track_valid\\n\";\n\n    // CSV data\n    for (const auto& s : samples) {\n        csv << s.sample_id << \",\" << s.camera_capture_ns << \",\" << s.vision_done_ns << \",\"\n            << s.track_done_ns << \",\" << s.actuation_done_ns << \",\"\n            << s.capture_to_vision_us << \",\" << s.vision_to_track_us << \",\"\n            << s.track_to_actuation_us << \",\" << s.end_to_end_us << \",\" << s.frame_sequence\n            << \",\" << (s.track_valid ? \"1\" : \"0\") << \"\\n\";\n    }\n\n    csv.close();\n\n    // Print summary statistics\n    if (!samples.empty()) {\n        std::vector<uint64_t> e2e_values;\n        for (const auto& s : samples) {\n            e2e_values.push_back(s.end_to_end_us);\n        }\n\n        uint64_t min_e2e = *std::min_element(e2e_values.begin(), e2e_values.end());\n        uint64_t max_e2e = *std::max_element(e2e_values.begin(), e2e_values.end());\n        uint64_t sum_e2e = 0;\n        for (auto v : e2e_values) sum_e2e += v;\n        double mean_e2e = static_cast<double>(sum_e2e) / e2e_values.size();\n\n        // Compute std dev\n        double variance = 0;\n        for (auto v : e2e_values) {\n            double diff = static_cast<double>(v) - mean_e2e;\n            variance += diff * diff;\n        }\n        variance /= e2e_values.size();\n        double stddev_e2e = std::sqrt(variance);\n\n        std::cout << \"\\n=== SUMMARY STATISTICS ===\" << std::endl;\n        std::cout << \"Samples collected: \" << samples.size() << std::endl;\n        std::cout << \"\\nEnd-to-End Latency (µs):\" << std::endl;\n        std::cout << \"  Min:     \" << min_e2e << \" µs (\" << min_e2e / 1000.0 << \" ms)\"\n                  << std::endl;\n        std::cout << \"  Max:     \" << max_e2e << \" µs (\" << max_e2e / 1000.0 << \" ms)\"\n                  << std::endl;\n        std::cout << \"  Mean:    \" << static_cast<uint64_t>(mean_e2e) << \" µs (\"\n                  << mean_e2e / 1000.0 << \" ms)\" << std::endl;\n        std::cout << \"  Std Dev: \" << static_cast<uint64_t>(stddev_e2e) << \" µs\" << std::endl;\n        std::cout << \"  Jitter (±): \" << static_cast<uint64_t>(stddev_e2e) << \" µs\" << std::endl;\n    }\n\n    std::cout << \"\\n=== Measurement Complete ===\" << std::endl;\n\n    return 0;\n}\n"
}
```

**Output:**
```
Wrote file successfully.
```

---

## Assistant (Build · hy3-preview-free · 6.4s)

Now I need to add the new measurement tool to the build system. Let me check and update CMakeLists.txt.

**Tool: read**

**Input:**
```json
{
  "filePath": "/home/pi/AuroreMkVII/CMakeLists.txt",
  "limit": 100
}
```

**Output:**
```
<path>/home/pi/AuroreMkVII/CMakeLists.txt</path>
<type>file</type>
<content>1: # Aurore MkVII - Fire Control System
2: # CMakeLists.txt - Main build configuration
3: #
4: # Build instructions:
5: #   mkdir build && cd build
6: #   cmake .. -DCMAKE_BUILD_TYPE=Release
7: #   cmake --build . -j$(nproc)
8: #
9: # Requirements:
10: #   - CMake 3.16+
11: #   - GCC 12+ or clang 15+ (aarch64-linux-gnu for Raspberry Pi)
12: #   - libcamera-dev
13: #   - libopencv-dev (>= 4.5)
14: #   - Linux with PREEMPT_RT kernel (optional but recommended)
15: 
16: cmake_minimum_required(VERSION 3.16)
17: 
18: project(AuroreMkVII
19:     VERSION 0.1.0
20:     DESCRIPTION "Fire Control System - Real-time Vision and Actuation"
21:     LANGUAGES CXX
22: )
23: 
24: # Configure CTest to run serially (avoid CPU contention on laptop)
25: include(CTest)
26: set(CTEST_PARALLEL_LEVEL 1)
27: 
28: # ============================================================================
29: # Build Options
30: # ============================================================================
31: 
32: option(AURORE_ENABLE_NEON "Enable ARM NEON SIMD optimizations" ON)
33: option(AURORE_ENABLE_GPU "Enable VideoCore VII GPU acceleration" OFF)
34: if(AURORE_ENABLE_GPU)
35:     add_definitions(-DAURORE_USE_GPU)
36: endif()
37: option(AURORE_ENABLE_TESTS "Build unit tests" ON)
38: option(AURORE_ENABLE_COVERAGE "Enable code coverage" OFF)
39: option(AURORE_ENABLE_SANITIZERS "Enable sanitizers (debug only)" OFF)
40: option(AURORE_REALTIME "Enable real-time optimizations" ON)
41: 
42: # Removed AURORE_LAPTOP_BUILD as mocks are being permanently removed for target hardware.
43: 
44: # ============================================================================
45: # C++ Standard Configuration
46: # ============================================================================
47: 
48: set(CMAKE_CXX_STANDARD 17)
49: set(CMAKE_CXX_STANDARD_REQUIRED ON)
50: set(CMAKE_CXX_EXTENSIONS OFF)
51: 
52: # Export compile commands for IDE integration
53: set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
54: 
55: # ============================================================================
56: # Compiler Flags
57: # ============================================================================
58: 
59: # Base optimization flags
60: set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
61: set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
62: set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DNDEBUG")
63: 
64: # ARM-specific flags for ARMv8.2-A (Cortex-A72 on RPi 5, or generic ARMv8-A)
65: if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
66:     message(STATUS "Building for ARM64 (ARMv8.2-A)")
67:     
68:     if(AURORE_ENABLE_NEON)
69:         # Use ARMv8.2-A with NEON and crypto extensions
70:         # Generic ARMv8-A flags for broader compatibility
71:         add_compile_options(-march=armv8-a+fp+simd)
72:         add_compile_definitions(AURORE_HAS_NEON AURORE_TARGET_AARCH64)
73:         message(STATUS "ARM NEON SIMD enabled")
74:     endif()
75: elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|x64")
76:     message(STATUS "Building for x86_64 (development/host)")
77:     add_compile_definitions(AURORE_HOST_X86_64)
78:     
79:     # Enable SSE/AVX on x86_64 for development testing
80:     if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
81:         add_compile_options(-msse4.2 -mavx2)
82:     endif()
83: endif()
84: 
85: # Real-time flags
86: if(AURORE_REALTIME)
87:     add_compile_definitions(AURORE_REALTIME)
88:     
89:     # Link with pthreads real-time extensions
90:     set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lpthread -lrt")
91:     
92:     message(STATUS "Real-time optimizations enabled")
93: endif()
94: 
95: # Warning flags
96: add_compile_options(
97:     -Wall
98:     -Wextra
99:     -Wpedantic
100:     -Wconversion

(Showing lines 1-100 of 1177. Use offset=101 to continue.)
</content>
```

---

## Assistant (Build · hy3-preview-free · 67.4s)

---

