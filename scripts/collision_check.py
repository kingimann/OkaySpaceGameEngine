#!/usr/bin/env python3
"""Finds collisions between the open branches and PRs before they become merge conflicts or duplicate work.

For every claude/* and claude2/* branch (and every open PR) it takes the files changed against origin/main and reports:
  1. file overlaps between branches (cross-lane and shared-file ones are flagged)
  2. line-range overlaps in one file (the same lines or functions edited twice)
  3. branches with no claim and no PR; stale claims (no push for 24 hours); claims that break the claim rules
  4. branches that touch files outside their lane (docs/lanes.json)
  5. generated files edited without their source (by hand)
  6. two branches adding a test of the same name, the same test file or the same CMake target (or one already on main)
  7. branches far behind main
  8. copies between branches (scripts/dup_lint.py): a block of 80+ tokens that two open branches both add (exact copy,
     even in different files), and new class/function names in two branches that are equal or nearly equal
Output: Markdown (default) or JSON (--json). Standard library and git only.

  python3 scripts/collision_check.py                       use the local remote refs, fetch claims from issue #422
  python3 scripts/collision_check.py --branch claude2/x    only collisions that involve this branch (exit 1 on a cross-lane
                                                           or claim overlap, or an exact copy of another open
                                                           branch's new code, used by branch-lint)
  python3 scripts/collision_check.py --offline             no GitHub calls (no claims, no PR list)
  python3 scripts/collision_check.py --claims-file f.json --prs-file p.json   inputs for tests
"""
import argparse, json, os, re, subprocess, sys, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import collab_lib as L
import dup_lint as D

NEW_TEST_RE = re.compile(r"^\+\s*(?:add_test\s*\(\s*NAME\s+([A-Za-z0-9_.${}-]+)|add_executable\s*\(\s*([A-Za-z0-9_.${}-]+)|okay_add_test\s*\(\s*([A-Za-z0-9_.${}-]+))")
TEST_FILE_RE = re.compile(r"^tests/(test_[A-Za-z0-9_]+\.cpp)$")


def added_targets(root, ref, base="origin/main"):
    """(test names, executable targets) added by the branch's diff of CMake files."""
    names, targets = set(), set()
    out = L.git(root, "diff", "-U0", "--no-renames", f"{base}...{ref}", "--", "*CMakeLists.txt", "*.cmake")
    for line in out.splitlines():
        m = NEW_TEST_RE.match(line)
        if not m: continue
        if m.group(1) and "$" not in m.group(1): names.add(m.group(1))
        if m.group(2) and "$" not in m.group(2): targets.add(m.group(2))
        if m.group(3) and "$" not in m.group(3): targets.add(m.group(3))
    return names, targets


def main_targets(root, base="origin/main"):
    names, targets = set(), set()
    for f in L.git(root, "ls-tree", "-r", "--name-only", base).splitlines():
        if f.endswith("CMakeLists.txt") and (f == "tests/CMakeLists.txt" or f.count("/") <= 1):
            txt = L.git(root, "show", f"{base}:{f}")
            names |= set(re.findall(r"add_test\s*\(\s*NAME\s+([A-Za-z0-9_.-]+)", txt))
            targets |= set(re.findall(r"add_executable\s*\(\s*([A-Za-z0-9_.-]+)", txt))
    return names, targets


_anc = {}


def is_stacked(root, A, B):
    """True when one branch tip is an ancestor of the other (a stack, not a collision)."""
    k = (A["sha"], B["sha"])
    if k not in _anc:
        _anc[k] = any(subprocess.run(["git", "-C", root, "merge-base", "--is-ancestor", x["sha"], y["sha"]],
                                     stderr=subprocess.DEVNULL).returncode == 0 for x, y in ((A, B), (B, A)))
    return _anc[k]


def ranges_overlap(ra, rb):
    for a in ra:
        for b in rb:
            if a[0] <= b[1] and b[0] <= a[1]:
                return (max(a[0], b[0]), min(a[1], b[1]))
    return None


