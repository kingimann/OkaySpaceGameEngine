#!/usr/bin/env python3
"""Maintain dev/versions.json, the list of TEST BUILDS (the "dev channel"). Docs: docs/release/release_channels.md ("Test builds").

scripts/publish-supabase-dev.sh calls this. It is the test builds' counterpart of scripts/versions_index.py, and it is kept apart on purpose:
nothing here can write, read or change the list of real releases, and a real release can never be written by this tool.

  dev_index.py merge --version 2.876.0.17 --branch claude/walk-vs-run --sha <commit> --note "try the run speed" --date 2026-11-16 \
      --file OkayEngine.exe=PATH --file OkaySpacePlayer.exe=PATH --file OkaySpace.exe=PATH --out NEW.json \
      [--existing OLD.json] [--bucket-versions "2.876.0.17 2.876.0.16"] [--keep 3] [--max-entries 50]
  dev_index.py notes --version 2.876.0.17 --base 2.876.0 --branch ... --sha ... --note ... --out NOTES-DEV.txt
  dev_index.py summary --version 2.876.0.17 --base 2.876.0 --branch ... --sha ... --note ...        (Markdown for the job summary, on stdout)
  dev_index.py prune-list --existing OLD.json --listing "v2.876.0.15 v2.876.0.16 versions.json DEV.txt" --version 2.876.0.17 --keep 3 [--branch main]
  dev_index.py check --index BACK.json --version 2.876.0.17 --file OkayEngine.exe=PATH ...
  dev_index.py --self-test

Exit codes: 0 fine, 1 (check only) the index does not match the files, 2 bad input or a missing checksum, 3 the test version is already listed.

The version of a test build has FOUR numbers (2.876.0.17: the 17th test build made on top of release 2.876.0; shared/TestBuild.hpp explains why).
A version with three numbers is a real release and is refused here. A test build is never replaced: an existing version is exit 3, so a
checksum somebody has already seen cannot change (CLAUDE.md rule 12).

Schema (version 1, the same envelope as the real list so the launcher's reader can parse it; it has no "latest", "lts" or "archive"):
  { "schema": 1, "dev": "2.876.0.17", "bucketPath": "v{version}/{file}",
    "versions": [ { "version": "2.876.0.17", "channel": "dev", "date": "2026-11-16", "inBucket": true,
                    "branch": "claude/walk-vs-run", "sha": "<commit>", "note": "...", "mainCount": 1500, "mainSha": "<main commit>",
                    "files": { "OkayEngine.exe": { "size": 123, "sha256": "..." } } } ] }
"mainCount" / "mainSha" (optional, --main-count / --main-sha): the build's place on main (git rev-list --count of the main commit it was
built from or on top of); missing = not known. The files live at <bucket>/dev/v<version>/<file>. Versions that were pruned from the bucket stay in the list with inBucket false (their number
is never used again) and are never offered. Written from the plain description above; standard library only.
"""
import argparse
import hashlib
import json
import os
import re
import sys

SCHEMA = 1
BUCKET_PATH = "v{version}/{file}"
REQUIRED_FILES = ("OkayEngine.exe", "OkaySpacePlayer.exe", "OkaySpace.exe")
DEFAULT_KEEP = 3
MAIN_BRANCH = "main"        # the only branch the launcher's automatic test-build update follows (okay::testbuild::kMainBranch)
MAX_KEEP = 10
DEFAULT_MAX_ENTRIES = 50
MAX_BRANCH_CHARS = 80
MAX_NOTE_CHARS = 140

_NUM = r"(?:0|[1-9][0-9]*)"
_TEST_RE = re.compile(r"^(%s)\.(%s)\.(%s)\.([1-9][0-9]*)$" % (_NUM, _NUM, _NUM))
_BASE_RE = re.compile(r"^(%s)\.(%s)\.(%s)$" % (_NUM, _NUM, _NUM))
_FILE_RE = re.compile(r"^[A-Za-z0-9._-]+$")
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_DATE_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}$")
_BRANCH_RE = re.compile(r"^[A-Za-z0-9._/+-]+$")


class Refused(Exception):
    """The input is not acceptable (exit 2)."""


class Exists(Exception):
    """The test version is already listed (exit 3)."""


# ---- the words ---------------------------------------------------------------------------------------------------------------------

def parse_test_version(text):
    """'2.876.0.17' -> (2, 876, 0, 17); anything else (three numbers, a fourth of 0, a fifth number, a suffix, leading zeros) -> None."""
    if not isinstance(text, str):
        return None
    m = _TEST_RE.match(text)
    return tuple(int(g) for g in m.groups()) if m else None


def base_of(version):
    """'2.876.0.17' -> '2.876.0'."""
    t = parse_test_version(version)
    return None if t is None else "%d.%d.%d" % t[:3]


def is_base_release(text):
    return isinstance(text, str) and _BASE_RE.match(text) is not None


def clean_line(text, max_chars=MAX_NOTE_CHARS):
    """One line of plain printable ASCII: other characters dropped, runs of spaces collapsed, ends trimmed, at most max_chars long."""
    out = []
    size = 0
    space = False
    for ch in text if isinstance(text, str) else "":
        c = ord(ch)
        if ch in " \t":
            space = size > 0
            continue
        if c < 0x21 or c > 0x7E:
            continue
        if size + (2 if space else 1) > max_chars:
            break
        if space:
            out.append(" ")
            size += 1
            space = False
        out.append(ch)
        size += 1
    return "".join(out)


def clean_branch(text):
    """A git branch or tag name as a label: letters, digits and . _ / + - only, at most 80 characters; None when it is not."""
    if not isinstance(text, str) or not text or len(text) > MAX_BRANCH_CHARS or not _BRANCH_RE.match(text):
        return None
    if ".." in text or text[0] in "/-." or text[-1] == "/":
        return None
    return text


