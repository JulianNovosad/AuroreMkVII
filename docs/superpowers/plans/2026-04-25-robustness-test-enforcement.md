# Robustness Test & CI Enforcement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build, fix, and enforce the 10-part robustness test suite (Parts 1–10) plus CI tier enforcement (Part 11) for Aurore MkVII.

**Architecture:** Build-first, fix-forward. All 10 test families exist as untracked files; test_infrastructure.hpp/cpp is the shared foundation. CI uses ctest label filtering (`-L tierN`). Hardware-dependent tests hard-fail when hardware absent and are assigned Tier 4 (HIL). No mocks anywhere.

**Tech Stack:** C++17, CMake/CTest, GitHub Actions, ctest label filtering, CLOCK_MONOTONIC_RAW, SCHED_FIFO (optional), libcamera (Tier 4 only).

---

## Task 1: Create scripts/build-native.sh

**Files:**
- Create: `scripts/build-native.sh`

- [ ] **Step 1: Create the script**

```bash
cat > /home/pi/AuroreMkVII/scripts/build-native.sh << 'EOF'
#!/bin/bash
# build-native.sh - Native build on the Pi (aarch64) or x86_64 host
# Usage: ./scripts/build-native.sh [Debug|Release|RelWithDebInfo]
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_TYPE="${1:-Release}"
BUILD_DIR="$PROJECT_DIR/build-native"
echo "=== Aurore MkVII Native Build ==="
echo "Build type: $BUILD_TYPE"
echo "Build directory: $BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DAURORE_ENABLE_TESTS=ON \
    -DAURORE_REALTIME=ON
cmake --build . -j"$(nproc)"
echo "=== Build complete ==="
EOF
chmod +x /home/pi/AuroreMkVII/scripts/build-native.sh
```

- [ ] **Step 2: Verify the script exists and is executable**

```bash
ls -la /home/pi/AuroreMkVII/scripts/build-native.sh
```

Expected: `-rwxr-xr-x` permissions

---

## Task 2: Initial Build — Capture All Errors

**Files:**
- Build directory: `build-native/`

- [ ] **Step 1: Run build, capturing output**

```bash
cd /home/pi/AuroreMkVII && mkdir -p build-native && cd build-native && cmake .. -DCMAKE_BUILD_TYPE=Release -DAURORE_ENABLE_TESTS=ON -DAURORE_REALTIME=ON 2>&1 | tail -20
```

- [ ] **Step 2: Attempt build, capture first errors**

```bash
cd /home/pi/AuroreMkVII/build-native && cmake --build . -j$(nproc) 2>&1 | grep -E "error:|undefined|cannot" | head -40
```

- [ ] **Step 3: Identify which test targets fail to compile**

```bash
cd /home/pi/AuroreMkVII/build-native && cmake --build . --target memory_resource_test numeric_robustness_test state_mode_integrity_test fault_containment_test concurrency_pathology_test hostile_input_test resource_exhaustion_test reset_recovery_test temporal_consistency_test observability_test 2>&1 | grep "error:" | head -60
```

---

## Task 3: Fix Compilation Errors in Test Files

For each error found in Task 2, apply the minimal fix. Common patterns:

- Duplicate macro definitions → guard with `#ifndef`
- Missing `#include` for headers actually used
- API mismatch (e.g. wrong argument counts)
- `static` function that should be non-static

- [ ] **Step 1: Fix each failing test binary one at a time, in tier order (tier0 first)**

Tier 0: `memory_resource_test`, `numeric_robustness_test`
Tier 1: `state_mode_integrity_test`, `fault_containment_test`, `concurrency_pathology_test`, `hostile_input_test`, `resource_exhaustion_test`, `reset_recovery_test`  
Tier 2: `temporal_consistency_test`, `observability_test`

For each, run:
```bash
cd /home/pi/AuroreMkVII/build-native && cmake --build . --target <test_name> 2>&1 | grep "error:"
```

- [ ] **Step 2: After each fix, rebuild to verify error is gone**

```bash
cd /home/pi/AuroreMkVII/build-native && cmake --build . --target <test_name> 2>&1 | grep -c "error:" && echo "errors remain" || echo "clean"
```

- [ ] **Step 3: Full build must be clean**

```bash
cd /home/pi/AuroreMkVII/build-native && cmake --build . -j$(nproc) 2>&1 | grep "error:" | wc -l
```

Expected: `0`

---

## Task 4: Fix CI YAML — Label Filtering, Nightly Schedule, Regression Protection

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Replace `-R regex` patterns with `-L tierN` label filtering**

In `.github/workflows/ci.yml`, the Tier 0 step becomes:
```yaml
- name: Run Tier 0 tests
  run: cd build-native && ctest -L tier0 --output-on-failure -j$(nproc)
```

