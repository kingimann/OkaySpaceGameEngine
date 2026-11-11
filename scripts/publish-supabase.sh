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
#   VERSION.txt              uploaded LAST, so nobody sees a version before its files
#   versions.json            index of every released version with sizes and SHA-256
#                            (scripts/versions_index.py; a failure here only warns)
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
printf '%s\n' "$VER" > "$RESP.ver"
upload "$RESP.ver" "$VERSION_NAME" text/plain 30
rm -f "$RESP.ver"

# ---- Keep only the newest $KEEP versions (latest channel only) -----------------
# The LTS line is never pruned: its versions are what "stay on LTS" installs.
list() {   # list <prefix>  -> one name per line
    curl -sS -X POST "$SUPABASE_URL/storage/v1/object/list/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" \
        -d "{\"prefix\":\"$1\",\"limit\":1000}" | python3 -c 'import sys, json; [print(o["name"]) for o in json.load(sys.stdin)]'
}
OLD=()
if [ "$CHANNEL" = latest ]; then
    LTS_RE='^$'   # matches no version: nothing is protected
    if [ -n "$CUR_LTS" ]; then LTS_RE="^$(line_of "$CUR_LTS" | sed 's/\./\\./g')\\."; fi
    mapfile -t OLD < <(list "" | grep -E '^v[0-9.]+$' | sed 's/^v//' | { grep -vE "$LTS_RE" || true; } |
        sort -t. -k1,1n -k2,2n -k3,3n | head -n -"$KEEP")
fi
for v in "${OLD[@]}"; do
    mapfile -t FILES < <(list "v$v/")
    [ "${#FILES[@]}" -gt 0 ] || continue
    body=$(printf '%s\n' "${FILES[@]}" | python3 -c 'import sys, json; v = sys.argv[1]; print(json.dumps({"prefixes": ["v%s/%s" % (v, l.strip()) for l in sys.stdin if l.strip()]}))' "$v")
    curl -sS -o /dev/null -X DELETE "$SUPABASE_URL/storage/v1/object/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" -d "$body"
    echo "  removed old v$v"
done

# ---- versions.json: every released version with sizes and SHA-256 ---------------
# Not needed for an update to work (VERSION.txt already went out), so a failure only warns.
update_index() {
    local old="$RESP.idx.in" new="$RESP.idx.out" latest="$CUR_LATEST" lts="$CUR_LTS" n
    if [ "$CHANNEL" = lts ]; then lts="$VER"; else latest="$VER"; fi
    curl -fsS --max-time 20 "$PUBLIC/versions.json" -o "$old" 2>/dev/null || rm -f "$old"
    local inbucket
    inbucket="$(list "" | grep -E '^v[0-9.]+$' | sed 's/^v//' | tr '\n' ' ')" || return 1
    local args=(merge --existing "$old" --version "$VER" --channel "$CHANNEL" --date "$(date -u +%F)"
                --repo "$ARCHIVE_REPO" --bucket-versions "$inbucket" --out "$new")
    [ -n "$latest" ] && args+=(--latest "$latest")
    [ -n "$lts" ] && args+=(--lts "$lts")
    for n in "${NAMES[@]}"; do args+=(--file "$n=${SRC[$n]}"); done
    python3 scripts/versions_index.py "${args[@]}" || return 1
    upload "$new" versions.json "application/json" 30
    rm -f "$old" "$new"
}
( update_index ) || echo "warning: versions.json was not updated (the release itself is published)" >&2   # (subshell: a failed upload exits only it)

echo "Done: launchers will see v$VER at $PUBLIC/$VERSION_NAME"
