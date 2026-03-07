#!/bin/bash
# reload_tpu_modules.sh - Reload gasket/apex kernel modules for Coral Edge TPU
#
# Usage: sudo ./reload_tpu_modules.sh
#
# This script attempts to recover a stuck TPU by:
# 1. Unbinding the device from the apex driver
# 2. Reloading the kernel modules
# 3. Rebinding the device

set -e

echo "=== TPU Module Reload Script ==="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: This script must be run as root (sudo)"
    exit 1
fi

# Check current module state
echo "Current module state:"
lsmod | grep -E "apex|gasket" || echo "No modules loaded"

# Get device BDF
DEVICE="0001:01:00.0"
if ! lspci -s "$DEVICE" &>/dev/null; then
    echo "ERROR: TPU device ($DEVICE) not found"
    exit 1
fi

echo ""
echo "Step 1: Unbinding device from apex driver..."
echo "$DEVICE" > /sys/bus/pci/devices/$DEVICE/driver_override 2>/dev/null || true
echo "$DEVICE" > /sys/bus/pci/drivers/apex/unbind 2>/dev/null || echo "  Device not bound to apex (may already be unbound)"

# Small delay
sleep 1

echo ""
echo "Step 2: Unloading kernel modules..."
# Unload in reverse dependency order
if lsmod | grep -q "^apex "; then
    if ! modprobe -r apex 2>/dev/null; then
        echo "  Warning: Could not unload apex module (may be in use)"
    else
        echo "  apex module unloaded"
    fi
fi

if lsmod | grep -q "^gasket "; then
    if ! modprobe -r gasket 2>/dev/null; then
        echo "  Warning: Could not unload gasket module (may be in use)"
    else
        echo "  gasket module unloaded"
    fi
fi

# Wait for cleanup
sleep 2

echo ""
echo "Step 3: Reloading kernel modules..."
modprobe gasket && echo "  gasket module loaded"
modprobe apex && echo "  apex module loaded"

# Wait for device creation
sleep 2

echo ""
echo "Step 4: Checking device status..."
if [ -e "/dev/apex_0" ]; then
    echo "  /dev/apex_0 exists"
    ls -la /dev/apex*
else
    echo "  WARNING: /dev/apex_0 not found"
fi

echo ""
echo "Module state after reload:"
lsmod | grep -E "apex|gasket"

echo ""
echo "Done. Try running the application again."
echo ""
echo "If this doesn't help, the TPU may need a full power cycle."