def build_report(root, claims_comments=None, prs=None, base="origin/main", behind_limit=150, max_age_days=3,
                 only_branch=None, now=None, use_disk_cache=True, extra_branches=None, merged_heads=(), copies=True):
    now = now or L.now_utc()
    lanes = L.load_lanes(root)
    cache = L.DiffCache(root, base, use_disk_cache)
    all_files = set(L.git(root, "ls-tree", "-r", "--name-only", base).splitlines())
    claims, events = L.parse_claims(claims_comments or [])
    pr_branches = {p["head"]["ref"]: p for p in (prs or []) if p.get("head")}
    brs = L.branches(root)
    if extra_branches:
        brs += extra_branches
    info, live = {}, {}
    dates = {b["name"]: b["date"] for b in brs}
    focus_files = None
    if only_branch:  # quick mode (branch-lint): full diff only for the pushing branch and for branches sharing a file with it
        fb = [b for b in brs if b["name"] == only_branch]
        focus_files = set(cache.light("origin/" + only_branch, fb[0]["sha"])["files"]) if fb else set()
    for b in brs:  # live branches for the copy check: recent (or with a PR or claim), not merged
        age = (now - b["date"]).days if b["date"] else None
        if (age is not None and age > max_age_days and b["name"] not in pr_branches and b["name"] not in claims) or b["sha"] in merged_heads:
            continue
        lf = cache.light("origin/" + b["name"], b["sha"])
        if lf["files"] and not (lf["merged_like"] and b["name"] not in pr_branches):
            live[b["name"]] = b
    for b in brs:
        if focus_files is not None and b["name"] != only_branch:
            if (now - b["date"]).days > max_age_days if b["date"] else False:
                if b["name"] not in pr_branches and b["name"] not in claims: continue
            if b["sha"] in merged_heads: continue
            lf = cache.light("origin/" + b["name"], b["sha"])
            if lf["merged_like"] or not (set(lf["files"]) & focus_files): continue
        ref = "origin/" + b["name"]
        age = (now - b["date"]).days if b["date"] else None
        if age is not None and age > max_age_days and b["name"] not in pr_branches and b["name"] not in claims:
            continue
        if b["sha"] in merged_heads:
            continue  # its PR was merged
        if L.git(root, "rev-list", "--count", f"{base}..{ref}").strip() in ("", "0"):
            continue  # already merged into main
        f = cache.facts(ref, b["sha"])
        if not f["files"] or (f["merged_like"] and b["name"] not in pr_branches):
            continue
        info[b["name"]] = dict(b, ref=ref, lane=L.lane_of_branch(b["name"]), age_days=age, **f)
    cache.save()
    rep = {"base": base, "generated": now.strftime("%Y-%m-%d %H:%M UTC"), "branches": len(info), "overlaps": [], "shared_files": [],
           "unclaimed": [], "claims": [], "out_of_lane": [], "generated_by_hand": [], "duplicate_tests": [],
           "behind": [], "claim_overlaps": [], "claim_conflicts": [], "cross_clones": [], "cross_names": []}

    names = sorted(info)
    focus = only_branch
    # 1/2. overlaps
    shared_hits = {}
    for a, b in itertools.combinations(names, 2):
        if focus and focus not in (a, b): continue
        A, B = info[a], info[b]
        if is_stacked(root, A, B):
            continue  # one branch was built on the other: they merge in order, no conflict
        common = sorted(set(A["files"]) & set(B["files"]))
        if not common: continue
        plain, rng = [], []
        for f in common:
            o = L.owner_of(f, lanes)
            if o == "shared" or L.generated_entry(f, lanes):
                shared_hits.setdefault(f, set()).update((a, b))
                continue
            plain.append(f)
            r = ranges_overlap(A["hunks"].get(f, []), B["hunks"].get(f, []))
            if r and A["files"][f] == "M" and B["files"][f] == "M":
                rng.append((f, r[0], r[1], A["merge_base"] != B["merge_base"]))
        code_rng = [x for x in rng if not x[0].endswith((".md", ".txt"))]
        if not plain and not rng: continue
        cross = bool(A["lane"] and B["lane"] and A["lane"] != B["lane"])
        has_code = any(not f.endswith((".md", ".txt", ".tsv")) for f in plain)
        rep["overlaps"].append({"a": a, "b": b, "files": plain, "line_overlaps": [{"file": f, "from": s, "to": e, "approx": ap} for f, s, e, ap in rng],
                                "cross_lane": cross, "severity": "high" if (code_rng or (cross and has_code)) else "medium"})
    rep["overlaps"].sort(key=lambda o: (o["severity"] != "high", -len(o["line_overlaps"]), -len(o["files"])))
    for f, bs in sorted(shared_hits.items(), key=lambda kv: -len(kv[1])):
        if focus and focus not in bs: continue
        rep["shared_files"].append({"file": f, "branches": sorted(bs), "generated": bool(L.generated_entry(f, lanes))})

    # 3. claims and branches without one
    active = {}
    for br, c in claims.items():
        i = info.get(br)
        last = dates.get(br)
        st = L.claim_status(c, last, now)
        c["status"] = st
        if st == "active": active[br] = c
    for br, c in claims.items():
        others = [o for k, o in active.items() if k != br]
        probs = L.check_claim_rules(c, lanes, others, all_files) if c["status"] == "active" else []
        if c.get("has_fields") is False and c["status"] == "active":
            pass
        rep["claims"].append({"branch": br, "lane": L.lane_of_branch(br), "status": c["status"], "files": c.get("files", []), "url": c.get("url"),
                              "created_at": c.get("created_at"), "agreed": c.get("agreed"), "problems": probs,
                              "has_branch": br in info, "has_pr": br in pr_branches})
        for p in probs:
            if "overlaps" in p:
                rep["claim_overlaps"].append({"branch": br, "problem": p})
    # a branch that changes files another branch's active claim covers
    for n in names:
        if focus and n != focus: continue
        i = info[n]
        mine = claims.get(n)
        for br, c in active.items():
            if br == n or not c.get("files"): continue
            # files every branch edits (lanes.json owner "shared", or listed in "claim_exempt") never need an agreement
            hit = sorted({f for f in i["files"] for g in c["files"] if L.glob_match(f, g) and L.owner_of(f, lanes) != "shared" and not L.generated_entry(f, lanes) and f not in lanes.get("claim_exempt", [])})
            if hit:
                cross = bool(i["lane"] and L.lane_of_branch(br) and i["lane"] != L.lane_of_branch(br))
                if mine and mine.get("agreed"): cross = False
                rep["claim_conflicts"].append({"branch": n, "claim": br, "files": hit[:8], "count": len(hit), "cross_lane": cross, "url": c.get("url")})
    for n in names:
        i = info[n]
        if focus and n != focus: continue
        if n not in claims and n not in pr_branches and (i["age_days"] is None or i["age_days"] <= 7):
            rep["unclaimed"].append({"branch": n, "files": len(i["files"]), "age_days": i["age_days"]})

    # 4. out of lane, 5. generated by hand, 7. behind
    for n in names:
        if focus and n != focus: continue
        i = info[n]
        if i["lane"]:
            bad = [f for f in i["files"] if not f.startswith("docs/") and L.owner_of(f, lanes) in ("claude2", "coordinator") and L.owner_of(f, lanes) != i["lane"]]
            if bad:
                rep["out_of_lane"].append({"branch": n, "lane": i["lane"], "files": bad[:12], "count": len(bad)})
        for f in i["files"]:
            g = L.generated_entry(f, lanes)
            if g and not any(L.glob_match(s, sg) for s in i["files"] for sg in g["sources"]):
                rep["generated_by_hand"].append({"branch": n, "file": f, "regen": g["regen"]})
        if i["behind"] > behind_limit:
            rep["behind"].append({"branch": n, "behind": i["behind"], "age_days": i["age_days"]})

    # 6. duplicate tests
    mnames, mtargets = main_targets(root, base)
    seen_names, seen_targets, seen_files = {}, {}, {}
    for n in names:
        i = info[n]
        tn, tt = added_targets(root, i["ref"], base)
        for t in tn: seen_names.setdefault(("test", t), []).append(n)
        for t in tt: seen_targets.setdefault(("target", t), []).append(n)
        for f, st in i["files"].items():
            if st == "A" and TEST_FILE_RE.match(f): seen_files.setdefault(("file", f), []).append(n)
        for t in tn & mnames: seen_names.setdefault(("test", t), []).append("main")
        for t in tt & mtargets: seen_targets.setdefault(("target", t), []).append("main")
    for d in (seen_names, seen_targets, seen_files):
        for (kind, name), bs in d.items():
            if len(set(bs)) > 1 and (not focus or focus in bs):
                rep["duplicate_tests"].append({"kind": kind, "name": name, "branches": sorted(set(bs))})

    # 8. copies between open branches
    if copies and len(live) > 1 and (not focus or focus in live):
        refs = {n: "origin/" + n for n in live}
        st = lambda a, b: is_stacked(root, live[a], live[b])
        rep.update({"cross_" + k: v for k, v in D.cross_branch(root, base, refs, focus, jobs=min(4, os.cpu_count() or 1), stacked=st).items()})
    return rep


