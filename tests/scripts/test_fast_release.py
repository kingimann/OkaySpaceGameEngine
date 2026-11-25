#!/usr/bin/env python3
"""Offline tests of the fast release path (scripts/fast_release.py, .github/workflows/fast-release.yml, docs/release/releasing.md).

GitHub Actions cannot run here, so this does what can be done without it:
  * the decisions, with fake data: the next version (minor, patch, override, refusals: exists, older, LTS line, bad text), the CI gate
    (no run, running, red, cancelled, green run with a failed job, the newest run of a sha wins), the changelog entries (parsing,
    every refusal, assembly, "already released" by file name), the stamp, the docs sync, the after-release check;
  * the command line end to end in a throw-away git repository, including the same `git ls-tree | sed` the workflow uses;
  * the workflows read as YAML: shape, no user input pasted into shell, every shell block passes `bash -n`, every fast step in
    release.yml is skipped unless stamp_version is set (so the old path is unchanged), the CI gate and rule 12 are present;
  * every real entry in dist/changelog.d is valid.

Exit 0 = fine, 1 = a check failed, 77 = PyYAML is not installed (ctest shows that as skipped).
"""
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

try:
    import yaml
except ImportError:
    print("SKIP: PyYAML is not installed, so the workflows cannot be read")
    sys.exit(77)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(ROOT, "scripts", "fast_release.py")
spec = importlib.util.spec_from_file_location("fast_release", SCRIPT)
fr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fr)

fails = []


def check(cond, what):
    if not cond:
        fails.append(what)


def refused(fn, *a, **k):
    try:
        fn(*a, **k)
    except fr.Refused as e:
        return str(e)
    return None


