#!/bin/bash
# deploy-to-rpi.sh - Deploy Aurore MkVII binaries to Raspberry Pi 5
#
# Usage: ./scripts/deploy-to-rpi.sh [user@hostname]
#
# Default: pi@aurorpi.local (or pi@192.168.1.XX)
#
# Prerequisites:
#   - SSH access to Raspberry Pi 5
#   - SSH key-based authentication recommended
#   - Cross-compiled binaries from build-rpi.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build-rpi"

# Default target
RPI_USER="${RPI_USER:-pi}"
RPI_HOST="${RPI_HOST:-aurorpi.local}"
RPI_TARGET="${1:-${RPI_USER}@${RPI_HOST}}"
RPI_APP_DIR="${RPI_APP_DIR:-/home/${RPI_USER}/aurore}"

echo "=== Aurore MkVII Deploy to Raspberry Pi 5 ==="
echo "Target: $RPI_TARGET"
echo "Remote directory: $RPI_APP_DIR"
echo ""

# Check build directory
if [[ ! -d "$BUILD_DIR" ]]; then
    echo "ERROR: Build directory not found: $BUILD_DIR"
    echo ""
    echo "Run ./scripts/build-rpi.sh first to cross-compile."
    exit 1
fi

# Check for binaries
if [[ ! -f "$BUILD_DIR/aurore" ]]; then
    echo "ERROR: Main executable not found in $BUILD_DIR"
    exit 1
fi

# Create remote directory
echo "Creating remote directory..."
ssh -o StrictHostKeyChecking=accept-new "$RPI_TARGET" "mkdir -p $RPI_APP_DIR"

# Deploy binaries
echo "Deploying binaries..."
scp -o StrictHostKeyChecking=accept-new "$BUILD_DIR/aurore" "$RPI_TARGET:$RPI_APP_DIR/"
scp -o StrictHostKeyChecking=accept-new "$BUILD_DIR/config/"*.json* "$RPI_TARGET:$RPI_APP_DIR/" 2>/dev/null || true

# Deploy scripts
echo "Deploying helper scripts..."
ssh -o StrictHostKeyChecking=accept-new "$RPI_TARGET" "mkdir -p $RPI_APP_DIR/scripts"
scp -o StrictHostKeyChecking=accept-new "$SCRIPT_DIR/jitter_monitor.sh" "$RPI_TARGET:$RPI_APP_DIR/scripts/" 2>/dev/null || true
scp -o StrictHostKeyChecking=accept-new "$SCRIPT_DIR/wcet_analysis.sh" "$RPI_TARGET:$RPI_APP_DIR/scripts/" 2>/dev/null || true

# Set permissions
echo "Setting permissions..."
ssh -o StrictHostKeyChecking=accept-new "$RPI_TARGET" "chmod +x $RPI_APP_DIR/aurore"
ssh -o StrictHostKeyChecking=accept-new "$RPI_TARGET" "chmod +x $RPI_APP_DIR/scripts/"*.sh 2>/dev/null || true

# Verify deployment
echo ""
echo "=== Verifying Deployment ==="
ssh -o StrictHostKeyChecking=accept-new "$RPI_TARGET" "ls -la $RPI_APP_DIR"
echo ""
ssh -o StrictHostKeyChecking=accept-new "$RPI_TARGET" "file $RPI_APP_DIR/aurore"
echo ""

# Instructions
echo "=== Deployment Complete ==="
echo ""
echo "SSH into Raspberry Pi 5:"
echo "  ssh $RPI_TARGET"
echo ""
echo "Run the application:"
echo "  cd $RPI_APP_DIR"
echo "  sudo ./aurore --help"
echo ""
echo "For real-time operation, configure your RPi 5:"
echo "  1. Install PREEMPT_RT kernel (if not already)"
echo "  2. Add to /boot/firmware/cmdline.txt:"
echo "     isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1"
echo "  3. Set CPU governor to performance:"
echo "     echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"
echo "  4. Reboot and run with sudo for SCHED_FIFO"
echo ""
