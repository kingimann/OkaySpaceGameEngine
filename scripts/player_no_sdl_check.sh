#!/bin/sh
# Proof that the game player makes NO SDL call with OKAY_PLATFORM_BACKEND=own (docs/sdl_removal_plan.md, "Player";
# Linux, Xvfb). The player's version of scripts/launcher_no_sdl_check.sh and scripts/editor_no_sdl_check.sh.
#
# How: a tiny "trap" library is built from the list of functions libSDL2 exports (nm -D on the very libSDL2 the player loads).
# It defines every one of them; each writes "SDL CALL: <name>" and ends the process with exit code 97. It is put in front of
# libSDL2 with LD_PRELOAD, so any call the player makes into SDL lands in the trap instead. libSDL2 itself is still loaded (it
# is a link dependency); loading is not a call.
#
#   1. control: the default player (sdl2 backend) under the trap must stop at its first SDL call (shows the trap works);
#   2. own, the play run: the player opens the scene the way `OkaySpacePlayer.exe <scene>` does (docs/try_it.md), plays for
#      15 seconds with keys and the mouse (W held, Shift+W, A and D, Space, the mouse moved and clicked, F3 for the stats
#      overlay and F3 again, F11 to fullscreen and back; Esc would quit), screenshots along the way, then its window gets WM_DELETE_WINDOW (what a close button sends):
#      exit code 0 and no "SDL CALL" line, so start-up, play and shut-down are all covered;
#   3. the default player (no trap) through the same steps, for the pictures to compare against.
#
#   scripts/player_no_sdl_check.sh <path to the built okay-player> <scene.okayscene> <output folder>
#
# A player built with -DOKAY_WITH_SDL=OFF loads no libSDL2 at all: the script then says so (ldd) and only runs step 2 (with
# nothing to trap, the proof is ldd itself). Needs Xvfb, xdotool, ImageMagick (import, compare), cc, nm and libX11 (for the
# little window-close helper). Exit 77 when a tool is missing, 1 when a check fails.
set -u
exe=${1:?player binary}
scene=${2:?scene file}
out=${3:?output folder}
for t in Xvfb xdotool import compare cc nm ldd; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed"; exit 77; }
done
exe=$(cd "$(dirname "$exe")" && pwd)/$(basename "$exe")
scene=$(cd "$(dirname "$scene")" && pwd)/$(basename "$scene")
lib=$(ldd "$exe" | awk '/libSDL2/ {print $3; exit}')
nosdl=0
if [ -z "$lib" ] || [ ! -f "$lib" ]; then
    nosdl=1
    echo "the player loads no libSDL2 (ldd):"; ldd "$exe" | grep -i -E "sdl|not found" || echo "  (no SDL library in the list)"
fi
mkdir -p "$out"
work=$(mktemp -d) || exit 1
disp=:$((30 + $$ % 25))
Xvfb "$disp" -screen 0 1280x800x24 -nolisten tcp >"$work/xvfb.log" 2>&1 &
xpid=$!
cleanup() {
    for p in "$work"/run-*/pid; do [ -f "$p" ] && kill -9 "$(cat "$p")" 2>/dev/null; done
    for d in "$work"/run-*; do [ -f "$d/log.txt" ] && cp "$d/log.txt" "$out/player-no-sdl-log-$(basename "$d" | sed 's/^run-//').txt"; done
    sleep 1
    kill "$xpid" 2>/dev/null; rm -rf "$work"
}
trap cleanup EXIT INT TERM
fail=0

# ---- the trap library and the close helper ---------------------------------------------------------------------------------
if [ $nosdl = 0 ]; then
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
    echo "trap: $count SDL functions from $lib"
fi
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

