#!/usr/bin/env bash
# Publish a Windows build to Supabase Storage, so launchers and editors update
# from there without a GitHub workflow. Run it after scripts/build-windows-dist.sh.
#
#   SUPABASE_ACCESS_TOKEN=sbp_...  scripts/publish-supabase.sh   # management token (the service key is fetched)
#   SUPABASE_SERVICE_KEY=eyJ...    scripts/publish-supabase.sh   # or the service_role key itself
#
# Keys come from the environment only: never put them in a file in this repo.
#
# TWO SCRIPTS, ONE SWITCH (PUBLISH_V2). This file is the NEW publish script (the lock, fresh reads, "latest" never backwards, the rule-12
# guard). It has not run against the real storage service yet, so it is OFF by default: unless PUBLISH_V2=1 this file hands over, unchanged,
# to scripts/publish-supabase-v1.sh, a verbatim copy of the script that released everything so far. The release workflow and
# scripts/release-local.sh call the old one directly unless PUBLISH_V2=1 is set (one line at the top of release.yml). Plain
# `scripts/publish-supabase.sh` by hand (CLAUDE.md rule 13) therefore still does what it always did. The rest of this header is about
# the new script, and everything below the dispatcher only runs with PUBLISH_V2=1.
#
# What it uploads (public bucket "releases"; created on first use):
#   v<ver>/OkaySpace.exe, v<ver>/OkayEngine.exe, v<ver>/OkaySpacePlayer.exe,
#   v<ver>/CHANGELOG.txt     the version's files, in their own folder
#   NOTES.txt                "what's new" bullet lines (the launcher's card)
#   versions.json            index of every released version with sizes and SHA-256 of OkayEngine.exe,
#                            OkaySpacePlayer.exe and OkaySpace.exe (scripts/versions_index.py). The launcher
#                            REFUSES to update a file that has no checksum there, so the publish FAILS if any
#                            checksum cannot be written or does not read back from the bucket (checked before
#                            VERSION.txt goes out, so nobody is offered a version they cannot install)
#   versions.json.sig        ed25519 signature of versions.json (scripts/sign_index.py), made with the seed in $OKAY_UPDATE_SIGNING_SEED
#                            when that is set; launchers built with OKAY_UPDATE_TRUSTED_KEYS refuse an unsigned or wrongly signed index
#                            (docs/updater.md "Signed releases"). The seed comes from the environment only: never written to a file or logged
#   VERSION.txt              uploaded LAST, so nobody sees a version before its files and its checksums
#   release.lock             exists only while a publish runs (see "Several publishes at once")
# Older version folders beyond the newest $KEEP are deleted, except the LTS line and the version being published.
#
# Channels (CHANNEL, default latest). Releases are numbered 2.<build>.<patch>:
#   latest   every normal release: writes VERSION.txt and NOTES.txt and prunes old
#            versions. Refuses a version in the current LTS line. A version OLDER than the current
#            latest is still published (files and index entry) but never moves VERSION.txt or NOTES.txt back.
#   lts      a patch of the long-term-support line (for example 2.850.1, built from
#            a branch cut at 2.850.0): writes LTS.txt and NOTES-LTS.txt, never touches
#            VERSION.txt or NOTES.txt, never prunes. The first publish of a new line
#            promotes that line to LTS; within a line the patch must not go backwards.
#   Versions in the LTS line are never pruned. See docs/release_channels.md.
#
# Several publishes at once (docs/release_channels.md, "Several releases at once"):
#   Builds may run in parallel, publishing may not: it reads and rewrites files that every version shares (versions.json, VERSION.txt,
#   the pruned folders), and two publishes at once would lose one version's checksums or move "latest" backwards. So everything from
#   the channel check to the end of pruning runs while this script holds a lock: the object release.lock in the bucket. It is taken by
#   uploading release.lock WITHOUT x-upsert, which the storage API refuses when the object exists (an atomic create), and it says who
#   holds it (version, run, time).
#     * Held by another publish: look again every LOCK_POLL_SECONDS (plus up to as much at random), up to LOCK_WAIT_SECONDS, then stop
#       with nothing changed.
#     * Not renewed for LOCK_STALE_SECONDS: that publish is dead and the lock is taken over, with a loud warning. Deleting a lock is
#       by name, so two waiters could each delete the other's fresh lock; to take over, a waiter first CLAIMS the dead lock by creating
#       release.lock.claim-<its token> (an atomic create: one waiter wins), looks at the lock again, and deletes it only if it is still
#       that lock and still older than the limit by its latest time stamp (a holder that renewed meanwhile keeps its lock). A claim that
#       nobody finished (older than LOCK_CLAIM_STALE_SECONDS) is removed by the next waiter.
#     * Given back when the script ends for any reason (success, an error, Ctrl-C, SIGTERM, a cancelled job), but only if it is still
#       ours. curl and sleep run in the background of this shell and are waited for, because bash runs a trap only after the foreground
#       command it waits for has finished: a signal would otherwise wait for the curl in flight. (In a workflow step write
#       `exec scripts/publish-supabase.sh`, so the runner's signal reaches this script itself and not a shell that waits for it.)
#       The give-back is tried for about 30 s.
#     * While it runs the script renews the lock after uploads and checks it is still ours before it commits (versions.json, VERSION.txt).
#     * Lock times are the SERVER's time (the Date header of its answers), so a wrong clock on this machine neither makes a live lock
#       look dead nor a dead one look alive.
#     * What is published now (VERSION.txt, LTS.txt, versions.json) is read under the lock through the authenticated API, never through
#       the public address: that one is served from a CDN cache, and a stale read would undo the lock.
#     * A read that must succeed is tried 6 times over about 30 s. An answer that was cut off (curl fails after "200"), or an
#       index that is not a valid schema-1 index, is never taken for an empty index: the publish stops instead of rebuilding the
#       version history from nothing, and a merged index that lists fewer versions than the one read is refused.
#   DRY_RUN=1 takes no lock and writes nothing.
#   A version that is already listed in versions.json with DIFFERENT files is refused (never publish an existing version again: users may
#   hold its checksums; CLAUDE.md rule 12). The same files again are a safe re-run. ALLOW_REPUBLISH=1 replaces it on purpose.
#
# Options (environment):
#   PUBLISH_V2=1 use this new script (without it: the old one, see above)
#   CHANNEL      latest (default) or lts
#   DIST_DIR     build output to publish (default build-win-dist)
#   NOTES_FILE   bullet lines for NOTES.txt (default: the last 6 commit subjects)
#   KEEP         how many non-LTS versions to keep in the bucket (default 3)
#   SUPABASE_URL project URL (default: the one baked into CMakeLists.txt)
#   BUCKET       the bucket (default releases; scripts/release-rehearsal.sh uses a throwaway one)
#   ARCHIVE_REPO GitHub repo holding every release, written into versions.json
#                (default $GITHUB_REPOSITORY, else kingimann/OkaySpaceGameEngine)
#   OKAY_UPDATE_SIGNING_SEED  64 hex digits, the private signing key. Unset = the index is published unsigned (a warning); only
#                            launchers built without OKAY_UPDATE_TRUSTED_KEYS accept that
#   REQUIRE_SIGNED=1         stop instead of publishing an unsigned index (set it once the launchers trust a key)
#   LOCK_WAIT_SECONDS        how long to wait for another publish (default 2400 = 40 min)
#   LOCK_STALE_SECONDS       a lock not renewed for this long is taken over (default 1200 = 20 min)
#   LOCK_POLL_SECONDS        pause between looks at a held lock, plus up to as much at random (default 10, so 10-20 s)
#   LOCK_RENEW_SECONDS       renew the lock after an upload when this long has passed since the last renewal (default 60)
#   LOCK_CLAIM_STALE_SECONDS a takeover claim older than this was never finished and is removed (default 120)
#   READ_RETRY_BASE_SECONDS  a read that must succeed is tried 6 times, pausing 1x, 2x, 3x, 4x, 5x this many seconds (default 2: 30 s)
#   ALLOW_REPUBLISH=1        publish over a version that is already listed with different files
#   DRY_RUN=1    check everything, upload nothing, take no lock
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

