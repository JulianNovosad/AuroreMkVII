#!/bin/bash
# Aurore MkVII Calibration Server Startup Script

cd /home/pi/AuroreMkVII/aurore-link

echo "Starting Aurore calibration server..."
echo "Access the calibration interface at: http://$(hostname -I | awk '{print $1}'):8080/calibrate"
echo "Press Ctrl+C to stop"
echo ""

node server.js
