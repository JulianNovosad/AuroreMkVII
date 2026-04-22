#!/bin/bash
# reset-usb-camera.sh — recover a stuck GEMBIRD AX2311 USB webcam
#
# Symptoms fixed:
#   "uvcvideo: Failed to set UVC probe control : -71"
#   "usb: can't set config #1, error -71"
#
# Run as root (or with sudo). Safe to run while aurore-link is running.

set -euo pipefail

VID="1908"
PID="2311"

echo "[reset-usb-camera] Looking for ${VID}:${PID}..."

# Find the sysfs path for this device
SYSFS_DEV=""
for dev in /sys/bus/usb/devices/[0-9]*; do
    [ -f "$dev/idVendor" ] || continue
    v=$(cat "$dev/idVendor" 2>/dev/null)
    p=$(cat "$dev/idProduct" 2>/dev/null)
    if [[ "$v" == "$VID" && "$p" == "$PID" ]]; then
        SYSFS_DEV="$dev"
        break
    fi
done

if [[ -z "$SYSFS_DEV" ]]; then
    echo "[reset-usb-camera] Device ${VID}:${PID} not found on USB bus."
    echo "  -> Physically unplug and replug the camera, then run this script again."
    exit 1
fi

BUS_PORT=$(basename "$SYSFS_DEV")
echo "[reset-usb-camera] Found at $SYSFS_DEV (bus-port: $BUS_PORT)"

# Check current port state
STATE=$(cat "$SYSFS_DEV/port/state" 2>/dev/null || echo "unknown")
echo "[reset-usb-camera] Port state: $STATE"

if [[ "$STATE" == "configured" ]]; then
    echo "[reset-usb-camera] Camera is already configured — checking for video node..."
    # Check if video node exists
    for dev in /sys/class/video4linux/video*/device/driver; do
        link=$(readlink "$dev" 2>/dev/null || true)
        if [[ "$link" == *uvcvideo* ]]; then
            vnode=$(echo "$dev" | sed 's|/device/driver||;s|.*/||')
            echo "[reset-usb-camera] UVC video node: /dev/$vnode — camera OK"
            exit 0
        fi
    done
    echo "[reset-usb-camera] Configured but no video node — forcing usbreset..."
fi

# Hardware reset via usbreset (sends USB_REQ_RESET to the port)
DEV_PATH=$(readlink -f "$SYSFS_DEV/dev" 2>/dev/null | head -1)
BUSNUM=$(cat "$SYSFS_DEV/busnum")
DEVNUM=$(cat "$SYSFS_DEV/devnum")
USB_DEV_PATH="/dev/bus/usb/$(printf '%03d' "$BUSNUM")/$(printf '%03d' "$DEVNUM")"

echo "[reset-usb-camera] Sending USB hardware reset to $USB_DEV_PATH..."
if usbreset "$USB_DEV_PATH" 2>/dev/null; then
    echo "[reset-usb-camera] USB reset sent OK"
else
    echo "[reset-usb-camera] usbreset failed — trying unbind/bind..."
    echo "$BUS_PORT" | tee /sys/bus/usb/drivers/usb/unbind > /dev/null
    sleep 1
    echo "$BUS_PORT" | tee /sys/bus/usb/drivers/usb/bind > /dev/null
fi

sleep 3

# Verify
for dev in /sys/class/video4linux/video*/device/driver; do
    link=$(readlink "$dev" 2>/dev/null || true)
    if [[ "$link" == *uvcvideo* ]]; then
        vnode=$(echo "$dev" | sed 's|/device/driver||;s|.*/||')
        echo "[reset-usb-camera] SUCCESS: UVC camera at /dev/$vnode"
        exit 0
    fi
done

echo "[reset-usb-camera] FAILED: camera still not enumerated."
echo "  -> Try physically unplugging and replugging the camera."
exit 1
