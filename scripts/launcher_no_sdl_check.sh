#!/bin/sh
# Proof that the launcher makes NO SDL call with OKAY_LAUNCHER_RENDERER=own and OKAY_PLATFORM_BACKEND=own (Linux, Xvfb).
#
# How: a tiny "trap" library is built from the list of functions libSDL2 exports (nm -D on the very libSDL2 the launcher
# loads). It defines every one of them; each writes "SDL CALL: <name>" and ends the process with exit code 97. It is put
# in front of libSDL2 with LD_PRELOAD, so any call the launcher (or Dear ImGui's SDL backends linked into it) makes into
# SDL lands in the trap instead. libSDL2 itself is still loaded (it is a link dependency); loading is not a call.
#
#   1. control: the default renderer (sdl) under the trap must stop at its first SDL call (shows the trap works);
#   2. own + own, a clean run: the Create page for N frames, then the launcher exits by itself (OKAY_LAUNCHER_FRAME_STATS
#      with _EXIT), so start-up and shut-down are both covered; exit code 0 and no "SDL CALL" line;
#   3. own + own, the busy run: the chat in its own window (saved off screen, so the display list must bring it back), a
#      voice clip playing, a voice call (microphone capture and playback on ALSA's null device), a click on Play, typing
#      in the search box; screenshots of both windows; still alive at the end with no "SDL CALL" line.
#
#   scripts/launcher_no_sdl_check.sh <path to the built OkaySpace launcher> <output folder> [frames]
#
# Needs Xvfb, xdotool, ImageMagick (import), cc and nm. Exit 77 when a tool is missing or the launcher does not load a
# shared libSDL2 (then there is nothing to trap), 1 when a check fails.
set -u
exe=${1:?launcher binary}
out=${2:?output folder}
frames=${3:-120}
for t in Xvfb xdotool import cc nm ldd; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed"; exit 77; }
done
exe=$(cd "$(dirname "$exe")" && pwd)/$(basename "$exe")
lib=$(ldd "$exe" | awk '/libSDL2/ {print $3; exit}')
if [ -z "$lib" ] || [ ! -f "$lib" ]; then echo "SKIP: the launcher does not load a shared libSDL2"; exit 77; fi
mkdir -p "$out"
work=$(mktemp -d) || exit 1
disp=:$((30 + $$ % 25))
Xvfb "$disp" -screen 0 2400x1000x24 -nolisten tcp >"$work/xvfb.log" 2>&1 &
xpid=$!
cleanup() {
    for p in "$work"/run-*/pid; do [ -f "$p" ] && kill -9 "$(cat "$p")" 2>/dev/null; done
    for d in "$work"/run-*; do [ -f "$d/log.txt" ] && cp "$d/log.txt" "$out/no-sdl-log-$(basename "$d" | sed 's/^run-//').txt"; done
    sleep 1   # let the killed launchers go before their folders are removed
    kill "$xpid" 2>/dev/null; rm -rf "$work"
}
trap cleanup EXIT INT TERM
fail=0

# ---- the trap library --------------------------------------------------------------------------------------------
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

export DISPLAY="$disp" LIBGL_ALWAYS_SOFTWARE=1
# start <name> <renderer> <platform backend> [extra env...]: a fresh copy of the launcher in its own folder, under the trap.
start() {
    name=$1; rend=$2; pb=$3; shift 3
    dir="$work/run-$name"; mkdir -p "$dir/home"
    cp "$exe" "$dir/OkaySpace"
    [ -f "$work/cfg-$name" ] && cp "$work/cfg-$name" "$dir/launcher.cfg"
    [ -f "$dir/launcher.cfg" ] || printf 'win_w=1160\nwin_h=752\n' > "$dir/launcher.cfg"
    # The subshell records the launcher's own pid (env execs it) and, when it ends, its exit code.
    ( cd "$dir" || exit 1
      env HOME="$dir/home" TMPDIR="$dir" OKAY_UI_DEMO_TAB=0 OKAY_UPDATE_ON_LAUNCH=0 OKAY_ACCOUNT_SERVER=http://127.0.0.1:9 \
        HTTPS_PROXY=http://127.0.0.1:9 HTTP_PROXY=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 http_proxy=http://127.0.0.1:9 \
        OKAY_LAUNCHER_RENDERER="$rend" OKAY_PLATFORM_BACKEND="$pb" OKAY_SDL_TRAP_LOG="$dir/trap.txt" \
        LD_PRELOAD="$work/libsdltrap.so" "$@" "$dir/OkaySpace" > "$dir/log.txt" 2>&1 &
      echo $! > "$dir/pid.tmp"; mv "$dir/pid.tmp" "$dir/pid"
      wait $!; echo $? > "$dir/exit.tmp"; mv "$dir/exit.tmp" "$dir/exit" ) </dev/null >/dev/null 2>&1 &
}
wait_window() {   # <title> -> window id or ""
    i=0; wid=""
    while [ $i -lt 100 ] && [ -z "$wid" ]; do sleep 0.2; wid=$(xdotool search --onlyvisible --name "$1" 2>/dev/null | head -n1); i=$((i+1)); done
    echo "$wid"
}
wait_exit() {     # <name> <seconds>: wait for that run's exit code file
    i=0; while [ $i -lt $(($2 * 5)) ] && [ ! -f "$work/run-$1/exit" ]; do sleep 0.2; i=$((i+1)); done
    [ -f "$work/run-$1/exit" ] && cat "$work/run-$1/exit"
}

