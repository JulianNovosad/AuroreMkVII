<!-- MAINTENANCE EVAL PROMPT - SESSION CONTINUATION ONLY -->
<!-- DO NOT use this prompt for fresh audits. Use only for continuation sessions. -->

You are still the main coordinator agent for the full codebase maintenance and spec conformance round on AuroreMkVII.

**NOTE:** This prompt is designed for continuation sessions. If the referenced errors below do not match your current state, you MUST first run discovery commands to determine actual current state.

**Discovery Phase (REQUIRED FIRST STEP):**
Before dispatching any fix agents, you MUST:
1. Run `make -j$(nproc)` to identify current build errors
2. Run `git status` to see staged/modified files
3. Run `git diff HEAD --stat` to identify heavily changed files
4. Use actual current state to dispatch fix agents

The errors mentioned below (camera_wrapper.cpp:365, etc.) are EXAMPLES from a previous session. Your actual errors may differ.

---

The previous Phase 2 implementation wave (18 of 20 agents) completed many tasks and staged a large number of changes, but the build is **not clean** and therefore **no commits were created yet**.

Current status as of 2026-03-09:

- cmake succeeded
- make fails because of -Werror=unused-variable in src/drivers/camera_wrapper.cpp:365
  → const auto& plane = fb->planes()[0]; is declared but never used

- Many other compilation units and most tests appear to have built/linked successfully
- git status shows a very large number of modified headers and sources + several new files from the previous agents

Hard rules that still apply:
- You (main coordinator) NEVER edit files, write code, or run git commit yourself
- All actual code changes, test additions, fixes, refactors MUST come from dispatched sub-agents
- -Werror stays enabled — no disabling, no pragmas, no [[maybe_unused]], no casting-to-void tricks, no commenting out, no #ifdef hacks
- Only real, semantic fixes are allowed. For unused variable warnings, follow this decision tree:
  1. If variable was used in removed/refactored code → delete the declaration
  2. If variable should be used per spec but isn't → add the missing logic per spec requirement
  3. If variable is legacy/deprecated → remove via proper deprecation workflow
  4. Never: add (void)var, [[maybe_unused]], pragmas, or fake usage
- Maximize parallelism: dispatch as many independent fix agents as possible (target 8–15) for this narrow clean-build phase
- Do not attempt to commit until the **entire** project builds cleanly with make -j$(nproc) in Release mode and all tests pass (or the failing ones are demonstrably fixed)
- Respect all previous CLAUDE.md / workflow rules

---

## Autonomous Iteration & Termination Rules

**You operate fully autonomously.** You MUST:

- Continue execution in a **closed loop** without user intervention
- Automatically re-dispatch fix agents, test agents, or audit agents as needed
- Re-run build and test cycles after **every** fix batch
- **NOT** stop on partial success
- Only terminate when **ALL** of the following are simultaneously true:
  - Full build succeeds in Release mode
  - All existing tests pass
  - All newly generated tests pass
  - No compiler warnings (-Werror enforced)
  - No sanitizer findings (ASan/UBSan clean)
  - All spec-mandated tests pass (tests tagged with spec requirement IDs)
  - All public API contracts documented in spec.md have corresponding implementation

**Explicitly forbidden:**

- Asking the user "what next"
- Pausing after intermediate success
- Declaring success without verification output (show the test results, show the build output)

**Iteration is mandatory.** If any verification step fails, you MUST:
1. Diagnose the root cause
2. Dispatch targeted fix agents
3. Re-verify
4. Repeat until clean

**Session budget awareness:** If fewer than 500 tokens remain or session timeout is imminent:
1. Commit all clean changes immediately
2. Write checkpoint file to `agent_sessions/[session_id]/checkpoint.md` with current state
3. Document remaining work items explicitly
4. Terminate cleanly with status report

---

## Frontend ↔ Robotics Coupling Tests

**You MUST automatically analyze and test all JS/frontend/control-layer interfaces.**

For every data contract crossing into the robotics backend, you MUST generate tests that validate:

- **Coordinate system consistency** (screen pixels ↔ world coordinates ↔ actuator angles)
- **Units correctness** (meters vs millimeters, radians vs degrees, pixels vs world space)
- **Timestamp alignment and latency assumptions** (frame capture → processing → actuation delay)
- **Frame-of-reference correctness** (origin points, axis directions, handedness)
- **Safety invariance**: frontend-originated commands MUST NOT produce undefined or unsafe backend states

**Test requirements:**

- Tests MUST be integrated into the **existing test framework** (do NOT invent a new harness)
- Tests MUST run as part of the normal test suite (`ctest`, `pytest`, etc.)
- Tests MUST **fail hard** on mismatch or ambiguity
- Tests MUST include both unit-level contract tests and integration-level end-to-end tests

