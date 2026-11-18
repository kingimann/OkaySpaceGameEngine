#!/bin/sh
# Start an OkaySpace program (launcher or editor) on a private Xvfb display, wait, save a screenshot, stop it.
#   scripts/headless_app_shot.sh <program> <seconds> <out.png> [VAR=value ...]
# Example (the own platform backend, our own ImGui input and renderer):
#   scripts/headless_app_shot.sh build/bin/OkaySpace 25 own.png OKAY_PLATFORM_BACKEND=own
# Needs Xvfb and ImageMagick's `import`. Settings go to a throw-away HOME, so nothing of yours is touched. The program is stopped with
# SIGKILL (SDL turns SIGTERM into a close request, which makes the launcher ask "Exit or minimise?"). The program's log is next to the picture.
prog=$1; secs=$2; out=$3; shift 3
[ -x "$prog" ] || { echo "no such program: $prog" >&2; exit 2; }
command -v Xvfb >/dev/null && command -v import >/dev/null || { echo "needs Xvfb and ImageMagick (import)" >&2; exit 77; }
tmp=$(mktemp -d) || exit 2
trap 'kill $xp 2>/dev/null; rm -rf "$tmp"' EXIT
Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$tmp/disp" >/dev/null 2>&1 &
xp=$!
i=0; while [ ! -s "$tmp/disp" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
export DISPLAY=":$(head -n1 "$tmp/disp" | tr -d '\n')" HOME="$tmp/home" XDG_RUNTIME_DIR="$tmp/home" XDG_DATA_HOME="$tmp/home/data" SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1
mkdir -p "$tmp/home"
for kv in "$@"; do export "$kv"; done
"$prog" >"${out%.png}.log" 2>&1 &
pp=$!
sleep "$secs"
import -window root "$out"
if kill -0 $pp 2>/dev/null; then echo "still running after $secs s (good)"; else echo "the program exited before the picture"; fi
kill -9 $pp 2>/dev/null
echo "picture: $out   log: ${out%.png}.log"
