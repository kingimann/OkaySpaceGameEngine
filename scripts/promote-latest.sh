#!/usr/bin/env bash
# Mark the GitHub release v<version> as "latest" (the release that launchers and releases/latest/download/<file> follow), but only
# FORWARD: when a newer release is already latest, or a newer version is already published, this leaves v<version> a normal release
# and exits 0.
#
#   scripts/promote-latest.sh 2.876.0
#   DRY_RUN=1 scripts/promote-latest.sh 2.876.0      # say what would happen, change nothing
#
# Why: releases can be built and published in parallel (docs/release_channels.md, "Several releases at once"). The one that finishes
# its build last is not always the newest, and `gh release edit --latest` is last-writer-wins, so an older release finishing late would
# take "latest" away from a newer one. The version is compared with
#   1. the release GitHub marks as latest now (gh release view), and
#   2. "latest" in versions.json on the release server: the newest version whose checksums are published
#      (scripts/publish-supabase.sh never moves it backwards).
# Newer than the given version in either = leave it alone (the newer release's own run makes it latest). Equal or older = promote.
#
# Used by the "Make the release visible (latest)" job of .github/workflows/release.yml and by scripts/release-local.sh.
#
# Environment:
#   GH_TOKEN            for gh (the workflow passes github.token)
#   GITHUB_REPOSITORY   owner/name (also GH_REPO); without it gh works out the repository from the git remote
#   RELEASES_BASE       public address of the release bucket, e.g. https://<ref>.supabase.co/storage/v1/object/public/releases
#                       (default: built from SUPABASE_URL, else the project URL baked into CMakeLists.txt, and BUCKET, default releases)
#   TRIES, TRY_PAUSE   how often and how long apart (seconds) a failing gh or curl call is repeated (3 and 3)
#   DRY_RUN=1           change nothing
set -euo pipefail
cd "$(dirname "$0")/.."

say() { echo "promote-latest: $*"; if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then echo "$*" >> "$GITHUB_STEP_SUMMARY"; fi; return 0; }
die() { echo "promote-latest: $*" >&2; exit 1; }
# decline <message>: v<version> is NOT made latest because something newer is. A run that quietly does nothing looks like a bug, so this
# is also an annotation on the run page in GitHub Actions.
decline() {
    say "$*"
    if [ "${GITHUB_ACTIONS:-}" = true ]; then echo "::warning title=Release not made latest::$*"; fi
    return 0
}

VER="${1:-}"; VER="${VER#v}"
[[ "$VER" =~ ^[0-9]+(\.[0-9]+)+$ ]] || die "usage: scripts/promote-latest.sh <version, like 2.876.0>"
TRIES="${TRIES:-3}"; TRY_PAUSE="${TRY_PAUSE:-3}"
REPO="${GH_REPO:-${GITHUB_REPOSITORY:-}}"
REPO_ARGS=(); if [ -n "$REPO" ]; then REPO_ARGS=(--repo "$REPO"); fi

# version_newer <a> <b>: a is a strictly newer version than b (2.9.0 < 2.10.0; 2.875 equals 2.875.0)
version_newer() {
    local a="$1" b="$2"
    while [ "$(printf '%s' "$a" | tr -cd . | wc -c)" -lt 2 ]; do a="$a.0"; done
    while [ "$(printf '%s' "$b" | tr -cd . | wc -c)" -lt 2 ]; do b="$b.0"; done
    [ "$a" != "$b" ] && [ "$(printf '%s\n%s\n' "$a" "$b" | sort -V | tail -n1)" = "$a" ]
}

# 1. The release GitHub calls latest now (nothing when there is none yet).
GH_LATEST=""
err="$(mktemp)"; trap 'rm -f "$err"' EXIT
tag=""; ok=0
for ((i = 1; i <= TRIES; i++)); do
    if tag="$(gh release view "${REPO_ARGS[@]}" --json tagName --jq .tagName 2>"$err")"; then ok=1; break; fi
    if grep -qi 'release not found' "$err"; then tag=""; ok=1; break; fi   # gh's words when there is no latest release yet (the first ever)
    echo "promote-latest: could not ask GitHub for the latest release (try $i of $TRIES): $(head -c 200 "$err")" >&2
    sleep "$TRY_PAUSE"
done
[ "$ok" = 1 ] || die "cannot tell which release is latest on GitHub, so v$VER is NOT marked latest (it could take latest away from a newer release). Run this again, or: gh release edit v$VER --latest"
tag="${tag#v}"; tag="$(printf '%s' "$tag" | tr -d '\r ')"
if [[ "$tag" =~ ^[0-9]+(\.[0-9]+)+$ ]]; then GH_LATEST="$tag"; fi

# 2. "latest" in the published list (the release server). Unknown (no list, no answer) = not a reason to stop: GitHub's answer decides.
if [ -z "${RELEASES_BASE:-}" ]; then
    url="${SUPABASE_URL:-$(grep -m1 '^set(OKAY_DEFAULT_ACCOUNT_URL' CMakeLists.txt 2>/dev/null | grep -oE 'https://[a-z0-9]+\.supabase\.co' || true)}"
    if [ -n "$url" ]; then RELEASES_BASE="${url%/}/storage/v1/object/public/${BUCKET:-releases}"; fi
fi
PUB_LATEST=""
if [ -n "${RELEASES_BASE:-}" ]; then
    for ((i = 1; i <= TRIES; i++)); do
        if body="$(curl -fsS --max-time 20 -H 'Cache-Control: no-cache' "${RELEASES_BASE%/}/versions.json?nocache=$(date +%s)$RANDOM" 2>/dev/null)"; then
            PUB_LATEST="$(printf '%s' "$body" | python3 -c 'import sys, json; v = json.load(sys.stdin).get("latest"); print(v if isinstance(v, str) else "")' 2>/dev/null || true)"
            break
        fi
        sleep "$TRY_PAUSE"
    done
else
    echo "promote-latest: no release server address (RELEASES_BASE): comparing with GitHub's latest release only" >&2
fi
[[ "$PUB_LATEST" =~ ^[0-9]+(\.[0-9]+)+$ ]] || PUB_LATEST=""

# 3. Decide.
if [ -n "$GH_LATEST" ] && version_newer "$GH_LATEST" "$VER"; then
    decline "A newer release (v$GH_LATEST) is already latest on GitHub: v$VER stays a normal release (it is not made latest)."
    exit 0
fi
if [ -n "$PUB_LATEST" ] && version_newer "$PUB_LATEST" "$VER"; then
    decline "A newer release (v$PUB_LATEST) is already published: its own run makes it latest, so v$VER stays a normal release (it is not made latest)."
    exit 0
fi
if [ -n "$GH_LATEST" ] && ! version_newer "$VER" "$GH_LATEST"; then   # equal
    say "v$VER is already the latest release."
    exit 0
fi
if [ "${DRY_RUN:-0}" = 1 ]; then say "DRY_RUN: would mark v$VER as the latest release (latest on GitHub now: ${GH_LATEST:-none}; newest published: ${PUB_LATEST:-unknown})."; exit 0; fi
gh release edit "v$VER" "${REPO_ARGS[@]}" --latest
say "v$VER is now the latest release, with checksums published."
