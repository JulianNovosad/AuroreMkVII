# Aurore Mk VI - Operator Manual

## Installation
1. Flash Raspberry Pi OS Lite (64-bit).
2. Copy `artifacts/kernel/Image` to `/boot/kernel8.img`.
3. Copy `artifacts/kernel/*.dtb` to `/boot/`.
4. Reboot.

## Runtime
To start the system:
```bash
sudo ./artifacts/bin/aurore_mk_vi
```

Or after copying to /usr/local/bin:
```bash
sudo aurore_mk_vi
```

## Monitoring
Check `unified.csv` in `/var/log/aurore/current/`.

## Emergency Stop
Disengage the physical kill-switch to stop actuation immediately.
