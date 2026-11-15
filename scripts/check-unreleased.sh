#!/usr/bin/env bash
# Refuse to build a release for a version that is already released (CLAUDE.md rule 12: never re-run the release workflow for a
# version that already exists).
#
#   scripts/check-unreleased.sh 2.876.0
#
# Exits 1 when v<version> already exists:
#   * versions.json on the release server lists it (its checksums are published), or
#   * a GitHub release v<version> exists (CHECK_GITHUB_RELEASE=1, the default; `gh release view`).
# Why this runs at the START of the release job: a second run of one version rebuilds the programs, so the files and checksums people
# already have would change, and the GitHub release step (which comes BEFORE the Supabase publish in that job) would overwrite the
# release assets of the version before anything else could refuse. The publish script refuses a changed version too, but only after
# the GitHub assets are already overwritten. Used by .github/workflows/release.yml when PUBLISH_V2=1.
#
# A half-finished release is not a reason to run the workflow again: finish it from the failed run's artifact instead
# (docs/release_channels.md, "If the Supabase step of a run failed").
#
# Environment:
#   GH_TOKEN                for gh (the workflow passes github.token)
#   GITHUB_REPOSITORY       owner/name (also GH_REPO); without it gh works out the repository from the git remote
#   RELEASES_BASE           public address of the release bucket (default: built from SUPABASE_URL, else the project URL baked into
#                           CMakeLists.txt, and BUCKET, default releases)
#   CHECK_GITHUB_RELEASE    1 (default) also refuse when a GitHub release for the version exists; 0 only looks at versions.json (the
#                           workflow sets 0 for a tag push, where the release may have been created by hand just before the tag)
#   TRIES, TRY_PAUSE        how often and how long apart (seconds) a failing gh or curl call is repeated (3 and 3)
set -euo pipefail
cd "$(dirname "$0")/.."

die() { echo "check-unreleased: $*" >&2; if [ "${GITHUB_ACTIONS:-}" = true ]; then echo "::error title=Version already released::$*"; fi; exit 1; }

VER="${1:-}"; VER="${VER#v}"
[[ "$VER" =~ ^[0-9]+(\.[0-9]+)+$ ]] || { echo "usage: scripts/check-unreleased.sh <version, like 2.876.0>" >&2; exit 2; }
TRIES="${TRIES:-3}"; TRY_PAUSE="${TRY_PAUSE:-3}"
REPO="${GH_REPO:-${GITHUB_REPOSITORY:-}}"
REPO_ARGS=(); if [ -n "$REPO" ]; then REPO_ARGS=(--repo "$REPO"); fi
WHAT="Running the release workflow for it again would rebuild it and change the files and checksums that people already have (CLAUDE.md rule 12). Bump the version in CMakeLists.txt (a new release PR). If that release really failed half way, finish it from the failed run's artifact instead (docs/release_channels.md, \"If the Supabase step of a run failed\")."

TMP="$(mktemp)"; trap 'rm -f "$TMP" "$TMP.err"' EXIT

# 1. A GitHub release for this version.
if [ "${CHECK_GITHUB_RELEASE:-1}" != 0 ]; then
    ok=0
    for ((i = 1; i <= TRIES; i++)); do
        if gh release view "v$VER" "${REPO_ARGS[@]}" --json tagName --jq .tagName > "$TMP" 2> "$TMP.err"; then
            die "v$VER already has a GitHub release. $WHAT"
        fi
        if grep -qi 'release not found' "$TMP.err"; then ok=1; break; fi   # gh's words for "no such release": good
        echo "check-unreleased: could not ask GitHub about v$VER (try $i of $TRIES): $(head -c 200 "$TMP.err")" >&2
        sleep "$TRY_PAUSE"
    done
    [ "$ok" = 1 ] || die "cannot tell whether v$VER already has a GitHub release, so the release is stopped before it builds anything (run it again; this check only fails when GitHub cannot be asked)"
fi

# 2. The checksum list on the release server. Not being able to read it is not a reason to stop (the publish step checks the same
#    thing again under its lock); a list that says the version is there is.
if [ -z "${RELEASES_BASE:-}" ]; then
    url="${SUPABASE_URL:-$(grep -m1 '^set(OKAY_DEFAULT_ACCOUNT_URL' CMakeLists.txt 2>/dev/null | grep -oE 'https://[a-z0-9]+\.supabase\.co' || true)}"
    if [ -n "$url" ]; then RELEASES_BASE="${url%/}/storage/v1/object/public/${BUCKET:-releases}"; fi
fi
if [ -n "${RELEASES_BASE:-}" ]; then
    for ((i = 1; i <= TRIES; i++)); do
        code="$(curl -sS -o "$TMP" -w '%{http_code}' --max-time 20 -H 'Cache-Control: no-cache' "${RELEASES_BASE%/}/versions.json?nocache=$(date +%s)$RANDOM" 2>/dev/null || true)"
        if [ "$code" = 200 ]; then
            if python3 -c '
import json, sys
want = sys.argv[2]
data = json.load(open(sys.argv[1]))
have = [e.get("version") for e in data.get("versions", []) if isinstance(e, dict)]
sys.exit(0 if want in have else 1)' "$TMP" "$VER" 2>/dev/null; then
                die "v$VER is already listed in versions.json on the release server. $WHAT"
            fi
            echo "check-unreleased: v$VER is not in versions.json and has no GitHub release: fine to build."
            exit 0
        fi
        if [ "$code" = 404 ] || { [ "$code" = 400 ] && grep -qiE 'not.found|"statusCode" *: *"?404"?' "$TMP" 2>/dev/null; }; then
            echo "check-unreleased: there is no versions.json yet (the first release), and v$VER has no GitHub release: fine to build."
            exit 0
        fi
        sleep "$TRY_PAUSE"
    done
    echo "check-unreleased: WARNING: could not read versions.json (HTTP ${code:-000}); only the GitHub check was made. The publish step checks again." >&2
else
    echo "check-unreleased: no release server address (RELEASES_BASE): only the GitHub check was made." >&2
fi
echo "check-unreleased: v$VER has no GitHub release: fine to build."
