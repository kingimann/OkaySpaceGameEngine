#!/usr/bin/env python3
"""The shipped programs must not need GitHub at run time (owner decision 2026-11-20).

Checks, all run in CI (tests/scripts/test_no_github_runtime.py):

1. Code: no GitHub web address (github.com, api.github.com, *.githubusercontent.com, ".../releases/download",
   ".../releases/latest") in the code of launcher/, editor/, player/, shared/ and okayui/, outside comments.
   Tests, CMakeLists.txt (build-time fetches) and third_party folders are not shipped code and are skipped.
   A line that really must name GitHub (a player's own profile link prefix) carries the marker "github-ok:" and a reason.

1b. Words: no "GitHub" inside a string literal in launcher/, shared/ and okayui/ (anything the launcher can show).
1c. Shipped files: no GitHub address in dist/, supabase/auth-pages/ or the installer script.

2. Build: every release and test build bakes in the update server. CMakeLists.txt refuses a release configure
   (OKAY_REQUIRE_SODIUM=ON marks one) without an https://<project>.supabase.co OKAY_DEFAULT_ACCOUNT_URL;
   scripts/build-windows-dist.sh passes -DOKAY_REQUIRE_SODIUM=ON and stops when OKAY_REQUIRE_ACCOUNT=1 finds no URL;
   release.yml and dev-build.yml set OKAY_REQUIRE_ACCOUNT "1" and build with build-windows-dist.sh;
   config/public-account.json names an https supabase.co project.

Usage: check_no_github_runtime.py [--root DIR]. Exit 0 = fine, 1 = findings (printed one per line).
Written from scratch for this repository.
"""
import argparse
import json
import os
import re
import sys

CODE_DIRS = ("launcher", "editor", "player", "shared", "okayui")
CODE_EXT = (".cpp", ".hpp", ".h", ".cc", ".c", ".inl", ".mm")
URL_RE = re.compile(r"(?i)(?:[a-z0-9.-]*\.)?(?:github\.com|githubusercontent\.com)|releases/download/|releases/latest\b")
MARKER = "github-ok:"


