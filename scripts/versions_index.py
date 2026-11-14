#!/usr/bin/env python3
"""Maintain versions.json, the index of released OkaySpace builds.

publish-supabase.sh calls this after every publish. The index tells the launcher
which versions exist, which one is the newest ("latest") and which one is the
long-term-support line ("lts"), with the size and SHA-256 of every file so a
download can be checked before it is run. Versions stay in the index after they
are removed from the bucket (inBucket: false); the launcher then falls back to
the GitHub release listed in "archive".

  versions_index.py merge --existing OLD.json --version 2.856.0 --channel latest \
      --file OkayEngine.exe=path/to/OkayEngine.exe --file OkaySpacePlayer.exe=... \
      --date 2026-11-09 --bucket-versions "2.856.0 2.855.0" --latest 2.856.0 \
      --lts 2.850.1 --repo owner/name --out NEW.json
  versions_index.py --self-test

Schema (version 1):
  { "schema": 1, "latest": "2.856.0", "lts": "2.850.1",
    "archive": "https://github.com/owner/name/releases/download/v{version}/{file}",
    "bucketPath": "v{version}/{file}",
    "versions": [ { "version": "2.856.0", "channel": "latest", "date": "2026-11-09",
                    "inBucket": true,
                    "files": { "OkayEngine.exe": { "size": 123, "sha256": "..." } } } ] }
Versions are sorted newest first. "channel" is the channel a version was published on.
"""
import argparse
import hashlib
import json
import os
import re
import sys

SCHEMA = 1
CHANNELS = ("latest", "lts")
BUCKET_PATH = "v{version}/{file}"
_VERSION_RE = re.compile(r"^v?(\d+)(?:\.(\d+))?(?:\.(\d+))?$")
_FILE_RE = re.compile(r"^[A-Za-z0-9._-]+$")
_SHA_RE = re.compile(r"^[0-9a-f]{64}$")
# The files the launcher's updater downloads. It REFUSES to install a file that has no checksum in the index
# (launcher/UpdaterCore.hpp), so a publish that cannot list a size and SHA-256 for every one of these must fail.
REQUIRED_FILES = ("OkayEngine.exe", "OkaySpacePlayer.exe", "OkaySpace.exe")


def parse_version(text):
    """'2.855.0' / 'v2.855' / '2' -> (2, 855, 0); anything else -> None."""
    if not isinstance(text, str):
        return None
    m = _VERSION_RE.match(text.strip())
    if not m:
        return None
    parts = [int(g) if g is not None else 0 for g in m.groups()]
    return tuple(parts)


def canonical(text):
    v = parse_version(text)
    return None if v is None else "%d.%d.%d" % v


def line_of(text):
    """The build line: '2.850.1' -> '2.850'."""
    v = parse_version(text)
    return None if v is None else "%d.%d" % v[:2]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def file_entry(path):
    return {"size": os.path.getsize(path), "sha256": sha256_file(path)}


def _clean_entry(e):
    """An entry from an existing index, or None if it is unusable."""
    if not isinstance(e, dict):
        return None
    ver = canonical(e.get("version"))
    if ver is None:
        return None
    files = {}
    raw = e.get("files")
    if isinstance(raw, dict):
        for name, info in raw.items():
            if not isinstance(name, str) or not _FILE_RE.match(name) or not isinstance(info, dict):
                continue
            size, sha = info.get("size"), info.get("sha256")
            if isinstance(size, int) and not isinstance(size, bool) and size >= 0 \
                    and isinstance(sha, str) and _SHA_RE.match(sha):
                files[name] = {"size": size, "sha256": sha}
    channel = e.get("channel") if e.get("channel") in CHANNELS else "latest"
    date = e.get("date") if isinstance(e.get("date"), str) else ""
    return {"version": ver, "channel": channel, "date": date,
            "inBucket": bool(e.get("inBucket", False)), "files": files}


def missing_checksums(files, required=REQUIRED_FILES):
    """The required file names that lack a size or a 64-hex SHA-256 in `files` (a name -> {size, sha256} dict)."""
    bad = []
    for name in required:
        info = files.get(name) if isinstance(files, dict) else None
        ok = isinstance(info, dict) and isinstance(info.get("sha256"), str) and _SHA_RE.match(info["sha256"]) \
            and isinstance(info.get("size"), int) and not isinstance(info.get("size"), bool) and info["size"] > 0
        if not ok:
            bad.append(name)
    return bad


