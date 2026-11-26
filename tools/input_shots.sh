#!/bin/sh
# Build and run tools/input_shots.cpp by hand (it is not part of the CMake build).
#   tools/input_shots.sh <build folder> [output folder] [work folder]
# The build folder is a CMake build of this repository with the okayui libraries and the engine built (for example after
# `ninja okay-editor` or `ninja test_okayui_editor_panels`); Dear ImGui comes from its _deps/imgui-src. The Input panel is drawn
# with OkayUI (the editor's own screen), so the tool links OkayUI and the engine from that folder.
# The pictures are NOT kept in the repository: the output folder defaults to $OKAY_SHOT_DIR, else a temp folder.
# Compiles Dear ImGui, its SDL renderer backend and the tool (-O0), links them with the build's libraries, then writes the PNGs
# (needs SDL2 and ImageMagick's `convert`).
set -e
B=$(cd "$1" && pwd); IM=$B/_deps/imgui-src
OUT=${2:-${OKAY_SHOT_DIR:-${TMPDIR:-/tmp}/input_shots_out}}; WORK=${3:-${TMPDIR:-/tmp}/input_shots_build}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$WORK" "$OUT"
for f in $IM/imgui.cpp $IM/imgui_draw.cpp $IM/imgui_tables.cpp $IM/imgui_widgets.cpp $IM/backends/imgui_impl_sdlrenderer2.cpp $ROOT/tools/input_shots.cpp; do
  o="$WORK/$(basename "$f" .cpp).o"
  [ "$o" -nt "$f" ] || nice g++ -std=c++17 -O0 -I"$ROOT/editor" -I"$ROOT/engine/include" -I"$ROOT/shared" -I"$ROOT/okayui/include" -I"$ROOT/okayui/screens" \
    -I"$ROOT/okayui/soft" -isystem /usr/include/SDL2 -isystem "$IM" -isystem "$IM/backends" -c "$f" -o "$o"
done
nice g++ "$WORK"/*.o "$B/okayui/libokay_ui_soft.a" "$B/okayui/libokay_ui_screens.a" "$B/okayui/libokay_ui.a" "$B/engine/libokay_engine.a" \
  "$B/engine/libokay_crypto.a" "$B/engine/libokay_physcore.a" "$B/okayscript/libokayscript_lang.a" "$B/engine/libokay_platform.a" \
  -lSDL2 -ldl -lpthread -o "$WORK/input_shots"
"$WORK/input_shots" "$OUT"
