#!/bin/bash
# scripts/configure-boot.sh - Configure boot parameters for real-time operation
#
# This script configures:
# - CPU isolation for CPUs 2-3
# - IRQ affinity to CPUs 0-1
# - RCU callbacks off isolated CPUs
#
# Usage: sudo ./scripts/configure-boot.sh

set -e

BOOT_DIR="/boot/firmware"
CMDLINE_FILE="$BOOT_DIR/cmdline.txt"

echo "=== AuroreMkVII Real-Time Boot Configuration ==="
echo ""

# Backup current cmdline.txt
echo "[1/3] Backing up current cmdline.txt..."
cp "$CMDLINE_FILE" "${CMDLINE_FILE}.bak.$(date +%Y%m%d_%H%M%S)"
echo "  Backed up to ${CMDLINE_FILE}.bak.*"

# Remove any existing real-time parameters
echo "[2/3] Removing existing real-time parameters..."
sed -i 's/isolcpus=[0-9,-]*//g' "$CMDLINE_FILE"
sed -i 's/nohz_full=[0-9,-]*//g' "$CMDLINE_FILE"
sed -i 's/rcu_nocbs=[0-9,-]*//g' "$CMDLINE_FILE"
sed -i 's/irqaffinity=[0-9,-]*//g' "$CMDLINE_FILE"
# Clean up extra spaces
sed -i 's/  */ /g' "$CMDLINE_FILE"
echo "  Cleaned existing parameters"

# Add real-time parameters at the beginning
echo "[3/3] Adding real-time boot parameters..."
sed -i '1s/^/isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1 /' "$CMDLINE_FILE"
echo "  Added: isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1"

echo ""
echo "=== Boot Configuration Complete ==="
echo ""
echo "Current cmdline.txt contents:"
cat "$CMDLINE_FILE"
echo ""
echo ""
echo "IMPORTANT: A reboot is required for these changes to take effect."
echo ""
echo "After reboot, verify configuration:"
echo "  cat /proc/cmdline | grep isolcpus"
echo "  uname -r  # Should show custom kernel"
echo "  grep PREEMPT_RT /proc/config.gz  # Should show CONFIG_PREEMPT_RT=y"
echo ""
echo "Reboot now? [y/N] "
read -r
if [[ $REPLY =~ ^[Yy]$ ]]; then
    sudo reboot
fi
