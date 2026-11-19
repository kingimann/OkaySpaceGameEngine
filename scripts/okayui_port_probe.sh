#!/bin/bash
# Syntax-check Dear ImGui panels against OkayUI (no imgui.h: okayui/probe/imgui.h stands in), and list the errors by kind.
# usage: scripts/okayui_port_probe.sh editor/ConsolePanel.cpp [more files...]
# Nothing is linked or run: a compile check says nothing about behaviour.
cd "$(dirname "$0")/.."
for f in "$@"; do
  out=$(g++ -std=c++17 -fsyntax-only -Iokayui/probe -Iokayui/include -Iokayui/screens -Ieditor -Ilauncher -Ishared -Iengine/include -I/usr/include/SDL2 "$f" 2>&1)
  echo "== $f: $(echo "$out" | grep -c ' error: ') errors"
  echo "$out" | grep ' error: ' | sed -E 's/^[^ ]+ error: //' | sed -E "s/'[^']*'/X/g; s/[0-9]+/N/g" | sort | uniq -c | sort -rn | head -${PROBE_TOP:-15}
done