def run(*args, cwd=None):
    p = subprocess.run([sys.executable, "-I", SCRIPT] + list(args), cwd=cwd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


# ---- versions ----------------------------------------------------------------------------------------------------
ex = fr.existing_versions(tags=["v2.879.0", "v2.880.0", "v2.850.1", "v2.880.0.3", "vfoo", "dev-1"],
                          releases=["v2.878.0"], index={"latest": "2.880.0", "lts": "2.850.1",
                                                        "versions": [{"version": "2.877.0"}, {"version": "bad"}]})
check(ex == {(2, 879, 0), (2, 880, 0), (2, 850, 1), (2, 878, 0), (2, 877, 0)}, "existing_versions ignores four-number and odd tags: %r" % ex)
check(fr.fmt(fr.next_version(ex)[0]) == "2.881.0", "minor bump after 2.880.0")
check(fr.fmt(fr.next_version(ex, bump="patch")[0]) == "2.880.1", "patch bump after 2.880.0")
check(fr.fmt(fr.next_version(ex, cmake=(2, 870, 0))[0]) == "2.881.0", "an old CMakeLists.txt version is ignored")
check(fr.fmt(fr.next_version(ex, cmake=(2, 883, 0))[0]) == "2.883.0", "a pre-bumped, unreleased CMakeLists.txt version is used as it is")
check(fr.fmt(fr.next_version(ex, override="2.890.0")[0]) == "2.890.0", "an explicit newer version is accepted")
check("rule 12" in (refused(fr.next_version, ex, override="2.880.0") or ""), "an existing version is refused (rule 12)")
check("not newer" in (refused(fr.next_version, ex, override="2.870.5") or ""), "an older version is refused")
check(refused(fr.next_version, ex, override="2.881") is not None, "two numbers are refused")
check(refused(fr.next_version, ex, override="2.881.0.1") is not None, "four numbers are refused")
check(refused(fr.next_version, ex, override="x") is not None, "text is refused")
check(refused(fr.next_version, ex, bump="major") is not None, "an unknown bump is refused")
check("long-term-support" in (refused(fr.next_version, {(2, 880, 0)}, override="2.880.1", lts="2.880.0") or ""), "the LTS line is refused")
check(fr.next_version(set())[0] == (2, 1, 0) and fr.next_version(set(), cmake=(2, 5, 0))[0] == (2, 5, 0), "first release")
check(fr.cmake_version("cmake_minimum_required(VERSION 3.16)\nproject(X\n    VERSION 2.880.0\n") == (2, 880, 0),
      "the build script's way of reading the version skips 'VERSION 3.16'")

# ---- the CI gate -------------------------------------------------------------------------------------------------
SHA = "a" * 40


def run_(i, status="completed", conclusion="success", sha=SHA, event="push", created="2026-11-17T10:00:00Z"):
    return {"id": i, "head_sha": sha, "event": event, "status": status, "conclusion": conclusion, "created_at": created, "run_number": i}


check(fr.ci_run_decision({"workflow_runs": []}, SHA)["state"] == "missing", "no run at all")
check(fr.ci_run_decision([run_(1, sha="b" * 40)], SHA)["state"] == "missing", "a run for another commit does not count")
check(fr.ci_run_decision([run_(1, event="pull_request")], SHA)["state"] == "missing", "a pull request run does not count")
check(fr.ci_run_decision([run_(1, status="in_progress", conclusion=None)], SHA)["state"] == "running", "running")
check(fr.ci_run_decision([run_(1, status="queued", conclusion=None)], SHA)["state"] == "running", "queued")
check(fr.ci_run_decision([run_(1, conclusion="failure")], SHA)["state"] == "red", "red")
check(fr.ci_run_decision([run_(1, conclusion="cancelled")], SHA)["state"] == "red", "cancelled is not green")
check(fr.ci_run_decision([run_(1, conclusion="skipped")], SHA)["state"] == "red", "skipped is not green")
d = fr.ci_run_decision({"workflow_runs": [run_(1, conclusion="failure", created="2026-11-17T09:00:00Z"), run_(2)]}, SHA)
check(d["state"] == "pass-run" and d["run_id"] == 2, "a newer green re-run replaces an older red one")
d = fr.ci_run_decision([run_(1), run_(2, conclusion="failure", created="2026-11-17T11:00:00Z")], SHA)
check(d["state"] == "red", "a newer red re-run wins over an older green one")
check(SHA[:10] in fr.ci_run_decision([], SHA)["message"], "the message names the commit")
check(fr.ci_jobs_decision([{"name": "a", "conclusion": "success"}, {"name": "b", "conclusion": "skipped"}])["ok"], "success and skipped jobs pass")
j = fr.ci_jobs_decision({"jobs": [{"name": "Build & test (Ubuntu)", "conclusion": "success"}, {"name": "own-http", "conclusion": "failure"}]})
check(not j["ok"] and j["bad"] == ["own-http: failure"], "a failed job is named")
check(not fr.ci_jobs_decision([{"name": "x", "conclusion": None, "status": "in_progress"}])["ok"], "an unfinished job fails")
check(not fr.ci_jobs_decision([])["ok"], "no jobs listed fails")
check(fr.ci_jobs_failed([{"name": "a", "conclusion": "success"}, {"name": "b", "conclusion": None, "status": "in_progress"}])["ok"], "running jobs are not 'already red'")
_ok = [{"name": n, "conclusion": "success"} for n in fr.RELEASE_GATE_JOBS]
check(fr.ci_gate_decision(_ok + [{"name": "WebGL", "conclusion": None, "status": "in_progress"}])["ok"], "gate: a running non-gate job is not waited for")
check(fr.ci_gate_decision(_ok[:-1])["pending"], "gate: a missing gate job is pending")
check(fr.ci_gate_decision(_ok[:-1] + [{"name": fr.RELEASE_GATE_JOBS[-1], "conclusion": None, "status": "in_progress"}])["pending"], "gate: a running gate job is pending")
check(fr.ci_gate_decision(_ok + [{"name": "WebGL", "conclusion": "failure"}])["bad"], "gate: a red job anywhere refuses")
_ci = open(os.path.join(ROOT, ".github", "workflows", "ci.yml"), encoding="utf-8").read()
check(all("    name: " + n + "\n" in _ci for n in fr.RELEASE_GATE_JOBS), "every RELEASE_GATE_JOBS name is a job name in ci.yml")
check(not fr.ci_jobs_failed([{"name": "a", "conclusion": "cancelled"}])["ok"], "a cancelled job in a running run stops the wait")
check(not fr.ci_jobs_failed({"jobs": [{"name": "a", "conclusion": "failure"}]})["ok"], "a failed job in a running run stops the wait")

# ---- entries -----------------------------------------------------------------------------------------------------
GOOD = "section: Sandbox\npr: 12, #13\nroadmap: Digging is faster\n---\n- Digging is faster now.\n- Second line of\n  the second bullet.\n"
e = fr.parse_entry("12.txt", GOOD)
check(e["section"] == "Sandbox" and e["prs"] == [12, 13] and e["roadmap"] == "Digging is faster", "header fields")
check(e["bullets"] == ["Digging is faster now.", "Second line of the second bullet."], "bullets join their continuation lines: %r" % e["bullets"])
check(fr.parse_entry("a.txt", "- Just a bullet.\n")["section"] == fr.DEFAULT_SECTION, "no header = default section")
for what, text in [("empty", ""), ("only header", "section: X\n---\n"), ("no bullet", "Hello\n"), ("blank line", "- a\n\n- b\n"),
                   ("tab", "- a\tb\n"), ("unknown key", "title: x\n---\n- a\n"), ("repeated key", "pr: 1\npr: 2\n---\n- a\n"),
                   ("bad pr", "pr: abc\n---\n- a\n"), ("too long", "- " + "x" * 120 + "\n"), ("bad continuation", "- a\nb\n")]:
    try:
        fr.parse_entry("x.txt", text)
        fails.append("parse_entry accepted: " + what)
    except ValueError:
        pass

tmp = tempfile.mkdtemp(prefix="fastrel-")
try:
    ed = os.path.join(tmp, "changelog.d")
    os.makedirs(os.path.join(ed, "released", "2.800.0"))

    def put(name, text, d=ed):
        with open(os.path.join(d, name), "w", encoding="utf-8") as f:
            f.write(text)

    put("README.txt", "not an entry\n")
    put("_template.txt", "not an entry\n")
    put("released/2.800.0/1.txt", "broken, but history is not read")
    put("10.txt", "pr: 10\n---\n- Ten.\n")
    put("9.txt", "pr: 9\nsection: Sandbox\n---\n- Nine.\n")
    put("old.txt", "- Already released.\n")
    put("11.txt", "section: Sandbox\npr: 11\n---\n- Eleven.\n- Eleven b.\n")
    entries, problems = fr.load_entries(ed)
    check(not problems, "valid entries load: %r" % problems)
    check([x["id"] for x in entries] == ["10.txt", "11.txt", "9.txt", "old.txt"] or [x["id"] for x in entries] == ["9.txt", "10.txt", "11.txt", "old.txt"],
          "entries sort naturally: %r" % [x["id"] for x in entries])
    check([x["id"] for x in entries][:3] == ["9.txt", "10.txt", "11.txt"], "9 before 10 (natural order)")
    new = fr.new_entries(entries, ["old.txt", "README.txt"])
    check([x["id"] for x in new] == ["9.txt", "10.txt", "11.txt"], "an entry the last release had is not new")
    block = fr.render_block("2.881.0", new)
    check(block == "v2.881.0\n  Sandbox\n  - Nine.\n  - Eleven.\n  - Eleven b.\n  Fixes and new things\n  - Ten.\n",
          "block layout and section grouping: %r" % block)
    check(fr.roadmap_row("2.881.0", new) == "| 2.881.0 | Nine (#9); Ten (#10); Eleven (#11) |", "roadmap row: %s" % fr.roadmap_row("2.881.0", new))
    check(fr.notes_lines(new, 3) == ["- Nine.", "- Ten.", "- Eleven."] and len(fr.notes_lines(new)) == 4, "notes lines: first bullets first, capped")

    base = "OkaySpace Game Engine — Changelog\n\nv2.880.0\n  Old\n  - Old thing.\n\nv2.879.0\n  - Older.\n"
    merged = fr.merge_changelog(base, "2.881.0", block)
    check(merged.startswith("OkaySpace Game Engine — Changelog\n\nv2.881.0\n  Sandbox\n"), "the new block goes above the newest release")
    check(merged.index("v2.881.0") < merged.index("v2.880.0") < merged.index("v2.879.0") and "\n\nv2.880.0\n" in merged, "old releases kept, blank line between")
    check("already" in (refused(fr.merge_changelog, merged, "2.881.0", block) or ""), "a version written up twice is refused")
    check(fr.merge_changelog("", "2.1.0", "v2.1.0\n  - a\n").startswith("OkaySpace Game Engine"), "an empty base gets the title")

    files = fr.build_notes(ed, ["old.txt"], base, "2.881.0")
    check(set(files) == {"CHANGELOG.txt", "block.txt", "NOTES.txt", "ROADMAP_ROW.txt", "body.md", "entries.txt"}, "all notes files made")
    check(files["entries.txt"] == "9.txt\n10.txt\n11.txt\n", "entries.txt lists what was used")
    check(files["body.md"].startswith("## What's new in v2.881.0") and "### Sandbox" in files["body.md"] and "- Nine." in files["body.md"], "release body: %r" % files["body.md"])
    check("no new changelog entries" in (refused(fr.build_notes, ed, ["9.txt", "10.txt", "11.txt", "old.txt"], base, "2.881.0") or "").lower(), "nothing new is refused")
    check("Small fixes" in fr.build_notes(ed, ["9.txt", "10.txt", "11.txt", "old.txt"], base, "2.881.0", allow_empty=True)["CHANGELOG.txt"], "allow_empty releases anyway")
    put("bad.txt", "oops\n")
    put("not-txt.md", "- x\n")
    check("not valid" in (refused(fr.build_notes, ed, [], base, "2.881.0") or ""), "an invalid entry stops the release")
    os.remove(os.path.join(ed, "bad.txt"))
    os.remove(os.path.join(ed, "not-txt.md"))

    # ---- stamp ------------------------------------------------------------------------------------------------
    cm = "cmake_minimum_required(VERSION 3.16)\nproject(X\n    VERSION 2.880.0\n    LANGUAGES C)\nset(Y VERSION 1.2.3)\n"
    st = fr.stamp_cmake(cm, "2.881.0")
    check(st == cm.replace("VERSION 2.880.0", "VERSION 2.881.0"), "stamp changes exactly the project VERSION line")
    check(refused(fr.stamp_cmake, "project(X)\n", "2.881.0") is not None, "stamp refuses a file without a VERSION line")
    check(refused(fr.stamp_cmake, cm, "2.881") is not None, "stamp refuses a bad version")
    real = open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8").read()
    stamped = fr.stamp_cmake(real, "9.9.9")
    check(fr.cmake_version(stamped) == (9, 9, 9) and len(stamped.splitlines()) == len(real.splitlines()), "stamp works on the real CMakeLists.txt")
    diff = [(a, b) for a, b in zip(real.splitlines(), stamped.splitlines()) if a != b]
    check(len(diff) == 1, "stamp changes exactly one line of the real CMakeLists.txt")

    # ---- command line end to end, with a real throw-away git repository, like the workflow -------------------------
    repo = os.path.join(tmp, "repo")
    os.makedirs(os.path.join(repo, "dist", "changelog.d"))
    g = lambda *a: subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid", "-c", "commit.gpgsign=false"] + list(a),
                                  cwd=repo, capture_output=True, text=True, check=True).stdout
    g("init", "-q")
    shutil.copy(os.path.join(ed, "old.txt"), os.path.join(repo, "dist", "changelog.d", "old.txt"))
    g("add", "-A"); g("commit", "-q", "-m", "one"); g("tag", "v2.880.0")
    for n in ("9.txt", "10.txt", "11.txt"):
        shutil.copy(os.path.join(ed, n), os.path.join(repo, "dist", "changelog.d", n))
    g("add", "-A"); g("commit", "-q", "-m", "two")
    released = subprocess.run("git ls-tree --name-only refs/tags/v2.880.0 dist/changelog.d/ | sed 's#.*/##'", shell=True, cwd=repo,
                              capture_output=True, text=True).stdout.split()
    check(released == ["old.txt"], "the workflow's ls-tree | sed lists the entries the last release had: %r" % released)
    with open(os.path.join(tmp, "released.txt"), "w") as f:
        f.write("\n".join(released) + "\n")
    with open(os.path.join(tmp, "base.txt"), "w", encoding="utf-8") as f:
        f.write(base)
    with open(os.path.join(tmp, "tags.txt"), "w") as f:
        f.write("v2.880.0\n")
    with open(os.path.join(tmp, "rel.json"), "w") as f:
        json.dump([{"tagName": "v2.880.0"}, {"tagName": "v2.881.0"}], f)    # a draft release 2.881.0 exists
    with open(os.path.join(tmp, "idx.json"), "w") as f:
        json.dump({"latest": "2.880.0", "versions": []}, f)
    cmk = os.path.join(tmp, "CMakeLists.txt")
    with open(cmk, "w") as f:
        f.write(cm)
    rc, out, err = run("next-version", "--tags", os.path.join(tmp, "tags.txt"), "--releases", os.path.join(tmp, "rel.json"),
                       "--index", os.path.join(tmp, "idx.json"), "--cmake", cmk)
    check(rc == 0 and json.loads(out)["version"] == "2.882.0", "a draft release counts as existing: %s %s" % (out, err))
    rc, out, err = run("next-version", "--tags", os.path.join(tmp, "tags.txt"), "--index", os.path.join(tmp, "idx.json"), "--cmake", cmk,
                       "--version", "2.880.0")
    check(rc == 1 and "REFUSED" in err, "CLI refuses an existing version with exit 1")
    outdir = os.path.join(tmp, "out")
    rc, out, err = run("notes", "--entries-dir", os.path.join(repo, "dist", "changelog.d"), "--released-list", os.path.join(tmp, "released.txt"),
                       "--base", os.path.join(tmp, "base.txt"), "--version", "2.881.0", "--out-dir", outdir)
    check(rc == 0 and "3 new entries" in out, "CLI notes: %s %s" % (out, err))
    check(os.path.isfile(os.path.join(outdir, "CHANGELOG.txt")), "CLI notes wrote CHANGELOG.txt")
    rc, out, err = run("notes", "--entries-dir", os.path.join(repo, "dist", "changelog.d"), "--released-list", os.path.join(tmp, "released.txt"),
                       "--base", os.path.join(outdir, "CHANGELOG.txt"), "--version", "2.881.0", "--out-dir", outdir + "2")
    check(rc == 1 and "already" in err, "CLI notes refuses a changelog that already has the version")
    rc, out, err = run("stamp", "--version", "2.882.0", "--cmake", cmk)
    check(rc == 0 and "VERSION 2.882.0" in open(cmk).read(), "CLI stamp")
    rc, out, err = run("check-entries", "--entries-dir", os.path.join(ed, "nonexistent"))
    check(rc == 0, "check-entries on a missing folder is fine")
    put("zzz.txt", "nope\n")
    rc, out, err = run("check-entries", "--entries-dir", ed)
    check(rc == 1 and "zzz.txt" in err, "check-entries fails on a bad entry and names it")
    os.remove(os.path.join(ed, "zzz.txt"))

    # ---- sync-docs ----------------------------------------------------------------------------------------------
    sd = os.path.join(tmp, "sync")
    os.makedirs(os.path.join(sd, "dist", "changelog.d"))
    for n in ("9.txt", "10.txt", "11.txt", "old.txt"):
        shutil.copy(os.path.join(ed, n), os.path.join(sd, "dist", "changelog.d", n))
    with open(os.path.join(sd, "ROADMAP.md"), "w") as f:
        f.write("# R\n\n| Version | What |\n|---|---|\n| 2.880.0 | old |\n")
    with open(os.path.join(sd, "dist", "CHANGELOG.txt"), "w", encoding="utf-8") as f:
        f.write(base)
    with open(os.path.join(sd, "CMakeLists.txt"), "w") as f:
        f.write(cm)
    rc, out, err = run("sync-docs", "--version", "2.881.0", "--notes-dir", outdir, "--cmake", os.path.join(sd, "CMakeLists.txt"),
                       "--roadmap", os.path.join(sd, "ROADMAP.md"), "--changelog", os.path.join(sd, "dist", "CHANGELOG.txt"),
                       "--entries-dir", os.path.join(sd, "dist", "changelog.d"))
    check(rc == 0, "sync-docs ran: %s %s" % (out, err))
    rm = open(os.path.join(sd, "ROADMAP.md")).read()
    check(rm.index("| 2.881.0 |") < rm.index("| 2.880.0 |") and rm.count("| 2.881.0 |") == 1, "the ROADMAP row goes first in the table")
    check("VERSION 2.881.0" in open(os.path.join(sd, "CMakeLists.txt")).read(), "sync-docs sets the CMakeLists.txt version")
    check(open(os.path.join(sd, "dist", "CHANGELOG.txt"), encoding="utf-8").read().startswith("OkaySpace Game Engine"), "the changelog keeps its title")
    left = sorted(os.listdir(os.path.join(sd, "dist", "changelog.d")))
    check(left == ["old.txt", "released"] and sorted(os.listdir(os.path.join(sd, "dist", "changelog.d", "released", "2.881.0"))) == ["10.txt", "11.txt", "9.txt"],
          "released entries move into released/<version>/: %r" % left)
    rc, out, err = run("sync-docs", "--version", "2.881.0", "--notes-dir", outdir, "--cmake", os.path.join(sd, "CMakeLists.txt"),
                       "--roadmap", os.path.join(sd, "ROADMAP.md"), "--changelog", os.path.join(sd, "dist", "CHANGELOG.txt"),
                       "--entries-dir", os.path.join(sd, "dist", "changelog.d"))
    check(rc == 1, "sync-docs twice is refused")
