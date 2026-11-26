#!/usr/bin/env bash
# Publish a TEST BUILD (the "dev channel") to the dev/ folder of the Supabase release bucket, so the owner can try a branch in the launcher
# without a pull request, a GitHub Release or a normal release. Docs: docs/release/release_channels.md ("Test builds"). Run it after the Windows
# distribution is built; the workflow .github/workflows/dev-build.yml does that.
#
#   SUPABASE_SERVICE_KEY=eyJ...  DEV_BRANCH=claude/walk-vs-run  DEV_SHA=<commit>  scripts/publish-supabase-dev.sh
#
# This is a SEPARATE script from scripts/publish-supabase.sh on purpose: the real release publisher has no "dev" code path at all, so a test
# build cannot change what "latest" or "lts" publish, and this script cannot write anything outside dev/. Keys come from the environment
# only: never put them in a file, never trace this script (it refuses `set -x` while a key is set).
#
# What it writes (public bucket "releases"; it must already exist; nothing else is ever written, read the assert_dev function):
#   dev/v<ver>/OkaySpace.exe, OkayEngine.exe, OkaySpacePlayer.exe   the test build's files. Uploaded WITHOUT x-upsert: the storage service
#                            itself refuses to overwrite a file that is there, so a published test build can never change.
#   dev/versions.json        the list of test builds with the size and SHA-256 of the three programs (scripts/dev_index.py). The launcher
#                            REFUSES a file with no checksum there, so the publish stops if one cannot be written or does not read back
#   dev/versions.json.sig    ed25519 signature of that list (scripts/sign_index.py), made when OKAY_UPDATE_SIGNING_SEED is set
#   dev/NOTES-DEV.txt        "Test build of <branch> (<sha>)", the note, and the base release: the launcher's "what's new" lines
#   dev/DEV.txt              the newest test version, uploaded LAST (for people and scripts; the launcher trusts only the signed list)
# It NEVER writes VERSION.txt, NOTES.txt, LTS.txt, NOTES-LTS.txt, versions.json, versions.json.sig or anything in v<ver>/ at the bucket root,
# and it never creates, changes or deletes the bucket. Only the newest DEV_KEEP (3) test builds stay in the bucket; older dev/v<ver>/ folders
# are deleted (and listed as not in the bucket); leftovers of a publish that stopped half way go with them.
#
# The version of a test build has FOUR numbers (2.876.0.17 = the 17th test build on top of release 2.876.0, shared/TestBuild.hpp). It must be
# built on the release that is published NOW (VERSION.txt on the server): a test build numbered after an older release would be older than
# what people run and nobody would be offered it, so that publish is refused ("start a new test build").
#
# Refused, with nothing written: a version with three numbers; a version that is already listed or whose folder already has files (a test
# build is never published twice: its checksums may be known, CLAUDE.md rule 12); a file that has no checksum; a damaged list on the server
# (never replaced by an empty one); a base release that is no longer the newest.
#
# Options (environment):
#   DEV_BRANCH   the branch (or tag, or commit) that was built (required; letters, digits and . _ / + - only)
#   DEV_SHA      the commit that was built (required; 7 to 40 hex digits)
#   DEV_NOTE     one line from whoever started the build (optional; plain ASCII, 140 characters)
#   DIST_DIR     build output to publish (default build-win-dist): OkaySpace.exe, Tools/OkayEngine.exe, Tools/OkaySpacePlayer.exe, VERSION.txt
#   DEV_KEEP     how many test builds to keep in the bucket (default 3, at most 10)
#   SUPABASE_URL project URL (default: the one baked into CMakeLists.txt)
#   BUCKET       the bucket (default releases)
#   SUPABASE_SERVICE_KEY (or SUPABASE_ACCESS_TOKEN, a management token the service key is fetched with): needed unless DRY_RUN=1
#   OKAY_UPDATE_SIGNING_SEED  64 hex digits, the private signing key (same as the release publish). Unset = the list is published unsigned
#                            (a warning); only launchers built without OKAY_UPDATE_TRUSTED_KEYS accept that
#   REQUIRE_SIGNED=1         stop instead of publishing an unsigned list
#   MAX_MB       per-file limit (default 100, the project's storage limit)
#   READ_RETRY_SECONDS  pause between the tries of a read that must succeed (default 2; tried 3 times)
#   DRY_RUN=1    run every check that can be run without a key (the build, the words, the checksums, what the server says now) and upload nothing
set -euo pipefail
cd "$(dirname "$0")/.."

