#!/usr/bin/env bash
# scripts/sign_index.py and the signing guards of scripts/publish-supabase.sh: throwaway keys, local files only, no network.
# Checks: RFC 8032 vector, sign/verify round trip, tamper and wrong-key refusal, the seed is never printed, a missing or malformed seed
# is a clear error that does not echo the value, the seed is read from the environment only, and the publish script refuses to run
# under `set -x` with a seed in scope.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
S="python3 -I $REPO/scripts/sign_index.py"
fail=0
bad() { echo "FAIL: $*"; fail=1; }

# RFC 8032 section 7.1 test 1 (empty message) through the library functions.
python3 -I - "$REPO" <<'PY' || bad "RFC 8032 vector"
import sys
sys.path.insert(0, sys.argv[1] + "/scripts")
import sign_index as s
seed = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
assert s.public_key(seed).hex() == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
assert s.sign(seed, b"").hex() == ("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46b"
                                   "d25bf5f0595bbe24655141438e7a100b")
PY

keys="$($S genkey)"
seed="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_SIGNING_SEED=//p')"
pub="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_TRUSTED_KEYS=//p')"
[[ "$seed" =~ ^[0-9a-f]{64}$ && "$pub" =~ ^[0-9a-f]{64}$ ]] || bad "genkey output"
keys2="$($S genkey)"; pub2="$(printf '%s\n' "$keys2" | sed -n 's/^OKAY_UPDATE_TRUSTED_KEYS=//p')"
[ "$pub" != "$pub2" ] || bad "genkey repeats itself"

printf '{"schema": 1}\n' > "$T/versions.json"
out="$(OKAY_UPDATE_SIGNING_SEED="$seed" $S sign --file "$T/versions.json" --out "$T/versions.json.sig" 2>&1)" || bad "sign failed: $out"
case "$out" in *"$seed"*) bad "the seed was printed by sign";; esac
grep -qE '^okay-sig1 [0-9a-f]{128}$' "$T/versions.json.sig" || bad "signature format"
case "$(cat "$T/versions.json.sig")" in *"$seed"*) bad "the seed is in the signature file";; esac
[ "$(OKAY_UPDATE_SIGNING_SEED="$seed" $S pubkey)" = "$pub" ] || bad "pubkey does not match genkey"
$S verify --file "$T/versions.json" --sig "$T/versions.json.sig" --pub "$pub" >/dev/null 2>&1 || bad "verify of a good signature"
$S verify --file "$T/versions.json" --sig "$T/versions.json.sig" --pub "$pub2,$pub" >/dev/null 2>&1 || bad "verify with two keys (rotation)"
$S verify --file "$T/versions.json" --sig "$T/versions.json.sig" --pub "$pub2" >/dev/null 2>&1 && bad "verify accepted the wrong key"
printf '{"schema": 2}\n' > "$T/tampered.json"
$S verify --file "$T/tampered.json" --sig "$T/versions.json.sig" --pub "$pub" >/dev/null 2>&1 && bad "verify accepted a changed file"
printf 'okay-sig1 %0128d\n' 0 > "$T/zero.sig"
$S verify --file "$T/versions.json" --sig "$T/zero.sig" --pub "$pub" >/dev/null 2>&1 && bad "verify accepted a zero signature"

# The seed: environment only, a clear error, never echoed.
out="$(env -u OKAY_UPDATE_SIGNING_SEED $S sign --file "$T/versions.json" --out "$T/x.sig" 2>&1)" && bad "sign without a seed succeeded"
case "$out" in *"OKAY_UPDATE_SIGNING_SEED is not set"*) ;; *) bad "missing seed: unclear message: $out";; esac
out="$(OKAY_UPDATE_SIGNING_SEED="not-hex-secret-value" $S sign --file "$T/versions.json" --out "$T/x.sig" 2>&1)" && bad "sign with a bad seed succeeded"
case "$out" in *"not-hex-secret-value"*) bad "a malformed seed was echoed";; esac
[ ! -e "$T/x.sig" ] || bad "a signature file was written without a valid seed"
$S sign --seed "$seed" --file "$T/versions.json" --out "$T/y.sig" >/dev/null 2>&1 && bad "the seed was accepted on the command line"

# publish-supabase.sh guards (they run before any network use).
mkdir -p "$T/dist"; printf '1.2.3\n' > "$T/dist/VERSION.txt"
out="$(env -u SUPABASE_SERVICE_KEY -u SUPABASE_ACCESS_TOKEN OKAY_UPDATE_SIGNING_SEED="$seed" DIST_DIR="$T/dist" bash -x "$REPO/scripts/publish-supabase.sh" 2>&1)" && bad "ran under set -x with a seed"
case "$out" in *"refusing to run with 'set -x'"*) ;; *) bad "set -x guard message missing";; esac
# The trace of the guard itself must not hold the seed: the check happens before anything expands it.
case "$out" in *"$seed"*) bad "the set -x trace printed the seed";; esac
out="$(env -u SUPABASE_SERVICE_KEY -u SUPABASE_ACCESS_TOKEN OKAY_UPDATE_SIGNING_SEED="abc" DIST_DIR="$T/dist" bash "$REPO/scripts/publish-supabase.sh" 2>&1)" && bad "a short seed was accepted"
case "$out" in *"not 64 hex digits"*) ;; *) bad "short seed message: $out";; esac
out="$(env -u SUPABASE_SERVICE_KEY -u SUPABASE_ACCESS_TOKEN -u OKAY_UPDATE_SIGNING_SEED REQUIRE_SIGNED=1 DIST_DIR="$T/dist" bash "$REPO/scripts/publish-supabase.sh" 2>&1)" && bad "REQUIRE_SIGNED without a seed ran"
case "$out" in *"REQUIRE_SIGNED=1 but OKAY_UPDATE_SIGNING_SEED is not set"*) ;; *) bad "REQUIRE_SIGNED message: $out";; esac

[ "$fail" = 0 ] && echo "sign_index: ok"
exit "$fail"
