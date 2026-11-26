#!/usr/bin/env bash
# The place on main of the commit being built, as "<count> <sha>" on one line: <sha> is the newest commit of origin/main that the build contains
# (HEAD itself for a build of main; for a branch, the main commit it was built on top of = the merge base) and <count> is
# `git rev-list --count <sha>`. The launcher compares these counts to tell whether a release holds anything a running test build does not
# (launcher/UpdaterCore.hpp DecideOffer, shared/TestBuild.hpp ContentAhead); release.yml and dev-build.yml build it into the programs
# (OKAY_MAIN_COUNT) and publish it in versions.json / dev/versions.json (mainCount, mainSha).
#
#   bash scripts/main_position.sh [ref]          # ref defaults to HEAD
#   bash scripts/main_position.sh --self-test
#
# A shallow checkout (the default in GitHub Actions) is deepened first: a count from a shallow history would be wrong. When the place cannot be
# found (no origin/main, no network) it prints nothing and exits 0, so the build goes on and the place reads as "not known" (0).
set -euo pipefail

position() {
    local ref="${1:-HEAD}" base count
    if [ "$(git rev-parse --is-shallow-repository 2>/dev/null || echo false)" = true ]; then
        timeout 900 git fetch --quiet --unshallow origin 2>/dev/null || return 0
    fi
    timeout 300 git fetch --quiet origin "+refs/heads/main:refs/remotes/origin/main" 2>/dev/null || true   # (offline or slow: use what is here)
    git rev-parse --verify --quiet origin/main >/dev/null || return 0
    base="$(git merge-base "$ref" origin/main 2>/dev/null)" || return 0
    count="$(git rev-list --count "$base" 2>/dev/null)" || return 0
    [[ "$count" =~ ^[1-9][0-9]*$ && "$base" =~ ^[0-9a-f]{40}$ ]] || return 0
    printf '%s %s\n' "$count" "$base"
}

self_test() {
    local d fails=0 out c1 c3
    d="$(mktemp -d)"
    trap 'rm -rf "$d"' RETURN
    (
        set -e
        cd "$d"
        git init --quiet -b main origin.git && cd origin.git
        git -c user.name=t -c user.email=t@t commit --quiet --allow-empty -m one
        git -c user.name=t -c user.email=t@t commit --quiet --allow-empty -m two
        git -c user.name=t -c user.email=t@t commit --quiet --allow-empty -m three
        cd .. && git clone --quiet origin.git work && cd work
        git checkout --quiet -b feature HEAD~1
        git -c user.name=t -c user.email=t@t commit --quiet --allow-empty -m branch-work
    ) || { echo "main_position self-test: could not make the test repository"; return 1; }
    cd "$d/work"
    out="$(position main)"; c3="$(git rev-parse main)"
    [ "$out" = "3 $c3" ] || { echo "FAIL: a build of main is at its own place ($out)"; fails=1; }
    out="$(position feature)"; c1="$(git rev-parse main~1)"
    [ "$out" = "2 $c1" ] || { echo "FAIL: a branch build is at the main commit it was built on ($out)"; fails=1; }
    git remote remove origin
    out="$(position feature)"
    [ -z "$out" ] || { echo "FAIL: no origin/main gives nothing ($out)"; fails=1; }
    cd /
    echo "main_position self-test: $([ "$fails" = 0 ] && echo ok || echo FAILED)"
    return "$fails"
}

if [ "${1:-}" = "--self-test" ]; then self_test; exit $?; fi
position "${1:-HEAD}"
