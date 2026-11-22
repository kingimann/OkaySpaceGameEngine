#!/usr/bin/env python3
"""Check that a code move is a pure move: lines taken out of one file come back unchanged, in the same order, in the
files they moved to. Only edits written down in a rules file (and harmless scaffolding such as includes, namespace
lines, blank lines and comments) are allowed on top of that.

Used for the editor/main.cpp split (docs/editor/main_split_plan.md). Standard library only; git is used to read the
base version of each file.

    python3 scripts/check_pure_move.py --base origin/main --source editor/main.cpp \
        --dest editor/BuildGame.hpp --dest editor/BuildGame.cpp --dest editor/MainShared.hpp \
        --rules docs/editor/moves/01_build_game.rules

What it does:
  1. Diffs every listed file between --base and the new version (the working tree, or --head REV).
  2. Lines removed from --source are the moved lines. Each --dest file's added lines are matched against them in order
     (difflib's longest matching blocks), so a moved line that comes back in a different order does not match.
  3. Whatever is left over must be explained:
       - a removed line that did not come back must be dropped by a `drop:` rule (blank lines are dropped by default);
       - an added line that is not a moved line must be scaffolding (blank, `//` comment, #pragma once, #include,
         `namespace x {`, `} // namespace x`) or match an `add:` rule;
       - a line that came back edited must be listed as a `change:` rule (old ==> new). Rules compare lines with
         the surrounding whitespace ignored.
  4. Lines added to --source itself (for example the new #include) must be scaffolding or match an `add:` rule.

Rules file (one rule per line, # starts a comment line):
    change: <old line> ==> <new line>       one moved line that had to change to compile (each rule is used once)
    add: <regular expression>               an added line that is allowed (new declarations, guards)
    drop: <regular expression>              a removed line that is allowed to disappear

Exit code 0 when the move is pure, 1 when it is not, 2 on a usage error.
"""
import argparse
import difflib
import os
import re
import subprocess
import sys
import tempfile

SCAFFOLDING = [
    (re.compile(r"^\s*$"), "blank"),
    (re.compile(r"^\s*//"), "comment"),
    (re.compile(r"^\s*#\s*pragma\s+once\s*$"), "pragma once"),
    (re.compile(r"^\s*#\s*include\s+[<\"][^>\"]+[>\"]\s*(//.*)?$"), "include"),
    (re.compile(r"^\s*namespace(\s+[A-Za-z_]\w*(::[A-Za-z_]\w*)*)?\s*\{\s*(//.*)?$"), "namespace open"),
    (re.compile(r"^\s*\}\s*//\s*namespace\b.*$"), "namespace close"),
]


