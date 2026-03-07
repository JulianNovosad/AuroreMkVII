#!/bin/bash
set -euo pipefail

# Aurore Mk VI - Log Rotation
# Retains last 3 runs, archives older ones.

LOG_ROOT="/var/log/aurore"
ARCHIVE_DIR="${LOG_ROOT}/archive"

if [ ! -d "$LOG_ROOT" ]; then
    echo "Log root $LOG_ROOT does not exist. Nothing to rotate."
    exit 0
fi

mkdir -p "$ARCHIVE_DIR"

# Find run directories, sort by name (timestamp), exclude archive
# We look for directories matching run-*
RUN_DIRS=($(find "$LOG_ROOT" -maxdepth 1 -type d -name "run-*" | sort))

# Keep last 3
KEEP=3
COUNT=${#RUN_DIRS[@]}

if [ "$COUNT" -gt "$KEEP" ]; then
    REMOVE_COUNT=$((COUNT - KEEP))
    TO_ARCHIVE=("${RUN_DIRS[@]:0:$REMOVE_COUNT}")
    
    echo "Archiving $REMOVE_COUNT older runs..."
    
    # Create archive name based on timestamp of the first run being archived
    ARCHIVE_NAME="archive_$(date +%s).tar.gz"
    
    tar -czf "${ARCHIVE_DIR}/${ARCHIVE_NAME}" "${TO_ARCHIVE[@]}"
    
    # Remove archived directories
    rm -rf "${TO_ARCHIVE[@]}"
    
    echo "Archived to ${ARCHIVE_DIR}/${ARCHIVE_NAME}"
else
    echo "Log count ($COUNT) <= $KEEP. No rotation needed."
fi
