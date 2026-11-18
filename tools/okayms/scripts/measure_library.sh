#!/bin/sh
# Compiles every engine/src/**/*.cpp to an object with okay-cl, one file at a time, and records the first error of each.
# Usage: tools/okayms/scripts/measure_library.sh [results-file]   (from the repo root; default tools/okayms/results/library.txt)
# - Each file has a 60 second limit (TIMEOUT_S to change). A result line "path|OK", "path|TIMEOUT 60s" or "path|first error"
#   is appended to the results file as soon as the file is done.
# - Resumable: files already in the results file are skipped, so a cut-off run continues where it stopped.
# - At the end (or with SUMMARY_ONLY=1) it prints the table of first errors.
# Run it niced in the background on a busy machine:  nice -n 10 tools/okayms/scripts/measure_library.sh &
set -u
ROOT=$(CDPATH= cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"
RES=${1:-tools/okayms/results/library.txt}
CL=${OKAY_CL:-tools/okayms/build/okay-cl}
# A private copy of the compiler: rebuilding it while the run is going does not change the results.
SNAP=$(mktemp /tmp/measure_cl_XXXXXX)
cp "$CL" "$SNAP"
CL=$SNAP
LIMIT=${TIMEOUT_S:-60}
OBJ=$(mktemp -u /tmp/measure_XXXXXX.obj)
touch "$RES"
if [ "${SUMMARY_ONLY:-0}" != 1 ]; then
  find engine/src -name '*.cpp' | sort | while read -r f; do
    grep -q "^$f|" "$RES" && continue
    out=$(timeout "$LIMIT" "$CL" /c "/Fo$OBJ" "$f" /I engine/include /I engine/src /I shared /I tests /I tools/okayms/stdinc 2>&1)
    rc=$?
    if [ $rc -eq 124 ]; then r="TIMEOUT ${LIMIT}s"
    elif [ $rc -ne 0 ]; then r=$(printf '%s\n' "$out" | head -1 | sed 's/okay-cl: [^:]*: //' | cut -c1-180)
    else r=OK; fi
    rm -f "$OBJ"
    echo "$f|$r" >> "$RES"
  done
fi
rm -f "$SNAP"
total=$(wc -l < "$RES")
ok=$(grep -c '|OK$' "$RES")
echo "files: $total, compile: $ok, fail: $((total - ok))"
grep -v '|OK$' "$RES" | cut -d'|' -f2 | sed 's/^[^ ]*:[0-9]*: //;s/(in .*//;s/\x27[^\x27]*\x27//g' | cut -c1-60 | sort | uniq -c | sort -rn | head -25
