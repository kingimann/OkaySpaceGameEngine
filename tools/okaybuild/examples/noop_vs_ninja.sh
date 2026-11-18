#!/bin/sh
# Times a no-op rebuild (everything already built) with okay-build and with Ninja, and the rebuild after touching one header.
# usage: sh noop_vs_ninja.sh OKAY_BUILD_EXE MANIFEST BUILD_DIR NINJA_BUILD_DIR HEADER TARGET...
# MANIFEST is the build.okay (in the repository root) already built into BUILD_DIR; NINJA_BUILD_DIR is a CMake+Ninja tree of the same
# sources already built for the same TARGETs (ninja target names). Prints the median of 20 no-op runs in milliseconds for each tool.
# Linux only; nothing here was run on Windows.
set -e
tool=$1; man=$2; bd=$3; nb=$4; hdr=$5; shift 5
now() { date +%s%N; }
median() { sort -n | awk '{a[NR]=$1} END{print a[int((NR+1)/2)]}'; }
t=""; i=0; while [ $i -lt 20 ]; do s=$(now); "$tool" -f "$man" --build-dir "$bd" "$@" >/dev/null 2>&1; e=$(now); t="$t $(( (e-s)/1000 ))
"; i=$((i+1)); done
echo "okay-build no-op (median of 20, process start included): $(printf '%s' "$t" | median) microseconds"
t=""; i=0; while [ $i -lt 20 ]; do s=$(now); ninja -C "$nb" "$@" >/dev/null 2>&1; e=$(now); t="$t $(( (e-s)/1000 ))
"; i=$((i+1)); done
echo "ninja no-op (median of 20, process start included):       $(printf '%s' "$t" | median) microseconds"
