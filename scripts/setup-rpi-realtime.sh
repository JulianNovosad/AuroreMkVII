#!/bin/bash
# scripts/setup-rpi-realtime.sh
# Configure Raspberry Pi 5 for real-time operation
#
# This script configures:
# - CPU isolation for CPUs 2-3
# - CPU governor to performance
# - SCHED_FIFO permissions
# - Memory lock limits
#
# Usage: sudo ./scripts/setup-rpi-realtime.sh
# Reboot required after running.

set -e

echo "=== Raspberry Pi 5 Real-Time Configuration ==="
echo ""

# Detect Raspberry Pi
if ! grep -q "Raspberry Pi" /proc/device-tree/model 2>/dev/null; then
    echo "WARNING: This script is designed for Raspberry Pi."
    echo "         Some configurations may not apply to your system."
    echo ""
    read -p "Continue anyway? [y/N] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Configure CPU isolation
echo "[1/5] Configuring CPU isolation (isolcpus=2-3)..."
CMDLINE_FILE="/boot/firmware/cmdline.txt"
if [ ! -f "$CMDLINE_FILE" ]; then
    CMDLINE_FILE="/boot/cmdline.txt"
fi

if [ -f "$CMDLINE_FILE" ]; then
    cp /boot/firmware/cmdline.txt /boot/firmware/cmdline.txt.bak
    echo "Backed up cmdline.txt to cmdline.txt.bak"
    # Remove any existing isolcpus settings
    sed -i 's/isolcpus=[0-9,-]*//g' "$CMDLINE_FILE"
    sed -i 's/nohz_full=[0-9,-]*//g' "$CMDLINE_FILE"
    sed -i 's/rcu_nocbs=[0-9,-]*//g' "$CMDLINE_FILE"
    sed -i 's/irqaffinity=[0-9,-]*//g' "$CMDLINE_FILE"
    
    # Add new settings at the beginning of the file
    sed -i '1s/^/isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1 /' "$CMDLINE_FILE"
    echo "  Added to $CMDLINE_FILE:"
    echo "  isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1"
else
    echo "  WARNING: Could not find cmdline.txt"
    echo "  Manual configuration required."
fi

# Configure CPU governor
echo "[2/5] Configuring CPU governor (performance mode)..."
GOVERNOR_FILE="/etc/rc.local"
if [ -f "$GOVERNOR_FILE" ]; then
    # Remove existing cpufreq settings
    sed -i '/cpufreq-set/d' "$GOVERNOR_FILE"
fi

# Add cpufreq settings to rc.local
cat >> "$GOVERNOR_FILE" << 'EOF'

# Set CPU governor to performance for real-time operation
for cpu in 0 1 2 3; do
    cpufreq-set -c $cpu -g performance 2>/dev/null || true
done
EOF
echo "  Added CPU governor settings to $GOVERNOR_FILE"

# Install cpufrequtils
echo "[3/5] Installing cpufrequtils..."
apt install -y cpufrequtils

# Configure SCHED_FIFO permissions
echo "[4/5] Configuring SCHED_FIFO permissions..."
LIMITS_FILE="/etc/security/limits.d/99-realtime.conf"
cat > "$LIMITS_FILE" << 'EOF'
# Real-time permissions for AuroreMkVII
@realtime - rtprio 99
@realtime - memlock unlimited
EOF
echo "  Created $LIMITS_FILE"

# Add current user to realtime group
echo "  Adding user '$(whoami)' to realtime group..."
usermod -a -G realtime $(whoami) 2>/dev/null || {
    # Group doesn't exist, create it
    groupadd realtime
    usermod -a -G realtime $(whoami)
}

# Configure memlock limits
echo "[5/5] Configuring memory lock limits..."
SYSCTL_FILE="/etc/sysctl.d/99-realtime.conf"
cat > "$SYSCTL_FILE" << 'EOF'
# Real-time memory lock limits
vm.max_map_count = 262144
EOF
echo "  Created $SYSCTL_FILE"

echo ""
echo "=== Configuration Complete ==="
echo ""
echo "IMPORTANT: A reboot is required for CPU isolation to take effect."
echo ""
echo "After reboot, verify configuration:"
echo "  cat /proc/cmdline | grep isolcpus"
echo "  cat /etc/security/limits.d/99-realtime.conf"
echo "  groups  # Should include 'realtime'"
echo ""
echo "To run AuroreMkVII with real-time priorities:"
echo "  sudo ./aurore  # Required for SCHED_FIFO and mlockall"
echo ""
echo "Reboot now? [y/N] "
read -r
if [[ $REPLY =~ ^[Yy]$ ]]; then
    reboot
fi
