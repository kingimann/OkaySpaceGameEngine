#!/usr/bin/env python3
"""The logic behind the one-dispatch "Fast release" workflow (.github/workflows/fast-release.yml, docs/release/releasing.md).

Everything that decides something lives here, as plain functions over plain data (JSON files, text files), so it can be tested
offline with fake data (tests/scripts/test_fast_release.py). The workflow only fetches data (git, `gh`, curl) and calls this.

  fast_release.py next-version --tags T.txt [--releases R.json] [--index versions.json] [--cmake CMakeLists.txt]
                               [--bump minor|patch] [--version X.Y.Z]            -> JSON {version, latest, ...}; exit 1 = refused
  fast_release.py ci-run  --runs RUNS.json --sha SHA                            -> JSON {state, run_id, message}; exit 1 unless state=pass-run
  fast_release.py ci-jobs --jobs JOBS.json                                      -> JSON {ok, bad[]}; exit 1 unless every job passed or was skipped
  fast_release.py notes --entries-dir D --released-list L.txt --base CHANGELOG.txt --version V --out-dir OUT [--allow-empty]
  fast_release.py check-entries [--entries-dir dist/changelog.d]                 (what ctest and PR checks run)
  fast_release.py stamp --version V [--cmake CMakeLists.txt]                     (the version the build bakes in)
  fast_release.py sync-docs --version V --notes-dir OUT [--cmake ...] [--roadmap ...] [--changelog ...] [--entries-dir ...]
  fast_release.py verify --version V --sha SHA --release R.json --latest-tag vX --supabase-version "X" --index versions.json
                         [--sig versions.json.sig --trusted-keys hex,hex]

Entry files (dist/changelog.d/<id>.txt), one per change, written in the change's own PR (so nothing ever conflicts):

    section: Fixes and new things     optional; the heading the bullets go under (default shown)
    pr: 938, 945                      optional; PR numbers, shown in the ROADMAP row
    roadmap: one line for ROADMAP.md  optional; default is the first bullet
    ---
    - What changed, in plain words players can read. A second line of the same
      bullet is indented by two spaces.

Written from our own design (docs/release/releasing.md); Python standard library only.
"""
import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SECTION = "Fixes and new things"
REQUIRED_EXES = ("OkaySpace.exe", "OkayEngine.exe", "OkaySpacePlayer.exe")
# Assets a complete release has on GitHub (release.yml uploads these; the Linux server comes from the second job).
REQUIRED_ASSETS = REQUIRED_EXES + ("VERSION.txt", "CHANGELOG.txt", "okay-server-linux-x64", "OkaySpace-Setup.exe")
MAX_LINE = 100
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
VER_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
SHA_RE = re.compile(r"^[0-9a-f]{64}$")


class Refused(Exception):
    """A rule stopped the release. The message is what the person reading the workflow log is told."""


# ---- versions ---------------------------------------------------------------------------------------------------
def parse_ver(text):
    """'2.880.0' (a leading v is fine) -> (2, 880, 0). Anything else, four-number test builds included, -> None."""
    m = VER_RE.match(str(text).strip().lstrip("v"))
    return tuple(int(g) for g in m.groups()) if m else None


def fmt(t):
    return "%d.%d.%d" % t


def cmake_version(text):
    """The project VERSION in CMakeLists.txt, read the way scripts/build-windows-dist.sh reads it (the first x.y.z after VERSION)."""
    m = re.search(r"VERSION[ \t]+([0-9]+\.[0-9]+\.[0-9]+)", text)
    return parse_ver(m.group(1)) if m else None


def existing_versions(tags=(), releases=(), index=None):
    """Every released three-number version we can learn about: git tags, GitHub releases (drafts included) and versions.json."""
    found = set()
    for t in list(tags) + list(releases):
        v = parse_ver(t)
        if v:
            found.add(v)
    if isinstance(index, dict):
        for key in ("latest", "lts"):
            v = parse_ver(index.get(key, "")) if index.get(key) else None
            if v:
                found.add(v)
        for e in index.get("versions", []) or []:
            v = parse_ver(e.get("version", "")) if isinstance(e, dict) else None
            if v:
                found.add(v)
    return found