**Dispatch test-generation agents when:**
- A function signature contains types with unit ambiguity (e.g., `float angle` without rad/deg suffix)
- A data contract crosses process boundaries (socket, shared memory, file) without explicit serialization tests
- A coordinate transform function lacks test coverage for all 4 quadrants

---

## Geometric, Kinematic, and Targeting Validation Tests

**You MUST automatically generate tests for all geometric and targeting logic.**

Required test coverage includes:

- **Ballistic solvers** — trajectory computation, p_hit tables, range/angle interpolation
- **Target prediction** — lead angle calculation, velocity estimation, time-to-intercept
- **Coordinate transforms** — camera ↔ world, gimbal ↔ turret, pixel ↔ angular
- **Sight-over-bore compensation** — parallax correction at various ranges
- **Safety cones, interlocks, and exclusion zones** — angular bounds, no-fire sectors
- **Servo/actuator command bounds** — min/max angles, velocity limits, acceleration limits

**Test types required:**

- **Deterministic numeric cases** — known input → known output with tolerance bounds
- **Edge cases**:
  - Singularities (gimbal lock, zero elevation, 90° azimuth)
  - Zero velocity targets
  - Extreme angles (limits of travel)
  - Max/min range boundaries
  - Division-by-zero guards
- **Regression tests** — for every bug fixed during the run, add a test that would have caught it

**Visualization-logic consistency:** Tests MUST verify that the **data structures** passed to the renderer match the data structures used for safety decisions. Example: if `SafetyCone::angle_min` is used for enforcement, the same value must be passed to `Renderer::drawCone(angle_min, ...)`. Test the data flow, not the rendered pixels.

**Test-to-spec traceability:** Every generated test MUST include a comment with format:
```cpp
// Spec: AM7-L2-ACTUATION-03 (servo angle bounds)
// Spec: AM7-L3-BALLISTICS-07 (p_hit table interpolation)
```
Tests without spec references will be rejected during verification.

**Dispatch geometry-test agents** when:
- Any ballistic/kinematic function is modified
- New coordinate transforms are introduced
- Safety zone logic changes
- Visualization regions are added or modified

---

## Fail-Fast and Crash-Loud Error Handling Policy

**The system MUST crash immediately on:**

- Invalid geometry (e.g., impossible angles, NaN coordinates)
- Contract violations (e.g., missing required fields, type mismatches)
- Out-of-range actuator commands (e.g., angles beyond physical limits)
- Corrupted sensor input (e.g., malformed frames, impossible timestamps)
- Undefined or NaN state (e.g., division by zero, sqrt of negative)

**Every crash MUST be:**

- **Safe** — no uncontrolled actuation, system fails to a known safe state
- **Explicit** — no silent fallback, no "best effort" continuation
- **Diagnostic** — emits actionable error information

**Every crash path MUST emit a clear error message explaining:**

- **What failed** — specific variable, function, or invariant
- **Why it failed** — the violated condition or invalid value
- **Root cause category** — environmental, configuration-related, or code-level
- **Concrete remediation steps** — what a developer or user can do to fix it

**Explicitly forbidden:**

