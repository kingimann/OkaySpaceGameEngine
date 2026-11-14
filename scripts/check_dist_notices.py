#!/usr/bin/env python3
"""Fail if a release staging folder or archive does not carry the third-party notices.

  python3 scripts/check_dist_notices.py <folder-or-.zip> [--strict]   check a staging folder (build-win-dist) or a zip
  python3 scripts/check_dist_notices.py --selftest                    build a good and a bad folder and check both

A good package has THIRD_PARTY_NOTICES.txt at its top (a zip may have one wrapping folder), the same bytes as the
repository's file, and every licence file listed by `gen_third_party_notices.py --list-files` under Licenses/ with
the same bytes as the repository's file. --strict also fails while any shipped item still has no licence text.
It lists files only; it never runs the release.
"""
import os
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import gen_third_party_notices as g  # noqa: E402


def norm(b):
    return b.replace(b"\r\n", b"\n")


def read_package(path):
    """Return {relative path with / separators: bytes} for a folder or zip, minus one wrapping folder in a zip."""
    files = {}
    if os.path.isdir(path):
        for d, _, names in os.walk(path):
            for n in names:
                full = os.path.join(d, n)
                with open(full, "rb") as f:
                    files[os.path.relpath(full, path).replace(os.sep, "/")] = f.read()
        return files
    with zipfile.ZipFile(path) as z:
        names = [n for n in z.namelist() if not n.endswith("/")]
        top = {n.split("/", 1)[0] for n in names}
        strip = ""
        if "THIRD_PARTY_NOTICES.txt" not in names and len(top) == 1:
            strip = top.pop() + "/"
        for n in names:
            files[n[len(strip):] if n.startswith(strip) else n] = z.read(n)
    return files


def check(path, strict=False):
    problems = []
    try:
        files = read_package(path)
    except (OSError, zipfile.BadZipFile) as e:
        return ["cannot read %s: %s" % (path, e)]
    items = g.load()
    with open(g.OUT, "rb") as f:
        want = norm(f.read())
    got = files.get("THIRD_PARTY_NOTICES.txt")
    if got is None:
        problems.append("THIRD_PARTY_NOTICES.txt is missing from the package top")
    elif not got.strip():
        problems.append("THIRD_PARTY_NOTICES.txt is empty")
    elif norm(got) != want:
        problems.append("THIRD_PARTY_NOTICES.txt differs from the repository's (stale copy)")
    for it in items:
        for p in it["licence_files"]:
            name = "Licenses/" + g.staged_name(p)
            with open(os.path.join(g.ROOT, p), "rb") as f:
                src = norm(f.read())
            if name not in files:
                problems.append("licence file missing: %s (from %s, for %s)" % (name, p, it["name"]))
            elif norm(files[name]) != src:
                problems.append("licence file differs from the repository's: %s" % name)
    if strict:
        for it in items:
            if not it["licence_files"] and "no_text_needed" not in it:
                problems.append("strict: no licence text for %s" % it["name"])
    return problems


def selftest():
    py = sys.executable
    with tempfile.TemporaryDirectory() as tmp:
        good = os.path.join(tmp, "good")
        os.makedirs(good)
        subprocess.check_call([py, os.path.join(ROOT, "scripts", "gen_third_party_notices.py"), "--stage", good],
                              stdout=subprocess.DEVNULL)
        errs = check(good)
        if errs:
            print("selftest FAILED: a staged folder was rejected: %s" % errs)
            return 1
        # a zip with one wrapping folder, as release.yml makes it
        zp = os.path.join(tmp, "good.zip")
        with zipfile.ZipFile(zp, "w") as z:
            for d, _, names in os.walk(good):
                for n in names:
                    full = os.path.join(d, n)
                    z.write(full, "OkaySpace-windows-vTEST/" + os.path.relpath(full, good).replace(os.sep, "/"))
        errs = check(zp)
        if errs:
            print("selftest FAILED: a good zip was rejected: %s" % errs)
            return 1
        # bad 1: no notices file
        os.remove(os.path.join(good, "THIRD_PARTY_NOTICES.txt"))
        if not check(good):
            print("selftest FAILED: a folder without THIRD_PARTY_NOTICES.txt was accepted")
            return 1
        # bad 2: a licence file missing
        subprocess.check_call([py, os.path.join(ROOT, "scripts", "gen_third_party_notices.py"), "--stage", good],
                              stdout=subprocess.DEVNULL)
        lic = os.path.join(good, "Licenses")
        victim = sorted(os.listdir(lic))[0]
        os.remove(os.path.join(lic, victim))
        if not check(good):
            print("selftest FAILED: a folder with a licence file missing was accepted")
            return 1
        # bad 3: empty folder
        empty = os.path.join(tmp, "empty")
        os.makedirs(empty)
        if not check(empty):
            print("selftest FAILED: an empty folder was accepted")
            return 1
    print("selftest passed: good folder and zip accepted; missing notices, missing licence file and empty folder rejected")
    return 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    args = [a for a in argv if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__)
        return 2
    problems = check(args[0], strict="--strict" in argv)
    for p in problems:
        print("FAIL: " + p)
    if problems:
        return 1
    missing = [it["name"] for it in g.load() if not it["licence_files"] and "no_text_needed" not in it]
    if missing:
        print("warning: the notices still say 'licence text missing' for: " + ", ".join(missing))
    print("OK: %s carries THIRD_PARTY_NOTICES.txt and the licence files" % args[0])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