def clean_sha(text):
    """A commit id: 7 to 40 hex digits (upper case folded to lower); None when it is not."""
    if not isinstance(text, str) or not 7 <= len(text) <= 40:
        return None
    t = text.lower()
    return t if re.match(r"^[0-9a-f]+$", t) else None


# The Claudes' branch folders, longest first. Read from docs/lanes.json (the "lanes" map) when this runs from a checkout; the
# fixed list is the fallback. shared/TestBuild.hpp DisplayBranch() has the same list (scripts/tests/test_collab.py checks both).
LANE_PREFIXES_FALLBACK = ("claude2/", "claude3/", "claude/")


def lane_prefixes():
    try:
        with open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "lanes.json"), encoding="utf-8") as f:
            got = [v["prefix"] for v in json.load(f).get("lanes", {}).values() if isinstance(v, dict) and v.get("prefix")]
        if got:
            return tuple(sorted(set(got), key=len, reverse=True))
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return LANE_PREFIXES_FALLBACK


def display_branch(branch):
    """The Claudes' branch folders ('claude/', 'claude2/', 'claude3/') are dropped: 'claude/walk-vs-run' -> 'walk-vs-run'."""
    for prefix in lane_prefixes():
        if branch.startswith(prefix) and len(branch) > len(prefix):
            return branch[len(prefix):]
    return branch


def title(branch, sha):
    """'Test build of walk-vs-run (abc1234)'. The same text shared/TestBuild.hpp Title() makes; a test compares them."""
    b = clean_branch(branch)
    s = clean_sha(sha)
    out = "Test build"
    if b:
        out += " of " + display_branch(b)
    if s:
        out += " (" + s[:7] + ")"
    return out


MAX_CHANGE_LINES = 50        # the launcher keeps 60 lines per build (launcher/PageLayout.hpp kNotesMaxLines); title, note and footer use the rest
MAX_CHANGE_CHARS = 400       # one line at most (kNotesMaxLineBytes)