def fails_for_branch(rep, branch):
    """Reasons branch-lint fails a push: the branch changes files covered by an open claim of the OTHER lane."""
    out = []
    for c in rep["claim_conflicts"]:
        if c["branch"] == branch and c["cross_lane"]:
            out.append(f"{branch} changes {', '.join(c['files'][:4])} ({c['count']} files) which the open claim of {c['claim']} (other lane) covers. "
                       f"Ask on issue #422 for 'Agree: {branch}', or wait for 'Release: {c['claim']}'.")
    for c in rep.get("cross_clones", []):
        if branch in (c["a"], c["b"]):
            me, other = ("a", "b") if c["a"] == branch else ("b", "a")
            out.append(f"{c[me + '_file']}:{c[me + '_lines'][0]}-{c[me + '_lines'][1]} is the same code ({c['tokens']} tokens) as "
                       f"{c[other + '_file']}:{c[other + '_lines'][0]}-{c[other + '_lines'][1]} on the open branch {c[other]}. "
                       f"Agree on issue #422 which branch owns it; the other builds on it (docs/process/no_duplicates.md).")
    return out


def warnings_for_branch(rep, branch):
    """Warnings for branch-lint: new names that are equal or nearly equal to a new name on another open branch."""
    out = []
    for n in rep.get("cross_names", []):
        if branch in (n["a"], n["b"]):
            me, other = ("a", "b") if n["a"] == branch else ("b", "a")
            out.append(f"new name {n[me + '_name']} ({n[me + '_where']}) looks like {n[other + '_name']} ({n[other + '_where']}) on the open branch "
                       f"{n[other]} ({n['why']}). Are you two building the same thing? Talk on issue #422.")
    return out


