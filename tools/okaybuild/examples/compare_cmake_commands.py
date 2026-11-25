#!/usr/bin/env python3
"""Compare the compile commands CMake wrote (compile_commands.json from a configure with
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON) with the ones okay-build writes (`okay-build -f build.okay compdb`) for the same sources.
For every source file present in both it compares the -D definitions, the -I / -isystem folders and the other flags, as sets
(order, output paths and dependency-file options are ignored). A source compiled by several targets has every command compared. Prints how many sources match exactly and the first differences.
usage: compare_cmake_commands.py CMAKE_COMPILE_COMMANDS.json OKAYBUILD_COMPILE_COMMANDS.json REPO_ROOT"""
import json, os, shlex, sys

def load(path, root):
    out = {}
    for e in json.load(open(path)):
        f = os.path.normpath(os.path.join(e.get("directory", "."), e["file"]))
        args = e.get("arguments") or shlex.split(e["command"])
        out.setdefault(f, []).append(args)
    return out

def norm(args, root):
    defs, incs, flags = set(), set(), set()
    i = 1
    while i < len(args):
        a = args[i]
        if a in ("-o", "-MF", "-MT", "-MQ", "-c"):
            i += 2 if a != "-c" else 2; continue
        if a in ("-MD", "-MMD", "-MP"): i += 1; continue
        if a.startswith("-ffile-prefix-map="): i += 1; continue
        if a == "-isystem" or a == "-I":
            p = args[i + 1]; incs.add(os.path.normpath(p)); i += 2; continue
        if a.startswith("-I"): incs.add(os.path.normpath(a[2:])); i += 1; continue
        if a == "-D": defs.add(args[i + 1]); i += 2; continue
        if a.startswith("-D"): defs.add(a[2:]); i += 1; continue
        if a == "-x": i += 2; continue
        if a.endswith((".cpp", ".c", ".cc")) and not a.startswith("-"): i += 1; continue
        flags.add(a); i += 1
    return defs, incs, flags

def rel(s, root):
    return {x.replace(root + "/", "") for x in s}

def main():
    cm, ok, root = sys.argv[1], sys.argv[2], os.path.realpath(sys.argv[3])
    a, b = load(cm, root), load(ok, root)
    both = [f for f in a if f in b and f.startswith(root)]
    same = 0; shown = 0
    kinds = {"defs": 0, "incs": 0, "flags": 0}
    # account defines are hidden on purpose; OKAY_TEST_OUTPUT_DIR and OKAY_TEST_TMP_DIR name the build folder, which differs between the two trees
    skip = ("OKAY_DEFAULT_ACCOUNT_", "OKAY_TEST_OUTPUT_DIR=", "OKAY_TEST_TMP_DIR=")
    def key(args):
        d, i, fl = norm(args, root)
        return (frozenset(x for x in d if not x.startswith(skip)), frozenset(rel(i, root)), frozenset(fl))
    commands = 0
    for f in sorted(both):
        # a source compiled by several targets has several commands; the two tools may list them in another order, so each
        # CMake command is paired with an equal okay-build command first, and only what is left over is compared and shown
        ka, kb = [key(x) for x in a[f]], [key(x) for x in b[f]]
        commands += len(ka)
        left_a, left_b = [], list(kb)
        for k in ka:
            if k in left_b: left_b.remove(k)
            else: left_a.append(k)
        if not left_a: same += 1; continue
        da, ia, fa = left_a[0]; db, ib, fb = left_b[0] if left_b else kb[0]
        d, i, fl = da ^ db, ia ^ ib, fa ^ fb
        for k, v in (("defs", d), ("incs", i), ("flags", fl)):
            if v: kinds[k] += 1
        if shown < 15:
            shown += 1
            print("DIFF", f.replace(root + "/", ""), "(%d of %d commands unmatched)" % (len(left_a), len(ka)))
            if d: print("   defs only one side:", sorted(d))
            if i: print("   include folders only one side:", sorted(i))
            if fl: print("   flags only one side:", sorted(fl))
    only_cmake = [f for f in a if f.startswith(root) and f not in b]
    only_ok = [f for f in b if f.startswith(root) and f not in a]
    print("sources in both: %d, identical settings: %d, different: %d (defs %d, include folders %d, flags %d)" % (len(both), same, len(both) - same, kinds["defs"], kinds["incs"], kinds["flags"]))
    print("only in CMake: %d, only in okay-build: %d; CMake compile commands compared: %d" % (len(only_cmake), len(only_ok), commands))
    for f in sorted(only_cmake)[:5]: print("   only CMake:", f.replace(root + "/", ""))
    for f in sorted(only_ok)[:5]: print("   only okay-build:", f.replace(root + "/", ""))
main()
