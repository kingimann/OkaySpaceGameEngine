#!/bin/sh
# Proof that the editor makes NO SDL call with OKAY_EDITOR_PLATFORM=own, OKAY_EDITOR_RENDERER=own and OKAY_PLATFORM_BACKEND=own
# (docs/sdl_removal_plan.md, stage E3; Linux, Xvfb). The editor's version of scripts/launcher_no_sdl_check.sh.
#
# How: a tiny "trap" library is built from the list of functions libSDL2 exports (nm -D on the very libSDL2 the editor loads).
# It defines every one of them; each writes "SDL CALL: <name>" and ends the process with exit code 97. It is put in front of
# libSDL2 with LD_PRELOAD, so any call the editor (or Dear ImGui's SDL backends linked into it) makes into SDL lands in the trap
# instead. libSDL2 itself is still loaded (it is a link dependency); loading is not a call.
#
#   1. control: the default editor (sdl, sdl, sdl2) under the trap must stop at its first SDL call (shows the trap works);
#   2. own x 3, a clean run: the scene opens, the editor runs for a while, then its window is closed the way a window manager
#      closes it (WM_DELETE_WINDOW): exit code 0 and no "SDL CALL" line, so start-up and shut-down are both covered;
#   3. own x 3, the busy run: the same clicks and typing as the earlier editor runs (a Hierarchy row, typing "crate" into the
#      Hierarchy search, a click in the Scene view), the view switched to 3D and flown with the right mouse button held and W
#      pressed, then Play with Ctrl+P, the Game view's "Capture mouse" (relative mouse mode: the pointer is held and hidden)
#      and Esc to give it back, and Stop with Ctrl+P; screenshots along the way; still alive at the end with no "SDL CALL" line;
#   4. the default editor (no trap) through the very same steps, for the screenshots to compare against; ImageMagick's
#      `compare` counts the pixels that differ.
#
#   scripts/editor_no_sdl_check.sh <path to the built OkayEngine> <scene.okayscene> <output folder>
#
# Needs Xvfb, xdotool, ImageMagick (import, compare), cc, nm and libX11 (for the little window-close helper). Exit 77 when a
# tool is missing or the editor does not load a shared libSDL2 (then there is nothing to trap), 1 when a check fails.
set -u
exe=${1:?editor binary}
scene=${2:?scene file}
out=${3:?output folder}
for t in Xvfb xdotool import compare cc nm ldd; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed"; exit 77; }
done
exe=$(cd "$(dirname "$exe")" && pwd)/$(basename "$exe")
scene=$(cd "$(dirname "$scene")" && pwd)/$(basename "$scene")
lib=$(ldd "$exe" | awk '/libSDL2/ {print $3; exit}')
if [ -z "$lib" ] || [ ! -f "$lib" ]; then echo "SKIP: the editor does not load a shared libSDL2"; exit 77; fi
mkdir -p "$out"
work=$(mktemp -d) || exit 1
disp=:$((30 + $$ % 25))
Xvfb "$disp" -screen 0 1600x900x24 -nolisten tcp >"$work/xvfb.log" 2>&1 &
xpid=$!
cleanup() {
    for p in "$work"/run-*/pid; do [ -f "$p" ] && kill -9 "$(cat "$p")" 2>/dev/null; done
    for d in "$work"/run-*; do [ -f "$d/log.txt" ] && cp "$d/log.txt" "$out/editor-no-sdl-log-$(basename "$d" | sed 's/^run-//').txt"; done
    sleep 1
    kill "$xpid" 2>/dev/null; rm -rf "$work"
}
trap cleanup EXIT INT TERM
fail=0