# ---- The switch: without PUBLISH_V2=1 this is the old script, untouched ------------------------------------------------------
if [ "${PUBLISH_V2:-0}" != 1 ]; then
    # (the old script's own guard only sees its own shell, so the same refusal is made here for `bash -x scripts/publish-supabase.sh`)
    case "$-" in *x*) if [ -v OKAY_UPDATE_SIGNING_SEED ]; then echo "publish-supabase: refusing to run with 'set -x' while OKAY_UPDATE_SIGNING_SEED is set (the trace would print it)" >&2; exit 1; fi;; esac
    exec bash "$HERE/publish-supabase-v1.sh" "$@"
fi
cd "$HERE/.."

DIST_DIR="${DIST_DIR:-build-win-dist}"
KEEP="${KEEP:-3}"
BUCKET="${BUCKET:-releases}"
SUPABASE_URL="${SUPABASE_URL:-$(grep -m1 '^set(OKAY_DEFAULT_ACCOUNT_URL' CMakeLists.txt | grep -oE 'https://[a-z0-9]+\.supabase\.co')}"
SUPABASE_URL="${SUPABASE_URL%/}"
REF="$(printf '%s' "$SUPABASE_URL" | sed -E 's#https://([a-z0-9]+)\.supabase\.co#\1#')"
MAX_MB="${MAX_MB:-100}"   # per-file limit; the Supabase project and bucket limits were raised to 100 MB (project Settings > Storage)
MAX_BYTES=$((MAX_MB * 1024 * 1024))

die() { echo "publish-supabase: $*" >&2; exit 1; }
# A warning that also shows up as an annotation on the run page when this runs in GitHub Actions.
warn() {
    echo "publish-supabase: WARNING: $*" >&2
    if [ "${GITHUB_ACTIONS:-}" = true ]; then echo "::warning title=Release publish::$*"; fi
    return 0
}

# ---- Waiting is done in the background of THIS shell --------------------------------------------------------------------------
# bash runs a trap only after the foreground command it is waiting for has finished, so a cancelled job (SIGTERM) would wait for the
# curl in flight (an upload can take minutes) or for a sleep, and the lock would stay behind for up to 20 minutes. curl and sleep are
# therefore started in the background of this shell and waited for with the `wait` builtin, which a trapped signal ends at once. (A
# command substitution would be a foreground wait again, so these functions hand their results back in variables.)
CURL_PID="" NAP_PID=""
nap() {   # nap <seconds>: a sleep that a signal ends at once
    sleep "$1" &
    NAP_PID=$!
    wait "$NAP_PID" || true
    NAP_PID=""
}

# One place removes the temp files and gives the lock back, however the script ends: success, die, Ctrl-C, SIGTERM (a cancelled job).
# (drop_claim and release_lock are defined with the other lock functions below; they only run once there is something to give back.)
RESP="" NOTES="" LOCK_HELD=0 LOCK_MAYBE=0 CLAIM_NAME=""
cleanup() {
    local rc=$?
    set +e
    trap '' INT TERM HUP   # (another cancel signal must not cut the clean-up short)
    if [ -n "$CURL_PID" ]; then kill "$CURL_PID" 2>/dev/null; fi
    if [ -n "$NAP_PID" ]; then kill "$NAP_PID" 2>/dev/null; fi
    if [ -n "$CLAIM_NAME" ]; then drop_claim; fi
    if [ "$LOCK_HELD" = 1 ] || [ "$LOCK_MAYBE" = 1 ]; then release_lock; fi
    if [ -n "$RESP" ]; then rm -f -- "$RESP" "$RESP".*; fi
    if [ -n "$NOTES" ]; then rm -f -- "$NOTES"; fi
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

# The signing seed is read from the environment by scripts/sign_index.py only. Never trace commands while it can be in scope.
# (-v tests that it is set without expanding the value, so even this check leaves nothing in a trace.)
case "$-" in *x*) if [ -v OKAY_UPDATE_SIGNING_SEED ]; then die "refusing to run with 'set -x' while OKAY_UPDATE_SIGNING_SEED is set (the trace would print it)"; fi;; esac
if [ -n "${OKAY_UPDATE_SIGNING_SEED:-}" ]; then
    [[ "$OKAY_UPDATE_SIGNING_SEED" =~ ^[0-9a-fA-F]{64}$ ]] || die "OKAY_UPDATE_SIGNING_SEED is not 64 hex digits"
elif [ "${REQUIRE_SIGNED:-0}" = 1 ]; then
    die "REQUIRE_SIGNED=1 but OKAY_UPDATE_SIGNING_SEED is not set"
fi

VER="$(head -n1 "$DIST_DIR/VERSION.txt" 2>/dev/null | tr -d '\r ' || true)"   # (|| true: let the check below say why)
[[ "$VER" =~ ^[0-9]+(\.[0-9]+)+$ ]] || die "no version in $DIST_DIR/VERSION.txt (run scripts/build-windows-dist.sh first)"

