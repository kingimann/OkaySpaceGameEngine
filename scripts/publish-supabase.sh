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
# Older version folders beyond the newest $KEEP are deleted.
#
# Options (environment):
#   DIST_DIR     build output to publish (default build-win-dist)
#   NOTES_FILE   bullet lines for NOTES.txt (default: the last 6 commit subjects)
#   KEEP         how many versions to keep in the bucket (default 3)
#   SUPABASE_URL project URL (default: the one baked into CMakeLists.txt)
#   DRY_RUN=1    check everything, upload nothing
set -euo pipefail
cd "$(dirname "$0")/.."

DIST_DIR="${DIST_DIR:-build-win-dist}"
KEEP="${KEEP:-3}"
BUCKET="${BUCKET:-releases}"
SUPABASE_URL="${SUPABASE_URL:-$(grep -m1 '^set(OKAY_DEFAULT_ACCOUNT_URL' CMakeLists.txt | grep -oE 'https://[a-z0-9]+\.supabase\.co')}"
SUPABASE_URL="${SUPABASE_URL%/}"
REF="$(printf '%s' "$SUPABASE_URL" | sed -E 's#https://([a-z0-9]+)\.supabase\.co#\1#')"
MAX_BYTES=$((50 * 1024 * 1024))   # Supabase's per-file upload limit on this plan

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
    [ "$sz" -le "$MAX_BYTES" ] || die "$n is $((sz / 1048576)) MB, over Supabase's 50 MB file limit (is its debug info split off?)"
    head -c2 "$f" | grep -q '^MZ' || die "$f isn't a Windows program"
done

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

echo "Publishing v$VER to $SUPABASE_URL (bucket '$BUCKET')"
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
upload "$NOTES" NOTES.txt "text/plain; charset=utf-8" 30
printf '%s\n' "$VER" > "$RESP.ver"
upload "$RESP.ver" VERSION.txt text/plain 30
rm -f "$RESP.ver"

# ---- Keep only the newest $KEEP versions --------------------------------------
list() {   # list <prefix>  -> one name per line
    curl -sS -X POST "$SUPABASE_URL/storage/v1/object/list/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" \
        -d "{\"prefix\":\"$1\",\"limit\":1000}" | python3 -c 'import sys, json; [print(o["name"]) for o in json.load(sys.stdin)]'
}
mapfile -t OLD < <(list "" | grep -E '^v[0-9.]+$' | sed 's/^v//' | sort -t. -k1,1n -k2,2n -k3,3n | head -n -"$KEEP")
for v in "${OLD[@]}"; do
    mapfile -t FILES < <(list "v$v/")
    [ "${#FILES[@]}" -gt 0 ] || continue
    body=$(printf '%s\n' "${FILES[@]}" | python3 -c 'import sys, json; v = sys.argv[1]; print(json.dumps({"prefixes": ["v%s/%s" % (v, l.strip()) for l in sys.stdin if l.strip()]}))' "$v")
    curl -sS -o /dev/null -X DELETE "$SUPABASE_URL/storage/v1/object/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" -d "$body"
    echo "  removed old v$v"
done

echo "Done: launchers will see v$VER at $SUPABASE_URL/storage/v1/object/public/$BUCKET/VERSION.txt"