# ---- the trap library and the close helper ---------------------------------------------------------------------------------
nm -D --defined-only "$lib" | awk '$2 == "T" || $2 == "W" || $2 == "i" {print $3}' | sed 's/@.*//' | grep '^SDL_' | sort -u > "$work/names.txt"
count=$(wc -l < "$work/names.txt")
{
    echo '#include <fcntl.h>'
    echo '#include <stdlib.h>'
    echo '#include <string.h>'
    echo '#include <unistd.h>'
    echo 'static void okay_sdl_trap(const char* name) {'
    echo '    char line[256]; size_t n = strlen(name); if (n > 200) n = 200;'
    echo '    memcpy(line, "SDL CALL: ", 10); memcpy(line + 10, name, n); line[10 + n] = 10;'
    echo '    (void)!write(2, line, 11 + n);'
    echo '    const char* path = getenv("OKAY_SDL_TRAP_LOG");'
    echo '    if (path) { int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600); if (fd >= 0) { (void)!write(fd, line, 11 + n); close(fd); } }'
    echo '    _exit(97);'
    echo '}'
    while read -r name; do echo "void $name(void) { okay_sdl_trap(\"$name\"); }"; done < "$work/names.txt"
} > "$work/sdltrap.c"
if ! cc -shared -fPIC -O0 -w -o "$work/libsdltrap.so" "$work/sdltrap.c"; then echo "FAIL: the trap library did not build"; exit 1; fi
# xclose <window id>: send WM_DELETE_WINDOW, what a window manager's close button does (Xvfb has no window manager).
cat > "$work/xclose.c" <<'EOF'
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char** argv) {
    Display* d = XOpenDisplay(NULL);
    if (!d || argc < 2) return 1;
    Window w = (Window)strtoul(argv[1], NULL, 0);
    XEvent e; memset(&e, 0, sizeof(e));
    e.xclient.type = ClientMessage; e.xclient.window = w; e.xclient.format = 32;
    e.xclient.message_type = XInternAtom(d, "WM_PROTOCOLS", False);
    e.xclient.data.l[0] = (long)XInternAtom(d, "WM_DELETE_WINDOW", False);
    e.xclient.data.l[1] = CurrentTime;
    XSendEvent(d, w, False, NoEventMask, &e);
    XCloseDisplay(d);
    return 0;
}
EOF
if ! cc -O0 -w -o "$work/xclose" "$work/xclose.c" -lX11; then echo "SKIP: cannot build the close helper (libX11 development files)"; exit 77; fi
echo "trap: $count SDL functions from $lib"

export DISPLAY="$disp" LIBGL_ALWAYS_SOFTWARE=1
# start <name> <own|sdl> <trap 0|1>: a fresh editor in its own home folder.
start() {
    name=$1; mode=$2; trapit=$3
    dir="$work/run-$name"; mkdir -p "$dir/home" "$dir/tmp"
    if [ "$mode" = own ]; then sw="OKAY_EDITOR_PLATFORM=own OKAY_EDITOR_RENDERER=own OKAY_PLATFORM_BACKEND=own"
    else sw="OKAY_EDITOR_PLATFORM=sdl OKAY_EDITOR_RENDERER=sdl OKAY_PLATFORM_BACKEND=sdl2"; fi
    pre=""
    [ "$trapit" = 1 ] && pre="LD_PRELOAD=$work/libsdltrap.so"
    ( cd "$dir" || exit 1
      # shellcheck disable=SC2086
      env HOME="$dir/home" TMPDIR="$dir/tmp" XDG_CONFIG_HOME="$dir/home/.config" XDG_DATA_HOME="$dir/home/.local/share" OKAY_DEV=1 \
        HTTPS_PROXY=http://127.0.0.1:9 HTTP_PROXY=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 http_proxy=http://127.0.0.1:9 \
        OKAY_UPDATE_SERVER=http://127.0.0.1:9 OKAY_ALSA_DEVICE=null OKAY_SDL_TRAP_LOG="$dir/trap.txt" $sw $pre \
        "$exe" --dev --open "$scene" > "$dir/log.txt" 2>&1 &
      echo $! > "$dir/pid.tmp"; mv "$dir/pid.tmp" "$dir/pid"
      wait $!; echo $? > "$dir/exit.tmp"; mv "$dir/exit.tmp" "$dir/exit" ) </dev/null >/dev/null 2>&1 &
}
wait_window() {   # -> the largest visible window's id, or ""
    i=0; wid=""
    while [ $i -lt 150 ] && [ -z "$wid" ]; do
        sleep 0.2
        wid=$(xdotool search --onlyvisible --name "OkayEngine|OkaySpace Editor" 2>/dev/null | head -n1)
        i=$((i+1))
    done
    echo "$wid"
}
wait_exit() {     # <name> <seconds>
    i=0; while [ $i -lt $(($2 * 5)) ] && [ ! -f "$work/run-$1/exit" ]; do sleep 0.2; i=$((i+1)); done
    [ -f "$work/run-$1/exit" ] && cat "$work/run-$1/exit"
}
trapped() { [ -s "$work/run-$1/trap.txt" ] || grep -q "SDL CALL" "$work/run-$1/log.txt"; }

