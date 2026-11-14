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
# Older version folders beyond the newest $KEEP are deleted, except the LTS line.
#
# Channels (CHANNEL, default latest). Releases are numbered 2.<build>.<patch>:
#   latest   every normal release: writes VERSION.txt and NOTES.txt and prunes old
#            versions. Refuses a version in the current LTS line.
#   lts      a patch of the long-term-support line (for example 2.850.1, built from
#            a branch cut at 2.850.0): writes LTS.txt and NOTES-LTS.txt, never touches
#            VERSION.txt or NOTES.txt, never prunes. The first publish of a new line
#            promotes that line to LTS; within a line the patch must not go backwards.
#   Versions in the LTS line are never pruned. See docs/release_channels.md.
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
#   DRY_RUN=1    check everything, upload nothing
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

# ---- Channel -------------------------------------------------------------------
CHANNEL="${CHANNEL:-latest}"
[[ "$CHANNEL" == latest || "$CHANNEL" == lts ]] || die "CHANNEL must be latest or lts (got '$CHANNEL')"
ARCHIVE_REPO="${ARCHIVE_REPO:-${GITHUB_REPOSITORY:-kingimann/OkaySpaceGameEngine}}"
PUBLIC="$SUPABASE_URL/storage/v1/object/public/$BUCKET"
line_of() { printf '%s' "$1" | cut -d. -f1-2; }   # 2.850.1 -> 2.850
# fetch_version <file in bucket>: its first line if it is a version, else nothing
fetch_version() {
    local v
    v="$(curl -fsS --max-time 20 "$PUBLIC/$1" 2>/dev/null | head -n1 | tr -d '\r ' || true)"
    if [[ "$v" =~ ^[0-9]+(\.[0-9]+)+$ ]]; then printf '%s' "$v"; fi
}
CUR_LTS="$(fetch_version LTS.txt)"
CUR_LATEST="$(fetch_version VERSION.txt)"
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
RESP="$(mktemp)"; trap 'rm -f "$RESP"' EXIT

# ---- Checksums: the updater refuses files without one, so refuse to publish without them -----------------------
# (runs before DRY_RUN returns and before anything is uploaded; no network)
PRE=(merge --version "$VER" --channel "$CHANNEL" --repo "$ARCHIVE_REPO" --out "$RESP.preflight")
for n in "${NAMES[@]}"; do PRE+=(--file "$n=${SRC[$n]}"); done
python3 scripts/versions_index.py "${PRE[@]}" ||
    die "cannot compute a size and SHA-256 for every file; the launcher would refuse this release"
rm -f "$RESP.preflight"

echo "Publishing v$VER to $SUPABASE_URL (bucket '$BUCKET', channel $CHANNEL)"
if [ "${DRY_RUN:-0}" = 1 ]; then echo "DRY_RUN: everything checks out; nothing uploaded."; exit 0; fi

# ---- Bucket (public reads; writes need the service key) ----------------------
code=$(curl -sS -o "$RESP" -w '%{http_code}' -X POST "$SUPABASE_URL/storage/v1/bucket" "${AUTH[@]}" \
    -H "Content-Type: application/json" \
    -d "{\"id\":\"$BUCKET\",\"name\":\"$BUCKET\",\"public\":true,\"file_size_limit\":$MAX_BYTES}")
if [ "$code" != 200 ] && ! grep -qi "exist" "$RESP"; then die "creating the bucket failed ($code): $(cat "$RESP")"; fi

# upload <file> <path in bucket> <content-type> <cache seconds>
upload() {
    local code
    code=$(curl -sS -o "$RESP" -w '%{http_code}' --retry 3 --retry-all-errors -X POST \
        "$SUPABASE_URL/storage/v1/object/$BUCKET/$2" "${AUTH[@]}" \
        -H "x-upsert: true" -H "Content-Type: $3" -H "cache-control: max-age=$4" \
        --data-binary @"$1")
    [ "$code" = 200 ] || die "upload of $2 failed ($code): $(cat "$RESP")"
    echo "  uploaded $2 ($(( $(stat -c %s "$1") / 1024 )) KB)"
}

for n in "${NAMES[@]}"; do upload "${SRC[$n]}" "v$VER/$n" application/octet-stream 3600; done

