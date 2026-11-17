#!/bin/sh
# A build made with -DOKAY_WITH_SDL=OFF (docs/sdl_removal_plan.md, "A build with no SDL"): show that its three programs link no
# SDL (ldd) and run them under Xvfb with NO switches set (in such a build our own code is the default): each one starts, gets a
# few inputs, gives a screenshot and exits cleanly. Linux only.
#
#   scripts/no_sdl_build_check.sh <bin folder of the OKAY_WITH_SDL=OFF build> <scene.okayscene> <output folder>
#
#   launcher (OkaySpace):  the Create page; a click into the project search box and typing "abc"; a click on the sidebar's Play;
#                          screenshots; it exits by itself after a number of frames (OKAY_LAUNCHER_FRAME_STATS_EXIT): exit 0.
#   player (okay-player):  the scene; W held, the mouse moved and clicked, F3 (stats overlay); a screenshot; the window's close
#                          button (WM_DELETE_WINDOW): exit 0.
#   editor (OkayEngine):   --dev --open <scene>; a click in the Scene view, a Hierarchy row, typing "crate" into the Hierarchy
#                          search, Ctrl+P (Play) and Ctrl+P (Stop); screenshots; the window's close button: exit 0.
#
# Needs Xvfb, xdotool, ImageMagick (import), cc and libX11 (for the window-close helper). Exit 77 when a tool is missing, 1 when a
# check fails. A program that is not in the folder is reported and skipped.
set -u
bin=${1:?bin folder}
scene=${2:?scene file}
out=${3:?output folder}
for t in Xvfb xdotool import cc ldd; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed"; exit 77; }
done
bin=$(cd "$bin" && pwd)
scene=$(cd "$(dirname "$scene")" && pwd)/$(basename "$scene")
mkdir -p "$out"
work=$(mktemp -d) || exit 1
disp=:$((30 + $$ % 25))
Xvfb "$disp" -screen 0 1600x900x24 -nolisten tcp >"$work/xvfb.log" 2>&1 &
xpid=$!
cleanup() {
    for p in "$work"/run-*/pid; do [ -f "$p" ] && kill -9 "$(cat "$p")" 2>/dev/null; done
    for d in "$work"/run-*; do [ -f "$d/log.txt" ] && cp "$d/log.txt" "$out/no-sdl-build-log-$(basename "$d" | sed 's/^run-//').txt"; done
    sleep 1
    kill "$xpid" 2>/dev/null; rm -rf "$work"
}
trap cleanup EXIT INT TERM
fail=0

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

# ---- ldd: no SDL library ---------------------------------------------------------------------------------------------------
for p in OkaySpace okay-player OkayEngine; do
    [ -x "$bin/$p" ] || { echo "NOTE: $bin/$p is not there (not built)"; continue; }
    if ldd "$bin/$p" | grep -qi sdl; then echo "FAIL: $p links SDL:"; ldd "$bin/$p" | grep -i sdl; fail=1
    else echo "ldd $p: no SDL ($(ldd "$bin/$p" | awk '{print $1}' | tr '\n' ' '))"; fi
done

export DISPLAY="$disp" LIBGL_ALWAYS_SOFTWARE=1
# start <name> <program> [args...]: a fresh copy in its own folder, no OKAY_* switch set.
start() {
    name=$1; prog=$2; shift 2
    dir="$work/run-$name"; mkdir -p "$dir/home" "$dir/tmp"
    ( cd "$dir" || exit 1
      env -u OKAY_PLATFORM_BACKEND -u OKAY_LAUNCHER_RENDERER -u OKAY_EDITOR_PLATFORM -u OKAY_EDITOR_RENDERER \
        HOME="$dir/home" TMPDIR="$dir/tmp" XDG_CONFIG_HOME="$dir/home/.config" XDG_DATA_HOME="$dir/home/.local/share" \
        HTTPS_PROXY=http://127.0.0.1:9 HTTP_PROXY=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 http_proxy=http://127.0.0.1:9 \
        OKAY_UPDATE_SERVER=http://127.0.0.1:9 OKAY_ACCOUNT_SERVER=http://127.0.0.1:9 OKAY_ALSA_DEVICE=null \
        OKAY_UI_DEMO_TAB=0 OKAY_UPDATE_ON_LAUNCH=0 $EXTRA_ENV "$prog" "$@" > "$dir/log.txt" 2>&1 &
      echo $! > "$dir/pid.tmp"; mv "$dir/pid.tmp" "$dir/pid"
      wait $!; echo $? > "$dir/exit.tmp"; mv "$dir/exit.tmp" "$dir/exit" ) </dev/null >/dev/null 2>&1 &
}
wait_window() {   # <name pattern> -> the window id or ""
    i=0; wid=""
    while [ $i -lt 300 ] && [ -z "$wid" ]; do sleep 0.2; wid=$(xdotool search --onlyvisible --name "$1" 2>/dev/null | head -n1); i=$((i+1)); done
    echo "$wid"
}
wait_exit() {     # <name> <seconds>
    i=0; while [ $i -lt $(($2 * 5)) ] && [ ! -f "$work/run-$1/exit" ]; do sleep 0.2; i=$((i+1)); done
    [ -f "$work/run-$1/exit" ] && cat "$work/run-$1/exit"
}
origin() { eval "$(xdotool getwindowgeometry --shell "$1")"; ox=$X; oy=$Y; }
click() { xdotool mousemove $((ox + $1)) $((oy + $2)); sleep 0.25; xdotool click 1; sleep 0.6; }