# The clicks and keys of runs 3 and 4 (window-relative; the editor opens at 1280 x 800 in the middle of the 1600 x 900 screen).
session() {   # <name> <window id>
    n=$1; w=$2
    eval "$(xdotool getwindowgeometry --shell "$w")"; ox=$X; oy=$Y
    c() { xdotool mousemove $((ox + $1)) $((oy + $2)); sleep 0.25; xdotool click 1; sleep 0.6; }
    xdotool windowfocus "$w" 2>/dev/null
    sleep 1
    import -window root "$out/$n-1-start.png"
    c 697 590          # the Scene view
    c 440 300          # a ball in the Scene view
    c 100 434          # a Hierarchy row
    c 130 97           # the Hierarchy search box
    xdotool type --delay 120 crate
    sleep 2
    import -window root "$out/$n-2-after-input.png"
    # 3D, then fly: right button held over the view, the mouse moved, W held for a second.
    c 298 97           # the 2D / 3D switch
    sleep 1.5
    import -window root "$out/$n-3-before-fly.png"
    xdotool mousemove $((ox + 570)) $((oy + 420)); sleep 0.3
    xdotool mousedown 3; sleep 0.3
    for s in 1 2 3 4 5 6 7 8; do xdotool mousemove_relative -- 12 4; sleep 0.08; done
    xdotool keydown w; sleep 1.0; xdotool keyup w
    sleep 0.2; xdotool mouseup 3
    sleep 1.5
    import -window root "$out/$n-4-after-fly.png"
    # Play and Stop.
    xdotool mousemove $((ox + 697)) $((oy + 700)); sleep 0.3
    xdotool key ctrl+p; sleep 4
    import -window root "$out/$n-5-playing.png"
    # The Game view's "Capture mouse" box: relative mouse mode (the pointer is held in the window and hidden; the game reads
    # the motion), then Esc gives the pointer back (relative mode off, the pointer parked in the middle of the Game view).
    c 815 97; sleep 1
    for s in 1 2 3 4 5 6; do xdotool mousemove_relative -- 25 10; sleep 0.15; done
    sleep 0.5
    eval "$(xdotool getmouselocation --shell)"
    echo "$n: pointer while captured at $((X - ox)),$((Y - oy)) in the window (after moving it 150,60 from the box at 815,97)" >> "$work/pointer.txt"
    import -window root "$out/$n-5b-captured.png"
    xdotool key Escape; sleep 1.5
    eval "$(xdotool getmouselocation --shell)"
    echo "$n: pointer after Esc at $((X - ox)),$((Y - oy)) in the window" >> "$work/pointer.txt"
    xdotool key ctrl+p; sleep 3
    import -window root "$out/$n-6-stopped.png"
}
differ() {   # <a.png> <b.png> -> number of differing pixels (ImageMagick AE)
    compare -metric AE "$1" "$2" null: 2>&1 | awk '{print $1}'
}