# name in the bucket -> file on disk
declare -a NAMES=(OkaySpace.exe OkayEngine.exe OkaySpacePlayer.exe)
declare -A SRC=(
    [OkaySpace.exe]="$DIST_DIR/OkaySpace.exe"
    [OkayEngine.exe]="$DIST_DIR/Tools/OkayEngine.exe"
    [OkaySpacePlayer.exe]="$DIST_DIR/Tools/OkaySpacePlayer.exe"
)
for n in "${NAMES[@]}"; do
    f="${SRC[$n]}"
    [ -f "$f" ] || die "missing $f"
    sz=$(stat -c %s "$f")
    [ "$sz" -le "$MAX_BYTES" ] || die "$n is $((sz / 1048576)) MB, over the $MAX_MB MB file limit (is its debug info split off, or raise MAX_MB and the Supabase storage limit?)"
    head -c2 "$f" | grep -q '^MZ' || die "$f isn't a Windows program"
done

# ---- Lock settings ---------------------------------------------------------------------------------------------------
LOCK_NAME=release.lock
LOCK_WAIT_SECONDS="${LOCK_WAIT_SECONDS:-2400}"
LOCK_STALE_SECONDS="${LOCK_STALE_SECONDS:-1200}"
LOCK_POLL_SECONDS="${LOCK_POLL_SECONDS:-10}"
LOCK_RENEW_SECONDS="${LOCK_RENEW_SECONDS:-60}"
LOCK_CLAIM_STALE_SECONDS="${LOCK_CLAIM_STALE_SECONDS:-120}"
READ_RETRY_BASE_SECONDS="${READ_RETRY_BASE_SECONDS:-2}"
READ_TRIES=6   # (a read that must succeed: 6 tries, pausing 1x, 2x, 3x, 4x, 5x the base: 30 s with the default 2)
for _n in LOCK_WAIT_SECONDS LOCK_STALE_SECONDS LOCK_POLL_SECONDS LOCK_RENEW_SECONDS LOCK_CLAIM_STALE_SECONDS READ_RETRY_BASE_SECONDS; do
    [[ "${!_n}" =~ ^[0-9]+$ ]] || die "$_n must be a whole number of seconds (got '${!_n}')"
    printf -v "$_n" '%d' "$((10#${!_n}))"   # (a value like 08 is not octal)
done
[ "$LOCK_POLL_SECONDS" -ge 1 ] || LOCK_POLL_SECONDS=1

# ---- Channel -------------------------------------------------------------------
CHANNEL="${CHANNEL:-latest}"
[[ "$CHANNEL" == latest || "$CHANNEL" == lts ]] || die "CHANNEL must be latest or lts (got '$CHANNEL')"
ARCHIVE_REPO="${ARCHIVE_REPO:-${GITHUB_REPOSITORY:-kingimann/OkaySpaceGameEngine}}"
PUBLIC="$SUPABASE_URL/storage/v1/object/public/$BUCKET"
line_of() { printf '%s' "$1" | cut -d. -f1-2; }   # 2.850.1 -> 2.850
# version_newer <a> <b>: a is a strictly newer version than b (2.9.0 < 2.10.0; 2.875 equals 2.875.0)
version_newer() {
    local a="$1" b="$2"
    while [ "$(printf '%s' "$a" | tr -cd . | wc -c)" -lt 2 ]; do a="$a.0"; done
    while [ "$(printf '%s' "$b" | tr -cd . | wc -c)" -lt 2 ]; do b="$b.0"; done
    [ "$a" != "$b" ] && [ "$(printf '%s\n%s\n' "$a" "$b" | sort -V | tail -n1)" = "$a" ]
}
# fetch_version <file in bucket>: its first line if it is a version, else nothing.
# This reads the PUBLIC address (cache-busted, but it is a CDN): good for an early "will this be refused" look, not for decisions
# made under the lock (those use bucket_version below).
fetch_version() {
    local v
    v="$(curl -fsS --max-time 20 -H 'Cache-Control: no-cache' "$PUBLIC/$1?nocache=$(date +%s)" 2>/dev/null | head -n1 | tr -d '\r ' || true)"
    if [[ "$v" =~ ^[0-9]+(\.[0-9]+)+$ ]]; then printf '%s' "$v"; fi
}
# The rules that depend on what is published (CUR_LTS, CUR_LATEST). Run once early (so a wrong channel fails in seconds, also under
# DRY_RUN) and again under the lock, on what is published at that moment.
check_channel_rules() {
    if [ "$CHANNEL" = latest ]; then
        if [ -n "$CUR_LTS" ] && [ "$(line_of "$VER")" = "$(line_of "$CUR_LTS")" ]; then
            die "v$VER is in the LTS line $(line_of "$CUR_LTS"): publish it with CHANNEL=lts, not as the latest version"
        fi
    else
        if [ -n "$CUR_LTS" ] && [ "$(line_of "$VER")" = "$(line_of "$CUR_LTS")" ] && [ "$VER" != "$CUR_LTS" ] &&
           [ "$(printf '%s\n%s\n' "$CUR_LTS" "$VER" | sort -V | tail -n1)" != "$VER" ]; then
            die "v$VER is older than the current LTS v$CUR_LTS in the same line (patches only go forward)"
        fi
        if [ -n "$CUR_LTS" ] && [ "$(line_of "$VER")" != "$(line_of "$CUR_LTS")" ]; then
            echo "Note: v$VER starts a new LTS line $(line_of "$VER") (the current one is $(line_of "$CUR_LTS"))."
        fi
    fi
}
CUR_LTS="$(fetch_version LTS.txt)"
CUR_LATEST="$(fetch_version VERSION.txt)"
check_channel_rules

# ---- Key: the service key, or fetch it with a management token ---------------
KEY="${SUPABASE_SERVICE_KEY:-}"
if [ -z "$KEY" ]; then
    [ -n "${SUPABASE_ACCESS_TOKEN:-}" ] || die "set SUPABASE_ACCESS_TOKEN (sbp_...) or SUPABASE_SERVICE_KEY"
    KEY="$(curl -sS --fail -H "Authorization: Bearer $SUPABASE_ACCESS_TOKEN" \
        "https://api.supabase.com/v1/projects/$REF/api-keys?reveal=true" |
        python3 -c 'import sys, json; print(next(k["api_key"] for k in json.load(sys.stdin) if k.get("name") == "service_role"))')" ||
        die "couldn't get the service key with that token"
fi
AUTH=(-H "Authorization: Bearer $KEY" -H "apikey: $KEY")
RESP="$(mktemp)"   # (the exit trap above removes it and every "$RESP.*" file)

