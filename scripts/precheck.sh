#!/usr/bin/env bash
# Ready to push? The checks that turned PRs red most often, run here in about a minute instead of after a CI run.
# CLAUDE.md rule 7a. Run it before every push; the pre-push hook (scripts/install_hooks.sh) runs it for you.
#
#   bash scripts/precheck.sh [--base origin/main] [--head HEAD] [--build <dir with compile_commands.json>] [--tests]
#
# --tests (needs --build: a configured Ninja build folder of THIS checkout, e.g. cmake -S . -B build -G Ninja
#   -DCMAKE_BUILD_TYPE=Release, as CI's "Build & test (Ubuntu)" job does): builds every test program, which only
#   recompiles what this branch changed, then runs each test program that had to be rebuilt, so every test that can see
#   the change runs here before CI does. Jobs are capped by memory (scripts/ci_jobs.sh). The first run in a new build
#   folder builds everything (long); later runs only what changed.
#
# Errors (exit 1) - each one would make a CI job red:
#   1. conflict markers in the files the branch changes
#   2. the branch does not merge cleanly with the base (merge main in first)
#   3. branch_lint all: Windows thread includes (rule 21a), secrets, built programs or big binaries, duplicate code
#   4. the feature map is not regenerated (python3 scripts/gen_feature_map.py; checks the working tree, so run it on
#      the checked-out branch you push, which is what the hook does)
#   5. copies between open branches (collision_check --branch, offline, so no GitHub call)
#   6. with --build (or OKAY_PRECHECK_BUILD): a changed .cpp file does not compile (-fsyntax-only, using its own
#      command from compile_commands.json, one file at a time, so it fits in little memory)
# Warnings only: the code index is stale; the branch is far behind the base.
# Nothing is built or pushed. Standard tools only (bash, git, python3, and the compiler for step 6).
set -uo pipefail
base="origin/main"; head="HEAD"; build="${OKAY_PRECHECK_BUILD:-}"; tests=0
while [ $# -gt 0 ]; do
  case "$1" in
    --base) base="$2"; shift 2 ;;
    --head) head="$2"; shift 2 ;;
    --build) build="$2"; shift 2 ;;
    --tests) tests=1; shift ;;
    *) echo "precheck: unknown argument $1" >&2; exit 2 ;;
  esac
done
root=$(git rev-parse --show-toplevel)
cd "$root" || exit 2
errors=0
fail() { echo "  FAIL: $*"; errors=$((errors + 1)); }
ok()   { echo "  ok:   $*"; }
warn() { echo "  warn: $*"; }

git rev-parse -q --verify "$base" >/dev/null || { echo "precheck: $base not found (git fetch origin main)"; exit 2; }
changed=$(git diff --name-only --diff-filter=AM "$base...$head")
echo "precheck: $head against $base ($(echo "$changed" | grep -c . ) changed files)"

# 1. conflict markers
marks=""
for f in $changed; do
  [ -f "$f" ] || continue
  git show "$head:$f" 2>/dev/null | grep -n -E '^(<<<<<<<|=======|>>>>>>>)( |$)' >/dev/null && marks="$marks $f"
done
[ -z "$marks" ] && ok "no conflict markers" || fail "conflict markers in:$marks"

# 2. merges cleanly with the base
if git merge-tree --write-tree "$base" "$head" >/dev/null 2>&1; then ok "merges cleanly with $base"
else fail "does not merge cleanly with $base: merge it in and resolve first (git merge $base)"; fi
behind=$(git rev-list --count "$head..$base")
[ "$behind" -gt 50 ] && warn "$behind commits behind $base: merge it in so CI tests what will land"

# 3. branch_lint all (includes, secrets, binaries, docs, dups)
if python3 scripts/branch_lint.py all --base "$base" --head "$head" >/tmp/precheck_lint.$$ 2>&1; then ok "branch_lint all"
else fail "branch_lint all:"; grep -E '::error|ERROR|FAIL' /tmp/precheck_lint.$$ | head -15 | sed 's/^/        /'; fi
rm -f /tmp/precheck_lint.$$

# 4. feature map (and the code index, warning only, as in branch-lint)
python3 scripts/gen_feature_map.py --check >/dev/null 2>&1 && ok "feature map up to date" \
  || fail "feature map out of date: run python3 scripts/gen_feature_map.py and commit"
