#!/usr/bin/env bash
# Build okay-cc by itself. Not part of the engine CMake, CI, or releases.
# Usage: tools/okaycc/build.sh
set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -Wall -Wextra -Wpedantic -Wshadow -g -O0 -I src)

mkdir -p build

objs=()
for src in src/diag.cpp src/token.cpp src/lexer.cpp src/files.cpp src/preproc.cpp src/parser.cpp src/ir.cpp src/codegen.cpp; do
  obj="build/$(basename "${src%.cpp}").o"
  "$CXX" "${FLAGS[@]}" -c "$src" -o "$obj"
  objs+=("$obj")
done

"$CXX" "${FLAGS[@]}" -o build/okay-cc src/main.cpp "${objs[@]}"
"$CXX" "${FLAGS[@]}" -o build/test_lexer tests/test_lexer.cpp "${objs[@]}"
"$CXX" "${FLAGS[@]}" -o build/test_preproc tests/test_preproc.cpp "${objs[@]}"
"$CXX" "${FLAGS[@]}" -o build/test_parser tests/test_parser.cpp "${objs[@]}"
"$CXX" "${FLAGS[@]}" -o build/test_typecheck tests/test_typecheck.cpp "${objs[@]}"
"$CXX" "${FLAGS[@]}" -o build/test_ir tests/test_ir.cpp "${objs[@]}"
"$CXX" "${FLAGS[@]}" -o build/test_codegen tests/test_codegen.cpp "${objs[@]}"

./build/test_lexer
./build/test_preproc
./build/test_parser
./build/test_typecheck
./build/test_ir
./build/test_codegen
tests/test_plan.sh