export DISPLAY="$disp" LIBGL_ALWAYS_SOFTWARE=1
# start <name> <own|sdl> <trap 0|1>: a fresh player in its own home folder.
start() {
    name=$1; mode=$2; trapit=$3
    dir="$work/run-$name"; mkdir -p "$dir/home" "$dir/tmp"
    if [ "$mode" = own ]; then sw="OKAY_PLATFORM_BACKEND=own"; else sw="OKAY_PLATFORM_BACKEND=sdl2"; fi
    pre=""
    [ "$trapit" = 1 ] && [ $nosdl = 0 ] && pre="LD_PRELOAD=$work/libsdltrap.so"
    ( cd "$dir" || exit 1
      # shellcheck disable=SC2086
      env HOME="$dir/home" TMPDIR="$dir/tmp" XDG_CONFIG_HOME="$dir/home/.config" XDG_DATA_HOME="$dir/home/.local/share" \
        HTTPS_PROXY=http://127.0.0.1:9 HTTP_PROXY=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 http_proxy=http://127.0.0.1:9 \
        OKAY_ALSA_DEVICE=null SDL_AUDIODRIVER=dummy OKAY_SDL_TRAP_LOG="$dir/trap.txt" $sw $pre \
        "$exe" "$scene" > "$dir/log.txt" 2>&1 &
      echo $! > "$dir/pid.tmp"; mv "$dir/pid.tmp" "$dir/pid"
      wait $!; echo $? > "$dir/exit.tmp"; mv "$dir/exit.tmp" "$dir/exit" ) </dev/null >/dev/null 2>&1 &
}
wait_window() {   # -> the largest visible named window's id, or "" (the player's hidden 16 x 16 GL helper window is never visible)
    i=0; wid=""
    while [ $i -lt 150 ] && [ -z "$wid" ]; do
        sleep 0.2
        best=0
        for w in $(xdotool search --onlyvisible --name '' 2>/dev/null); do
            [ -n "$(xdotool getwindowname "$w" 2>/dev/null)" ] || continue   # the root window and other unnamed ones
            eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)" || continue
            a=$((WIDTH * HEIGHT))
            if [ "$a" -gt "$best" ] && [ "$WIDTH" -gt 100 ]; then best=$a; wid=$w; fi
        done
        i=$((i+1))
    done
    echo "$wid"
}
wait_exit() {     # <name> <seconds>
    i=0; while [ $i -lt $(($2 * 5)) ] && [ ! -f "$work/run-$1/exit" ]; do sleep 0.2; i=$((i+1)); done
    [ -f "$work/run-$1/exit" ] && cat "$work/run-$1/exit"
}
trapped() { [ -s "$work/run-$1/trap.txt" ] || grep -q "SDL CALL" "$work/run-$1/log.txt"; }

# 15 seconds of play with keys and the mouse (window-relative).
play() {   # <name> <window id>
    n=$1; w=$2
    eval "$(xdotool getwindowgeometry --shell "$w")"; ox=$X; oy=$Y; ww=$WIDTH; wh=$HEIGHT
    xdotool windowfocus "$w" 2>/dev/null
    xdotool mousemove $((ox + ww / 2)) $((oy + wh / 2)); sleep 0.3
    import -window root "$out/$n-1-start.png"
    xdotool keydown w; sleep 2.0; xdotool keyup w                      # walk
    xdotool keydown shift keydown w; sleep 1.5; xdotool keyup w keyup shift   # run
    import -window root "$out/$n-2-moved.png"
    xdotool keydown a; sleep 0.8; xdotool keyup a
    xdotool keydown d; sleep 0.8; xdotool keyup d
    xdotool key space; sleep 0.5
    for s in 1 2 3 4 5 6 7 8 9 10; do xdotool mousemove_relative -- 15 3; sleep 0.08; done   # look around
    xdotool click 1; sleep 0.4; xdotool click 3; sleep 0.4
    xdotool click 4; sleep 0.2; xdotool click 5; sleep 0.3                                    # wheel up and down
    import -window root "$out/$n-3-looked.png"
    xdotool key F3; sleep 1.5                                                                 # the stats overlay
    import -window root "$out/$n-4-stats.png"
    xdotool key F3; sleep 0.5
    xdotool key F11; sleep 1.5                                                                # fullscreen (Build Settings' toggle)
    import -window root "$out/$n-4b-fullscreen.png"
    xdotool key F11; sleep 1.0                                                                # and back
    xdotool keydown s; sleep 1.0; xdotool keyup s
    xdotool type --delay 100 hello; sleep 0.5
    sleep 2
    import -window root "$out/$n-5-end.png"
}
differ() {   # <a.png> <b.png> -> number of differing pixels (ImageMagick AE)
    compare -metric AE "$1" "$2" null: 2>&1 | awk '{print $1}'
}

