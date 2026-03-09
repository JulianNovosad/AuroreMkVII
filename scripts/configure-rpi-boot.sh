#!/bin/bash
# =============================================================================
# configure-rpi-boot.sh - Raspberry Pi Boot Configuration Script
# =============================================================================
# Purpose: Apply HDMI and Bluetooth disablement to /boot/firmware/config.txt
# Target:  Raspberry Pi 5 running Aurore MkVII
#
# Usage:
#   sudo ./scripts/configure-rpi-boot.sh [--dry-run] [--restore]
#
# Options:
#   --dry-run   Show changes without applying
#   --restore   Restore from backup if available
#
# Requirements:
#   - Must be run as root (sudo)
#   - Backup of original config.txt is created automatically
# =============================================================================

set -euo pipefail

# =============================================================================
# Configuration
# =============================================================================
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
readonly CONFIG_SOURCE="$PROJECT_ROOT/config/config.txt.rpi"
readonly CONFIG_TARGET="/boot/firmware/config.txt"
readonly CONFIG_BACKUP="/boot/firmware/config.txt.backup.$(date +%Y%m%d_%H%M%S)"
readonly LOG_PREFIX="[AuroreMkVII-config]"

# =============================================================================
# Colors for output
# =============================================================================
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# =============================================================================
# Logging functions
# =============================================================================
log_info() {
    echo -e "${BLUE}${LOG_PREFIX} [INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}${LOG_PREFIX} [OK]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}${LOG_PREFIX} [WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}${LOG_PREFIX} [ERROR]${NC} $1" >&2
}

# =============================================================================
# Helper functions
# =============================================================================
check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

check_source_exists() {
    if [[ ! -f "$CONFIG_SOURCE" ]]; then
        log_error "Source config file not found: $CONFIG_SOURCE"
        exit 1
    fi
    log_info "Source config: $CONFIG_SOURCE"
}

backup_config() {
    if [[ -f "$CONFIG_TARGET" ]]; then
        log_info "Creating backup: $CONFIG_BACKUP"
        cp "$CONFIG_TARGET" "$CONFIG_BACKUP"
        log_success "Backup created successfully"
    else
        log_warning "No existing config.txt found at $CONFIG_TARGET"
    fi
}

# =============================================================================
# Idempotent configuration application
# =============================================================================
apply_config_idempotent() {
    local dry_run="${1:-false}"
    
    log_info "Applying configuration (idempotent)..."
    
    # Create a temporary file for the new configuration
    local temp_config
    temp_config=$(mktemp)
    
    # Copy source config to temp file
    cp "$CONFIG_SOURCE" "$temp_config"
    
    # Check if target config exists and merge existing customizations
    if [[ -f "$CONFIG_TARGET" ]]; then
        log_info "Existing config.txt found - checking for customizations..."
        
        # Extract any lines from existing config that are NOT in our reference
        # and are not commented out
        local existing_custom=""
        while IFS= read -r line; do
            # Skip empty lines and comments
            [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
            
            # Check if this line exists in our reference config
            if ! grep -qxF "$line" "$CONFIG_SOURCE"; then
                existing_custom+="$line"$'\n'
            fi
        done < "$CONFIG_TARGET"
        
        if [[ -n "$existing_custom" ]]; then
            log_info "Preserving existing customizations..."
            echo "" >> "$temp_config"
            echo "# =============================================================================" >> "$temp_config"
            echo "# Preserved customizations from original config.txt" >> "$temp_config"
            echo "# =============================================================================" >> "$temp_config"
            echo -n "$existing_custom" >> "$temp_config"
        fi
    fi
    
    if [[ "$dry_run" == "true" ]]; then
        log_info "[DRY-RUN] Would apply the following configuration:"
        echo "---"
        cat "$temp_config"
        echo "---"
        rm -f "$temp_config"
        return 0
    fi
    
    # Apply the configuration
    log_info "Installing new config.txt..."
    cp "$temp_config" "$CONFIG_TARGET"
    rm -f "$temp_config"
    
    log_success "Configuration applied successfully"
}

# =============================================================================
# Verification functions
# =============================================================================
verify_config() {
    log_info "Verifying configuration..."
    
    local errors=0
    
    # Check HDMI blanking
    if grep -q "^hdmi_blanking=1" "$CONFIG_TARGET"; then
        log_success "HDMI blanking enabled (hdmi_blanking=1)"
    else
        log_error "HDMI blanking not configured"
        ((errors++))
    fi
    
    # Check HDMI force hotplug
    if grep -q "^hdmi_force_hotplug=0" "$CONFIG_TARGET"; then
        log_success "HDMI hotplug disabled (hdmi_force_hotplug=0)"
    else
        log_error "HDMI hotplug not configured"
        ((errors++))
    fi
    
    # Check Bluetooth disable
    if grep -q "^dtoverlay=disable-bt" "$CONFIG_TARGET"; then
        log_success "Bluetooth disabled (dtoverlay=disable-bt)"
    else
        log_error "Bluetooth disable overlay not configured"
        ((errors++))
    fi
    
    return $errors
}

# =============================================================================
# Restore function
# =============================================================================
restore_from_backup() {
    local backup_file
    backup_file=$(ls -t /boot/firmware/config.txt.backup.* 2>/dev/null | head -n1)
    
    if [[ -z "$backup_file" ]]; then
        log_error "No backup files found in /boot/firmware/"
        exit 1
    fi
    
    log_info "Restoring from backup: $backup_file"
    cp "$backup_file" "$CONFIG_TARGET"
    log_success "Configuration restored from backup"
    log_info "Reboot required for changes to take effect"
}

# =============================================================================
# Main
# =============================================================================
main() {
    local dry_run="false"
    local restore="false"
    
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --dry-run)
                dry_run="true"
                shift
                ;;
            --restore)
                restore="true"
                shift
                ;;
            -h|--help)
                echo "Usage: $0 [--dry-run] [--restore]"
                echo ""
                echo "Options:"
                echo "  --dry-run   Show changes without applying"
                echo "  --restore   Restore from most recent backup"
                echo "  -h, --help  Show this help message"
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                exit 1
                ;;
        esac
    done
    
    echo -e "${BLUE}=============================================================================${NC}"
    echo -e "${BLUE}  Aurore MkVII - Raspberry Pi Boot Configuration${NC}"
    echo -e "${BLUE}=============================================================================${NC}"
    echo ""
    
    if [[ "$restore" == "true" ]]; then
        check_root
        restore_from_backup
        exit 0
    fi
    
    check_root
    check_source_exists
    
    log_info "This script will:"
    echo "  1. Backup existing config.txt (if present)"
    echo "  2. Apply HDMI disablement (hdmi_blanking=1, hdmi_force_hotplug=0)"
    echo "  3. Apply Bluetooth disablement (dtoverlay=disable-bt)"
    echo "  4. Preserve any existing customizations"
    echo ""
    
    if [[ "$dry_run" != "true" ]]; then
        read -p "Continue? [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            log_info "Aborted by user"
            exit 0
        fi
    fi
    
    backup_config
    apply_config_idempotent "$dry_run"
    
    if [[ "$dry_run" != "true" ]]; then
        verify_config
        
        echo ""
        log_success "Configuration complete!"
        log_info "A reboot is required for changes to take effect."
        log_info "To restore the original configuration, run:"
        echo "  sudo $0 --restore"
    fi
}

main "$@"