# ---- 1. control: the default editor must hit the trap -----------------------------------------------------------------------
start control sdl 1
code=$(wait_exit control 60)
first=$(head -n1 "$work/run-control/trap.txt" 2>/dev/null)
echo "control (sdl platform, sdl renderer, sdl2 backend): exit code ${code:-none}, first trapped call: ${first:-none}"
if [ "${code:-}" != 97 ] || [ -z "$first" ]; then echo "FAIL: the trap did not catch the default editor's SDL calls"; fail=1; fi

# ---- 2. own x 3, clean start and close ----------------------------------------------------------------------------------
start clean own 1
wid=$(wait_window)
if [ -z "$wid" ]; then echo "FAIL: no editor window (clean run)"; tail -n 30 "$work/run-clean/log.txt"; fail=1
else
    sleep 15
    "$work/xclose" "$wid"
    code=$(wait_exit clean 60)
    echo "own x 3, clean run (opened the scene, then the window was closed): exit code ${code:-none} (still running if none)"
    grep -E "^editor: (no SDL|platform|renderer)" "$work/run-clean/log.txt"
    if trapped clean; then echo "FAIL: SDL was called:"; cat "$work/run-clean/trap.txt"; fail=1
    elif [ "${code:-}" != 0 ]; then echo "FAIL: the editor did not exit cleanly"; tail -n 20 "$work/run-clean/log.txt"; fail=1
    else echo "PASS: no SDL call from start to exit"; fi
fi
[ -f "$work/run-clean/pid" ] && kill -9 "$(cat "$work/run-clean/pid")" 2>/dev/null

# ---- 3. own x 3, the busy run ---------------------------------------------------------------------------------------------
start own own 1
wid=$(wait_window)
if [ -z "$wid" ]; then echo "FAIL: no editor window (busy run)"; tail -n 30 "$work/run-own/log.txt"; fail=1
else
    sleep 14
    session own "$wid"
    if [ -f "$work/run-own/exit" ]; then echo "FAIL: the editor ended during the busy run (exit $(cat "$work/run-own/exit"))"; fail=1; fi
    if trapped own; then echo "FAIL: SDL was called:"; cat "$work/run-own/trap.txt"; fail=1
    else echo "PASS: no SDL call during the busy run (clicks, typing, right-mouse fly, Play, mouse capture and Esc, Stop)"; fi
fi
[ -f "$work/run-own/pid" ] && kill "$(cat "$work/run-own/pid")" 2>/dev/null
sleep 3

# ---- 4. the default editor through the same steps (no trap), for the pictures ----------------------------------------------
start default sdl 0
wid=$(wait_window)
if [ -z "$wid" ]; then echo "NOTE: no default editor window"; tail -n 20 "$work/run-default/log.txt"
else
    sleep 14
    session default "$wid"
fi
[ -f "$work/run-default/pid" ] && kill "$(cat "$work/run-default/pid")" 2>/dev/null
sleep 2

# ---- what the pictures say -------------------------------------------------------------------------------------------------
for s in 1-start 2-after-input 3-before-fly 4-after-fly 5-playing 5b-captured 6-stopped; do
    if [ -f "$out/own-$s.png" ] && [ -f "$out/default-$s.png" ]; then
        compare "$out/default-$s.png" "$out/own-$s.png" "$out/diff-$s.png" 2>/dev/null
        echo "default vs own, $s: $(differ "$out/default-$s.png" "$out/own-$s.png") pixels differ (of 1440000)"
    fi
done
for n in own default; do
    [ -f "$out/$n-3-before-fly.png" ] && echo "$n: the fly changed $(differ "$out/$n-3-before-fly.png" "$out/$n-4-after-fly.png") pixels; Play changed $(differ "$out/$n-4-after-fly.png" "$out/$n-5-playing.png"); Stop changed $(differ "$out/$n-5-playing.png" "$out/$n-6-stopped.png")"
done
[ -f "$work/pointer.txt" ] && cat "$work/pointer.txt"

[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME CHECKS FAILED"
exit $fail
