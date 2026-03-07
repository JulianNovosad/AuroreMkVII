# Verification Plan: Native Tests + Full Spec Compliance Audit

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

---

## Security Fixes Applied (2026-03-07)

This verification plan has been updated to reflect documentation drift fixes:

| Fix | Description | Impact on Verification |
|-----|-------------|------------------------|
| SEC-001 | Test command updated to `ctest --output-on-failure` | Agent A1 uses correct test command |
| SEC-003 | Added docs for TelemetryWriter, StateMachine, BallisticSolver, FusionHat | Auditors have reference documentation |
| SEC-004 | Tracker changed from CSRT to KCF | Agent B3 validates KCF tracker |
| SEC-007 | docs/telemetry.md created | Agent B6 has telemetry reference |
| SEC-008 | docs/state_machine.md created | Agent B1 has state machine reference |

**Note:** Agent B3 (Tracking) should validate KCF tracker implementation per `include/aurore/tracker.hpp`. KCF provides 1-2ms execution time vs 10-20ms for CSRT, meeting WCET ≤5ms budget.

---

**Goal:** Verify the engagement pipeline implementation by running all native tests and auditing every AM7-L* requirement in `spec.md` against the source code, then closing the `verification_complete` gate in session_20260306.

**Architecture:** Two phases. Phase 1 dispatches 13 agents in parallel — a build/test cluster and a full spec audit cluster. Phase 2 runs truthchecker + quality_gate sequentially to finalize verdict and update the blackboard. Results are merged into `docs/reports/2026-03-06-verification.md`.

**Tech Stack:** C++17, CMake 3.16+, ctest, OpenCV 4.6, blackboard at `agent_sessions/session_20260306_001/blackboard/`

---

## Task 1: Verify Build Environment

**Files:**
- Read: `CMakeLists.txt`
- Read: `scripts/build-native.sh`

**Step 1: Check if native build directory exists and is current**

```bash
ls build-native/ 2>/dev/null && echo "EXISTS" || echo "MISSING"
```

Expected: `EXISTS` with `CMakeCache.txt` present. If MISSING, proceed to Step 2. If EXISTS, skip to Task 2.

**Step 2: Build native (only if missing or stale)**

```bash
./scripts/build-native.sh Release 2>&1 | tail -30
```

Expected: `[100%] Built target aurore` with no errors. If build fails, STOP and report the error — do not proceed to parallel dispatch.

**Step 3: Confirm test binaries exist**

```bash
ls build-native/*_test build-native/*_tests 2>/dev/null
```

Expected: at least `ring_buffer_test`, `timing_test`, `safety_monitor_test` listed.

---

## Task 2: Phase 1 — Parallel Agent Dispatch (13 agents)

Dispatch ALL of the following agents simultaneously. Do not wait for any one before launching the others.

### Agent Group A: Build & Test Cluster

**Agent A1 — `test-coverage-architect`**
> Role: Run the full native ctest suite and audit test coverage.

Prompt:
```
Run all native tests in the Aurore MkVII project.

Working directory: /home/laptop/AuroreMkVII
Build directory: build-native/

Steps:
1. Run: cd build-native && ctest --output-on-failure -j$(nproc)
2. Capture full output.
3. Produce a structured table:
   | Test Name | Status | Duration | Failure Reason (if any) |
4. Count total PASS / FAIL.
5. If any test FAILS, capture the full failure output verbatim.
6. Report: total tests, pass count, fail count, list of failures with output.
```

**Agent A2 — `build-optimizer`**
> Role: Analyze the CMake build for correctness, warnings, and unused stubs.

Prompt:
```
Analyze the Aurore MkVII native build for issues.

Working directory: /home/laptop/AuroreMkVII

Steps:
1. Read CMakeLists.txt fully.
2. Run: cd build-native && cmake --build . -- -n 2>&1 | head -50  (dry-run to see targets)
3. Run: cd build-native && cmake --build . 2>&1 | grep -E "warning:|error:" | head -60
4. Check for commented-out TODO stubs in CMakeLists.txt.
5. Report:
   - Any compiler warnings (grouped by file)
   - Any TODO stubs still disabled in CMake
   - Whether all 6 new subsystem sources (state_machine, orb_detector, csrt_tracker, ballistic_solver, fusion_hat_i2c, hud_socket) are compiled
```

