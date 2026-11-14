#!/bin/sh
# Run a test program on a private Xvfb display. Exit 77 (ctest: skipped) when Xvfb
# is not installed or does not start, so machines without it stay green.
#   run_under_xvfb.sh <program> [args...]
if ! command -v Xvfb >/dev/null 2>&1; then echo "SKIP: Xvfb not installed"; exit 77; fi
tmp=$(mktemp -d) || exit 77
trap 'kill "$xpid" 2>/dev/null; rm -rf "$tmp"' EXIT INT TERM
Xvfb -displayfd 3 -screen 0 1024x768x24 -nolisten tcp -noreset 3>"$tmp/display" >"$tmp/xvfb.log" 2>&1 &
xpid=$!
i=0
while [ ! -s "$tmp/display" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
if [ ! -s "$tmp/display" ]; then echo "SKIP: Xvfb did not start"; cat "$tmp/xvfb.log"; exit 77; fi
DISPLAY=":$(head -n1 "$tmp/display" | tr -d '\n')"
export DISPLAY
# A software GL stack is enough (and the only one on a headless box).
export LIBGL_ALWAYS_SOFTWARE=1
"$@"
rc=$?
exit $rc