die() { echo "publish-supabase-dev: $*" >&2; exit 1; }
note() { echo "publish-supabase-dev: $*" >&2; }

# Never trace commands while a key can be in scope (-v tests that a variable is set without expanding it, so even this leaves nothing in a trace).
case "$-" in *x*)
    if [ -v SUPABASE_SERVICE_KEY ] || [ -v SUPABASE_ACCESS_TOKEN ] || [ -v OKAY_UPDATE_SIGNING_SEED ]; then
        die "refusing to run with 'set -x' while a key is set (the trace would print it)"
    fi;;
esac
if [ -n "${OKAY_UPDATE_SIGNING_SEED:-}" ]; then
    [[ "$OKAY_UPDATE_SIGNING_SEED" =~ ^[0-9a-fA-F]{64}$ ]] || die "OKAY_UPDATE_SIGNING_SEED is not 64 hex digits"
elif [ "${REQUIRE_SIGNED:-0}" = 1 ]; then
    die "REQUIRE_SIGNED=1 but OKAY_UPDATE_SIGNING_SEED is not set"
fi

DIST_DIR="${DIST_DIR:-build-win-dist}"
BUCKET="${BUCKET:-releases}"
KEEP="${DEV_KEEP:-3}"
[[ "$KEEP" =~ ^[0-9]+$ ]] && [ "$KEEP" -ge 1 ] && [ "$KEEP" -le 10 ] || die "DEV_KEEP must be a number from 1 to 10 (got '$KEEP')"
# The project URL: SUPABASE_URL, else OKAY_DEFAULT_ACCOUNT_URL, else config/public-account.json (the value the build bakes in).
. "$(dirname "$0")/public-account.sh"
SUPABASE_URL="${SUPABASE_URL:-${OKAY_DEFAULT_ACCOUNT_URL:-$(okay_public_account url)}}"
SUPABASE_URL="${SUPABASE_URL%/}"
[ -n "$SUPABASE_URL" ] || die "no SUPABASE_URL (or OKAY_DEFAULT_ACCOUNT_URL) is set and config/public-account.json has no url"
REF="$(printf '%s' "$SUPABASE_URL" | sed -E 's#https://([a-z0-9]+)\.supabase\.co#\1#')"
MAX_MB="${MAX_MB:-100}"
MAX_BYTES=$((MAX_MB * 1024 * 1024))
RETRY_SECONDS="${READ_RETRY_SECONDS:-2}"
AREA=dev                                   # fixed on purpose: not an option, so no setting can point this script at the real release files
API="$SUPABASE_URL/storage/v1"
PUBLIC="$API/object/public/$BUCKET"
DEV_NOTE="${DEV_NOTE:-}"
DEV_BRANCH="${DEV_BRANCH:-}"
DEV_SHA="${DEV_SHA:-}"

# ---- The build ----------------------------------------------------------------------------------------------------------------------
VER="$(head -n1 "$DIST_DIR/VERSION.txt" 2>/dev/null | tr -d '\r ' || true)"   # (|| true: let the check below say why)
[[ "$VER" =~ ^(0|[1-9][0-9]*)(\.(0|[1-9][0-9]*)){2}\.[1-9][0-9]*$ ]] ||
    die "'$VER' in $DIST_DIR/VERSION.txt is not a test version: it needs FOUR numbers, the fourth at least 1 (for example 2.876.0.17). A real release is published with scripts/publish-supabase.sh"
BASE="${VER%.*}"                           # 2.876.0.17 -> 2.876.0 : the release it was made on
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
    [ "$sz" -le "$MAX_BYTES" ] || die "$n is $((sz / 1048576)) MB, over the $MAX_MB MB file limit"
    head -c2 "$f" | grep -q '^MZ' || die "$f isn't a Windows program"
done

T="$(mktemp -d)"
RESP="$T/resp"
cleanup() { rm -rf "$T"; }
trap cleanup EXIT
IDX=(python3 -I scripts/dev_index.py)

# ---- The words and the checksums (no network, no key) -----------------------------------------------------------------------------
# dev_index.py checks the branch, the commit and the note (and writes the notes); a bad one stops here, before anything is read or written.
"${IDX[@]}" notes --version "$VER" --base "$BASE" --branch="$DEV_BRANCH" --sha="$DEV_SHA" --note="$DEV_NOTE" --out "$T/NOTES-DEV.txt" ||
    die "the branch, the commit or the note was refused (see above)"