**Agent A3 — `runtime-debugger-analyzer`**
> Role: Pre-analyze test sources for logic errors before running, to anticipate failures.

Prompt:
```
Analyze the test sources for Aurore MkVII to identify likely test failures before execution.

Working directory: /home/laptop/AuroreMkVII

Read all files in:
- agent_sessions/session_20260305_001/blackboard/artifacts/generated_tests_001/
- tests/ (if directory exists)

For each test file:
1. Identify the functions under test.
2. Check whether the corresponding implementation headers and sources exist.
3. Flag any test that references a symbol not present in the headers.
4. Report: list of tests likely to PASS, likely to FAIL, and reason for predicted failures.
```

### Agent Group B: Spec Compliance Cluster

All agents in Group B must first read `spec.md` in full, then audit their assigned subsystem files.

**Agent B1 — `code-compliance-researcher` (State Machine)**
> Subsystem: MODE / State Machine

Prompt:
```
Audit Aurore MkVII state machine implementation against spec.md requirements.

Working directory: /home/laptop/AuroreMkVII

Step 1: Read spec.md in full.
Step 2: Extract every requirement tagged AM7-L*-MODE-* (also check AM7-L*-SM-* if present).
Step 3: Read:
  - include/aurore/state_machine.hpp
  - src/state_machine/state_machine.cpp

Step 4: For each requirement, determine:
  - PASS: clearly implemented
  - PARTIAL: stub or incomplete implementation
  - GAP: not implemented

Step 5: Output a table:
  | Req ID | Description | Status | Evidence (file:line or "none") |

Step 6: List all GAP and PARTIAL items separately for easy remediation.
```

**Agent B2 — `code-compliance-researcher` (Vision/Detection)**
> Subsystem: VIS / Vision & Detection

Prompt:
```
Audit Aurore MkVII vision and detection implementation against spec.md requirements.

Working directory: /home/laptop/AuroreMkVII

Step 1: Read spec.md in full.
Step 2: Extract every requirement tagged AM7-L*-VIS-* or AM7-L*-DET-*.
Step 3: Read:
  - include/aurore/detector.hpp
  - src/vision/orb_detector.cpp

Step 4: For each requirement, determine PASS / PARTIAL / GAP with evidence.
Step 5: Output compliance table + GAP/PARTIAL summary.
```

**Agent B3 — `code-compliance-researcher` (Tracking)**
> Subsystem: TRACK / Tracking

Prompt:
```
Audit Aurore MkVII tracking implementation against spec.md requirements.

Working directory: /home/laptop/AuroreMkVII

Step 1: Read spec.md in full.
Step 2: Extract every requirement tagged AM7-L*-TRACK-* or AM7-L*-TRK-*.
Step 3: Read:
  - include/aurore/tracker.hpp
  - src/tracking/csrt_tracker.cpp

Step 4: For each requirement, determine PASS / PARTIAL / GAP with evidence.
Step 5: Output compliance table + GAP/PARTIAL summary.
```

**Agent B4 — `code-compliance-researcher` (Actuation/Gimbal)**
> Subsystem: ACT / Actuation

Prompt:
```
Audit Aurore MkVII actuation and gimbal driver implementation against spec.md requirements.

Working directory: /home/laptop/AuroreMkVII

Step 1: Read spec.md in full.
Step 2: Extract every requirement tagged AM7-L*-ACT-* or AM7-L*-GIM-*.
Step 3: Read:
  - include/aurore/fusion_hat.hpp
  - src/drivers/fusion_hat_i2c.cpp

Step 4: For each requirement, determine PASS / PARTIAL / GAP with evidence.
Step 5: Output compliance table + GAP/PARTIAL summary.
```

**Agent B5 — `code-compliance-researcher` (Ballistics)**
> Subsystem: BALL / Ballistics

Prompt:
```
Audit Aurore MkVII ballistics solver implementation against spec.md requirements.

Working directory: /home/laptop/AuroreMkVII

Step 1: Read spec.md in full.
Step 2: Extract every requirement tagged AM7-L*-BALL-* or AM7-L*-FC-*.
Step 3: Read:
  - include/aurore/ballistic_solver.hpp
  - src/actuation/ballistic_solver.cpp

Step 4: For each requirement, determine PASS / PARTIAL / GAP with evidence.
Step 5: Output compliance table + GAP/PARTIAL summary.
```

**Agent B6 — `code-compliance-researcher` (HUD & Telemetry)**
> Subsystem: HUD / TEL

