#!/bin/sh
# The launcher's two renderers (OKAY_LAUNCHER_RENDERER=sdl, the default, and own) side by side under Xvfb:
#   1. screenshots of the Create page with each, at the default size (1160 x 752) and at a small size (760 x 500),
#      and the pixel difference between sdl and own;
#   2. own only: a click into the Create page's search box and typing "abc" reach ImGui (the input log) and change the
#      picture, and a click on "Play" in the sidebar opens the Play page;
#   3. the median frame time over N frames (default 300) on the Create page, sdl and own, vsync off.
#
#   scripts/launcher_renderer_check.sh <path to the built OkaySpace launcher> <output folder> [frames]
#
# Needs Xvfb, xdotool and ImageMagick (import, compare, convert). The network is blocked for the launcher (a proxy on a
# closed port) so nothing outside changes the picture. Each run gets its own folder (launcher.cfg sits next to the exe).
# Exit 77 when a tool is missing, 1 when a check fails.
set -u
exe=${1:?launcher binary}
out=${2:?output folder}
frames=${3:-300}
for t in Xvfb xdotool import compare convert; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed"; exit 77; }
done
mkdir -p "$out"
work=$(mktemp -d) || exit 1
disp=:$((60 + $$ % 30))
Xvfb "$disp" -screen 0 1600x1000x24 -nolisten tcp >"$work/xvfb.log" 2>&1 &
xpid=$!
cleanup() {
    for p in "$work"/run-*/pid; do [ -f "$p" ] && kill -9 "$(cat "$p")" 2>/dev/null; done
    kill "$xpid" 2>/dev/null; rm -rf "$work"
}
trap cleanup EXIT INT TERM
sleep 1
export DISPLAY="$disp" LIBGL_ALWAYS_SOFTWARE=1
fail=0

# start <name> <renderer> <w> <h> [extra env...]: a fresh copy of the launcher in its own folder; prints the window id.
start() {
    name=$1; rend=$2; w=$3; h=$4; shift 4
    dir="$work/run-$name"; mkdir -p "$dir/home"
    cp "$exe" "$dir/OkaySpace"
    printf 'win_w=%s\nwin_h=%s\n' "$w" "$h" > "$dir/launcher.cfg"
    ( cd "$dir" && exec env HOME="$dir/home" OKAY_UI_DEMO_TAB=0 OKAY_UPDATE_ON_LAUNCH=0 OKAY_ACCOUNT_SERVER=http://127.0.0.1:9 \
        HTTPS_PROXY=http://127.0.0.1:9 HTTP_PROXY=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 http_proxy=http://127.0.0.1:9 \
        OKAY_LAUNCHER_RENDERER="$rend" OKAY_LAUNCHER_INPUT_LOG=1 "$@" "$dir/OkaySpace" > "$dir/log.txt" 2>&1 ) </dev/null >/dev/null 2>&1 &
    echo $! > "$dir/pid"
    i=0; wid=""
    while [ $i -lt 100 ] && [ -z "$wid" ]; do sleep 0.2; wid=$(xdotool search --onlyvisible --name "OkaySpace Launcher" 2>/dev/null | head -n1); i=$((i+1)); done
    echo "$wid"
}
# stop <name>: end that run's launcher (only the process this script started) and wait for its window to go.
stop() {
    [ -f "$work/run-$1/pid" ] && kill -9 "$(cat "$work/run-$1/pid")" 2>/dev/null   # -9: a plain TERM opens the Exit / Minimize dialog
    i=0; while [ $i -lt 50 ] && xdotool search --onlyvisible --name "OkaySpace Launcher" >/dev/null 2>&1; do sleep 0.2; i=$((i+1)); done
}