def merge(existing, version, channel, files, date, bucket_versions, latest, lts, repo, required=REQUIRED_FILES):
    """Return the new index (a dict). `existing` may be None or garbage.
    Raises ValueError unless `files` has a size and SHA-256 for every name in `required`."""
    ver = canonical(version)
    if ver is None:
        raise ValueError("bad version: %r" % (version,))
    if channel not in CHANNELS:
        raise ValueError("bad channel: %r" % (channel,))
    for name, info in files.items():
        if not _FILE_RE.match(name):
            raise ValueError("bad file name: %r" % (name,))
        if not _SHA_RE.match(info.get("sha256", "")):
            raise ValueError("bad sha256 for %s" % name)

    lacking = missing_checksums(files, required)
    if lacking:
        raise ValueError("no checksum for %s: the updater refuses files without one, so this release cannot be published"
                         % ", ".join(lacking))

    old = existing if isinstance(existing, dict) and existing.get("schema") == SCHEMA else {}
    entries = {}
    for e in old.get("versions", []) if isinstance(old.get("versions"), list) else []:
        c = _clean_entry(e)
        if c is not None:
            entries[c["version"]] = c
    entries[ver] = {"version": ver, "channel": channel, "date": date or "",
                    "inBucket": True, "files": dict(sorted(files.items()))}

    if bucket_versions is not None:
        present = set(filter(None, (canonical(v) for v in bucket_versions)))
        present.add(ver)
        for v, e in entries.items():
            e["inBucket"] = v in present

    def pick(new, key):
        if new:
            c = canonical(new)
            if c is None:
                raise ValueError("bad %s version: %r" % (key, new))
            return c
        c = canonical(old.get(key))
        return c

    out = {"schema": SCHEMA}
    lat, lt = pick(latest, "latest"), pick(lts, "lts")
    if lat:
        out["latest"] = lat
    if lt:
        out["lts"] = lt
    out["archive"] = "https://github.com/%s/releases/download/v{version}/{file}" % repo
    out["bucketPath"] = BUCKET_PATH
    out["versions"] = [entries[v] for v in sorted(entries, key=parse_version, reverse=True)]
    return out


def _read_json(path):
    if not path or not os.path.isfile(path):
        return None
    try:
        with open(path, "r", encoding="utf-8-sig") as f:
            return json.load(f)
    except (OSError, ValueError) as err:
        sys.stderr.write("versions_index: ignoring unreadable %s (%s)\n" % (path, err))
        return None


def _cmd_check(a):
    """Fail (exit 1) unless the index in --index lists, for --version, the size and SHA-256 of every --file as it is on disk."""
    data = _read_json(a.index)
    ver = canonical(a.version)
    entry = None
    if isinstance(data, dict) and data.get("schema") == SCHEMA and isinstance(data.get("versions"), list):
        entry = next((e for e in data["versions"] if isinstance(e, dict) and canonical(e.get("version")) == ver), None)
    if entry is None:
        sys.stderr.write("versions_index: %s does not list v%s\n" % (a.index, a.version))
        return 1
    listed = entry.get("files") if isinstance(entry.get("files"), dict) else {}
    problems = ["%s is missing, or has no valid size/SHA-256" % n for n in missing_checksums(listed)]
    for spec in a.file or []:
        name, _, path = spec.partition("=")
        if name in listed and os.path.isfile(path) and listed[name] != file_entry(path) and name not in [x.split()[0] for x in problems]:
            problems.append("%s: the index does not match the file that was uploaded" % name)
    for p in problems:
        sys.stderr.write("versions_index: %s\n" % p)
    return 1 if problems else 0


def _cmd_merge(a):
    files = {}
    for spec in a.file or []:
        name, sep, path = spec.partition("=")
        if not sep or not os.path.isfile(path):
            sys.stderr.write("versions_index: bad --file %r\n" % spec)
            return 2
        files[name] = file_entry(path)
    bucket = a.bucket_versions.split() if a.bucket_versions is not None else None
    try:
        out = merge(_read_json(a.existing), a.version, a.channel, files, a.date,
                    bucket, a.latest, a.lts, a.repo)
    except ValueError as err:
        sys.stderr.write("versions_index: %s\n" % err)
        return 2
    text = json.dumps(out, indent=2, sort_keys=False) + "\n"
    tmp = a.out + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, a.out)
    return 0


# ---- self test --------------------------------------------------------------

