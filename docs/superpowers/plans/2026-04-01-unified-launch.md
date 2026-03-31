# Unified Launch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One command starts both the Aurore MkVII C++ binary and the aurore-link Node.js server; stopping one stops both; the system auto-starts on Pi boot.

**Architecture:** A wrapper shell script starts Node.js in the background as the non-root user, registers a trap to kill it on exit, then `exec`s the C++ binary so the binary becomes the script's PID. A systemd service unit wraps the script for boot-time auto-start.

**Tech Stack:** bash, Node.js 18+, systemd

---

### Task 1: Create `scripts/launch.sh`

**Files:**
- Create: `scripts/launch.sh`

- [ ] **Step 1: Create the script**

```bash
#!/usr/bin/env bash
# launch.sh — Start aurore-link (Node.js HUD) and the Aurore C++ binary together.
# Run as root (required for SCHED_FIFO/mlockall). Node.js runs as NODE_USER.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

# Determine non-root user for Node.js
NODE_USER="${SUDO_USER:-pi}"

# Path to build output (prefer Release, fall back to any build dir)
BINARY="${REPO_DIR}/build-rpi/aurore"
if [[ ! -x "$BINARY" ]]; then
    BINARY="${REPO_DIR}/build/aurore"
fi
if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: aurore binary not found. Build first with scripts/build-rpi.sh" >&2
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

echo "Starting aurore-link as user '$NODE_USER'..."
sudo -u "$NODE_USER" node "${REPO_DIR}/aurore-link/server.js" &
NODE_PID=$!
echo "aurore-link started (PID $NODE_PID)"

echo "Starting aurore binary: $BINARY"
exec "$BINARY" "$@"
```

- [ ] **Step 2: Make it executable**

```bash
chmod +x /home/pi/AuroreMkVII/scripts/launch.sh
```

- [ ] **Step 3: Smoke-test (dry run — no hardware required)**

Run with `--dry-run` flag (the C++ binary accepts this) so it exits cleanly:

```bash
cd /home/pi/AuroreMkVII
sudo ./scripts/launch.sh --dry-run
```

Expected output (order may vary):
```
Starting aurore-link as user 'pi'...
aurore-link started (PID <N>)
Starting aurore binary: .../aurore
...
Stopping aurore-link (PID <N>)...
```

Both processes should exit cleanly. Verify with:
```bash
pgrep -a node | grep server.js  # should be empty after exit
```

- [ ] **Step 4: Commit**

```bash
git add scripts/launch.sh
git commit -m "feat: add launch.sh to start aurore + aurore-link as one unit"
```

---

### Task 2: Create `systemd/aurore.service`

**Files:**
- Create: `systemd/aurore.service`

- [ ] **Step 1: Create the systemd directory and unit file**

```bash
mkdir -p /home/pi/AuroreMkVII/systemd
```

Contents of `systemd/aurore.service`:

```ini
[Unit]
Description=Aurore MkVII Fire Control System
After=network.target

[Service]
Type=simple
ExecStart=/home/pi/AuroreMkVII/scripts/launch.sh
WorkingDirectory=/home/pi/AuroreMkVII
User=root
Restart=on-failure
RestartSec=5
KillMode=process

[Install]
WantedBy=multi-user.target
```

`KillMode=process` ensures systemd only signals the main PID (the C++ binary after `exec`). The `EXIT` trap in the script handles Node.js cleanup.

- [ ] **Step 2: Install and enable the service**

```bash
sudo cp /home/pi/AuroreMkVII/systemd/aurore.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable aurore
```

Expected output from enable:
```
Created symlink /etc/systemd/system/multi-user.target.wants/aurore.service → /etc/systemd/system/aurore.service.
```

- [ ] **Step 3: Test the service start/stop**

```bash
sudo systemctl start aurore
sleep 2
sudo systemctl status aurore
```

Expected: `Active: active (running)`, main PID is the `aurore` C++ binary.

```bash
sudo systemctl stop aurore
sudo systemctl status aurore
```

Expected: `Active: inactive (dead)`. Verify Node.js also stopped:
```bash
pgrep -a node | grep server.js  # should be empty
```

- [ ] **Step 4: Commit**

```bash
git add systemd/aurore.service
git commit -m "feat: add systemd service unit for boot-time auto-start"
```

---

### Task 3: Update README with launch instructions

**Files:**
- Modify: `README.md` — add a "Running" section

- [ ] **Step 1: Add launch instructions to README**

Find the existing running/usage section in `README.md` and add/replace with:

```markdown
## Running

### Start everything (dev / field)

```bash
sudo ./scripts/launch.sh          # starts aurore + aurore-link HUD
# HUD: http://<pi-ip>:8080/
# Ctrl+C stops both cleanly
```

### Boot-time auto-start (production)

```bash
sudo cp systemd/aurore.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now aurore

# Check status
sudo systemctl status aurore

# Logs
journalctl -u aurore -f
```
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: add unified launch instructions to README"
```