Prompt:
```
Audit Aurore MkVII HUD socket and telemetry implementation against spec.md requirements.

Working directory: /home/laptop/AuroreMkVII

Step 1: Read spec.md in full.
Step 2: Extract every requirement tagged AM7-L*-HUD-* or AM7-L*-TEL-*.
Step 3: Read:
  - include/aurore/hud_socket.hpp
  - src/common/hud_socket.cpp
  - include/aurore/telemetry_writer.hpp
  - include/aurore/telemetry_types.hpp
  - src/common/telemetry_writer.cpp

Step 4: For each requirement, determine PASS / PARTIAL / GAP with evidence.
Step 5: Output compliance table + GAP/PARTIAL summary.
```

**Agent B7 — `cpp-integration-validator` (Core/Safety/RT)**
> Subsystem: SAFE / RT / Core primitives

Prompt:
```
Audit Aurore MkVII core real-time primitives and safety monitor against spec.md requirements.

Working directory: /home/laptop/AuroreMkVII

Step 1: Read spec.md in full.
Step 2: Extract every requirement tagged AM7-L*-SAFE-*, AM7-L*-RT-*, AM7-L*-CORE-*, or AM7-L*-TIMING-*.
Step 3: Read:
  - include/aurore/safety_monitor.hpp
  - include/aurore/ring_buffer.hpp
  - include/aurore/timing.hpp
  - include/aurore/camera_wrapper.hpp

Step 4: For each requirement, verify:
  - Memory ordering correctness (acquire/release on all atomics)
  - No heap allocation in RT paths
  - WCET budget compliance (≤5ms per frame documented)
  - 1kHz safety monitor interface present

Step 5: Determine PASS / PARTIAL / GAP with evidence.
Step 6: Output compliance table + GAP/PARTIAL summary.
```

### Agent Group C: Cross-Cutting Auditors

**Agent C1 — `static-security-auditor`**

Prompt:
```
Perform a security audit of the Aurore MkVII codebase focused on the newly implemented subsystems.

Working directory: /home/laptop/AuroreMkVII

Read all files in src/ and include/aurore/.

Check for:
1. Buffer overflows (fixed-size arrays with unchecked indices)
2. Integer overflow in ballistic calculations
3. Race conditions (shared state accessed without atomics or locks)
4. Unsafe I2C/UART operations (no error checking on ioctl/write)
5. UNIX socket path traversal or injection (hud_socket)
6. Unchecked return values from system calls

Report: severity (CRITICAL/HIGH/MEDIUM/LOW), file:line, description, recommendation.
```

**Agent C2 — `documentation-drift-detector`**

Prompt:
```
Check for documentation drift in Aurore MkVII — verify CLAUDE.md and spec.md accurately describe the current implementation.

Working directory: /home/laptop/AuroreMkVII

Step 1: Read CLAUDE.md section "What Is Implemented vs. TODO".
Step 2: Check each item marked as TODO against the actual source files in src/.
Step 3: Read spec.md threading model table.
Step 4: Read src/main.cpp to verify thread model matches spec.
Step 5: Report any discrepancies between documentation claims and actual source state.
```

**Agent C3 — `performance-regression-profiler`**

Prompt:
```
Analyze Aurore MkVII source for potential WCET (worst-case execution time) violations on the hot path.

Working directory: /home/laptop/AuroreMkVII

Read all files in src/ and include/aurore/.

WCET budget: ≤5ms per 120Hz frame on RPi 5 (Cortex-A76).
Safety monitor: 1kHz = ≤1ms per cycle.

Check for:
1. Any heap allocation (new, malloc, std::vector push_back, std::string construction) in RT thread paths
2. Any memcpy on the critical path (zero-copy invariant)
3. Any blocking system calls (non-async I/O, sleep, mutex lock without timeout)
4. OpenCV operations on the hot path that are known to be slow (e.g., full-frame operations each cycle)
5. Lock contention patterns

Report: file:line, violation type, severity (BLOCKS_WCET / WARNING / INFO).
```

---

## Task 3: Collect All Agent Results

After all 13 agents complete, collect their outputs. Create a consolidated intermediate file:

```bash
mkdir -p docs/reports
```

