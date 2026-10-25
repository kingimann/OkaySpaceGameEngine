#!/usr/bin/env bash
# scripts/publish-official-game.sh against a fake Supabase (a stub `curl` on
# PATH; nothing leaves the machine). Rolling back with FORCE=1 to an older
# version must not delete that version in the "keep the newest $KEEP" clean-up:
# it is the live one once VERSION.txt points at it.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
command -v python3 >/dev/null || { echo "SKIP: python3 not found"; exit 0; }

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin" "$T/game/Data"
printf 'MZfake' > "$T/game/Game.exe"
printf 'title=Game\n' > "$T/game/Data/game.okayconfig"

# The bucket: v1.0.0 .. v1.3.0 under official/, v1.3.0 live.
cat > "$T/bin/curl" <<'EOF'
#!/usr/bin/env bash
out="" fmt="" data="" method=GET url=""
while [ $# -gt 0 ]; do
    case "$1" in
        -o) out="$2"; shift ;;
        -w) fmt="$2"; shift ;;
        -d|--data-binary) data="$2"; shift ;;
        -X) method="$2"; shift ;;
        -H|--retry) shift ;;
        -*) ;;
        *) url="$1" ;;
    esac
    shift
done
reply() { if [ -n "$out" ]; then printf '%s' "$1" > "$out"; else printf '%s' "$1"; fi; [ -n "$fmt" ] && printf '200'; return 0; }
case "$method $url" in
    "GET "*/object/public/*/official/VERSION.txt*) reply "1.3.0" ;;
    "POST "*/storage/v1/bucket) reply '{"message":"already exists"}' ;;
    "POST "*/object/list/*)
        case "$data" in
            *'"prefix":"official/"'*) reply '[{"name":"v1.0.0","id":null},{"name":"v1.1.0","id":null},{"name":"v1.2.0","id":null},{"name":"v1.3.0","id":null},{"name":"VERSION.txt","id":"x"}]' ;;
            *) reply '[{"name":"Game.exe","id":"y"}]' ;;
        esac ;;
    "POST "*) echo "upload ${url#*/object/}" >> "$FAKE_LOG"; reply '{}' ;;
    "DELETE "*) echo "delete $data" >> "$FAKE_LOG"; reply '[]' ;;
    *) echo "unexpected: $method $url" >> "$FAKE_LOG"; exit 22 ;;
esac
EOF
chmod +x "$T/bin/curl"

export FAKE_LOG="$T/log"; : > "$FAKE_LOG"
PATH="$T/bin:$PATH" FORCE=1 KEEP=3 SUPABASE_URL="https://fake.supabase.co" SUPABASE_SERVICE_KEY="not-a-key" \
    bash "$REPO/scripts/publish-official-game.sh" "$T/game" 1.0.0 > "$T/out" 2>&1 || { cat "$T/out"; echo "FAIL: the publish failed"; exit 1; }

fail=0
grep -q "upload releases/official/VERSION.txt" "$FAKE_LOG" || { echo "FAIL: VERSION.txt wasn't uploaded"; fail=1; }
if grep "^delete" "$FAKE_LOG" | grep -q 'official/v1\.0\.0/'; then echo "FAIL: the clean-up deleted the version it just made live"; fail=1; fi
if grep -q "unexpected" "$FAKE_LOG"; then grep "unexpected" "$FAKE_LOG"; fail=1; fi
[ "$fail" = 0 ] || { echo "--- log"; cat "$FAKE_LOG"; echo "--- output"; cat "$T/out"; exit 1; }
echo "publish_official_cleanup: ok"
