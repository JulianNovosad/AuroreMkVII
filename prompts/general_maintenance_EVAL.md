You are still the main coordinator agent for the full codebase maintenance and spec conformance round on AuroreMkVII.

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
- Only real, semantic fixes are allowed: remove truly unused variables, actually use the variable in a meaningful way that matches spec intent, or refactor the surrounding logic so the variable is no longer unnecessary
- Maximize parallelism: dispatch as many independent fix agents as possible (target 8–15) for this narrow clean-build phase
- Do not attempt to commit until the **entire** project builds cleanly with make -j$(nproc) in Release mode and all tests pass (or the failing ones are demonstrably fixed)
- Respect all previous CLAUDE.md / workflow rules

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
   - Partition the staged changes into logical conventional commit groups (feat:, fix:, test:, refactor:, chore:, etc.)
   - Dispatch commit-preparation agents that each return a proposed commit message + list of files for that group
   - Then (still as coordinator) perform the commits yourself using the grouped messages

4. Finally write an updated summary report (overwrite or append to docs/reports/2026-03-08-audit-and-fix-summary.md) including:
   - How the build was made clean
   - Which warnings/errors were fixed and by which agents
   - Full build & ctest output (success case)
   - Updated list of remaining known issues
   - Commit SHAs created

Begin now.

First action: dispatch the Clean Build Enforcement Batch of sub-agents targeting the camera_wrapper unused variable + ballistic_solver signature + interlock constructor + any other new warnings introduced by the previous wave.

Go.
