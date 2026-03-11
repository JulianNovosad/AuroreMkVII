---

## **Project: AuroreMkVII RT Kernel Build**
**Target**: Raspberry Pi 5 (BCM2712)  
**Base OS**: Raspberry Pi OS Lite (Debian Trixie 13)  
**Kernel Version**: 6.12.47+ (matching current running config)  
**Goal**: Hard real-time (PREEMPT_RT) kernel with full hardware support

---

### **Hardware Inventory**
| Component | Interface | Kernel Requirement | Notes |
|-----------|-----------|-------------------|-------|
| SunFounder Fusion HAT+ | I2C (0x17) | `CONFIG_I2C_BCM2835`, `CONFIG_I2C_CHARDEV` | Onboard MCU handles PWM/ADC/motors |
| Laser Rangefinder M01 | UART (GPIO32/33, 9600 baud) | `CONFIG_SERIAL_AMBA_PL011`, `CONFIG_SERIAL_8250` | 3.3V logic, continuous measurement mode |
| Raspberry Pi Camera Module 3 | CSI-2 | `CONFIG_VIDEO_IMX708`, `CONFIG_VIDEO_RASPBERRYPI` | IMX708 sensor |
| Pi 5 SoC | — | `CONFIG_ARCH_BCM2712`, `CONFIG_ARM64` | BCM2712-specific drivers |

---

### **Build Specification**

**Host (PC) Requirements:**
- Ubuntu/Debian with `crossbuild-essential-arm64`
- Toolchain: `aarch64-linux-gnu-`
- Build environment: `bc`, `bison`, `flex`, `libssl-dev`, `make`, `libncurses5-dev`

**Source:**
- Repository: `https://github.com/raspberrypi/linux.git `
- Branch: `rpi-6.12.y`
- Target commit: matching 6.12.47+ (or latest stable in branch)

**Configuration Base:**
- `bcm2712_defconfig` (Pi 5 default)
- **OR** `/boot/config-6.12.47+rpt-rpi-2712` from running system (if exact parity required)

**Critical Config Changes:**
```diff
-CONFIG_PREEMPT=y
+CONFIG_PREEMPT_RT=y
+CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE=y
+CONFIG_HZ_1000=y
```

**Build Targets:**
- `Image.gz` (kernel image)
- `modules` (loadable drivers)
- `dtbs` (device tree blobs for Pi 5)

---

### **Installation Method**
1. Cross-compile on PC
2. Package kernel, modules, and DTBs
3. Transfer to Pi via SCP/USB/network
4. Install to `/boot/firmware/` with unique name (e.g., `kernel_2712_rt.img`)
5. Update `/boot/firmware/config.txt`: `kernel=kernel_2712_rt.img`
6. Reboot, verify with `uname -r` and `grep PREEMPT_RT /boot/config-$(uname -r)`

---

### **Verification Criteria**
- [ ] `uname -r` shows custom kernel version
- [ ] `/proc/sys/kernel/sched_rt_period_us` exists (RT scheduler active)
- [ ] `i2cdetect -y 1` shows 0x17 (HAT MCU responsive)
- [ ] Laser rangefinder responds on UART (9600 baud)
- [ ] Camera Module 3 detected and functional (`libcamera-hello` test)
- [ ] `raspi-config` functional (userland compatibility preserved)

---

### **Constraints**
- Maintain full Raspberry Pi OS Lite ecosystem (raspi-config, apt, etc.)
- No modification to userland or filesystem layout
- Minimal kernel config changes (only RT + performance governors)
- Cross-compile on PC for speed, no native Pi compilation

---