if [ -f scripts/gen_code_index.py ]; then
  python3 scripts/gen_code_index.py --check >/dev/null 2>&1 && ok "code index up to date" \
    || warn "code index out of date: python3 scripts/gen_code_index.py"
fi

# 5. copies between open branches (offline: the local remote refs, no GitHub call)
br=$(git rev-parse --abbrev-ref "$head" 2>/dev/null); br="${br#origin/}"
if [ -n "$br" ] && [ "$br" != "HEAD" ] && [ -f scripts/collision_check.py ]; then
  if python3 scripts/collision_check.py --branch "$br" --offline >/tmp/precheck_cc.$$ 2>&1; then ok "no copies or claim clashes with other open branches"
  else fail "collision_check --branch $br:"; grep -i -E 'collision|clash|same code' /tmp/precheck_cc.$$ | head -8 | sed 's/^/        /'; fi
  rm -f /tmp/precheck_cc.$$
fi

# 6. changed C++ files compile (syntax only), if a build folder with compile_commands.json is given
if [ -n "$build" ] && [ -f "$build/compile_commands.json" ]; then
  cpps=$(echo "$changed" | grep -E '\.(cpp|cc)$' || true)
  n=0; bad=""
  for f in $cpps; do
    cmd=$(python3 - "$build/compile_commands.json" "$root/$f" <<'PY'
import json, sys, shlex
db, want = sys.argv[1], sys.argv[2]
for e in json.load(open(db)):
    if e["file"] == want:
        args = e.get("arguments") or shlex.split(e["command"])
        out = [a for i, a in enumerate(args) if a != "-o" and (i == 0 or args[i - 1] != "-o")]
        print(e["directory"] + "\t" + shlex.join(out + ["-fsyntax-only"]))
        break
PY
)
    [ -z "$cmd" ] && continue
    n=$((n + 1))
    (cd "${cmd%%$'\t'*}" && eval "${cmd#*$'\t'}") >/dev/null 2>&1 || bad="$bad $f"
  done
  [ -z "$bad" ] && ok "$n changed C++ files compile (syntax only)" || fail "does not compile:$bad"
elif [ -n "$build" ]; then
  warn "no compile_commands.json in $build: C++ syntax check skipped"
fi

# 7. --tests: build what changed and run every test program that had to be rebuilt
if [ "$tests" = 1 ]; then
  if [ -z "$build" ] || [ ! -f "$build/build.ninja" ]; then
    fail "--tests needs --build <configured Ninja build folder of this checkout>"
  else
    stamp=$(mktemp); sleep 1; touch "$stamp"
    jobs=$( [ -f scripts/ci_jobs.sh ] && bash scripts/ci_jobs.sh 4 2>/dev/null || echo 2 )
    targets=$(ninja -C "$build" -t targets all 2>/dev/null | sed -n 's/^\(tests\/test_[A-Za-z0-9_]*\): .*/\1/p' | sort -u)
    if ! ninja -C "$build" -j "$jobs" $targets >/tmp/precheck_build.$$ 2>&1; then
      fail "build failed:"; grep -E 'error|FAILED' /tmp/precheck_build.$$ | head -10 | sed 's/^/        /'
    else
      rebuilt=$(find "$build/tests" -maxdepth 1 -type f -name 'test_*' -newer "$stamp" -perm -u+x | sort)
      n=0; bad=""
      for t in $rebuilt; do
        n=$((n + 1)); name=$(basename "$t" | sed 's/^test_//')
        # run it the way ctest does (from the tests folder), with ctest's own command when it knows the test
        if ctest --test-dir "$build" -R "^${name}\$" -N 2>/dev/null | grep -q "Test *#"; then
          ctest --test-dir "$build" -R "^${name}\$" --timeout 300 >/tmp/precheck_t.$$ 2>&1 || bad="$bad $name"
        else
          (cd "$build/tests" && timeout 300 "$t" >/tmp/precheck_t.$$ 2>&1) || bad="$bad $name"
        fi
      done
      rm -f /tmp/precheck_t.$$
      if [ -n "$bad" ]; then fail "tests failing:$bad (re-run: ctest --test-dir $build -R <name> --output-on-failure)"
      else ok "$n rebuilt test programs pass"; fi
    fi
    rm -f /tmp/precheck_build.$$ "$stamp"
  fi
fi

if [ "$errors" -gt 0 ]; then
  echo "precheck: $errors problem(s). Fix them before pushing; each would turn a CI job red."
  exit 1
fi
echo "precheck: ready to push"