- Graceful degradation on invalid input (crash, don't limp along)
- Silent clamping of out-of-range values
- Logging without aborting
- Continuing execution in an invalid state
- Swallowing exceptions in real-time threads

**Dispatch error-handling agents** when:
- Any function lacks input validation guards
- Error messages are vague or unactionable
- Silent fallbacks are discovered
- Any "should never happen" code path lacks a crash guard

**Scope clarification:** The Fail-Fast policy applies to **runtime code behavior** during test execution, not to the agent's workflow. The agent MUST continue iterating on fix batches even when tests fail. A test failure is expected during iteration; the system failing to crash when it should is a bug to fix.

---

## Edge-Case, Stress, and Abuse Testing

**You MUST actively search for and test edge cases implied by the spec.**

For each function, interface, or state machine, generate tests for:

- **Boundary values** — min/max valid inputs, just-inside and just-outside bounds
- **Invalid inputs** — null pointers, empty strings, negative counts, zero divisors
- **Rapid state changes** — fast toggling, re-entrant calls, concurrent mutations
- **Timing jitter** — early/late arrivals, skipped frames, burst arrivals
- **Partial initialization** — calling functions before full system init, missing dependencies

**Test generation rules:**

- Include **at least one test per discovered edge case**
- **Tie each edge-case test to a concrete spec clause or invariant** (e.g., "AM7-L2-ACTUATION-03: servo angle bounds")
- Name tests descriptively: `test_<function>_<edge_condition>_<expected_behavior>`
- Document the edge case in the test comment with a reference to the spec requirement

**Test naming conventions (all test types):**
- Coupling tests: `test_coupling_<interface>_<property>_<expected>`
- Geometry tests: `test_geometry_<component>_<scenario>_<expected>`
- Edge-case tests: `test_edge_<function>_<condition>_<expected>`

**Stress testing:**

- Run high-frequency iteration tests (1000+ cycles) on real-time paths
- Verify no memory leaks under sustained load
- Verify ring buffers don't overflow under backpressure
- Verify deadline monitors catch period overruns

**Dispatch edge-case agents when:**
- A new function or class is added without boundary tests
- A spec requirement lacks explicit edge-case coverage
- A bug fix reveals a previously untested edge condition
- Any function lacks input validation guards for "should never happen" cases

---

## Test Integration Requirement

**Newly generated tests are first-class citizens:**

- They MUST run automatically in all verification phases
- A build is **NOT** considered clean unless:
  - Legacy tests pass
  - Newly generated coupling tests pass
  - Newly generated geometry tests pass
  - Newly generated edge-case tests pass
- Do NOT skip or disable tests to achieve a passing build

**Test framework immutability:** Do NOT modify the test framework itself (CTestLists.txt structure, pytest fixtures, test main entry points). Only add new test files or extend existing test functions.

**Test registration requirement:** Every new test file MUST be added to the appropriate CTestLists.txt or pytest discovery configuration. Verification step: run `ctest -N` or `pytest --collect-only` to confirm test is discovered before declaring success.

---

Immediate next phase — Clean Build Enforcement Batch

1. Dispatch a focused batch of read + fix sub-agents (aim for 8–12 parallel agents):
   - At least one agent MUST be assigned exclusively to src/drivers/camera_wrapper.cpp line ~365 → diagnose why 'plane' is unused and propose a real fix (use it correctly or remove it if spec-compliant)
   - Other agents investigate the files that were heavily modified in the previous wave and are most likely to have introduced new warnings/errors:
     - src/actuation/ballistic_solver.cpp and include/aurore/ballistic_solver.hpp (known previous signature mismatch)
     - src/safety/interlock_controller.cpp and include/aurore/interlock_controller.hpp (known constructor issue in tests)
     - src/drivers/camera_wrapper.cpp (current blocker + any other warnings)
     - include/aurore/security.hpp + src/common/security.cpp (new security code)
     - src/vision/apriltag_detector.cpp + include/aurore/apriltag_detector.hpp (new file)
     - Any file that shows large ++++++ / ------- in git diff --stat
   - Every fix agent must:
     - Read spec.md relevant sections
     - Read the code context around the warning/error
     - Propose ONE atomic, spec-aligned change that eliminates the warning/error without masking
     - Run the relevant build command inside its scope after the change (cmake + make on the affected target)
     - Return: file(s) changed, exact diff, build command output (success or new error)

2. After all fix agents report:
   - Synthesize: are there still any compile errors or -Werror violations?
   - If yes → dispatch more targeted fix agents (loop until clean)
   - If clean → run full verification:
     - make -j$(nproc) again (full build)
     - ctest -V (or equivalent full test suite run)
     - Report exact output of both

3. Only when BOTH full build succeeds AND all tests pass (or fixed):
   - Partition the staged changes into logical conventional commit groups using these grouping rules (in priority order):
     1. Security fixes → `fix(security): ...`
     2. Bug fixes → `fix(subsystem): ...`
     3. New features → `feat(subsystem): ...`
     4. Test additions → `test(subsystem): ...`
     5. Refactors without behavior change → `refactor(subsystem): ...`
     6. Build/config changes → `chore(build): ...`
     
     Each commit should contain all files for one logical concern. Do not split a single feature across multiple commits.
   - Dispatch commit-preparation agents that each return a proposed commit message + list of files for that group
   - Then (still as coordinator) perform the commits yourself using the grouped messages

4. Finally write an updated summary report (overwrite or append to docs/reports/2026-03-08-audit-and-fix-summary.md) including:
   - How the build was made clean
   - Which warnings/errors were fixed and by which agents
   - Full build & ctest output (success case)
   - Updated list of remaining known issues
   - Commit SHAs created

---

## Commit Failure Handling

**If a commit fails:**
1. Run `git status` to determine what was staged vs committed
2. If partial commit occurred: `git reset --soft HEAD~1` to uncommit but keep staged
3. Re-attempt with smaller file groups
4. After 3 failures: write checkpoint and terminate with error report

---

Begin now.

First action: dispatch the Clean Build Enforcement Batch of sub-agents targeting the camera_wrapper unused variable + ballistic_solver signature + interlock constructor + any other new warnings introduced by the previous wave.

Go.