def short(f):
    return "/".join(f.split("/")[-2:])


def to_markdown(rep, brief=False, title_level=2):
    h = "#" * title_level
    out = []
    n_high = sum(1 for o in rep["overlaps"] if o["severity"] == "high")
    out += [f"{h} Collision report", "",
            f"{rep['branches']} claude/* and claude2/* branches with changes against `{rep['base']}`. "
            f"{len(rep['overlaps'])} branch pairs touch the same files ({n_high} serious). Generated by `scripts/collision_check.py`; "
            "plain rules in `docs/process/collaboration.md`.", ""]
    ov = rep["overlaps"][:12 if brief else 60]
    if ov:
        out += [f"{h}# File and line overlaps between branches", "", "| Branches | Same files (not shared files) | Same lines | Lanes |", "|---|---|---|---|"]
        for o in ov:
            ranges = "; ".join(f"`{short(x['file'])}` {x['from']}-{x['to']}{' (approx)' if x['approx'] else ''}" for x in o["line_overlaps"][:3]) or "-"
            files = ", ".join(f"`{short(f)}`" for f in o["files"][:4]) + (f" +{len(o['files']) - 4}" if len(o["files"]) > 4 else "")
            out.append(f"| `{o['a']}` + `{o['b']}` | {files} | {ranges} | {'CROSS-LANE' if o['cross_lane'] else 'same lane'} |")
        if len(rep["overlaps"]) > len(ov): out.append(f"\n... and {len(rep['overlaps']) - len(ov)} more pairs (run the script for all).")
        out.append("")
    if rep["shared_files"]:
        out += [f"{h}# Shared and generated files touched by several branches (handshake needed)", "", "| File | Branches |", "|---|---|"]
        for s in rep["shared_files"][:10 if brief else 40]:
            out.append(f"| `{s['file']}`{' (generated)' if s['generated'] else ''} | {len(s['branches'])}: " + ", ".join(f"`{b}`" for b in s["branches"][:5]) + (" ..." if len(s["branches"]) > 5 else "") + " |")
        out.append("")
    if rep["duplicate_tests"]:
        out += [f"{h}# Same test name, test file or CMake target in more than one place", ""]
        for d in rep["duplicate_tests"][:15]:
            out.append(f"- {d['kind']} `{d['name']}`: " + ", ".join(f"`{b}`" for b in d["branches"]))
        out.append("")
    if rep.get("cross_clones"):
        out += [f"{h}# The same new code on two open branches (exact copies of 80+ tokens)", ""]
        for c in rep["cross_clones"][:20]:
            out.append(f"- `{c['a']}` `{c['a_file']}:{c['a_lines'][0]}-{c['a_lines'][1]}` = `{c['b']}` `{c['b_file']}:{c['b_lines'][0]}-{c['b_lines'][1]}` ({c['tokens']} tokens)")
        out.append("")
    if rep.get("cross_names"):
        out += [f"{h}# Similar new names on two open branches", ""]
        for n in rep["cross_names"][:20 if brief else 60]:
            out.append(f"- `{n['a']}` {n['a_name']} (`{n['a_where']}`) ~ `{n['b']}` {n['b_name']} (`{n['b_where']}`): {n['why']}")
        out.append("")
    if rep["claim_conflicts"]:
        out += [f"{h}# Branches changing files that another branch's open claim covers", ""]
        for c in rep["claim_conflicts"][:15]:
            out.append(f"- `{c['branch']}` changes {', '.join('`' + short(f) + '`' for f in c['files'][:3])} covered by the claim of `{c['claim']}`" + (" (**other lane**)" if c["cross_lane"] else " (same lane)"))
        out.append("")
    if rep["out_of_lane"]:
        out += [f"{h}# Branches touching files outside their lane", ""]
        for o in rep["out_of_lane"][:15]:
            out.append(f"- `{o['branch']}` ({o['lane']}): {o['count']} files, e.g. " + ", ".join(f"`{f}`" for f in o["files"][:3]))
        out.append("")
    if rep["generated_by_hand"]:
        out += [f"{h}# Generated files changed without their source (edited by hand?)", ""]
        for g in rep["generated_by_hand"][:15]:
            out.append(f"- `{g['branch']}` changed `{g['file']}` only; regenerate with `{g['regen']}`")
        out.append("")
    if rep["unclaimed"]:
        out += [f"{h}# Branches with no claim and no PR (pushed in the last 7 days)", ""]
        out.append(", ".join(f"`{u['branch']}`" for u in rep["unclaimed"][:30]) + (f" ... (+{len(rep['unclaimed']) - 30})" if len(rep["unclaimed"]) > 30 else ""))
        out.append("")
    if rep["behind"] and not brief:
        out += [f"{h}# Branches far behind main", ""]
        for b in rep["behind"][:20]:
            out.append(f"- `{b['branch']}`: {b['behind']} commits behind, last push {b['age_days']} d ago")
        out.append("")
    return "\n".join(out) + "\n"


