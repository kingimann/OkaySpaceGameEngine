#!/bin/sh
# End-to-end checks of the real editor on a private Xvfb display (shared/TestHooks.hpp scripts; the editor must be built with
# -DOKAY_ENABLE_TEST_HOOKS=ON, never a release build). Not run by CI (CI builds no editor with test hooks); run it by hand:
#   tests/scripts/test_editor_session.sh path/to/OkayEngine [work-dir]
# 1. quit with nothing unsaved closes at once; 2. quit with unsaved work asks, Cancel keeps it open, Don't Save closes;
# 3. the dock layout and the open panels are written to the per-user folder and read back on the next start;
# 4. without a confirmed OkaySpace sign-in the editor shows the sign-in window and opens no project; 5. a saved session the
# server cannot confirm is blocked too ("Can't reach the OkaySpace server", no offline grace).
# Pictures go to the work folder (outside the repository). Exit 0 = all passed.
ed=$1; work=${2:-/tmp/okay_editor_session}
[ -x "$ed" ] || { echo "usage: $0 <OkayEngine built with test hooks> [work-dir]" >&2; exit 2; }
command -v Xvfb >/dev/null || { echo "needs Xvfb" >&2; exit 77; }
ed="$(cd "$(dirname "$ed")" && pwd)/$(basename "$ed")"
rm -rf "$work"; mkdir -p "$work" || exit 2
fails=0
gate=OKAY_TEST_NO_SIGNIN=1   # the sign-in gate is off except in checks 4 and 5
extra=OKAY_TEST_EXTRA=0      # one more variable for a run (a harmless placeholder when none)
Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$work/disp" >/dev/null 2>&1 &
xp=$!
i=0; while [ ! -s "$work/disp" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
disp=":$(head -n1 "$work/disp" | tr -d '\n')"
# run NAME SCRIPT-TEXT [args...]: one editor run in $work/NAME with the per-user folder $work/cfg; sets $code
run() {
    name=$1; shift; text=$1; shift
    mkdir -p "$work/$name" "$work/home"
    printf '%s\n' "$text" > "$work/$name/script.txt"
    ( cd "$work/$name" && env DISPLAY="$disp" HOME="$work/home" XDG_CONFIG_HOME="$work/home/config" OKAY_ALSA_DEVICE=null LIBGL_ALWAYS_SOFTWARE=1 \
        OKAY_UPDATE_ON_LAUNCH=0 OKAY_EDITOR_CONFIG_DIR="$work/cfg" OKAY_TEST_SCRIPT="$work/$name/script.txt" \
        $gate $extra timeout -s KILL 120 "$ed" --dev "$@" >"$work/$name/app.log" 2>&1 )
    code=$?
}
check() { if [ "$1" = 0 ]; then echo "ok:   $2"; else echo "FAIL: $2"; fails=$((fails+1)); fi; }

# 1. A new project (saved by the editor as it is made): the close button quits at once (exit 0, before the script's `exit 7`).
run clean "6000 close
20000 exit 7" --new-project "$work/projects/Clean"
check $([ "$code" = 0 ] && echo 0 || echo 1) "nothing unsaved: the close button quits without asking (exit $code)"
# 3a. ...and on the way out it left the layout and the open panels in the per-user folder.
check $([ -s "$work/cfg/imgui.ini" ] && grep -q '^\[Docking\]' "$work/cfg/imgui.ini" && echo 0 || echo 1) "the dock layout is saved to the per-user folder (cfg/imgui.ini)"
check $(grep -q '^panel_console [01]$' "$work/cfg/okay_settings.txt" 2>/dev/null && echo 0 || echo 1) "the open panels are saved (okay_settings.txt panel_ lines)"
check $([ ! -e "$work/clean/imgui.ini" ] && [ ! -e "$work/clean/okay_settings.txt" ] && echo 0 || echo 1) "nothing is written to the working folder"

# 3b. Open the Profiler and close the Console in the saved settings: the next start shows that, and writes it back the same.
sed -i 's/^panel_profiler 0$/panel_profiler 1/; s/^panel_console 1$/panel_console 0/' "$work/cfg/okay_settings.txt"
scene=$(find "$work/projects/Clean" -name '*.okayscene' | head -n1)
run reopen "5000 shot reopen.png
6000 close
20000 exit 7" --open "$scene"
check $([ "$code" = 0 ] && echo 0 || echo 1) "the second start opened and closed (exit $code)"
check $(grep -q '^panel_profiler 1$' "$work/cfg/okay_settings.txt" && grep -q '^panel_console 0$' "$work/cfg/okay_settings.txt" && echo 0 || echo 1) "the open and closed panels came back (Profiler open, Console closed)"
check $(grep -q '^\[Docking\]' "$work/cfg/imgui.ini" && echo 0 || echo 1) "the dock layout is still there after the second run"

# 2. Unsaved work (an untitled scene, no project): the close button asks; Cancel keeps it open; Don't Save closes.
extra=OKAY_ALLOW_NO_PROJECT=1
run dirty "4000 close
5000 shot prompt.png
5500 click 611 460
7000 shot after_cancel.png
8000 close
9000 shot prompt_again.png
9500 click 743 460
20000 exit 7"
extra=OKAY_TEST_EXTRA=0
check $([ "$code" = 0 ] && echo 0 || echo 1) "unsaved work: asked, Cancel kept it open, Don't Save quit (exit $code)"
check $(grep -c "\[test-hook\].*close" "$work/dirty/app.log" | grep -q '^2$' && echo 0 || echo 1) "both close requests arrived (the first was answered with Cancel)"

# 4. The sign-in gate (no OKAY_TEST_NO_SIGNIN, no saved session in the private home): the sign-in window, no project opened.
gate=OKAY_TEST_NO_SIGNIN=0
run gate "5000 shot gate.png
6000 exit 7" --open "$scene"
gate=OKAY_TEST_NO_SIGNIN=1
check $([ "$code" = 7 ] && echo 0 || echo 1) "gate run ended by the script (exit $code)"
check $(grep -q "Opened " "$work/gate/app.log" && echo 1 || echo 0) "no project opened before signing in"

# 5. A saved session the server cannot confirm (nothing listens on port 1): "Can't reach the OkaySpace server", still blocked.
gate=OKAY_TEST_NO_SIGNIN=0
mkdir -p "$work/home/config/OkaySpace"; printf 'tester\nsaved.token.x\nrefresh\n' > "$work/home/config/OkaySpace/session.txt"
extra=OKAY_ACCOUNT_SERVER=http://127.0.0.1:1
run offline "6000 shot offline.png
7000 exit 7" --open "$scene"
extra=OKAY_TEST_EXTRA=0
gate=OKAY_TEST_NO_SIGNIN=1
check $([ "$code" = 7 ] && echo 0 || echo 1) "offline run ended by the script (exit $code)"
check $(grep -q "Opened " "$work/offline/app.log" && echo 1 || echo 0) "an unconfirmed session opens no project (no offline grace)"

kill $xp 2>/dev/null; wait $xp 2>/dev/null
echo "pictures: $work/*/ ; $fails failed"
[ $fails = 0 ]
