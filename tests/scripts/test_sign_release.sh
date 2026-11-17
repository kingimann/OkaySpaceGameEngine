#!/usr/bin/env bash
# scripts/sign-release.sh (SEC1 item 3): throwaway keys, local files, no network. Checks: signs and verifies, tampered file and wrong
# key refused, a skip (not a failure) without the seed, REQUIRE_SIGNED turns the skip into a failure, check-key notices a seed whose
# public key the programs do not trust, a malformed seed is refused without echoing it, the seed is never printed, and `set -x` is refused.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
R="$REPO/scripts/sign-release.sh"
fail=0
bad() { echo "FAIL: $*"; fail=1; }
keys="$(python3 -I "$REPO/scripts/sign_index.py" genkey)"
seed="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_SIGNING_SEED=//p')"
pub="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_TRUSTED_KEYS=//p')"
pub2="$(python3 -I "$REPO/scripts/sign_index.py" genkey | sed -n 's/^OKAY_UPDATE_TRUSTED_KEYS=//p')"
printf '{"schema": 1}\n' > "$T/versions.json"

out="$(env -u OKAY_UPDATE_SIGNING_SEED "$R" sign "$T/versions.json" 2>&1)"; rc=$?
[ $rc -eq 0 ] && [[ "$out" == *SKIPPED* ]] && [ ! -e "$T/versions.json.sig" ] || bad "no seed must skip with exit 0 and write nothing ($rc: $out)"
env -u OKAY_UPDATE_SIGNING_SEED REQUIRE_SIGNED=1 "$R" sign "$T/versions.json" >/dev/null 2>&1 && bad "REQUIRE_SIGNED=1 without a seed must fail"
out="$(OKAY_UPDATE_SIGNING_SEED="$seed" OKAY_UPDATE_TRUSTED_KEYS="$pub" "$R" sign "$T/versions.json" 2>&1)" || bad "sign failed: $out"
case "$out" in *"$seed"*) bad "the seed was printed";; esac
grep -qE '^okay-sig1 [0-9a-f]{128}$' "$T/versions.json.sig" || bad "signature format"
"$R" verify "$T/versions.json" "$T/versions.json.sig" "$pub" >/dev/null 2>&1 || bad "verify of a good signature"
"$R" verify "$T/versions.json" "$T/versions.json.sig" "$pub2" >/dev/null 2>&1 && bad "verify accepted the wrong key"
printf '{"schema": 2}\n' > "$T/tampered.json"
"$R" verify "$T/tampered.json" "$T/versions.json.sig" "$pub" >/dev/null 2>&1 && bad "verify accepted a tampered file"
head -c 40 "$T/versions.json.sig" > "$T/cut.sig"
"$R" verify "$T/versions.json" "$T/cut.sig" "$pub" >/dev/null 2>&1 && bad "verify accepted a truncated signature"
OKAY_UPDATE_SIGNING_SEED="$seed" OKAY_UPDATE_TRUSTED_KEYS="$pub2" "$R" sign "$T/versions.json" "$T/other.sig" >/dev/null 2>&1 && bad "sign must fail when the programs would not trust the key"
OKAY_UPDATE_SIGNING_SEED="$seed" OKAY_UPDATE_TRUSTED_KEYS="$pub" "$R" check-key >/dev/null 2>&1 || bad "check-key with a matching key"
OKAY_UPDATE_SIGNING_SEED="$seed" OKAY_UPDATE_TRUSTED_KEYS="$pub2,$pub" "$R" check-key >/dev/null 2>&1 || bad "check-key with a rotation list"
OKAY_UPDATE_SIGNING_SEED="$seed" OKAY_UPDATE_TRUSTED_KEYS="$pub2" "$R" check-key >/dev/null 2>&1 && bad "check-key accepted a key the programs do not trust"
OKAY_UPDATE_SIGNING_SEED="$seed" OKAY_UPDATE_TRUSTED_KEYS="" "$R" check-key >/dev/null 2>&1 && bad "check-key with an empty trusted list must fail"
env -u OKAY_UPDATE_SIGNING_SEED "$R" check-key >/dev/null 2>&1 || bad "check-key without a seed must skip, not fail"
out="$(OKAY_UPDATE_SIGNING_SEED="not-hex-secret-value" "$R" sign "$T/versions.json" 2>&1)" && bad "a malformed seed must fail"
case "$out" in *not-hex-secret-value*) bad "a malformed seed was echoed";; esac
out="$(OKAY_UPDATE_SIGNING_SEED="$seed" bash -x "$R" status 2>&1)" && bad "set -x with a seed in scope must be refused"
case "$out" in *"$seed"*) bad "set -x printed the seed";; esac
case "$(OKAY_UPDATE_SIGNING_SEED="$seed" "$R" status)" in *"$seed"*) bad "status printed the seed";; esac
[ $fail -eq 0 ] && echo "sign_release: ok"
exit $fail