# ---- Checksums: the updater refuses files without one, so refuse to publish without them -----------------------
# (runs before DRY_RUN returns and before anything is uploaded; no network)
PRE=(merge --version "$VER" --channel "$CHANNEL" --repo "$ARCHIVE_REPO" --out "$RESP.preflight")
for n in "${NAMES[@]}"; do PRE+=(--file "$n=${SRC[$n]}"); done
python3 scripts/versions_index.py "${PRE[@]}" ||
    die "cannot compute a size and SHA-256 for every file; the launcher would refuse this release"
rm -f "$RESP.preflight"

echo "Publishing v$VER to $SUPABASE_URL (bucket '$BUCKET', channel $CHANNEL)"
if [ "${DRY_RUN:-0}" = 1 ]; then echo "DRY_RUN: everything checks out; nothing uploaded, no lock taken."; exit 0; fi

# ---- HTTP: every call goes through here ----------------------------------------------------------------------------------------
# http <body file> <curl args...>   runs curl in the background of this shell (see above) and sets
#   HTTP_CODE  the status, or 000 when there was no complete answer: no answer, OR AN ANSWER THAT BROKE OFF (curl fails after the
#              server said 200 and promised more than it sent: the body file then holds a piece of the object, which must not be
#              taken for the whole object)
#   HTTP_RC    curl's own exit status
# The body file is emptied first, so a call that fails never shows the reply to an earlier call. The server's Date header is kept
# (CLOCK_OFFSET): lock times are the server's time.
HTTP_CODE=000 HTTP_RC=0 CLOCK_OFFSET=0
http() {
    local body="$1"
    shift
    : > "$body"
    : > "$RESP.hdr"
    : > "$RESP.code"
    curl -sS -D "$RESP.hdr" -o "$body" -w '%{http_code}' "$@" > "$RESP.code" 2>/dev/null &
    CURL_PID=$!
    HTTP_RC=0
    wait "$CURL_PID" || HTTP_RC=$?
    CURL_PID=""
    HTTP_CODE=000
    if [ "$HTTP_RC" = 0 ]; then
        HTTP_CODE="$(cat "$RESP.code" 2>/dev/null || true)"
        [[ "$HTTP_CODE" =~ ^[0-9]{3}$ ]] || HTTP_CODE=000
    fi
    server_clock
    return 0
}
server_clock() {   # remember how far the server's clock is from ours
    local d s
    d="$(sed -n '/^[Dd][Aa][Tt][Ee]:/{s/^[Dd][Aa][Tt][Ee]: *//;p;q;}' "$RESP.hdr" 2>/dev/null | tr -d '\r')"
    if [ -n "$d" ] && s="$(date -d "$d" +%s 2>/dev/null)"; then CLOCK_OFFSET=$(( s - $(date +%s) )); fi
    return 0
}
now_s() { printf '%s' "$(( $(date +%s) + CLOCK_OFFSET ))"; }   # the server's idea of "now", in seconds

# ---- Bucket (public reads; writes need the service key) ----------------------
http "$RESP" --max-time 60 -X POST "$SUPABASE_URL/storage/v1/bucket" "${AUTH[@]}" \
    -H "Content-Type: application/json" \
    -d "{\"id\":\"$BUCKET\",\"name\":\"$BUCKET\",\"public\":true,\"file_size_limit\":$MAX_BYTES}"
if [ "$HTTP_CODE" != 200 ] && ! grep -qi "exist" "$RESP"; then die "creating the bucket failed ($HTTP_CODE): $(head -c 300 "$RESP")"; fi

# ---- Reading the bucket as it is NOW -------------------------------------------------------------------------------
# api_get <name in bucket> <file>: the object through the authenticated API, which is not served from the CDN cache that the public
# address is (the docs say an overwritten file can stay stale at the edge for a while). Sets HTTP_CODE; the body is in <file>.
# Everything that decides what to write under the lock is read with this.
api_get() {
    http "$2" --max-time 30 -H 'Cache-Control: no-cache' "${AUTH[@]}" "$SUPABASE_URL/storage/v1/object/$BUCKET/$1?nocache=$(date +%s)$RANDOM"
}
# The storage API says "this object does not exist" with 404, and some versions with 400 and the 404 in the body (and "this object
# exists already" with 409, or 400 and the 409 in the body); both spellings are accepted. is_x <http code> <body file>
# (scripts/release-rehearsal.sh carries the same two patterns; a test keeps them equal.)
is_missing() {
    [ "$1" = 404 ] && return 0
    [ "$1" = 400 ] && grep -qiE '"statusCode" *: *"?404"?|not.found|does not exist|no such' "$2" 2>/dev/null
}
is_duplicate() {
    [ "$1" = 409 ] && return 0
    [ "$1" = 400 ] && grep -qiE '"statusCode" *: *"?409"?|already.exist|duplicate' "$2" 2>/dev/null
}
# retry_pause <try just made>: between the tries of a read that must succeed (pauses of 1x, 2x, ... the base; none after the last try)
retry_pause() {
    if [ "$1" -lt "$READ_TRIES" ]; then nap $(( READ_RETRY_BASE_SECONDS * $1 )); fi
    return 0
}
# bucket_version <file in bucket>: sets BV to the version on its first line, or "" when the file is not there. An unreadable bucket
# stops the publish: guessing "not there" could move "latest" backwards.
BV=""
bucket_version() {
    local f="$RESP.bv" i v
    BV=""
    for ((i = 1; i <= READ_TRIES; i++)); do
        api_get "$1" "$f"
        if [ "$HTTP_CODE" = 200 ]; then
            v="$(head -n1 "$f" | tr -d '\r ' || true)"
            if [[ "$v" =~ ^[0-9]+(\.[0-9]+)+$ ]]; then BV="$v"; fi
            return 0
        fi
        if is_missing "$HTTP_CODE" "$f"; then return 0; fi
        retry_pause "$i"
    done
    die "cannot read $1 from the bucket (HTTP $HTTP_CODE): not publishing without knowing what is published now"
}

# ---- The release lock ----------------------------------------------------------------------------------------------
LOCK_FILE="$RESP.lock" LOCK_IN="$RESP.lockin" LOCK_TOUCHED=0
LOCK_TOKEN="$(python3 -c 'import uuid; print(uuid.uuid4().hex)' 2>/dev/null || true)"
[ -n "$LOCK_TOKEN" ] || LOCK_TOKEN="$(date +%s)-$$-$RANDOM$RANDOM"
if [ -n "${GITHUB_RUN_ID:-}" ]; then
    _att="${GITHUB_RUN_ATTEMPT:-1}"
    LOCK_HOLDER="github-run-${GITHUB_RUN_ID//[^0-9]/}-attempt-${_att//[^0-9]/}"
else
    LOCK_HOLDER="local-pid-$$"   # (no host or user name: the bucket is public)
