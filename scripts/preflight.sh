#!/usr/bin/env bash
# Local preflight: run this before every push. It finds in under a minute the mistakes that otherwise cost a 25 to 90
# minute CI run (docs/preflight.md, docs/ci_failures.md). No build is needed.
#   bash scripts/preflight.sh                  checks the branch against origin/main
#   bash scripts/preflight.sh --no-fetch       do not run `git fetch origin main` first
#   bash scripts/preflight.sh --base <ref>     compare with another ref
# Exit code 0 = nothing found, 1 = at least one problem (the list at the end says which).
# Steps: merge conflicts with main; branch_lint (rule 21a thread includes, secrets, binaries); test registration;
# feature map and try-it entries; MinGW -fsyntax-only on changed engine/launcher/editor/player/shared/okayui files
# (headers also with <intrin.h> before and after); host -fsyntax-only on changed tests/*.cpp.
set -u
cd "$(dirname "$0")/.." || exit 2
BASE=origin/main; FETCH=1
while [ $# -gt 0 ]; do
    case "$1" in
        --no-fetch) FETCH=0 ;;
        --base) shift; BASE="${1:?--base needs a ref}" ;;
        -h|--help) sed -n 2,12p "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
PY=python3; command -v python3 >/dev/null 2>&1 || PY=python
FAILED=()
step() { # step "<title>" cmd args...
    local title="$1"; shift
    printf '\n== %s\n' "$title"
    if "$@"; then echo "   ok"; else echo "   FAILED: $title"; FAILED+=("$title"); fi
}

if [ "$FETCH" = 1 ] && [ "$BASE" = origin/main ]; then git fetch --quiet origin main 2>/dev/null || echo "(could not fetch origin/main, using what is here)"; fi
git rev-parse --verify --quiet "$BASE" >/dev/null || { echo "no $BASE here: run 'git fetch origin main' first" >&2; exit 2; }

conflicts() {
    # git merge-tree --write-tree (git 2.38+) does a real merge in memory and touches nothing.
    local out rc
    out=$(git merge-tree --write-tree --name-only "$BASE" HEAD 2>&1); rc=$?
    if [ $rc -eq 0 ]; then return 0; fi
    if [ $rc -ne 1 ]; then echo "   (could not run git merge-tree: $(echo "$out" | head -1)); try: git merge $BASE"; return 0; fi
    echo "$out" | awk 'NR==1{next} /^$/{exit} {print "   conflict in: " $0}'
    echo "   Fix: git merge $BASE, resolve (keep both sides), build the touched tests, push."
    echo "   Generated files (docs/FEATURE_MAP.md, docs/feature_map/*): take main's copy, then run python3 scripts/gen_feature_map.py."
    echo "   docs/try_it.md is generated from docs/try_it/*.md: do not edit it, add a file in docs/try_it/ (python3 scripts/gen_try_it.py)."
    return 1
}
step "Merge conflicts with $BASE" conflicts

CHANGED=$(git diff --name-only "$BASE"...HEAD 2>/dev/null)
echo; echo "Changed files against $BASE: $(echo "$CHANGED" | grep -c .)"

step "Rule 21a: no <mutex>/<thread>/std::call_once in Windows code" $PY scripts/branch_lint.py includes --base "$BASE" --head HEAD
step "No secrets" $PY scripts/branch_lint.py secrets --base "$BASE" --head HEAD
step "No programs or big binary files" $PY scripts/branch_lint.py binaries --base "$BASE" --head HEAD
step "Test registration (no unmarked duplicate, no shared or new fixed /tmp path)" $PY scripts/lint_test_registration.py --base "$BASE"
step "Try-it entries are valid" $PY scripts/gen_try_it.py --check-entries
step "Feature map is up to date" $PY scripts/gen_feature_map.py --check

if command -v x86_64-w64-mingw32-g++-win32 >/dev/null 2>&1 || command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    step "MinGW -fsyntax-only on changed Windows-reachable files" $PY scripts/branch_lint.py mingw --strict --base "$BASE" --head HEAD
else
    echo; echo "== MinGW -fsyntax-only: SKIPPED (no x86_64-w64-mingw32-g++ here; install g++-mingw-w64-x86-64). CI will still check it."
fi
step "Host -fsyntax-only on changed tests/*.cpp" $PY scripts/branch_lint.py tests --base "$BASE" --head HEAD

echo
if [ ${#FAILED[@]} -eq 0 ]; then echo "PREFLIGHT OK (this is not the full CI: it does not build or run the tests)."; exit 0; fi
echo "PREFLIGHT FAILED: ${#FAILED[@]} step(s):"; printf '  - %s\n' "${FAILED[@]}"; exit 1
