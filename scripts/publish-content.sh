#!/usr/bin/env bash
# Publish a game's downloadable content (docs/content_delivery.md) to Supabase Storage with the plain storage REST API (curl), the same way
# scripts/publish-supabase.sh publishes the program. The folder is the content: models, textures, animations, scenes, audio, dialogue, scripts
# and data ONLY (okay-content refuses programs and libraries). Nothing here talks to anything but your Supabase project.
#
#   OKAY_CONTENT_SIGNING_SEED=<64 hex>  SUPABASE_SERVICE_KEY=eyJ...  scripts/publish-content.sh <folder> --game <id> --version <n> [--min-app <v>] [--group <prefix>=<name>]...
#   (or SUPABASE_ACCESS_TOKEN=sbp_... instead of the service key: the key is fetched, as publish-supabase.sh does)
#
# Keys come from the environment only: never put them in a file in this repo, never on the command line. The signing seed is read by okay-content
# (never printed or logged); make a key pair with `python3 scripts/sign_index.py genkey` (keep the seed, put the PUBLIC key in the game's
# content.okayconfig as keys=...).
#
# What it does, in this order (a client never sees a manifest before the files it lists):
#   1. okay-content make-manifest <folder>   writes content.manifest and content.manifest.sig (signed with the seed)
#   2. okay-content verify <folder>          checks the signature and every file's size and SHA-256 locally
#   3. uploads every listed file to  <bucket>/<game>/<path>   (x-upsert: a changed file replaces the old one)
#   4. uploads content.manifest.sig, then content.manifest LAST
#   5. reads the manifest and signature back from the PUBLIC url and verifies them again; prints the content URL to put in the game's config
# A client that is downloading while step 3 replaces a file sees a checksum mismatch and retries (it never keeps a wrong file).
#
# Options (environment):
#   BUCKET            bucket name (default game-content; created public on first use)
#   SUPABASE_URL      project URL (default: the one baked into the build, see scripts/public-account.sh)
#   OKAY_CONTENT_BIN  path to the okay-content tool (default: PATH, then build*/bin/okay-content)
#   MAX_MB            per-file limit (default 100: the Supabase bucket limit)
#   ALLOW_UNSIGNED=1  publish without a signature (a game whose key is not set yet; clients with a key configured will refuse it)
#   DRY_RUN=1         make the manifest and verify it, upload nothing
set -euo pipefail
cd "$(dirname "$0")/.."

die() { echo "publish-content: $*" >&2; exit 1; }
case "$-" in *x*) if [ -v OKAY_CONTENT_SIGNING_SEED ]; then die "refusing to run with 'set -x' while OKAY_CONTENT_SIGNING_SEED is set (the trace would print it)"; fi;; esac

FOLDER="${1:-}"; [ -n "$FOLDER" ] && [ -d "$FOLDER" ] || die "usage: publish-content.sh <folder> --game <id> --version <n> [--min-app <v>] [--group <prefix>=<name>]..."
shift
GAME=""; VERSION=""; MANIFEST_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --game) GAME="${2:-}"; shift 2;;
        --version) VERSION="${2:-}"; shift 2;;
        --min-app|--group) MANIFEST_ARGS+=("$1" "${2:-}"); shift 2;;
        *) die "unknown option $1";;
    esac
done
[[ "$GAME" =~ ^[a-z0-9._-]{1,64}$ ]] || die "--game must be 1 to 64 characters of a-z 0-9 . _ -"
[[ "$VERSION" =~ ^[1-9][0-9]*$ ]] || die "--version must be a whole number (1, 2, 3 ...; it must go UP with every publish)"

TOOL="${OKAY_CONTENT_BIN:-}"
if [ -z "$TOOL" ]; then
    TOOL="$(command -v okay-content || true)"
    for c in build/bin/okay-content build-win-dist/Tools/okay-content.exe build-win/bin/okay-content.exe; do [ -n "$TOOL" ] || { [ -x "$c" ] && TOOL="$c"; }; done
fi
[ -n "$TOOL" ] && [ -x "$TOOL" ] || die "okay-content not found: build it (cmake target okay-content) or set OKAY_CONTENT_BIN"

if [ -z "${OKAY_CONTENT_SIGNING_SEED:-}" ]; then
    [ "${ALLOW_UNSIGNED:-0}" = 1 ] || die "OKAY_CONTENT_SIGNING_SEED is not set (set ALLOW_UNSIGNED=1 to publish unsigned; games with a key configured will refuse it)"
    echo "  WARNING: publishing UNSIGNED content"
else
    [[ "$OKAY_CONTENT_SIGNING_SEED" =~ ^[0-9a-fA-F]{64}$ ]] || die "OKAY_CONTENT_SIGNING_SEED is not 64 hex digits"
fi

"$TOOL" make-manifest "$FOLDER" --game "$GAME" --version "$VERSION" ${MANIFEST_ARGS[@]+"${MANIFEST_ARGS[@]}"} || die "could not build the manifest"
PUB=""
if [ -n "${OKAY_CONTENT_SIGNING_SEED:-}" ]; then
    PUB="$("$TOOL" pubkey)" || die "could not derive the public key"
    "$TOOL" verify "$FOLDER" --keys "$PUB" --game "$GAME" || die "the folder does not verify against its own manifest"
