#!/usr/bin/env bash
# OkaySpace launcher (Linux/macOS): update from GitHub, build, and run.
# Builds the launcher once, then hands off to it. Extra args pass through.
set -euo pipefail
cd "$(dirname "$0")"

LAUNCHER=build/bin/okayspace-launcher
if [ ! -x "$LAUNCHER" ]; then
    echo "[launch] First run: building the launcher..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build build --target okayspace-launcher -j >/dev/null
fi

exec "$LAUNCHER" "$@"
