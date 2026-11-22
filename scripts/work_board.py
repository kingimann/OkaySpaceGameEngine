#!/usr/bin/env python3
"""Writes the live work board (Markdown) from GitHub data: open PRs with their CI state, recently merged PRs, the
branches of every lane (claude/*, claude2/*, claude3/*: the prefixes in docs/lanes.json) with their last commit, and the latest
comments on the coordination issue #422.

Standard library only. Uses GITHUB_TOKEN from the environment when present (a higher rate limit), otherwise works
unauthenticated for a few calls. Usage: python3 scripts/work_board.py [--repo owner/name] [--out file]

The agent list and the owner's open decisions are not on GitHub: they live in docs/status/work_board.md (rewritten by hand)
and are only linked from here. Everything below the header is generated, never edit it by hand.
"""
import argparse, datetime, json, os, sys, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

def api(path, repo):
    url = f"https://api.github.com/repos/{repo}{path}"
    req = urllib.request.Request(url, headers={"User-Agent": "work-board", "Accept": "application/vnd.github+json"})
    tok = os.environ.get("GITHUB_TOKEN")
    if tok:
        req.add_header("Authorization", f"Bearer {tok}")
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)

def ago(iso):
    if not iso:
        return "?"
    t = datetime.datetime.strptime(iso, "%Y-%m-%dT%H:%M:%SZ")
    m = int((datetime.datetime.utcnow() - t).total_seconds() // 60)
    if m < 1: return "just now"
    if m < 90: return f"{m} min ago"
    if m < 60 * 36: return f"{m // 60} h ago"
    return f"{m // 1440} d ago"

def ci_summary(repo, sha):
    runs = api(f"/commits/{sha}/check-runs?per_page=100", repo)["check_runs"]
    counts = {"green": 0, "running": 0, "queued": 0, "red": 0}
    red = []
    for c in runs:
        if c["status"] == "queued": counts["queued"] += 1
        elif c["status"] != "completed": counts["running"] += 1
        elif c["conclusion"] in ("success", "skipped", "neutral"): counts["green"] += 1
        else:
            counts["red"] += 1; red.append(c["name"])
    return counts, red

def esc(s):
    return (s or "").replace("|", "\\|").replace("\n", " ")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", "kingimann/OkaySpaceGameEngine"))
    ap.add_argument("--out", default="-")
    a = ap.parse_args()
    repo = a.repo
    now = datetime.datetime.utcnow().strftime("%Y-%m-%d %H:%M UTC")
    out = [f"# Live work board", "",
           f"Generated {now} by `scripts/work_board.py` (a scheduled workflow rewrites this about every 10 minutes). "
           "Rule 6 (`CLAUDE.md`): a PR merges only when every CI job is finished and green.", "",
           "Hand-written parts (agents, owner decisions): [docs/status/work_board.md](https://github.com/%s/blob/main/docs/status/work_board.md)." % repo, ""]
    prs = api("/pulls?state=open&per_page=50&sort=created&direction=asc", repo)
    out += [f"## Open pull requests ({len(prs)})", "", "| PR | Title | State | CI | Updated |", "|---|---|---|---|---|"]
    for p in prs:
        try:
            c, red = ci_summary(repo, p["head"]["sha"])
        except Exception as e:
            c, red = None, []
        if c is None: ci = "unknown"
        else:
            ci = f"{c['green']} green, {c['running']} running, {c['queued']} queued, {c['red']} red"
            if red: ci += " (" + ", ".join(red[:3]) + ")"
        verdict = "red" if c and c["red"] else ("ready to merge" if c and not c["running"] and not c["queued"] and c["green"] else "waiting")
        state = ("draft, " if p.get("draft") else "") + verdict
        out.append(f"| [#{p['number']}]({p['html_url']}) | {esc(p['title'])[:90]} | {state} | {ci} | {ago(p['updated_at'])} |")
    out.append("")
    merged = [p for p in api("/pulls?state=closed&per_page=15&sort=updated&direction=desc", repo) if p.get("merged_at")][:8]
    out += ["## Recently merged", "", "| PR | Title | Merged |", "|---|---|---|"]
    for p in merged:
        out.append(f"| [#{p['number']}]({p['html_url']}) | {esc(p['title'])[:90]} | {ago(p['merged_at'])} |")
    out.append("")
    import collab_lib as L
    lanes = L.default_lanes()   # docs/lanes.json of this checkout
    prefixes = L.lane_prefixes(lanes)
    rows = []
    # The workflow checks out the full history, so the branch dates and subjects come from git (no API call per branch).
    root = L.repo_root(os.path.dirname(os.path.abspath(__file__)))
    fmt = "%(refname:short)\t%(objectname:short=7)\t%(committerdate:iso-strict)\t%(contents:subject)"
    for line in L.git(root, "for-each-ref", f"--format={fmt}", "refs/remotes/origin").splitlines():
        parts = line.split("\t", 3)
        if len(parts) < 4: continue
        n = parts[0][len("origin/"):] if parts[0].startswith("origin/") else parts[0]
        if L.lane_of_branch(n, lanes):
            d = parts[2]
            try:
                d = datetime.datetime.fromisoformat(d).astimezone(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
            except ValueError:
                d = ""
            rows.append((d, n, parts[3], parts[1]))
    if not rows:   # no remote refs here: ask GitHub (every page; it lists branches by name, so claude3/* comes last)
        for b in L.gh_pages("/branches", repo, 15):
            if not L.lane_of_branch(b["name"], lanes): continue
            try:
                cm = api(f"/commits/{b['commit']['sha']}", repo)
                rows.append((cm["commit"]["committer"]["date"], b["name"], cm["commit"]["message"].splitlines()[0], b["commit"]["sha"][:7]))
            except Exception:
                rows.append(("", b["name"], "", b["commit"]["sha"][:7]))
    rows.sort(reverse=True)
    who = ", ".join(f"`{p}*` ({L.lane_display(k, lanes)})" for k, p in prefixes.items())
    out += [f"## Branches of every lane: {who}, newest first", "",
            "| Lane | Count |", "|---|---|"]
    for k, p in prefixes.items():
        out.append(f"| {L.lane_display(k, lanes)} (`{p}*`) | {sum(1 for r in rows if L.lane_of_branch(r[1], lanes) == k)} |")
    out += ["", "| Branch | Lane | Last commit | Head | When |", "|---|---|---|---|---|"]
    for d, n, m, s in rows[:40]:
        out.append(f"| `{n}` | {L.lane_display(L.lane_of_branch(n, lanes), lanes)} | {esc(m)[:80]} | `{s}` | {ago(d)} |")
    out.append("")
    try:
        cs = api("/issues/422/comments?per_page=100", repo)
        cs.sort(key=lambda x: x["created_at"], reverse=True)
        out += ["## Latest on the coordination thread (#422)", ""]
        for c in cs[:5]:
            first = next((l for l in c["body"].splitlines() if l.strip()), "").lstrip("# ").strip()
            out.append(f"- {ago(c['created_at'])}: [{esc(first)[:110]}]({c['html_url']})")
        out.append("")
    except Exception:
        pass
    # Claims table and collision report (scripts/collision_check.py, docs/process/collaboration.md). Needs a full git history with the
    # remote branches; when that is missing the board still gets written, with a one-line note instead.
    try:
        import collab_lib as L, collision_check as CC
        comments = L.gh_pages("/issues/422/comments", repo, 10)
        pr_list = L.gh_pages("/pulls?state=open", repo, 3)
        merged = {p["head"]["sha"] for p in L.gh_pages("/pulls?state=closed&sort=updated&direction=desc", repo, 12) if p.get("merged_at") and p.get("head")}
        root = L.repo_root(os.path.dirname(os.path.abspath(__file__)))
        rep = CC.build_report(root, comments, pr_list, merged_heads=merged, use_disk_cache=False)
        out += [CC.claims_markdown(rep).rstrip(), "", CC.to_markdown(rep, brief=True).rstrip(), ""]
    except Exception as e:
        out += ["## Claims and collisions", "", f"(not available this run: {type(e).__name__}: {str(e)[:120]})", ""]
    text = "\n".join(out) + "\n"
    if a.out == "-": sys.stdout.write(text)
    else: open(a.out, "w", encoding="utf-8").write(text)

if __name__ == "__main__":
    main()