Verify you have results from:
- [ ] A1 (test-coverage-architect) — ctest results table
- [ ] A2 (build-optimizer) — build warnings + stub status
- [ ] A3 (runtime-debugger-analyzer) — predicted failure analysis
- [ ] B1–B7 (code-compliance-researcher × 7) — per-subsystem compliance tables
- [ ] C1 (static-security-auditor) — security findings
- [ ] C2 (documentation-drift-detector) — drift findings
- [ ] C3 (performance-regression-profiler) — WCET findings

If any agent failed or produced no output, note it in the report as `AGENT_TIMEOUT`.

---

## Task 4: Merge Into Verification Report

**Files:**
- Create: `docs/reports/2026-03-06-verification.md`

Write the merged report with this structure:

```markdown
# Aurore MkVII Verification Report
**Date:** 2026-03-06
**Session:** session_20260306_001
**Build:** native x86_64

---

## 1. Test Results

| Test Binary | Status | Duration |
|-------------|--------|----------|
| ...         | PASS   | Xms      |

**Summary:** X/Y tests passing.

### Failing Tests (if any)
[verbatim ctest output for failures]

---

## 2. Spec Compliance

### 2.1 State Machine (AM7-L*-MODE-*)
| Req ID | Description | Status | Evidence |
...

### 2.2 Vision/Detection (AM7-L*-VIS-*)
...

### 2.3 Tracking (AM7-L*-TRACK-*)
...

### 2.4 Actuation (AM7-L*-ACT-*)
...

### 2.5 Ballistics (AM7-L*-BALL-*)
...

### 2.6 HUD & Telemetry (AM7-L*-HUD-*, AM7-L*-TEL-*)
...

### 2.7 Core / Safety / RT (AM7-L*-SAFE-*, AM7-L*-RT-*)
...

---

## 3. Security Findings
[C1 output, grouped by severity]

---

## 4. Documentation Drift
[C2 output]

---

## 5. WCET Risk Analysis
[C3 output]

---

## 6. Build Analysis
[A2 output: warnings, disabled stubs]

---

## 7. Summary

| Category | PASS | PARTIAL | GAP |
|----------|------|---------|-----|
| Tests    | X    | —       | Y   |
| State Machine | | | |
| Vision   | | | |
| Tracking | | | |
| Actuation| | | |
| Ballistics| | | |
| HUD/Tel  | | | |
| Core/Safety| | | |
| **Total** | | | |

### Remediation Priority (GAPs and PARTIALs)
1. [highest priority gap]
2. ...
```

---

## Task 5: Run truthchecker

**Agent: `truthchecker`**

Prompt:
```
Verify the Aurore MkVII verification report at docs/reports/2026-03-06-verification.md.

Working directory: /home/laptop/AuroreMkVII

Checks:
1. File exists and is readable.
2. All 7 subsystem compliance sections are present (non-empty tables).
3. Test results section contains at least one test result.
4. Summary table is complete.
5. Cross-check: for any requirement marked PASS, verify the cited evidence file:line actually exists by reading the file.
6. For any requirement marked GAP, verify there is truly no implementation by searching the codebase.

Verdict: PASS (all checks pass) or FAIL (any critical check fails, with reasons).
```

---

## Task 6: Run quality_gate + Update Blackboard

**Agent: `quality_gate`**

Prompt:
```
Update the quality gate for session_20260306_001 based on the truthchecker verdict.

Working directory: /home/laptop/AuroreMkVII
Blackboard: agent_sessions/session_20260306_001/blackboard/

Step 1: Read quality_gates.json.
Step 2: Read docs/reports/2026-03-06-verification.md summary section.
Step 3: If truthchecker = PASS and 0 GAPs and 0 test failures:
  - Set verification_complete = "passed"
  - Set session status = "completed"
Else:
  - Set verification_complete = "failed"
  - Set failure_reason to list of GAPs and failing tests
Step 4: Write updated quality_gates.json.
Step 5: Write updated status.json (set status to "completed" or "failed").
Step 6: Append event to event_log.json:
  {
    "event": "verification_complete",
    "agent": "quality_gate",
    "verdict": "passed|failed",
    "details": "..."
  }
```

---

## Task 7: Present Results to User

Summarize the final report for the user:

1. Test pass/fail count
2. Total spec requirements audited, with PASS/PARTIAL/GAP breakdown
3. Top 3 highest-priority gaps or failures (if any)
4. Gate status: `verification_complete = passed|failed`
5. Link to full report: `docs/reports/2026-03-06-verification.md`
