#!/usr/bin/env python3
"""Before you start a feature: find out what already exists.

  python3 scripts/find_existing.py vehicle seat
  python3 scripts/find_existing.py "tween position" sha256 --offline

Searches, for the keywords: the feature map (docs/FEATURE_MAP.md and docs/feature_map/*.md), docs/status/UNFINISHED.md and
docs/status/unfinished_engine.md, the titles of docs/**/*.md, class/component/node/function names under engine/ editor/ launcher/
shared/ okayui/ tests/ (git grep), example scenes, test names, and the open lane branches (claude/*, claude2/*, claude3/*: docs/lanes.json) (names and
the files they change against origin/main), plus open PR titles and Claim comments on issue #422 when GitHub answers.
Also: the generated code index (docs/engine/code_index.md, scripts/gen_code_index.py), the shared-code list
(docs/engine/shared_code.md) and existing names that are equal or nearly equal to the keywords joined into one name
(scripts/dup_lint.py; for example "json parse" also finds ParseJson and parse_json). See docs/process/no_duplicates.md.
It prints a ranked "Existing:" report. Paste the short "Existing:" block at the end into your Claim comment.
Standard library and git only. Nothing is changed. Written for this repo (plain regex and git grep).
"""
import argparse, glob, os, re, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import collab_lib as L

CODE_DIRS = ["engine", "editor", "launcher", "shared", "okayui", "player", "tests", "examples", "relay", "sandbox"]
STOP = {"the", "a", "an", "of", "and", "for", "to", "in", "on", "with", "node", "nodes", "system"}


def tokens_of(args):
    """'vehicle seat' / 'VehicleSeat' / 'vehicle_seat' -> ['vehicle','seat']. Keeps a single word like sha256 whole."""
    out = []
    for a in args:
        a = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", a)
        for t in re.split(r"[\s_\-/.,]+", a.lower()):
            if t and t not in STOP:
                out.append(t)
    return list(dict.fromkeys(out))


def stem(t):
    return t[:-1] if len(t) > 4 and t.endswith("s") and not t.endswith("ss") else t


def variants(tokens):
    """Spellings of the whole phrase for sha256-style words: sha256, sha-256, sha_256."""
    outs = set(tokens)
    for t in tokens:
        m = re.match(r"^([a-z]+)(\d+)$", t)
        if m:
            outs.add(m.group(1) + "[-_ ]?" + m.group(2))
    return outs


def tok_regex(t):
    m = re.match(r"^([a-z]+)(\d+)$", t)
    return (m.group(1) + "[-_ ]?" + m.group(2)) if m else re.escape(stem(t))


def all_in(text, toks):
    low = text.lower()
    return all(re.search(tok_regex(t), low) for t in toks)


def n_in(text, toks):
    low = text.lower()
    return sum(1 for t in toks if re.search(tok_regex(t), low))


# ---------------------------------------------------------------- doc sources

def feature_map(root, toks):
    res = []
    for p in sorted(glob.glob(os.path.join(root, "docs", "feature_map", "*.md"))):
        base = os.path.basename(p)
        if base.startswith("_") or base == "README.md":
            continue
        cur, buf = None, []
        def flush():
            if cur:
                text = "\n".join(buf)
                n = n_in(cur[1] + "\n" + text, toks)
                together = len(toks) == 1 or all_in(cur[1], toks) or any(all_in(l, toks) for l in buf)  # all words in one line
                if n and n == len(toks) and together:
                    f = {}
                    for line in buf:
                        m = re.match(r"^- \*\*([^:*]+):\*\*\s*(.*)$", line)
                        if m: f[m.group(1)] = m.group(2)
                    res.append({"score": 30 + 10 * n + (10 if all_in(cur[1], toks) else 0), "id": cur[0], "title": cur[1], "file": f"docs/feature_map/{base}",
                                "status": f.get("Status", ""), "code": f.get("Code", ""), "tests": f.get("Tests", ""), "what": f.get("What", "")})
        for line in open(p, encoding="utf-8", errors="replace"):
            line = line.rstrip("\n")
            m = re.match(r"^### (\d+\.\d+) (.+?)\s*$", line)
            if m:
                flush(); cur, buf = (m.group(1), m.group(2)), []
            elif re.match(r"^## ", line):
                flush(); cur, buf = None, []
            elif cur:
                buf.append(line)
        flush()
    return sorted(res, key=lambda r: -r["score"])


