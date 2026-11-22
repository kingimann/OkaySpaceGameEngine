#!/usr/bin/env python3
"""Builds an agent brief from docs/process/agent_brief_template.md with the CURRENT rules pasted in full.

  python3 scripts/make_brief.py --goal "..." --lane claude3 --branch claude3/x --files "engine/src/UI/**" \
      [--keywords "tween position"] [--read docs/a.md ...] [--test-first name] [--with-branch origin/claude/owner-rule2]
  python3 scripts/make_brief.py --rules-only [--with-branch B ...]      just the rules text

--lane is any lane in docs/lanes.json (coordinator, claude2, claude3); --branch defaults to that lane's prefix.
The rules are `CLAUDE.md` from origin/main (so a stale checkout does not matter). Each --with-branch adds the rules that exist
in that branch's CLAUDE.md but not on main yet (rules PRs that are not merged), so an agent never starts without them.
Standard library and git only.
"""
import argparse, datetime, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import collab_lib as L

RULE_RE = re.compile(r"^(\d+[a-z]?)\. ", re.M)


def rule_blocks(text):
    """{'4a': 'full text of the rule', ...} in order."""
    ms = list(RULE_RE.finditer(text))
    out = {}
    for i, m in enumerate(ms):
        end = ms[i + 1].start() if i + 1 < len(ms) else len(text)
        block = text[m.start():end]
        block = re.split(r"\n## ", block)[0]  # stop at the next section heading
        out[m.group(1)] = block.rstrip() + "\n"
    return out


def rules_text(root, base="origin/main", extra_branches=()):
    main = L.git(root, "show", f"{base}:CLAUDE.md") or open(os.path.join(root, "CLAUDE.md"), encoding="utf-8").read()
    parts = [main.rstrip() + "\n"]
    have = rule_blocks(main)
    for b in extra_branches:
        txt = L.git(root, "show", f"{b}:CLAUDE.md")
        if not txt:
            parts.append(f"\n(could not read CLAUDE.md from {b})\n"); continue
        new = [(k, v) for k, v in rule_blocks(txt).items() if k not in have or have[k] != v]
        if new:
            parts.append(f"\n### Rules from {b} that are not merged into main yet (they apply now)\n\n" + "\n".join(v for _, v in new))
            for k, v in new: have[k] = v
    return "\n".join(parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    known = L.lane_names()   # the lanes of docs/lanes.json in this checkout
    ap.add_argument("--goal", default="<goal>"); ap.add_argument("--lane", default="claude2", choices=known)
    ap.add_argument("--branch", default=None, help="default: the lane's prefix + <name>"); ap.add_argument("--files", default="<globs>")
    ap.add_argument("--keywords", default=None); ap.add_argument("--test-first", default="<test name>")
    ap.add_argument("--read", nargs="*", default=[]); ap.add_argument("--offlimits", default=None)
    ap.add_argument("--with-branch", action="append", default=[]); ap.add_argument("--rules-only", action="store_true")
    ap.add_argument("--root", default="."); ap.add_argument("--base", default="origin/main")
    a = ap.parse_args()
    root = L.repo_root(a.root)
    rules = rules_text(root, a.base, a.with_branch)
    if a.rules_only:
        print(rules); return 0
    tpl = open(os.path.join(root, "docs", "process", "agent_brief_template.md"), encoding="utf-8").read()
    tpl = tpl.split("<!-- BRIEF START: everything from here down is sent to the agent -->", 1)[-1].lstrip("\n")
    lanes = L.load_lanes(root)
    if a.lane not in L.lane_names(lanes):
        ap.error(f"--lane {a.lane} is not in {root}/docs/lanes.json")
    prefix = L.lane_prefixes(lanes)[a.lane]
    branch = a.branch or prefix + "<name>"
    name = L.lane_display(a.lane, lanes)
    who = (f"an agent of the coordinator (branches `{prefix}*`)" if a.lane == "coordinator"
           else f"{name} (branches `{prefix}*`, issue #{L.CLAIM_ISSUE})")
    # off limits: a few globs of every other lane (the full list is docs/lanes.json)
    # (a glob that contains this lane's own paths, such as engine/src/Platform/** around Platform/Steam, is left out)
    mine = [g for r in lanes["rules"] if r["owner"] == a.lane for g in L.rule_globs(r)]
    own = []
    for other in L.lane_names(lanes):
        if other == a.lane: continue
        theirs = [g for r in lanes["rules"] if r["owner"] == other for g in L.rule_globs(r)]
        own += [g for g in theirs if not any(L.globs_overlap(g, m) for m in mine)][:8]
    vals = {"GOAL": a.goal, "WHO": who, "BRANCH": branch, "KEYWORDS": a.keywords or a.goal, "FILES": a.files, "TESTFIRST": a.test_first,
            "READ": "\n".join(f"- `{p}`" for p in a.read + ["CLAUDE.md", "docs/process/collaboration.md", "docs/lanes.json", "docs/process/review_log.md", "docs/FEATURE_MAP.md"]),
            "OFFLIMITS": a.offlimits or ", ".join(f"`{g}`" for g in own) + " ...",
            "DATE": datetime.date.today().isoformat(), "RULES": rules}
    for k, v in vals.items():
        tpl = tpl.replace("{{" + k + "}}", v)
    tpl = tpl.replace("<test name>", a.test_first) if a.test_first != "<test name>" else tpl
    sys.stdout.write(tpl)
    return 0


if __name__ == "__main__":
    sys.exit(main())