def change_lines(changelog_text, base):
    """The detailed "what's in this build" list: the bullets of dist/CHANGELOG.txt's block for the base version (the version the
    test builds are on), read by fast_release.changelog_entry (one reader for both). Control characters are dropped, each line is
    cut to MAX_CHANGE_CHARS and the list to MAX_CHANGE_LINES. Owner, 2026-11-26: "makes the whats new more detailed with all the
    updates you added listed"."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from fast_release import changelog_entry
    out = []
    for e in changelog_entry(changelog_text or "", base):
        for b in e["bullets"]:
            t = re.sub(r"[\x00-\x1f\x7f]", " ", b).strip()
            if not t:
                continue
            if len(t) > MAX_CHANGE_CHARS:
                t = t[:MAX_CHANGE_CHARS - 3].rstrip() + "..."
            out.append(t)
    return out[:MAX_CHANGE_LINES]


def notes_text(version, base, branch, sha, note, changes=None):
    """The newest test build's notes: the launcher's "what's new" card shows the first lines, then "Show all"."""
    lines = [title(branch, sha)]
    n = clean_line(note)
    if n:
        lines.append(n)
    s = clean_sha(sha)
    lines.append("Built on release %s%s." % (base, " from commit " + s if s else ""))
    if changes:
        lines.append("What's in this build:")
        lines += ["- " + c for c in changes]
    lines.append("A test build for trying out unfinished work. It may be broken. Turn 'Get test builds' off in the launcher's Settings to stop getting these.")
    return "\n".join(lines) + "\n"


def md_escape(text):
    """Text that is shown in a GitHub job summary (Markdown): everything that could start markup or a link is escaped."""
    return re.sub(r"([\\`*_{}\[\]#|<>&~])", r"\\\1", text)


def summary_text(version, base, branch, sha, note):
    """The job summary of a published test build, in plain words (Markdown). The note is escaped: it is text, never markup."""
    n = clean_line(note)
    lines = ["### Test build published", "", "**v%s** - %s" % (version, md_escape(title(branch, sha)))]
    if n:
        lines.append("> " + md_escape(n))
    lines += [
        "",
        "**Owners:** open the launcher, go to Settings > Behavior and switch **Get test builds** on. The build appears as an update after a minute or so "
        "(click the version number at the top left of the launcher to look right now); press Install now, then Restart now. It may be broken. "
        "Switch Get test builds off again to stop getting them; Settings then offers **Go back to the stable version**.",
        "",
        "Nothing else changed: no GitHub release, no tag, and the normal \"latest\" update list is exactly as it was. "
        "This test build was made on release %s." % base,
    ]
    return "\n".join(lines) + "\n"


# ---- files and entries -------------------------------------------------------------------------------------------------------------

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def file_entry(path):
    return {"size": os.path.getsize(path), "sha256": sha256_file(path)}


def missing_checksums(files):
    """The required names that lack a size above 0 or a 64-hex SHA-256 in `files` (name -> {size, sha256})."""
    bad = []
    for name in REQUIRED_FILES:
        info = files.get(name) if isinstance(files, dict) else None
        ok = (isinstance(info, dict) and isinstance(info.get("sha256"), str) and _SHA256_RE.match(info["sha256"]) is not None
              and isinstance(info.get("size"), int) and not isinstance(info.get("size"), bool) and info["size"] > 0)
        if not ok:
            bad.append(name)
    return bad


def position_fields(count, sha):
    """The test build's place on main as entry fields ({"mainCount": N, "mainSha": "..."}): git rev-list --count of the main commit it was built
    from (a branch build: the main commit it was built on top of) and that commit. The launcher compares it with a release's (scripts/versions_index.py
    writes the same two fields for releases; launcher/UpdaterCore.hpp DecideOffer). None / "" = not given. Refused: a count that is not a whole
    number above 0, a commit that is not a commit id."""
    fields = {}
    if count is not None:
        if isinstance(count, bool) or not isinstance(count, int) or count < 1:
            raise Refused("the place on main %r is not a whole number above 0" % (count,))
        fields["mainCount"] = count
    if sha:
        c = clean_sha(sha)
        if c is None:
            raise Refused("the main commit %r is not 7 to 40 hex digits" % (sha,))
        fields["mainSha"] = c
    return fields


def _clean_entry(e):
    """An entry of an existing list, or None when it is unusable (anything that is not a well-formed test build)."""
    if not isinstance(e, dict) or parse_test_version(e.get("version")) is None:
        return None
    files = {}
    raw = e.get("files")
    if isinstance(raw, dict):
        for name, info in raw.items():
            if not isinstance(name, str) or not _FILE_RE.match(name) or not isinstance(info, dict):
                continue
            size, sha = info.get("size"), info.get("sha256")
            if isinstance(size, int) and not isinstance(size, bool) and size >= 0 and isinstance(sha, str) and _SHA256_RE.match(sha):
                files[name] = {"size": size, "sha256": sha}
    date = e.get("date") if isinstance(e.get("date"), str) and _DATE_RE.match(e.get("date")) else ""
    out = {"version": e["version"], "channel": "dev", "date": date, "inBucket": bool(e.get("inBucket", False)),
           "branch": clean_branch(e.get("branch")) or "", "sha": clean_sha(e.get("sha")) or "",
           "note": clean_line(e.get("note")), "files": files}
    try:   # an old list's place on main: kept when usable, dropped (never refused) when not
        out.update(position_fields(e.get("mainCount"), e.get("mainSha")))
    except Refused:
        pass
    return out


def _entries_of(existing):
    """version -> cleaned entry, from an existing list (None / wrong schema = no entries)."""
    out = {}
    if isinstance(existing, dict) and existing.get("schema") == SCHEMA and isinstance(existing.get("versions"), list):
        for e in existing["versions"]:
            c = _clean_entry(e)
            if c is not None:
                out[c["version"]] = c
    return out


def kept_versions(branch_of, candidates, keep, must=None):
    """Which of `candidates` (test versions) stay in the bucket: `keep` of them, main builds first (owner, 2026-11-26: "updates are installed in
    order"). The launcher's automatic update follows main builds only, so branch builds must never push every main build out of the bucket: the
    newest `keep - 1` main builds and the newest 1 build of any other branch stay (keep 1: the newest main build), and when there are fewer of
    either the rest is filled with the newest of what is left. `must` (the build being published) always stays and uses up its own kind's share.
    `branch_of` maps a version to its branch (missing = not main)."""
    ordered = sorted(set(candidates), key=parse_test_version, reverse=True)
    is_main = lambda v: branch_of.get(v) == MAIN_BRANCH
    other_share = 1 if keep >= 2 else 0
    share = {True: keep - other_share, False: other_share}
    chosen = []
    if must is not None and must in ordered:
        chosen.append(must)
        share[is_main(must)] -= 1
    for v in ordered:
        if v not in chosen and share[is_main(v)] > 0 and len(chosen) < keep:
            chosen.append(v)
            share[is_main(v)] -= 1
    for v in ordered:                       # fewer of one kind than its share: fill with the newest of the rest
        if len(chosen) >= keep:
            break
        if v not in chosen:
            chosen.append(v)
    return set(chosen)


def merge(existing, version, files, date, branch, sha, note, bucket_versions=None, keep=DEFAULT_KEEP, max_entries=DEFAULT_MAX_ENTRIES,
          main_count=None, main_sha=None):
    """Return the new list (a dict). Raises Refused for input that must not be published and Exists when the version is already listed."""
    if parse_test_version(version) is None:
        raise Refused("%r is not a test version: it needs four numbers, the fourth at least 1 (for example 2.876.0.17). Real releases "
                      "are published with scripts/publish-supabase.sh" % (version,))
    b = clean_branch(branch)
    if b is None:
        raise Refused("the branch name %r is not allowed (letters, digits and . _ / + - only, at most %d characters)" % (branch, MAX_BRANCH_CHARS))
    s = clean_sha(sha)
    if s is None:
        raise Refused("the commit id %r is not 7 to 40 hex digits" % (sha,))
    if not isinstance(date, str) or not _DATE_RE.match(date):
        raise Refused("the date %r is not YYYY-MM-DD" % (date,))
    if not isinstance(keep, int) or not 1 <= keep <= MAX_KEEP:
        raise Refused("keep must be 1 to %d" % MAX_KEEP)
    if not isinstance(max_entries, int) or max_entries < keep:
        raise Refused("max-entries must be at least keep")
    for name, info in files.items():
        if not _FILE_RE.match(name):
            raise Refused("bad file name: %r" % (name,))
        if not isinstance(info, dict) or not _SHA256_RE.match(str(info.get("sha256", ""))):
            raise Refused("bad sha256 for %s" % name)
    lacking = missing_checksums(files)
    if lacking:
        raise Refused("no checksum for %s: the updater refuses files without one, so this test build cannot be published" % ", ".join(lacking))
    position = position_fields(main_count, main_sha)

    entries = _entries_of(existing)
    if version in entries:
        raise Exists("test build %s is already listed: a test build is never published twice (its checksums may already be known). "
                     "Start a new test build instead" % version)
    entries[version] = {"version": version, "channel": "dev", "date": date, "inBucket": True, "branch": b, "sha": s,
                        "note": clean_line(note), "files": dict(sorted(files.items()))}
    entries[version].update(position)

    if bucket_versions is not None:
        present = set(v for v in bucket_versions if parse_test_version(v) is not None)
        present.add(version)
        for v, e in entries.items():
            e["inBucket"] = v in present
    ordered = sorted(entries, key=parse_test_version, reverse=True)
    in_bucket = [v for v in ordered if entries[v]["inBucket"]]
    stay = kept_versions({v: entries[v]["branch"] for v in in_bucket}, in_bucket, keep, version)
    for v in in_bucket:                     # never claim more than `keep` installable builds, main builds first (kept_versions)
        if v not in stay:
            entries[v]["inBucket"] = False
    ordered = ordered[:max_entries]
    versions = [entries[v] for v in ordered]
    newest = next((e["version"] for e in versions if e["inBucket"]), ordered[0])
    return {"schema": SCHEMA, "dev": newest, "bucketPath": BUCKET_PATH, "versions": versions}


def prune_list(existing, listing, version, keep=DEFAULT_KEEP, branch=None):
    """The versions to delete from the bucket's dev/ folder after publishing `version` (a build of `branch`): everything listed that is not among
    the `keep` indexed builds that stay, main builds first (kept_versions; the new one counts). A folder that is not in the list at all (the leftovers of a publish that stopped half way) goes too.
    Only well-formed test versions are ever returned, never `version` itself."""
    listed = set()
    for name in listing:
        n = name[1:] if isinstance(name, str) and name.startswith("v") else None
        if n is not None and parse_test_version(n) is not None:
            listed.add(n)
    entries = _entries_of(existing)
    candidates = (set(entries) & listed) | {version}
    branch_of = {v: e["branch"] for v, e in entries.items()}
    branch_of[version] = clean_branch(branch) or ""
    keep_set = kept_versions(branch_of, candidates, keep, version)
    return sorted((v for v in listed if v not in keep_set and v != version), key=parse_test_version)


def check(index, version, files):
    """Problems (a list of plain sentences) between a list read back from the bucket and the files that were uploaded; [] = fine."""
    problems = []
    entry = None
    if isinstance(index, dict) and index.get("schema") == SCHEMA and isinstance(index.get("versions"), list):
        entry = next((e for e in index["versions"] if isinstance(e, dict) and e.get("version") == version), None)
    if entry is None:
        return ["the list does not hold test build %s" % version]
    listed = entry.get("files") if isinstance(entry.get("files"), dict) else {}
    lacking = missing_checksums(listed)
    problems.extend("%s is missing, or has no valid size and SHA-256" % n for n in lacking)
    for name, path in files.items():
        if name in listed and name not in lacking and os.path.isfile(path) and listed[name] != file_entry(path):
            problems.append("%s: the list does not match the file that was uploaded" % name)
    if entry.get("inBucket") is not True:
        problems.append("test build %s is not marked as in the bucket" % version)
    return problems


# ---- command line ------------------------------------------------------------------------------------------------------------------

def _read_json(path):
    """(data, error): data None with error None = no such file; a damaged file is an error, never an empty list."""
    if not path or not os.path.isfile(path):
        return None, None
    try:
        with open(path, "r", encoding="utf-8-sig") as f:
            return json.load(f), None
    except (OSError, ValueError) as err:
        return None, "%s is not readable JSON (%s)" % (path, err)


def _files_from(specs):
    files = {}
    for spec in specs or []:
        name, sep, path = spec.partition("=")
        if not sep or not _FILE_RE.match(name) or not os.path.isfile(path):
            raise Refused("bad --file %r" % (spec,))
        files[name] = file_entry(path)
    return files


def _write_atomic(path, text):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def _die(code, message):
    sys.stderr.write("dev_index: %s\n" % message)
    return code


def _cmd_merge(a):
    try:
        files = _files_from(a.file)
        existing, err = _read_json(a.existing)
        if err:
            raise Refused("the existing test build list is damaged and was left alone: " + err)
        bucket = a.bucket_versions.split() if a.bucket_versions is not None else None
        mc = getattr(a, "main_count", "") or ""
        if mc not in ("", "0") and not re.match(r"^[0-9]{1,12}$", mc):
            raise Refused("--main-count %r is not a number" % (mc,))
        count = int(mc) if mc not in ("", "0") else None
        out = merge(existing, a.version, files, a.date, a.branch, a.sha, a.note, bucket, a.keep, a.max_entries, count, getattr(a, "main_sha", "") or None)
    except Refused as e:
        return _die(2, str(e))
    except Exists as e:
        return _die(3, str(e))
    _write_atomic(a.out, json.dumps(out, indent=2) + "\n")
    return 0


def _cmd_notes(a):
    if parse_test_version(a.version) is None:
        return _die(2, "%r is not a test version" % (a.version,))
    if not is_base_release(a.base) or a.base != base_of(a.version):
        return _die(2, "the base release %r is not the first three numbers of %s" % (a.base, a.version))
    if clean_branch(a.branch) is None or clean_sha(a.sha) is None:
        return _die(2, "the branch or the commit id is not allowed")
    changes = None
    if a.changelog:
        try:
            with open(a.changelog, encoding="utf-8") as f:
                changes = change_lines(f.read(), a.base)
        except OSError as e:
            return _die(2, "cannot read the changelog: %s" % e)
    _write_atomic(a.out, notes_text(a.version, a.base, a.branch, a.sha, a.note, changes))
    return 0


def _cmd_summary(a):
    if parse_test_version(a.version) is None or not is_base_release(a.base) or a.base != base_of(a.version):
        return _die(2, "bad --version or --base")
    sys.stdout.write(summary_text(a.version, a.base, a.branch, a.sha, a.note))
    return 0


def _cmd_prune_list(a):
    existing, err = _read_json(a.existing)
    if err:
        return _die(2, "the existing test build list is damaged, so nothing is pruned: " + err)
    if parse_test_version(a.version) is None or not 1 <= a.keep <= MAX_KEEP:
        return _die(2, "bad --version or --keep")
    for v in prune_list(existing, a.listing.split(), a.version, a.keep, a.branch):
        print(v)
    return 0


def _cmd_check(a):
    data, err = _read_json(a.index)
    if err:
        return _die(1, err)
    try:
        files = {}
        for spec in a.file or []:
            name, _, path = spec.partition("=")
            files[name] = path
    except ValueError:
        return _die(2, "bad --file")
    problems = check(data, a.version, files)
    for p in problems:
        sys.stderr.write("dev_index: %s\n" % p)
    return 1 if problems else 0


# ---- self test ---------------------------------------------------------------------------------------------------------------------

def _self_test():
    import tempfile
    fails = []

    def ok(cond, what):
        if not cond:
            fails.append(what)

    def raises(exc, fn, what):
        try:
            fn()
            fails.append("should raise: " + what)
        except exc:
            pass

    sha = "a" * 64
    f1 = {n: {"size": 10, "sha256": sha} for n in REQUIRED_FILES}
    c = "0123456789abcdef0123456789abcdef01234567"

    ok(parse_test_version("2.876.0.17") == (2, 876, 0, 17), "parse")
    for bad in ("2.876.0", "2.876.0.0", "2.876.0.17.1", "2.876", "v2.876.0.17", "2.876.0.17-rc1", "2.876.0.017", "02.876.0.17", "", "dev", None, 5, "2.876.0.x", " 2.876.0.17"):
        ok(parse_test_version(bad) is None, "parse rejects %r" % (bad,))
    ok(base_of("2.876.0.17") == "2.876.0" and base_of("2.876.0") is None, "base_of")
    ok(clean_line("  try   the \t new run speed \n") == "try the new run speed", "clean_line")
    ok(clean_line("a\x01b\x7fcäd") == "abcd" and len(clean_line("x" * 500)) == MAX_NOTE_CHARS and clean_line("   ") == "", "clean_line drops and trims")
    ok(clean_line("ab cd", 4) == "ab c" and clean_line("ab cd", 3) == "ab", "clean_line cap")
    ok(all(len(clean_line("aa bb cc dd ee ff", n)) <= n for n in range(12)), "clean_line never longer than the cap")
    ok(clean_branch("claude/walk-vs-run") == "claude/walk-vs-run" and clean_branch("release_2.877+rc-1") == "release_2.877+rc-1", "branch ok")
    for bad in ("", "a b", "a;b", "a\"b", "$(x)", "../x", "a..b", "/abs", "-flag", ".hidden", "trailing/", "naïve", "a\nb", "a|b", "a`b`", "x" * 81, None):
        ok(clean_branch(bad) is None, "branch rejects %r" % (bad,))
    ok(clean_sha("ABC1234") == "abc1234" and clean_sha("abc123") is None and clean_sha("abc123g") is None and clean_sha("a" * 41) is None, "sha")
    ok(title("claude/walk-vs-run", "abc1234def") == "Test build of walk-vs-run (abc1234)", "title")
    ok(title("claude2/x", "ABC1234") == "Test build of x (abc1234)" and title("main", c) == "Test build of main (0123456)", "title forms")
    ok(title("claude3/x", "ABC1234") == "Test build of x (abc1234)" and display_branch("claude3/") == "claude3/", "claude3 branches")
    ok(title("claude/", "abc1234") == "Test build (abc1234)" and title("bad branch!", "zzz") == "Test build" and title("claude/walk", "") == "Test build of walk", "title leaves out what is unusable")

    # ---- merge
    idx = merge(None, "2.876.0.17", f1, "2026-11-16", "claude/walk-vs-run", c, "try it")
    ok(idx["schema"] == 1 and idx["dev"] == "2.876.0.17" and idx["bucketPath"] == BUCKET_PATH, "first list")
    ok(set(idx) == {"schema", "dev", "bucketPath", "versions"}, "no latest, lts or archive")
    e = idx["versions"][0]
    ok(e["channel"] == "dev" and e["inBucket"] is True and e["branch"] == "claude/walk-vs-run" and e["sha"] == c and e["note"] == "try it", "entry")
    ok(list(e["files"]) == sorted(REQUIRED_FILES), "files sorted")

    idx2 = merge(idx, "2.876.0.18", f1, "2026-11-16", "main", "abc1234", "", ["2.876.0.17", "2.876.0.18"])
    ok([v["version"] for v in idx2["versions"]] == ["2.876.0.18", "2.876.0.17"] and idx2["dev"] == "2.876.0.18", "newest first")
    idx3 = merge(idx2, "2.876.0.9", f1, "2026-11-16", "main", "abc1234", "")   # numeric order: .9 is older than .17
    ok([v["version"] for v in idx3["versions"]] == ["2.876.0.18", "2.876.0.17", "2.876.0.9"], "numeric order, not text")
    idx4 = merge(idx3, "2.877.0.1", f1, "2026-11-17", "main", "abc1234", "")
    ok(idx4["versions"][0]["version"] == "2.877.0.1" and idx4["dev"] == "2.877.0.1", "a newer base sorts first")

    # The build's place on main (mainCount = git rev-list --count of the main commit it was built from or on top of, mainSha = that commit):
    # written when given, kept by later merges; a list without it stays valid (the launcher reads a missing count as "not known").
    m40 = "d" * 40
    mi = merge(idx4, "2.877.0.2", f1, "2026-11-17", "main", "abc1234", "", main_count=1500, main_sha=m40)
    ok(mi["versions"][0]["mainCount"] == 1500 and mi["versions"][0]["mainSha"] == m40, "mainCount and mainSha written")
    ok("mainCount" not in mi["versions"][1], "an old entry has no mainCount")
    mj = merge(mi, "2.877.0.3", f1, "2026-11-17", "main", "abc1234", "")
    ok(next(v for v in mj["versions"] if v["version"] == "2.877.0.2")["mainCount"] == 1500, "mainCount kept by the next merge")
    ok("mainCount" not in mj["versions"][0], "no count given: none written")
    for badc in (-1, 0, True, "12", 1.5):
        raises(Refused, lambda b=badc: merge(idx4, "2.877.0.2", f1, "2026-11-17", "main", "abc1234", "", main_count=b), "bad mainCount %r" % (badc,))
    raises(Refused, lambda: merge(idx4, "2.877.0.2", f1, "2026-11-17", "main", "abc1234", "", main_count=5, main_sha="nope"), "bad mainSha")
    raises(Exists, lambda: merge(idx, "2.876.0.17", f1, "2026-11-16", "main", "abc1234", ""), "an existing version")
    pruned = merge(idx2, "2.876.0.19", f1, "2026-11-16", "main", "abc1234", "", ["2.876.0.19", "2.876.0.18"])   # .17 was pruned
    raises(Exists, lambda: merge(pruned, "2.876.0.17", f1, "2026-11-16", "main", "abc1234", ""), "a pruned version is never reused")
    ok(next(v for v in pruned["versions"] if v["version"] == "2.876.0.17")["inBucket"] is False, "pruned stays listed, not in the bucket")
    for badv in ("2.876.0", "2.876.0.0", "2.876.0.17.1", "dev", "", "v2.876.0.17"):
        raises(Refused, lambda v=badv: merge(None, v, f1, "2026-11-16", "main", "abc1234", ""), "bad version %r" % badv)
    raises(Refused, lambda: merge(None, "2.876.0.17", f1, "2026-11-16", "a b", "abc1234", ""), "bad branch")
    raises(Refused, lambda: merge(None, "2.876.0.17", f1, "2026-11-16", "main", "xyz", ""), "bad sha")
    raises(Refused, lambda: merge(None, "2.876.0.17", f1, "16-11-2026", "main", "abc1234", ""), "bad date")
    raises(Refused, lambda: merge(None, "2.876.0.17", f1, "2026-11-16", "main", "abc1234", "", keep=0), "keep 0")
    for lack, what in ((dict(list(f1.items())[:2]), "a file missing"), ({**f1, "OkaySpace.exe": {"size": 10, "sha256": ""}}, "an empty sha"),
                       ({**f1, "OkaySpace.exe": {"size": 0, "sha256": sha}}, "a zero size"), ({**f1, "OkaySpace.exe": {"sha256": sha}}, "no size"), ({}, "no files")):
        try:
            merge(None, "2.876.0.17", lack, "2026-11-16", "main", "abc1234", "")
            fails.append("refuses " + what)
        except Refused as err:
            ok("checksum" in str(err) or "sha256" in str(err), "says why for " + what)
    # keep and max-entries
    big = None
    for n in range(1, 8):
        big = merge(big, "2.876.0.%d" % n, f1, "2026-11-16", "main", "abc1234", "", None, 3, 5)
    ok(len(big["versions"]) == 5 and [v["version"] for v in big["versions"]][0] == "2.876.0.7", "at most max-entries listed")
    ok([v["inBucket"] for v in big["versions"]] == [True, True, True, False, False], "never more than keep installable")
    # garbage in the old list is ignored, the good entries survive
    junk = {"schema": 1, "versions": [{"version": "nope"}, 7, {"version": "2.876.0"}, {"version": "2.876.0.4", "inBucket": True, "files": {"../x": {"size": 1, "sha256": sha}, "ok.exe": {"size": 1, "sha256": sha}}}]}
    m = merge(junk, "2.876.0.5", f1, "2026-11-16", "main", "abc1234", "")
    ok([v["version"] for v in m["versions"]] == ["2.876.0.5", "2.876.0.4"] and list(m["versions"][1]["files"]) == ["ok.exe"], "unusable entries dropped")
    ok(len(merge({"schema": 2, "versions": [{"version": "2.876.0.4"}]}, "2.876.0.5", f1, "2026-11-16", "main", "abc1234", "")["versions"]) == 1, "unknown schema starts fresh")
    ok(len(merge("junk", "2.876.0.5", f1, "2026-11-16", "main", "abc1234", "")["versions"]) == 1, "garbage list starts fresh")

    # ---- prune_list
    ex = merge(merge(merge(None, "2.876.0.1", f1, "2026-11-16", "m", "abc1234", ""), "2.876.0.2", f1, "2026-11-16", "m", "abc1234", ""), "2.876.0.3", f1, "2026-11-16", "m", "abc1234", "")
    ok(prune_list(ex, ["v2.876.0.1", "v2.876.0.2", "v2.876.0.3", "versions.json", "DEV.txt", "NOTES-DEV.txt"], "2.876.0.4", 3) == ["2.876.0.1"], "the oldest goes")
    ok(prune_list(ex, ["v2.876.0.1", "v2.876.0.2", "v2.876.0.3"], "2.876.0.4", 5) == [], "nothing goes while there is room")
    ok(prune_list(ex, ["v2.876.0.1", "v2.876.0.3", "v2.876.0.9"], "2.876.0.4", 2) == ["2.876.0.1", "2.876.0.9"], "newest two indexed builds stay, the old one and the leftover go")
    ok(prune_list(ex, ["v2.876.0.1", "v2.876.0.2", "v2.876.0.3", "v2.876.0.77"], "2.876.0.4", 3) == ["2.876.0.1", "2.876.0.77"], "an unlisted leftover goes, whatever its number")
    ok(prune_list(None, ["v2.876.0.5"], "2.876.0.6", 3) == ["2.876.0.5"], "no list yet: every folder is a leftover (nothing can be installed from it)")
    ok("2.876.0.4" not in prune_list(ex, ["v2.876.0.4", "v2.876.0.1"], "2.876.0.4", 1), "the new version is never pruned")
    ok(prune_list(ex, ["v2.876.0", "v2.876", "vdev", "v2.876.0.0", "x2.876.0.1", "dev", "v../x", "v2.876.0.1/../x"], "2.876.0.4", 3) == [], "only well-formed test versions are ever returned")
    ok(prune_list(ex, [], "2.876.0.4", 3) == [], "empty listing")
    # main builds first (owner, 2026-11-26): branch builds never push every main build out of the bucket
    mb = merge(None, "2.903.8.126", f1, "2026-11-26", "main", "abc1234", "")
    for n, br in ((127, "claude/okaycc-29"), (128, "claude2/x"), (129, "claude3/y")):
        mb = merge(mb, "2.903.8.%d" % n, f1, "2026-11-26", br, "abc1234", "")
    listing = ["v2.903.8.126", "v2.903.8.127", "v2.903.8.128"]
    ok(prune_list(mb, listing, "2.903.8.129", 3, "claude3/y") == ["2.903.8.127"], "3 branch builds after a main build: the main build stays")
    ok([(v["version"], v["inBucket"]) for v in mb["versions"]] == [("2.903.8.129", True), ("2.903.8.128", True), ("2.903.8.127", False), ("2.903.8.126", True)],
       "merge keeps the main build installable (only 1 main build: the newest other builds fill the rest)")
    ok(prune_list(mb, listing, "2.903.8.129", 2, "claude3/y") == ["2.903.8.127", "2.903.8.128"], "keep 2: the new branch build and the main build")
    two = merge(merge(None, "2.903.8.1", f1, "2026-11-26", "main", "abc1234", ""), "2.903.8.2", f1, "2026-11-26", "main", "abc1234", "")
    for n in (3, 4, 5):
        two = merge(two, "2.903.8.%d" % n, f1, "2026-11-26", "claude/b", "abc1234", "")
    ok([v["version"] for v in two["versions"] if v["inBucket"]] == ["2.903.8.5", "2.903.8.2", "2.903.8.1"], "the newest 2 main builds and the newest 1 other")
    ok(prune_list(two, ["v2.903.8.1", "v2.903.8.2", "v2.903.8.5"], "2.903.8.6", 3, "main") == ["2.903.8.1"], "a new main build: 2 main + the newest other")
    ok(prune_list(two, ["v2.903.8.1", "v2.903.8.2", "v2.903.8.5"], "2.903.8.6", 3, "claude/c") == ["2.903.8.5"], "a new branch build replaces the older branch build")
    allm = None
    for n in range(1, 6):
        allm = merge(allm, "2.903.8.%d" % n, f1, "2026-11-26", "main", "abc1234", "")
    ok([v["version"] for v in allm["versions"] if v["inBucket"]] == ["2.903.8.5", "2.903.8.4", "2.903.8.3"], "only main builds: the newest 3")
    ok(kept_versions({"2.903.8.1": "main"}, ["2.903.8.1", "2.903.8.2"], 1) == {"2.903.8.1"}, "keep 1: the newest main build")
    ok(kept_versions({"2.903.8.1": "main"}, ["2.903.8.1", "2.903.8.2"], 1, "2.903.8.2") == {"2.903.8.2"}, "keep 1: the build being published always stays")
    ok(kept_versions({}, ["2.903.8.1", "2.903.8.2", "2.903.8.3"], 2) == {"2.903.8.3", "2.903.8.2"}, "no main builds: the newest ones")

    # ---- notes
    nt = notes_text("2.876.0.17", "2.876.0", "claude/walk-vs-run", c, "try the new run speed")
    ok(nt.splitlines()[0] == "Test build of walk-vs-run (0123456)" and "try the new run speed" in nt and "release 2.876.0" in nt and nt.endswith("\n"), "notes text")
    ok("\x01" not in notes_text("2.876.0.17", "2.876.0", "main", c, "a\x01b") and notes_text("2.876.0.17", "2.876.0", "main", c, "").count("\n") == 3, "notes with no note")

    cl = "Changelog\n\nv2.876.0\n  Title line\n  - First change\n    continued here\n  - Second\x01 change\n\nv2.875.9\n  - Older\n"
    ch = change_lines(cl, "2.876.0")
    ok(ch == ["First change continued here", "Second  change"], "changes: the base version's bullets, joined, no control characters")
    ok(change_lines(cl, "2.877.0") == [] and change_lines("", "2.876.0") == [], "changes: no block, no lines")
    ok(len(change_lines("v1.0.0\n" + "".join("  - item %d\n" % i for i in range(80)), "1.0.0")) == MAX_CHANGE_LINES, "changes: capped")
    ok(len(change_lines("v1.0.0\n  - " + "x" * 900 + "\n", "1.0.0")[0]) == MAX_CHANGE_CHARS, "changes: long line cut")
    nt2 = notes_text("2.876.0.17", "2.876.0", "main", c, "", ch).splitlines()
    ok(nt2[2] == "What's in this build:" and nt2[3] == "- First change continued here" and nt2[-1].startswith("A test build"), "notes list the changes before the footer")

    # ---- summary
    sm = summary_text("2.876.0.17", "2.876.0", "claude/walk-vs-run", c, "see [this](http://x.invalid) <b>bold</b> *now*")
    ok("Get test builds" in sm and "v2.876.0.17" in sm and "walk-vs-run" in sm and "Go back to the stable version" in sm and "release 2.876.0" in sm, "summary says what to do")
    ok("[this](" not in sm and "<b>" not in sm and "\\[this\\]" in sm and "\\<b\\>" in sm and "\\*now\\*" in sm, "summary escapes the note")
    ok("\n> " not in summary_text("2.876.0.17", "2.876.0", "main", c, ""), "no quote line without a note")

    # ---- the command line, on real files
    with tempfile.TemporaryDirectory() as d:
        paths = {}
        for n in REQUIRED_FILES:
            paths[n] = os.path.join(d, "src_" + n)
            with open(paths[n], "wb") as fh:
                fh.write(("MZ " + n).encode())
        out = os.path.join(d, "v.json")
        a = argparse.Namespace(existing=os.path.join(d, "none.json"), version="2.876.0.17", branch="claude/walk-vs-run", sha=c, note="n", date="2026-11-16",
                               file=[n + "=" + paths[n] for n in REQUIRED_FILES], bucket_versions=None, keep=3, max_entries=50, out=out)
        ok(_cmd_merge(a) == 0, "cli merge runs")
        data, err = _read_json(out)
        ok(err is None and data["versions"][0]["files"]["OkayEngine.exe"] == {"size": len("MZ OkayEngine.exe"), "sha256": hashlib.sha256(b"MZ OkayEngine.exe").hexdigest()}, "cli hashes the file")
        ok(not os.path.exists(out + ".tmp"), "no temp file left")
        a2 = argparse.Namespace(**vars(a))
        a2.existing = out
        a2.out = os.path.join(d, "again.json")
        ok(_cmd_merge(a2) == 3 and not os.path.exists(a2.out), "cli: an existing version is exit 3 and writes nothing")
        a3 = argparse.Namespace(**vars(a))
        a3.version = "2.876.0"
        a3.out = os.path.join(d, "stable.json")
        ok(_cmd_merge(a3) == 2 and not os.path.exists(a3.out), "cli: a three-number version is exit 2 and writes nothing")
        a4 = argparse.Namespace(**vars(a))
        a4.file = [n + "=" + paths[n] for n in REQUIRED_FILES[:2]]
        a4.out = os.path.join(d, "nosum.json")
        ok(_cmd_merge(a4) == 2 and not os.path.exists(a4.out), "cli: a missing file (no checksum) is exit 2 and writes nothing")
        with open(os.path.join(d, "damaged.json"), "w", encoding="utf-8") as fh:
            fh.write("{ not json")
        a6 = argparse.Namespace(**vars(a))
        a6.version, a6.out, a6.main_count, a6.main_sha = "2.876.0.18", os.path.join(d, "pos.json"), "1500", "d" * 40
        ok(_cmd_merge(a6) == 0 and _read_json(a6.out)[0]["versions"][0]["mainCount"] == 1500, "cli: --main-count is written")
        a6.main_count, a6.out = "12x", os.path.join(d, "pos2.json")
        ok(_cmd_merge(a6) == 2 and not os.path.exists(a6.out), "cli: a --main-count that is not a number is exit 2")
        a5 = argparse.Namespace(**vars(a))
        a5.existing = os.path.join(d, "damaged.json")
        a5.out = os.path.join(d, "fresh.json")
        ok(_cmd_merge(a5) == 2 and not os.path.exists(a5.out), "cli: a damaged existing list is never replaced by a fresh one")
        # check
        ca = argparse.Namespace(index=out, version="2.876.0.17", file=[n + "=" + paths[n] for n in REQUIRED_FILES])
        ok(_cmd_check(ca) == 0, "check passes on a good list")
        ca.version = "2.876.0.99"
        ok(_cmd_check(ca) == 1, "check fails for an unlisted version")
        ca.version = "2.876.0.17"
        with open(paths["OkaySpace.exe"], "wb") as fh:
            fh.write(b"tampered")
        ok(_cmd_check(ca) == 1, "check fails when the uploaded file differs")
        broken = _read_json(out)[0]
        del broken["versions"][0]["files"]["OkaySpacePlayer.exe"]
        with open(out, "w", encoding="utf-8") as fh:
            json.dump(broken, fh)
        ca.file = []
        ok(_cmd_check(ca) == 1, "check fails when the list lacks a file")
        ca.index = os.path.join(d, "nope.json")
        ok(_cmd_check(ca) == 1, "check fails without a list")

    for f in fails:
        sys.stderr.write("FAIL: %s\n" % f)
    print("dev_index self-test: %s (%d failures)" % ("FAILED" if fails else "ok", len(fails)))
    return 1 if fails else 0


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--self-test", action="store_true")
    sub = p.add_subparsers(dest="cmd")
    m = sub.add_parser("merge")
    m.add_argument("--existing", default="")
    m.add_argument("--version", required=True)
    m.add_argument("--branch", required=True)
    m.add_argument("--sha", required=True)
    m.add_argument("--note", default="")
    m.add_argument("--date", required=True)
    m.add_argument("--file", action="append")
    m.add_argument("--bucket-versions", default=None)
    m.add_argument("--keep", type=int, default=DEFAULT_KEEP)
    m.add_argument("--max-entries", type=int, default=DEFAULT_MAX_ENTRIES)
    m.add_argument("--main-count", default="")   # the build's place on main (git rev-list --count); "" or 0 = not known
    m.add_argument("--main-sha", default="")     # the main commit it was built from (or on top of)
    m.add_argument("--out", required=True)
    n = sub.add_parser("notes")
    n.add_argument("--version", required=True)
    n.add_argument("--base", required=True)
    n.add_argument("--branch", required=True)
    n.add_argument("--sha", required=True)
    n.add_argument("--note", default="")
    n.add_argument("--changelog", default="", help="dist/CHANGELOG.txt: list its block for --base as \"What's in this build\"")
    n.add_argument("--out", required=True)
    sm = sub.add_parser("summary")
    sm.add_argument("--version", required=True)
    sm.add_argument("--base", required=True)
    sm.add_argument("--branch", required=True)
    sm.add_argument("--sha", required=True)
    sm.add_argument("--note", default="")
    pl = sub.add_parser("prune-list")
    pl.add_argument("--existing", default="")
    pl.add_argument("--listing", default="")
    pl.add_argument("--version", required=True)
    pl.add_argument("--keep", type=int, default=DEFAULT_KEEP)
    pl.add_argument("--branch", default="")       # the branch of --version (main builds are kept first)
    c = sub.add_parser("check")
    c.add_argument("--index", required=True)
    c.add_argument("--version", required=True)
    c.add_argument("--file", action="append")
    a = p.parse_args(argv)
    if a.self_test:
        return _self_test()
    if a.cmd == "merge":
        return _cmd_merge(a)
    if a.cmd == "notes":
        return _cmd_notes(a)
    if a.cmd == "summary":
        return _cmd_summary(a)
    if a.cmd == "prune-list":
        return _cmd_prune_list(a)
    if a.cmd == "check":
        return _cmd_check(a)
    p.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