Tier 1:
```yaml
- name: Run Tier 1 tests
  run: cd build-native && ctest -L tier1 --output-on-failure -j$(nproc)
```

Tier 2:
```yaml
- name: Run Tier 2 tests
  run: cd build-native && ctest -L tier2 --output-on-failure -j$(nproc)
```

Tier 3 (nightly):
```yaml
- name: Run Tier 3 tests
  run: cd build-native && ctest -L tier3 --output-on-failure -j$(nproc)
```

- [ ] **Step 2: Add nightly schedule trigger to workflow `on:` block**

```yaml
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]
  schedule:
    - cron: '0 2 * * *'   # 02:00 UTC nightly
```

- [ ] **Step 3: Harden regression protection job to fail on test deletion**

Replace the `regression-protection` job step body with:
```bash
DELETED=$(git diff --name-only --diff-filter=D HEAD~1 HEAD 2>/dev/null | grep "^tests/" || true)
if [ -n "$DELETED" ]; then
  echo "ERROR: Test files deleted without approval:"
  echo "$DELETED"
  echo "Add the 'allow-test-deletion' label to this PR to override."
  exit 1
fi
```

- [ ] **Step 4: Remove duplicate `build-and-test` job**

The `build-and-test` job reruns what the tier jobs already do. Remove it or make it depend on tier jobs with `needs:`.

- [ ] **Step 5: Fix `Check Tier N results` steps — remove the redundant double-ctest invocations**

The existing check steps re-run ctest a second time to count pass/fail. Replace with:
```yaml
- name: Run Tier 0 tests
  run: |
    cd build-native
    ctest -L tier0 --output-on-failure -j$(nproc)
```
CTest already returns non-zero on failure, so no second invocation needed.

---

## Task 5: Run Tests by Tier — Fix Logic Failures

- [ ] **Step 1: Run Tier 0, review output**

```bash
cd /home/pi/AuroreMkVII/build-native && ctest -L tier0 --output-on-failure
```

Expected: All pass. Fix any failures before proceeding.

- [ ] **Step 2: Run Tier 1**

```bash
cd /home/pi/AuroreMkVII/build-native && ctest -L tier1 --output-on-failure
```

- [ ] **Step 3: Run Tier 2**

```bash
cd /home/pi/AuroreMkVII/build-native && ctest -L tier2 --output-on-failure
```

- [ ] **Step 4: Run full suite, collect count**

```bash
cd /home/pi/AuroreMkVII/build-native && ctest --output-on-failure 2>&1 | tail -5
```

Expected: Test count ≥ 247 (237 existing + 10 new families).

---

## Task 6: Write Design Doc and docs/test_family_definitions.md

**Files:**
- Create/update: `docs/test_family_definitions.md`
- Create: `docs/superpowers/specs/2026-04-25-robustness-test-enforcement-design.md`

- [ ] **Step 1: Verify docs/test_family_definitions.md has accurate tier table**

The file must contain:
- Tier table (binary → part → tier → blocks_merge → hardware_required)
- Hard-fail hardware policy (tests FAIL, not skip, if hardware absent; assigned Tier 4)
- Rule: CMakeLists LABELS are the single source of truth for CI tier membership
- Rule: Test files may not be deleted without `allow-test-deletion` PR label

- [ ] **Step 2: Write the spec doc**

Content: Summary of design decisions, tier mapping, enforcement rules.

---

## Task 7: Commit Everything

- [ ] **Step 1: Stage all new and modified files**

```bash
cd /home/pi/AuroreMkVII
git add scripts/build-native.sh
git add include/aurore/test_infrastructure.hpp include/aurore/test_utils.hpp
git add src/test_infrastructure.cpp
git add tests/unit/memory_resource_test.cpp tests/unit/numeric_robustness_test.cpp
git add tests/unit/state_mode_integrity_test.cpp tests/unit/fault_containment_test.cpp
git add tests/unit/concurrency_pathology_test.cpp tests/unit/hostile_input_test.cpp
git add tests/unit/resource_exhaustion_test.cpp tests/unit/reset_recovery_test.cpp
git add tests/unit/temporal_consistency_test.cpp tests/unit/observability_test.cpp
git add CMakeLists.txt .github/workflows/ci.yml
git add AGENTS.md docs/test_family_definitions.md
git add docs/superpowers/
```

- [ ] **Step 2: Commit**

```bash
cd /home/pi/AuroreMkVII && git commit -m "$(cat <<'EOF'
test: add 10-part robustness suite with tier-labelled CI enforcement

Parts 1-10 cover: stack/heap integrity, state/mode integrity, fault
containment, concurrency pathology, temporal consistency, numeric
robustness, hostile input, resource exhaustion, reset recovery, and
observability. All hardware-dependent tests are Tier 4 (hard-fail when
hardware absent). CI uses ctest -L tierN label filtering. Nightly
schedule added for Tier 3 stress/soak.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```
