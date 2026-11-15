#!/usr/bin/env bash
# Download the pinned libsodium MinGW prebuilt, unpack it, and check the prefix really holds what the release build needs.
#
# Usage: scripts/fetch-libsodium-mingw.sh <work-dir>
#   Prints the prefix (<work-dir>/libsodium-mingw/libsodium-win64) as the last line on stdout. Callers export it as
#   OKAY_SODIUM_MINGW_PREFIX. Fails (exit 1) if the download, the SHA-256 pin or the layout is wrong: a release must never
#   quietly build without libsodium (CLAUDE.md rule 18, docs/own_stack_rollout.md).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
dir="${1:?usage: $0 <work-dir>}"
mkdir -p "$dir"
line="$(grep -v -E '^[[:space:]]*(#|$)' "$here/libsodium-mingw.pin" | head -n1)"
sha="${line%% *}"; url="${line#* }"
bash "$here/fetch-tarball.sh" --sha256 "$sha" "$dir/libsodium-mingw.tar.gz" "$url" >&2
rm -rf "$dir/libsodium-mingw"; mkdir -p "$dir/libsodium-mingw"
tar xzf "$dir/libsodium-mingw.tar.gz" -C "$dir/libsodium-mingw"
prefix="$dir/libsodium-mingw/libsodium-win64"
for f in lib/libsodium.a include/sodium.h; do
  [ -s "$prefix/$f" ] || { echo "error: the libsodium tarball has no libsodium-win64/$f (found: $(ls "$dir/libsodium-mingw" | tr '\n' ' ')); refusing to build a release without libsodium" >&2; exit 1; }
done
echo "$prefix"
