#!/usr/bin/env bash
# run_web.sh — Build sqlc if needed, install dependencies, and launch Flask webapp
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

echo "=== SQL Query Compiler & Optimizer — Web Application Launcher ==="

# 1. Build sqlc if missing
if [[ ! -x "$ROOT_DIR/sqlc" ]]; then
    echo "[1/3] Compiler binary ./sqlc not found. Building with make..."
    make -j4
else
    echo "[1/3] Compiler binary ./sqlc is ready."
fi

# 2. Check and install Python dependencies if needed
echo "[2/3] Checking Python dependencies..."
python3 -c "import flask" 2>/dev/null || {
    echo "Installing Flask dependencies from webapp/requirements.txt..."
    pip3 install -r webapp/requirements.txt --break-system-packages 2>/dev/null || pip3 install -r webapp/requirements.txt
}

# 3. Launch Flask web application
echo "[3/3] Starting Flask web server on http://127.0.0.1:5000 ..."
export FLASK_APP="webapp.app"
export FLASK_ENV="development"
python3 webapp/app.py
