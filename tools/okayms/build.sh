#!/bin/sh
# Standalone build for tools/okayms. The engine CMake build does not call this.
# Usage, from anywhere: tools/okayms/build.sh
# SAN=1 turns on AddressSanitizer and UndefinedBehaviorSanitizer.
# One file is compiled at a time (disk and CPU stay small).
set -eu
cd "$(dirname "$0")"
ROOT=$(CDPATH= cd ../.. && pwd)
CXX=${CXX:-g++}

rm -rf build
mkdir -p build

cc() {
  if [ "${SAN:-0}" = "1" ]; then
    # shellcheck disable=SC2086
    "$CXX" -std=c++17 -Wall -Wextra -Werror -g -I include \
      -fsanitize=address,undefined -fno-omit-frame-pointer "$@"
  else
    # shellcheck disable=SC2086
    "$CXX" -std=c++17 -Wall -Wextra -Werror -g -I include "$@"
  fi
}

objs=""
if [ -d src ]; then
  for s in src/*.cpp; do
    [ -f "$s" ] || continue
    base=$(basename "$s" .cpp)
    cc -c "$s" -o "build/${base}.o"
    objs="$objs build/${base}.o"
  done
fi

if [ -d cmd ]; then
  for c in cmd/*.cpp; do
    [ -f "$c" ] || continue
    base=$(basename "$c" .cpp)
    # shellcheck disable=SC2086
    cc "$c" $objs -o "build/${base}"
  done
fi

fail=0
ran=0
for t in tests/test_*.cpp; do
  [ -f "$t" ] || continue
  name=$(basename "$t" .cpp)
  # shellcheck disable=SC2086
  cc "$t" $objs -o "build/${name}"
  ran=$((ran + 1))
  echo "== ${name} =="
  if ! "build/${name}" "$ROOT"; then
    fail=1
  fi
done

if [ "$ran" -eq 0 ]; then
  echo "no tests found" >&2
  exit 1
fi
exit "$fail"
