#!/usr/bin/env bash
# Publish a Windows build to Supabase Storage, so launchers and editors update
# from there without a GitHub workflow. Run it after scripts/build-windows-dist.sh.
#
#   SUPABASE_ACCESS_TOKEN=sbp_...  scripts/publish-supabase.sh   # management token (the service key is fetched)
#   SUPABASE_SERVICE_KEY=eyJ...    scripts/publish-supabase.sh   # or the service_role key itself
#
# Keys come from the environment only: never put them in a file in this repo.
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
#     * Not renewed for LOCK_STALE_SECONDS: that publish is dead; the lock is deleted and taken over, with a loud warning.
#     * Given back when the script ends for any reason (success, an error, Ctrl-C, SIGTERM), but only if it is still ours. While it runs
#       the script renews the lock after uploads and checks it is still ours before it commits (versions.json, VERSION.txt).
#     * What is published now (VERSION.txt, LTS.txt, versions.json) is read under the lock through the authenticated API, never through
#       the public address: that one is served from a CDN cache, and a stale read would undo the lock.
#   DRY_RUN=1 takes no lock and writes nothing.
#   A version that is already listed in versions.json with DIFFERENT files is refused (never publish an existing version again: users may
#   hold its checksums; CLAUDE.md rule 12). The same files again are a safe re-run. ALLOW_REPUBLISH=1 replaces it on purpose.
#
# Options (environment):
#   CHANNEL      latest (default) or lts
#   DIST_DIR     build output to publish (default build-win-dist)
#   NOTES_FILE   bullet lines for NOTES.txt (default: the last 6 commit subjects)
#   KEEP         how many non-LTS versions to keep in the bucket (default 3)
#   SUPABASE_URL project URL (default: the one baked into CMakeLists.txt)
#   ARCHIVE_REPO GitHub repo holding every release, written into versions.json
#                (default $GITHUB_REPOSITORY, else kingimann/OkaySpaceGameEngine)
#   OKAY_UPDATE_SIGNING_SEED  64 hex digits, the private signing key. Unset = the index is published unsigned (a warning); only
#                            launchers built without OKAY_UPDATE_TRUSTED_KEYS accept that
#   REQUIRE_SIGNED=1         stop instead of publishing an unsigned index (set it once the launchers trust a key)
#   LOCK_WAIT_SECONDS        how long to wait for another publish (default 2400 = 40 min)
#   LOCK_STALE_SECONDS       a lock not renewed for this long is taken over (default 1200 = 20 min)
#   LOCK_POLL_SECONDS        pause between looks at a held lock, plus up to as much at random (default 10, so 10-20 s)
#   LOCK_RENEW_SECONDS       renew the lock after an upload when this long has passed since the last renewal (default 60)
#   ALLOW_REPUBLISH=1        publish over a version that is already listed with different files
#   DRY_RUN=1    check everything, upload nothing, take no lock
set -euo pipefail
cd "$(dirname "$0")/.."

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

