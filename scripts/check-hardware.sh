#!/bin/bash
# check-hardware.sh - Pre-flight hardware verification for Aurore MkVII
# 
# This script verifies all required hardware is connected before running tests.
# Run with: sudo ./scripts/check-hardware.sh
#
# Exit codes:
#   0 - All hardware present
#   1 - Missing hardware (check output for details)

# Don't exit on error - we want to check all hardware
set +e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PASS_COUNT=0
FAIL_COUNT=0
WARN_COUNT=0

echo "========================================"
echo "Aurore MkVII Hardware Pre-flight Check"
echo "========================================"
echo ""

# Function to check and report
check_hardware() {
    local name="$1"
    local command="$2"
    local fix="$3"
    
    if eval "$command" > /dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} $name"
        ((PASS_COUNT++))
        return 0
    else
        echo -e "${RED}[FAIL]${NC} $name"
        echo "       Fix: $fix"
        ((FAIL_COUNT++))
        return 1
    fi
}

# Function to check and warn (non-critical)
check_optional() {
    local name="$1"
    local command="$2"
    
    if eval "$command" > /dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} $name"
        ((PASS_COUNT++))
        return 0
    else
        echo -e "${YELLOW}[WARN]${NC} $name (optional)"
        ((WARN_COUNT++))
        return 0
    fi
}

echo "=== Camera System ==="
check_hardware "IMX708 camera (MIPI CSI)" \
    "rpicam-hello --list-cameras 2>&1 | grep -q imx708" \
    "Reseat MIPI CSI cable, ensure camera is powered"

echo ""
echo "=== UART Devices ==="
check_hardware "UART0 (ttyAMA0/ttyAMA10)" \
    "ls /dev/ttyAMA0 /dev/ttyAMA10 2>/dev/null" \
    "Enable UART in /boot/firmware/config.txt (enable_uart=1)"

echo ""
echo "=== I2C Bus ==="
check_hardware "I2C-1 bus" \
    "ls /dev/i2c-1" \
    "Enable I2C in /boot/firmware/config.txt (dtparam=i2c_arm=on)"

echo ""
echo "=== Fusion HAT+ ==="
check_hardware "Fusion HAT sysfs entries" \
    "ls /sys/class/fusion_hat/fusion_hat 2>/dev/null" \
    "Ensure HAT is properly seated on GPIO header"

# Skip i2cdetect as it can hang - check sysfs instead
check_hardware "Fusion HAT I2C driver loaded" \
    "ls /sys/class/fusion_hat/ 2>/dev/null" \
    "Check HAT connection: ls /sys/class/fusion_hat/"

check_hardware "Fusion HAT PWM channels" \
    "ls /sys/class/fusion_hat/fusion_hat/pwm/pwm0 2>/dev/null" \
    "Check HAT driver loaded: lsmod | grep fusion"

echo ""
echo "=== Laser Rangefinder ==="
check_optional "M01 Laser UART (ttyAMA10)" \
    "ls /dev/ttyAMA10 2>/dev/null"

echo ""
echo "=== Servos ==="
check_optional "Servo PWM channels" \
    "ls /sys/class/fusion_hat/fusion_hat/pwm/ 2>/dev/null | grep -q pwm"

echo ""
echo "=== System Configuration ==="
check_hardware "Real-time kernel (PREEMPT_RT)" \
    "uname -v | grep -qi PREEMPT_RT" \
    "Install RT kernel: sudo rpi-update (if available)"

check_hardware "CPU governor (performance)" \
    "cat /sys/devices/system/cpu/cpufreq/policy*/scaling_governor 2>/dev/null | grep -q performance" \
    "Set governor: echo performance | sudo tee /sys/devices/system/cpu/cpufreq/policy*/scaling_governor"

echo ""
echo "=== CPU Isolation ==="
if grep -q "isolcpus=2-3" /boot/firmware/cmdline.txt 2>/dev/null || \
   grep -q "isolcpus=2-3" /boot/cmdline.txt 2>/dev/null; then
    echo -e "${GREEN}[PASS]${NC} CPUs 2-3 isolated for real-time threads"
    ((PASS_COUNT++))
else
    echo -e "${YELLOW}[WARN]${NC} CPUs 2-3 not isolated (recommended for RT)"
    echo "       Fix: Add 'isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3' to /boot/firmware/cmdline.txt"
    ((WARN_COUNT++))
fi

echo ""
echo "========================================"
echo "Summary"
echo "========================================"
echo -e "${GREEN}Passed:${NC}   $PASS_COUNT"
echo -e "${RED}Failed:${NC}   $FAIL_COUNT"
echo -e "${YELLOW}Warnings:${NC} $WARN_COUNT"
echo ""

if [ $FAIL_COUNT -gt 0 ]; then
    echo -e "${RED}HARDWARE CHECK FAILED${NC}"
    echo "Please connect missing hardware and re-run this script."
    echo ""
    echo "After connecting hardware, run tests with:"
    echo "  cd build-release && sudo ctest --output-on-failure"
    exit 1
else
    echo -e "${GREEN}ALL HARDWARE PRESENT${NC}"
    echo "Ready to run hardware tests."
    exit 0
fi