def next_version(existing, cmake=None, bump="minor", override=None, lts=None):
    """The version this release gets. Returns (version tuple, why). Raises Refused.

    Releases are numbered 2.<build>.<patch>. "minor" counts the build (2.880.0 -> 2.881.0, what every release did so far), "patch"
    counts the patch (2.880.0 -> 2.880.1). The counter starts from the newest published version; a CMakeLists.txt VERSION that is
    newer than anything published (somebody pre-bumped it) is used as it is. Never a version that already exists (rule 12), never one
    in the LTS line (those are cut on their own branch, docs/release/release_channels.md)."""
    if bump not in ("minor", "patch"):
        raise Refused("bump must be minor or patch (got %r)" % bump)
    latest = max(existing) if existing else None
    if override:
        v = parse_ver(override)
        if not v or not VER_RE.match(str(override).strip().lstrip("v")):
            raise Refused("the version %r is not three numbers like 2.881.0" % override)
        if v in existing:
            raise Refused("v%s already exists. Never release a version twice: it would rebuild the programs and change their checksums "
                          "(CLAUDE.md rule 12)." % fmt(v))
        if latest and v <= latest:
            raise Refused("v%s is not newer than the newest release v%s; launchers only move forward." % (fmt(v), fmt(latest)))
        why = "the version you typed"
    elif latest is None:
        v = cmake or (2, 1, 0)
        why = "no release exists yet: the CMakeLists.txt version"
    elif cmake and cmake > latest:
        v = cmake
        why = "CMakeLists.txt already says %s, newer than the newest release %s" % (fmt(cmake), fmt(latest))
    elif bump == "minor":
        v = (latest[0], latest[1] + 1, 0)
        why = "next build after %s" % fmt(latest)
    else:
        v = (latest[0], latest[1], latest[2] + 1)
        why = "next patch after %s" % fmt(latest)
    if v in existing:
        raise Refused("v%s already exists (rule 12)." % fmt(v))
    lts_v = parse_ver(lts) if lts else None
    if lts_v and v[:2] == lts_v[:2]:
        raise Refused("v%s is in the long-term-support line %d.%d; patches of that line are released from its own lts/ branch." % ((fmt(v),) + lts_v[:2]))
    return v, why


# ---- the CI gate (rule 6, without a second CI run) ---------------------------------------------------------------
def ci_run_decision(runs, sha):
    """Which CI run counts for this commit? `runs` is the `workflow_runs` list of GitHub's
    GET /repos/{r}/actions/workflows/ci.yml/runs?head_sha=SHA&event=push answer (or the whole answer object)."""
    if isinstance(runs, dict):
        runs = runs.get("workflow_runs", [])
    mine = [r for r in runs if isinstance(r, dict) and r.get("head_sha") == sha and r.get("event") == "push"]
    if not mine:
        return {"state": "missing", "run_id": None,
                "message": "There is no CI run for commit %s on main. Wait for CI to start, or merge nothing and dispatch again." % sha[:10]}
    # Newest first. A later re-run of the same sha replaces an older failed one only if it is newer.
    mine.sort(key=lambda r: (r.get("created_at") or "", r.get("run_number") or 0), reverse=True)
    r = mine[0]
    rid = r.get("id")
    if r.get("status") != "completed":
        return {"state": "running", "run_id": rid, "message": "CI for commit %s is still %s (run %s). Wait for every job to finish, then dispatch again."
                % (sha[:10], r.get("status"), rid)}
    if r.get("conclusion") != "success":
        return {"state": "red", "run_id": rid, "message": "CI for commit %s finished as %s (run %s). A release needs every job green (rule 6): fix it on main first."
                % (sha[:10], r.get("conclusion"), rid)}
    return {"state": "pass-run", "run_id": rid, "message": "CI run %s finished green." % rid}