finally:
    shutil.rmtree(tmp, ignore_errors=True)

# ---- the after-release check ---------------------------------------------------------------------------------------
H = {n: chr(ord("a") + i) * 64 for i, n in enumerate(fr.REQUIRED_EXES)}
good_release = {"tag_name": "v2.881.0", "draft": False, "target_commitish": SHA,
                "assets": [{"name": n, "size": 10, "digest": "sha256:" + H[n] if n in H else None} for n in fr.REQUIRED_ASSETS]}
good_index = {"latest": "2.881.0", "versions": [{"version": "2.881.0", "inBucket": True, "files": {n: {"size": 1, "sha256": H[n]} for n in H}}]}


def vr(release=good_release, latest="v2.881.0", sv="2.881.0", index=good_index, **kw):
    return fr.verify_release("2.881.0", SHA, release, latest, sv, index, **kw)


def only_bad(rows):
    return [w for ok, w in rows if not ok]


check(not only_bad(vr()), "a complete release passes: %r" % only_bad(vr()))
check(only_bad(vr(latest="v2.880.0")), "GitHub latest on another version fails")
check(only_bad(vr(sv="2.880.0")), "VERSION.txt on the update server on another version fails")
check(only_bad(vr(release=dict(good_release, draft=True))), "a draft fails")
check(only_bad(vr(release=dict(good_release, target_commitish="c" * 40))), "a tag on another commit fails")
check(only_bad(vr(release=dict(good_release, assets=[a for a in good_release["assets"] if a["name"] != "okay-server-linux-x64"]))), "a missing Linux server asset fails")
bad_idx = json.loads(json.dumps(good_index))
del bad_idx["versions"][0]["files"]["OkayEngine.exe"]
check(only_bad(vr(index=bad_idx)), "a missing SHA-256 fails")
bad_idx = json.loads(json.dumps(good_index))
bad_idx["versions"][0]["files"]["OkaySpace.exe"]["sha256"] = "f" * 64
check(any("GitHub equals" in w for w in only_bad(vr(index=bad_idx))), "GitHub and versions.json disagreeing about a program fails")
check(only_bad(vr(index={})), "an empty versions.json fails")
check(only_bad(vr(trusted_keys=True, sig_present=False, sig_ok=False)), "trusted keys set but no signature fails")
check(not only_bad(vr(trusted_keys=True, sig_present=True, sig_ok=True)), "trusted keys and a good signature pass")
check(only_bad(vr(sig_present=True, sig_ok=False)), "a present but wrong signature fails")

