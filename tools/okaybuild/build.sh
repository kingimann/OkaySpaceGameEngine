#!/bin/sh
# Standalone build of the okay-build prototype and its tests. Not part of CMake or CI.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-/tmp/okaybuild-bin}
mkdir -p "$out"
g++ -std=c++17 -O0 -Wall -Wextra -pthread "$here/okay-build.cpp" -o "$out/okay-build"
g++ -std=c++17 -O0 -Wall -Wextra "$here/test_okaybuild.cpp" -o "$out/test_okaybuild"
"$out/test_okaybuild" "$out/okay-build"