def ci_jobs_decision(jobs):
    """Every job of that run succeeded or was skipped. (A workflow can finish 'success' with a job that was allowed to fail.)"""
    if isinstance(jobs, dict):
        jobs = jobs.get("jobs", [])
    if not jobs:
        return {"ok": False, "bad": ["(no jobs listed)"], "count": 0}
    bad = []
    for j in jobs:
        c = j.get("conclusion")
        if c not in ("success", "skipped"):
            bad.append("%s: %s" % (j.get("name", "?"), c or j.get("status") or "unknown"))
    return {"ok": not bad, "bad": bad, "count": len(jobs)}


# The smaller "release gate": the CI jobs that build and test what ships (the Windows programs, the player/editor, the scripting
# sanitizers, the update-signing tests). The other jobs (Assimp comparison, own HTTP, own stack, no-libsodium crypto, WebGL, SQL) are
# informational for a release: they do not build anything that is released. Display names of ci.yml jobs; a test keeps this list honest.
RELEASE_GATE_JOBS = (
    "Build & test (Ubuntu)",
    "Build player runtime (Ubuntu)",
    "Build & self-test editor (Ubuntu)",
    "Export a game and check its visual scripts (Ubuntu)",
    "Scripting and robustness tests (ASan + UBSan)",
    "Cross-compile Windows build (MinGW)",
    "Windows export check",
    "Windows updater restart",
    "Ticket function, update signing and account server tests",
    "Anti-cheat tests with libsodium (Ubuntu)",
)


def ci_gate_decision(jobs, gate=RELEASE_GATE_JOBS):
    """The release gate, for a CI run that may still be going: every gate job success/skipped, and NO job anywhere already red or
    cancelled. Jobs outside the gate that are still running are not waited for. Returns {ok, bad, pending, count}."""
    if isinstance(jobs, dict):
        jobs = jobs.get("jobs", [])
    by = {j.get("name"): j for j in jobs}
    bad = ci_jobs_failed(jobs)["bad"]
    pending = []
    for n in gate:
        j = by.get(n)
        c = j.get("conclusion") if j else None
        if c not in ("success", "skipped") and not (j and c in ("failure", "cancelled", "timed_out", "startup_failure", "action_required")):
            pending.append("%s: %s" % (n, (j.get("status") if j else None) or "not started"))
    return {"ok": not bad and not pending, "bad": bad, "pending": pending, "count": len(jobs)}


def ci_jobs_failed(jobs):
    """Jobs of a CI run that is still going which already ended red or cancelled (so waiting longer cannot make that run green)."""
    if isinstance(jobs, dict):
        jobs = jobs.get("jobs", [])
    bad = ["%s: %s" % (j.get("name", "?"), j.get("conclusion")) for j in jobs
           if j.get("conclusion") in ("failure", "cancelled", "timed_out", "startup_failure", "action_required")]
    return {"ok": not bad, "bad": bad, "count": len(jobs)}


# ---- changelog entries ---------------------------------------------------------------------------------------------
def natural_key(s):
    return [int(p) if p.isdigit() else p for p in re.split(r"(\d+)", s)]


