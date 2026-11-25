#!/usr/bin/env python3
"""Checks the test shards made by -DOKAY_TEST_SHARD=K/N (cmake/OkayTestShard.cmake; docs/process/automated_testing.md "CI speed").

  check_test_shards.py BUILD_DIR
      One configured build folder (CI runs this in each shard job after Configure). Fails when:
      - a test ctest would run uses a program of ours (a path in the build folder that ninja knows how to make) that the
        default build ("all") does not make, so the test would fail with "file not found";
      - in a shard build: the tests ctest lists are not exactly the tests okay_test_shard.json gives to this shard, or the
        manifest names a test twice.

  check_test_shards.py --full FULL_DIR SHARD_DIR [SHARD_DIR...]
      Local check: the shard folders together list every test of the unsharded folder exactly once, and each passes the
      one-folder check above. Prints the per-shard counts.

Needs cmake's ctest and ninja on PATH. Reads only; builds nothing.
"""
import json
import os
import re
import subprocess
import sys


def ctest_listing(build):
    out = subprocess.run(["ctest", "--show-only=json-v1"], cwd=build, check=True, capture_output=True, text=True).stdout
    return json.loads(out).get("tests", [])


def ninja_lines(build, *args):
    out = subprocess.run(["ninja", "-C", build, "-t", *args], check=True, capture_output=True, text=True).stdout
    return out.splitlines()


def built_by_all(build):
    """Every file the default build makes or reads: the file nodes of `ninja -t graph all` (edges are drawn as shape=ellipse)."""
    files = set()
    for ln in ninja_lines(build, "graph", "all"):
        m = re.match(r'^"[^"]+" \[label="(.*)"\]$', ln)
        if m:
            files.add(m.group(1))
    return files


def test_commands(build):
    """name -> command words, read from the CTestTestfile.cmake files (ctest --show-only leaves out the command of a test whose
    program is not built yet, which before a build is every one of them)."""
    commands = {}
    for root, _dirs, files in os.walk(build):
        if "CTestTestfile.cmake" not in files:
            continue
        with open(os.path.join(root, "CTestTestfile.cmake"), encoding="utf-8") as f:
            for ln in f:
                m = re.match(r'^\s*add_test\((?:\[=\[(.*?)\]=\]|(\S+)) (.*)\)\s*$', ln)
                if m:
                    commands[m.group(1) or m.group(2)] = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(3))
    return commands


def check_one(build):
    """Returns (problems, names of the tests ctest lists)."""
    build = os.path.realpath(build)
    problems = []
    tests = ctest_listing(build)
    names = [t["name"] for t in tests]
    outputs = {ln.rsplit(": ", 1)[0] for ln in ninja_lines(build, "targets", "all") if ": " in ln}
    built = built_by_all(build)
    commands = test_commands(build)
    for t in tests:
        for word in commands.get(t["name"], []):
            if not os.path.isabs(word):
                continue
            rel = os.path.relpath(os.path.realpath(word), build)
            if rel.startswith(".."):
                continue
            if rel in outputs and rel not in built:
                problems.append("test %s runs %s, which the default build does not make" % (t["name"], rel))
    if len(set(names)) != len(names):
        problems.append("ctest lists a test name twice")
    manifest = os.path.join(build, "okay_test_shard.json")
    if os.path.exists(manifest):
        with open(manifest, encoding="utf-8") as f:
            m = json.load(f)
        rows = m["tests"]
        all_names = [r["name"] for r in rows]
        if len(set(all_names)) != len(all_names):
            problems.append("okay_test_shard.json names a test twice")
        mine = {r["name"] for r in rows if r["shard"] == m["shard"]}
        if mine != set(names):
            problems.append("shard %d/%d: ctest lists %d tests, the manifest gives it %d (only in ctest: %s; only in manifest: %s)"
                            % (m["shard"], m["of"], len(names), len(mine), sorted(set(names) - mine)[:10], sorted(mine - set(names))[:10]))
        print("shard %d/%d: %d of %d tests, %d test programs built" % (m["shard"], m["of"], len(names), len(rows), len(m["programs"])))
    else:
        print("not sharded: %d tests" % len(names))
    if len(commands) != len(names):
        problems.append("read %d test commands from CTestTestfile.cmake, ctest lists %d tests" % (len(commands), len(names)))
    return problems, names


def main(argv):
    if len(argv) >= 3 and argv[0] == "--full":
        problems, full = check_one(argv[1])
        seen = {}
        for d in argv[2:]:
            p, names = check_one(d)
            problems += ["%s: %s" % (d, x) for x in p]
            for n in names:
                seen.setdefault(n, []).append(d)
        twice = sorted(n for n, ds in seen.items() if len(ds) > 1)
        missing = sorted(set(full) - set(seen))
        extra = sorted(set(seen) - set(full))
        if twice:
            problems.append("%d tests are in more than one shard: %s" % (len(twice), twice[:10]))
        if missing:
            problems.append("%d tests are in no shard: %s" % (len(missing), missing[:10]))
        if extra:
            problems.append("%d tests are only in a shard: %s" % (len(extra), extra[:10]))
        print("full: %d tests; shards together: %d" % (len(full), sum(len(v) for v in seen.values())))
    elif len(argv) == 1:
        problems, _ = check_one(argv[0])
    else:
        print(__doc__)
        return 2
    for p in problems:
        print("FAIL: " + p)
    if not problems:
        print("OK")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