TITLE="$(head -n1 "$T/NOTES-DEV.txt")"
DATE="$(date -u +%F)"
FILEARGS=()
for n in "${NAMES[@]}"; do FILEARGS+=(--file "$n=${SRC[$n]}"); done
# The updater refuses files without a checksum, so refuse to publish without them (this also runs for DRY_RUN).
set +e
"${IDX[@]}" merge --version "$VER" --branch="$DEV_BRANCH" --sha="$DEV_SHA" --note="$DEV_NOTE" --date "$DATE" "${FILEARGS[@]}" --out "$T/preflight.json"
rc=$?
set -e
[ "$rc" = 0 ] || die "cannot compute a size and SHA-256 for every file, or a field was refused; the launcher would refuse this build"

# ---- Safety: nothing is ever written outside dev/ ----------------------------------------------------------------------------------
assert_dev() {   # assert_dev <path in the bucket>
    case "$1" in
        dev/versions.json|dev/versions.json.sig|dev/DEV.txt|dev/NOTES-DEV.txt) ;;
        dev/v*/*) [[ "$1" =~ ^dev/v(0|[1-9][0-9]*)(\.(0|[1-9][0-9]*)){2}\.[1-9][0-9]*/[A-Za-z0-9._-]+$ ]] || die "internal error: refusing the path '$1'" ;;
        *) die "internal error: refusing to write outside dev/ ('$1')" ;;
    esac
}

# ---- Reading the server ------------------------------------------------------------------------------------------------------------
# get_public <path in bucket> <out file>: 0 = read, 4 = the object is not there, 1 = could not be read (after 3 tries).
# (Supabase answers a missing object with HTTP 400 and "not_found" in the body, or 404.) The query string and header keep a CDN copy out of it.
get_public() {
    local code i
    for i in 1 2 3; do
        code="$(curl -sS -o "$2" -w '%{http_code}' --max-time 30 -H 'Cache-Control: no-cache' "$PUBLIC/$1?nocache=$(date +%s)$RANDOM" 2>/dev/null || true)"
        case "$code" in
            200) return 0 ;;
            404) return 4 ;;
            400) if grep -qiE 'not.?found|does not exist' "$2" 2>/dev/null; then return 4; fi ;;
        esac
        [ "$i" = 3 ] || sleep "$RETRY_SECONDS"
    done
    return 1
}