fi
iso_of() { date -u -d "@$1" +%Y-%m-%dT%H:%M:%SZ; }
# The lock object: who holds it, for which version, and when it was last renewed (epoch = seconds, utc = the same for people).
lock_body() {
    local t
    t="$(now_s)"
    printf 'okayspace-release-lock-v1\nversion=%s\nchannel=%s\nholder=%s\ntoken=%s\nutc=%s\nepoch=%s\n' \
        "$VER" "$CHANNEL" "$LOCK_HOLDER" "$LOCK_TOKEN" "$(iso_of "$t")" "$t"
}
claim_body() {   # claim_body <token of the dead lock it is for>
    local t
    t="$(now_s)"
    printf 'okayspace-release-lock-claim-v1\ntoken=%s\nfor=%s\nholder=%s\nutc=%s\nepoch=%s\n' "$LOCK_TOKEN" "$1" "$LOCK_HOLDER" "$(iso_of "$t")" "$t"
}
lock_field() { sed -n "/^$2=/{s/^$2=//;p;q;}" "$1" | tr -d '\r'; }   # lock_field <lock file> <name>
# put_object <name in bucket> <local file> <upsert true|false>: one POST of a small text object; HTTP_CODE says how it went
put_object() {
    http "$RESP.put" --max-time 60 -X POST "$SUPABASE_URL/storage/v1/object/$BUCKET/$1" "${AUTH[@]}" \
        -H "x-upsert: $3" -H 'Content-Type: text/plain' -H 'cache-control: max-age=0' --data-binary @"$2"
}
# delete_object <name in bucket>: one DELETE call (the same call that prunes old versions); HTTP_CODE says how it went
delete_object() {
    http "$RESP.del" --max-time 30 -X DELETE "$SUPABASE_URL/storage/v1/object/$BUCKET" "${AUTH[@]}" \
        -H 'Content-Type: application/json' -d "{\"prefixes\":[\"$1\"]}"
}
drop_claim() {   # give the takeover claim back (best effort: a claim nobody finished is removed by the next waiter anyway)
    [ -n "$CLAIM_NAME" ] || return 0
    local c="$CLAIM_NAME"
    CLAIM_NAME=""
    delete_object "$c"
    return 0
}
# takeover_stale <token> <version> <holder> <utc> <age>: the lock looks dead. Only one waiter may delete it, so it is CLAIMED first:
# release.lock.claim-<token> is created without upsert (an atomic create: one waiter gets it). With the claim, look at the lock again
# and delete it only if it is still that lock and still older than the limit by its LATEST time stamp (a holder that renewed meanwhile
# keeps its lock). Returns 0 when the dead lock was deleted (the caller then creates its own at once), 1 when it was not.
takeover_stale() {
    local tok="$1" claim e age2=""
    if ! [[ "$tok" =~ ^[A-Za-z0-9._-]{1,100}$ ]]; then
        echo "  (the lock's token cannot be used to claim it, so it is not taken over; delete $LOCK_NAME from the bucket by hand if it is dead)" >&2
        return 1
    fi
    claim="$LOCK_NAME.claim-$tok"
    claim_body "$tok" > "$LOCK_FILE.claim"
    put_object "$claim" "$LOCK_FILE.claim" false
    if [ "$HTTP_CODE" = 200 ] || [ "$HTTP_CODE" = 201 ]; then
        CLAIM_NAME="$claim"
    elif is_duplicate "$HTTP_CODE" "$RESP.put"; then
        # Another waiter holds the claim. If it is old, that waiter died before it finished: remove the claim; the next look can claim.
        api_get "$claim" "$RESP.claim"
        if [ "$HTTP_CODE" = 200 ]; then
            e="$(lock_field "$RESP.claim" epoch)"
            if [[ "$e" =~ ^[0-9]+$ ]] && (( $(now_s) - e > LOCK_CLAIM_STALE_SECONDS )); then
                warn "a takeover of the dead release lock was started and never finished (claim $claim is $(( $(now_s) - e )) s old): removing the claim"
                delete_object "$claim"
            fi
        fi
        return 1
    else
        return 1   # (no clean answer: the next look tries again)
    fi
    # We hold the claim: look at the lock once more.
    api_get "$LOCK_NAME" "$LOCK_IN"
    if [ "$HTTP_CODE" = 200 ] && [ "$(lock_field "$LOCK_IN" token)" = "$tok" ]; then
        e="$(lock_field "$LOCK_IN" epoch)"
        if [[ "$e" =~ ^[0-9]+$ ]]; then age2=$(( $(now_s) - e )); fi
        if [ -n "$age2" ] && (( age2 > LOCK_STALE_SECONDS )); then
            warn "the release lock is $((age2 / 60)) min old (v$2, held by $3, last renewed $4) and was never renewed: that publish died or is stuck. TAKING IT OVER now."
            delete_object "$LOCK_NAME"
            if [ "$HTTP_CODE" = 200 ]; then
                drop_claim
                return 0
            fi
        else
            echo "  (the lock was renewed meanwhile: its holder is alive, so it is not taken over)"
        fi
    fi
    drop_claim
    return 1
}
acquire_lock() {
    local started=$SECONDS tries=0 note=-1000 errs=0 hv="" hh="" hu="" he="" ht="" age="?" who
    while :; do
        if (( tries > 0 && SECONDS - started >= LOCK_WAIT_SECONDS )); then
            who="an unknown publish"; if [ -n "$hh" ]; then who="$hh (v$hv, last renewed $hu)"; fi
            die "gave up after $LOCK_WAIT_SECONDS s waiting for the release lock, held by $who. Nothing was published. If that publish is really gone, delete $LOCK_NAME from the '$BUCKET' bucket (or wait: a lock that is not renewed for $LOCK_STALE_SECONDS s is taken over) and run this again"
        fi
        tries=$((tries + 1))
        lock_body > "$LOCK_FILE"
        LOCK_MAYBE=1   # (from here on a lock of ours may exist, whatever the answer says: the answer can be lost)
        put_object "$LOCK_NAME" "$LOCK_FILE" false
        if [ "$HTTP_CODE" = 200 ] || [ "$HTTP_CODE" = 201 ]; then
            LOCK_HELD=1; LOCK_MAYBE=0; LOCK_TOUCHED=$SECONDS
            echo "  took the release lock ($LOCK_NAME) for v$VER after $((SECONDS - started)) s"
            return 0
        fi
        if ! is_duplicate "$HTTP_CODE" "$RESP.put"; then
            # Neither "created" nor "exists already": publishing without the lock is not safe, so stop after a few tries.
            errs=$((errs + 1))
            if (( errs >= 5 )); then die "cannot take the release lock: HTTP $HTTP_CODE from the storage API: $(head -c 300 "$RESP.put" 2>/dev/null)"; fi
            echo "  taking the release lock failed (HTTP $HTTP_CODE); trying again" >&2
            nap "$LOCK_POLL_SECONDS"
            continue
        fi
        errs=0
        # It exists. Whose is it? (It can be ours: an earlier create may have gone through while its answer was lost.)
        api_get "$LOCK_NAME" "$LOCK_IN"
        if [ "$HTTP_CODE" = 200 ]; then
            ht="$(lock_field "$LOCK_IN" token)"; hv="$(lock_field "$LOCK_IN" version)"; hh="$(lock_field "$LOCK_IN" holder)"
            hu="$(lock_field "$LOCK_IN" utc)"; he="$(lock_field "$LOCK_IN" epoch)"
            if [ -n "$ht" ] && [ "$ht" = "$LOCK_TOKEN" ]; then
                LOCK_HELD=1; LOCK_MAYBE=0; LOCK_TOUCHED=$SECONDS
                echo "  took the release lock ($LOCK_NAME) for v$VER after $((SECONDS - started)) s"
                return 0
            fi
            LOCK_MAYBE=0   # (someone else's lock)
            if [[ "$he" =~ ^[0-9]+$ ]]; then
                age=$(( $(now_s) - he ))
                if (( age > LOCK_STALE_SECONDS )); then
                    if takeover_stale "$ht" "$hv" "$hh" "$hu" "$age"; then continue; fi
                fi
            else
                age="?"
            fi
            if (( SECONDS - note >= 60 )); then
                echo "  $LOCK_NAME is held by ${hh:-an unknown publish} (v${hv:-?}, renewed $age s ago); waiting for it, up to $((LOCK_WAIT_SECONDS - (SECONDS - started))) s more"
                if [[ "$he" =~ ^[0-9]+$ ]] && (( age < -300 )); then
                    echo "  (its time stamp is in the future: a clock is wrong somewhere, so it is not taken over)"
                elif [[ ! "$he" =~ ^[0-9]+$ ]]; then
                    echo "  (it has no readable time stamp, so it is not taken over; delete $LOCK_NAME from the bucket by hand if it is dead)"
                fi
                note=$SECONDS
            fi
        elif is_missing "$HTTP_CODE" "$LOCK_IN"; then
            LOCK_MAYBE=0
            nap 1   # released between our create and our look: try again at once
            continue
        else
            echo "  cannot read the release lock (HTTP $HTTP_CODE); trying again" >&2
        fi
        nap $(( LOCK_POLL_SECONDS + RANDOM % (LOCK_POLL_SECONDS + 1) ))
    done
}
# lock_touch: we must still hold the lock; renew its time stamp. Stops the publish when the lock is gone or someone else holds it
# (this publish stalled so long that it was taken over): better to stop than to commit over a newer publish.
lock_touch() {
    [ "$LOCK_HELD" = 1 ] || return 0
    local i code=""
    for ((i = 1; i <= READ_TRIES; i++)); do
        api_get "$LOCK_NAME" "$LOCK_IN"
        code="$HTTP_CODE"
        if [ "$code" = 200 ] || is_missing "$code" "$LOCK_IN"; then break; fi
        retry_pause "$i"
    done
    if [ "$code" = 200 ]; then
        if [ "$(lock_field "$LOCK_IN" token)" != "$LOCK_TOKEN" ]; then
            LOCK_HELD=0   # not ours any more: never delete it
            die "the release lock was taken over by $(lock_field "$LOCK_IN" holder) (v$(lock_field "$LOCK_IN" version)) while this publish of v$VER was running (it was not renewed for $LOCK_STALE_SECONDS s). Stopping before anything more is changed; run the publish again"
        fi
    elif is_missing "$code" "$LOCK_IN"; then
        LOCK_HELD=0
        die "the release lock ($LOCK_NAME) was deleted while this publish of v$VER was running. Stopping before anything more is changed; run the publish again"
    else
        die "cannot read the release lock (HTTP $code): stopping before anything more is changed"
    fi
    lock_body > "$LOCK_FILE"
    put_object "$LOCK_NAME" "$LOCK_FILE" true
    if [ "$HTTP_CODE" = 200 ]; then LOCK_TOUCHED=$SECONDS; else warn "could not renew the release lock (HTTP $HTTP_CODE); carrying on"; fi
}
# release_lock: give the lock back, but only if it is still ours. Safe to call twice; never stops the script. It is tried for about
# 30 s (a lock left behind blocks the next release for 20 minutes). LOCK_MAYBE: the create may have gone through without an answer.
release_lock() {
    [ "$LOCK_HELD" = 1 ] || [ "$LOCK_MAYBE" = 1 ] || return 0
    local was_held="$LOCK_HELD" i
    LOCK_HELD=0
    LOCK_MAYBE=0
    for ((i = 1; i <= READ_TRIES; i++)); do
        api_get "$LOCK_NAME" "$LOCK_IN"
        if [ "$HTTP_CODE" = 200 ]; then
            if [ "$(lock_field "$LOCK_IN" token)" != "$LOCK_TOKEN" ]; then
                if [ "$was_held" = 1 ]; then warn "the release lock belongs to $(lock_field "$LOCK_IN" holder) (v$(lock_field "$LOCK_IN" version)) now, not to this publish: leaving it alone"; fi
                return 0
            fi
            delete_object "$LOCK_NAME"
            if [ "$HTTP_CODE" = 200 ]; then echo "  gave the release lock back"; return 0; fi
            # (no clean answer: look again; the next look shows whether it went through)
        elif is_missing "$HTTP_CODE" "$LOCK_IN"; then
            if [ "$was_held" = 1 ]; then warn "the release lock was already gone when this publish ended"; fi
            return 0
        fi
        retry_pause "$i"
    done
    warn "could not give the release lock back (HTTP $HTTP_CODE): it expires after $LOCK_STALE_SECONDS s, or delete $LOCK_NAME from the '$BUCKET' bucket by hand"
    return 0
}