# ---- 1. control: the sdl renderer must hit the trap ----------------------------------------------------------------
start control sdl sdl2
code=$(wait_exit control 30)
first=$(head -n1 "$work/run-control/trap.txt" 2>/dev/null)
echo "control (sdl renderer, sdl2 backend): exit code ${code:-none}, first trapped call: ${first:-none}"
if [ "${code:-}" != 97 ] || [ -z "$first" ]; then echo "FAIL: the trap did not catch the sdl renderer's SDL calls"; fail=1; fi

# ---- 2. own + own, clean start and exit ----------------------------------------------------------------------------
start clean own own OKAY_LAUNCHER_FRAME_STATS="$frames" OKAY_LAUNCHER_FRAME_STATS_EXIT=1
wid=$(wait_window "OkaySpace Launcher")
if [ -n "$wid" ]; then xdotool mousemove 2390 990; sleep 2; import -window "$wid" "$out/no-sdl-create.png"; fi
code=$(wait_exit clean 120)
echo "own + own, clean run ($frames frames, then exit): exit code ${code:-none} (still running if none)"
grep -E "launcher: (renderer|no SDL)|frame time" "$work/run-clean/log.txt"
if [ -s "$work/run-clean/trap.txt" ] || grep -q "SDL CALL" "$work/run-clean/log.txt"; then
    echo "FAIL: SDL was called:"; cat "$work/run-clean/trap.txt"; fail=1
elif [ "${code:-}" != 0 ]; then echo "FAIL: the launcher did not exit cleanly"; tail -n 20 "$work/run-clean/log.txt"; fail=1
else echo "PASS: no SDL call from start to exit"; fi
[ -f "$work/run-clean/pid" ] && kill -9 "$(cat "$work/run-clean/pid")" 2>/dev/null

# ---- 3. own + own, the busy run --------------------------------------------------------------------------------------
# The chat window's saved place is far off screen: KeepWindowOnScreen (the display list) must bring it back.
printf 'win_w=1160\nwin_h=752\nchat_win_x=5000\nchat_win_y=3000\nchat_win_w=600\nchat_win_h=480\n' > "$work/cfg-busy"
start busy own own OKAY_CHAT_DEMO=1 OKAY_CHAT_DEMO_PLAY=1 OKAY_VOICE_DEMO=127.0.0.1:9 OKAY_ALSA_DEVICE=null
wid=$(wait_window "OkaySpace Launcher")
if [ -z "$wid" ]; then echo "FAIL: no launcher window (busy run)"; cat "$work/run-busy/log.txt"; fail=1
else
    sleep 4
    cwid=$(wait_window "OkaySpace Chat")
    if [ -n "$cwid" ]; then
        eval "$(xdotool getwindowgeometry --shell "$cwid")"
        echo "chat window (saved at 5000,3000) is at $X,$Y size ${WIDTH}x$HEIGHT on a 2400x1000 screen"
        if [ "$X" -ge 2400 ] || [ "$Y" -ge 1000 ] || [ "$X" -lt 0 ] || [ "$Y" -lt 0 ]; then echo "FAIL: the chat window was left off screen"; fail=1; fi
        import -window "$cwid" "$out/no-sdl-chat.png"
        xdotool windowmove "$cwid" 1790 40; sleep 1   # beside the launcher: its screenshots then show only the launcher
    else echo "NOTE: no chat window appeared"; fi
    eval "$(xdotool getwindowgeometry --shell "$wid")"
    lx=$X; ly=$Y
    xdotool windowfocus "$wid" 2>/dev/null   # no window manager here: the keyboard focus does not follow the click by itself
    xdotool mousemove $((lx + 650)) $((ly + 442)) click 1;  # the Create page's project search box
    sleep 0.5; xdotool type --delay 80 "abc"; sleep 1
    import -window "$wid" "$out/no-sdl-search.png"
    xdotool mousemove $((lx + 60)) $((ly + 150)) click 1; sleep 1.5   # the sidebar's Play
    xdotool mousemove 2390 990; sleep 1
    import -window "$wid" "$out/no-sdl-play.png"
    sleep 2
    # Our own audio is ALSA loaded at run time: the voice call's microphone and speaker devices load libasound.
    if grep -q libasound "/proc/$(cat "$work/run-busy/pid")/maps" 2>/dev/null; then echo "audio: libasound is loaded (our ALSA devices for the call and the clip)"
    else echo "NOTE: libasound is not loaded (no audio device was opened)"; fi
    if [ -f "$work/run-busy/exit" ]; then echo "FAIL: the launcher ended during the busy run (exit $(cat "$work/run-busy/exit"))"; fail=1; fi
    grep -E "launcher: (renderer|no SDL)|voice|audio|ALSA" "$work/run-busy/log.txt" | head -n 12
    if [ -s "$work/run-busy/trap.txt" ] || grep -q "SDL CALL" "$work/run-busy/log.txt"; then
        echo "FAIL: SDL was called:"; cat "$work/run-busy/trap.txt"; fail=1
    else echo "PASS: no SDL call during the busy run (chat window, voice clip, voice call, clicks, typing)"; fi
fi

[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME CHECKS FAILED"
exit $fail