def _self_test():
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)

    check(parse_version("2.855.0") == (2, 855, 0), "parse full")
    check(parse_version("v2.855") == (2, 855, 0), "parse v-prefix and short")
    check(parse_version("2") == (2, 0, 0), "parse major only")
    for bad in ("", "dev", "2.x", "2.855.0.1", "..", None, 5, "2.-1.0"):
        check(parse_version(bad) is None, "parse rejects %r" % (bad,))
    check(canonical(" v2.850.1 ") == "2.850.1", "canonical trims")
    check(line_of("2.850.1") == "2.850" and line_of("junk") is None, "line_of")
    check(parse_version("2.9.0") < parse_version("2.10.0"), "numeric compare, not text")

    sha = "a" * 64
    f1 = {n: {"size": 10, "sha256": sha} for n in REQUIRED_FILES}
    idx = merge(None, "2.855.0", "latest", f1, "2026-11-09", ["2.855.0"], "2.855.0", None, "o/n")
    check(idx["schema"] == 1 and idx["latest"] == "2.855.0" and "lts" not in idx, "first index")
    check(idx["archive"] == "https://github.com/o/n/releases/download/v{version}/{file}", "archive template")
    check([v["version"] for v in idx["versions"]] == ["2.855.0"], "one version")

    idx2 = merge(idx, "2.856.0", "latest", f1, "2026-11-10", ["2.856.0", "2.855.0"], "2.856.0", None, "o/n")
    check([v["version"] for v in idx2["versions"]] == ["2.856.0", "2.855.0"], "sorted newest first")
    check(idx2["latest"] == "2.856.0", "latest moves")

    # An LTS patch must not move "latest"; "lts" is set.
    idx3 = merge(idx2, "2.850.1", "lts", f1, "2026-11-11", ["2.856.0", "2.855.0", "2.850.1"], "2.856.0", "2.850.1", "o/n")
    check(idx3["latest"] == "2.856.0" and idx3["lts"] == "2.850.1", "lts publish keeps latest")
    check(idx3["versions"][-1]["version"] == "2.850.1" and idx3["versions"][-1]["channel"] == "lts", "lts entry")

    # Pruned from the bucket: stays in the index, inBucket false, hashes kept.
    idx4 = merge(idx3, "2.857.0", "latest", f1, "2026-11-12", ["2.857.0", "2.856.0", "2.850.1"], "2.857.0", None, "o/n")
    old = [v for v in idx4["versions"] if v["version"] == "2.855.0"][0]
    check(old["inBucket"] is False and old["files"]["OkayEngine.exe"]["sha256"] == sha, "pruned stays indexed")
    check(idx4["lts"] == "2.850.1", "lts kept when not passed")

    # Re-publishing a version replaces its entry, no duplicates.
    f2 = {n: {"size": 11, "sha256": "b" * 64} for n in REQUIRED_FILES}
    idx5 = merge(idx4, "2.857.0", "latest", f2, "2026-11-12", None, "2.857.0", None, "o/n")
    same = [v for v in idx5["versions"] if v["version"] == "2.857.0"]
    check(len(same) == 1 and same[0]["files"]["OkayEngine.exe"]["size"] == 11, "republish replaces")

    # Garbage in the old index is ignored; good entries in a damaged one survive.
    check(merge("junk", "2.1.0", "latest", f1, "", None, None, None, "o/n")["versions"][0]["version"] == "2.1.0", "garbage index")
    bad = {"schema": 1, "versions": [{"version": "nope"}, 7, {"version": "2.2.0", "files": {"../x": {"size": 1, "sha256": sha},
           "ok.exe": {"size": 1, "sha256": sha}, "bad.exe": {"size": -1, "sha256": sha}}}]}
    m = merge(bad, "2.3.0", "latest", f1, "", None, None, None, "o/n")
    kept = [v for v in m["versions"] if v["version"] == "2.2.0"]
    check(len(kept) == 1 and list(kept[0]["files"]) == ["ok.exe"], "unusable entries and files dropped")
    check(merge({"schema": 2, "versions": [{"version": "2.2.0"}]}, "2.3.0", "latest", f1, "", None, None, None, "o/n")["versions"][0]["version"] == "2.3.0"
          and len(merge({"schema": 2, "versions": [{"version": "2.2.0"}]}, "2.3.0", "latest", f1, "", None, None, None, "o/n")["versions"]) == 1,
          "unknown schema starts fresh")

    for args, what in (
        (("dev", "latest", f1), "bad version"),
        (("2.1.0", "beta", f1), "bad channel"),
        (("2.1.0", "latest", {"a/b": {"sha256": sha}}), "bad file name"),
        (("2.1.0", "latest", {"a": {"sha256": "xyz"}}), "bad sha"),
    ):
        try:
            merge(None, args[0], args[1], args[2], "", None, None, None, "o/n")
            check(False, "rejects " + what)
        except ValueError:
            pass
    try:
        merge(None, "2.1.0", "latest", f1, "", None, "garbage", None, "o/n")
        check(False, "rejects bad latest")
    except ValueError:
        pass

    # The updater needs a checksum for every file: a publish without one is refused.
    for lack, what in ((dict(list(f1.items())[:2]), "a file missing"),
                       ({**f1, "OkaySpace.exe": {"size": 10, "sha256": ""}}, "an empty sha"),
                       ({**f1, "OkaySpace.exe": {"size": 0, "sha256": sha}}, "a zero size"),
                       ({**f1, "OkaySpace.exe": {"sha256": sha}}, "no size"),
                       ({}, "no files")):
        try:
            merge(None, "2.1.0", "latest", lack, "", None, None, None, "o/n")
            check(False, "refuses " + what)
        except ValueError as err:
            check("checksum" in str(err) or "sha256" in str(err), "says why for " + what)

    # Round trip through JSON text and the CLI on real files.
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "OkayEngine.exe")
        with open(p, "wb") as fh:
            fh.write(b"hello")
        paths = {}
        for n in REQUIRED_FILES:
            paths[n] = os.path.join(d, "src_" + n)
            with open(paths[n], "wb") as fh:
                fh.write(b"hello")
        out = os.path.join(d, "v.json")
        a = argparse.Namespace(existing=os.path.join(d, "none.json"), version="2.9.0", channel="latest",
                               file=[n + "=" + paths[n] for n in REQUIRED_FILES], date="2026-01-01", bucket_versions="2.9.0",
                               latest="2.9.0", lts=None, repo="o/n", out=out)
        check(_cmd_merge(a) == 0, "cli merge runs")
        data = _read_json(out)
        check(data and data["versions"][0]["files"]["OkayEngine.exe"] ==
              {"size": 5, "sha256": hashlib.sha256(b"hello").hexdigest()}, "cli hashes the file")
        check(not os.path.exists(out + ".tmp"), "no temp file left")
        with open(out, "w", encoding="utf-8") as fh:
            fh.write("{ not json")
        check(_read_json(out) is None, "unreadable json ignored")
        a.existing = out
        check(_cmd_merge(a) == 0, "cli merge over a damaged index")
        # check: the written index matches the files; a missing hash or a different file fails
        ca = argparse.Namespace(index=out, version="2.9.0", file=[n + "=" + paths[n] for n in REQUIRED_FILES])
        check(_cmd_check(ca) == 0, "check passes on a good index")
        ca.version = "9.9.9"
        check(_cmd_check(ca) == 1, "check fails for an unlisted version")
        ca.version = "2.9.0"
        with open(paths["OkaySpace.exe"], "wb") as fh:
            fh.write(b"tampered")
        check(_cmd_check(ca) == 1, "check fails when the uploaded file differs")
        broken = _read_json(out)
        del broken["versions"][0]["files"]["OkaySpacePlayer.exe"]
        with open(out, "w", encoding="utf-8") as fh:
            json.dump(broken, fh)
        ca.file = []
        check(_cmd_check(ca) == 1, "check fails when the index lacks a file")
        ca.index = os.path.join(d, "nope.json")
        check(_cmd_check(ca) == 1, "check fails without an index")
        a.file = ["Missing.exe=" + os.path.join(d, "nope")]
        check(_cmd_merge(a) == 2, "cli refuses a missing file")

    for f in fails:
        sys.stderr.write("FAIL: %s\n" % f)
    print("versions_index self-test: %s (%d failures)" % ("FAILED" if fails else "ok", len(fails)))
    return 1 if fails else 0


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--self-test", action="store_true")
    sub = p.add_subparsers(dest="cmd")
    c = sub.add_parser("check")
    c.add_argument("--index", required=True)
    c.add_argument("--version", required=True)
    c.add_argument("--file", action="append")
    m = sub.add_parser("merge")
    m.add_argument("--existing", default="")
    m.add_argument("--version", required=True)
    m.add_argument("--channel", required=True, choices=CHANNELS)
    m.add_argument("--file", action="append")
    m.add_argument("--date", default="")
    m.add_argument("--bucket-versions", default=None)
    m.add_argument("--latest", default="")
    m.add_argument("--lts", default="")
    m.add_argument("--repo", required=True)
    m.add_argument("--out", required=True)
    a = p.parse_args(argv)
    if a.self_test:
        return _self_test()
    if a.cmd == "check":
        return _cmd_check(a)
    if a.cmd == "merge":
        return _cmd_merge(a)
    p.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