# One place removes the temp files and gives the lock back, however the script ends: success, die, Ctrl-C, SIGTERM (a cancelled job).
# (release_lock is defined with the other lock functions below; it only runs once the lock is held.)
RESP="" NOTES="" LOCK_HELD=0
cleanup() {
    local rc=$?
    set +e
    if [ "$LOCK_HELD" = 1 ]; then release_lock; fi
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
for _n in LOCK_WAIT_SECONDS LOCK_STALE_SECONDS LOCK_POLL_SECONDS LOCK_RENEW_SECONDS; do
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

# ---- Bucket (public reads; writes need the service key) ----------------------
code=$(curl -sS -o "$RESP" -w '%{http_code}' --max-time 60 -X POST "$SUPABASE_URL/storage/v1/bucket" "${AUTH[@]}" \
    -H "Content-Type: application/json" \
    -d "{\"id\":\"$BUCKET\",\"name\":\"$BUCKET\",\"public\":true,\"file_size_limit\":$MAX_BYTES}")
if [ "$code" != 200 ] && ! grep -qi "exist" "$RESP"; then die "creating the bucket failed ($code): $(cat "$RESP")"; fi

# ---- Reading the bucket as it is NOW -------------------------------------------------------------------------------
# api_get <name in bucket> <file>: the object through the authenticated API, which is not served from the CDN cache that the public
# address is (the docs say an overwritten file can stay stale at the edge for a while). Prints the HTTP status (000: no answer); the body
# is in <file>. Everything that decides what to write under the lock is read with this.
api_get() {
    local code
    code="$(curl -sS -o "$2" -w '%{http_code}' --max-time 30 -H 'Cache-Control: no-cache' "${AUTH[@]}" \
        "$SUPABASE_URL/storage/v1/object/$BUCKET/$1?nocache=$(date +%s)$RANDOM" 2>/dev/null || true)"
    printf '%s' "${code:-000}"
}
# The storage API says "this object does not exist" with 404, and some versions with 400 and the 404 in the body (and "this object
# exists already" with 409, or 400 and the 409 in the body); both spellings are accepted. is_x <http code> <body file>
is_missing() {
    [ "$1" = 404 ] && return 0
    [ "$1" = 400 ] && grep -qiE '"statusCode" *: *"?404"?|not.found|does not exist|no such' "$2" 2>/dev/null
}
is_duplicate() {
    [ "$1" = 409 ] && return 0
    [ "$1" = 400 ] && grep -qiE '"statusCode" *: *"?409"?|already.exist|duplicate' "$2" 2>/dev/null
}
# bucket_version <file in bucket>: the version on its first line, or nothing when the file is not there. An unreadable bucket stops the
# publish: guessing "not there" could move "latest" backwards.
bucket_version() {
    local f="$RESP.bv" code="" v i
    for i in 1 2 3; do
        code="$(api_get "$1" "$f")"
        if [ "$code" = 200 ]; then
            v="$(head -n1 "$f" | tr -d '\r ' || true)"
            rm -f "$f"
            if [[ "$v" =~ ^[0-9]+(\.[0-9]+)+$ ]]; then printf '%s' "$v"; fi
            return 0
        fi
        if is_missing "$code" "$f"; then rm -f "$f"; return 0; fi
        if [ "$i" -lt 3 ]; then sleep "$i"; fi
    done
    die "cannot read $1 from the bucket (HTTP $code): not publishing without knowing what is published now"
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
# The lock object: who holds it, for which version, and when it was last renewed (epoch = seconds, utc = the same for people).
lock_body() {
    printf 'okayspace-release-lock-v1\nversion=%s\nchannel=%s\nholder=%s\ntoken=%s\nutc=%s\nepoch=%s\n' \
        "$VER" "$CHANNEL" "$LOCK_HOLDER" "$LOCK_TOKEN" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$(date +%s)"
}
lock_field() { sed -n "/^$2=/{s/^$2=//;p;q;}" "$1" | tr -d '\r'; }   # lock_field <lock file> <name>
delete_lock() {   # delete release.lock (the same call that prunes old versions); 0 when the API said OK
    local code="" i
    for i in 1 2 3; do
        code="$(curl -sS -o "$RESP.del" -w '%{http_code}' --max-time 30 -X DELETE "$SUPABASE_URL/storage/v1/object/$BUCKET" "${AUTH[@]}" \
            -H 'Content-Type: application/json' -d "{\"prefixes\":[\"$LOCK_NAME\"]}" 2>/dev/null || true)"
        if [ "${code:-000}" = 200 ]; then return 0; fi
        if [ "$i" -lt 3 ]; then sleep 1; fi
    done
    return 1
}
acquire_lock() {
    local started=$SECONDS tries=0 note=-1000 errs=0 code lcode hv="" hh="" hu="" he="" ht="" age="?" who
    while :; do
        if (( tries > 0 && SECONDS - started >= LOCK_WAIT_SECONDS )); then
            who="an unknown publish"; if [ -n "$hh" ]; then who="$hh (v$hv, last renewed $hu)"; fi
            die "gave up after $LOCK_WAIT_SECONDS s waiting for the release lock, held by $who. Nothing was published. If that publish is really gone, delete $LOCK_NAME from the '$BUCKET' bucket (or wait: a lock that is not renewed for $LOCK_STALE_SECONDS s is taken over) and run this again"
        fi
        tries=$((tries + 1))
        lock_body > "$LOCK_FILE"
        code="$(curl -sS -o "$RESP" -w '%{http_code}' --max-time 60 -X POST "$SUPABASE_URL/storage/v1/object/$BUCKET/$LOCK_NAME" "${AUTH[@]}" \
            -H 'x-upsert: false' -H 'Content-Type: text/plain' -H 'cache-control: max-age=0' --data-binary @"$LOCK_FILE" 2>/dev/null || true)"
        code="${code:-000}"
        if [ "$code" = 200 ] || [ "$code" = 201 ]; then
            LOCK_HELD=1; LOCK_TOUCHED=$SECONDS
            echo "  took the release lock ($LOCK_NAME) for v$VER after $((SECONDS - started)) s"
            return 0
        fi
        if ! is_duplicate "$code" "$RESP"; then
            # Neither "created" nor "exists already": publishing without the lock is not safe, so stop after a few tries.
            errs=$((errs + 1))
            if (( errs >= 5 )); then die "cannot take the release lock: HTTP $code from the storage API: $(head -c 300 "$RESP" 2>/dev/null)"; fi
            echo "  taking the release lock failed (HTTP $code); trying again" >&2
            sleep "$LOCK_POLL_SECONDS"
            continue
        fi
        errs=0
        # It exists. Whose is it? (It can be ours: an earlier create may have gone through while its answer was lost.)
        lcode="$(api_get "$LOCK_NAME" "$LOCK_IN")"
        if [ "$lcode" = 200 ]; then
            ht="$(lock_field "$LOCK_IN" token)"; hv="$(lock_field "$LOCK_IN" version)"; hh="$(lock_field "$LOCK_IN" holder)"
            hu="$(lock_field "$LOCK_IN" utc)"; he="$(lock_field "$LOCK_IN" epoch)"
            if [ -n "$ht" ] && [ "$ht" = "$LOCK_TOKEN" ]; then
                LOCK_HELD=1; LOCK_TOUCHED=$SECONDS
                echo "  took the release lock ($LOCK_NAME) for v$VER after $((SECONDS - started)) s"
                return 0
            fi
            if [[ "$he" =~ ^[0-9]+$ ]]; then
                age=$(( $(date +%s) - he ))
                if (( age > LOCK_STALE_SECONDS )); then
                    warn "the release lock is $((age / 60)) min old (v$hv, held by $hh, last renewed $hu) and was never renewed: that publish died or is stuck. TAKING IT OVER now."
                    sleep $((RANDOM % 4))   # (two waiters can get here together: the second finds a changed lock and backs off)
                    lcode="$(api_get "$LOCK_NAME" "$LOCK_IN")"
                    if [ "$lcode" = 200 ] && [ "$(lock_field "$LOCK_IN" token)" = "$ht" ]; then delete_lock || true; fi
                    continue
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
        elif is_missing "$lcode" "$LOCK_IN"; then
            sleep 1   # released between our create and our look: try again at once
            continue
        else
            echo "  cannot read the release lock (HTTP $lcode); trying again" >&2
        fi
        sleep $(( LOCK_POLL_SECONDS + RANDOM % (LOCK_POLL_SECONDS + 1) ))
    done
}
# lock_touch: we must still hold the lock; renew its time stamp. Stops the publish when the lock is gone or someone else holds it
# (this publish stalled so long that it was taken over): better to stop than to commit over a newer publish.
lock_touch() {
    [ "$LOCK_HELD" = 1 ] || return 0
    local code="" i ht
    for i in 1 2 3; do
        code="$(api_get "$LOCK_NAME" "$LOCK_IN")"
        if [ "$code" = 200 ] || is_missing "$code" "$LOCK_IN"; then break; fi
        if [ "$i" -lt 3 ]; then sleep "$i"; fi
    done
    if [ "$code" = 200 ]; then
        ht="$(lock_field "$LOCK_IN" token)"
        if [ "$ht" != "$LOCK_TOKEN" ]; then
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
    code="$(curl -sS -o "$RESP.renew" -w '%{http_code}' --max-time 60 -X POST "$SUPABASE_URL/storage/v1/object/$BUCKET/$LOCK_NAME" "${AUTH[@]}" \
        -H 'x-upsert: true' -H 'Content-Type: text/plain' -H 'cache-control: max-age=0' --data-binary @"$LOCK_FILE" 2>/dev/null || true)"
    if [ "${code:-000}" = 200 ]; then LOCK_TOUCHED=$SECONDS; else warn "could not renew the release lock (HTTP ${code:-000}); carrying on"; fi
}
# release_lock: give the lock back, but only if it is still ours. Safe to call twice; never stops the script.
release_lock() {
    [ "$LOCK_HELD" = 1 ] || return 0
    LOCK_HELD=0
    local code="" i
    for i in 1 2 3; do
        code="$(api_get "$LOCK_NAME" "$LOCK_IN")"
        if [ "$code" = 200 ] || is_missing "$code" "$LOCK_IN"; then break; fi
        if [ "$i" -lt 3 ]; then sleep 1; fi
    done
    if [ "$code" = 200 ]; then
        if [ "$(lock_field "$LOCK_IN" token)" != "$LOCK_TOKEN" ]; then
            warn "the release lock belongs to $(lock_field "$LOCK_IN" holder) (v$(lock_field "$LOCK_IN" version)) now, not to this publish: leaving it alone"
            return 0
        fi
    elif is_missing "$code" "$LOCK_IN"; then
        warn "the release lock was already gone when this publish ended"
        return 0
    else
        warn "cannot read the release lock (HTTP $code), so it is NOT deleted: it expires after $LOCK_STALE_SECONDS s, or delete $LOCK_NAME from the '$BUCKET' bucket"
        return 0
    fi
    if delete_lock; then echo "  gave the release lock back"
    else warn "could not delete $LOCK_NAME: it expires after $LOCK_STALE_SECONDS s, or delete it from the '$BUCKET' bucket by hand"; fi
    return 0
}

echo "Taking the release lock ($LOCK_NAME): releases are published one at a time"
acquire_lock

# ---- From here on this publish is the only one. What is published NOW (the answers read before the lock may be old) ---------------
CUR_LTS="$(bucket_version LTS.txt)"
CUR_LATEST="$(bucket_version VERSION.txt)"
check_channel_rules

# The index as it is now. (Nobody else can change it while we hold the lock, so this one read serves the checks below and the merge.)
IDX_OLD="$RESP.idx.in"
read_index() {
    local code="" i
    for i in 1 2 3; do
        code="$(api_get versions.json "$IDX_OLD")"
        if [ "$code" = 200 ]; then return 0; fi
        if is_missing "$code" "$IDX_OLD"; then rm -f "$IDX_OLD"; return 0; fi   # no index yet is fine: the first publish
        if [ "$i" -lt 3 ]; then sleep "$i"; fi
    done
    die "cannot read versions.json from the bucket (HTTP $code): not rebuilding the index from nothing"
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
    local code
    code=$(curl -sS -o "$RESP" -w '%{http_code}' --connect-timeout 30 --speed-limit 1024 --speed-time 120 --retry 3 --retry-all-errors -X POST \
        "$SUPABASE_URL/storage/v1/object/$BUCKET/$2" "${AUTH[@]}" \
        -H "x-upsert: true" -H "Content-Type: $3" -H "cache-control: max-age=$4" \
        --data-binary @"$1" || true)
    code="${code:-000}"
    [ "$code" = 200 ] || die "upload of $2 failed ($code): $(cat "$RESP")"
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

list() {   # list <prefix>  -> one name per line
    curl -sS --max-time 60 -X POST "$SUPABASE_URL/storage/v1/object/list/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" \
        -d "{\"prefix\":\"$1\",\"limit\":1000}" | python3 -c 'import sys, json; [print(o["name"]) for o in json.load(sys.stdin)]'
}
# Which old versions go (latest channel only; the LTS line is never pruned: its versions are what "stay on LTS" installs).
# Never the version being published, even when it is among the oldest (an older release finishing late): its folder stays until a
# later publish prunes it, so the index never says "in the bucket" about files that were just deleted.
OLD=()
if [ "$CHANNEL" = latest ]; then
    LTS_RE='^$'   # matches no version: nothing is protected
    if [ -n "$CUR_LTS" ]; then LTS_RE="^$(line_of "$CUR_LTS" | sed 's/\./\\./g')\\."; fi
    mapfile -t OLD < <(list "" | grep -E '^v[0-9.]+$' | sed 's/^v//' | { grep -vE "$LTS_RE" || true; } |
        sort -t. -k1,1n -k2,2n -k3,3n | head -n -"$KEEP" | { grep -vxF "$VER" || true; })
fi

# Sign versions.json and publish the signature next to it; read both back and verify the PUBLISHED pair before VERSION.txt goes out.
# Called right after versions.json is uploaded (a launcher that fetches in between sees a mismatch and refuses: it retries on the next check).
sign_and_upload_index() {   # sign_and_upload_index <local versions.json>
    local sig="$RESP.idx.sig" pub back="$RESP.idx.sigback" body="$RESP.idx.pubbody"
    if [ -z "${OKAY_UPDATE_SIGNING_SEED:-}" ]; then
        echo "  WARNING: versions.json is published UNSIGNED (OKAY_UPDATE_SIGNING_SEED is not set): launchers built with OKAY_UPDATE_TRUSTED_KEYS will refuse it"
        return 0
    fi
    python3 scripts/sign_index.py sign --file "$1" --out "$sig" || die "could not sign versions.json"
    pub="$(python3 scripts/sign_index.py pubkey)" || die "could not derive the public key"
    upload "$sig" versions.json.sig "text/plain" 30
    curl -fsS --max-time 30 -H 'Cache-Control: no-cache' "$PUBLIC/versions.json.sig?nocache=$(date +%s)" -o "$back" ||
        die "versions.json.sig cannot be read back from the bucket"
    curl -fsS --max-time 30 -H 'Cache-Control: no-cache' "$PUBLIC/versions.json?nocache=$(date +%s)" -o "$body" ||
        die "versions.json cannot be read back from the bucket"
    python3 scripts/sign_index.py verify --file "$body" --sig "$back" --pub "$pub" >/dev/null ||
        die "the published versions.json does not verify against the published signature"
    rm -f "$sig" "$back" "$body"
    echo "  signed versions.json (public key ${pub:0:16}...)"
}

# ---- versions.json: every released version with sizes and SHA-256 ---------------
# REQUIRED: the launcher's updater refuses a version whose files have no checksum here. It goes up BEFORE VERSION.txt and is read
# back from the bucket and compared with the files that were uploaded; any failure stops the publish (VERSION.txt is not written,
# so no launcher is offered this version). Re-running the publish is safe when the files are the same (see the rule 12 check above).
# The index it starts from is the one read under the lock (IDX_OLD); "latest" in it is the newer of the current latest and this version.
update_index() {
    local new="$RESP.idx.out" back="$RESP.idx.back" latest="$CUR_LATEST" lts="$CUR_LTS" n
    if [ "$CHANNEL" = lts ]; then lts="$VER"; elif [ "$SET_LATEST" = 1 ]; then latest="$VER"; fi
    local inbucket v
    inbucket="$(list "" | grep -E '^v[0-9.]+$' | sed 's/^v//')" || die "could not list the bucket for versions.json"
    for v in "${OLD[@]}"; do inbucket="$(printf '%s\n' "$inbucket" | grep -vxF "$v" || true)"; done   # (about to be pruned)
    inbucket="$(printf '%s\n' "$inbucket" | tr '\n' ' ')"
    local args=(merge --existing "$IDX_OLD" --version "$VER" --channel "$CHANNEL" --date "$(date -u +%F)"
                --repo "$ARCHIVE_REPO" --bucket-versions "$inbucket" --out "$new")
    if [ -n "$latest" ]; then args+=(--latest "$latest"); fi
    if [ -n "$lts" ]; then args+=(--lts "$lts"); fi
    python3 scripts/versions_index.py "${args[@]}" "${FARGS[@]}" || die "versions.json could not be built with a checksum for every file"
    lock_touch   # still ours? (the commit starts here)
    upload "$new" versions.json "application/json" 30
    sign_and_upload_index "$new"
    curl -fsS --max-time 30 -H 'Cache-Control: no-cache' "$PUBLIC/versions.json?nocache=$(date +%s)" -o "$back" ||
        die "versions.json cannot be read back from the bucket"
    python3 scripts/versions_index.py check --index "$back" --version "$VER" "${FARGS[@]}" ||
        die "the published versions.json does not carry the checksums of the uploaded files"
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
    mapfile -t FILES < <(list "v$v/")
    [ "${#FILES[@]}" -gt 0 ] || continue
    body=$(printf '%s\n' "${FILES[@]}" | python3 -c 'import sys, json; v = sys.argv[1]; print(json.dumps({"prefixes": ["v%s/%s" % (v, l.strip()) for l in sys.stdin if l.strip()]}))' "$v")
    curl -sS -o /dev/null --max-time 60 -X DELETE "$SUPABASE_URL/storage/v1/object/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" -d "$body"
    echo "  removed old v$v"
done

release_lock
if [ "$SET_LATEST" = 1 ]; then
    echo "Done: launchers will see v$VER at $PUBLIC/$VERSION_NAME"
else
    echo "Done: v$VER is published and can be installed from the version list; v$CUR_LATEST stays the latest ($PUBLIC/$VERSION_NAME)"
fi
