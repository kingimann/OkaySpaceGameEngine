#!/usr/bin/env bash
# Publish the official OkaySpace game to Supabase Storage, so every launcher can
# install it from the Play page and keep it up to date. Run it on the folder the
# editor's File > Build Game made (<Game>.exe + SDL2.dll + Data/...).
#
#   SUPABASE_ACCESS_TOKEN=sbp_...  scripts/publish-official-game.sh <built-game-folder> [version]
#   SUPABASE_SERVICE_KEY=eyJ...    scripts/publish-official-game.sh <built-game-folder> [version]
#
# Keys come from the environment only: never put them in a file in this repo.
#
# What it uploads (public bucket "releases", beside the engine's own files):
#   official/v<ver>/<path>          every file of the game, in its own version folder
#   official/v<ver>/manifest.json   name, version, exe, notes, cover, size and each
#                                   file's size + sha256 (launchers verify every download)
#   official/NOTES.txt              "what's new" lines
#   official/VERSION.txt            uploaded LAST, so nobody sees a version before its files
# Older official/v* folders beyond the newest $KEEP are deleted.
#
# The version is [version], else the first line of VERSION.txt in the folder,
# else today's date (2026.10.24.1, then .2 for a second publish that day). It must
# be newer than the published one: launchers only ever move forward.
#
# Options (environment):
#   NAME         the game's name in the launcher (default: title= in game.okayconfig, else the exe's name)
#   EXE          the game's exe, relative to the folder (default: <title>.exe, or the only .exe)
#   NOTES_FILE   lines for NOTES.txt and the manifest's notes (default: "- <name> v<ver>")
#   KEEP         how many versions to keep in the bucket (default 3)
#   FORCE=1      publish even if the version isn't newer than the published one
#   SUPABASE_URL project URL (default: the one baked into CMakeLists.txt)
#   DRY_RUN=1    check everything and write the manifest, upload nothing
set -euo pipefail
cd "$(dirname "$0")/.."

die() { echo "publish-official-game: $*" >&2; exit 1; }