NOTES="$(mktemp)"; trap 'rm -f "$RESP" "$NOTES"' EXIT
if [ -n "${NOTES_FILE:-}" ]; then cp "$NOTES_FILE" "$NOTES"
else git log -6 --no-merges --pretty='- %s' > "$NOTES" 2>/dev/null || echo "- OkaySpace v$VER" > "$NOTES"; fi
CHANGELOG="$NOTES"   # the editor's update dialog shows this
[ -f "$DIST_DIR/CHANGELOG.txt" ] && CHANGELOG="$DIST_DIR/CHANGELOG.txt"   # (dist/CHANGELOG.txt is years stale: not used)
upload "$CHANGELOG" "v$VER/CHANGELOG.txt" "text/plain; charset=utf-8" 3600
if [ "$CHANNEL" = lts ]; then NOTES_NAME=NOTES-LTS.txt; VERSION_NAME=LTS.txt; else NOTES_NAME=NOTES.txt; VERSION_NAME=VERSION.txt; fi
upload "$NOTES" "$NOTES_NAME" "text/plain; charset=utf-8" 30

list() {   # list <prefix>  -> one name per line
    curl -sS -X POST "$SUPABASE_URL/storage/v1/object/list/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" \
        -d "{\"prefix\":\"$1\",\"limit\":1000}" | python3 -c 'import sys, json; [print(o["name"]) for o in json.load(sys.stdin)]'
}
# Which old versions go (latest channel only; the LTS line is never pruned: its versions are what "stay on LTS" installs).
OLD=()
if [ "$CHANNEL" = latest ]; then
    LTS_RE='^$'   # matches no version: nothing is protected
    if [ -n "$CUR_LTS" ]; then LTS_RE="^$(line_of "$CUR_LTS" | sed 's/\./\\./g')\\."; fi
    mapfile -t OLD < <(list "" | grep -E '^v[0-9.]+$' | sed 's/^v//' | { grep -vE "$LTS_RE" || true; } |
        sort -t. -k1,1n -k2,2n -k3,3n | head -n -"$KEEP")
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
# so no launcher is offered this version). Re-running the publish is safe.
update_index() {
    local old="$RESP.idx.in" new="$RESP.idx.out" back="$RESP.idx.back" latest="$CUR_LATEST" lts="$CUR_LTS" n
    if [ "$CHANNEL" = lts ]; then lts="$VER"; else latest="$VER"; fi
    curl -fsS --max-time 20 "$PUBLIC/versions.json" -o "$old" 2>/dev/null || rm -f "$old"   # (no index yet is fine: the first publish)
    local inbucket v
    inbucket="$(list "" | grep -E '^v[0-9.]+$' | sed 's/^v//')" || die "could not list the bucket for versions.json"
    for v in "${OLD[@]}"; do inbucket="$(printf '%s\n' "$inbucket" | grep -vxF "$v" || true)"; done   # (about to be pruned)
    inbucket="$(printf '%s\n' "$inbucket" | tr '\n' ' ')"
    local args=(merge --existing "$old" --version "$VER" --channel "$CHANNEL" --date "$(date -u +%F)"
                --repo "$ARCHIVE_REPO" --bucket-versions "$inbucket" --out "$new")
    [ -n "$latest" ] && args+=(--latest "$latest")
    [ -n "$lts" ] && args+=(--lts "$lts")
    local fargs=()
    for n in "${NAMES[@]}"; do args+=(--file "$n=${SRC[$n]}"); fargs+=(--file "$n=${SRC[$n]}"); done
    python3 scripts/versions_index.py "${args[@]}" || die "versions.json could not be built with a checksum for every file"
    upload "$new" versions.json "application/json" 30
    sign_and_upload_index "$new"
    curl -fsS --max-time 30 -H 'Cache-Control: no-cache' "$PUBLIC/versions.json?nocache=$(date +%s)" -o "$back" ||
        die "versions.json cannot be read back from the bucket"
    python3 scripts/versions_index.py check --index "$back" --version "$VER" "${fargs[@]}" ||
        die "the published versions.json does not carry the checksums of the uploaded files"
    rm -f "$old" "$new" "$back"
}
update_index   # (die exits the whole script: no VERSION.txt, no pruning)

printf '%s\n' "$VER" > "$RESP.ver"
upload "$RESP.ver" "$VERSION_NAME" text/plain 30
rm -f "$RESP.ver"

# ---- Keep only the newest $KEEP versions ------------------------------------------
for v in "${OLD[@]}"; do
    mapfile -t FILES < <(list "v$v/")
    [ "${#FILES[@]}" -gt 0 ] || continue
    body=$(printf '%s\n' "${FILES[@]}" | python3 -c 'import sys, json; v = sys.argv[1]; print(json.dumps({"prefixes": ["v%s/%s" % (v, l.strip()) for l in sys.stdin if l.strip()]}))' "$v")
    curl -sS -o /dev/null -X DELETE "$SUPABASE_URL/storage/v1/object/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" -d "$body"
    echo "  removed old v$v"
done

echo "Done: launchers will see v$VER at $PUBLIC/$VERSION_NAME"
