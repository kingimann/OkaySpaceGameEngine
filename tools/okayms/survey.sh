#!/bin/sh
# Survey: how many engine/src/**/*.cpp files okay-cl compiles to an object, and where the others stop first.
# Usage, from anywhere: tools/okayms/survey.sh [okay-cl binary]
# The command per file is the one of Rounds 23 to 26:
#   okay-cl /c <file> /I engine/include /I engine/src /I shared /I tools/okayms/stdinc
# A file counts only when okay-cl exits 0 and writes the object; each file has a 300-second limit (SURVEY_LIMIT).
# Two files are compiled at a time (SURVEY_JOBS; the machine is shared). Results go to $SURVEY_OUT (default
# /tmp/okayms-survey): ok.txt (the files that compile), errors.txt (file<TAB>first error), and the summary is printed.
# No compiler is built when one is given; otherwise the src/ and cmd/okay-cl.cpp files are built into $SURVEY_OUT.
# SURVEY_FLAGS adds flags to every compile (for example "/D_WIN32 /D_WIN64": okay-cl does not define _WIN32 itself, so
# by default the engine files are compiled on their non-Windows paths).
set -eu
HERE=$(CDPATH= cd "$(dirname "$0")" && pwd)
ROOT=$(CDPATH= cd "$HERE/../.." && pwd)
OUT=${SURVEY_OUT:-/tmp/okayms-survey}
JOBS=${SURVEY_JOBS:-2}
LIMIT=${SURVEY_LIMIT:-300}
mkdir -p "$OUT/obj" "$OUT/build"

CL=${1:-}
if [ -z "$CL" ]; then
  objs=""
  for s in "$HERE"/src/*.cpp; do
    o="$OUT/build/$(basename "$s" .cpp).o"
    g++ -std=c++17 -O1 -I "$HERE/include" -c "$s" -o "$o"
    objs="$objs $o"
  done
  # shellcheck disable=SC2086
  g++ -std=c++17 -O1 -I "$HERE/include" "$HERE/cmd/okay-cl.cpp" $objs -o "$OUT/build/okay-cl"
  CL="$OUT/build/okay-cl"
fi

cd "$ROOT"
: > "$OUT/errors.txt"
: > "$OUT/ok.txt"
FLAGS=${SURVEY_FLAGS:-}
export CL OUT LIMIT FLAGS
find engine/src -name '*.cpp' | sort | xargs -P "$JOBS" -I{} sh -c '
  f="$1"
  obj="$OUT/obj/$(echo "$f" | tr / _).obj"
  rm -f "$obj"
  # shellcheck disable=SC2086
  msg=$(timeout "$LIMIT" "$CL" /c "$f" /Fo"$obj" /I engine/include /I engine/src /I shared /I tools/okayms/stdinc $FLAGS 2>&1)
  rc=$?
  if [ "$rc" -eq 0 ] && [ -f "$obj" ]; then
    echo "$f" >> "$OUT/ok.txt"
  else
    [ "$rc" -eq 124 ] && msg="timeout"
    first=$(printf "%s\n" "$msg" | head -n 1 | sed "s|^okay-cl: $f: ||")
    printf "%s\t%s\n" "$f" "$first" >> "$OUT/errors.txt"
  fi
  rm -f "$obj"
' _ {}

total=$(find engine/src -name '*.cpp' | wc -l)
ok=$(wc -l < "$OUT/ok.txt")
sort -o "$OUT/ok.txt" "$OUT/ok.txt"
echo "compiled: $ok of $total"
echo "top first-error places (count, place):"
# The place is the text before the second colon ("file:line"), or the whole message when it has none.
cut -f2 "$OUT/errors.txt" | sed -E 's/^([^:]+:[0-9]+):.*/\1/' | sort | uniq -c | sort -rn | head -n "${SURVEY_TOP:-25}"
