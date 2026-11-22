#!/usr/bin/env python3
"""PR hygiene: a PR body needs a 'How to try it' section (CLAUDE.md rule 4a) and an 'Existing code' section
(docs/process/no_duplicates.md), both with real content.

  PR_BODY="$BODY" python3 scripts/check_pr_body.py [--files a b c | --base origin/main --head HEAD]

Passes when the section exists and says something: steps, or for a change with nothing to run (docs only, tests only, CI)
'Nothing to run: read <file>' or 'n/a, <why>'. Fails (exit 1) when the section is missing, empty, only a template
comment, or just a dash. A docs-only change (every changed file under docs/ or a .md file) may use any non-empty text.

'Existing code' lists what the PR reuses or replaces (names and files), or says
'none: searched code_index.md for <words>' (which words you searched docs/engine/code_index.md for).
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


EXISTING_RULE_SINCE = "2026-11-23T00:00:00Z"   # the Existing code rule applies to PRs opened from this time on
EXISTING_RE = re.compile(r"^#{1,6}\s*Existing code\b.*$", re.I | re.M)


def check_existing(body, files=None):
    """The 'Existing code' section: what the PR reuses or replaces, or 'none: searched code_index.md for <words>'."""
    m = EXISTING_RE.search(body or "")
    if not m:
        return False, ("the PR body has no '## Existing code' section. List the existing code this PR reuses or replaces "
                       "(names and files), or write 'none: searched code_index.md for <words>' (docs/process/no_duplicates.md).")
    rest = body[m.end():]
    nxt = re.search(r"^#{1,6}\s+\S", rest, re.M)
    t = meaningful(rest[:nxt.start()] if nxt else rest)
    docs_only = bool(files) and all(f.startswith("docs/") or f.endswith(".md") for f in files)
    if not t:
        return False, "the 'Existing code' section is empty. Name what you reuse or replace, or 'none: searched code_index.md for <words>'."
    if docs_only:
        return True, "ok"
    if re.match(r"(?i)\s*(none|nothing|n/?a|-)\b", t):
        if not re.search(r"(?i)searched\b.*code_index(\.md)?\b.*\bfor\b\s*\S+", t):
            return False, ("'Existing code' says none. Say what you searched: 'none: searched code_index.md for <words>' "
                           "(python3 scripts/gen_code_index.py --find <words>).")
        return True, "ok"
    if len(re.findall(r"\w+", t)) < 2:
        return False, "the 'Existing code' section is too short. Name the functions, classes or files this PR reuses or replaces."
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
    # CLAUDE.md rule 4a (owner, 2026-11-17): "How to try it" sections are no longer required, so this is a note only.
    ok_try, msg = check(body, files)
    print(("OK: " if ok_try else "note (not required, rule 4a): ") + msg)
    ok = True
    ok2, msg2 = check_existing(body, files)
    # PRs opened before the rule existed get a warning, not a failure, so they are not blocked mid-flight.
    created = os.environ.get("PR_CREATED_AT", "")
    grandfathered = bool(created) and created < EXISTING_RULE_SINCE
    if ok2:
        print("OK: " + msg2)
    elif grandfathered:
        print("::warning title=PR needs an Existing code section::" + msg2 + " (PR opened before " + EXISTING_RULE_SINCE + ": warning only)")
    else:
        print("::error title=PR needs an Existing code section::" + msg2)
    return 0 if ok and (ok2 or grandfathered) else 1


if __name__ == "__main__":
    sys.exit(main())
