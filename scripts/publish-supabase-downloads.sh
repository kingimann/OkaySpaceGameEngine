#!/usr/bin/env bash
# Put the public downloads of a release in the Supabase "releases" bucket, so nobody needs GitHub to get them
# (owner decision 2026-11-20: "I don't want any one to depend on GitHub"):
#   releases/v<ver>/<file>      every version's own copy
#   releases/latest/<file>      a stable address for the newest version (latest channel only; written last)
# Files (each is optional; a missing one is skipped with a note): the installer OkaySpace-Setup.exe, the zip
# OkaySpace-windows-v<ver>.zip, the Linux game server okay-server-linux-x64 (the editor's Dedicated Server export and server
# hosts download it). Run AFTER scripts/publish-supabase.sh has published this version's checksums (release.yml does).
#
# Env: SUPABASE_SERVICE_KEY (required, never printed), SUPABASE_URL (else config/public-account.json), VER (else
# build-win-dist/VERSION.txt), CHANNEL (latest|lts, default latest), FILES ("name=path" lines; default the three above),
# DRY_RUN=1 (check, upload nothing). Written from scratch for this repository.
set -euo pipefail
cd "$(dirname "$0")/.."
die() { echo "publish-supabase-downloads: $*" >&2; exit 1; }
. scripts/public-account.sh
SUPABASE_URL="${SUPABASE_URL:-${OKAY_DEFAULT_ACCOUNT_URL:-$(okay_public_account url)}}"
SUPABASE_URL="${SUPABASE_URL%/}"
[[ "$SUPABASE_URL" =~ ^https://[a-z0-9]+\.supabase\.co$ ]] || die "SUPABASE_URL must be https://<project>.supabase.co"
BUCKET="${BUCKET:-releases}"
VER="${VER:-$(head -n1 build-win-dist/VERSION.txt 2>/dev/null | tr -d '\r ' || true)}"
[[ "$VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "no release version (VER or build-win-dist/VERSION.txt)"
CHANNEL="${CHANNEL:-latest}"
[[ "$CHANNEL" == latest || "$CHANNEL" == lts ]] || die "CHANNEL must be latest or lts"
MAX_BYTES=$(( ${MAX_MB:-100} * 1024 * 1024 ))
FILES="${FILES:-OkaySpace-Setup.exe=OkaySpace-Setup.exe
OkaySpace-windows-v$VER.zip=OkaySpace-windows-v$VER.zip
okay-server-linux-x64=linux-server-dl/okay-server-linux-x64}"

declare -a NAMES=() PATHS=()
while IFS='=' read -r name path; do
    [ -n "$name" ] || continue
    [[ "$name" =~ ^[A-Za-z0-9._-]+$ ]] || die "bad file name '$name'"
    if [ ! -s "$path" ]; then echo "  (skipped $name: $path is not there)"; continue; fi
    [ "$(stat -c %s "$path")" -le "$MAX_BYTES" ] || die "$name is over the ${MAX_MB:-100} MB limit"
    NAMES+=("$name"); PATHS+=("$path")
done <<< "$FILES"
[ "${#NAMES[@]}" -gt 0 ] || die "nothing to publish"
echo "Public downloads for v$VER: ${NAMES[*]}"
if [ "${DRY_RUN:-0}" = 1 ]; then echo "DRY_RUN: nothing uploaded."; exit 0; fi
[ -n "${SUPABASE_SERVICE_KEY:-}" ] || die "set SUPABASE_SERVICE_KEY"
RESP="$(mktemp)"; trap 'rm -f "$RESP"' EXIT
upload() {   # <file> <path in bucket> <cache seconds>
    local code
    code=$(curl -sS -o "$RESP" -w '%{http_code}' --retry 3 --retry-all-errors -X POST "$SUPABASE_URL/storage/v1/object/$BUCKET/$2" \
        -H "Authorization: Bearer $SUPABASE_SERVICE_KEY" -H "apikey: $SUPABASE_SERVICE_KEY" \
        -H "x-upsert: true" -H "Content-Type: application/octet-stream" -H "cache-control: max-age=$3" --data-binary @"$1")
    [ "$code" = 200 ] || die "upload of $2 failed ($code): $(cat "$RESP")"
    echo "  uploaded $2"
}
for i in "${!NAMES[@]}"; do upload "${PATHS[$i]}" "v$VER/${NAMES[$i]}" 3600; done
# Check each one is really readable at its public address, the same bytes.
PUBLIC="$SUPABASE_URL/storage/v1/object/public/$BUCKET"
for i in "${!NAMES[@]}"; do
    want="$(sha256sum "${PATHS[$i]}" | cut -d' ' -f1)"
    got="$(curl -fsS --max-time 300 "$PUBLIC/v$VER/${NAMES[$i]}?nocache=$(date +%s)" | sha256sum | cut -d' ' -f1)" || got=""
    [ "$got" = "$want" ] || die "v$VER/${NAMES[$i]} is not readable at its public address with the right bytes"
done
if [ "$CHANNEL" = latest ]; then
    # latest/ never moves backwards: only when this version is the bucket's newest release.
    head_ver="$(curl -fsS --max-time 20 "$PUBLIC/VERSION.txt?nocache=$(date +%s)" | head -n1 | tr -d '\r ' || true)"
    if [ "$head_ver" = "$VER" ]; then
        # The stable zip name is OkaySpace-windows.zip (the versioned name changes every release).
        for i in "${!NAMES[@]}"; do
            n="${NAMES[$i]}"; [[ "$n" == OkaySpace-windows-v*.zip ]] && n="OkaySpace-windows.zip"
            upload "${PATHS[$i]}" "latest/$n" 60
        done
        printf '%s\n' "$VER" > "$RESP.v"; upload "$RESP.v" "latest/VERSION.txt" 60; rm -f "$RESP.v"
    else
        echo "  latest/ left alone: the newest release in the bucket is '$head_ver', not v$VER"
    fi
fi
echo "Public downloads published: $PUBLIC/v$VER/ (and $PUBLIC/latest/ when this is the newest release)."
