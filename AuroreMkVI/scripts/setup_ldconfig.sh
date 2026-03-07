#!/bin/bash
# Configure system library paths for Aurore dependencies
# Run this once after building or after adding new shared libraries

set -euo pipefail

echo "=== Configuring system library paths for Aurore ==="

# Create ld.so.conf.d include file
CONF_FILE="/etc/ld.so.conf.d/aurore.conf"
AURORE_LIBS=(
    "/home/pi/Aurore/artifacts/xnnpack/lib"
    "/home/pi/Aurore/artifacts/tflite/lib"
    "/home/pi/Aurore/artifacts/abseil/lib"
    "/home/pi/Aurore/artifacts/libedgetpu/lib"
    "/home/pi/Aurore/artifacts/fft2d/lib"
    "/home/pi/Aurore/artifacts/ffmpeg/lib"
)

# Remove existing config if it exists
sudo rm -f "$CONF_FILE"

# Add library paths
for lib_path in "${AURORE_LIBS[@]}"; do
    if [ -d "$lib_path" ]; then
        echo "Adding: $lib_path"
        echo "$lib_path" | sudo tee -a "$CONF_FILE" > /dev/null
    fi
done

# Update library cache
echo ""
echo "Running ldconfig..."
sudo ldconfig

# Verify key libraries
echo ""
echo "=== Verifying libraries ==="
for lib in libkleidiai.so libtensorflow-lite.so libabsl_*.so libedgetpu.so; do
    if ldconfig -p | grep -q "$lib"; then
        echo "✓ $lib found"
    else
        echo "✗ $lib not found"
    fi
done

echo ""
echo "Done. You can now run: sudo ./build/AuroreMkVI"
