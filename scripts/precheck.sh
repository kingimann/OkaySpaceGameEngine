#!/usr/bin/env bash
# Ready to push? The checks that turned PRs red most often, run here in about a minute instead of after a CI run.
# CLAUDE.md rule 7a. Run it before every push; the pre-push hook (scripts/install_hooks.sh) runs it for you.
#
#   bash scripts/precheck.sh [--base origin/main] [--head HEAD] [--build <dir with compile_commands.json>] [--tests]
#   bash scripts/precheck.sh --restore-generated [--base origin/main]   put the base's copy of every generated doc back
#   bash scripts/precheck.sh --list-generated                           print the generated docs (one path per line)
#   bash scripts/precheck.sh --regen-generated                          rebuild them (what regen-docs.yml runs on main)
#
# Generated docs are rebuilt on main only (.github/workflows/regen-docs.yml), never in a PR: the feature map
# (docs/FEATURE_MAP.md, docs/SYSTEMS.md, docs/feature_map/_index_*.md from scripts/gen_feature_map.py), the code index
# (docs/engine/code_index.md and docs/engine/code_index/ from scripts/gen_code_index.py) and docs/try_it.md
# (scripts/gen_try_it.py). A PR edits only the sources (docs/feature_map/NN-*.md pages, headers, docs/try_it/*.md).
# When every PR committed the generated copies, each merge made every other open PR conflict in them.
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
#   4. the feature map pages are not valid (python3 scripts/gen_feature_map.py --validate: a sub-area without its
#      What/Code/Status/Words lines, a bad System line, a part outside its sub-area; checks the working tree, so run it
#      on the checked-out branch you push, which is what the hook does)
#   5. copies between open branches (collision_check --branch, offline, so no GitHub call)
#   6. with --build (or OKAY_PRECHECK_BUILD): a changed .cpp file does not compile (-fsyntax-only, using its own
#      command from compile_commands.json, one file at a time, so it fits in little memory)
# Warnings only: the branch changes a generated doc (restore it with --restore-generated); a docs/try_it/ entry is not
# valid; the branch is far behind the base.
# Nothing is built or pushed. Standard tools only (bash, git, python3, and the compiler for step 6).
set -uo pipefail
base="origin/main"; head="HEAD"; build="${OKAY_PRECHECK_BUILD:-}"; tests=0; mode=check
while [ $# -gt 0 ]; do
  case "$1" in
    --restore-generated) mode=restore; shift ;;
    --list-generated) mode=list; shift ;;
    --regen-generated) mode=regen; shift ;;
    --base) base="$2"; shift 2 ;;
    --head) head="$2"; shift 2 ;;
    --build) build="$2"; shift 2 ;;
    --tests) tests=1; shift ;;
    *) echo "precheck: unknown argument $1" >&2; exit 2 ;;
  esac
done
root=$(git rev-parse --show-toplevel)
cd "$root" || exit 2

# The generated docs (paths and folders; git pathspecs) and the commands that rebuild them. The single list:
# regen-docs.yml reads it with --list-generated.
GENERATED=(docs/FEATURE_MAP.md docs/SYSTEMS.md docs/feature_map/_index_symptoms.md docs/feature_map/_index_az.md
           docs/engine/code_index.md docs/engine/code_index docs/try_it.md)
REGEN=("scripts/gen_feature_map.py" "scripts/gen_code_index.py" "scripts/gen_try_it.py")
if [ "$mode" = list ]; then printf '%s\n' "${GENERATED[@]}"; exit 0; fi
if [ "$mode" = regen ]; then
  rc=0
  for g in "${REGEN[@]}"; do
    [ -f "$g" ] || continue
    python3 "$g" || { echo "precheck: $g failed" >&2; rc=1; }
  done
  exit $rc
fi
errors=0
fail() { echo "  FAIL: $*"; errors=$((errors + 1)); }
ok()   { echo "  ok:   $*"; }
warn() { echo "  warn: $*"; }

git rev-parse -q --verify "$base" >/dev/null || { echo "precheck: $base not found (git fetch origin main)"; exit 2; }

if [ "$mode" = restore ]; then
  # every generated doc that differs from the base (committed or not), plus new untracked ones
  files=$( { git diff --name-only "$base" -- "${GENERATED[@]}"; git ls-files --others --exclude-standard -- "${GENERATED[@]}"; } | sort -u)
  if [ -z "$files" ]; then echo "precheck: the generated docs already match $base"; exit 0; fi
  for f in $files; do
    if git cat-file -e "$base:$f" 2>/dev/null; then git checkout -q "$base" -- "$f"
    else git rm -q -f --ignore-unmatch -- "$f" >/dev/null; rm -f -- "$f"; fi
    echo "  restored: $f"
  done
  echo "precheck: generated docs now match $base (they are rebuilt on main). Commit the result."
  exit 0
fi

changed=$(git diff --name-only --diff-filter=AM "$base...$head")
gen_changed=$(git diff --name-only "$base...$head" -- "${GENERATED[@]}")
gen_hint="bash scripts/precheck.sh --restore-generated (or git checkout $base -- <files>), then commit"
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
else fail "does not merge cleanly with $base: merge it in and resolve first (git merge $base)"
  [ -n "$gen_changed" ] && echo "        generated docs in the way? they are rebuilt on main; restore them: $gen_hint"; fi
behind=$(git rev-list --count "$head..$base")
[ "$behind" -gt 50 ] && warn "$behind commits behind $base: merge it in so CI tests what will land"

# 3. branch_lint all (includes, secrets, binaries, docs, dups)
if python3 scripts/branch_lint.py all --base "$base" --head "$head" >/tmp/precheck_lint.$$ 2>&1; then ok "branch_lint all"
else fail "branch_lint all:"; grep -E '::error|ERROR|FAIL' /tmp/precheck_lint.$$ | head -15 | sed 's/^/        /'; fi
rm -f /tmp/precheck_lint.$$

# 3b. fixed IP-audit findings stay fixed (docs/status/provenance_audit.md, "Fixes")
if python3 scripts/provenance_scan.py check >/tmp/precheck_prov.$$ 2>&1; then ok "provenance guards"
else fail "provenance_scan.py check:"; grep -v '^#' /tmp/precheck_prov.$$ | head -10 | sed 's/^/        /'; fi
rm -f /tmp/precheck_prov.$$

# 4. the hand-written sources of the generated docs are valid (the generated docs themselves are rebuilt on main)
if python3 scripts/gen_feature_map.py --validate >/tmp/precheck_fm.$$ 2>&1; then ok "feature map pages valid"
else fail "feature map pages (docs/feature_map/*.md):"; grep -E '^error:' /tmp/precheck_fm.$$ | head -10 | sed 's/^/        /'; fi
rm -f /tmp/precheck_fm.$$
if [ -f scripts/gen_try_it.py ] && ! python3 scripts/gen_try_it.py --check-entries >/dev/null 2>&1; then
  warn "a docs/try_it/ entry is not valid: python3 scripts/gen_try_it.py --check-entries"
fi
if [ -n "$gen_changed" ]; then
  warn "this branch changes generated docs: $(echo $gen_changed | tr '\n' ' ')"
  warn "  they are rebuilt on main (regen-docs.yml); committing them makes other PRs conflict. Use main's copy: $gen_hint"
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