def parse_entry(name, text):
    """One entry file -> dict, or raises ValueError with what is wrong."""
    lines = text.replace("\r\n", "\n").split("\n")
    while lines and lines[-1] == "":
        lines.pop()
    meta, i = {}, 0
    if "---" in lines:
        sep = lines.index("---")
        for ln in lines[:sep]:
            if not ln.strip():
                continue
            m = re.match(r"^(section|pr|roadmap):\s*(.*)$", ln)
            if not m:
                raise ValueError("%s: header line %r is not 'section:', 'pr:' or 'roadmap:'" % (name, ln))
            if m.group(1) in meta:
                raise ValueError("%s: '%s:' is written twice" % (name, m.group(1)))
            meta[m.group(1)] = m.group(2).strip()
        i = sep + 1
    body = lines[i:]
    if not any(b.strip() for b in body):
        raise ValueError("%s: no text under the header (write at least one '- ' bullet)" % name)
    if any(b.strip() == "" for b in body):
        raise ValueError("%s: blank line inside the text (one bullet list, no blank lines)" % name)
    if not body[0].startswith("- "):
        raise ValueError("%s: the text must start with a '- ' bullet" % name)
    for b in body:
        if "\t" in b or any(ord(c) < 32 for c in b):
            raise ValueError("%s: tabs and control characters are not allowed" % name)
        if len(b) > MAX_LINE:
            raise ValueError("%s: a line is %d characters long (wrap at about 75, never over %d)" % (name, len(b), MAX_LINE))
        if not (b.startswith("- ") or b.startswith("  ")):
            raise ValueError("%s: line %r must start with '- ' (a new bullet) or two spaces (the same bullet continued)" % (name, b[:30]))
    prs = []
    if meta.get("pr"):
        for p in re.split(r"[,\s]+", meta["pr"].strip()):
            if not p:
                continue
            if not re.match(r"^#?\d+$", p):
                raise ValueError("%s: 'pr:' must be PR numbers like '938, 945' (got %r)" % (name, p))
            prs.append(int(p.lstrip("#")))
    section = meta.get("section") or DEFAULT_SECTION
    if len(section) > 60 or "\n" in section:
        raise ValueError("%s: the section name is too long" % name)
    bullets, cur = [], None
    for b in body:
        if b.startswith("- "):
            cur = [b[2:].strip()]
            bullets.append(cur)
        else:
            cur.append(b.strip())
    bullets = [" ".join(x) for x in bullets]
    return {"id": name, "section": section, "prs": prs, "roadmap": meta.get("roadmap", ""), "body": body, "bullets": bullets}


def load_entries(entries_dir):
    """All entries directly in the folder (README.txt and names starting with _ or . are not entries; sub-folders such as released/
    are history and are not read). Returns (entries sorted naturally by id, list of problems)."""
    entries, problems = [], []
    if not os.path.isdir(entries_dir):
        return entries, problems
    for fn in sorted(os.listdir(entries_dir), key=natural_key):
        path = os.path.join(entries_dir, fn)
        if not os.path.isfile(path) or fn.startswith((".", "_")) or fn.lower().startswith("readme"):
            continue
        if not fn.endswith(".txt") or not ID_RE.match(fn[:-4]):
            problems.append("%s: an entry is a file named <id>.txt (letters, digits, dot, dash, underscore)" % fn)
            continue
        try:
            with open(path, encoding="utf-8") as f:
                entries.append(parse_entry(fn, f.read()))
        except ValueError as e:
            problems.append(str(e))
        except UnicodeDecodeError:
            problems.append("%s: not UTF-8 text" % fn)
    return entries, problems


def new_entries(entries, released_names):
    """Entries that were not already in the previous release's source tree (that is what 'released' means: no file is ever deleted)."""
    done = set(released_names)
    return [e for e in entries if e["id"] not in done]


def render_block(version, entries):
    """The changelog text of one release, in the layout of dist/CHANGELOG.txt."""
    order, groups = [], {}
    for e in entries:
        if e["section"] not in groups:
            groups[e["section"]] = []
            order.append(e["section"])
        groups[e["section"]].append(e)
    out = ["v%s" % version]
    for s in order:
        out.append("  %s" % s)
        for e in groups[s]:
            out.extend("  " + b for b in e["body"])
    return "\n".join(out) + "\n"


def merge_changelog(base, version, block):
    """The new block goes above the newest old release. Refuses a base that already has this version."""
    if re.search(r"(?m)^v%s\s*$" % re.escape(version), base):
        raise Refused("the changelog already has a v%s section: that version was already written up." % version)
    lines = base.replace("\r\n", "\n").split("\n")
    at = next((i for i, ln in enumerate(lines) if re.match(r"^v\d+\.\d+", ln)), None)
    if at is None:
        head = base.rstrip("\n")
        return (head + "\n\n" if head else "OkaySpace Game Engine — Changelog\n\n") + block
    before = "\n".join(lines[:at]).rstrip("\n")
    return (before + "\n\n" if before else "") + block + "\n" + "\n".join(lines[at:])


