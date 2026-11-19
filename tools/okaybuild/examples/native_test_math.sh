#!/bin/sh
# Builds the engine library and tests/test_math with okay-build only (no CMake, no Ninja), then runs it.
# usage: sh tools/okaybuild/examples/native_test_math.sh OKAY_BUILD_EXE BUILD_DIR [JOBS]
# Run from anywhere; it works on the repository this script lives in. Debug flags (-O0 -g0) come from `okay-build native`.
# Needs g++, the SDL2 and libsodium dev headers. Several GB of disk in BUILD_DIR; delete it when done.
# Not part of CMake, CI or releases. Linux only; nothing here was run on Windows.
set -e
tool=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
out=${2:?usage: native_test_math.sh OKAY_BUILD_EXE BUILD_DIR [JOBS]}
jobs=${3:-2}
repo=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$repo"
# okay-build works from the folder of its manifest and the manifest names sources relative to the repository root,
# so the generated manifest sits in the repository root for the run (it is removed again at the end).
man="$repo/build.native.okay"
trap 'rm -f "$man"' EXIT
mkdir -p "$out"
"$tool" native . -o "$man"
"$tool" -f "$man" -j"$jobs" --build-dir "$out" test_math
"$out/build/test_math"