# ---- launcher ----------------------------------------------------------------------------------------------------------------
if [ -x "$bin/OkaySpace" ]; then
    mkdir -p "$work/run-launcher"; cp "$bin/OkaySpace" "$work/run-launcher/OkaySpace"
    printf 'win_w=1160\nwin_h=752\n' > "$work/run-launcher/launcher.cfg"
    EXTRA_ENV="OKAY_LAUNCHER_FRAME_STATS=1500 OKAY_LAUNCHER_FRAME_STATS_EXIT=1" start launcher "$work/run-launcher/OkaySpace"
    wid=$(wait_window "OkaySpace Launcher")
    if [ -z "$wid" ]; then echo "FAIL: no launcher window"; tail -n 20 "$work/run-launcher/log.txt"; fail=1
    else
        sleep 2; origin "$wid"; xdotool windowfocus "$wid" 2>/dev/null
        import -window root "$out/launcher-1-create.png"
        click 650 442; xdotool type --delay 80 "abc"; sleep 1          # the Create page's project search box
        import -window root "$out/launcher-2-search.png"
        click 60 150; sleep 1.5                                        # the sidebar's Play
        import -window root "$out/launcher-3-play.png"
        code=$(wait_exit launcher 180)
        echo "launcher: exit code ${code:-none} (it exits by itself after 1500 frames)"
        grep -E "launcher: (renderer|no SDL)|frame time" "$work/run-launcher/log.txt" | head -n 4
        if [ "${code:-}" != 0 ]; then echo "FAIL: the launcher did not exit cleanly"; tail -n 20 "$work/run-launcher/log.txt"; fail=1
        else echo "PASS: launcher started, took the clicks and typing, exited cleanly"; fi
    fi
fi

# ---- player ------------------------------------------------------------------------------------------------------------------
if [ -x "$bin/okay-player" ]; then
    EXTRA_ENV="" start player "$bin/okay-player" "$scene"
    wid=""; i=0
    while [ $i -lt 150 ] && [ -z "$wid" ]; do   # the player's window is named after the game; the largest named visible one
        sleep 0.2; best=0
        for w in $(xdotool search --onlyvisible --name '' 2>/dev/null); do
            [ -n "$(xdotool getwindowname "$w" 2>/dev/null)" ] || continue
            eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)" || continue
            [ "$WIDTH" -gt 100 ] && [ $((WIDTH * HEIGHT)) -gt "$best" ] && { best=$((WIDTH * HEIGHT)); wid=$w; }
        done
        i=$((i+1))
    done
    if [ -z "$wid" ]; then echo "FAIL: no player window"; tail -n 20 "$work/run-player/log.txt"; fail=1
    else
        sleep 3; origin "$wid"; xdotool windowfocus "$wid" 2>/dev/null
        import -window root "$out/player-1-start.png"
        xdotool keydown w; sleep 2; xdotool keyup w
        xdotool mousemove_relative -- 60 10; sleep 0.3; xdotool click 1; sleep 0.3
        xdotool key F3; sleep 1.5
        import -window root "$out/player-2-played.png"
        "$work/xclose" "$wid"
        code=$(wait_exit player 60)
        echo "player: exit code ${code:-none} (after the window's close button)"
        grep -E "stack:|painter" "$work/run-player/log.txt" | head -n 3
        if [ "${code:-}" != 0 ]; then echo "FAIL: the player did not exit cleanly"; tail -n 20 "$work/run-player/log.txt"; fail=1
        else echo "PASS: player started, played, closed cleanly"; fi
    fi
    [ -f "$work/run-player/pid" ] && kill -9 "$(cat "$work/run-player/pid")" 2>/dev/null
fi

# ---- editor ------------------------------------------------------------------------------------------------------------------
if [ -x "$bin/OkayEngine" ]; then
    EXTRA_ENV="OKAY_DEV=1" start editor "$bin/OkayEngine" --dev --open "$scene"
    wid=$(wait_window "OkayEngine|OkaySpace Editor")
    if [ -z "$wid" ]; then echo "FAIL: no editor window"; tail -n 30 "$work/run-editor/log.txt"; fail=1
    else
        sleep 14; origin "$wid"; xdotool windowfocus "$wid" 2>/dev/null
        import -window root "$out/editor-1-start.png"
        click 697 590          # the Scene view
        click 100 434          # a Hierarchy row
        click 130 97           # the Hierarchy search box
        xdotool type --delay 120 crate; sleep 2
        import -window root "$out/editor-2-input.png"
        xdotool mousemove $((ox + 697)) $((oy + 700)); sleep 0.3
        xdotool key ctrl+p; sleep 4
        import -window root "$out/editor-3-playing.png"
        xdotool key ctrl+p; sleep 3
        "$work/xclose" "$wid"
        code=$(wait_exit editor 60)
        echo "editor: exit code ${code:-none} (after the window's close button)"
        grep -E "^editor: (no SDL|platform|renderer)" "$work/run-editor/log.txt" | head -n 4
        if [ "${code:-}" != 0 ]; then echo "FAIL: the editor did not exit cleanly"; tail -n 20 "$work/run-editor/log.txt"; fail=1
        else echo "PASS: editor started, took the clicks, typing and Play / Stop, closed cleanly"; fi
    fi
fi

[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME CHECKS FAILED"
exit $fail
