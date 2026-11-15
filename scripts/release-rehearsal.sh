#!/usr/bin/env bash
# A dress rehearsal of the NEW publish path (scripts/publish-supabase.sh with PUBLISH_V2=1) against the REAL storage service, in a
# THROWAWAY bucket that this script makes and always deletes. No real release is touched: the bucket "releases" is never read or written.
#
#   BUCKET=zz-publish-test-<anything> SUPABASE_SERVICE_KEY=... scripts/release-rehearsal.sh run       build, publish, check, clean up
#   BUCKET=zz-publish-test-<anything> SUPABASE_SERVICE_KEY=... scripts/release-rehearsal.sh cleanup   empty and delete the bucket (safe to repeat)
#
# In GitHub it is started with the "Run workflow" button of "Release rehearsal" (.github/workflows/release-rehearsal.yml; steps in
# docs/try_it.md), which uses the repository secret SUPABASE_SERVICE_KEY the same way release.yml does. The key is read from the
# environment only and is never printed (this script refuses to run under `set -x`, and no call prints headers).
#
# What it does:
#   1. builds tiny dummy releases (three files that start with MZ, as the publish script wants) and a throwaway signing key,
#   2. publishes two versions at the SAME moment (2.900.0 and 2.901.0) and checks: one took the lock and the other waited, both gave it
#      back, versions.json lists both with the SHA-256 of OkaySpace.exe, OkayEngine.exe and OkaySpacePlayer.exe, VERSION.txt and "latest"
#      are the higher one, no lock is left, and the signature verifies,
#   3. publishes the same files again (must say "same"), and the same version with DIFFERENT files (must be refused, nothing changed),
#   4. reads the public address right after an update (a cached read must not matter): publishes 2.902.0 and then an OLDER 2.899.0, and
#      checks nothing was lost and "latest" did not move back,
#   5. asks the service the three questions the lock is built on and PRINTS the real answers: the authenticated read of an existing object,
#      the answer for an object that does not exist, and the answer for creating an object that already exists without upsert (409? 400?),
#   6. empties and deletes the bucket (also from a separate "cleanup" step that runs even when this one is cancelled).
# Every check is a PASS or FAIL row in the step summary ($GITHUB_STEP_SUMMARY) and in the log; the exit status is 1 if any check failed.
#
# Environment: BUCKET (required, must start with zz-publish-test-), SUPABASE_SERVICE_KEY (required), SUPABASE_URL (default: the project in
# CMakeLists.txt), LOCK_POLL_SECONDS (default 3, for the publishes), GITHUB_STEP_SUMMARY (where the summary goes; default: only the log).
set -euo pipefail
cd "$(dirname "$0")/.."

MODE="${1:-run}"
die() { echo "release-rehearsal: $*" >&2; exit 1; }

# ---- Safety first: only ever a throwaway bucket (checked before anything is read, written or deleted) ---------------------------------
BUCKET="${BUCKET:-}"
[[ "$BUCKET" =~ ^zz-publish-test-[a-z0-9-]+$ ]] ||
    die "refusing to run: BUCKET must be zz-publish-test- followed by letters, digits or hyphens (got '${BUCKET:-<empty>}'). This script creates and DELETES that bucket, so it never takes any other"
case "$MODE" in run|cleanup) ;; *) die "usage: scripts/release-rehearsal.sh run|cleanup" ;; esac
case "$-" in *x*) die "refusing to run with 'set -x' (a trace would print the service key)" ;; esac

KEY="${SUPABASE_SERVICE_KEY:-}"
if [ -z "$KEY" ]; then
    if [ "$MODE" = cleanup ]; then echo "release-rehearsal: no SUPABASE_SERVICE_KEY, so there is nothing to clean up."; exit 0; fi
    die "SUPABASE_SERVICE_KEY is not set (the repository secret, or your own shell)"
