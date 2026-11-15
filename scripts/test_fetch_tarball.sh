#!/usr/bin/env bash
# Offline test of scripts/fetch-tarball.sh SHA-256 pinning (file:// URLs, no network). Used by ctest: fetch_tarball_pin.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
d="$(mktemp -d)" || exit 2
trap 'rm -rf "$d"' EXIT
mkdir "$d/x"; echo hello > "$d/x/a"; tar czf "$d/t.tgz" -C "$d/x" a
good="$(sha256sum "$d/t.tgz" | cut -d' ' -f1)"
bad="$(printf '%s' "$good" | tr '0-9a-f' '1-9a-f0')"   # same length, different digits
fails=0
expect() { local want="$1" what="$2"; shift 2; "$@" >/dev/null 2>&1; local rc=$?
  [ "$rc" -eq "$want" ] || { echo "FAIL: $what (exit $rc, wanted $want)" >&2; fails=$((fails + 1)); }; }
export FETCH_ATTEMPTS=1
expect 0 "right pin"                       bash "$here/fetch-tarball.sh" --sha256 "$good" "$d/o1" "file://$d/t.tgz"
expect 0 "right pin in upper case"         bash "$here/fetch-tarball.sh" --sha256 "$(printf '%s' "$good" | tr a-f A-F)" "$d/o2" "file://$d/t.tgz"
expect 1 "wrong pin"                       bash "$here/fetch-tarball.sh" --sha256 "$bad" "$d/o3" "file://$d/t.tgz"
[ ! -e "$d/o3" ] || { echo "FAIL: a file with the wrong hash was left behind" >&2; fails=$((fails + 1)); }
expect 2 "TODO placeholder fails"          bash "$here/fetch-tarball.sh" --sha256 TODO "$d/o4" "file://$d/t.tgz"
expect 2 "short hash fails"                bash "$here/fetch-tarball.sh" --sha256 abcd "$d/o5" "file://$d/t.tgz"
expect 2 "empty pin fails"                 bash "$here/fetch-tarball.sh" --sha256 "" "$d/o6" "file://$d/t.tgz"
expect 1 "wrong pin from the environment"  env OKAY_FETCH_SHA256="$bad" bash "$here/fetch-tarball.sh" "$d/o7" "file://$d/t.tgz"
expect 0 "no pin (old behaviour)"          bash "$here/fetch-tarball.sh" "$d/o8" "file://$d/t.tgz"
# the repository's libsodium pin must be a real 64-digit hash (not a TODO)
pin="$(grep -v -E '^[[:space:]]*(#|$)' "$here/libsodium-mingw.pin" | head -n1 | cut -d' ' -f1)"
printf '%s' "$pin" | grep -q -E '^[0-9a-f]{64}$' || { echo "FAIL: scripts/libsodium-mingw.pin has no 64-digit SHA-256" >&2; fails=$((fails + 1)); }
[ "$fails" -eq 0 ] && echo "fetch-tarball pin test passed" || { echo "$fails failure(s)" >&2; exit 1; }
