#!/usr/bin/env bash
# scripts/publish-supabase-dev.sh (the test builds' publisher) against a FAKE storage server (tests/scripts/fake_storage.py, loopback only), and
# the real release publisher's refusals of what it must never publish. Nothing here touches the network or a real key: the key below is a made-up
# string, and the signing key is made fresh by scripts/sign_index.py genkey.
#
# What it proves (the rules of docs/release_channels.md "Test builds"):
#   * a test build writes ONLY under dev/ (every request is in the fake's log), never VERSION.txt, NOTES.txt, LTS.txt, versions.json or anything else at
#     the bucket root, never touches the bucket itself; the real release files are byte for byte what they were;
#   * the list goes up before DEV.txt, DEV.txt is last, an unsigned list is a warning and a signed one verifies;
#   * a version that exists, a version whose folder has leftovers, a damaged list, a stale base release, a three-number version, a missing file,
#     a bad branch / commit / key / setting are refused with NOTHING written;
#   * only the newest DEV_KEEP builds stay; old ones and leftovers of a half publish are deleted, only under dev/v<ver>/, and stay listed as gone;
#   * the key and the signing seed never appear in any output, log or object;
#   * scripts/publish-supabase.sh refuses CHANNEL=dev and a four-number version.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# Whatever the machine that runs this has in its environment (a real key on the publisher's machine!) must never reach these scripts.
unset SUPABASE_SERVICE_KEY SUPABASE_ACCESS_TOKEN OKAY_UPDATE_SIGNING_SEED REQUIRE_SIGNED DRY_RUN DEV_KEEP DEV_BRANCH DEV_SHA DEV_NOTE DIST_DIR BUCKET MAX_MB CHANNEL KEEP NOTES_FILE
T="$(mktemp -d)"
KEY="test-key-not-real"
SERVER_PID=""
fail=0
bad() { echo "FAIL: $*"; fail=1; }
cleanup() { [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT

python3 -I "$REPO/tests/scripts/fake_storage.py" --root "$T/store" --log "$T/requests.log" --key "$KEY" --port-file "$T/port" &
SERVER_PID=$!
for _ in $(seq 1 100); do [ -s "$T/port" ] && break; sleep 0.1; done
[ -s "$T/port" ] || { echo "FAIL: the fake storage server did not start"; exit 1; }
export SUPABASE_URL="http://127.0.0.1:$(cat "$T/port")"

REL="$T/store/releases"
SHA=0123456789abcdef0123456789abcdef01234567

# ---- helpers -------------------------------------------------------------------------------------------------------------------------
reset_store() {   # an empty bucket that holds a published release: 2.876.0 with its files and the real list
    rm -rf "$T/store"; mkdir -p "$REL/v2.876.0"
    printf '2.876.0\n' > "$REL/VERSION.txt"
    printf -- '- the release notes\n' > "$REL/NOTES.txt"
    printf '2.850.1\n' > "$REL/LTS.txt"
    printf '{"schema": 1, "latest": "2.876.0", "versions": []}\n' > "$REL/versions.json"
    printf 'okay-sig1 %s\n' "$(printf '0%.0s' $(seq 1 128))" > "$REL/versions.json.sig"
    for n in OkaySpace.exe OkayEngine.exe OkaySpacePlayer.exe; do printf 'MZ release %s' "$n" > "$REL/v2.876.0/$n"; done
    : > "$T/requests.log"
}
mkdist() {   # mkdist <version> : a fake build output
    rm -rf "$T/dist"; mkdir -p "$T/dist/Tools"
    printf 'MZ launcher %s' "$1" > "$T/dist/OkaySpace.exe"
    printf 'MZ editor %s %s' "$1" "$(head -c 300 /dev/zero | tr '\0' x)" > "$T/dist/Tools/OkayEngine.exe"
    printf 'MZ player %s' "$1" > "$T/dist/Tools/OkaySpacePlayer.exe"
    printf '%s\n' "$1" > "$T/dist/VERSION.txt"
}
fp_all()    { ( cd "$T/store" && find . -type f | LC_ALL=C sort | xargs -r sha256sum | sha256sum | cut -c1-64 ); }
fp_stable() { ( cd "$T/store" && find . -path ./releases/dev -prune -o -type f -print | LC_ALL=C sort | xargs -r sha256sum | sha256sum | cut -c1-64 ); }
writes()    { grep -E '^(POST /storage/v1/(object/releases/|bucket)|DELETE )' "$T/requests.log" || true; }
publish() {   # publish [-u NAME ...] [ENV=VALUE ...] : runs the dev publisher (-u leaves a default out), output in $T/out.txt, prints the exit code
    local defaults=(DIST_DIR="$T/dist" DEV_BRANCH=claude/walk-vs-run DEV_SHA="$SHA" DEV_NOTE="try the new run speed" SUPABASE_SERVICE_KEY="$KEY" READ_RETRY_SECONDS=0)
    local drop=() use=() e d skip
    while [ "${1:-}" = "-u" ]; do drop+=("$2"); shift 2; done
    for e in "${defaults[@]}"; do
        skip=0
        for d in ${drop[@]+"${drop[@]}"}; do [ "${e%%=*}" = "$d" ] && skip=1; done
        [ "$skip" = 1 ] || use+=("$e")
    done
    env "${use[@]}" "$@" bash "$REPO/scripts/publish-supabase-dev.sh" > "$T/out.txt" 2>&1
    echo $?
}
out() { cat "$T/out.txt"; }
expect_refused() {   # expect_refused <what> <exit code> <text the message must contain> : nothing may have been written
    local what="$1" code="$2" text="$3"
    [ "$code" -ne 0 ] || bad "$what: expected a refusal, got exit 0"
    out | grep -qF -- "$text" || bad "$what: the message does not say '$text' (output: $(out | tr '\n' '|' | cut -c1-300))"
    [ -z "$(writes)" ] || bad "$what: something was written: $(writes | head -3 | tr '\n' '|')"
}

# ---- 1. a dry run needs no key and writes nothing ---------------------------------------------------------------------------------------
reset_store; mkdist 2.876.0.17
before="$(fp_all)"
code="$(publish -u SUPABASE_SERVICE_KEY DRY_RUN=1)"
[ "$code" = 0 ] || bad "dry run: exit $code ($(out | tail -3 | tr '\n' '|'))"
out | grep -qF "DRY_RUN would write, and only these:" || bad "dry run: no list of what would be written"
for want in "dev/v2.876.0.17/OkaySpace.exe" "dev/v2.876.0.17/OkayEngine.exe" "dev/v2.876.0.17/OkaySpacePlayer.exe" "dev/versions.json" "dev/NOTES-DEV.txt" "dev/DEV.txt"; do
    out | grep -qF "  $want" || bad "dry run: '$want' is not in the list"
done
out | sed -n '/would write/,$p' | grep -E '^  ' | grep -vE '^  (dev/|\(delete\) dev/)' && bad "dry run: it lists a path outside dev/"
[ -z "$(writes)" ] || bad "dry run: it wrote ($(writes | head -2 | tr '\n' '|'))"
[ "$(fp_all)" = "$before" ] || bad "dry run: the store changed"

# ---- 2. a first publish: only dev/, in order, the real release untouched ------------------------------------------------------------------
reset_store; mkdist 2.876.0.17
stable_before="$(fp_stable)"
code="$(publish)"
[ "$code" = 0 ] || bad "first publish: exit $code ($(out | tail -4 | tr '\n' '|'))"
out | grep -qF "UNSIGNED" || bad "first publish: no warning that the list is unsigned"
out | grep -qF "Test build of walk-vs-run (0123456)" || bad "first publish: the title is not said"
[ "$(fp_stable)" = "$stable_before" ] || bad "first publish: a REAL release file changed"
for f in dev/v2.876.0.17/OkaySpace.exe dev/v2.876.0.17/OkayEngine.exe dev/v2.876.0.17/OkaySpacePlayer.exe dev/versions.json dev/NOTES-DEV.txt dev/DEV.txt; do
    [ -s "$REL/$f" ] || bad "first publish: $f was not written"
done
[ "$(cat "$REL/dev/DEV.txt" 2>/dev/null)" = "2.876.0.17" ] || bad "first publish: DEV.txt does not say 2.876.0.17"
[ -f "$REL/dev/versions.json.sig" ] && bad "first publish: a signature appeared with no signing key"
w="$(writes)"
[ -n "$w" ] || bad "first publish: nothing was written?"
echo "$w" | grep -vE '^POST /storage/v1/object/releases/dev/[A-Za-z0-9._/-]+ 200$' && bad "first publish: a write outside dev/ (or a failed one)"
echo "$w" | grep -qE '/bucket' && bad "first publish: it touched the bucket itself"
echo "$w" | grep -E 'VERSION\.txt|NOTES\.txt|LTS\.txt|NOTES-LTS' | grep -v 'dev/' && bad "first publish: it wrote a release-feed name"
first_idx="$(echo "$w" | grep -n 'dev/versions.json ' | head -1 | cut -d: -f1)"; notes_idx="$(echo "$w" | grep -n 'NOTES-DEV.txt' | head -1 | cut -d: -f1)"
head_idx="$(echo "$w" | grep -n 'dev/DEV.txt' | head -1 | cut -d: -f1)"; last_idx="$(echo "$w" | wc -l)"
[ -n "$first_idx" ] && [ -n "$notes_idx" ] && [ -n "$head_idx" ] && [ "$first_idx" -lt "$notes_idx" ] && [ "$notes_idx" -lt "$head_idx" ] || bad "first publish: the order is not list, notes, DEV.txt"
[ "$head_idx" = "$last_idx" ] || bad "first publish: DEV.txt is not the last write"
echo "$w" | head -3 | grep -qE 'dev/v2\.876\.0\.17/' || bad "first publish: the build's files are not uploaded first"
python3 -I - "$REL/dev/versions.json" "$T/dist" <<'PY' || bad "first publish: the list is not what the launcher reads"
import json, sys, hashlib, os
j = json.load(open(sys.argv[1])); d = sys.argv[2]
assert j["schema"] == 1 and j["dev"] == "2.876.0.17" and "latest" not in j and "lts" not in j and "archive" not in j, j
e = j["versions"][0]
assert e["version"] == "2.876.0.17" and e["channel"] == "dev" and e["inBucket"] is True and e["branch"] == "claude/walk-vs-run" and e["sha"] == "0123456789abcdef0123456789abcdef01234567", e
for name, rel in (("OkaySpace.exe", "OkaySpace.exe"), ("OkayEngine.exe", "Tools/OkayEngine.exe"), ("OkaySpacePlayer.exe", "Tools/OkaySpacePlayer.exe")):
    data = open(os.path.join(d, rel), "rb").read()
    assert e["files"][name] == {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}, name
PY
head -1 "$REL/dev/NOTES-DEV.txt" | grep -qxF "Test build of walk-vs-run (0123456)" || bad "first publish: NOTES-DEV.txt does not start with the title"
grep -q "$KEY" "$T/out.txt" && bad "first publish: the key is in the output"
grep -rqF "$KEY" "$T/store" && bad "first publish: the key is stored in an object"
grep -qF "$KEY" "$T/requests.log" && bad "first publish: the key is in the request log"
snap_one="$(fp_all)"

# ---- 3. the same version again: refused, nothing written ------------------------------------------------------------------------------------
: > "$T/requests.log"
code="$(publish)"
expect_refused "the same version again" "$code" "already"
[ "$(fp_all)" = "$snap_one" ] || bad "the same version again: the store changed"

# ---- 4. the next build with room for one: the old one goes, only under dev/, and stays listed as gone ---------------------------------------------
mkdist 2.876.0.18; : > "$T/requests.log"
code="$(publish DEV_KEEP=1)"
[ "$code" = 0 ] || bad "second publish: exit $code ($(out | tail -4 | tr '\n' '|'))"
[ ! -e "$REL/dev/v2.876.0.17" ] || bad "second publish: the old build was not removed"
[ -s "$REL/dev/v2.876.0.18/OkayEngine.exe" ] || bad "second publish: the new build is missing"
[ "$(fp_stable)" = "$stable_before" ] || bad "second publish: a REAL release file changed"
grep -E '^DELETE ' "$T/requests.log" | grep -qv '^DELETE /storage/v1/object/releases 200$' && bad "second publish: a delete was not the expected one"
python3 -I - "$REL/dev/versions.json" <<'PY' || bad "second publish: the list does not say which builds are gone"
import json, sys
j = json.load(open(sys.argv[1]))
by = {e["version"]: e for e in j["versions"]}
assert j["dev"] == "2.876.0.18" and by["2.876.0.18"]["inBucket"] is True and by["2.876.0.17"]["inBucket"] is False, by
assert list(by) == ["2.876.0.18", "2.876.0.17"], list(by)
PY
echo "$(cat "$REL/dev/DEV.txt")" | grep -qx "2.876.0.18" || bad "second publish: DEV.txt is not the newest"
# the number of a removed build is never used again
mkdist 2.876.0.17; : > "$T/requests.log"; snap_two="$(fp_all)"
code="$(publish)"
expect_refused "a removed build's number again" "$code" "already"
[ "$(fp_all)" = "$snap_two" ] || bad "a removed build's number again: the store changed"

# ---- 5. leftovers of a publish that stopped half way are cleaned up ------------------------------------------------------------------------------
mkdist 2.876.0.19; mkdir -p "$REL/dev/v2.876.0.5"; printf 'MZ leftover' > "$REL/dev/v2.876.0.5/OkaySpace.exe"; : > "$T/requests.log"
code="$(publish)"
[ "$code" = 0 ] || bad "leftover clean-up: exit $code ($(out | tail -3 | tr '\n' '|'))"
[ ! -e "$REL/dev/v2.876.0.5" ] || bad "leftover clean-up: the leftover folder stayed"
[ -s "$REL/dev/v2.876.0.18/OkaySpace.exe" ] && [ -s "$REL/dev/v2.876.0.19/OkaySpace.exe" ] || bad "leftover clean-up: a kept build was removed"
[ "$(fp_stable)" = "$stable_before" ] || bad "leftover clean-up: a REAL release file changed"

# ---- 6. what must be refused, with nothing written ----------------------------------------------------------------------------------------------
refuse_case() {   # refuse_case <label> <text the message must contain> [ENV=VALUE ...]  (the store and the build are set up by the caller)
    local label="$1" text="$2"; shift 2
    : > "$T/requests.log"; local fp; fp="$(fp_all)"
    local code; code="$(publish "$@")"
    expect_refused "$label" "$code" "$text"
    [ "$(fp_all)" = "$fp" ] || bad "$label: the store changed"
}
reset_store
for badver in 2.876.0 2.876.0.0 2.876.0.5.1 v2.876.0.5 2.876.0.030; do
    mkdist "$badver"; refuse_case "the version $badver" "FOUR numbers"
done
mkdist 2.876.0.30;  rm "$T/dist/Tools/OkayEngine.exe"; refuse_case "a missing program (no checksum)" "missing"
mkdist 2.876.0.30;  printf 'not a program' > "$T/dist/OkaySpace.exe"; refuse_case "a file that is not a Windows program" "isn't a Windows program"
mkdist 2.876.0.30;  refuse_case "a bad branch" "refused" DEV_BRANCH='a b;rm -rf /'
refuse_case "an empty branch" "refused" DEV_BRANCH=
refuse_case "a bad commit" "refused" DEV_SHA=xyz
refuse_case "a branch that climbs out of its place" "refused" DEV_BRANCH='../../etc'
refuse_case "a keep of 0" "DEV_KEEP" DEV_KEEP=0
refuse_case "a keep of 11" "DEV_KEEP" DEV_KEEP=11
refuse_case "a signature that is required but cannot be made" "REQUIRE_SIGNED" REQUIRE_SIGNED=1
refuse_case "a bad signing key" "64 hex digits" OKAY_UPDATE_SIGNING_SEED=not-a-seed
refuse_case "no key" "SUPABASE_SERVICE_KEY" -u SUPABASE_SERVICE_KEY
refuse_case "a wrong key" "could not list" SUPABASE_SERVICE_KEY=another-key-not-real
reset_store; printf '2.877.0\n' > "$REL/VERSION.txt"; mkdist 2.876.0.30
refuse_case "a base release that is no longer the newest" "Start a new test build"
reset_store; rm "$REL/VERSION.txt"
refuse_case "no release published yet" "no release is published yet"
reset_store; mkdir -p "$REL/dev/v2.876.0.30"; printf 'MZ half' > "$REL/dev/v2.876.0.30/OkaySpace.exe"
refuse_case "leftover files of this very version" "already has files"
reset_store; mkdir -p "$REL/dev"; printf '{ nope' > "$REL/dev/versions.json"
refuse_case "a damaged list on the server" "nothing was changed"
reset_store
# a trace would print the key: refused when a key is set
: > "$T/requests.log"
env DIST_DIR="$T/dist" DEV_BRANCH=main DEV_SHA="$SHA" SUPABASE_SERVICE_KEY="$KEY" bash -x "$REPO/scripts/publish-supabase-dev.sh" > "$T/out.txt" 2>&1
[ $? -ne 0 ] || bad "bash -x with a key set was not refused"
grep -qF "refusing to run with 'set -x'" "$T/out.txt" || bad "bash -x: the refusal is not explained"
grep -qF "$KEY" "$T/out.txt" && bad "bash -x: the key is in the trace"
[ -z "$(writes)" ] || bad "bash -x: something was written"

# ---- 7. a signed publish: the signature verifies, the seed never shows ---------------------------------------------------------------------------------
reset_store; mkdist 2.876.0.31; stable_seven="$(fp_stable)"
keys="$(python3 -I "$REPO/scripts/sign_index.py" genkey)"
SEED="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_SIGNING_SEED=//p')"; PUB="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_TRUSTED_KEYS=//p')"
[ ${#SEED} = 64 ] && [ ${#PUB} = 64 ] || bad "signing: could not make a throwaway key"
code="$(publish OKAY_UPDATE_SIGNING_SEED="$SEED" GITHUB_STEP_SUMMARY="$T/summary.md" DEV_NOTE='see [x](http://evil.invalid) <b>hi</b>')"
[ "$code" = 0 ] || bad "signed publish: exit $code ($(out | tail -4 | tr '\n' '|'))"
grep -qF "Get test builds" "$T/summary.md" && grep -qF "v2.876.0.31" "$T/summary.md" && grep -qF "walk-vs-run" "$T/summary.md" || bad "the job summary does not say what to do ($(head -c 200 "$T/summary.md" 2>/dev/null | tr '\n' '|'))"
grep -qF "<b>" "$T/summary.md" && bad "the job summary lets the note's markup through"
grep -qF "[x](" "$T/summary.md" && bad "the job summary lets the note's link through"
out | grep -qF "UNSIGNED" && bad "signed publish: it says the list is unsigned"
out | grep -qF "signed dev/versions.json" || bad "signed publish: it does not say it signed"
[ -s "$REL/dev/versions.json.sig" ] || bad "signed publish: no signature file"
python3 -I "$REPO/scripts/sign_index.py" verify --file "$REL/dev/versions.json" --sig "$REL/dev/versions.json.sig" --pub "$PUB" > /dev/null 2>&1 || bad "signed publish: the signature does not verify"
w="$(writes)"; echo "$w" | grep -vE '^POST /storage/v1/object/releases/dev/[A-Za-z0-9._/-]+ 200$' && bad "signed publish: a write outside dev/"
echo "$w" | grep -n 'versions.json.sig' | head -1 | grep -q . || bad "signed publish: the signature was not uploaded"
[ "$(echo "$w" | tail -1 | grep -c 'dev/DEV.txt')" = 1 ] || bad "signed publish: DEV.txt is not last"
grep -qF "$SEED" "$T/out.txt" && bad "signed publish: the seed is in the output"
grep -rqF "$SEED" "$T/store" && bad "signed publish: the seed is stored in an object"
grep -qF "$SEED" "$T/requests.log" && bad "signed publish: the seed is in the request log"
grep -qF "$KEY" "$T/out.txt" && bad "signed publish: the key is in the output"
[ "$(fp_stable)" = "$stable_seven" ] || bad "signed publish: the real release files changed"

# ---- 8. the REAL release publisher refuses what must not go out as a release -------------------------------------------------------------------------
reset_store; mkdist 2.876.0.32; : > "$T/requests.log"; fp="$(fp_all)"
env DIST_DIR="$T/dist" CHANNEL=dev SUPABASE_SERVICE_KEY="$KEY" DRY_RUN=1 bash "$REPO/scripts/publish-supabase.sh" > "$T/out.txt" 2>&1
[ $? -ne 0 ] || bad "publish-supabase.sh accepted CHANNEL=dev"
out | grep -qF "CHANNEL must be" || bad "publish-supabase.sh: CHANNEL=dev is refused without saying why ($(out | tail -2 | tr '\n' '|'))"
env DIST_DIR="$T/dist" CHANNEL=latest SUPABASE_SERVICE_KEY="$KEY" DRY_RUN=1 bash "$REPO/scripts/publish-supabase.sh" > "$T/out.txt" 2>&1
[ $? -ne 0 ] || bad "publish-supabase.sh accepted a four-number version as a release"
out | grep -qE "bad version|cannot compute|checksum" || bad "publish-supabase.sh: the four-number version is refused without saying why ($(out | tail -2 | tr '\n' '|'))"
env DIST_DIR="$T/dist" CHANNEL=lts SUPABASE_SERVICE_KEY="$KEY" DRY_RUN=1 bash "$REPO/scripts/publish-supabase.sh" > "$T/out.txt" 2>&1
[ $? -ne 0 ] || bad "publish-supabase.sh accepted a four-number version as an LTS patch"
[ -z "$(writes)" ] && [ "$(fp_all)" = "$fp" ] || bad "the release publisher wrote something while refusing"

[ "$fail" = 0 ] && echo "dev_publish: ok"
exit "$fail"
