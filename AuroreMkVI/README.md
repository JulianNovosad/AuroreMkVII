# Aurore Mk VI

**Defense-grade Embedded System**

## Overview
Aurore Mk VI is a safety-critical, real-time embedded system designed for the Raspberry Pi 5. It features a zero-copy inference pipeline, deterministic actuation, and strict auditing.

## Doctrine
This project adheres to the GEMINI doctrine. See `.gemini/GEMINI.md`.

## Directory Structure

- `docs/`: Documentation and reports.
- `src/`: C++17 source code.
- `kernel/`: Kernel patches and config.
- `deps/`: Third-party dependencies.

## Requirements
- Raspberry Pi 5 (4GB+)
- Raspberry Pi OS Lite (64-bit)
- Active Cooling



## Artifacts
Artifacts are generated in `artifacts/`, including:
- `bin/aurore_mk_vi` - Main executable
- `kernel/Image` - Kernel image
- `checksums.txt` - SHA256 checksums

## Running
```bash
sudo ./artifacts/bin/aurore_mk_vi
```

Or install system-wide:
```bash
sudo cp artifacts/bin/aurore_mk_vi /usr/local/bin/
sudo aurore_mk_vi
```

## Monitoring
Live telemetry: Run binary
System logs: `/var/log/aurore/current/unified.csv`