[ $# -ge 1 ] && [ $# -le 2 ] || die "usage: scripts/publish-official-game.sh <built-game-folder> [version]"
GAME_DIR="${1%/}"
[ -d "$GAME_DIR" ] || die "no folder $GAME_DIR"
GAME_DIR="$(cd "$GAME_DIR" && pwd)"

KEEP="${KEEP:-3}"
BUCKET="${BUCKET:-releases}"
PREFIX="official"
SUPABASE_URL="${SUPABASE_URL:-$(grep -m1 '^set(OKAY_DEFAULT_ACCOUNT_URL' CMakeLists.txt | grep -oE 'https://[a-z0-9]+\.supabase\.co')}"
SUPABASE_URL="${SUPABASE_URL%/}"
REF="$(printf '%s' "$SUPABASE_URL" | sed -E 's#https://([a-z0-9]+)\.supabase\.co#\1#')"
MAX_BYTES=$((50 * 1024 * 1024))   # Supabase's per-file upload limit on this plan
[[ "$KEEP" =~ ^[1-9][0-9]*$ ]] || die "KEEP must be a whole number (got $KEEP)"

# ---- The game's files ---------------------------------------------------------
# Every file under the folder, relative, forward slashes. Hidden files (.git,
# .DS_Store, ...) are left out.
mapfile -t FILES < <(cd "$GAME_DIR" && find . -type f ! -path '*/.*' -printf '%P\n' | LC_ALL=C sort)
[ "${#FILES[@]}" -gt 0 ] || die "$GAME_DIR is empty"

# The title the editor's Build Game wrote (Data/game.okayconfig, or at the root).
CFG=""
for c in "$GAME_DIR/Data/game.okayconfig" "$GAME_DIR/game.okayconfig"; do [ -f "$c" ] && { CFG="$c"; break; }; done
TITLE=""
[ -n "$CFG" ] && TITLE="$(grep -m1 '^title=' "$CFG" | cut -d= -f2- | tr -d '\r')" || true

# The exe: EXE, else <title>.exe, else the only .exe at the top (not a -Server.exe).
if [ -z "${EXE:-}" ]; then
    if [ -n "$TITLE" ] && [ -f "$GAME_DIR/$TITLE.exe" ]; then EXE="$TITLE.exe"
    else
        mapfile -t EXES < <(cd "$GAME_DIR" && find . -maxdepth 1 -type f -iname '*.exe' ! -iname '*-Server.exe' -printf '%P\n')
        [ "${#EXES[@]}" -eq 1 ] || die "found ${#EXES[@]} game exes in $GAME_DIR; say which with EXE=<name>.exe"
        EXE="${EXES[0]}"
    fi
fi
[ -f "$GAME_DIR/$EXE" ] || die "no $EXE in $GAME_DIR"
head -c2 "$GAME_DIR/$EXE" | grep -q '^MZ' || die "$EXE isn't a Windows program (build the game for Windows)"
NAME="${NAME:-${TITLE:-$(basename "$EXE" .exe)}}"

# The published version (public, no key needed); "" if nothing is published yet.
PUB_URL="$SUPABASE_URL/storage/v1/object/public/$BUCKET/$PREFIX"
PUBLISHED="$(curl -sS --fail --retry 2 "$PUB_URL/VERSION.txt?cb=$(date +%s)" 2>/dev/null | head -n1 | tr -d '\r ' || true)"
[[ "$PUBLISHED" =~ ^[0-9]+(\.[0-9]+)+$ ]] || PUBLISHED=""

VER="${2:-}"
[ -n "$VER" ] || VER="$(head -n1 "$GAME_DIR/VERSION.txt" 2>/dev/null | tr -d '\r ' || true)"
if [ -z "$VER" ]; then
    VER="$(date +%Y.%m.%d).1"
    if [[ "$PUBLISHED" == "$(date +%Y.%m.%d)."* ]]; then VER="$(date +%Y.%m.%d).$(( ${PUBLISHED##*.} + 1 ))"; fi
fi
[[ "$VER" =~ ^[0-9]+(\.[0-9]+)+$ ]] || die "bad version '$VER' (use dotted numbers, e.g. 1.2.0)"
newer() {   # newer <a> <b>: a > b, comparing dotted numbers
    python3 -c 'import sys; p = lambda s: [int(x) for x in s.split(".")]; a, b = p(sys.argv[1]), p(sys.argv[2]); n = max(len(a), len(b)); sys.exit(0 if a + [0] * (n - len(a)) > b + [0] * (n - len(b)) else 1)' "$1" "$2"
}
if [ -n "$PUBLISHED" ] && ! newer "$VER" "$PUBLISHED"; then
    [ "${FORCE:-0}" = 1 ] || die "v$VER isn't newer than the published v$PUBLISHED (pass a newer version, or FORCE=1)"
    echo "FORCE: publishing v$VER over v$PUBLISHED"
fi

# ---- Check every file, then write the manifest ---------------------------------
# The launcher refuses unsafe paths, so catch them here first (same rules).
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
NOTES="$TMP/NOTES.txt"
if [ -n "${NOTES_FILE:-}" ]; then cp "$NOTES_FILE" "$NOTES"; else printf -- '- %s v%s\n' "$NAME" "$VER" > "$NOTES"; fi
IMAGE=""
for c in cover.png cover.jpg; do [ -f "$GAME_DIR/$c" ] && { IMAGE="$c"; break; }; done

printf '%s\n' "${FILES[@]}" > "$TMP/files.txt"
python3 - "$GAME_DIR" "$TMP/files.txt" "$TMP/manifest.json" "$MAX_BYTES" "$NAME" "$VER" "$EXE" "$NOTES" "$IMAGE" <<'PY' || exit 1
import hashlib, json, os, re, sys
root, listing, out, max_bytes, name, ver, exe, notes_file, image = sys.argv[1:]
max_bytes = int(max_bytes)
devices = {"con", "prn", "aux", "nul", "conin$", "conout$"} | {f"com{i}" for i in range(1, 10)} | {f"lpt{i}" for i in range(1, 10)}
def unsafe(p):
    if p.startswith("/") or len(p) > 400: return "absolute or too long"
    if re.search(r'[\x00-\x1f\x7f\\:<>"|?*]', p): return "a character Windows can't store"
    for i, part in enumerate(p.split("/")):
        if part in ("", ".", ".."): return "an empty, '.' or '..' part"
        if part.endswith((".", " ")): return "a part ending in a dot or space"
        if part.split(".")[0].lower() in devices: return "a reserved device name"
        if i == 0 and part.lower() == ".okayofficial": return "the launcher's own folder"
    return None
files, seen, total = [], set(), 0
for rel in open(listing, encoding="utf-8").read().splitlines():
    why = unsafe(rel)
    if why: sys.exit(f"publish-official-game: {rel!r} has {why}; rename it")
    if rel.lower() in seen: sys.exit(f"publish-official-game: {rel!r} differs from another file only in case")
    seen.add(rel.lower())
    path = os.path.join(root, rel)
    size = os.path.getsize(path)
    if size > max_bytes:
        sys.exit(f"publish-official-game: {rel} is {size // 1048576} MB, over Supabase's 50 MB file limit")
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""): h.update(block)
    files.append({"path": rel, "size": size, "sha256": h.hexdigest()})
    total += size
m = {"name": name, "version": ver, "exe": exe, "notes": open(notes_file, encoding="utf-8").read().strip()}
if image: m["image"] = image
m["size"] = total
m["files"] = files
with open(out, "w", encoding="utf-8") as f: json.dump(m, f, indent=1, ensure_ascii=False)
print(f"  {len(files)} files, {total / 1048576:.1f} MB")
PY

