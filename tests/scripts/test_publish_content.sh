#!/usr/bin/env bash
# scripts/publish-content.sh against a FAKE storage server (tests/scripts/fake_storage.py, loopback only, made-up service key, fresh throwaway signing key).
# Proves: files go up before the signature and the manifest, the manifest is LAST; the published folder syncs and verifies with okay-content; the seed and the
# service key never appear in output or log; an unsigned publish, a program file, a bad game id and a bad version are refused with NOTHING uploaded;
# DRY_RUN uploads nothing. usage: test_publish_content.sh <path to okay-content>
set -uo pipefail
TOOL="${1:?usage: test_publish_content.sh <okay-content>}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; SERVER_PID=""
cleanup() { [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }
KEY="made-up-service-key-for-the-test"
keys="$(python3 -I "$REPO/scripts/sign_index.py" genkey)"
seed="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_SIGNING_SEED=//p')"
pub="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_TRUSTED_KEYS=//p')"
python3 -I "$REPO/tests/scripts/fake_storage.py" --root "$T/store" --log "$T/requests.log" --key "$KEY" --port-file "$T/port" &
SERVER_PID=$!
for _ in $(seq 1 100); do [ -s "$T/port" ] && break; sleep 0.1; done
[ -s "$T/port" ] || { echo "FAIL: the fake storage server did not start"; exit 1; }
export SUPABASE_URL="http://127.0.0.1:$(cat "$T/port")" SUPABASE_SERVICE_KEY="$KEY" OKAY_CONTENT_BIN="$TOOL"
P="$REPO/scripts/publish-content.sh"

mkdir -p "$T/in/models" "$T/in/scripts" "$T/in/my folder"
head -c 5000 /dev/urandom > "$T/in/models/ship.glb"; printf 'x=1\n' > "$T/in/scripts/boss.okayvs"; printf 'hi\n' > "$T/in/my folder/note one.txt"

# refusals: nothing uploaded
: > "$T/requests.log"
env -u OKAY_CONTENT_SIGNING_SEED "$P" "$T/in" --game demo --version 1 >/dev/null 2>&1 && bad "an unsigned publish must be refused without ALLOW_UNSIGNED"
OKAY_CONTENT_SIGNING_SEED="$seed" "$P" "$T/in" --game "Bad Game" --version 1 >/dev/null 2>&1 && bad "a bad game id must be refused"
OKAY_CONTENT_SIGNING_SEED="$seed" "$P" "$T/in" --game demo --version 0 >/dev/null 2>&1 && bad "version 0 must be refused"
OKAY_CONTENT_SIGNING_SEED="not-hex" "$P" "$T/in" --game demo --version 1 >/dev/null 2>&1 && bad "a malformed seed must be refused"
cp "$T/in/models/ship.glb" "$T/in/run.exe"
OKAY_CONTENT_SIGNING_SEED="$seed" "$P" "$T/in" --game demo --version 1 >/dev/null 2>&1 && bad "a folder with a program file must be refused"
rm "$T/in/run.exe"
[ ! -s "$T/requests.log" ] || bad "a refused publish sent requests: $(cat "$T/requests.log")"

# dry run
out="$(OKAY_CONTENT_SIGNING_SEED="$seed" DRY_RUN=1 "$P" "$T/in" --game demo --version 1 2>&1)" || bad "dry run failed: $out"
[[ "$out" == *"nothing uploaded"* ]] && [ ! -s "$T/requests.log" ] || bad "dry run must upload nothing"

# the real thing
out="$(OKAY_CONTENT_SIGNING_SEED="$seed" "$P" "$T/in" --game demo --version 1 2>&1)"; rc=$?
[ $rc -eq 0 ] || bad "publish failed: $out"
case "$out$(cat "$T/requests.log")" in *"$seed"*) bad "the signing seed was printed or logged";; esac
case "$out$(cat "$T/requests.log")" in *"$KEY"*) bad "the service key was printed or logged";; esac
B="$T/store/game-content/demo"
for f in models/ship.glb scripts/boss.okayvs "my folder/note one.txt" content.manifest content.manifest.sig; do [ -f "$B/$f" ] || bad "missing in bucket: $f"; done
last="$(grep '^POST /storage/v1/object/game-content/' "$T/requests.log" | tail -n1)"
[[ "$last" == *content.manifest\ 200 ]] || bad "content.manifest must be uploaded LAST (last upload: $last)"
sigline="$(grep -n 'content.manifest.sig' "$T/requests.log" | head -n1 | cut -d: -f1)"; manline="$(grep -n '/content.manifest 200' "$T/requests.log" | head -n1 | cut -d: -f1)"
[ -n "$sigline" ] && [ -n "$manline" ] && [ "$sigline" -lt "$manline" ] || bad "the signature must go up before the manifest"
[[ "$out" == *"published manifest verifies"* ]] || bad "the read-back check did not run: $out"
"$TOOL" verify "$B" --keys "$pub" --game demo >/dev/null 2>&1 || bad "the published folder does not verify"
"$TOOL" sync "$B" "$T/client" --game demo --keys "$pub" 2>&1 | grep -q "status=updated" || bad "a client cannot sync from what was published"
cmp -s "$T/in/models/ship.glb" "$T/client/trees/1/models/ship.glb" || bad "client file differs"
[ -f "$T/client/trees/1/my folder/note one.txt" ] || bad "a name with spaces did not survive"

[ $fail -eq 0 ] && echo "publish content: ok"
exit $fail