def claims_markdown(rep, title_level=2):
    h = "#" * title_level
    out = [f"{h} Claims (issue #{L.CLAIM_ISSUE})", ""]
    if not rep["claims"]:
        return "\n".join(out + ["No `Claim:` comments found (or GitHub was not reachable).", ""]) + "\n"
    out += ["| Branch | Lane | Status | Files | Problems |", "|---|---|---|---|---|"]
    for c in sorted(rep["claims"], key=lambda c: (c["status"] != "active", c["branch"])):
        link = f"[`{c['branch']}`]({c['url']})" if c.get("url") else f"`{c['branch']}`"
        files = ", ".join(f"`{f}`" for f in c["files"][:3]) or "-"
        st = c["status"] + ("" if c["has_branch"] else ", no branch pushed yet")
        out.append(f"| {link} | {c['lane'] or '?'} | {st} | {files} | {'; '.join(c['problems'])[:140] or '-'} |")
    out.append("")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=".")
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", L.REPO_DEFAULT))
    ap.add_argument("--branch")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--offline", action="store_true")
    ap.add_argument("--claims-file"); ap.add_argument("--prs-file")
    ap.add_argument("--behind", type=int, default=150)
    ap.add_argument("--max-age-days", type=int, default=3, help="branches without an open PR or claim and older than this are not live work")
    ap.add_argument("--brief", action="store_true")
    a = ap.parse_args()
    root = L.repo_root(a.root)
    comments, prs, merged = [], [], set()
    if a.claims_file: comments = json.load(open(a.claims_file))
    elif not a.offline: comments = L.gh_pages(f"/issues/{L.CLAIM_ISSUE}/comments", a.repo)
    if a.prs_file: prs = json.load(open(a.prs_file))
    elif not a.offline:
        try: prs = L.gh_pages("/pulls?state=open", a.repo, 3)
        except Exception: prs = []
        try:
            merged = {p["head"]["sha"] for p in L.gh_pages("/pulls?state=closed&sort=updated&direction=desc", a.repo, 12) if p.get("merged_at") and p.get("head")}
        except Exception: merged = set()
    br = a.branch[len("origin/"):] if a.branch and a.branch.startswith("origin/") else a.branch
    rep = build_report(root, comments, prs, a.base, a.behind, a.max_age_days, br, merged_heads=merged)
    if a.json: print(json.dumps(rep, indent=1, default=str))
    else:
        sys.stdout.write(to_markdown(rep, a.brief) + "\n" + claims_markdown(rep))
    if br:
        fails = fails_for_branch(rep, br)
        for f in fails: print(f"::error::collision: {f}", file=sys.stderr)
        for w in warnings_for_branch(rep, br): print(f"::warning::similar name on another branch: {w}", file=sys.stderr)
        return 1 if fails else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
