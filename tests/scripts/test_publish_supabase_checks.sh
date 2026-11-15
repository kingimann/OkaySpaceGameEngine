#!/usr/bin/env bash
# scripts/publish-supabase.sh stops before any network use when the build is
# missing, and says why (it used to exit 1 with no message under set -e).
# Run for every way into the publish: the default (scripts/publish-supabase.sh hands over to the old
# script unless PUBLISH_V2=1), the old script directly (scripts/publish-supabase-v1.sh), and the new
# script (PUBLISH_V2=1).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0

for variant in default v1-direct v2; do
    case "$variant" in
        default)   script="$REPO/scripts/publish-supabase.sh";    v2=0 ;;
        v1-direct) script="$REPO/scripts/publish-supabase-v1.sh"; v2=0 ;;
        v2)        script="$REPO/scripts/publish-supabase.sh";    v2=1 ;;
    esac
    out="$(env -u SUPABASE_SERVICE_KEY -u SUPABASE_ACCESS_TOKEN PUBLISH_V2="$v2" DIST_DIR="$T/no-such-dist" \
           bash "$script" 2>&1)"
    code=$?
    [ "$code" -ne 0 ] || { echo "FAIL [$variant]: exit code 0 without a build"; fail=1; }
    case "$out" in
        *"no version in $T/no-such-dist/VERSION.txt"*) ;;
        *) echo "FAIL [$variant]: no reason given (output: '$out')"; fail=1 ;;
    esac

    # A build with a version but no exes: the next check names the missing file.
    rm -rf "$T/dist"; mkdir -p "$T/dist"; printf '1.2.3\n' > "$T/dist/VERSION.txt"
    out="$(env -u SUPABASE_SERVICE_KEY -u SUPABASE_ACCESS_TOKEN PUBLISH_V2="$v2" DIST_DIR="$T/dist" bash "$script" 2>&1)"
    case "$out" in
        *"missing $T/dist/OkaySpace.exe"*) ;;
        *) echo "FAIL [$variant]: missing exe not reported (output: '$out')"; fail=1 ;;
    esac
done
[ "$fail" = 0 ] && echo "publish_supabase_checks: ok"
exit "$fail"
