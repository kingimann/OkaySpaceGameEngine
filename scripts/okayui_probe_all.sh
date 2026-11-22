#!/bin/bash
# Probe every .cpp under editor/ and launcher/ that mentions ImGui (the include, a type or a call) against OkayUI; prints "file errors" per file
# and a total. Syntax check only (nothing is linked or run). usage: scripts/okayui_probe_all.sh [outfile]
cd "$(dirname "$0")/.."
out=${1:-/dev/stdout}
files=$(grep -lE 'ImGui|imgui|ImDrawList' editor/*.cpp launcher/*.cpp 2>/dev/null)
probe() { n=$(g++ -std=c++17 -fsyntax-only -DOKAY_HAVE_OKAYUI -Iokayui/probe -Iokayui/probe/backends -Iokayui/include -Iokayui/screens -Iokayui/soft -Ieditor -Ieditor/vendor -Ilauncher -Ishared -Iengine/include -I/usr/include/SDL2 "$1" 2>&1 | grep -c ' error: '); echo "$1 $n"; }
export -f probe
echo "$files" | xargs -P2 -I{} bash -c 'probe {}' | sort > "$out.tmp" 2>/dev/null || true
cat "$out.tmp" > "$out"; rm -f "$out.tmp"
awk '{t+=$2; if ($2>0) b++} END {print "TOTAL", NR, "files,", b+0, "with errors,", t+0, "errors"}' "$out" >> "$out"
