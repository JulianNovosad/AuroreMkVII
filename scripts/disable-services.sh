#!/bin/bash
#
# disable-services.sh - Disable unnecessary services per AM7-L2-OS-003
#
# Masks and disables 11 non-essential services to reduce system load
# and improve real-time performance on Raspberry Pi 5 target.
#
# Services disabled:
#   - bluetooth.service
#   - ModemManager.service
#   - wpa_supplicant.service (if Ethernet only)
#   - avahi-daemon.service
#   - triggerhappy.service
#   - pigpio.service
#   - nodered.service
#   - apt-daily.service
#   - apt-daily-upgrade.service
#   - phd54875.service
#   - systemd-networkd-wait-online.service
#
# Usage: sudo ./disable-services.sh
#

set -euo pipefail

# Color output helpers
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

LOG_FILE="/var/log/aurore/disable-services.log"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

# Ensure log directory exists
mkdir -p "$(dirname "$LOG_FILE")"

log() {
    local msg="[$TIMESTAMP] $1"
    echo -e "$msg"
    echo "$msg" >> "$LOG_FILE"
}

log_success() {
    log "${GREEN}✓ $1${NC}"
}

log_warning() {
    log "${YELLOW}⚠ $1${NC}"
}

log_error() {
    log "${RED}✗ $1${NC}"
}

# Check if running as root
if [[ $EUID -ne 0 ]]; then
    log_error "This script must be run as root (sudo)"
    exit 1
fi

log "Starting service disablement (AM7-L2-OS-003 compliance)"
log "======================================================="

# List of services to disable
SERVICES=(
    "bluetooth.service"
    "ModemManager.service"
    "wpa_supplicant.service"
    "avahi-daemon.service"
    "triggerhappy.service"
    "pigpio.service"
    "nodered.service"
    "apt-daily.service"
    "apt-daily-upgrade.service"
    "phd54875.service"
    "systemd-networkd-wait-online.service"
)

# Track changes
CHANGED=0
FAILED=0
SKIPPED=0

# Function to disable a service (idempotent)
disable_service() {
    local service="$1"
    
    # Check if service exists
    if ! systemctl list-unit-files --type=service | grep -q "^${service}"; then
        log_warning "Service ${service} not found - skipping"
        ((SKIPPED++))
        return 0
    fi
    
    # Check current status
    local is_active=false
    local is_enabled=false
    local is_masked=false
    
    if systemctl is-active --quiet "$service" 2>/dev/null; then
        is_active=true
    fi
    
    if systemctl is-enabled --quiet "$service" 2>/dev/null; then
        is_enabled=true
    fi
    
    if systemctl is-masked --quiet "$service" 2>/dev/null; then
        is_masked=true
    fi
    
    # If already masked, skip (idempotent)
    if [[ "$is_masked" == "true" ]]; then
        log "Service ${service}: already masked - no action needed"
        ((SKIPPED++))
        return 0
    fi
    
    # Stop if active
    if [[ "$is_active" == "true" ]]; then
        log "Stopping ${service}..."
        if systemctl stop "$service"; then
            log_success "Stopped ${service}"
        else
            log_error "Failed to stop ${service}"
            ((FAILED++))
            return 1
        fi
    fi
    
    # Mask the service (prevents manual/auto start)
    log "Masking ${service}..."
    if systemctl mask "$service"; then
        log_success "Masked ${service}"
        ((CHANGED++))
    else
        log_error "Failed to mask ${service}"
        ((FAILED++))
        return 1
    fi
    
    return 0
}

# Disable each service
log ""
log "Disabling services..."
log ""

for service in "${SERVICES[@]}"; do
    disable_service "$service" || true
done

# Special handling for wpa_supplicant - warn if not Ethernet-only
log ""
log_warning "Note: wpa_supplicant.service has been disabled."
log_warning "If WiFi connectivity is needed, re-enable with:"
log_warning "  sudo systemctl unmask wpa_supplicant.service"
log_warning "  sudo systemctl enable wpa_supplicant.service"

# Verification phase
log ""
log "Verifying service status..."
log "============================"

VERIFIED_OK=0
VERIFIED_FAILED=0

for service in "${SERVICES[@]}"; do
    if systemctl list-unit-files --type=service | grep -q "^${service}"; then
        if systemctl is-masked --quiet "$service" 2>/dev/null; then
            log_success "${service}: masked (verified)"
            ((VERIFIED_OK++))
        else
            log_error "${service}: NOT masked (verification failed)"
            ((VERIFIED_FAILED++))
        fi
    else
        log "${service}: not installed (skipped verification)"
    fi
done

# Summary
log ""
log "======================================================="
log "Summary:"
log "  - Services masked: ${CHANGED}"
log "  - Already masked/not found: ${SKIPPED}"
log "  - Failed: ${FAILED}"
log "  - Verified OK: ${VERIFIED_OK}"
log "  - Verification failed: ${VERIFIED_FAILED}"
log ""

if [[ $FAILED -eq 0 && $VERIFIED_FAILED -eq 0 ]]; then
    log_success "All services disabled successfully!"
    log "Log file: ${LOG_FILE}"
    exit 0
else
    log_error "Some services failed to disable or verify."
    log "Review log file: ${LOG_FILE}"
    exit 1
fi