fi
SUPABASE_URL="${SUPABASE_URL:-$(grep -m1 '^set(OKAY_DEFAULT_ACCOUNT_URL' CMakeLists.txt | grep -oE 'https://[a-z0-9]+\.supabase\.co')}"
SUPABASE_URL="${SUPABASE_URL%/}"
API="$SUPABASE_URL/storage/v1"
PUBLIC="$API/object/public/$BUCKET"
AUTH=(-H "Authorization: Bearer $KEY" -H "apikey: $KEY")
WORK="$(mktemp -d)"
SUMMARY="${GITHUB_STEP_SUMMARY:-$WORK/summary.md}"

# ---- The same two answer shapes the publish script accepts (a test keeps these lines equal to the ones in scripts/publish-supabase.sh) -----
is_missing() {
    [ "$1" = 404 ] && return 0
    [ "$1" = 400 ] && grep -qiE '"statusCode" *: *"?404"?|not.found|does not exist|no such' "$2" 2>/dev/null
}
is_duplicate() {
    [ "$1" = 409 ] && return 0
    [ "$1" = 400 ] && grep -qiE '"statusCode" *: *"?409"?|already.exist|duplicate' "$2" 2>/dev/null
}

# call <body file> <curl args...>: one request; sets CODE (000 when there is no complete answer, a cut-off one included); headers in $WORK/hdr
CODE=000
call() {
    local body="$1"
    shift
    : > "$body"
    CODE="$(curl -sS -D "$WORK/hdr" -o "$body" -w '%{http_code}' --max-time 60 "$@" 2>/dev/null)" || CODE=000
    [[ "$CODE" =~ ^[0-9]{3}$ ]] || CODE=000
}
auth_get() { call "$2" -H 'Cache-Control: no-cache' "${AUTH[@]}" "$API/object/$BUCKET/$1?nocache=$(date +%s)$RANDOM"; }   # auth_get <name> <file>
excerpt() { head -c 200 "$1" 2>/dev/null | tr '\r\n|' '   ' ; }
header() { local v; v="$(sed -n "/^$1:/I{s/^[^:]*: *//;p;q;}" "$WORK/hdr" 2>/dev/null | tr -d '\r')"; printf '%s' "${v:-(none)}"; }   # header <name>: that header of the last answer

# ---- Results ----------------------------------------------------------------------------------------------------------------------
FAILED=0 ROWS=0
row() {   # row <PASS|FAIL> <what was checked> <what was seen>
    ROWS=$((ROWS + 1))
    [ "$1" = PASS ] || FAILED=$((FAILED + 1))
    printf '| %d | %s | **%s** | %s |\n' "$ROWS" "$2" "$1" "${3//|/\\|}" >> "$WORK/table.md"
    echo "[$1] $2 -- $3"
}
check() { if [ "$1" = 0 ]; then row PASS "$2" "$3"; else row FAIL "$2" "$3"; fi; }   # check <0 = ok> <what> <seen>
LEARNED=()
learn() { LEARNED+=("| $1 | $2 | $3 |"); echo "[seen] $1 -> $2 $3"; }   # learn <question> <status> <body or headers>

# ---- The bucket ---------------------------------------------------------------------------------------------------------------------
drop_bucket() {   # empty the throwaway bucket and delete it; sets DROP_SEEN; returns 0 when it is gone (or never existed)
    local i
    DROP_SEEN=""
    for i in 1 2 3; do
        call "$WORK/empty" -X POST "${AUTH[@]}" "$API/bucket/$BUCKET/empty"
        local ec="$CODE" eb; eb="$(excerpt "$WORK/empty")"
        call "$WORK/del" -X DELETE "${AUTH[@]}" "$API/bucket/$BUCKET"
        local dc="$CODE" db; db="$(excerpt "$WORK/del")"
        call "$WORK/gone" "${AUTH[@]}" "$API/bucket/$BUCKET"
        DROP_SEEN="empty: HTTP $ec $eb; delete: HTTP $dc $db; look again: HTTP $CODE"
        if is_missing "$CODE" "$WORK/gone"; then return 0; fi
        sleep 3
    done
    return 1
}
write_summary() {   # write_summary <headline>
    {
        echo "## Release rehearsal"
        echo
        echo "$1"
        echo
        echo "Throwaway bucket: \`$BUCKET\`. The real \`releases\` bucket was not touched."
        echo
        echo "| # | What was checked | Result | What was seen |"
        echo "|---|---|---|---|"
        cat "$WORK/table.md" 2>/dev/null || true
        if [ "${#LEARNED[@]}" -gt 0 ]; then
            echo
            echo "### What the real storage service answered"
            echo
            echo "| Question | Status | Body or headers |"
            echo "|---|---|---|"
            printf '%s\n' "${LEARNED[@]}"
        fi
    } >> "$SUMMARY"
}

if [ "$MODE" = cleanup ]; then
    if drop_bucket; then echo "release-rehearsal: the throwaway bucket $BUCKET is gone ($DROP_SEEN)."; code=0
    else echo "release-rehearsal: COULD NOT delete the throwaway bucket $BUCKET ($DROP_SEEN). Delete it in the Supabase dashboard (Storage)." >&2; code=1; fi
    {
        echo "### Cleanup"
        echo
        if [ "$code" = 0 ]; then echo "The throwaway bucket \`$BUCKET\` is emptied and deleted ($DROP_SEEN)."
        else echo "**The throwaway bucket \`$BUCKET\` could NOT be deleted** ($DROP_SEEN). Delete it in the Supabase dashboard (Storage)."; fi
    } >> "$SUMMARY"
    rm -rf "$WORK"
    exit "$code"
fi

# ---- run -----------------------------------------------------------------------------------------------------------------------------
finish() {   # (also runs when the script is cancelled or fails half way: the bucket must not be left behind)
    local rc=$?
    set +e
    trap '' INT TERM HUP
    if [ -n "${BUCKET_DROPPED:-}" ]; then :; else drop_bucket; fi
    rm -rf "$WORK"
    exit "$rc"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

make_dist() {   # make_dist <dir> <version> <tag>: three tiny "programs" and VERSION.txt, laid out like build-win-dist
    mkdir -p "$1/Tools"
    printf '%s\n' "$2" > "$1/VERSION.txt"
    local f
    for f in OkaySpace.exe Tools/OkayEngine.exe Tools/OkaySpacePlayer.exe; do printf 'MZ rehearsal %s %s %s\n' "$2" "$3" "$f" > "$1/$f"; done
}
make_dist "$WORK/a" 2.900.0 a
make_dist "$WORK/b" 2.901.0 b
make_dist "$WORK/b2" 2.901.0 b-with-different-files
make_dist "$WORK/c" 2.902.0 c
make_dist "$WORK/d" 2.899.0 d
printf -- '- release rehearsal\n' > "$WORK/notes.txt"
keys="$(python3 scripts/sign_index.py genkey)"   # a throwaway signing key: used here and nowhere else, never printed
SEED="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_SIGNING_SEED=//p')"
PUB="$(printf '%s\n' "$keys" | sed -n 's/^OKAY_UPDATE_TRUSTED_KEYS=//p')"
unset keys

publish() {   # publish <dist dir> <log file> -> the script's exit status (the log is for the checks; it is printed when it is done)
    PUBLISH_V2=1 BUCKET="$BUCKET" DIST_DIR="$1" NOTES_FILE="$WORK/notes.txt" OKAY_UPDATE_SIGNING_SEED="$SEED" ARCHIVE_REPO="${GITHUB_REPOSITORY:-kingimann/OkaySpaceGameEngine}" \
        LOCK_POLL_SECONDS="${LOCK_POLL_SECONDS:-3}" bash scripts/publish-supabase.sh > "$2" 2>&1
}
show_log() { echo "----- $1"; sed 's/^/    /' "$2"; }

# From here every check records its own PASS or FAIL: a failing test command must not end the script before it is written down
# (under `set -e`, `A && B; check $?` would exit when A passes and B fails). Unset variables are still errors.
set +e

# 1+2. two publishes at the same moment
echo "== Publishing 2.900.0 and 2.901.0 at the same moment into $BUCKET"
( set +e; publish "$WORK/a" "$WORK/a.log"; echo $? > "$WORK/a.rc" ) &
( set +e; publish "$WORK/b" "$WORK/b.log"; echo $? > "$WORK/b.rc" ) &
wait
rc_a="$(cat "$WORK/a.rc")"; rc_b="$(cat "$WORK/b.rc")"
show_log "publish of 2.900.0 (exit $rc_a)" "$WORK/a.log"
show_log "publish of 2.901.0 (exit $rc_b)" "$WORK/b.log"
[ "$rc_a" = 0 ] && [ "$rc_b" = 0 ]; check $? "Both publishes finished without an error" "exit codes $rc_a and $rc_b"
both=0; for l in a b; do grep -q 'took the release lock' "$WORK/$l.log" && grep -q 'gave the release lock back' "$WORK/$l.log" || both=1; done
check "$both" "Each publish took the lock and gave it back" "\"took the release lock\" and \"gave the release lock back\" in both logs"
waited="neither"; grep -q 'is held by' "$WORK/a.log" && waited="2.900.0"; grep -q 'is held by' "$WORK/b.log" && waited="2.901.0"
[ "$waited" != neither ]; check $? "One publish found the lock held and waited for it" "the one that waited: $waited"

auth_get versions.json "$WORK/idx.json"
if [ "$CODE" = 200 ]; then
    detail="$(python3 - "$WORK/idx.json" "2.900.0=$WORK/a" "2.901.0=$WORK/b" <<'PY'
import hashlib, json, os, sys
idx = json.load(open(sys.argv[1]))
problems = []
if idx.get("schema") != 1:
    problems.append("schema is %r" % idx.get("schema"))
for spec in sys.argv[2:]:
    ver, d = spec.split("=", 1)
    entry = next((e for e in idx.get("versions", []) if e.get("version") == ver), None)
    if entry is None:
        problems.append("v%s is not in versions.json" % ver)
        continue
    for name, rel in (("OkaySpace.exe", "OkaySpace.exe"), ("OkayEngine.exe", "Tools/OkayEngine.exe"), ("OkaySpacePlayer.exe", "Tools/OkaySpacePlayer.exe")):
        data = open(os.path.join(d, rel), "rb").read()
        f = (entry.get("files") or {}).get(name) or {}
        if f.get("sha256") != hashlib.sha256(data).hexdigest() or f.get("size") != len(data):
            problems.append("v%s %s has no matching size and SHA-256" % (ver, name))
print("; ".join(problems) if problems else "both versions listed, three SHA-256 each, all matching the files")
sys.exit(1 if problems else 0)
PY
)"; ok=$?
else detail="HTTP $CODE reading versions.json"; ok=1; fi
check "$ok" "versions.json lists both versions with the SHA-256 of OkaySpace.exe, OkayEngine.exe and OkaySpacePlayer.exe" "$detail"

latest="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1])).get("latest", ""))' "$WORK/idx.json" 2>/dev/null || true)"
auth_get VERSION.txt "$WORK/ver.txt"; vtxt="$(head -n1 "$WORK/ver.txt" | tr -d '\r ')"
[ "$latest" = 2.901.0 ] && [ "$vtxt" = 2.901.0 ] && [ "$CODE" = 200 ]; check $? "VERSION.txt and \"latest\" in versions.json are the higher version" "VERSION.txt: ${vtxt:-<none>} (HTTP $CODE); latest: ${latest:-<none>}"
auth_get release.lock "$WORK/lock.txt"
is_missing "$CODE" "$WORK/lock.txt"; check $? "No lock is left behind" "release.lock read: HTTP $CODE $(excerpt "$WORK/lock.txt")"
call "$WORK/sig.json" -H 'Cache-Control: no-cache' "$PUBLIC/versions.json?nocache=$(date +%s)$RANDOM"; c1="$CODE"
call "$WORK/sig.sig" -H 'Cache-Control: no-cache' "$PUBLIC/versions.json.sig?nocache=$(date +%s)$RANDOM"; c2="$CODE"
[ "$c1" = 200 ] && [ "$c2" = 200 ] && python3 scripts/sign_index.py verify --file "$WORK/sig.json" --sig "$WORK/sig.sig" --pub "$PUB" > /dev/null 2>&1
check $? "The published versions.json verifies against the published signature (read from the public address)" "HTTP $c1 and $c2"

# 3. the same files again, then the same version with different files
echo "== Publishing 2.901.0 again: the same files, then different files"
( set +e; publish "$WORK/b" "$WORK/same.log"; echo $? > "$WORK/same.rc" )
show_log "2.901.0 again, same files (exit $(cat "$WORK/same.rc"))" "$WORK/same.log"
[ "$(cat "$WORK/same.rc")" = 0 ] && grep -q 'already published with exactly these files' "$WORK/same.log"
check $? "Publishing the same files again is a safe re-run" "exit $(cat "$WORK/same.rc"); the log says \"already published with exactly these files\""
before="$(auth_get v2.901.0/OkayEngine.exe "$WORK/eng.before"; sha256sum "$WORK/eng.before" | cut -d' ' -f1)"
( set +e; publish "$WORK/b2" "$WORK/diff.log"; echo $? > "$WORK/diff.rc" )
show_log "2.901.0 with DIFFERENT files (exit $(cat "$WORK/diff.rc"))" "$WORK/diff.log"
after="$(auth_get v2.901.0/OkayEngine.exe "$WORK/eng.after"; sha256sum "$WORK/eng.after" | cut -d' ' -f1)"
[ "$(cat "$WORK/diff.rc")" != 0 ] && grep -q 'already published with DIFFERENT files' "$WORK/diff.log" && [ "$before" = "$after" ]
check $? "The same version with different files is refused and nothing in the bucket changed" "exit $(cat "$WORK/diff.rc"); OkayEngine.exe checksum before and after the attempt equal: $([ "$before" = "$after" ] && echo yes || echo NO)"

# 4. a cached public read must not matter
echo "== A cached public read must not matter: publish 2.902.0, then an OLDER 2.899.0"
for name in VERSION.txt versions.json; do call "$WORK/prime" "$PUBLIC/$name"; done   # plain reads, as a launcher would make them
( set +e; publish "$WORK/c" "$WORK/c.log"; echo $? > "$WORK/c.rc" )
call "$WORK/after-c" "$PUBLIC/VERSION.txt"
shown="$(head -n1 "$WORK/after-c" | tr -d '\r ')"
learn "public VERSION.txt, read right after 2.902.0 was published (a plain read, no cache-busting)" "HTTP $CODE" "showed \`${shown:-nothing}\`; cache-control: $(header cache-control); age: $(header age); cf-cache-status: $(header cf-cache-status); x-cache: $(header x-cache)"
( set +e; publish "$WORK/d" "$WORK/d.log"; echo $? > "$WORK/d.rc" )
show_log "2.902.0 (exit $(cat "$WORK/c.rc"))" "$WORK/c.log"
show_log "the older 2.899.0 (exit $(cat "$WORK/d.rc"))" "$WORK/d.log"
auth_get VERSION.txt "$WORK/ver2.txt"; vtxt2="$(head -n1 "$WORK/ver2.txt" | tr -d '\r ')"
auth_get versions.json "$WORK/idx2.json"
listed="$(python3 -c 'import json, sys; print(" ".join(e["version"] for e in json.load(open(sys.argv[1]))["versions"]))' "$WORK/idx2.json" 2>/dev/null || true)"
[ "$(cat "$WORK/c.rc")" = 0 ] && [ "$(cat "$WORK/d.rc")" = 0 ] && [ "$vtxt2" = 2.902.0 ] && [ "$listed" = "2.902.0 2.901.0 2.900.0 2.899.0" ] && grep -q 'is OLDER than the version that is latest now' "$WORK/d.log"
check $? "An older release published after a newer one is kept in the list and does not move \"latest\" back, whatever the public address shows" "VERSION.txt: ${vtxt2:-<none>}; versions.json lists: ${listed:-<nothing>}"

# 5. the three questions the lock is built on, with the real answers
echo "== What does the real service answer?"
auth_get VERSION.txt "$WORK/q1"
[ "$CODE" = 200 ] && [ "$(head -n1 "$WORK/q1" | tr -d '\r ')" = 2.902.0 ]
check $? "The authenticated read returns an object that exists" "GET /object/$BUCKET/VERSION.txt: HTTP $CODE, body \`$(excerpt "$WORK/q1")\`, content-type: $(header content-type)"
learn "authenticated read of an object that exists" "HTTP $CODE" "$(excerpt "$WORK/q1")"
auth_get "does-not-exist-$RANDOM.txt" "$WORK/q2"
is_missing "$CODE" "$WORK/q2"
check $? "The answer for an object that does not exist has the shape the script expects (404, or 400 with the 404 in the body)" "HTTP $CODE, body \`$(excerpt "$WORK/q2")\`"
learn "authenticated read of an object that does not exist" "HTTP $CODE" "$(excerpt "$WORK/q2")"
printf 'first\n' > "$WORK/probe.txt"
probe() { call "$1" -X POST "${AUTH[@]}" -H "x-upsert: $2" -H 'Content-Type: text/plain' --data-binary @"$WORK/probe.txt" "$API/object/$BUCKET/probe.txt"; }
probe "$WORK/p1" false; c_first="$CODE"
probe "$WORK/p2" false; c_second="$CODE"
is_duplicate "$c_second" "$WORK/p2" && { [ "$c_first" = 200 ] || [ "$c_first" = 201 ]; }
check $? "Creating an object that already exists WITHOUT upsert is refused in the shape the script expects (409, or 400 with the 409 in the body)" "first create: HTTP $c_first; second create: HTTP $c_second, body \`$(excerpt "$WORK/p2")\`"
learn "create without upsert, object does not exist yet" "HTTP $c_first" "$(excerpt "$WORK/p1")"
learn "create without upsert, object already exists (this is the lock)" "HTTP $c_second" "$(excerpt "$WORK/p2")"
probe "$WORK/p3" true
[ "$CODE" = 200 ]; check $? "Creating it WITH upsert overwrites it (this is how the lock is renewed)" "HTTP $CODE, body \`$(excerpt "$WORK/p3")\`"
call "$WORK/p4" -X DELETE "${AUTH[@]}" -H 'Content-Type: application/json' -d '{"prefixes":["probe.txt"]}' "$API/object/$BUCKET"; c_del="$CODE"
auth_get probe.txt "$WORK/p5"
[ "$c_del" = 200 ] && is_missing "$CODE" "$WORK/p5"
check $? "Deleting an object by name works (this is how the lock is given back)" "DELETE: HTTP $c_del, body \`$(excerpt "$WORK/p4")\`; read afterwards: HTTP $CODE"
learn "delete by name" "HTTP $c_del" "$(excerpt "$WORK/p4")"

# 6. clean up (the trap does it again if anything above stopped early)
echo "== Cleaning up"
if drop_bucket; then BUCKET_DROPPED=1; row PASS "The throwaway bucket was emptied and deleted" "$DROP_SEEN"
else row FAIL "The throwaway bucket was emptied and deleted" "$DROP_SEEN"; fi

if [ "$FAILED" = 0 ]; then
    write_summary "**PASSED.** The new publish path behaved correctly on the real storage service: $ROWS checks, none failed. Nothing was switched on: \`PUBLISH_V2\` at the top of \`release.yml\` is still \`\"0\"\`; turning it on is one line, and is the coordinator's decision."
    echo "release-rehearsal: PASSED ($ROWS checks)"
    exit 0
fi
write_summary "**FAILED.** $FAILED of $ROWS checks failed, so the new publish path must stay OFF (\`PUBLISH_V2\` stays \`\"0\"\`). Send this page to the coordinator: the rows below say what the real service did."
echo "release-rehearsal: FAILED ($FAILED of $ROWS checks)" >&2
exit 1
