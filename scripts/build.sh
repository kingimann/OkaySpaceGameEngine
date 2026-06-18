#!/usr/bin/env bash
# Build OkaySpaceGameEngine natively (Linux/macOS) and run the demo.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=build
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j

echo
echo "Built. Run the demo with:  ./$BUILD_DIR/bin/sandbox"
echo "Run the tests with:        (cd $BUILD_DIR && ctest --output-on-failure)"
