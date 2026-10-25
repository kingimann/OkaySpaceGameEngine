#!/usr/bin/env bash
# scripts/publish-supabase.sh stops before any network use when the build is
# missing, and says why (it used to exit 1 with no message under set -e).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

out="$(env -u SUPABASE_SERVICE_KEY -u SUPABASE_ACCESS_TOKEN DIST_DIR="$T/no-such-dist" \
       bash "$REPO/scripts/publish-supabase.sh" 2>&1)"
code=$?
fail=0
[ "$code" -ne 0 ] || { echo "FAIL: exit code 0 without a build"; fail=1; }
case "$out" in
    *"no version in $T/no-such-dist/VERSION.txt"*) ;;
    *) echo "FAIL: no reason given (output: '$out')"; fail=1 ;;
esac

# A build with a version but no exes: the next check names the missing file.
mkdir -p "$T/dist"; printf '1.2.3\n' > "$T/dist/VERSION.txt"
out="$(env -u SUPABASE_SERVICE_KEY -u SUPABASE_ACCESS_TOKEN DIST_DIR="$T/dist" bash "$REPO/scripts/publish-supabase.sh" 2>&1)"
case "$out" in
    *"missing $T/dist/OkaySpace.exe"*) ;;
    *) echo "FAIL: missing exe not reported (output: '$out')"; fail=1 ;;
esac
[ "$fail" = 0 ] && echo "publish_supabase_checks: ok"
exit "$fail"
