#!/usr/bin/env bash
# Checks that the internal compiler plan names every stage and the documents
# it is written from. This is a text check, not a compile test.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
PLAN="$ROOT/docs/platform/own_compiler_plan.md"
fail=0
if [[ ! -f "$PLAN" ]]; then
  echo "missing $PLAN"
  exit 1
fi
need() {
  if ! grep -q -F -- "$1" "$PLAN"; then
    echo "plan is missing: $1"
    fail=1
  fi
}
need "lexer"
need "preprocessor"
need "parser"
need "type checker"
need "intermediate"
need "optimizer"
need "x86-64"
need "ELF"
need "COFF"
need "assembler"
need "linker"
need "driver"
need "ISO/IEC 9899"
need "ISO/IEC 14882"
need "System V"
need "Windows x64"
need "Intel"
need "not in the engine CMake"
need "off by default"
if [[ "$fail" -ne 0 ]]; then
  echo "plan check failed"
  exit 1
fi
echo "plan check passed"