echo "Taking the release lock ($LOCK_NAME): releases are published one at a time"
acquire_lock

# ---- From here on this publish is the only one. What is published NOW (the answers read before the lock may be old) ---------------
bucket_version LTS.txt
CUR_LTS="$BV"
bucket_version VERSION.txt
CUR_LATEST="$BV"
check_channel_rules

# The index as it is now. (Nobody else can change it while we hold the lock, so this one read serves the checks below and the merge.)
# Never taken for "no index yet" unless the storage says so (404): a read that fails, breaks off, or returns something that is not a
# schema-1 index stops the publish, because rebuilding the index from nothing would erase the version history.
IDX_OLD="$RESP.idx.in"
read_index() {
    local i
    for ((i = 1; i <= READ_TRIES; i++)); do
        api_get versions.json "$IDX_OLD"
        if [ "$HTTP_CODE" = 200 ]; then
            if python3 scripts/versions_index.py validate --index "$IDX_OLD" 2> "$RESP.why"; then return 0; fi
            die "versions.json in the bucket is not a usable index ($(head -c 200 "$RESP.why")): not overwriting it. Fix or delete it by hand, then run the publish again"
        fi
        if is_missing "$HTTP_CODE" "$IDX_OLD"; then rm -f "$IDX_OLD"; return 0; fi   # no index yet is fine: the first publish
        retry_pause "$i"
    done
    die "cannot read versions.json from the bucket (HTTP $HTTP_CODE): not rebuilding the index from nothing"
}
read_index
FARGS=()
for n in "${NAMES[@]}"; do FARGS+=(--file "$n=${SRC[$n]}"); done