def roadmap_row(version, entries):
    parts = []
    for e in entries:
        text = e["roadmap"] or e["bullets"][0]
        text = text.strip().rstrip(".")
        if e["prs"]:
            text += " (%s)" % ", ".join("#%d" % p for p in e["prs"])
        parts.append(text)
    return "| %s | %s |" % (version, "; ".join(parts))


def notes_lines(entries, limit=8):
    """The launcher's 'what's new' card (NOTES.txt): "- text" lines, one per entry first, then second bullets, at most `limit`."""
    firsts = [e["bullets"][0] for e in entries]
    rest = [b for e in entries for b in e["bullets"][1:]]
    out = []
    for b in firsts + rest:
        b = b.strip()
        out.append("- " + (b if len(b) <= 200 else b[:197] + "..."))
        if len(out) >= limit:
            break
    return out


def changelog_entry(changelog_text, version):
    """The bullets of one version's block in dist/CHANGELOG.txt, as [{"bullets": [...]}] for notes_lines().
    A block starts at a line "v<version>" and ends at the next line that starts with "v<digit>". A bullet is a line
    starting with "- " after its indent; deeper indented lines without "- " continue the bullet above."""
    lines = changelog_text.splitlines()
    head = "v" + version
    start = next((i for i, l in enumerate(lines) if l.strip() == head), None)
    if start is None:
        return []
    bullets = []
    for l in lines[start + 1:]:
        if re.match(r"v\d", l):
            break
        t = l.strip()
        if not t:
            continue
        if t.startswith("- "):
            bullets.append(t[2:].strip())
        elif bullets and l.startswith("    "):
            bullets[-1] += " " + t
    return [{"bullets": [b]} for b in bullets]


def build_notes(entries_dir, released_names, base_text, version, allow_empty=False):
    """-> dict of file name -> text, everything the release needs from the entries. Raises Refused."""
    entries, problems = load_entries(entries_dir)
    if problems:
        raise Refused("Changelog entries are not valid:\n  " + "\n  ".join(problems))
    new = new_entries(entries, released_names)
    if not new and not allow_empty:
        raise Refused("No new changelog entries since the last release: players would see nothing under v%s. Every change adds a file "
                      "dist/changelog.d/<id>.txt (docs/release/releasing.md); or dispatch with allow_empty to release anyway." % version)
    block = render_block(version, new) if new else "v%s\n  %s\n  - Small fixes and improvements.\n" % (version, DEFAULT_SECTION)
    changelog = merge_changelog(base_text, version, block)
    body = ["## What's new in v%s" % version, ""]
    last = None
    for e in new:
        if e["section"] != last:
            body += ["", "### %s" % e["section"]]
            last = e["section"]
        body += ["- " + b for b in e["bullets"]]
    if not new:
        body += ["- Small fixes and improvements."]
    return {
        "CHANGELOG.txt": changelog,
        "block.txt": block,
        "NOTES.txt": "\n".join(notes_lines(new) or ["- OkaySpace v%s" % version]) + "\n",
        "ROADMAP_ROW.txt": roadmap_row(version, new) + "\n" if new else "| %s | Small fixes and improvements |\n" % version,
        "body.md": "\n".join(body).replace("\n\n\n", "\n\n").strip() + "\n",
        "entries.txt": "".join(e["id"] + "\n" for e in new),
    }


