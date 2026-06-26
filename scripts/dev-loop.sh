#!/usr/bin/env bash
# dev-loop.sh — Autonomous AuroreMkVII development loop
#
# Runs a non-interactive opencode session in the project directory every 4 hours.
# Each session reads the issue report and spec, picks the highest-priority
# unresolved item, implements it, runs tests, and commits on success.
#
# Usage:
#   ./scripts/dev-loop.sh            — run one development session now
#   ./scripts/dev-loop.sh install    — install cron job (every 4 hours)
#   ./scripts/dev-loop.sh uninstall  — remove the cron job

set -euo pipefail

# Ensure opencode is in PATH (cron has minimal PATH)
export PATH="/home/pi/.opencode/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
LOG_DIR="$PROJECT_DIR/agent_logs"
SCRIPT_PATH="$(realpath "$SCRIPT_DIR/dev-loop.sh")"

# ---------------------------------------------------------------------------
# The development prompt — sent to Claude on every run
# ---------------------------------------------------------------------------
read -r -d '' PROMPT << 'EOF' || true
Continue core development on AuroreMkVII. Work through these steps in order:

STEP 1 — ORIENT
Read docs/issue_report.md from top to bottom. Identify the highest-priority
unresolved item from the "Critical Blockers" or "Priority Issues" sections.
If those are exhausted, take the next incomplete row from the "Completion by
Area" table whose percentage is below 100%.

STEP 2 — REQUIREMENTS
Read spec.md. Find every requirement tagged AM7-L{n}-{subsystem}-{id} that
is relevant to the chosen item. These are binding — implement to the letter.

STEP 3 — CONVENTIONS
Read CLAUDE.md. Review code style, real-time constraints, no-mock rule, and
MISRA compliance requirements before writing any code.

STEP 4 — EXPLORE (bounded)
Grep for the symbols, functions, or files involved. Read at most 5 source
files before starting implementation. State your approach in 2-3 sentences.

STEP 5 — IMPLEMENT
Make the change. Rules:
  - No mocks, simulations, or fakes — ever.  Hardware tests must connect to
    real hardware or fail immediately with a clear error.
  - No heap allocation in real-time threads after init.
  - All atomics: explicit memory_order_acquire / memory_order_release.
  - MISRA C++:2023: no implicit conversions, no unreachable code.
  - One logical unit per session — do not bundle unrelated changes.

STEP 6 — BUILD AND TEST
  cd build-release && cmake --build . -j$(nproc)
  cd build-release && ctest --output-on-failure
If the build or any test fails, diagnose and fix it before proceeding.
If a hardware test cannot run because hardware is absent, note it and pick
a different item instead — never skip or stub a hardware test.

STEP 7 — COMMIT
Only commit after a clean build and test run.
Commit message format: "<type>: <summary> (<spec-id>)"
Example: "fix: clamp gimbal rate on freecam overflow (AM7-L3-ACT-002)"

STEP 8 — UPDATE THE ISSUE REPORT
Edit docs/issue_report.md:
  - Mark the completed item as done with today's date.
  - Recalculate the completion percentage for the relevant area.
  - Update the "Critical Blockers" list if the item was on it.
  - Add a one-line entry under "Is the Product Fully Done?" describing what
    changed this session.

Do exactly one logical unit of work. Stop after step 8.
EOF

# ---------------------------------------------------------------------------
# Cron entry
# ---------------------------------------------------------------------------
CRON_ENTRY="0 */4 * * * cd '$PROJECT_DIR' && '$SCRIPT_PATH' >> '$LOG_DIR/dev-loop.log' 2>&1"

# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

cmd_run() {
    mkdir -p "$LOG_DIR"
    local logfile="$LOG_DIR/dev-loop-$(date +%Y%m%dT%H%M%S).log"

    echo "========================================================"
    echo " AuroreMkVII dev-loop  $(date '+%Y-%m-%d %H:%M:%S')"
    echo " Log: $logfile"
    echo "========================================================"

    cd "$PROJECT_DIR"

    opencode run \
        --model opencode/minimax-m2.5-free \
        "$PROMPT" \
        2>&1 | tee "$logfile"

    local exit_code="${PIPESTATUS[0]}"

    echo ""
    echo "========================================================"
    echo " Session ended  $(date '+%Y-%m-%d %H:%M:%S')  exit=$exit_code"
    echo "========================================================"

    return "$exit_code"
}

cmd_install() {
    mkdir -p "$LOG_DIR"
    # Remove any existing entry for this script, then add the new one
    local existing
    existing=$(crontab -l 2>/dev/null || true)
    printf '%s\n%s\n' "$(echo "$existing" | grep -v "$SCRIPT_PATH")" "$CRON_ENTRY" | crontab -
    echo "Cron job installed (every 4 hours):"
    crontab -l | grep "$SCRIPT_PATH"
}

cmd_uninstall() {
    crontab -l 2>/dev/null | grep -v "$SCRIPT_PATH" | crontab -
    echo "Cron job removed."
}

# ---------------------------------------------------------------------------
# Dispatch
# ---------------------------------------------------------------------------
COMMAND="${1:-run}"

case "$COMMAND" in
    run|"")    cmd_run ;;
    install)   cmd_install ;;
    uninstall) cmd_uninstall ;;
    *)
        echo "Usage: $0 {run|install|uninstall}" >&2
        exit 1
        ;;
esac