# ---- 1. control: the default player must hit the trap ------------------------------------------------------------------------
if [ $nosdl = 0 ]; then
    start control sdl 1
    code=$(wait_exit control 60)
    first=$(head -n1 "$work/run-control/trap.txt" 2>/dev/null)
    echo "control (sdl2 backend): exit code ${code:-none}, first trapped call: ${first:-none}"
    if [ "${code:-}" != 97 ] || [ -z "$first" ]; then echo "FAIL: the trap did not catch the default player's SDL calls"; fail=1; fi
fi

# ---- 2. own: play for 15 seconds, then close ------------------------------------------------------------------------------
start own own 1
wid=$(wait_window)
if [ -z "$wid" ]; then echo "FAIL: no player window (own)"; tail -n 30 "$work/run-own/log.txt"; fail=1
else
    sleep 3
    play own "$wid"
    if [ -f "$work/run-own/exit" ]; then echo "FAIL: the player ended during play (exit $(cat "$work/run-own/exit"))"; fail=1; fi
    "$work/xclose" "$wid"
    code=$(wait_exit own 60)
    echo "own backend, played 15 s with keys and the mouse, then the window was closed: exit code ${code:-none} (still running if none)"
    grep -E "platform|painter|no SDL" "$work/run-own/log.txt" | head -n 6
    if trapped own; then echo "FAIL: SDL was called:"; cat "$work/run-own/trap.txt"; fail=1
    elif [ "${code:-}" != 0 ]; then echo "FAIL: the player did not exit cleanly"; tail -n 20 "$work/run-own/log.txt"; fail=1
    elif [ $nosdl = 1 ]; then echo "PASS: no SDL library loaded, played and closed cleanly"
    else echo "PASS: no SDL call from start to exit"; fi
fi
[ -f "$work/run-own/pid" ] && kill -9 "$(cat "$work/run-own/pid")" 2>/dev/null
sleep 2

# ---- 3. the default player through the same steps (no trap), for the pictures ----------------------------------------------
if [ $nosdl = 0 ]; then
    start default sdl 0
    wid=$(wait_window)
    if [ -z "$wid" ]; then echo "NOTE: no default player window"; tail -n 20 "$work/run-default/log.txt"
    else
        sleep 3
        play default "$wid"
        "$work/xclose" "$wid"
        code=$(wait_exit default 30)
        echo "default player (sdl2 backend, no trap): exit code ${code:-none}"
    fi
    [ -f "$work/run-default/pid" ] && kill "$(cat "$work/run-default/pid")" 2>/dev/null
    sleep 1
    for s in 1-start 2-moved 3-looked 4-stats 4b-fullscreen 5-end; do
        if [ -f "$out/own-$s.png" ] && [ -f "$out/default-$s.png" ]; then
            echo "default vs own, $s: $(differ "$out/default-$s.png" "$out/own-$s.png") pixels differ (of 1024000)"
        fi
    done
fi
for n in own default; do
    [ -f "$out/$n-1-start.png" ] && [ -f "$out/$n-2-moved.png" ] && echo "$n: walking changed $(differ "$out/$n-1-start.png" "$out/$n-2-moved.png") pixels; the stats overlay changed $(differ "$out/$n-3-looked.png" "$out/$n-4-stats.png")"
done

[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME CHECKS FAILED"
exit $fail
