#!/usr/bin/env bash
# Optional git pre-push hook for this clone only: runs scripts/dup_lint.py check on what is about to be pushed,
# so a pasted block or a new copy of a shared helper is caught before CI sees it.
#
#   bash scripts/install_hooks.sh            install (this repository's hooks folder only)
#   bash scripts/install_hooks.sh --remove   remove it again
#
# Never installed automatically and never written to global git config. It refuses to replace a pre-push hook it did
# not write. Skip it for one push with: git push --no-verify
# Plain rules: docs/process/no_duplicates.md.
set -euo pipefail
MARK="okay-dup-lint-hook"
root=$(git rev-parse --show-toplevel)
hooks=$(git -C "$root" rev-parse --git-path hooks)
case "$hooks" in /*) ;; *) hooks="$root/$hooks" ;; esac
hook="$hooks/pre-push"

if [ "${1:-}" = "--remove" ]; then
  if [ -f "$hook" ] && grep -q "$MARK" "$hook"; then rm -f "$hook"; echo "removed $hook"; else echo "no dup-lint pre-push hook here"; fi
  exit 0
fi
if [ -f "$hook" ] && ! grep -q "$MARK" "$hook"; then
  echo "a pre-push hook that this script did not write is already at $hook; not touching it." >&2
  echo "add this line to it yourself:  python3 scripts/dup_lint.py check --base origin/main --head <sha being pushed>" >&2
  exit 1
fi
mkdir -p "$hooks"
cat > "$hook" <<'EOF'
#!/usr/bin/env bash
# okay-dup-lint-hook: written by scripts/install_hooks.sh. Remove with: bash scripts/install_hooks.sh --remove
# git gives one line per ref: <local ref> <local sha> <remote ref> <remote sha>
root=$(git rev-parse --show-toplevel)
base=origin/main
git rev-parse -q --verify "$base" >/dev/null || { echo "dup-lint: no $base here; skipped (git fetch origin main)"; exit 0; }
rc=0
while read -r lref lsha rref rsha; do
  case "$lsha" in 0000000000000000000000000000000000000000) continue ;; esac   # deleting a branch
  echo "dup-lint: checking $lref ($lsha) against $base"
  python3 "$root/scripts/dup_lint.py" check --root "$root" --base "$base" --head "$lsha" >/dev/null || rc=1
done
if [ $rc -ne 0 ]; then
  echo "dup-lint: duplicate code found (see above). Fix it, mark a real need with 'dup-allow: <reason>', or push with --no-verify." >&2
fi
exit $rc
EOF
chmod +x "$hook"
echo "installed $hook (runs scripts/dup_lint.py check before each push; remove with --remove)"
