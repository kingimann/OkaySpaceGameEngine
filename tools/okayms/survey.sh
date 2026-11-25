#!/bin/sh
# Survey: how many engine/src/**/*.cpp files okay-cl compiles to an object, and where the others stop first.
# Usage, from anywhere: tools/okayms/survey.sh [okay-cl binary]
# The command per file (Round 28; Rounds 23 to 27 had no okayscript/include):
#   okay-cl /c <file> /I engine/include /I engine/src /I shared /I okayscript/include /I tools/okayms/stdinc
# okayscript/include is the include folder CMake gives the engine through its okayscript_lang library.
# A file counts only when okay-cl exits 0 and writes the object; each file has a 300-second limit (SURVEY_LIMIT).
# Two files are compiled at a time (SURVEY_JOBS; the machine is shared). Results go to $SURVEY_OUT (default
# /tmp/okayms-survey): ok.txt (the files that compile), errors.txt (file<TAB>first error), and the summary is printed.
# No compiler is built when one is given; otherwise the src/ and cmd/okay-cl.cpp files are built into $SURVEY_OUT.
# The default run is the Windows survey: from Round 28 okay-cl predefines _WIN32, _WIN64, _M_X64, _M_AMD64 and _MSC_VER
# like the compiler it stands in for. SURVEY_PLAIN=1 is the plain survey (the engine's non-Windows paths): those macros
# are left out with /U. SURVEY_FLAGS adds flags to every compile.
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
if [ "${SURVEY_PLAIN:-0}" = "1" ]; then
  FLAGS="/U_WIN32 /U_WIN64 /U_M_X64 /U_M_AMD64 /U_MSC_VER /U_MSC_FULL_VER /U_MSVC_LANG $FLAGS"
fi
export CL OUT LIMIT FLAGS
find engine/src -name '*.cpp' | sort | xargs -P "$JOBS" -I{} sh -c '
  f="$1"
  obj="$OUT/obj/$(echo "$f" | tr / _).obj"
  rm -f "$obj"
  # shellcheck disable=SC2086
  msg=$(timeout "$LIMIT" "$CL" /c "$f" /Fo"$obj" /I engine/include /I engine/src /I shared /I okayscript/include /I tools/okayms/stdinc $FLAGS 2>&1)
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
