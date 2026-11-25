#!/usr/bin/env python3
"""Maintain dev/versions.json, the list of TEST BUILDS (the "dev channel"). Docs: docs/release/release_channels.md ("Test builds").

scripts/publish-supabase-dev.sh calls this. It is the test builds' counterpart of scripts/versions_index.py, and it is kept apart on purpose:
nothing here can write, read or change the list of real releases, and a real release can never be written by this tool.

  dev_index.py merge --version 2.876.0.17 --branch claude/walk-vs-run --sha <commit> --note "try the run speed" --date 2026-11-16 \
      --file OkayEngine.exe=PATH --file OkaySpacePlayer.exe=PATH --file OkaySpace.exe=PATH --out NEW.json \
      [--existing OLD.json] [--bucket-versions "2.876.0.17 2.876.0.16"] [--keep 6] [--max-entries 50]
  dev_index.py notes --version 2.876.0.17 --base 2.876.0 --branch ... --sha ... --note ... --out NOTES-DEV.txt
  dev_index.py summary --version 2.876.0.17 --base 2.876.0 --branch ... --sha ... --note ...        (Markdown for the job summary, on stdout)
  dev_index.py prune-list --existing OLD.json --listing "v2.876.0.15 v2.876.0.16 versions.json DEV.txt" --version 2.876.0.17 --keep 6
  dev_index.py check --index BACK.json --version 2.876.0.17 --file OkayEngine.exe=PATH ...
  dev_index.py --self-test

Exit codes: 0 fine, 1 (check only) the index does not match the files, 2 bad input or a missing checksum, 3 the test version is already listed.

The version of a test build has FOUR numbers (2.876.0.17: the 17th test build made on top of release 2.876.0; shared/TestBuild.hpp explains why).
A version with three numbers is a real release and is refused here. A test build is never replaced: an existing version is exit 3, so a
checksum somebody has already seen cannot change (CLAUDE.md rule 12).

Schema (version 1, the same envelope as the real list so the launcher's reader can parse it; it has no "latest", "lts" or "archive"):
  { "schema": 1, "dev": "2.876.0.17", "bucketPath": "v{version}/{file}",
    "versions": [ { "version": "2.876.0.17", "channel": "dev", "date": "2026-11-16", "inBucket": true,
                    "branch": "claude/walk-vs-run", "sha": "<commit>", "note": "...",
                    "files": { "OkayEngine.exe": { "size": 123, "sha256": "..." } } } ] }
The files live at <bucket>/dev/v<version>/<file>. Versions that were pruned from the bucket stay in the list with inBucket false (their number
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
DEFAULT_KEEP = 6
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


def notes_text(version, base, branch, sha, note):
    """The newest test build's notes: the launcher's "what's new" card shows the first lines."""
    lines = [title(branch, sha)]
    n = clean_line(note)
    if n:
        lines.append(n)
    s = clean_sha(sha)
    lines.append("Built on release %s%s." % (base, " from commit " + s if s else ""))
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
    return {"version": e["version"], "channel": "dev", "date": date, "inBucket": bool(e.get("inBucket", False)),
            "branch": clean_branch(e.get("branch")) or "", "sha": clean_sha(e.get("sha")) or "",
            "note": clean_line(e.get("note")), "files": files}


def _entries_of(existing):
    """version -> cleaned entry, from an existing list (None / wrong schema = no entries)."""
    out = {}
    if isinstance(existing, dict) and existing.get("schema") == SCHEMA and isinstance(existing.get("versions"), list):
        for e in existing["versions"]:
            c = _clean_entry(e)
            if c is not None:
                out[c["version"]] = c
    return out


def merge(existing, version, files, date, branch, sha, note, bucket_versions=None, keep=DEFAULT_KEEP, max_entries=DEFAULT_MAX_ENTRIES):
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

    entries = _entries_of(existing)
    if version in entries:
        raise Exists("test build %s is already listed: a test build is never published twice (its checksums may already be known). "
                     "Start a new test build instead" % version)
    entries[version] = {"version": version, "channel": "dev", "date": date, "inBucket": True, "branch": b, "sha": s,
                        "note": clean_line(note), "files": dict(sorted(files.items()))}

    if bucket_versions is not None:
        present = set(v for v in bucket_versions if parse_test_version(v) is not None)
        present.add(version)
        for v, e in entries.items():
            e["inBucket"] = v in present
    ordered = sorted(entries, key=parse_test_version, reverse=True)
    kept = 0
    for v in ordered:                       # never claim more than `keep` installable builds, newest first
        if entries[v]["inBucket"]:
            kept += 1
            if kept > keep:
                entries[v]["inBucket"] = False
    ordered = ordered[:max_entries]
    versions = [entries[v] for v in ordered]
    newest = next((e["version"] for e in versions if e["inBucket"]), ordered[0])
    return {"schema": SCHEMA, "dev": newest, "bucketPath": BUCKET_PATH, "versions": versions}


def prune_list(existing, listing, version, keep=DEFAULT_KEEP):
    """The versions to delete from the bucket's dev/ folder after publishing `version`: everything listed that is not among the newest `keep`
    indexed builds (the new one counts). A folder that is not in the list at all (the leftovers of a publish that stopped half way) goes too.
    Only well-formed test versions are ever returned, never `version` itself."""
    listed = set()
    for name in listing:
        n = name[1:] if isinstance(name, str) and name.startswith("v") else None
        if n is not None and parse_test_version(n) is not None:
            listed.add(n)
    indexed = set(_entries_of(existing))
    candidates = (indexed & listed) | {version}
    keep_set = set(sorted(candidates, key=parse_test_version, reverse=True)[:keep])
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
        out = merge(existing, a.version, files, a.date, a.branch, a.sha, a.note, bucket, a.keep, a.max_entries)
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
    _write_atomic(a.out, notes_text(a.version, a.base, a.branch, a.sha, a.note))
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
    for v in prune_list(existing, a.listing.split(), a.version, a.keep):
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

    # ---- notes
    nt = notes_text("2.876.0.17", "2.876.0", "claude/walk-vs-run", c, "try the new run speed")
    ok(nt.splitlines()[0] == "Test build of walk-vs-run (0123456)" and "try the new run speed" in nt and "release 2.876.0" in nt and nt.endswith("\n"), "notes text")
    ok("\x01" not in notes_text("2.876.0.17", "2.876.0", "main", c, "a\x01b") and notes_text("2.876.0.17", "2.876.0", "main", c, "").count("\n") == 3, "notes with no note")

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
    m.add_argument("--out", required=True)
    n = sub.add_parser("notes")
    n.add_argument("--version", required=True)
    n.add_argument("--base", required=True)
    n.add_argument("--branch", required=True)
    n.add_argument("--sha", required=True)
    n.add_argument("--note", default="")
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