for size in normal small; do
    if [ "$size" = normal ]; then w=1160; h=752; else w=760; h=500; fi
    for rend in sdl own; do
        wid=$(start "$rend-$size" "$rend" "$w" "$h")
        if [ -z "$wid" ]; then echo "FAIL: no window ($rend, $size)"; cat "$work/run-$rend-$size/log.txt"; fail=1; continue; fi
        xdotool mousemove 1590 990   # the pointer away from the window: no hover highlight
        sleep 4
        import -window "$wid" "$out/launcher-painter-$rend-$size.png"
        grep "launcher: renderer" "$work/run-$rend-$size/log.txt"
        stop "$rend-$size"
    done
    a="$out/launcher-painter-sdl-$size.png"; b="$out/launcher-painter-own-$size.png"
    if [ -f "$a" ] && [ -f "$b" ]; then
        total=$(convert "$a" -format '%[fx:w*h]' info:)
        ae=$(compare -metric AE "$a" "$b" "$out/launcher-painter-diff-$size.png" 2>&1 >/dev/null)
        ae8=$(compare -metric AE -fuzz 3% "$a" "$b" null: 2>&1 >/dev/null)
        mae=$(compare -metric MAE "$a" "$b" null: 2>&1 >/dev/null)
        echo "pixel difference sdl vs own ($size, $total pixels): $ae differ at all, $ae8 differ by more than 3%, mean absolute error $mae"
    fi
done

# ---- input with the own renderer ----
wid=$(start input own 1160 752)
if [ -z "$wid" ]; then echo "FAIL: no window for the input check"; fail=1; else
    sleep 3
    eval "$(xdotool getwindowgeometry --shell "$wid")"   # X, Y
    import -window "$wid" "$work/before.png"
    xdotool mousemove $((X + 650)) $((Y + 442)) click 1   # the Create page's project search box
    sleep 0.6
    xdotool type --delay 80 "abc"
    sleep 1
    import -window "$wid" "$work/typed.png"
    log="$work/run-input/log.txt"
    if grep -q 'launcher-input: mouse 0 down' "$log" && grep -q 'launcher-input: text "a"' "$log" && grep -q 'launcher-input: text "c"' "$log"; then
        echo "input: the click and the typing reached ImGui"
    else echo "FAIL: the click or the typing did not reach ImGui"; cat "$log"; fail=1; fi
    changed=$(compare -metric AE -fuzz 10% -extract 520x30+400+427 "$work/before.png" "$work/typed.png" null: 2>&1 >/dev/null)
    echo "input: pixels changed in the search box after typing: $changed"
    [ "${changed%%.*}" -gt 20 ] 2>/dev/null || { echo "FAIL: the search box did not change"; fail=1; }
    xdotool mousemove $((X + 60)) $((Y + 145)) click 1    # "Play" in the sidebar
    sleep 1.5
    import -window "$wid" "$work/play.png"
    cp "$work/typed.png" "$out/launcher-painter-own-typed.png"; cp "$work/play.png" "$out/launcher-painter-own-play.png"
    changed=$(compare -metric AE -fuzz 10% -extract 300x60+250+20 "$work/typed.png" "$work/play.png" null: 2>&1 >/dev/null)
    echo "input: pixels changed in the page title after clicking Play: $changed"
    [ "${changed%%.*}" -gt 50 ] 2>/dev/null || { echo "FAIL: clicking Play did not change the page"; fail=1; }
    stop input
fi

# ---- frame time ----
for rend in sdl own; do
    dir="$work/run-ft-$rend"
    wid=$(start "ft-$rend" "$rend" 1160 752 OKAY_LAUNCHER_FRAME_STATS="$frames" OKAY_LAUNCHER_FRAME_STATS_EXIT=1 OKAY_LAUNCHER_VSYNC=0)
    xdotool mousemove 1590 990
    i=0
    while [ $i -lt 600 ] && ! grep -q "launcher: frame time" "$dir/log.txt"; do sleep 0.2; i=$((i+1)); done
    grep "launcher: frame time" "$dir/log.txt" || { echo "FAIL: no frame time ($rend)"; cat "$dir/log.txt"; fail=1; }
    stop "ft-$rend"
done
exit $fail