def strip_comments(text):
    """C/C++ text with comments replaced by spaces (newlines kept), strings and characters left as they are."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            k = text.find("(", i + 2)
            if k < 0:
                out.append(c); i += 1; continue
            end = ")" + text[i + 2:k] + '"'
            j = text.find(end, k)
            j = n if j < 0 else j + len(end)
            out.append(text[i:j])
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(text[i:j])
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def code_findings(root):
    found = []
    for d in CODE_DIRS:
        top = os.path.join(root, d)
        for dirpath, dirnames, filenames in os.walk(top):
            dirnames[:] = sorted(x for x in dirnames if x not in ("tests", "test", "third_party", "build") and not x.startswith("."))
            for fn in sorted(filenames):
                if not fn.endswith(CODE_EXT) or fn.startswith("test_"):
                    continue
                path = os.path.join(dirpath, fn)
                try:
                    raw = open(path, encoding="utf-8", errors="replace").read()
                except OSError:
                    continue
                if not URL_RE.search(raw):
                    continue
                raw_lines = raw.split("\n")
                for no, line in enumerate(strip_comments(raw).split("\n"), 1):
                    if URL_RE.search(line) and MARKER not in raw_lines[no - 1]:
                        found.append("%s:%d: a GitHub address in shipped code (the programs must not need GitHub): %s"
                                     % (os.path.relpath(path, root), no, raw_lines[no - 1].strip()[:160]))
    return found


STRING_RE = re.compile(r'"(?:[^"\\\n]|\\.)*"')
WORD_DIRS = ("launcher", "shared", "okayui")
SHIPPED_FILES_DIRS = ("dist", "supabase/auth-pages")
SHIPPED_FILES = ("scripts/okayspace-installer.nsi",)


def word_findings(root):
    """The word "GitHub" in a string literal of the launcher (and the shared and OkayUI code it shows): every visible mention."""
    found = []
    for d in WORD_DIRS:
        for dirpath, dirnames, filenames in os.walk(os.path.join(root, d)):
            dirnames[:] = sorted(x for x in dirnames if x not in ("tests", "test", "third_party", "build") and not x.startswith("."))
            for fn in sorted(filenames):
                if not fn.endswith(CODE_EXT) or fn.startswith("test_"):
                    continue
                path = os.path.join(dirpath, fn)
                raw = open(path, encoding="utf-8", errors="replace").read()
                if "github" not in raw.lower():
                    continue
                raw_lines = raw.split("\n")
                for no, line in enumerate(strip_comments(raw).split("\n"), 1):
                    if MARKER in raw_lines[no - 1]:
                        continue
                    for lit in STRING_RE.findall(line):
                        if "github" in lit.lower() and not lit.startswith('"github_pat_') and not lit.startswith('"gh'):
                            found.append("%s:%d: the word GitHub in a string the launcher can show: %s" % (os.path.relpath(path, root), no, lit[:120]))
    return found


def shipped_file_findings(root):
    """github.com addresses in files that ship to players or are served to them (dist/, the installer, the auth pages)."""
    paths = [os.path.join(root, f) for f in SHIPPED_FILES]
    for d in SHIPPED_FILES_DIRS:
        for dirpath, dirnames, filenames in os.walk(os.path.join(root, d)):
            paths += [os.path.join(dirpath, f) for f in filenames]
    found = []
    for path in sorted(paths):
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for no, line in enumerate(text.split("\n"), 1):
            if URL_RE.search(line) and MARKER not in line:
                found.append("%s:%d: a GitHub address in a shipped file: %s" % (os.path.relpath(path, root), no, line.strip()[:160]))
    return found


def read(root, rel):
    try:
        return open(os.path.join(root, rel), encoding="utf-8").read()
    except OSError:
        return None


def build_findings(root):
    found = []
    cm = read(root, "CMakeLists.txt") or ""
    if not re.search(r"if\(OKAY_REQUIRE_SODIUM AND NOT OKAY_DEFAULT_ACCOUNT_URL MATCHES", cm) or "No update server" not in cm:
        found.append("CMakeLists.txt: a release configure (OKAY_REQUIRE_SODIUM=ON) must FAIL without an https://<project>.supabase.co OKAY_DEFAULT_ACCOUNT_URL")
    sh = read(root, "scripts/build-windows-dist.sh") or ""
    if re.search(r"^\s*[^#\n]*\bcp\b[^\n]*\bdocs/", sh, re.M):
        found.append("scripts/build-windows-dist.sh: copies a file from docs/ into the shipped folder (docs are internal and can name GitHub)")
    if "-DOKAY_REQUIRE_SODIUM=ON" not in sh:
        found.append("scripts/build-windows-dist.sh: must pass -DOKAY_REQUIRE_SODIUM=ON (it marks a release build, which turns on the update-server check)")
    if not re.search(r'OKAY_REQUIRE_ACCOUNT:-\}" = "1" \] && \{ \[ -z "\$_ACCT_URL" \]', sh):
        found.append("scripts/build-windows-dist.sh: must stop when OKAY_REQUIRE_ACCOUNT=1 and there is no account/update server URL")
    for wf in (".github/workflows/release.yml", ".github/workflows/dev-build.yml"):
        y = read(root, wf)
        if y is None:
            found.append("%s: missing" % wf)
            continue
        if not re.search(r'^\s+OKAY_REQUIRE_ACCOUNT:\s*"1"\s*$', y, re.M):
            found.append('%s: must set OKAY_REQUIRE_ACCOUNT: "1" so the build stops without an update server' % wf)
        if "scripts/build-windows-dist.sh" not in y:
            found.append("%s: must build with scripts/build-windows-dist.sh" % wf)
    cfg = read(root, "config/public-account.json")
    try:
        url = (json.loads(cfg).get("url") or "") if cfg else ""
    except ValueError:
        url = ""
    if not re.match(r"^https://[a-z0-9]+\.supabase\.co/?$", url):
        found.append("config/public-account.json: \"url\" must be the https://<project>.supabase.co address the updater reads releases from")
    return found


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--code-only", action="store_true")
    a = ap.parse_args(argv)
    found = code_findings(a.root) + word_findings(a.root) + shipped_file_findings(a.root) + ([] if a.code_only else build_findings(a.root))
    for f in found:
        print(f)
    print("check_no_github_runtime: %s" % ("%d problem(s)" % len(found) if found else "ok"))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
