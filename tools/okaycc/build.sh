#!/usr/bin/env bash
# Build okay-cc by itself. Not part of the engine CMake, CI, or releases.
# Usage: tools/okaycc/build.sh
set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -Wall -Wextra -Wpedantic -Wshadow -g -O0 -I src)

mkdir -p build

objs=()
for src in src/diag.cpp src/token.cpp src/lexer.cpp; do
  obj="build/$(basename "${src%.cpp}").o"
  "$CXX" "${FLAGS[@]}" -c "$src" -o "$obj"
  objs+=("$obj")
done

"$CXX" "${FLAGS[@]}" -o build/okay-cc src/main.cpp "${objs[@]}"
"$CXX" "${FLAGS[@]}" -o build/test_lexer tests/test_lexer.cpp "${objs[@]}"

./build/test_lexer
tests/test_plan.sh
