#!/bin/bash
set -euo pipefail

# Aurore Mk VI - Preflight Check
# Verifies environment, dependencies, and resources.

check_temp() {
    if [ -f /sys/class/thermal/thermal_zone0/temp ]; then
        TEMP=$(cat /sys/class/thermal/thermal_zone0/temp)
        TEMP_C=$((TEMP / 1000))
        # Default threshold 75C, can be overridden by env
        THRESHOLD=${THERMAL_THRESHOLD:-75}
        
        if [ "$TEMP_C" -ge "$THRESHOLD" ]; then
            echo "ERROR: Thermal throttling imminent. Temp: ${TEMP_C}C (Threshold: ${THRESHOLD}C)"
            exit 1
        fi
        echo "Thermal check passed: ${TEMP_C}C"
    else
        echo "WARNING: Cannot read thermal zone. Skipping check."
    fi
}

check_disk() {
    FREE_SPACE=$(df -k . | awk 'NR==2 {print $4}')
    # 20GB in KB
    REQUIRED=20971520
    
    if [ "$FREE_SPACE" -lt "$REQUIRED" ]; then
        echo "ERROR: Insufficient disk space. Available: $((FREE_SPACE/1024))MB, Required: 20000MB"
        exit 1
    fi
    echo "Disk space check passed."
}

check_deps() {
    local DEPS=("git" "cmake" "make" "gcc" "g++" "python3" "clang-tidy" "cppcheck" "fakeroot" "dpkg-query")
    local MISSING=()
    
    for dep in "${DEPS[@]}"; do
        if ! command -v "$dep" &> /dev/null; then
            MISSING+=("$dep")
        fi
    done
    
    # Check for library headers (mock check if pkg-config not available, strictly we need to check paths or try compile)
    # For now, we rely on apt package presence if possible, or simple check
    
    if [ ${#MISSING[@]} -ne 0 ]; then
        echo "ERROR: Missing dependencies: ${MISSING[*]}"
        echo "Run: sudo apt update && sudo apt install -y ${MISSING[*]} libssl-dev libprotobuf-dev"
        exit 1
    fi
    echo "Dependencies check passed."
}

# Parse args
SKIP_UPGRADE=0
FULL_CHECK=0
CHECK_TEMP_ONLY=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-upgrade)
            SKIP_UPGRADE=1
            shift
            ;;
        --full)
            FULL_CHECK=1
            shift
            ;;
        --check-temp)
            CHECK_TEMP_ONLY=1
            shift
            ;;
        *)
            echo "Unknown argument: $1"
            exit 1
            ;;
    esac
done

if [ "$CHECK_TEMP_ONLY" -eq 1 ]; then
    check_temp
    exit 0
fi

if [ "$FULL_CHECK" -eq 1 ]; then
    # OS Upgrade Step
    if [ "$SKIP_UPGRADE" -eq 0 ]; then
        echo "Performing OS upgrade..."
        sudo apt update && sudo apt upgrade -y
    else
        echo "Skipping OS upgrade per user request."
    fi
    
    # Capture Kernel Version
    mkdir -p artifacts/kernel
    KERNEL_PKG_VER=$(dpkg-query -W -f='${Package} ${Version}\n' raspberrypi-kernel 2>/dev/null || echo "unknown")
    echo "$KERNEL_PKG_VER" > artifacts/kernel/kernel_package_version.txt
    echo "Kernel package version recorded: $KERNEL_PKG_VER"
    
    # Capture Manifest (preliminary)
    mkdir -p deps
    echo "OS_KERNEL_PKG: $KERNEL_PKG_VER" > deps/manifest.txt
    
    check_disk
    check_deps
    check_temp
    
    # Submodule check
    if [ -f .gitmodules ]; then
         git submodule status || echo "WARNING: Submodules not initialized."
    fi
fi

echo "Preflight complete."