echo "Publishing $NAME v$VER ($EXE) to $SUPABASE_URL (bucket '$BUCKET', folder '$PREFIX/')"
[ -n "$PUBLISHED" ] && echo "  published now: v$PUBLISHED" || echo "  nothing published yet"

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
RESP="$TMP/resp"

if [ "${DRY_RUN:-0}" = 1 ]; then
    cp "$TMP/manifest.json" "${DRY_RUN_MANIFEST:-/dev/null}" 2>/dev/null || true
    echo "DRY_RUN: everything checks out; nothing uploaded."
    exit 0
fi

# ---- Bucket (public reads; writes need the service key) ----------------------
code=$(curl -sS -o "$RESP" -w '%{http_code}' -X POST "$SUPABASE_URL/storage/v1/bucket" "${AUTH[@]}" \
    -H "Content-Type: application/json" \
    -d "{\"id\":\"$BUCKET\",\"name\":\"$BUCKET\",\"public\":true,\"file_size_limit\":$MAX_BYTES}")
if [ "$code" != 200 ] && ! grep -qi "exist" "$RESP"; then die "creating the bucket failed ($code): $(cat "$RESP")"; fi

urlpath() { python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "$1"; }
ctype() {
    case "${1,,}" in
        *.png) echo image/png ;;   *.jpg|*.jpeg) echo image/jpeg ;;
        *.json) echo application/json ;;   *.txt|*.okayconfig) echo "text/plain; charset=utf-8" ;;
        *) echo application/octet-stream ;;
    esac
}
# upload <file> <path in bucket> <content-type> <cache seconds>
upload() {
    local code
    code=$(curl -sS -o "$RESP" -w '%{http_code}' --retry 3 --retry-all-errors -X POST \
        "$SUPABASE_URL/storage/v1/object/$BUCKET/$(urlpath "$2")" "${AUTH[@]}" \
        -H "x-upsert: true" -H "Content-Type: $3" -H "cache-control: max-age=$4" \
        --data-binary @"$1")
    [ "$code" = 200 ] || die "upload of $2 failed ($code): $(cat "$RESP")"
    echo "  uploaded $2 ($(( $(stat -c %s "$1") / 1024 )) KB)"
}

for f in "${FILES[@]}"; do upload "$GAME_DIR/$f" "$PREFIX/v$VER/$f" "$(ctype "$f")" 3600; done
upload "$TMP/manifest.json" "$PREFIX/v$VER/manifest.json" application/json 3600
upload "$NOTES" "$PREFIX/NOTES.txt" "text/plain; charset=utf-8" 30
printf '%s\n' "$VER" > "$TMP/VERSION.txt"
upload "$TMP/VERSION.txt" "$PREFIX/VERSION.txt" text/plain 30

# ---- Keep only the newest $KEEP versions --------------------------------------
list() {   # list <prefix>  -> "<name>\t<file|dir>" per entry (folders have no id)
    curl -sS -X POST "$SUPABASE_URL/storage/v1/object/list/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" \
        -d "{\"prefix\":\"$1\",\"limit\":1000}" |
        python3 -c 'import sys, json; [print(o["name"] + "\t" + ("dir" if o.get("id") is None else "file")) for o in json.load(sys.stdin)]'
}
files_under() {   # files_under <folder/>  -> every object under it, recursively
    local name kind
    while IFS=$'\t' read -r name kind; do
        [ -n "$name" ] || continue
        if [ "$kind" = dir ]; then files_under "$1$name/"; else printf '%s\n' "$1$name"; fi
    done < <(list "$1")
}
mapfile -t OLD < <(list "$PREFIX/" | cut -f1 | grep -E '^v[0-9.]+$' | sed 's/^v//' | sort -t. -k1,1n -k2,2n -k3,3n -k4,4n | head -n -"$KEEP")
for v in "${OLD[@]}"; do
    mapfile -t GONE < <(files_under "$PREFIX/v$v/")
    [ "${#GONE[@]}" -gt 0 ] || continue
    # In batches, so a big game doesn't make one huge request.
    for ((i = 0; i < ${#GONE[@]}; i += 200)); do
        body=$(printf '%s\n' "${GONE[@]:i:200}" | python3 -c 'import sys, json; print(json.dumps({"prefixes": [l.rstrip("\n") for l in sys.stdin if l.strip()]}))')
        curl -sS -o /dev/null -X DELETE "$SUPABASE_URL/storage/v1/object/$BUCKET" "${AUTH[@]}" -H "Content-Type: application/json" -d "$body"
    done
    echo "  removed old official v$v"
done

echo "Done: launchers will see $NAME v$VER at $PUB_URL/VERSION.txt"
