#!/bin/bash
# scripts/verify-rt-kernel.sh - Verify PREEMPT_RT kernel installation
#
# This script verifies:
# - Kernel version and PREEMPT_RT status
# - CPU isolation configuration
# - Hardware detection (I2C, Camera, UART)
#
# Usage: ./scripts/verify-rt-kernel.sh

set -e

echo "=== AuroreMkVII PREEMPT_RT Kernel Verification ==="
echo ""

# Check kernel version
echo "[1/7] Checking kernel version..."
UNAME_R=$(uname -r)
echo "  Kernel: $UNAME_R"

# Check PREEMPT_RT status
echo "[2/7] Checking PREEMPT_RT status..."
if grep -q "PREEMPT_RT" /proc/config.gz 2>/dev/null || zgrep -q "CONFIG_PREEMPT_RT=y" /proc/config.gz 2>/dev/null; then
    echo "  PREEMPT_RT: ENABLED"
elif [ -f "/boot/config-$UNAME_R" ] && grep -q "CONFIG_PREEMPT_RT=y" "/boot/config-$UNAME_R"; then
    echo "  PREEMPT_RT: ENABLED"
else
    echo "  PREEMPT_RT: NOT FOUND (may need to check /proc/config.gz)"
fi

# Check CPU isolation
echo "[3/7] Checking CPU isolation..."
CMDLINE=$(cat /proc/cmdline)
if echo "$CMDLINE" | grep -q "isolcpus=2-3"; then
    echo "  isolcpus=2-3: OK"
else
    echo "  isolcpus=2-3: NOT SET"
fi

if echo "$CMDLINE" | grep -q "nohz_full=2-3"; then
    echo "  nohz_full=2-3: OK"
else
    echo "  nohz_full=2-3: NOT SET"
fi

if echo "$CMDLINE" | grep -q "rcu_nocbs=2-3"; then
    echo "  rcu_nocbs=2-3: OK"
else
    echo "  rcu_nocbs=2-3: NOT SET"
fi

if echo "$CMDLINE" | grep -q "irqaffinity=0-1"; then
    echo "  irqaffinity=0-1: OK"
else
    echo "  irqaffinity=0-1: NOT SET"
fi

# Check CPU governor
echo "[4/7] Checking CPU governor..."
for cpu in 0 1 2 3; do
    GOVERNOR=$(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor 2>/dev/null || echo "N/A")
    echo "  CPU$cpu governor: $GOVERNOR"
done

# Check I2C bus
echo "[5/7] Checking I2C bus..."
if [ -c /dev/i2c-1 ]; then
    echo "  /dev/i2c-1: EXISTS"
    if command -v i2cdetect &> /dev/null; then
        echo "  Scanning I2C bus..."
        i2cdetect -y 1 2>/dev/null || echo "  (No devices detected or I2C not enabled)"
    else
        echo "  i2cdetect: NOT INSTALLED (install i2c-tools for hardware scan)"
    fi
else
    echo "  /dev/i2c-1: NOT FOUND"
fi

# Check camera
echo "[6/7] Checking camera subsystem..."
if [ -d /sys/class/video4linux ]; then
    CAMERAS=$(ls /sys/class/video4linux 2>/dev/null | wc -l)
    echo "  Video4Linux devices: $CAMERAS found"
    ls -la /dev/video* 2>/dev/null || echo "  /dev/video*: NOT FOUND"
else
    echo "  Video4Linux: NOT FOUND"
fi

# Check serial/UART
echo "[7/7] Checking UART/serial..."
if [ -c /dev/ttyAMA0 ]; then
    echo "  /dev/ttyAMA0: EXISTS (PL011 UART)"
elif [ -c /dev/ttyS0 ]; then
    echo "  /dev/ttyS0: EXISTS (mini UART)"
else
    echo "  UART: NOT FOUND (may need to enable in config.txt)"
fi

echo ""
echo "=== Verification Complete ==="
echo ""
echo "Summary:"
echo "  - If PREEMPT_RT is enabled and CPU isolation is configured, the kernel is ready."
echo "  - Run AuroreMkVII with: sudo ./aurore (required for SCHED_FIFO and mlockall)"
echo ""
