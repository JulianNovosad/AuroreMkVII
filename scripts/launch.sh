#!/usr/bin/env bash
# launch.sh — Start aurore-link (Node.js HUD) and the Aurore C++ binary together.
# Run as root (required for SCHED_FIFO/mlockall). Node.js runs as NODE_USER.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

# Determine non-root user for Node.js
NODE_USER="${SUDO_USER:-pi}"

# Path to build output (prefer build-native, then build-rpi, then build)
BINARY="${REPO_DIR}/build-native/aurore"
if [[ ! -x "$BINARY" ]]; then
    BINARY="${REPO_DIR}/build-rpi/aurore"
fi
if [[ ! -x "$BINARY" ]]; then
    BINARY="${REPO_DIR}/build/aurore"
fi
if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: aurore binary not found. Build first with: cmake --build build-native --target aurore" >&2
    exit 1
fi

NODE_PID=""

cleanup() {
    if [[ -n "$NODE_PID" ]] && kill -0 "$NODE_PID" 2>/dev/null; then
        echo "Stopping aurore-link (PID $NODE_PID)..."
        kill "$NODE_PID"
        wait "$NODE_PID" 2>/dev/null || true
    fi
}

trap cleanup EXIT INT TERM

# Create runtime socket directory (required for HUD telemetry socket)
mkdir -p /run/aurore
chmod 755 /run/aurore

echo "Starting aurore-link as user '$NODE_USER'..."
sudo -u "$NODE_USER" node "${REPO_DIR}/aurore-link/server.js" &
NODE_PID=$!
echo "aurore-link started (PID $NODE_PID)"

echo "Starting aurore binary: $BINARY"
exec "$BINARY" "$@"
