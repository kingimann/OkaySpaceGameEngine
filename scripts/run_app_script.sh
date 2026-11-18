#!/bin/sh
# Run the launcher or the editor headless on a private Xvfb display with a test script (shared/TestHooks.hpp), on one or both platform backends.
# The program must be built with -DOKAY_ENABLE_TEST_HOOKS=ON (never a release build).
#   scripts/run_app_script.sh <program> <script> <outdir> [seconds-limit] [backend ...]
# For each backend (default: sdl2 own) it starts the program with OKAY_TEST_SCRIPT=<script> and OKAY_PLATFORM_BACKEND=<backend>, waits for it to leave by itself,
# and checks that it exited with code 0 (or the code of the script's `exit N`; pass EXPECT_EXIT=N). Pictures the script saves with `shot NAME.png` land in
# <outdir>/<backend>/ (outside the repository). With two backends it then compares every picture of one with the other (ImageMagick `compare`, per-pixel
# fuzz 4 percent): MAX_DIFF_PIXELS (default 0) differing pixels allowed per picture. Exit 0 = all good. Environment: APP_ARGS (program arguments, the editor
# needs --dev), EXPECT_EXIT, MAX_DIFF_PIXELS, FUZZ.
prog=$1; script=$2; outdir=$3; limit=${4:-120}; shift 4 2>/dev/null || shift $#
backends=${*:-sdl2 own}
[ -x "$prog" ] && [ -f "$script" ] || { echo "usage: $0 <program> <script> <outdir> [seconds] [backend...]" >&2; exit 2; }
command -v Xvfb >/dev/null && command -v compare >/dev/null || { echo "needs Xvfb and ImageMagick" >&2; exit 77; }
prog="$(cd "$(dirname "$prog")" && pwd)/$(basename "$prog")"; mkdir -p "$outdir"; outdir="$(cd "$outdir" && pwd)"   # absolute: the program runs inside the output folder
want=${EXPECT_EXIT:-0}; maxdiff=${MAX_DIFF_PIXELS:-0}; fuzz=${FUZZ:-4%}
status=0
mkdir -p "$outdir"
for b in $backends; do
    tmp=$(mktemp -d) || exit 2
    mkdir -p "$outdir/$b" "$tmp/home"
    Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$tmp/disp" >/dev/null 2>&1 &
    xp=$!
    i=0; while [ ! -s "$tmp/disp" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
    # The launcher and editor keep their settings next to the program, so each run gets its own copy of the program in a private folder: no run starts from what
    # the other one saved (last page, window size ...).
    mkdir -p "$tmp/app" && cp "$prog" "$tmp/app/" || exit 2
    cprog="$tmp/app/$(basename "$prog")"
    # the script's `shot` paths are relative: they are written into the backend's folder
    ( cd "$outdir/$b" && env DISPLAY=":$(head -n1 "$tmp/disp" | tr -d '\n')" HOME="$tmp/home" XDG_RUNTIME_DIR="$tmp/home" XDG_DATA_HOME="$tmp/home/data" \
        SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 OKAY_UPDATE_ON_LAUNCH=0 OKAY_PLATFORM_BACKEND=$b OKAY_TEST_SCRIPT="$(cd "$(dirname "$script")" && pwd)/$(basename "$script")" \
        timeout -s KILL "$limit" "$cprog" $APP_ARGS >"$outdir/$b/app.log" 2>&1 )
    code=$?
    kill $xp 2>/dev/null; wait $xp 2>/dev/null; rm -rf "$tmp"
    if [ "$code" = "$want" ]; then echo "[$b] exited with code $code (as wanted)"; else echo "[$b] FAILED: exit code $code, wanted $want (137 = killed by the time limit); log: $outdir/$b/app.log"; status=1; fi
done
set -- $backends
if [ $# -ge 2 ]; then
    a=$1; b=$2
    for pic in "$outdir/$a"/*.png; do
        [ -f "$pic" ] || continue
        n=$(basename "$pic")
        if [ ! -f "$outdir/$b/$n" ]; then echo "[compare] $n: missing on $b"; status=1; continue; fi
        d=$(compare -metric AE -fuzz "$fuzz" "$pic" "$outdir/$b/$n" null: 2>&1 | awk '{print $1}')
        if [ "${d:-x}" -le "$maxdiff" ] 2>/dev/null; then echo "[compare] $n: $d pixels differ (allowed $maxdiff)"; else echo "[compare] $n: $d pixels differ (allowed $maxdiff) FAIL"; status=1; fi
    done
fi
exit $status
