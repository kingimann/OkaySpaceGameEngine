#!/bin/sh
# Build and run tools/input_shots.cpp by hand (it is not part of the CMake build).
#   tools/input_shots.sh <imgui folder, e.g. build/_deps/imgui-src> [output folder] [work folder]
# The pictures are NOT kept in the repository: the output folder defaults to $OKAY_SHOT_DIR, else a temp folder.
# Compiles only the engine files the input code needs (-O0), Dear ImGui and the SDL renderer backend, then writes the PNGs
# (needs SDL2 and ImageMagick's `convert`).
set -e
IM=$1; OUT=${2:-${OKAY_SHOT_DIR:-${TMPDIR:-/tmp}/input_shots_out}}; WORK=${3:-${TMPDIR:-/tmp}/input_shots_build}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$WORK" "$OUT"
for f in $(ls $ROOT/engine/src/Input/*.cpp | grep -v -e GameInput -e TouchControls) $ROOT/engine/src/Core/Prefs.cpp $ROOT/engine/src/Core/AccessibilitySettings.cpp $ROOT/engine/src/Core/Subtitles.cpp $ROOT/engine/src/Core/Localization.cpp $ROOT/engine/src/Core/LocTable.cpp $ROOT/tools/input_shots_stubs.cpp $ROOT/engine/src/Core/SaveFile.cpp $ROOT/engine/src/Graphics/ColorVision.cpp $IM/imgui.cpp $IM/imgui_draw.cpp $IM/imgui_tables.cpp $IM/imgui_widgets.cpp $IM/backends/imgui_impl_sdlrenderer2.cpp $ROOT/tools/input_shots.cpp; do
  o="$WORK/$(basename "$f" .cpp).o"
  [ "$o" -nt "$f" ] || nice g++ -std=c++17 -O0 -I"$ROOT/editor" -I"$ROOT/engine/include" -I"$ROOT/shared" -isystem /usr/include/SDL2 -isystem "$IM" -isystem "$IM/backends" -c "$f" -o "$o"
done
nice g++ "$WORK"/*.o -lSDL2 -lpthread -o "$WORK/input_shots"
"$WORK/input_shots" "$OUT"
