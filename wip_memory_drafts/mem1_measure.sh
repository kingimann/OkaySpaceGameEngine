#!/bin/sh
# mem1_measure.sh NAME PROGRAM SCRIPTTEXT [args...]
# Runs PROGRAM on a private Xvfb with a test script; samples /proc/PID/status at 15 s (RSS, RssAnon) and the CPU used
# between 15 s and 25 s (idle window); the script should `exit 0` after ~27 s. Writes OUT/NAME.{log,txt}.
name=$1; prog=$2; text=$3; shift 3
OUT=${OUT:-/tmp/claude-0/-home-user-OkaySpaceGameEngine/87100424-1153-58f2-babb-92d29de83134/scratchpad/mem1_out}
work=$OUT/work_$name; rm -rf "$work"; mkdir -p "$work/home" "$work/app" "$OUT"
printf '%s\n' "$text" > "$work/script.txt"
Xvfb -displayfd 3 -screen 0 1600x900x24 -nolisten tcp 3>"$work/disp" >/dev/null 2>&1 &
xp=$!
i=0; while [ ! -s "$work/disp" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
disp=":$(head -n1 "$work/disp" | tr -d '\n')"
cp "$prog" "$work/app/"; [ -n "$APP_SEED" ] && cp -R "$APP_SEED"/. "$work/app/"
cprog="$work/app/$(basename "$prog")"
( cd "$work" && exec env DISPLAY="$disp" HOME="$work/home" XDG_CONFIG_HOME="$work/home/config" XDG_RUNTIME_DIR="$work/home" XDG_DATA_HOME="$work/home/data" \
    SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 OKAY_UPDATE_ON_LAUNCH=0 OKAY_TEST_NO_SIGNIN=1 OKAY_PLATFORM_BACKEND=${BACKEND:-sdl2} \
    OKAY_EDITOR_CONFIG_DIR="$work/cfg" OKAY_TEST_SCRIPT="$work/script.txt" OKAY_MEMORY_REPORT=1 $EXTRA_ENV \
    timeout -s KILL 120 "$cprog" "$@" >"$OUT/$name.log" 2>&1 ) &
pp=$!
sleep 1
pid=$(pgrep -n -f "$cprog")
sleep ${T1:-14}
st() { awk '/^(VmRSS|RssAnon|RssFile|VmHWM)/{printf "%s %s ", $1, $2}' /proc/$pid/status; }
cpu() { awk '{print $14+$15}' /proc/$pid/stat; }
s1=$(st); c1=$(cpu); [ -n "$SMAPS" ] && python3 /tmp/claude-0/-home-user-OkaySpaceGameEngine/87100424-1153-58f2-babb-92d29de83134/scratchpad/mem1_smaps.py $pid > "$OUT/$name.smaps" 2>&1
sleep ${IDLE:-10}
c2=$(cpu); s2=$(st)
tck=$(getconf CLK_TCK)
wait $pp; code=$?
kill $xp 2>/dev/null; wait $xp 2>/dev/null
pct=$(awk -v a="$c1" -v b="$c2" -v t="$tck" -v s="${IDLE:-10}" 'BEGIN{printf "%.1f", (b-a)/t/s*100}')
echo "$name exit=$code at15s: $s1 | at25s: $s2 | idle CPU ${pct}% of one core" | tee "$OUT/$name.txt"