# Never publish an existing version again with different files (CLAUDE.md rule 12): users may already hold its checksums.
# The same files again are a safe re-run. (Checked before anything is uploaded: the uploads would replace the files.)
STATUS=new
if [ -f "$IDX_OLD" ]; then
    STATUS="$(python3 scripts/versions_index.py status --index "$IDX_OLD" --version "$VER" "${FARGS[@]}")" ||
        die "could not compare v$VER with versions.json"
fi
case "$STATUS" in
    new) ;;
    same) echo "v$VER is already published with exactly these files: publishing it again changes nothing (a safe re-run)." ;;
    different)
        if [ "${ALLOW_REPUBLISH:-0}" = 1 ]; then
            warn "ALLOW_REPUBLISH=1: replacing the files and checksums of v$VER that are already published"
        else
            die "v$VER is already published with DIFFERENT files. Publishing a version again would change checksums that users may already hold (CLAUDE.md rule 12: never re-publish an existing version). Bump the version, or set ALLOW_REPUBLISH=1 if replacing it is really what you want"
        fi ;;
    *) die "unexpected answer '$STATUS' from versions_index.py status" ;;
esac

# "latest" never moves backwards: an older version that finishes after a newer one is published (files and index entry, so it can be
# installed and has checksums) but VERSION.txt and NOTES.txt stay as they are.
SET_LATEST=1
if [ "$CHANNEL" = latest ] && [ -n "$CUR_LATEST" ] && version_newer "$CUR_LATEST" "$VER"; then
    SET_LATEST=0
    echo "v$VER is OLDER than the version that is latest now (v$CUR_LATEST). Publishing its files and its checksums so it can be installed, but VERSION.txt and NOTES.txt are left alone: latest does not move backwards."
fi

# upload <file> <path in bucket> <content-type> <cache seconds>
upload() {
    local why
    http "$RESP" --connect-timeout 30 --speed-limit 1024 --speed-time 120 --retry 3 --retry-all-errors -X POST \
        "$SUPABASE_URL/storage/v1/object/$BUCKET/$2" "${AUTH[@]}" \
        -H "x-upsert: true" -H "Content-Type: $3" -H "cache-control: max-age=$4" --data-binary @"$1"
    if [ "$HTTP_CODE" != 200 ]; then
        why="$HTTP_CODE"
        if [ "$HTTP_CODE" = 000 ]; then why="000, no complete answer, curl exit $HTTP_RC"; fi
        die "upload of $2 failed ($why): $(head -c 300 "$RESP")"
    fi
    echo "  uploaded $2 ($(( $(stat -c %s "$1") / 1024 )) KB)"
    # A slow publish must not look dead to the next one: renew the lock now and then (and check it is still ours).
    if [ "$LOCK_HELD" = 1 ] && (( SECONDS - LOCK_TOUCHED >= LOCK_RENEW_SECONDS )); then lock_touch; fi
}

for n in "${NAMES[@]}"; do upload "${SRC[$n]}" "v$VER/$n" application/octet-stream 3600; done

NOTES="$(mktemp)"
if [ -n "${NOTES_FILE:-}" ]; then cp "$NOTES_FILE" "$NOTES"
else git log -6 --no-merges --pretty='- %s' > "$NOTES" 2>/dev/null || echo "- OkaySpace v$VER" > "$NOTES"; fi
CHANGELOG="$NOTES"   # the editor's update dialog shows this
[ -f "$DIST_DIR/CHANGELOG.txt" ] && CHANGELOG="$DIST_DIR/CHANGELOG.txt"   # (dist/CHANGELOG.txt is years stale: not used)
upload "$CHANGELOG" "v$VER/CHANGELOG.txt" "text/plain; charset=utf-8" 3600
if [ "$CHANNEL" = lts ]; then NOTES_NAME=NOTES-LTS.txt; VERSION_NAME=LTS.txt; else NOTES_NAME=NOTES.txt; VERSION_NAME=VERSION.txt; fi
if [ "$SET_LATEST" = 1 ]; then upload "$NOTES" "$NOTES_NAME" "text/plain; charset=utf-8" 30; fi

# list_names <prefix>: the names under that prefix of the bucket, in the array LIST_NAMES. Returns 1 when the list could not be read.
LIST_NAMES=()
list_names() {
    LIST_NAMES=()
    http "$RESP.list" --max-time 60 -X POST "$SUPABASE_URL/storage/v1/object/list/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" \
        -d "{\"prefix\":\"$1\",\"limit\":1000}"
    [ "$HTTP_CODE" = 200 ] || return 1
    python3 -c 'import sys, json; [print(o["name"]) for o in json.load(open(sys.argv[1]))]' "$RESP.list" > "$RESP.names" 2>/dev/null || return 1
    mapfile -t LIST_NAMES < "$RESP.names"
    return 0
}
# Which old versions go (latest channel only; the LTS line is never pruned: its versions are what "stay on LTS" installs).
# Never the version being published, even when it is among the oldest (an older release finishing late): its folder stays until a
# later publish prunes it, so the index never says "in the bucket" about files that were just deleted.
OLD=()
if [ "$CHANNEL" = latest ]; then
    LTS_RE='^$'   # matches no version: nothing is protected
    if [ -n "$CUR_LTS" ]; then LTS_RE="^$(line_of "$CUR_LTS" | sed 's/\./\\./g')\\."; fi
    if list_names ""; then
        mapfile -t OLD < <(printf '%s\n' "${LIST_NAMES[@]}" | grep -E '^v[0-9.]+$' | sed 's/^v//' | { grep -vE "$LTS_RE" || true; } |
            sort -t. -k1,1n -k2,2n -k3,3n | head -n -"$KEEP" | { grep -vxF "$VER" || true; })
    else
        echo "  (could not list the bucket: no old versions are removed this time)"
    fi
