
---

<!-- MAINTENANCE WORKFLOW PROMPT - GENERIC/REUSABLE -->
<!-- Use this prompt for fresh audits starting from Phase 1 -->

# Full Codebase Maintenance & Spec Conformance Round

You are now running a **full codebase audit + conformance + hardening round**.

Follow this exact workflow:

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

## 1. Read the Specification

* Read `spec.md` (or the entire `specs/` directory if multiple files exist).

---

## 2. Phase 1 – Recon / Audit Batch (Read-Only)

**Goal:** Identify gaps and issues without making edits.

* Create a **batch of parallel sub-agents** (aim for 3).
* Assign each sub-agent **one focused slice** of the spec, e.g.:

  * One major feature
  * One subsystem/module
  * One non-functional requirement (performance, security, portability…)
  * One group of related edge cases
  * One interface/contract
  * One cross-cutting concern (error handling, logging, observability…)

**Audit sub-agent responsibilities:**

1. Read relevant parts of the **codebase** needed for its slice.
2. Classify current implementation status:

   * fully implemented
   * partially implemented
   * missing
   * broken
   * deprecated
   * unclear / ambiguous
3. List **concrete gaps/issues**, including (but not limited to):

   * missing functionality
   * spec mismatches
   * absent error handling
   * missing edge-case coverage
   * performance concerns
   * security vulnerabilities
   * portability issues (especially laptop vs Raspberry Pi 5)
   * maintainability / readability problems
4. Estimate **which files** would likely need to change.

**Return format (YAML):**
```yaml
slice: "Feature/Slice Name"
status: "fully | partial | missing | broken | deprecated | unclear"
gaps:
  - "Gap 1 description"
  - "Gap 2 description"
files_to_change:
  - "path/to/file1.cpp"
  - "path/to/file2.hpp"
```

---

## 3. Coordinator Synthesis (Main Agent Only)

* Collect and synthesize full audit report.
* Deduplicate overlapping findings.
* Prioritize gaps (severity + dependency order).
* Partition remaining work into **three independent implementation tasks** (target three parallel items).

**Output:** Write `agent_sessions/[current_session]/audit_report.md` with sections:
- Executive Summary (5 lines max)
- Gap Inventory (table: ID, Severity, Description, Files, Estimated Effort)
- Dependency Graph (text description of ordering constraints)
- Task Partitioning (3 tasks with explicit boundaries)

---

## 4. Phase 2 – Implementation & Test-Hardening Batch

* Dispatch a second wave of **three parallel implementation sub-agents**.
* Each agent gets **exactly one atomic, independent task**.

**Allowed actions (within assigned scope only):**

* Create/edit files
* Add/fix tests
* Fix logic
* Add guards/validation
* Narrow refactorings

**Mandatory rules for implementation agents:**

* Run relevant build/test/lint commands **after every non-trivial change**.
* **Never** merge or commit — only prepare clean changes.

**Return format:**

```text
Task: ______________________________
Files changed: …
Diff summary: …
Tests added/run: …
Verification commands & output:
• command 1 → result
• command 2 → result
```

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

**Example error message format:**

```
FATAL: BallisticSolver::compute_p_hit received NaN range
  Location: src/actuation/ballistic_solver.cpp:127
  Condition: range_m > 0 && std::isfinite(range_m)
  Actual value: range_m = nan
  Cause: Upstream rangefinder returned -1 (error code) without validation
  Fix: Validate rangefinder output before passing to ballistic solver
       See: src/vision/rangefinder.cpp line 89 for validation guard
```

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

## 5. Final Coordinator Phase (Main Agent Only)

* Collect results from all implementation agents.
* Check for cross-task conflicts → dispatch conflict-resolution agents if needed.
* **Integrate all changes**.
* Run **full project build + full test suite** (pytest, ctest, make, cargo, etc.).
* If anything fails → loop: create fix agents → re-verify.

**Only when everything passes cleanly:**

* Write conventional commit messages using these grouping rules (in priority order):
  1. Security fixes → `fix(security): ...`
  2. Bug fixes → `fix(subsystem): ...`
  3. New features → `feat(subsystem): ...`
  4. Test additions → `test(subsystem): ...`
  5. Refactors without behavior change → `refactor(subsystem): ...`
  6. Build/config changes → `chore(build): ...`
  
  Each commit should contain all files for one logical concern. Do not split a single feature across multiple commits.
* Perform the commits.
* Write a **final summary report** (Markdown) containing:

  * What was audited (major areas covered)
  * Major gaps that were closed
  * Tests added / coverage improvements
  * Final build & test suite status
  * Any remaining known issues or recommended follow-up work

---

## Commit Failure Handling

**If a commit fails:**
1. Run `git status` to determine what was staged vs committed
2. If partial commit occurred: `git reset --soft HEAD~1` to uncommit but keep staged
3. Re-attempt with smaller file groups
4. After 3 failures: write checkpoint and terminate with error report

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

## Hard Rules – You MUST Follow

* The **main coordinator agent (you)** **does NOT** perform file edits, writes, or commits.
  → All actual code changes **must** come from sub-agents.
* **Maximize parallelism:** Phase 1 recon batch targets 3 parallel audit agents. Phase 2 implementation batch targets 3 parallel implementation agents. Total target: 6 agents across both phases for large codebases/specs.
* Do **not** let planning/discussion consume the whole session — move quickly into execution after initial setup.
* Respect **all** existing rules in `CLAUDE.md` (guards, build-before-commit, conventional commits, verification gate, etc.).

**Test integration requirement:**

- Newly generated tests (coupling, geometry, edge-case, abuse) are **first-class citizens**
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

**Begin now:**

1. Read `spec.md` (or `specs/` directory).
2. Immediately start dispatching the **Phase 1 recon batch**.

---

