#!/usr/bin/env bash
# okay-content end to end (docs/content_delivery.md): make a signed manifest from a folder, verify it, refuse a program file and a tampered file,
# sync from a file:// folder, sync again (up to date), wrong key refused, and (when the tool has the own HTTP client and python3 is there) sync over
# HTTP from a loopback server that cuts the first download so the client must resume with a Range request. Throwaway keys, no internet.
# usage: test_content_cli.sh <path to okay-content>
set -uo pipefail
TOOL="${1:?usage: test_content_cli.sh <okay-content>}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'kill $SRV 2>/dev/null; rm -rf "$T"' EXIT
SRV=""
fail=0
bad() { echo "FAIL: $*"; fail=1; }
keys="$(python3 -I "$REPO/scripts/sign_index.py" genkey)"
seed="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_SIGNING_SEED=//p')"
pub="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_TRUSTED_KEYS=//p')"
pub2="$(python3 -I "$REPO/scripts/sign_index.py" genkey | sed -n 's/^OKAY_UPDATE_TRUSTED_KEYS=//p')"

mkdir -p "$T/pub/models" "$T/pub/scripts" "$T/pub/audio"
head -c 3000 /dev/urandom > "$T/pub/models/ship.glb"
printf 'x=1\n' > "$T/pub/scripts/boss.okayvs"
head -c 70000 /dev/urandom > "$T/pub/audio/hit.wav"

# unsigned without a seed (says so), then signed
out="$(env -u OKAY_CONTENT_SIGNING_SEED "$TOOL" make-manifest "$T/pub" --game cli --version 1 2>&1)" || bad "make-manifest failed: $out"
[[ "$out" == *UNSIGNED* ]] && [ ! -e "$T/pub/content.manifest.sig" ] || bad "an unsigned manifest must say so and write no .sig"
out="$(OKAY_CONTENT_SIGNING_SEED="$seed" "$TOOL" make-manifest "$T/pub" --game cli --version 1 2>&1)" || bad "signed make-manifest failed: $out"
case "$out" in *"$seed"*) bad "the seed was printed";; esac
grep -qE '^okay-sig1 [0-9a-f]{128}$' "$T/pub/content.manifest.sig" || bad "signature format"
[ "$(OKAY_CONTENT_SIGNING_SEED="$seed" "$TOOL" pubkey)" = "$pub" ] || bad "pubkey does not match the key sign_index.py made"
"$TOOL" verify "$T/pub" --keys "$pub" --game cli >/dev/null 2>&1 || bad "verify of a good folder"
"$TOOL" verify "$T/pub" --keys "$pub2" >/dev/null 2>&1 && bad "verify accepted the wrong key"
"$TOOL" verify "$T/pub" --keys "$pub" --game other >/dev/null 2>&1 && bad "verify accepted the wrong game"

# a program file is never content
cp "$T/pub/audio/hit.wav" "$T/pub/run.exe"
OKAY_CONTENT_SIGNING_SEED="$seed" "$TOOL" make-manifest "$T/pub" --game cli --version 2 >/dev/null 2>&1 && bad "make-manifest accepted run.exe"
rm "$T/pub/run.exe"
printf 'x' > "$T/pub/notes.xyz"
OKAY_CONTENT_SIGNING_SEED="$seed" "$TOOL" make-manifest "$T/pub" --game cli --version 2 >/dev/null 2>&1 && bad "make-manifest accepted an unknown file type"
rm "$T/pub/notes.xyz"

# tampered file after signing
cp -r "$T/pub" "$T/tam"; printf 'Z' | dd of="$T/tam/models/ship.glb" bs=1 seek=10 conv=notrunc 2>/dev/null
"$TOOL" verify "$T/tam" --keys "$pub" >/dev/null 2>&1 && bad "verify accepted a tampered file"

# sync from a folder (file://)
out="$("$TOOL" sync "$T/pub" "$T/c1" --game cli --keys "$pub" 2>&1)" || bad "sync failed: $out"
[[ "$out" == *"status=updated"*"downloaded=3"* ]] || bad "first sync should download 3 files: $out"
cmp -s "$T/pub/models/ship.glb" "$T/c1/trees/1/models/ship.glb" || bad "synced file differs"
out="$("$TOOL" sync "$T/pub" "$T/c1" --game cli --keys "$pub" 2>&1)" && [[ "$out" == *"status=up-to-date"* ]] || bad "second sync should be up to date: $out"
"$TOOL" status "$T/c1" | grep -q "version=1" || bad "status"
"$TOOL" sync "$T/pub" "$T/c2" --game cli --keys "$pub2" >/dev/null 2>&1 && bad "sync accepted a manifest signed by another key"
[ ! -e "$T/c2/current.txt" ] || bad "a refused sync must leave nothing committed"
"$TOOL" sync "$T/tam" "$T/c3" --game cli --keys "$pub" >/dev/null 2>&1 && bad "sync accepted a tampered file"
[ ! -e "$T/c3/current.txt" ] && [ ! -d "$T/c3/trees/1" ] || bad "a tampered file became visible"

# over HTTP with a cut download (needs the own HTTP client in the tool and python3)
if command -v python3 >/dev/null 2>&1; then
    CONTENT_SERVER_CUT="audio/hit.wav:20000" python3 -I "$REPO/tests/scripts/content_range_server.py" "$T/pub" "$T/port" & SRV=$!
    for _ in $(seq 1 50); do [ -s "$T/port" ] && break; sleep 0.1; done
    PORT="$(cat "$T/port" 2>/dev/null)"
    if [ -n "$PORT" ]; then
        out="$(OKAY_OWN_HTTP=1 "$TOOL" sync "http://127.0.0.1:$PORT/" "$T/c4" --game cli --keys "$pub" 2>&1)"; rc=$?
        if [[ "$out" == *"no built-in HTTP client"* ]]; then echo "note: http step skipped (tool built without the own HTTP client)"
        elif [ $rc -ne 0 ] || [[ "$out" != *"status=updated"*"retries=1"* ]]; then bad "http sync (with a cut download) failed: $out"
        else cmp -s "$T/pub/audio/hit.wav" "$T/c4/trees/1/audio/hit.wav" || bad "http file differs after resume"; fi
    else echo "note: http step skipped (server did not start)"; fi
fi
[ $fail -eq 0 ] && echo "content cli: ok"
exit $fail
