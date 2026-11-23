#!/usr/bin/env python3
"""Integration check: do all the open pull requests work TOGETHER, not just each one alone on main?

Every PR is tested against main on its own. Two PRs that are each green can still break each other once both are in
(two branches change the same function, add the same helper twice, or one renames what the other calls). This script
builds the "everything merged" tree so CI can build and test it, and reports which PRs clash.

  python3 scripts/integration_merge.py --prs 1130 1132 ...      merge these PR heads onto the current HEAD (main)
  python3 scripts/integration_merge.py --open [--drafts]        merge every open PR (GITHUB_TOKEN, GITHUB_REPOSITORY)
  python3 scripts/integration_merge.py --self-test              tests on a throw-away git repository

What it does, in PR-number order (oldest first, the order they would normally merge):
  1. fetch refs/pull/<n>/head;
  2. try to merge it onto the integration tree; a PR that conflicts is skipped (aborted) and reported, and then tried
     alone against main so the report says whether it conflicts with main or only with another open PR;
  3. writes integration_report.md (Markdown, for the job summary) and integration_report.json.
Exit code 0 even when PRs conflict (the build and tests that follow decide the job); --strict makes a conflict fatal.
Standard library and git only. Written for this repository (docs/process/collaboration.md, "Integration check").
"""
import argparse, json, os, subprocess, sys, tempfile, urllib.request


