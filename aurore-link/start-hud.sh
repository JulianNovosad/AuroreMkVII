#!/usr/bin/env bash
# start-hud.sh — Start the Aurore MkVII web HUD server (Node.js)
#
# Usage:
#   ./start-hud.sh              # Start server (interactive)
#   ./start-hud.sh &            # Start server (background)
#
# The server will be available at http://localhost:8080
# Press Ctrl+C to stop the server.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Check dependencies
if ! command -v node &> /dev/null; then
  echo "ERROR: Node.js is not installed. Please install Node.js 18+" >&2
  exit 1
fi

# Install dependencies if needed
if [[ ! -d "node_modules" ]]; then
  echo "Installing dependencies..."
  npm install
fi

echo "Starting Aurore MkVII HUD server on http://localhost:8080"
echo "Press Ctrl+C to stop."
echo ""

npm start