# ---- the workflows, read as YAML -------------------------------------------------------------------------------------


class StrictLoader(yaml.SafeLoader):
    pass


def _no_dups(loader, node, deep=False):
    seen = set()
    for k, _ in node.value:
        key = loader.construct_object(k, deep=deep)
        if key in seen:
            raise yaml.constructor.ConstructorError(None, None, "duplicate key %r" % (key,), k.start_mark)
        seen.add(key)
    return yaml.SafeLoader.construct_mapping(loader, node, deep)


StrictLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _no_dups)


def load(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        d = yaml.load(f, Loader=StrictLoader)
    if True in d and "on" not in d:   # YAML 1.1 reads `on:` as True
        d["on"] = d.pop(True)
    return d


FR = load(".github/workflows/fast-release.yml")
REL = load(".github/workflows/release.yml")

check(set(FR["on"]) == {"workflow_dispatch"}, "fast-release starts only by hand")
ins = FR["on"]["workflow_dispatch"]["inputs"]
check(set(ins) == {"dry_run", "bump", "version", "allow_empty", "gate"}, "fast-release inputs: %r" % sorted(ins))
check(FR["permissions"] == {"contents": "read"}, "fast-release is read-only by default")
check(FR["concurrency"]["group"] == "fast-release" and FR["concurrency"]["cancel-in-progress"] is False, "one fast release at a time, never cancelled")
check(set(FR["jobs"]) == {"plan", "release", "after"}, "three jobs")
check(FR["jobs"]["release"]["uses"] == "./.github/workflows/release.yml", "the release job calls release.yml")
check(FR["jobs"]["release"]["needs"] == "plan" and "dry_run" in str(FR["jobs"]["release"]["if"]), "release needs the plan and is skipped on a dry run")
check(FR["jobs"]["release"]["with"]["channel"] == "latest", "fast releases are the latest channel only (LTS has its own path)")
check(FR["jobs"]["release"]["permissions"] == {"contents": "write", "actions": "read"} and FR["jobs"]["release"]["secrets"] == "inherit", "release job: write + secrets for release.yml")
check(FR["jobs"]["plan"]["permissions"].get("actions") == "read", "plan can read CI runs")
check("contents" in FR["jobs"]["plan"]["permissions"] and FR["jobs"]["plan"]["permissions"]["contents"] == "read", "plan cannot write")
for name, job in FR["jobs"].items():
    if "uses" not in job:
        check(job.get("timeout-minutes"), "job %s has a timeout" % name)
steps = FR["jobs"]["plan"]["steps"]
text = open(os.path.join(ROOT, ".github/workflows/fast-release.yml"), encoding="utf-8").read()
check("ci.yml/runs?head_sha=$SHA&event=push" in text and "ci-jobs" in text, "the CI gate asks for main's own push run and checks its jobs")
check("refs/heads/main" in text and "origin/main" in text, "the plan refuses anything but main's head")
check("FAST_RELEASE_ENABLED" in text, "the owner's switch is in the workflow")
check("gh release view" in text and "rule 12" in text, "rule 12 is asked of GitHub directly")
check(not re.search(r"\bpush\b\s*(origin|--)|git push|gh release (create|upload)|--force", text), "fast-release.yml never pushes or creates a release itself")
check("set -x" not in text, "no shell tracing")
check(steps[0].get("with", {}).get("fetch-depth") == 0, "plan has the full history and tags")


def run_blocks(wf):
    for jn, job in wf["jobs"].items():
        for st in job.get("steps", []) or []:
            if "run" in st:
                yield jn, st.get("name", "?"), st["run"]


for jn, sn, block in run_blocks(FR):
    # Nothing a person types (inputs.version, bump) or GitHub text is pasted into shell: it comes through env.
    check("${{" not in block, "fast-release %s / %s: ${{ }} inside a run block" % (jn, sn))
    p = subprocess.run(["bash", "-n"], input=block, text=True, capture_output=True)
    check(p.returncode == 0, "fast-release %s / %s: bash -n: %s" % (jn, sn, p.stderr.strip()))

# release.yml: the old path is untouched, every fast step is gated
check("workflow_call" in REL["on"] and "workflow_dispatch" in REL["on"] and "push" in REL["on"], "release.yml keeps its triggers and gains workflow_call")
check(REL["on"]["workflow_dispatch"]["inputs"]["channel"]["default"] == "latest", "release.yml's dispatch input is unchanged")
check(REL["on"]["workflow_call"]["inputs"]["stamp_version"]["default"] == "", "stamp_version is empty unless passed")
fast = [(jn, st) for jn, job in REL["jobs"].items() for st in job.get("steps", []) or [] if str(st.get("name", "")).startswith("Fast release")]
check(len(fast) == 3, "release.yml has the three fast-release steps (found %d)" % len(fast))
for jn, st in fast:
    check(st.get("if") == "inputs.stamp_version", "step %r is skipped unless stamp_version is set" % st.get("name"))
rtext = open(os.path.join(ROOT, ".github/workflows/release.yml"), encoding="utf-8").read()
check(rtext.count("if: inputs.stamp_version") == 3, "exactly 3 steps are gated on stamp_version")
check("body_path: ${{ inputs.stamp_version && 'release-notes/body.md' || '' }}" in rtext, "the release body comes from the entries only in a fast release")
check("scripts/publish-supabase.sh" in rtext and "gh release edit" in rtext and "OKAY_UPDATE_TRUSTED_KEYS" in rtext, "publish, make-latest and signing are still release.yml's own")
for jn, sn, block in run_blocks(REL):
    if sn.startswith("Fast release"):
        p = subprocess.run(["bash", "-n"], input=block.replace("${{", "").replace("}}", ""), text=True, capture_output=True)
        check(p.returncode == 0, "release.yml %s: bash -n: %s" % (sn, p.stderr.strip()))
        check("${{" not in block, "release.yml fast step %s: no ${{ }} in the shell text" % sn)

# changelog-notes: the launcher's NOTES.txt for a normal release comes from that version's dist/CHANGELOG.txt block
# (2.903.2 shipped "Release 2.903.2 (#1143)" as its only note: the release runner's one-commit git log).
cl = """OkaySpace Game Engine - Changelog

v2.0.1
  Title line
  - First thing changed, which is a long sentence
    that goes on the next line.
  - Second thing.
    - A nested detail.

v2.0.0
  Older
  - Old thing.
"""
ents = fr.changelog_entry(cl, "2.0.1")
check([e["bullets"][0] for e in ents] == ["First thing changed, which is a long sentence that goes on the next line.", "Second thing.", "A nested detail."],
      "changelog_entry joins continued lines and stops at the next version: %r" % ents)
check(fr.notes_lines(ents)[0] == "- First thing changed, which is a long sentence that goes on the next line.", "notes_lines on a changelog block")
check(fr.changelog_entry(cl, "9.9.9") == [], "changelog_entry: no block for an unknown version")
check(fr.changelog_entry(cl, "2.0.0")[0]["bullets"] == ["Old thing."], "changelog_entry: the last block ends at the end of the file")
with tempfile.TemporaryDirectory() as d:
    cp = os.path.join(d, "CHANGELOG.txt")
    open(cp, "w").write(cl)
    r = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "scripts", "fast_release.py"), "changelog-notes", "--version", "2.0.1", "--changelog", cp], capture_output=True, text=True)
    check(r.returncode == 0 and r.stdout.splitlines()[1] == "- Second thing.", "changelog-notes command prints the notes: %r" % r.stdout)
    r = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "scripts", "fast_release.py"), "changelog-notes", "--version", "9.9.9", "--changelog", cp], capture_output=True, text=True)
    check(r.returncode == 1 and r.stdout == "", "changelog-notes exits 1 for a missing version (publish-supabase.sh then falls back)")
ptext = open(os.path.join(ROOT, "scripts", "publish-supabase.sh")).read()
check("changelog-notes" in ptext, "publish-supabase.sh takes NOTES.txt from the changelog block")

# every real entry is valid
_, problems = fr.load_entries(os.path.join(ROOT, "dist", "changelog.d"))
check(not problems, "entries in dist/changelog.d: %r" % problems)

for f in fails:
    sys.stderr.write("FAIL: %s\n" % f)
print("fast_release: %s (%d failures)" % ("FAILED" if fails else "ok", len(fails)))
sys.exit(1 if fails else 0)