# The release that is published now (VERSION.txt at the bucket root, read only). The build must be on top of it.
STABLE=""
set +e; get_public VERSION.txt "$T/stable.txt"; rc=$?; set -e
if [ "$rc" = 0 ]; then
    STABLE="$(head -n1 "$T/stable.txt" | tr -d '\r ')"
    [[ "$STABLE" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "VERSION.txt on the server says '$STABLE', which is not a release version, so the base of this test build cannot be checked"
    [ "$STABLE" = "$BASE" ] ||
        die "this test build ($VER) was made on release $BASE, but the newest release is now $STABLE. Anyone on $STABLE would never be offered a test build numbered after $BASE. Start a new test build"
elif [ "$rc" = 4 ]; then
    [ "${DRY_RUN:-0}" = 1 ] || die "no release is published yet (the server has no VERSION.txt): test builds are numbered after a release"
else
    [ "${DRY_RUN:-0}" = 1 ] || die "VERSION.txt could not be read from $SUPABASE_URL, so the base of this test build cannot be checked"
    note "DRY_RUN: the server could not be read; the base release was not checked."
fi

# ---- Credentials: the service key, or a management token it is fetched with (a dry run needs none) ----
KEY="${SUPABASE_SERVICE_KEY:-}"
AUTH=()
if [ "${DRY_RUN:-0}" != 1 ]; then
    if [ -z "$KEY" ]; then
        [ -n "${SUPABASE_ACCESS_TOKEN:-}" ] || die "set SUPABASE_SERVICE_KEY (or SUPABASE_ACCESS_TOKEN): test builds are uploaded with a key from the environment"
        KEY="$(curl -sS --fail -H "Authorization: Bearer $SUPABASE_ACCESS_TOKEN" \
            "https://api.supabase.com/v1/projects/$REF/api-keys?reveal=true" |
            python3 -c 'import sys, json; print(next(k["api_key"] for k in json.load(sys.stdin) if k.get("name") == "service_role"))')" ||
            die "couldn't get the service key with that token"
    fi
    AUTH=(-H "Authorization: Bearer $KEY" -H "apikey: $KEY")
fi

list() {   # list <prefix> : one name per line (folders without a slash); stops when the server cannot be listed
    local body
    body="$(curl -sS --fail --max-time 60 -X POST "$API/object/list/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" \
        -d "{\"prefix\":\"$1\",\"limit\":1000}" 2>/dev/null)" || die "could not list $BUCKET/$1 on the server"
    printf '%s' "$body" | python3 -c 'import sys, json; [print(o["name"]) for o in json.load(sys.stdin)]' || die "the listing of $BUCKET/$1 is not what was expected"
}

# What dev/ holds now. The listing (a key is needed) says what exists; with no key (a dry run) the public list is looked at instead.
LISTING=""
HAVE_INDEX=0
if [ "${DRY_RUN:-0}" != 1 ]; then
    LISTING="$(list "$AREA/")"
    if printf '%s\n' "$LISTING" | grep -qxF versions.json; then HAVE_INDEX=1; fi
else
    set +e; get_public "$AREA/versions.json" "$T/old.json"; rc=$?; set -e
    if [ "$rc" = 0 ]; then HAVE_INDEX=1
    elif [ "$rc" != 4 ]; then note "DRY_RUN: dev/versions.json could not be read; the 'already published' check used what could be read."; fi
fi
OLDIDX="$T/none.json"
if [ "$HAVE_INDEX" = 1 ]; then
    OLDIDX="$T/old.json"
    # A list that exists MUST be read (a failed read is never taken for "no list": that would start the history over).
    if [ "${DRY_RUN:-0}" != 1 ]; then
        get_public "$AREA/versions.json" "$OLDIDX" || die "dev/versions.json is on the server but could not be read: nothing was changed"
    fi
fi
if printf '%s\n' "$LISTING" | grep -qxF "v$VER"; then
    die "dev/v$VER/ already has files on the server (an earlier publish of this version stopped half way, or it is published already). A test build is never published twice: start a new test build"
fi

# ---- Which old builds go, and the new list ---------------------------------------------------------------------------------------
LISTING_ONE_LINE="$(printf '%s' "$LISTING" | tr '\n' ' ')"
set +e
OLD="$("${IDX[@]}" prune-list --existing "$OLDIDX" --listing "$LISTING_ONE_LINE" --version "$VER" --branch="$DEV_BRANCH" --keep "$KEEP")"
rc=$?
set -e
[ "$rc" = 0 ] || die "the list of old test builds could not be worked out (is dev/versions.json damaged?): nothing was changed"
STAY=""
for v in $(printf '%s' "$LISTING" | sed -n 's#^v\(.*\)$#\1#p'); do
    if ! printf '%s\n' "$OLD" | grep -qxF "$v"; then STAY="$STAY $v"; fi
done
STAY="$STAY $VER"
set +e
"${IDX[@]}" merge --existing "$OLDIDX" --version "$VER" --branch="$DEV_BRANCH" --sha="$DEV_SHA" --note="$DEV_NOTE" --date "$DATE" "${FILEARGS[@]}" \
    --bucket-versions "$STAY" --keep "$KEEP" --out "$T/new.json"
rc=$?
set -e
case "$rc" in
    0) ;;
    3) die "test build $VER is already in the list on the server. A test build is never published twice: start a new test build" ;;
    *) die "dev/versions.json could not be built with a checksum for every file: nothing was changed" ;;
esac

echo "Publishing test build v$VER ($TITLE) to $SUPABASE_URL (bucket '$BUCKET', folder '$AREA/', keeping the newest $KEEP)"
if [ "${DRY_RUN:-0}" = 1 ]; then
    echo "DRY_RUN would write, and only these:"
    for n in "${NAMES[@]}"; do echo "  $AREA/v$VER/$n"; done
    echo "  $AREA/versions.json"
    [ -n "${OKAY_UPDATE_SIGNING_SEED:-}" ] && echo "  $AREA/versions.json.sig"
    echo "  $AREA/NOTES-DEV.txt"
    echo "  $AREA/DEV.txt"
    for v in $OLD; do echo "  (delete) $AREA/v$v/"; done
    echo "DRY_RUN: everything checks out; nothing uploaded."
    exit 0
fi

# ---- Writing -----------------------------------------------------------------------------------------------------------------------
# upload <file> <path in bucket> <content-type> <cache seconds> <replace: 1 = may replace what is there, 0 = must be new>
upload() {
    local code
    assert_dev "$2"
    local up=()
    if [ "$5" = 1 ]; then up=(-H "x-upsert: true"); fi
    code=$(curl -sS -o "$RESP" -w '%{http_code}' --retry 3 --retry-all-errors -X POST "$API/object/$BUCKET/$2" "${AUTH[@]}" \
        "${up[@]}" -H "Content-Type: $3" -H "cache-control: max-age=$4" --data-binary @"$1") || die "upload of $2 failed (the server could not be reached)"
    [ "$code" = 200 ] || die "upload of $2 failed ($code): $(head -c 400 "$RESP")"
    echo "  uploaded $2 ($(( $(stat -c %s "$1") / 1024 )) KB)"
}

