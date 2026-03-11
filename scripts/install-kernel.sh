#!/bin/bash
# scripts/install-kernel.sh - Install custom PREEMPT_RT kernel for AuroreMkVII
# 
# This script installs the built kernel, modules, and DTBs to /boot/firmware/
# and configures boot parameters for real-time operation.
#
# Usage: sudo ./scripts/install-kernel.sh

set -e

KERNEL_SRC="/home/pi/linux-rt-kernel"
BOOT_DIR="/boot/firmware"
KERNEL_NAME="kernel_2712_rt.img"
BACKUP_SUFFIX=".bak.$(date +%Y%m%d_%H%M%S)"

echo "=== AuroreMkVII PREEMPT_RT Kernel Installation ==="
echo ""

# Verify build artifacts exist
echo "[1/6] Verifying build artifacts..."
if [ ! -f "$KERNEL_SRC/arch/arm64/boot/Image.gz" ]; then
    echo "ERROR: Kernel image not found. Build may not be complete."
    echo "  Expected: $KERNEL_SRC/arch/arm64/boot/Image.gz"
    exit 1
fi

if [ ! -d "$KERNEL_SRC/arch/arm64/boot/dts/broadcom" ]; then
    echo "ERROR: DTBs not found. Build may not be complete."
    exit 1
fi

echo "  Kernel image: OK"
echo "  DTBs: OK"

# Backup current kernel
echo "[2/6] Backing up current kernel..."
if [ -f "$BOOT_DIR/$KERNEL_NAME" ]; then
    cp "$BOOT_DIR/$KERNEL_NAME" "$BOOT_DIR/${KERNEL_NAME}${BACKUP_SUFFIX}"
    echo "  Backed up existing $KERNEL_NAME"
fi

# Install kernel image
echo "[3/6] Installing kernel image..."
cp "$KERNEL_SRC/arch/arm64/boot/Image.gz" "$BOOT_DIR/$KERNEL_NAME"
echo "  Installed $KERNEL_NAME to $BOOT_DIR/"

# Install modules
echo "[4/6] Installing kernel modules..."
cd "$KERNEL_SRC"
make modules_install INSTALL_MOD_PATH=/
echo "  Modules installed to /lib/modules/"

# Install DTBs
echo "[5/6] Installing device tree blobs..."
make dtbs_install INSTALL_DTBS_PATH="$BOOT_DIR"
echo "  DTBs installed to $BOOT_DIR/"

# Update config.txt
echo "[6/6] Updating boot configuration..."
CONFIG_FILE="$BOOT_DIR/config.txt"

# Remove any existing kernel= line for our RT kernel
if grep -q "^kernel=$KERNEL_NAME" "$CONFIG_FILE" 2>/dev/null; then
    sed -i "/^kernel=$KERNEL_NAME/d" "$CONFIG_FILE"
fi

# Add kernel line at end of config.txt
echo "kernel=$KERNEL_NAME" >> "$CONFIG_FILE"
echo "  Added 'kernel=$KERNEL_NAME' to $CONFIG_FILE"

echo ""
echo "=== Installation Complete ==="
echo ""
echo "Next steps:"
echo "1. Update cmdline.txt for CPU isolation (run configure-boot.sh)"
echo "2. Reboot: sudo reboot"
echo "3. Verify: uname -r && grep PREEMPT_RT /proc/config.gz"
echo ""
echo "To revert to original kernel:"
echo "  Edit $CONFIG_FILE and remove or comment the kernel=$KERNEL_NAME line"
