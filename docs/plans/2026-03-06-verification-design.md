# Verification Design: Native Build Tests + Full Spec Compliance Audit

**Date:** 2026-03-06
**Status:** Approved
**Security Fixes Applied:** 2026-03-07

---

## Security Fixes Applied (2026-03-07)

This verification design has been updated to reflect documentation drift fixes:

| Fix | Description | Impact on Verification |
|-----|-------------|------------------------|
| SEC-001 | Test command updated to `ctest --output-on-failure` | Test Runner uses correct command |
| SEC-004 | Tracker changed from CSRT to KCF | Tracking Auditor validates KCF, not CSRT |
| SEC-007 | docs/telemetry.md created | HUD/Telemetry Auditor has reference doc |
| SEC-008 | docs/state_machine.md created | State Machine Auditor has reference doc |

**Note:** Verification agents should validate KCF tracker implementation (1-2ms WCET) rather than CSRT (10-20ms). See `include/aurore/tracker.hpp` for actual implementation.

---

## Goal

Verify the engagement pipeline implementation (session_20260306) by:
1. Running all native unit/regression tests and confirming they pass
2. Auditing every AM7-L* requirement in `spec.md` against the implemented source code
3. Producing a consolidated verification report and closing the `verification_complete` gate

## Architecture

8 parallel subagents, all launched concurrently:

| Agent | Responsibility |
|-------|---------------|
| Test Runner | Build native target + run full `ctest` suite, produce pass/fail table |
| State Machine Auditor | Audit AM7-L*-MODE-* requirements vs `state_machine.*` |
| Vision Auditor | Audit AM7-L*-VIS-* vs `orb_detector.*`, `detector.hpp` |
| Tracking Auditor | Audit AM7-L*-TRACK-* vs `csrt_tracker.*`, `tracker.hpp` |
| Actuation Auditor | Audit AM7-L*-ACT-* vs `fusion_hat_i2c.*`, `fusion_hat.hpp` |
| Ballistics Auditor | Audit AM7-L*-BALL-* vs `ballistic_solver.*`, `ballistic_solver.hpp` |
| HUD/Telemetry Auditor | Audit AM7-L*-HUD-* and AM7-L*-TEL-* vs `hud_socket.*`, `telemetry_writer.*` |
| Core/Safety Auditor | Audit AM7-L*-SAFE-*, AM7-L*-RT-* vs `safety_monitor.*`, `ring_buffer.*`, `timing.*` |

## Data Flow

```
spec.md (read-only, shared)
    ├── State Machine Auditor ──┐
    ├── Vision Auditor ─────────┤
    ├── Tracking Auditor ───────┤
    ├── Actuation Auditor ──────┤──> merge ──> verification report
    ├── Ballistics Auditor ─────┤
    ├── HUD/Telemetry Auditor ──┤
    └── Core/Safety Auditor ────┘

native build
    └── Test Runner ────────────────> test results table
```

Each spec auditor works independently — no shared mutable state.

## Per-Agent Output Format

Each auditor produces a list of findings in this format:

```
[PASS]    AM7-L2-VIS-008  CSRT tracker integrated — csrt_tracker.hpp:TrackSolution
[PARTIAL] AM7-L2-VIS-012  Color segmentation stub present, not implemented
[GAP]     AM7-L2-VIS-015  No histogram equalization preprocessing
```

Status definitions:
- **PASS** — requirement clearly satisfied by existing implementation
- **PARTIAL** — stub or partial implementation present, not fully satisfied
- **GAP** — no implementation found for requirement

## Output Artifact

`docs/reports/2026-03-06-verification.md` with three sections:

1. **Test Results** — pass/fail counts, list of any failing tests with output
2. **Spec Compliance Table** — all AM7-L* tags grouped by subsystem, each with PASS/PARTIAL/GAP status and evidence
3. **Summary** — total counts, prioritized remediation list for PARTIAL/GAP items

## Gate Update

After merge, update `agent_sessions/session_20260306_001/blackboard/quality_gates.json`:
- `verification_complete` → `passed` if 0 GAPs and all tests pass
- `verification_complete` → `failed` if any GAPs or test failures, with failure_reason listing the items
