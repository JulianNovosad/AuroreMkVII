#!/bin/bash
# run_with_validation.sh - Run AuroreMkVI with post-execution validation
#
# Usage: ./run_with_validation.sh [duration_seconds]
#
# This script:
# 1. Builds the project if needed
# 2. Runs AuroreMkVI for specified duration (default: 60s)
# 3. Parses logs and generates validation report
# 4. Returns appropriate exit code

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
LOG_DIR="$PROJECT_DIR/logs"
EXECUTABLE="$BUILD_DIR/AuroreMkVI"
VALIDATOR="$SCRIPT_DIR/runtime_validator.py"

# Configuration
DURATION=${1:-60}
TIMEOUT_CMD="timeout"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_msg() {
    echo -e "${BLUE}[VALIDATE]${NC} $1"
}

success_msg() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

warn_msg() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error_msg() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check dependencies
check_dependencies() {
    log_msg "Checking dependencies..."

    if ! command -v python3 &> /dev/null; then
        error_msg "Python 3 not found"
        exit 1
    fi

    if [ ! -f "$VALIDATOR" ]; then
        error_msg "Validator script not found: $VALIDATOR"
        exit 1
    fi

    if [ ! -x "$EXECUTABLE" ]; then
        log_msg "Executable not found or not built. Building..."
        build_project
    fi
}

# Build the project
build_project() {
    log_msg "Building AuroreMkVI..."
    cd "$BUILD_DIR"

    if ! make AuroreMkVI -j4 2>&1 | tail -20; then
        error_msg "Build failed"
        exit 1
    fi

    success_msg "Build successful"
}

# Run the runtime
run_runtime() {
    log_msg "Starting AuroreMkVI for ${DURATION}s..."

    # Create log directory if needed
    mkdir -p "$LOG_DIR"

    # Generate timestamped log file
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    LOG_FILE="$LOG_DIR/aurore_${TIMESTAMP}.log"

    log_msg "Log file: $LOG_FILE"

    # Run with timeout
    # Note: We use a subshell to capture any crashes
    (
        set +e
        "$EXECUTABLE" 2>&1
        EXIT_CODE=$?
        exit $EXIT_CODE
    ) &
    PID=$!

    # Monitor progress
    ELAPSED=0
    while [ $ELAPSED -lt "$DURATION" ]; do
        if ! kill -0 $PID 2>/dev/null; then
            log_msg "Process exited early at ${ELAPSED}s"
            break
        fi
        sleep 5
        ELAPSED=$((ELAPSED + 5))
        echo -ne "\r${BLUE}[VALIDATE]${NC} Running: ${ELAPSED}s / ${DURATION}s  "
    done

    echo ""  # New line after progress

    # Kill if still running
    if kill -0 $PID 2>/dev/null; then
        log_msg "Terminating process after ${DURATION}s"
        kill -TERM $PID 2>/dev/null || true
        sleep 2
        kill -9 $PID 2>/dev/null || true
    fi

    # Wait for process to fully terminate
    wait $PID 2>/dev/null || true

    success_msg "Runtime execution complete"

    # Return the log file path
    echo "$LOG_FILE"
}

# Run validation
run_validation() {
    local LOG_FILE=$1

    log_msg "Running validation on $LOG_FILE..."

    cd "$PROJECT_DIR"

    # Run validator
    REPORT=$(python3 "$VALIDATOR" --log-file "$LOG_FILE" --duration "$DURATION" 2>&1)
    EXIT_CODE=$?

    echo "$REPORT"

    return $EXIT_CODE
}

# Main
main() {
    echo "========================================"
    echo "AuroreMkVI Runtime Validation"
    echo "========================================"
    echo ""

    # Check dependencies
    check_dependencies

    # Build if needed
    if [ ! -x "$EXECUTABLE" ]; then
        build_project
    fi

    # Run runtime
    LOG_FILE=$(run_runtime)

    # Run validation
    if [ -n "$LOG_FILE" ] && [ -f "$LOG_FILE" ]; then
        run_validation "$LOG_FILE"
        EXIT_CODE=$?

        echo ""
        echo "========================================"

        if [ $EXIT_CODE -eq 0 ]; then
            success_msg "Validation PASSED"
        elif [ $EXIT_CODE -eq 2 ]; then
            warn_msg "Validation completed with WARNINGS"
        else
            error_msg "Validation FAILED"
        fi

        log_msg "Full log: $LOG_FILE"
        log_msg "Validator script: $VALIDATOR"

        exit $EXIT_CODE
    else
        error_msg "No log file generated"
        exit 1
    fi
}

main "$@"
