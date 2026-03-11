# PREEMPT_RT Kernel Build Guide for AuroreMkVII

**Target**: Raspberry Pi 5 (BCM2712)  
**Kernel Version**: 6.12.47+ (rpi-6.12.y branch)  
**Goal**: Hard real-time (PREEMPT_RT) kernel with full hardware support

---

## Quick Start

```bash
# 1. Build the kernel (2-4 hours on Pi 5)
cd /home/pi/linux-rt-kernel
make -j4 Image.gz modules dtbs

# 2. Install kernel and modules
sudo ./scripts/install-kernel.sh

# 3. Configure boot parameters
sudo ./scripts/configure-boot.sh

# 4. Reboot
sudo reboot

# 5. Verify after reboot
./scripts/verify-rt-kernel.sh
```

---

## Hardware Requirements

| Component | Interface | Kernel Requirement |
|-----------|-----------|-------------------|
| SunFounder Fusion HAT+ | I2C (0x17) | `CONFIG_I2C_BCM2835`, `CONFIG_I2C_CHARDEV` |
| Laser Rangefinder M01 | UART (GPIO32/33, 9600 baud) | `CONFIG_SERIAL_AMBA_PL011` |
| Raspberry Pi Camera Module 3 | CSI-2 | `CONFIG_VIDEO_IMX708` |
| Pi 5 SoC | — | `CONFIG_ARCH_BCM2712`, `CONFIG_ARM64` |

---

## Build Prerequisites

Install build dependencies on Raspberry Pi 5:

```bash
sudo apt update
sudo apt install -y bc bison flex libssl-dev make libncurses-dev gcc g++ git curl
```

---

## Kernel Configuration

The kernel is configured with the following real-time optimizations:

| Option | Value | Purpose |
|--------|-------|---------|
| `CONFIG_PREEMPT_RT` | y | Full PREEMPT_RT real-time support |
| `CONFIG_HZ_1000` | y | 1000Hz timer frequency (1ms resolution) |
| `CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE` | y | Performance CPU governor by default |

### Source

- **Repository**: https://github.com/raspberrypi/linux.git
- **Branch**: rpi-6.12.y
- **Base Config**: Current running kernel config (`/boot/config-$(uname -r)`)

### Clone and Configure:

```bash
cd /home/pi
git clone --depth=1 --branch=rpi-6.12.y https://github.com/raspberrypi/linux.git linux-rt-kernel
cd linux-rt-kernel

# Use current running config as base
cp /boot/config-$(uname -r) .config

# Enable PREEMPT_RT
scripts/config --enable PREEMPT_RT --disable PREEMPT
make olddefconfig
```

---

## Build Process

```bash
cd /home/pi/linux-rt-kernel
make -j4 Image.gz modules dtbs 2>&1 | tee ~/kernel-build.log
```

**Expected build time**: 2-4 hours on Raspberry Pi 5 (varies with SD card speed and cooling)

**Build outputs**:
- `arch/arm64/boot/Image.gz` — Compressed kernel image
- `arch/arm64/boot/dts/broadcom/*.dtb` — Device tree blobs
- `modules/` — Kernel modules (installed to `/lib/modules/`)

---

## Installation

### Step 1: Install kernel

```bash
sudo ./scripts/install-kernel.sh
```

This script:
1. Verifies build artifacts exist
2. Backs up current kernel
3. Copies `Image.gz` to `/boot/firmware/kernel_2712_rt.img`
4. Installs kernel modules
5. Installs device tree blobs
6. Updates `/boot/firmware/config.txt`

### Step 2: Configure boot parameters

```bash
sudo ./scripts/configure-boot.sh
```

This adds the following to `/boot/firmware/cmdline.txt`:
```
isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1
```

**What these parameters do**:
- `isolcpus=2-3` — Isolate CPUs 2 and 3 from kernel scheduler
- `nohz_full=2-3` — Disable periodic timer ticks on isolated CPUs
- `rcu_nocbs=2-3` — Offload RCU callbacks from isolated CPUs
- `irqaffinity=0-1` — Route IRQs to CPUs 0-1 (non-isolated)

### Step 3: Reboot

```bash
sudo reboot
```

---

## Verification

After reboot, run:

```bash
./scripts/verify-rt-kernel.sh
```

### Manual verification:

```bash
# Check kernel version
uname -r

# Verify PREEMPT_RT is enabled
zgrep "CONFIG_PREEMPT_RT" /proc/config.gz

# Check boot parameters
cat /proc/cmdline | grep isolcpus

# Check CPU governors
for cpu in 0 1 2 3; do
    cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor
done

# Check I2C devices (Fusion HAT+ should show at 0x17)
i2cdetect -y 1

# Test camera
libcamera-hello --timeout 5000

# Test UART (if laser rangefinder connected)
cat /dev/ttyAMA0
```

---

## Running AuroreMkVII

With the PREEMPT_RT kernel installed, run AuroreMkVII with real-time priorities:

```bash
sudo ./aurore
```

**Why root is required**:
- `SCHED_FIFO` scheduling (real-time priority)
- `mlockall()` (lock memory, prevent page faults)
- Direct hardware access (I2C, GPIO, camera)

---

## Troubleshooting

### Kernel doesn't boot

1. Hold Shift during boot to access bootloader menu
2. Select original kernel from menu
3. Edit `/boot/firmware/config.txt` and remove/comment `kernel=kernel_2712_rt.img`

### PREEMPT_RT not enabled

Check that config was applied correctly:

```bash
zgrep "PREEMPT_RT" /proc/config.gz
```

If not showing `CONFIG_PREEMPT_RT=y`, rebuild kernel with correct config.

### I2C devices not detected

Ensure I2C is enabled in `/boot/firmware/config.txt`:

```
dtparam=i2c_arm=on
dtparam=i2c_arm_baudrate=400000
```

### Camera not detected

Ensure camera is enabled in `/boot/firmware/config.txt`:

```
camera-auto-enable=1
```

### High jitter on isolated CPUs

Check that IRQs are not routed to isolated CPUs:

```bash
cat /proc/interrupts | grep CPU
```

All interrupts should be on CPU0 or CPU1 only.

---

## Rollback

To revert to the stock kernel:

1. Edit `/boot/firmware/config.txt`
2. Remove or comment the line: `kernel=kernel_2712_rt.img`
3. Reboot: `sudo reboot`

The original kernel backup is stored at:
- `/boot/firmware/kernel_2712_rt.img.bak.YYYYMMDD_HHMMSS`

---

## Performance Expectations

With PREEMPT_RT kernel and proper CPU isolation:

| Metric | Target | Measurement |
|--------|--------|-------------|
| Vision pipeline latency | ≤3ms | `DeadlineMonitor` |
| End-to-end WCET | ≤5ms | `wcet_analysis.sh` |
| Jitter (99.9th percentile) | ≤5% | `jitter_monitor.sh` |
| Safety monitor response | ≤1ms | `SafetyMonitor` |

---

## References

- [Linux PREEMPT_RT documentation](https://wiki.linuxfoundation.org/realtime/documentation/start)
- [Raspberry Pi kernel source](https://github.com/raspberrypi/linux)
- [clock_nanosleep(2) man page](https://man7.org/linux/man-pages/man2/clock_nanosleep.2.html)
- [AuroreMkVII spec.md](spec.md)
- [docs/kernel_spec.md](kernel_spec.md)

---

**Last Updated**: 2026-03-11  
**Maintainer**: AuroreMkVII Project