fi

. "$(dirname "$0")/public-account.sh"
SUPABASE_URL="${SUPABASE_URL:-${OKAY_DEFAULT_ACCOUNT_URL:-$(okay_public_account url)}}"
SUPABASE_URL="${SUPABASE_URL%/}"
BUCKET="${BUCKET:-game-content}"
MAX_MB="${MAX_MB:-100}"; MAX_BYTES=$((MAX_MB * 1024 * 1024))
[ -n "$SUPABASE_URL" ] || die "no SUPABASE_URL (or OKAY_DEFAULT_ACCOUNT_URL) is set and config/public-account.json has no url (docs/accounts.md)"
REF="$(printf '%s' "$SUPABASE_URL" | sed -E 's#https://([a-z0-9]+)\.supabase\.co#\1#')"
PUBLIC="$SUPABASE_URL/storage/v1/object/public/$BUCKET/$GAME"

# the files the manifest lists (field 7 of each "file" line), and a size check against the bucket limit
mapfile -t FILES < <(awk -F'\t' '$1=="file"{print $7}' "$FOLDER/content.manifest")
[ "${#FILES[@]}" -gt 0 ] || die "the manifest lists no files"
for f in "${FILES[@]}"; do
    sz=$(stat -c %s "$FOLDER/$f"); [ "$sz" -le "$MAX_BYTES" ] || die "$f is $((sz / 1048576)) MB, over the $MAX_MB MB file limit"
done
echo "Publishing content version $VERSION of '$GAME' (${#FILES[@]} files) to $SUPABASE_URL (bucket '$BUCKET')"
if [ "${DRY_RUN:-0}" = 1 ]; then echo "DRY_RUN: everything checks out; nothing uploaded."; exit 0; fi

KEY="${SUPABASE_SERVICE_KEY:-}"
if [ -z "$KEY" ]; then
    [ -n "${SUPABASE_ACCESS_TOKEN:-}" ] || die "set SUPABASE_ACCESS_TOKEN (sbp_...) or SUPABASE_SERVICE_KEY"
    KEY="$(curl -sS --fail -H "Authorization: Bearer $SUPABASE_ACCESS_TOKEN" \
        "https://api.supabase.com/v1/projects/$REF/api-keys?reveal=true" |
        python3 -c 'import sys, json; print(next(k["api_key"] for k in json.load(sys.stdin) if k.get("name") == "service_role"))')" ||
        die "couldn't get the service key with that token"
fi
AUTH=(-H "Authorization: Bearer $KEY" -H "apikey: $KEY")
RESP="$(mktemp)"; trap 'rm -f "$RESP"; rm -rf "$RESP.back"' EXIT

code=$(curl -sS -o "$RESP" -w '%{http_code}' -X POST "$SUPABASE_URL/storage/v1/bucket" "${AUTH[@]}" -H "Content-Type: application/json" \
    -d "{\"id\":\"$BUCKET\",\"name\":\"$BUCKET\",\"public\":true,\"file_size_limit\":$MAX_BYTES}")
if [ "$code" != 200 ] && ! grep -qi "exist" "$RESP"; then die "creating the bucket failed ($code): $(cat "$RESP")"; fi

urlenc() { python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe="/"))' "$1"; }
upload() {   # upload <file> <name under the game's folder> <content-type> <cache seconds>
    local code
    code=$(curl -sS -o "$RESP" -w '%{http_code}' --retry 3 --retry-all-errors -X POST \
        "$SUPABASE_URL/storage/v1/object/$BUCKET/$GAME/$(urlenc "$2")" "${AUTH[@]}" \
        -H "x-upsert: true" -H "Content-Type: $3" -H "cache-control: max-age=$4" --data-binary @"$1")
    [ "$code" = 200 ] || die "upload of $2 failed ($code): $(cat "$RESP")"
    echo "  uploaded $2 ($(( $(stat -c %s "$1") / 1024 )) KB)"
}
for f in "${FILES[@]}"; do upload "$FOLDER/$f" "$f" application/octet-stream 60; done
[ -f "$FOLDER/content.manifest.sig" ] && upload "$FOLDER/content.manifest.sig" content.manifest.sig text/plain 30
upload "$FOLDER/content.manifest" content.manifest text/plain 30     # LAST: nobody sees a manifest before its files

# Read back from the public URL and verify the published pair.
mkdir -p "$RESP.back"
curl -fsS --max-time 30 -H 'Cache-Control: no-cache' "$PUBLIC/content.manifest?nocache=$(date +%s)" -o "$RESP.back/content.manifest" || die "content.manifest cannot be read back from the bucket"
if [ -n "$PUB" ]; then
    curl -fsS --max-time 30 -H 'Cache-Control: no-cache' "$PUBLIC/content.manifest.sig?nocache=$(date +%s)" -o "$RESP.back/content.manifest.sig" || die "content.manifest.sig cannot be read back from the bucket"
    "$TOOL" verify "$RESP.back" --keys "$PUB" --game "$GAME" --manifest-only >/dev/null || die "the published manifest does not verify against the published signature"
    echo "  published manifest verifies (public key ${PUB:0:16}...)"
fi
echo "Done. Content URL for the game's content.okayconfig:  url=$PUBLIC/"