fi

# read_back <name> <file>: the PUBLIC copy of an object (what launchers fetch), cache-busted. Returns 1 when it cannot be read.
read_back() {
    http "$2" --max-time 30 -H 'Cache-Control: no-cache' "$PUBLIC/$1?nocache=$(date +%s)$RANDOM"
    [ "$HTTP_CODE" = 200 ]
}
# Sign versions.json and publish the signature next to it; read both back and verify the PUBLISHED pair before VERSION.txt goes out.
# Called right after versions.json is uploaded (a launcher that fetches in between sees a mismatch and refuses: it retries on the next check).
sign_and_upload_index() {   # sign_and_upload_index <local versions.json>
    local sig="$RESP.idx.sig" pub back="$RESP.idx.sigback" body="$RESP.idx.pubbody" i ok=0
    if [ -z "${OKAY_UPDATE_SIGNING_SEED:-}" ]; then
        echo "  WARNING: versions.json is published UNSIGNED (OKAY_UPDATE_SIGNING_SEED is not set): launchers built with OKAY_UPDATE_TRUSTED_KEYS will refuse it"
        return 0
    fi
    python3 scripts/sign_index.py sign --file "$1" --out "$sig" || die "could not sign versions.json"
    pub="$(python3 scripts/sign_index.py pubkey)" || die "could not derive the public key"
    upload "$sig" versions.json.sig "text/plain" 30
    for ((i = 1; i <= READ_TRIES; i++)); do   # (the public copy may need a moment to show the new pair)
        if read_back versions.json.sig "$back" && read_back versions.json "$body" &&
           python3 scripts/sign_index.py verify --file "$body" --sig "$back" --pub "$pub" >/dev/null 2>&1; then ok=1; break; fi
        retry_pause "$i"
    done
    [ "$ok" = 1 ] || die "the published versions.json cannot be read back, or does not verify against the published signature"
    rm -f "$sig" "$back" "$body"
    echo "  signed versions.json (public key ${pub:0:16}...)"
}

# ---- versions.json: every released version with sizes and SHA-256 ---------------
# REQUIRED: the launcher's updater refuses a version whose files have no checksum here. It goes up BEFORE VERSION.txt and is read
# back from the bucket and compared with the files that were uploaded; any failure stops the publish (VERSION.txt is not written,
# so no launcher is offered this version). Re-running the publish is safe when the files are the same (see the rule 12 check above).
# The index it starts from is the one read under the lock (IDX_OLD); "latest" in it is the newer of the current latest and this version.
update_index() {
    local new="$RESP.idx.out" back="$RESP.idx.back" latest="$CUR_LATEST" lts="$CUR_LTS" inbucket v i ok=0
    if [ "$CHANNEL" = lts ]; then lts="$VER"; elif [ "$SET_LATEST" = 1 ]; then latest="$VER"; fi
    list_names "" || die "could not list the bucket for versions.json"
    inbucket="$(printf '%s\n' "${LIST_NAMES[@]}" | grep -E '^v[0-9.]+$' | sed 's/^v//')" || die "could not list the bucket for versions.json"
    for v in "${OLD[@]}"; do inbucket="$(printf '%s\n' "$inbucket" | grep -vxF "$v" || true)"; done   # (about to be pruned)
    inbucket="$(printf '%s\n' "$inbucket" | tr '\n' ' ')"
    local args=(merge --version "$VER" --channel "$CHANNEL" --date "$(date -u +%F)"
                --repo "$ARCHIVE_REPO" --bucket-versions "$inbucket" --out "$new")
    if [ -f "$IDX_OLD" ]; then args+=(--existing "$IDX_OLD"); fi
    if [ -n "$latest" ]; then args+=(--latest "$latest"); fi
    if [ -n "$lts" ]; then args+=(--lts "$lts"); fi
    python3 scripts/versions_index.py "${args[@]}" "${FARGS[@]}" || die "versions.json could not be built with a checksum for every file"
    # The new index must list every version the old one listed (a merge only adds or replaces one entry).
    if [ -f "$IDX_OLD" ]; then
        python3 scripts/versions_index.py superset --old "$IDX_OLD" --new "$new" ||
            die "the new versions.json would list fewer versions than the one in the bucket: not uploading it"
    fi
    lock_touch   # still ours? (the commit starts here)
    upload "$new" versions.json "application/json" 30
    sign_and_upload_index "$new"
    for ((i = 1; i <= READ_TRIES; i++)); do
        if read_back versions.json "$back" && python3 scripts/versions_index.py check --index "$back" --version "$VER" "${FARGS[@]}" 2>"$RESP.why"; then ok=1; break; fi
        retry_pause "$i"
    done
    [ "$ok" = 1 ] || die "the published versions.json cannot be read back, or does not carry the checksums of the uploaded files: $(head -c 200 "$RESP.why" 2>/dev/null)"
    rm -f "$IDX_OLD" "$new" "$back"
}
update_index   # (die exits the whole script: no VERSION.txt, no pruning)

if [ "$SET_LATEST" = 1 ]; then
    lock_touch   # still ours? (nothing is offered to launchers until the next line)
    printf '%s\n' "$VER" > "$RESP.ver"
    upload "$RESP.ver" "$VERSION_NAME" text/plain 30
    rm -f "$RESP.ver"
else
    echo "  left $VERSION_NAME at v$CUR_LATEST"
fi

# ---- Keep only the newest $KEEP versions ------------------------------------------
for v in "${OLD[@]}"; do
    list_names "v$v/" || continue
    [ "${#LIST_NAMES[@]}" -gt 0 ] || continue
    body=$(printf '%s\n' "${LIST_NAMES[@]}" | python3 -c 'import sys, json; v = sys.argv[1]; print(json.dumps({"prefixes": ["v%s/%s" % (v, l.strip()) for l in sys.stdin if l.strip()]}))' "$v")
    http "$RESP.del" --max-time 60 -X DELETE "$SUPABASE_URL/storage/v1/object/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" -d "$body"
    if [ "$HTTP_CODE" = 200 ]; then echo "  removed old v$v"; else warn "could not remove old v$v (HTTP $HTTP_CODE); the next publish tries again"; fi
done

release_lock
if [ "$SET_LATEST" = 1 ]; then
    echo "Done: launchers will see v$VER at $PUBLIC/$VERSION_NAME"
else
    echo "Done: v$VER is published and can be installed from the version list; v$CUR_LATEST stays the latest ($PUBLIC/$VERSION_NAME)"
fi