# ---- stamp ---------------------------------------------------------------------------------------------------------------
def stamp_cmake(text, version):
    """Replace the project VERSION line (the first indented 'VERSION x.y.z' line, like dev-build.yml does). Exactly one line changes."""
    if not VER_RE.match(version):
        raise Refused("version %r is not three numbers" % version)
    pat = re.compile(r"(?m)^([ \t]+VERSION[ \t]+)[0-9]+\.[0-9]+\.[0-9]+[ \t]*$")
    if not pat.search(text):
        raise Refused("CMakeLists.txt has no project VERSION line to put %s into." % version)
    new = pat.sub(lambda m: m.group(1) + version, text, count=1)
    if cmake_version(new) != parse_ver(version):
        raise Refused("after stamping, the build script would still read %s from CMakeLists.txt, not %s." % (cmake_version(new), version))
    return new


# ---- sync (optional catch-up PR so the repo's own files match what was released) ---------------------------------------------
def insert_roadmap_row(text, row):
    prefix = "| " + row.split("|")[1].strip() + " |"
    lines = text.split("\n")
    for i, ln in enumerate(lines):
        if ln.startswith("|---"):
            if any(l.startswith(prefix) for l in lines):
                raise Refused("ROADMAP.md already has a row for that version")
            lines.insert(i + 1, row)
            return "\n".join(lines)
    raise Refused("ROADMAP.md has no releases table")


# ---- the after-release check (what the owner does by hand today) -------------------------------------------------------------
def verify_release(version, sha, release, latest_tag, supabase_version, index, sig_present=None, sig_ok=None, trusted_keys=False):
    """-> list of (ok, what). Every row must be ok. `release` is GitHub's REST answer for GET /repos/{r}/releases/tags/v<version>."""
    rows = []

    def add(ok, what):
        rows.append((bool(ok), what))

    tag = "v" + version
    add(release.get("tag_name") == tag and not release.get("draft"), "GitHub release %s exists and is published (not a draft)" % tag)
    add(release.get("target_commitish") == sha, "the tag points at the commit that was tested (%s)" % sha[:10])
    assets = {a.get("name"): a for a in release.get("assets", []) if isinstance(a, dict)}
    for n in REQUIRED_ASSETS:
        add(n in assets and (assets[n].get("size") or 0) > 0, "GitHub asset %s is attached" % n)
    add(latest_tag == tag, "GitHub's \"latest\" release is %s (it is %s)" % (tag, latest_tag))
    add((supabase_version or "").strip() == version, "VERSION.txt on the update server says %s (it says %r)" % (version, (supabase_version or "").strip()))
    entry = None
    if isinstance(index, dict):
        add(index.get("latest") == version, "versions.json says latest is %s (it says %r)" % (version, index.get("latest")))
        entry = next((e for e in index.get("versions", []) if isinstance(e, dict) and e.get("version") == version), None)
    add(entry is not None, "versions.json lists v%s" % version)
    files = (entry or {}).get("files", {}) or {}
    for n in REQUIRED_EXES:
        h = (files.get(n) or {}).get("sha256") or ""
        add(SHA_RE.match(h), "versions.json has a SHA-256 for %s" % n)
        d = (assets.get(n) or {}).get("digest") or ""
        if d.startswith("sha256:") and SHA_RE.match(h):
            add(d[7:] == h, "%s: the SHA-256 on GitHub equals the one in versions.json (the same file in both places)" % n)
    if entry is not None:
        add(entry.get("inBucket") is True, "versions.json says v%s is in the bucket" % version)
    if trusted_keys:
        add(sig_present and sig_ok, "versions.json.sig exists and verifies against a trusted key (this build trusts signed feeds only)")
    elif sig_present is not None:
        add(sig_ok is not False, "versions.json.sig, when present, verifies")
    return rows


# ---- command line --------------------------------------------------------------------------------------------------------------
def _read(path, default=""):
    if not path:
        return default
    with open(path, encoding="utf-8") as f:
        return f.read()


def _read_json(path):
    """A JSON file, or (what `gh api --paginate --jq` writes) several JSON values one after another, which come back as a list."""
    t = _read(path)
    if not t.strip():
        return None
    try:
        return json.loads(t)
    except ValueError:
        dec, i, out = json.JSONDecoder(), 0, []
        while i < len(t):
            if t[i].isspace():
                i += 1
                continue
            v, i = dec.raw_decode(t, i)
            out.append(v)
        return out