def unfinished(root, toks):
    res = []
    for f in ("docs/status/UNFINISHED.md", "docs/status/unfinished_engine.md"):
        p = os.path.join(root, f)
        if not os.path.exists(p): continue
        for i, line in enumerate(open(p, encoding="utf-8", errors="replace"), 1):
            if all_in(line, toks):
                res.append({"file": f, "line": i, "text": line.strip()[:220]})
    return res[:12]


def doc_titles(root, toks):
    res = []
    for p in sorted(glob.glob(os.path.join(root, "docs", "**", "*.md"), recursive=True)):
        rel = os.path.relpath(p, os.path.join(root, "docs")).replace(os.sep, "/")
        if rel.startswith(("feature_map/", "try_it/", "verification/")):
            continue  # searched as the feature map above, or generated / old per-entry files
        name = os.path.basename(p)
        title = ""
        with open(p, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if line.startswith("#"):
                    title = line.lstrip("# ").strip(); break
        if all_in(name.replace("_", " ") + " " + title, toks) or (len(toks) > 1 and n_in(name.replace("_", " "), toks) >= 1 and all_in(title, toks)):
            res.append({"file": "docs/" + rel, "title": title})
    return res[:12]


# ---------------------------------------------------------------- code

def code_search(root, toks, limit_lines=4000):
    """Identifier-like names containing all keywords (any order, an optional _ - or space between), grouped by name."""
    parts = [tok_regex(t) for t in toks]
    import itertools
    pats = []
    for perm in itertools.permutations(parts):
        pats.append("[A-Za-z0-9_]*" + "[A-Za-z0-9]{0,14}[ _-]?[A-Za-z0-9]{0,14}".join(perm) if len(perm) > 1 else perm[0])
        if len(pats) >= 6: break
    pat = "(" + "|".join(pats) + ")"
    out = L.run(["git", "-C", root, "grep", "-I", "-n", "-i", "-o", "-E", pat, "--", *[d for d in CODE_DIRS if os.path.isdir(os.path.join(root, d))]])
    idents = collections.OrderedDict()
    tests, examples = collections.Counter(), collections.Counter()
    for k, line in enumerate(out.splitlines()):
        if k > limit_lines: break
        m = re.match(r"^([^:]+):(\d+):(.*)$", line)
        if not m: continue
        f, ln, txt = m.group(1), int(m.group(2)), m.group(3)
        if f.startswith("tests/"):
            tests[f] += 1
        if f.startswith("examples/"):
            examples[f] += 1
        key = txt.lower()
        e = idents.setdefault(key, {"name": txt, "count": 0, "def": None, "files": set()})
        e["count"] += 1; e["files"].add(f)
        if e["def"] is None and (f.endswith((".hpp", ".h")) or f.endswith(".cpp")) and not f.startswith("tests/"):
            e["def"] = (f, ln)
    ranked = sorted(idents.values(), key=lambda e: (-(e["def"] is not None and e["def"][0].endswith((".hpp", ".h"))), -e["count"]))
    return ranked[:15], tests, examples


def declarations(root, toks):
    """class/struct/enum declarations whose name holds any keyword (for single-word searches)."""
    pat = r"\b(class|struct|enum( class)?)\s+[A-Za-z0-9_]*(" + "|".join(tok_regex(t) for t in toks) + r")[A-Za-z0-9_]*"
    out = L.run(["git", "-C", root, "grep", "-I", "-n", "-i", "-E", pat, "--", "engine/include", "editor", "launcher", "shared", "okayui"])
    res = []
    for line in out.splitlines()[:300]:
        m = re.match(r"^([^:]+):(\d+):\s*(.*)$", line)
        if m and not m.group(3).rstrip().endswith(";"):
            res.append((m.group(1), int(m.group(2)), m.group(3)[:100]))
    return res[:12]


def test_names(root, toks):
    p = os.path.join(root, "tests", "CMakeLists.txt")
    res = []
    if os.path.exists(p):
        txt = open(p, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"add_test\s*\(\s*NAME\s+([A-Za-z0-9_.-]+)\s+COMMAND\s+([A-Za-z0-9_.${}-]+)", txt):
            if all_in(m.group(1).replace("_", " ") + " " + m.group(2).replace("_", " "), toks):
                res.append(m.group(1))
    return res[:12]


# ---------------------------------------------------------------- branches and GitHub

def branch_hits(root, toks, days=21, merged_heads=(), pr_titles=None, base="origin/main"):
    cache = L.DiffCache(root, base)
    now = L.now_utc()
    res = []
    pr_titles = pr_titles or {}
    for b in L.branches(root):
        age = (now - b["date"]).days if b["date"] else 0
        if age > days or b["sha"] in merged_heads:
            continue
        name_n = n_in(b["name"].replace("-", " ").replace("_", " "), toks)
        light = None
        # cheap path first: only compute the diff for branches that could match by name, PR title, or are fresh enough
        light = cache.light("origin/" + b["name"], b["sha"])
        if not light["files"] or (light["merged_like"] and b["name"] not in pr_titles):
            continue
        path_n = max([n_in(f.replace("_", " "), toks) for f in light["files"]] + [0])
        hit_files = [f for f in light["files"] if n_in(f.replace("_", " "), toks) == len(toks)]
        title = pr_titles.get(b["name"], "")
        title_n = n_in(title, toks)
        score = (60 if name_n == len(toks) else 0) + 15 * name_n + (35 if hit_files else 0) + 6 * path_n + (30 if title_n == len(toks) else 8 * title_n)
        if score >= 15 and (name_n or hit_files or title_n):
            res.append({"branch": b["name"], "score": score, "files": hit_files[:5], "age_days": age, "pr": title, "lane": L.lane_of_branch(b["name"])})
    cache.save()
    return sorted(res, key=lambda r: -r["score"])[:12]


def github_bits(repo, toks):
    prs, claims, merged = {}, [], set()
    try:
        for p in L.gh_pages("/pulls?state=open", repo, 3):
            prs[p["head"]["ref"]] = f"#{p['number']} {p['title']}"
        for c in L.gh_pages(f"/issues/{L.CLAIM_ISSUE}/comments", repo, 5):
            for m in L.CLAIM_RE.finditer(c.get("body") or ""):
                if m.group(1).lower() == "claim":
                    seg = (c["body"][m.start():m.start() + 900])
                    if n_in(seg, toks) >= max(1, len(toks) if len(toks) < 3 else 2):
                        claims.append({"branch": L.norm_branch(m.group(2)), "url": c.get("html_url"), "text": " ".join(seg.split())[:200]})
        merged = {p["head"]["sha"] for p in L.gh_pages("/pulls?state=closed&sort=updated&direction=desc", repo, 12) if p.get("merged_at") and p.get("head")}
    except Exception:
        pass
    return prs, claims, merged


# ---------------------------------------------------------------- report

def code_index_hits(root, toks, limit=15):
    """Rows of the generated code index (docs/engine/code_index/*.md) whose name, doc line or path holds every keyword."""
    res = []
    for page in sorted(glob.glob(os.path.join(root, "docs", "engine", "code_index", "*.md"))):
        path = None
        for line in open(page, encoding="utf-8", errors="replace"):
            m = re.match(r"^## `([^`]+)`", line)
            if m:
                path = m.group(1); continue
            m = re.match(r"^- `([^`]+)` (\w+), line (\d+)(?:: (.*))?$", line.rstrip("\n"))
            if m and path and all_in(" ".join((re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", m.group(1)), m.group(4) or "", path)), toks):
                res.append({"file": path, "line": int(m.group(3)), "kind": m.group(2), "name": m.group(1), "doc": (m.group(4) or "")[:120]})
    return res[:limit]


def shared_code_hits(root, toks):
    """Rows of docs/engine/shared_code.md (the one file to use per job) that mention any keyword."""
    p = os.path.join(root, "docs", "engine", "shared_code.md")
    if not os.path.exists(p):
        return []
    rows = [l.strip() for l in open(p, encoding="utf-8") if l.startswith("|") and not l.startswith(("| Job", "|---"))]
    return [r[:220] for r in rows if n_in(r, toks)][:6]


def near_names(root, toks, limit=8):
    """Existing names equal or nearly equal to the keywords joined into one name (dup_lint's similar-name search)."""
    try:
        import dup_lint as D
    except ImportError:
        return []
    name = "".join(t[:1].upper() + t[1:] for t in toks)
    if len(D.name_words(name)) < 2:
        return []
    ni = D.NameIndex()
    files = D.read_tree(root, "HEAD")
    for p, rows in D.all_names(files, min(4, os.cpu_count() or 1)).items():
        for n_, ln in rows:
            ni.add(n_, p, ln)
    out = []
    for g in ("c:", "p:", "j:"):
        out += [{"file": p, "line": ln, "name": n_, "why": why} for p, ln, n_, why in ni.similar(name, "", g)]
    return out[:limit]


def build(root, words, offline=False, repo=L.REPO_DEFAULT, days=21):
    toks = tokens_of(words)
    prs, claims, merged = ({}, [], set()) if offline else github_bits(repo, toks)
    pr_match = {b: t for b, t in prs.items()}
    idents, tests, examples = code_search(root, toks)
    return {"keywords": toks, "feature_map": feature_map(root, toks)[:8], "unfinished": unfinished(root, toks), "docs": doc_titles(root, toks),
            "identifiers": idents, "declarations": declarations(root, toks) if len(toks) == 1 else [],
            "tests_files": tests.most_common(8), "test_names": test_names(root, toks), "examples": examples.most_common(8),
            "branches": branch_hits(root, toks, days, merged, pr_match), "open_prs": [(b, t) for b, t in prs.items() if n_in(b.replace("-", " ") + " " + t, toks) == len(toks)],
            "claims": claims, "offline": offline,
            "code_index": code_index_hits(root, toks), "shared_code": shared_code_hits(root, toks), "near_names": near_names(root, toks)}


def render(r):
    toks = " ".join(r["keywords"])
    o = [f"# Existing: {toks}", ""]
    o.append("## Feature map (docs/FEATURE_MAP.md and docs/feature_map/*.md)")
    if r["feature_map"]:
        for f in r["feature_map"]:
            o.append(f"- **{f['id']} {f['title']}** ({f['file']}) - {f['status'][:60] or 'status n/a'}")
            if f["what"]: o.append(f"  - What: {f['what'][:200]}")
            if f["code"]: o.append(f"  - Code: {f['code'][:200]}")
            if f["tests"]: o.append(f"  - Tests: {f['tests'][:200]}")
    else: o.append("- nothing in the map")
    o += ["", "## Open notes (docs/status/UNFINISHED.md, docs/status/unfinished_engine.md)"]
    o += [f"- {u['file']}:{u['line']}: {u['text']}" for u in r["unfinished"]] or ["- none"]
    o += ["", "## Docs with this in the name or title"]
    o += [f"- {d['file']}: {d['title']}" for d in r["docs"]] or ["- none"]
    o += ["", "## Names in the code (class, component, node, function)"]
    for e in r["identifiers"]:
        loc = f"{e['def'][0]}:{e['def'][1]}" if e["def"] else sorted(e["files"])[0]
        o.append(f"- `{e['name']}` x{e['count']} in {len(e['files'])} files, first {loc}")
    for f, ln, t in r["declarations"]:
        o.append(f"- {f}:{ln}: `{t}`")
    if not r["identifiers"] and not r["declarations"]: o.append("- no matching names")
    o += ["", "## Code index (docs/engine/code_index.md: public classes and functions)"]
    o += [f"- `{c['name']}` {c['kind']} {c['file']}:{c['line']}" + (f": {c['doc']}" if c["doc"] else "") for c in r.get("code_index", [])] or ["- none"]
    o += ["", "## Shared code list (docs/engine/shared_code.md: use or extend these, never paste)"]
    o += [f"- {row}" for row in r.get("shared_code", [])] or ["- no row for these words"]
    if r.get("near_names"):
        o += ["", "## Existing names that look like the one you would write (reuse them?)"]
        o += [f"- `{n['name']}` {n['file']}:{n['line']} ({n['why']})" for n in r["near_names"]]
    o += ["", "## Tests"]
    o += [f"- ctest `{t}`" for t in r["test_names"]] + [f"- {f} ({n} hits)" for f, n in r["tests_files"]] or ["- none"]
    o += ["", "## Example scenes and samples"]
    o += [f"- {f} ({n} hits)" for f, n in r["examples"]] or ["- none"]
    o += ["", "## Open branches that touch it (read these before you start; they may already be doing this)"]
    for b in r["branches"]:
        extra = (" files: " + ", ".join(b["files"][:3])) if b["files"] else ""
        o.append(f"- `{b['branch']}` ({b['lane']}, pushed {b['age_days']} d ago, score {b['score']}){' PR ' + b['pr'] if b['pr'] else ''}{extra}")
    if not r["branches"]: o.append("- none")
    if r["open_prs"]:
        o += ["", "## Open PRs that match"] + [f"- `{b}`: {t}" for b, t in r["open_prs"]]
    if r["claims"]:
        o += ["", "## Claims on issue #422 that mention it"] + [f"- `{c['branch']}` {c['url']}: {c['text']}" for c in r["claims"][:6]]
    if r["offline"]:
        o += ["", "(offline: open PR titles and claims were not checked)"]
    # paste-ready
    bits = []
    if r["feature_map"]: bits.append("map " + ", ".join(f"{f['id']} {f['title']}" for f in r["feature_map"][:3]))
    if r["identifiers"]: bits.append("code " + ", ".join(f"`{e['name']}`" for e in r["identifiers"][:4]))
    if r.get("near_names"): bits.append("similar names " + ", ".join(f"`{n['name']}`" for n in r["near_names"][:3]))
    if r["test_names"] or r["tests_files"]: bits.append("tests " + ", ".join(r["test_names"][:3] or [f for f, _ in r["tests_files"][:3]]))
    if r["branches"]: bits.append("branches " + ", ".join(f"`{b['branch']}`" for b in r["branches"][:4]))
    if r["claims"]: bits.append("claims " + ", ".join(f"`{c['branch']}`" for c in r["claims"][:3]))
    o += ["", "## Paste into your Claim", "", "Existing: " + ("; ".join(bits) if bits else "nothing found for " + toks + " (say what you searched)") + " / Gap: <what is missing>", ""]
    return "\n".join(o)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("keywords", nargs="+")
    ap.add_argument("--root", default=".")
    ap.add_argument("--offline", action="store_true", help="do not ask GitHub (no PR titles or claims)")
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", L.REPO_DEFAULT))
    ap.add_argument("--days", type=int, default=21, help="only branches pushed in the last N days")
    a = ap.parse_args()
    root = L.repo_root(a.root)
    print(render(build(root, a.keywords, a.offline, a.repo, a.days)))


if __name__ == "__main__":
    main()
