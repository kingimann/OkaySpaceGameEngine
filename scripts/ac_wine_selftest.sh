#!/usr/bin/env bash
# Cross-compile tests/win/ac_selftest.cpp with MinGW and run it under wine (or copy the exe to Windows).
# Builds okay_ac_tool natively, makes a signed code baseline of the test exe with a throwaway key, and
# runs the exe twice: linked at its preferred base, and linked at an address the loader can't give it, so
# the relocated-image path (the code hash must not change with the load address) is exercised too.
# Needs: x86_64-w64-mingw32-g++, g++, openssl, wine64. Everything goes under $OUT.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-${TMPDIR:-/tmp}/ac_wine_selftest}"
rm -rf "$OUT"; mkdir -p "$OUT"
g++ -std=c++17 -O1 -I"$ROOT/shared" -I"$ROOT/engine/include" "$ROOT/tools/okay_ac_tool.cpp" -o "$OUT/okay_ac_tool"
openssl rand -hex 32 > "$OUT/test.key"   # a throwaway signing key (never a real one)
export WINEPREFIX="$OUT/prefix" WINEDEBUG=-all WINEARCH=win64 LC_ALL=C.UTF-8
run_variant() {   # name, extra link flags
    local name="$1"; shift
    local dir="$OUT/$name/game"
    mkdir -p "$dir/Data"
    x86_64-w64-mingw32-g++ -std=c++17 -O1 -static -I"$ROOT/shared" -I"$ROOT/engine/include" "$ROOT/tests/win/ac_selftest.cpp" \
        -o "$dir/ac_selftest.exe" "$@"
    OKAY_AC_SIGNING_KEY_FILE="$OUT/test.key" "$OUT/okay_ac_tool" baseline --build "selftest-$name" --out "$dir/Data/codebaseline.json" "$dir/ac_selftest.exe"
    "$OUT/okay_ac_tool" verify "$dir/Data/codebaseline.json"
    ( cd "$dir" && /usr/lib/wine/wine64 ./ac_selftest.exe )
}
echo "=== variant 1: preferred base ==="
run_variant preferred
echo "=== variant 2: relocated (linked at 0x7ffe0000, where the loader keeps its shared data) ==="
run_variant relocated -Wl,--dynamicbase -Wl,--image-base=0x7ffe0000
