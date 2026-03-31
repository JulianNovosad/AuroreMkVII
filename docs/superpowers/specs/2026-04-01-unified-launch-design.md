# Unified Launch Design

**Date:** 2026-04-01
**Status:** Approved

## Goal

Launch the Aurore MkVII C++ binary and the aurore-link Node.js server as a single unit — one command starts both, stopping one stops both, and the system auto-starts on Pi boot.

## Approach

Wrapper shell script (`scripts/launch.sh`) + systemd service (`systemd/aurore.service`). No changes to the C++ binary or Node.js server.

## `scripts/launch.sh`

1. Determine the non-root user: prefer `$SUDO_USER`, fall back to `pi`.
2. Start `node aurore-link/server.js` in the background via `sudo -u $NODE_USER`, capture its PID.
3. Register a `trap` on `EXIT`, `INT`, and `TERM` that kills the Node.js PID and waits for it to exit.
4. `exec` the C++ binary — this replaces the shell process so the binary inherits the script's PID. systemd tracks it correctly and `SIGTERM` goes directly to the C++ binary's existing signal handler.
5. When the C++ binary exits for any reason (clean shutdown, crash, SIGTERM), the `EXIT` trap fires and kills Node.js.

Node.js runs as the non-root user. The C++ binary runs as root (required for `SCHED_FIFO`/`mlockall`).

## `systemd/aurore.service`

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

`KillMode=process` ensures systemd only sends SIGTERM to the main PID (the C++ binary after `exec`). The C++ shutdown sequence kills the Node.js child via the trap.

## Dev/Field Use

```bash
sudo ./scripts/launch.sh    # start both
Ctrl+C                      # stops both cleanly
```

## Boot-Time Auto-Start

```bash
sudo cp systemd/aurore.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable aurore
sudo systemctl start aurore
```

## Files Created

| File | Purpose |
|------|---------|
| `scripts/launch.sh` | Wrapper script that starts both processes |
| `systemd/aurore.service` | systemd unit for boot auto-start |

## Out of Scope

- No changes to `src/main.cpp`
- No changes to `aurore-link/server.js`
- No process supervision / restart of individual components (systemd `Restart=on-failure` restarts the whole unit)