def git(*args, cwd=None, check=True):
    p = subprocess.run(["git", *args], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if check and p.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {p.stderr.strip()}")
    return p


def open_prs(repo, token, drafts):
    """Open PRs of the repository: [(number, title, draft)], oldest first."""
    out, page = [], 1
    while True:
        req = urllib.request.Request(f"https://api.github.com/repos/{repo}/pulls?state=open&per_page=100&page={page}",
                                     headers={"Accept": "application/vnd.github+json",
                                              **({"Authorization": f"Bearer {token}"} if token else {})})
        with urllib.request.urlopen(req, timeout=30) as r:
            items = json.load(r)
        if not items:
            break
        for it in items:
            if it.get("draft") and not drafts:
                continue
            out.append((int(it["number"]), it.get("title", ""), bool(it.get("draft"))))
        page += 1
    return sorted(out)


def fetch_pr(n, cwd=None, remote="origin"):
    git("fetch", "--quiet", remote, f"+refs/pull/{n}/head:refs/integration/pr-{n}", cwd=cwd)
    return f"refs/integration/pr-{n}"


def conflict_files(cwd=None):
    p = git("diff", "--name-only", "--diff-filter=U", cwd=cwd, check=False)
    return [f for f in p.stdout.split() if f]


def try_merge(ref, label, cwd=None):
    """Merge ref into HEAD. Returns (ok, conflicting files). On a conflict the merge is aborted."""
    p = git("merge", "--no-ff", "--no-edit", "-m", f"integration: {label}", ref, cwd=cwd, check=False)
    if p.returncode == 0:
        return True, []
    files = conflict_files(cwd)
    git("merge", "--abort", cwd=cwd, check=False)
    return False, files or ["(merge failed: " + (p.stderr.strip().splitlines() or ["?"])[-1] + ")"]


def conflicts_with_base(ref, base, cwd=None):
    """Does ref conflict with base on its own? (git merge-tree, no working-tree change)"""
    p = git("merge-tree", "--write-tree", base, ref, cwd=cwd, check=False)
    return p.returncode != 0


def integrate(prs, cwd=None, fetch=fetch_pr):
    """prs: [(number, title)]. Merges onto the current HEAD. Returns the report dict."""
    base = git("rev-parse", "HEAD", cwd=cwd).stdout.strip()
    merged, clashes = [], []
    for n, title in prs:
        ref = fetch(n, cwd=cwd)
        ok, files = try_merge(ref, f"#{n} {title}", cwd=cwd)
        if ok:
            merged.append({"pr": n, "title": title})
        else:
            alone = conflicts_with_base(ref, base, cwd=cwd)
            clashes.append({"pr": n, "title": title, "files": files,
                            "with": "main" if alone else "another open PR merged before it"})
    return {"base": base, "merged": merged, "conflicts": clashes,
            "head": git("rev-parse", "HEAD", cwd=cwd).stdout.strip()}


def markdown(rep):
    lines = ["## Integration check: all open PRs merged together", "",
             f"Base `{rep['base'][:10]}`, {len(rep['merged'])} PRs merged, {len(rep['conflicts'])} could not be merged.", ""]
    if rep["merged"]:
        lines.append("Merged: " + ", ".join(f"#{m['pr']}" for m in rep["merged"]))
        lines.append("")
    if rep["conflicts"]:
        lines += ["| PR | conflicts with | files |", "|---|---|---|"]
        for c in rep["conflicts"]:
            lines.append(f"| #{c['pr']} {c['title'][:60]} | {c['with']} | {', '.join(c['files'][:8])} |")
        lines.append("")
        lines.append("A PR that conflicts only with another open PR is the one to fix before both merge "
                     "(merge the other one first, then merge main into it).")
    lines.append("")
    lines.append("The build and tests below run on this merged tree: a failure there that no single PR shows means "
                 "two PRs break each other.")
    return "\n".join(lines) + "\n"


def self_test():
    fails = 0
    def ok(c, what):
        nonlocal fails
        print(("ok   " if c else "FAIL ") + what)
        fails += 0 if c else 1
    with tempfile.TemporaryDirectory() as d:
        def g(*a): return git(*a, cwd=d)
        g("init", "-q", "-b", "main"); g("config", "user.email", "t@t"); g("config", "user.name", "t")
        def write(name, text):
            with open(os.path.join(d, name), "w") as f: f.write(text)
        write("a.txt", "1\n2\n3\n"); write("b.txt", "x\n"); g("add", "-A"); g("commit", "-qm", "base")
        def branch(name, fname, text):
            g("checkout", "-q", "-b", name, "main"); write(fname, text); g("commit", "-qam", name); g("checkout", "-q", "main")
            return name
        branch("pr1", "b.txt", "x\ny\n")            # clean
        branch("pr2", "a.txt", "1\nTWO\n3\n")        # clean alone
        branch("pr3", "a.txt", "1\nzwei\n3\n")       # clean alone, clashes with pr2
        g("checkout", "-q", "main"); write("a.txt", "1\n2\n3\n")
        g("checkout", "-q", "-b", "pr4", "main"); write("b.txt", "conflict-with-main\n"); g("commit", "-qam", "pr4")
        g("checkout", "-q", "main"); write("b.txt", "main-changed\n"); g("commit", "-qam", "main moves")
        g("checkout", "-q", "-b", "integration", "main")
        refs = {1: "pr1", 2: "pr2", 3: "pr3", 4: "pr4"}
        rep = integrate([(1, "one"), (2, "two"), (3, "three"), (4, "four")], cwd=d, fetch=lambda n, cwd=None: refs[n])
        ok([m["pr"] for m in rep["merged"]] == [2], f"only #2 merges cleanly after main moved (got {rep['merged']})")
        by = {c["pr"]: c for c in rep["conflicts"]}
        ok(by.get(3, {}).get("with") == "another open PR merged before it", "#3 clashes only with #2")
        ok(by.get(4, {}).get("with") == "main", "#4 conflicts with main itself")
        ok(by.get(1, {}).get("with") == "main", "#1 conflicts with main (both changed b.txt)")
        ok("a.txt" in by.get(3, {}).get("files", []), "conflicting file named")
        ok(git("status", "--porcelain", cwd=d).stdout.strip() == "", "working tree clean after aborted merges")
        md = markdown(rep)
        ok("#3" in md and "another open PR" in md, "report names the clash")
    print(f"integration_merge self-test: {'ok' if not fails else str(fails) + ' failure(s)'}")
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prs", nargs="*", type=int)
    ap.add_argument("--open", action="store_true")
    ap.add_argument("--drafts", action="store_true", help="include draft PRs")
    ap.add_argument("--strict", action="store_true", help="exit 1 when any PR could not be merged")
    ap.add_argument("--out", default=".")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if a.open:
        prs = [(n, t) for n, t, _ in open_prs(os.environ["GITHUB_REPOSITORY"], os.environ.get("GITHUB_TOKEN", ""), a.drafts)]
    else:
        prs = [(n, "") for n in sorted(a.prs or [])]
    rep = integrate(prs)
    with open(os.path.join(a.out, "integration_report.json"), "w") as f: json.dump(rep, f, indent=1)
    md = markdown(rep)
    with open(os.path.join(a.out, "integration_report.md"), "w") as f: f.write(md)
    print(md)
    return 1 if (a.strict and rep["conflicts"]) else 0


if __name__ == "__main__":
    sys.exit(main())
