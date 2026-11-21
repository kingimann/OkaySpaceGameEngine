#!/usr/bin/env python3
"""scripts/check_no_github_runtime.py: passes on the repository, and catches each kind of GitHub use in a planted tree."""
import os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "scripts", "check_no_github_runtime.py")
fails = 0


def run(root, *extra):
    p = subprocess.run([sys.executable, "-I", TOOL, "--root", root] + list(extra), capture_output=True, text=True)
    return p.returncode, p.stdout


def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    fails += 0 if cond else 1


code, out = run(ROOT)
check(code == 0, "the repository itself is clean:\n" + out)

with tempfile.TemporaryDirectory() as t:
    def put(rel, text):
        os.makedirs(os.path.dirname(os.path.join(t, rel)), exist_ok=True)
        open(os.path.join(t, rel), "w").write(text)
    put("launcher/a.cpp", '// https://github.com/in/a/comment is fine\n/* api.github.com too */\nint x;\n')
    check(run(t, "--code-only")[0] == 0, "comments are not code")
    for rel, text, what in [
        ("launcher/b.cpp", 'const char* u = "https://github.com/o/r/releases/latest/download/";\n', "a launcher URL"),
        ("editor/c.cpp", 'Open("https://api.github.com/repos/o/r/releases/latest");\n', "an editor API call"),
        ("player/d.hpp", 'auto s = "https://raw.githubusercontent.com/o/r/main/x";\n', "a player raw URL"),
        ("shared/e.hpp", 'auto s = "Report it on GitHub";\n', "the word GitHub in a shown string"),
        ("dist/README.txt", "see https://github.com/o/r\n", "a shipped file"),
        ("scripts/okayspace-installer.nsi", 'WriteRegStr HKLM "x" "URLInfoAbout" "https://github.com/o/r"\n', "the installer"),
    ]:
        put(rel, text)
        code, out = run(t, "--code-only")
        check(code == 1 and rel in out, "catches " + what)
        os.remove(os.path.join(t, rel))
    put("launcher/f.cpp", 'auto s = "https://github.com/";   // github-ok: a reason\n')
    check(run(t, "--code-only")[0] == 0, "the marker allows a line")
    code, out = run(t)
    check(code == 1 and "CMakeLists.txt" in out and "release.yml" in out and "public-account.json" in out, "the build checks fail on an empty tree")

print("%d failure(s)" % fails)
sys.exit(1 if fails else 0)
