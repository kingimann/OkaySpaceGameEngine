#!/usr/bin/env python3
"""Offline test of scripts/branch_lint.py winpaths: file names Windows cannot check out (control characters, < > : " | ? *, a trailing space or dot,
reserved device names) are errors when a branch adds them; ordinary names are not. Uses a throw-away git repository."""
import os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import branch_lint as B

failures = 0
def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", what)

# the pure function
for good in ["a.txt", "docs/own_ui_toolkit.md", "okayui/tests/golden/x.png", ".gitignore", "dir.with.dots/file", "console.log", "COM10.txt", "nullable.cpp", "a b/c d.txt"]:
    check(B.windows_path_problem(good) is None, "accepted: %r" % good)
for bad in ["\x1f", "a/\x1fb", "ssets\x01", "a<b", "a>b", 'a"b', "a|b", "a?b", "a*b", "dir/na:me", "trail.", "trail ", "x/dir./f", "CON", "con.txt", "x/NUL.md", "AUX", "COM1", "lpt9.log", "PRN.c"]:
    check(B.windows_path_problem(bad) is not None, "refused: %r" % bad)

# the git check, on a real repository with odd names
with tempfile.TemporaryDirectory() as d:
    def git(*a):
        return subprocess.run(["git", "-C", d, "-c", "user.name=t", "-c", "user.email=t@t", *a], capture_output=True, text=True, check=True)
    git("init", "-q", "-b", "main")
    open(os.path.join(d, "ok.txt"), "w").write("x")
    git("add", "-A"); git("commit", "-q", "-m", "base")
    git("branch", "base")
    for n in ["fine.cpp", "\x1f", "aux.txt", "bad?.txt"]:
        open(os.path.join(d, n), "w").write("")
    git("add", "-A"); git("commit", "-q", "-m", "branch")
    f = B.check_winpaths(d, "base", "HEAD")
    names = sorted(x["file"] for x in f)
    check(len(f) == 3, "three bad files found, got %r" % names)
    check(all(x["sev"] == "error" for x in f), "all are errors")
    check(any("\\x1f" in x["file"] for x in f), "the control-character name is shown escaped")
    check(not any(x["file"] == "fine.cpp" for x in f), "fine.cpp is not flagged")
    check(B.check_winpaths(d, "HEAD", "HEAD") == [], "no diff, no findings")

print("branch_lint winpaths: %s (%d failures)" % ("ok" if not failures else "FAILED", failures))
sys.exit(1 if failures else 0)