def git(repo, *args):
    p = subprocess.run(["git", "-C", repo] + list(args), capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def read_version(repo, rev, path):
    """The file's text at `rev` (None = working tree). A missing file reads as empty."""
    if rev is None:
        full = os.path.join(repo, path)
        if not os.path.exists(full):
            return ""
        with open(full, encoding="utf-8", errors="surrogateescape", newline="") as f:
            return f.read()
    code, out, _ = git(repo, "show", "%s:%s" % (rev, path))
    return out if code == 0 else ""


def split_lines(text):
    return [l.rstrip("\r") for l in text.split("\n")] if text else []


def diff(old_text, new_text):
    """(removed, added): lists of (line number, text) from a minimal diff of the two texts."""
    old, new = split_lines(old_text), split_lines(new_text)
    removed, added = [], []
    # Trim the shared start and end first: for a move most of a large file is unchanged, and this keeps difflib fast.
    lo = 0
    while lo < len(old) and lo < len(new) and old[lo] == new[lo]:
        lo += 1
    hi_o, hi_n = len(old), len(new)
    while hi_o > lo and hi_n > lo and old[hi_o - 1] == new[hi_n - 1]:
        hi_o -= 1
        hi_n -= 1
    a, b = old[lo:hi_o], new[lo:hi_n]
    if len(a) * len(b) > 4000000:
        return diff_with_git(old_text, new_text)
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag in ("replace", "delete"):
            removed += [(lo + i + 1, a[i]) for i in range(i1, i2)]
        if tag in ("replace", "insert"):
            added += [(lo + j + 1, b[j]) for j in range(j1, j2)]
    return removed, added


def diff_with_git(old_text, new_text):
    """The same as diff() through `git diff --no-index --minimal`, for very large changed regions."""
    with tempfile.TemporaryDirectory() as t:
        po, pn = os.path.join(t, "old"), os.path.join(t, "new")
        for p, text in ((po, old_text), (pn, new_text)):
            with open(p, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
                f.write(text)
        p = subprocess.run(["git", "diff", "--no-index", "--no-color", "--no-ext-diff", "--minimal", "-U0", po, pn],
                           capture_output=True, text=True, errors="surrogateescape")
    removed, added = [], []
    on, nn, in_hunk = 0, 0, False
    for line in p.stdout.split("\n"):
        m = re.match(r"^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@", line)
        if m:
            on, nn, in_hunk = int(m.group(1)), int(m.group(2)), True
            continue
        if not in_hunk or not line:
            continue
        if line[0] == "-":
            removed.append((on, line[1:]))
            on += 1
        elif line[0] == "+":
            added.append((nn, line[1:]))
            nn += 1
    return removed, added


def load_rules(path):
    changes, adds, drops = [], [], []
    if not path:
        return changes, adds, drops
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            kind, _, rest = line.partition(":")
            kind, rest = kind.strip(), rest.strip()
            if kind == "change":
                if " ==> " not in rest:
                    raise ValueError("%s:%d: a change rule needs 'old ==> new'" % (path, n))
                old, new = rest.split(" ==> ", 1)
                changes.append({"old": old.strip(), "new": new.strip(), "used": False, "where": "%s:%d" % (path, n)})
            elif kind == "add":
                adds.append((re.compile(rest), "%s:%d" % (path, n)))
            elif kind == "drop":
                drops.append((re.compile(rest), "%s:%d" % (path, n)))
            else:
                raise ValueError("%s:%d: unknown rule kind '%s' (change, add or drop)" % (path, n, kind))
    return changes, adds, drops


def scaffolding_kind(text):
    for rx, name in SCAFFOLDING:
        if rx.search(text):
            return name
    return None


def check(repo, base, head, source, dests, rules_path, out=sys.stdout):
    changes, adds, drops = load_rules(rules_path)
    ok = True
    say = lambda s="": print(s, file=out)

    src_removed, src_added = diff(read_version(repo, base, source), read_version(repo, head, source))
    say("source %s: %d line(s) removed, %d line(s) added" % (source, len(src_removed), len(src_added)))

    used = [False] * len(src_removed)
    leftovers_added = []          # (file, line number, text) added lines that are not moved lines
    for dest in dests:
        dest_removed, dest_added = diff(read_version(repo, base, dest), read_version(repo, head, dest))
        for ln, text in dest_removed:
            ok = False
            say("  FAIL %s:%d: a line was removed from a destination file: %s" % (dest, ln, text))
        remaining = [i for i in range(len(src_removed)) if not used[i]]
        a = [src_removed[i][1] for i in remaining]
        b = [t for _, t in dest_added]
        sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
        matched_b = [False] * len(b)
        moved, blocks = 0, 0
        for blk in sm.get_matching_blocks():
            if blk.size == 0:
                continue
            # A run of only blank / comment lines is not counted as moved code (it would hide a reorder).
            if all(scaffolding_kind(a[blk.a + k]) in ("blank",) for k in range(blk.size)):
                continue
            blocks += 1
            for k in range(blk.size):
                used[remaining[blk.a + k]] = True
                matched_b[blk.b + k] = True
            moved += blk.size
        say("  %s: %d moved line(s) in %d block(s), %d other added line(s)"
            % (dest, moved, blocks, matched_b.count(False)))
        leftovers_added += [(dest, dest_added[j][0], b[j]) for j in range(len(b)) if not matched_b[j]]

    leftovers_removed = [(src_removed[i][0], src_removed[i][1]) for i in range(len(src_removed)) if not used[i]]

    # change rules: one removed line and one added line each.
    applied = []
    for rule in changes:
        ri = next((k for k, (_, t) in enumerate(leftovers_removed) if t.strip() == rule["old"]), None)
        ai = next((k for k, (_, _, t) in enumerate(leftovers_added) if t.strip() == rule["new"]), None)
        if ri is None or ai is None:
            ok = False
            say("  FAIL change rule %s was not used (old line %s, new line %s)"
                % (rule["where"], "found" if ri is not None else "not found", "found" if ai is not None else "not found"))
            continue
        rl = leftovers_removed.pop(ri)
        al = leftovers_added.pop(ai)
        applied.append("%s:%d -> %s:%d  %s  ==>  %s" % (source, rl[0], al[0], al[1], rule["old"], rule["new"]))
    if applied:
        say("allowed edits (%d):" % len(applied))
        for a in applied:
            say("  " + a)

    kinds = {}
    for f, ln, text in leftovers_added + [(source, ln, t) for ln, t in src_added]:
        k = scaffolding_kind(text)
        if k is None and any(rx.search(text) for rx, _ in adds):
            k = "added by rule"
        if k is None:
            ok = False
            say("  FAIL %s:%d: an added line that is not a moved line and no rule allows it: %s" % (f, ln, text))
        else:
            kinds[k] = kinds.get(k, 0) + 1
    for ln, text in leftovers_removed:
        if not text.strip():
            continue
        if any(rx.search(text) for rx, _ in drops):
            kinds["dropped by rule"] = kinds.get("dropped by rule", 0) + 1
            continue
        ok = False
        say("  FAIL %s:%d: a removed line did not come back unchanged (or came back out of order): %s" % (source, ln, text))
    if kinds:
        say("other lines: " + ", ".join("%s %d" % (k, v) for k, v in sorted(kinds.items())))
    say("PURE MOVE" if ok else "NOT A PURE MOVE")
    return ok


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--repo", default=".", help="the git repository (default: the current folder)")
    ap.add_argument("--base", required=True, help="the version before the move, e.g. origin/main")
    ap.add_argument("--head", default=None, help="the version after the move (default: the working tree)")
    ap.add_argument("--source", required=True, help="the file the code moved out of")
    ap.add_argument("--dest", action="append", required=True, help="a file the code moved into (repeat)")
    ap.add_argument("--rules", default=None, help="the allowed edits (see the top of this file)")
    args = ap.parse_args(argv)
    try:
        ok = check(args.repo, args.base, args.head, args.source, args.dest, args.rules)
    except (OSError, ValueError, re.error) as e:
        print("error: %s" % e, file=sys.stderr)
        return 2
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