for n in "${NAMES[@]}"; do upload "${SRC[$n]}" "$AREA/v$VER/$n" application/octet-stream 3600 0; done

# Sign the list and publish the signature next to it; read both back and verify the PUBLISHED pair (what launchers will fetch).
sign_and_upload_index() {   # sign_and_upload_index <local dev/versions.json>
    local sig="$T/idx.sig" pub back="$T/idx.sigback" body="$T/idx.pubbody"
    if [ -z "${OKAY_UPDATE_SIGNING_SEED:-}" ]; then
        note "WARNING: dev/versions.json is published UNSIGNED (OKAY_UPDATE_SIGNING_SEED is not set): launchers built with OKAY_UPDATE_TRUSTED_KEYS will refuse it"
        return 0
    fi
    python3 -I scripts/sign_index.py sign --file "$1" --out "$sig" >/dev/null || die "could not sign dev/versions.json"
    pub="$(python3 -I scripts/sign_index.py pubkey)" || die "could not derive the public key"
    upload "$sig" "$AREA/versions.json.sig" text/plain 30 1
    get_public "$AREA/versions.json.sig" "$back" || die "dev/versions.json.sig cannot be read back from the bucket"
    get_public "$AREA/versions.json" "$body" || die "dev/versions.json cannot be read back from the bucket"
    python3 -I scripts/sign_index.py verify --file "$body" --sig "$back" --pub "$pub" >/dev/null ||
        die "the published dev/versions.json does not verify against the published signature"
    echo "  signed dev/versions.json (public key ${pub:0:16}...)"
}

# The list goes up BEFORE the notes and DEV.txt, is read back from the bucket and compared with the files that were uploaded; any failure stops
# the publish (no DEV.txt, so nobody is told about a build whose checksums are not there).
upload "$T/new.json" "$AREA/versions.json" application/json 30 1
sign_and_upload_index "$T/new.json"
get_public "$AREA/versions.json" "$T/back.json" || die "dev/versions.json cannot be read back from the bucket"
CHECKARGS=()
for n in "${NAMES[@]}"; do CHECKARGS+=(--file "$n=${SRC[$n]}"); done
"${IDX[@]}" check --index "$T/back.json" --version "$VER" "${CHECKARGS[@]}" ||
    die "the published dev/versions.json does not carry the checksums of the uploaded files"
upload "$T/NOTES-DEV.txt" "$AREA/NOTES-DEV.txt" "text/plain; charset=utf-8" 30 1
printf '%s\n' "$VER" > "$T/DEV.txt"
upload "$T/DEV.txt" "$AREA/DEV.txt" text/plain 30 1

# ---- Keep only the newest builds -------------------------------------------------------------------------------------------------
for v in $OLD; do
    LISTED="$(list "$AREA/v$v/")" || { note "WARNING: could not list old test build v$v; it stays until the next publish"; continue; }
    mapfile -t FILES <<< "$LISTED"
    paths=()
    for f in "${FILES[@]}"; do
        [ -n "$f" ] || continue
        assert_dev "$AREA/v$v/$f"; paths+=("$AREA/v$v/$f")
    done
    [ "${#paths[@]}" -gt 0 ] || continue
    body="$(printf '%s\n' "${paths[@]}" | python3 -c 'import sys, json; print(json.dumps({"prefixes": [l.strip() for l in sys.stdin if l.strip()]}))')"
    code=$(curl -sS -o "$RESP" -w '%{http_code}' -X DELETE "$API/object/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" -d "$body") || code=000
    if [ "$code" = 200 ]; then echo "  removed old test build v$v"; else note "WARNING: could not remove old test build v$v ($code); it stays until the next publish"; fi
done

echo "Done: test build v$VER ($TITLE). Launchers with 'Get test builds' on will see it at $PUBLIC/$AREA/versions.json"
# In a GitHub job, say it in plain words on the run page too (the text is made, and the note escaped, by dev_index.py).
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    "${IDX[@]}" summary --version "$VER" --base "$BASE" --branch="$DEV_BRANCH" --sha="$DEV_SHA" --note="$DEV_NOTE" >> "$GITHUB_STEP_SUMMARY" || true
fi