def _lines(path):
    return [ln.strip() for ln in _read(path).splitlines() if ln.strip()]


def _release_names(path):
    data = _read_json(path) or []
    return [x.get("tagName", "") if isinstance(x, dict) else str(x) for x in data]


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("next-version")
    a.add_argument("--tags")
    a.add_argument("--releases")
    a.add_argument("--index")
    a.add_argument("--cmake", default=os.path.join(ROOT, "CMakeLists.txt"))
    a.add_argument("--bump", default="minor")
    a.add_argument("--version", default="")
    a = sub.add_parser("ci-run")
    a.add_argument("--runs", required=True)
    a.add_argument("--sha", required=True)
    a = sub.add_parser("ci-jobs")
    a.add_argument("--jobs", required=True)
    a.add_argument("--gate", choices=("release",), help="the smaller release gate (RELEASE_GATE_JOBS); exit 0 ok, 1 red, 3 still pending")
    a.add_argument("--failed-only", action="store_true", help="only look for jobs that already ended red/cancelled (a run still in progress)")
    a = sub.add_parser("notes")
    a.add_argument("--entries-dir", default=os.path.join(ROOT, "dist", "changelog.d"))
    a.add_argument("--released-list")
    a.add_argument("--base", required=True)
    a.add_argument("--version", required=True)
    a.add_argument("--out-dir", required=True)
    a.add_argument("--allow-empty", action="store_true")
    a = sub.add_parser("changelog-notes", help="NOTES.txt lines for one version, from its block in dist/CHANGELOG.txt")
    a.add_argument("--version", required=True)
    a.add_argument("--changelog", default=os.path.join(ROOT, "dist", "CHANGELOG.txt"))
    a.add_argument("--limit", type=int, default=8)
    a = sub.add_parser("check-entries")
    a.add_argument("--entries-dir", default=os.path.join(ROOT, "dist", "changelog.d"))
    a = sub.add_parser("stamp")
    a.add_argument("--version", required=True)
    a.add_argument("--cmake", default=os.path.join(ROOT, "CMakeLists.txt"))
    a = sub.add_parser("sync-docs")
    a.add_argument("--version", required=True)
    a.add_argument("--notes-dir", required=True)
    a.add_argument("--cmake", default=os.path.join(ROOT, "CMakeLists.txt"))
    a.add_argument("--roadmap", default=os.path.join(ROOT, "ROADMAP.md"))
    a.add_argument("--changelog", default=os.path.join(ROOT, "dist", "CHANGELOG.txt"))
    a.add_argument("--entries-dir", default=os.path.join(ROOT, "dist", "changelog.d"))
    a = sub.add_parser("verify")
    a.add_argument("--version", required=True)
    a.add_argument("--sha", required=True)
    a.add_argument("--release", required=True)
    a.add_argument("--latest-tag", default="")
    a.add_argument("--supabase-version", default="")
    a.add_argument("--index", required=True)
    a.add_argument("--sig-present", choices=("yes", "no"))
    a.add_argument("--sig-ok", choices=("yes", "no"))
    a.add_argument("--trusted-keys", action="store_true")
    args = p.parse_args(argv)

    try:
        if args.cmd == "changelog-notes":
            lines = notes_lines(changelog_entry(_read(args.changelog), args.version), args.limit)
            if not lines:
                return 1   # no block for this version: the caller falls back
            print("\n".join(lines))
            return 0
        if args.cmd == "next-version":
            index = _read_json(args.index) if args.index else None
            ex = existing_versions(_lines(args.tags) if args.tags else [], _release_names(args.releases) if args.releases else [], index)
            cm = cmake_version(_read(args.cmake))
            v, why = next_version(ex, cm, args.bump, args.version or None, (index or {}).get("lts") if isinstance(index, dict) else None)
            print(json.dumps({"version": fmt(v), "latest": fmt(max(ex)) if ex else "", "why": why, "known": len(ex)}))
        elif args.cmd == "ci-run":
            d = ci_run_decision(_read_json(args.runs) or [], args.sha)
            print(json.dumps(d))
            return 0 if d["state"] == "pass-run" else 1
        elif args.cmd == "ci-jobs":
            if args.gate:
                d = ci_gate_decision(_read_json(args.jobs) or [])
                print(json.dumps(d))
                return 0 if d["ok"] else (1 if d["bad"] else 3)
            d = (ci_jobs_failed if args.failed_only else ci_jobs_decision)(_read_json(args.jobs) or [])
            print(json.dumps(d))
            return 0 if d["ok"] else 1
        elif args.cmd == "notes":
            files = build_notes(args.entries_dir, _lines(args.released_list) if args.released_list else [], _read(args.base), args.version, args.allow_empty)
            os.makedirs(args.out_dir, exist_ok=True)
            for n, t in files.items():
                with open(os.path.join(args.out_dir, n), "w", encoding="utf-8", newline="\n") as f:
                    f.write(t)
            print("notes written to %s: %d new entries" % (args.out_dir, len([x for x in files["entries.txt"].split("\n") if x])))
        elif args.cmd == "check-entries":
            entries, problems = load_entries(args.entries_dir)
            for pr in problems:
                print("ERROR: " + pr, file=sys.stderr)
            ids = [e["id"] for e in entries]
            print("changelog entries: %d ok, %d problems" % (len(ids), len(problems)))
            return 1 if problems else 0
        elif args.cmd == "stamp":
            with open(args.cmake, encoding="utf-8", newline="") as f:
                text = f.read()
            new = stamp_cmake(text, args.version)
            with open(args.cmake, "w", encoding="utf-8", newline="") as f:
                f.write(new)
            print("CMakeLists.txt now says %s" % args.version)
        elif args.cmd == "sync-docs":
            row = _read(os.path.join(args.notes_dir, "ROADMAP_ROW.txt")).strip()
            block = _read(os.path.join(args.notes_dir, "block.txt"))
            done = _lines(os.path.join(args.notes_dir, "entries.txt"))
            if not row or not block:
                raise Refused("%s has no ROADMAP_ROW.txt / block.txt (run `notes` first)" % args.notes_dir)
            cm = _read(args.cmake)
            rm = insert_roadmap_row(_read(args.roadmap), row)
            cl = merge_changelog(_read(args.changelog), args.version, block)
            stamped = stamp_cmake(cm, args.version)
            for path, text in ((args.cmake, stamped), (args.roadmap, rm), (args.changelog, cl)):
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(text)
            dest = os.path.join(args.entries_dir, "released", args.version)
            moved = 0
            for n in done:
                src = os.path.join(args.entries_dir, n)
                if os.path.isfile(src):
                    os.makedirs(dest, exist_ok=True)
                    os.rename(src, os.path.join(dest, n))
                    moved += 1
            print("synced v%s: CMakeLists.txt, ROADMAP.md, dist/CHANGELOG.txt; %d entries moved to %s" % (args.version, moved, dest))
        elif args.cmd == "verify":
            rows = verify_release(args.version, args.sha, _read_json(args.release) or {}, args.latest_tag, args.supabase_version,
                                  _read_json(args.index) or {},
                                  None if not args.sig_present else args.sig_present == "yes",
                                  None if not args.sig_ok else args.sig_ok == "yes", args.trusted_keys)
            for ok, what in rows:
                print("%s  %s" % ("ok  " if ok else "FAIL", what))
            bad = [w for ok, w in rows if not ok]
            if bad:
                print("\n%d check(s) failed for v%s." % (len(bad), args.version), file=sys.stderr)
                return 1
            print("\nall %d checks passed for v%s." % (len(rows), args.version))
    except Refused as e:
        print("REFUSED: %s" % e, file=sys.stderr)
        return 1
    except (OSError, ValueError) as e:
        print("ERROR: %s" % e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
