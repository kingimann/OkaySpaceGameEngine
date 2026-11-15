#!/usr/bin/env python3
"""PR hygiene (CLAUDE.md rule 4a): a PR body needs a 'How to try it' section with real content.

  PR_BODY="$BODY" python3 scripts/check_pr_body.py [--files a b c | --base origin/main --head HEAD]

Passes when the section exists and says something: steps, or for a change with nothing to run (docs only, tests only, CI)
'Nothing to run: read <file>' or 'n/a, <why>'. Fails (exit 1) when the section is missing, empty, only a template
comment, or just a dash. A docs-only change (every changed file under docs/ or a .md file) may use any non-empty text.
Standard library only.
"""
import argparse, os, re, subprocess, sys

HEAD_RE = re.compile(r"^#{1,6}\s*How to try it\b.*$", re.I | re.M)


def section(body):
    m = HEAD_RE.search(body or "")
    if not m:
        return None
    rest = body[m.end():]
    nxt = re.search(r"^#{1,6}\s+\S", rest, re.M)
    return rest[:nxt.start()] if nxt else rest


def meaningful(text):
    t = re.sub(r"<!--.*?-->", "", text or "", flags=re.S)
    t = re.sub(r"^[\s>*+-]+$", "", t, flags=re.M)
    t = t.replace("`", "").strip()
    return t


def check(body, files=None):
    sec = section(body)
    if sec is None:
        return False, "the PR body has no '## How to try it' section (rule 4a). Add one: exact steps and what you should see, or 'Nothing to run: read <file>' for docs-only work."
    t = meaningful(sec)
    if not t:
        return False, "the 'How to try it' section is empty (only the template comment or a dash). Say what to open, click or press and what to expect, or 'Nothing to run: read <file>'."
    words = re.findall(r"\w+", t)
    docs_only = bool(files) and all(f.startswith("docs/") or f.endswith(".md") for f in files)
    if len(words) < 3 and not docs_only:
        return False, "the 'How to try it' section is too short to be steps. Write the steps, or 'Nothing to run: read <file>' / 'n/a, <why>'."
    if re.fullmatch(r"(?i)(n/?a|none|todo|tbd|-+)\.?", t.strip()):
        return False, "'How to try it' says only '" + t.strip() + "'. Say why there is nothing to run (for example 'Nothing to run: read docs/x.md')."
    return True, "ok"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--body-file")
    ap.add_argument("--files", nargs="*")
    ap.add_argument("--base"); ap.add_argument("--head", default="HEAD")
    a = ap.parse_args()
    body = open(a.body_file, encoding="utf-8").read() if a.body_file else os.environ.get("PR_BODY", "")
    files = a.files
    if files is None and a.base:
        files = subprocess.run(["git", "diff", "--name-only", f"{a.base}...{a.head}"], stdout=subprocess.PIPE, text=True).stdout.split()
    ok, msg = check(body, files)
    print(("OK: " if ok else "::error title=PR needs a How to try it section::") + msg)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
