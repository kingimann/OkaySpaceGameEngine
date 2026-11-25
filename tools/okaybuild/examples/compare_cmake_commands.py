#!/usr/bin/env python3
"""Compare the compile commands CMake wrote (compile_commands.json from a configure with
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON) with the ones okay-build writes (`okay-build -f build.okay compdb`) for the same sources.
For every source file present in both it compares the -D definitions, the -I / -isystem folders and the other flags, as sets
(order, output paths and dependency-file options are ignored). A source compiled by several targets has every command compared. Prints how many sources match exactly and the first differences.
usage: compare_cmake_commands.py CMAKE_COMPILE_COMMANDS.json OKAYBUILD_COMPILE_COMMANDS.json REPO_ROOT
       compare_cmake_commands.py --exact [--target NAME]... [--map CMAKE_BUILD_DIR=OKAYBUILD_BIN_DIR] CMAKE_COMPILE_COMMANDS.json OKAYBUILD_COMPILE_COMMANDS.json REPO_ROOT
--exact compares each object's command word by word, in order (for a manifest with compile_order = cmake). Commands are paired by
target and source (CMake's object path CMakeFiles/<target>.dir/..., okay-build's .okaybuild/obj/<target>/...). Left out of the
comparison: the compiler's name, the output, the dependency-file options (-MD/-MMD/-MF/-MT), -c and the source path (also a windres
line's source and .res output, which have no -c / -o), and okay-build's
-ffile-prefix-map; the account definitions are compared by name and place, not value (okay-build hides the value). --target limits
the comparison to those targets; then the object lists must also be the same. --map spells CMake's build folder the way okay-build
names its {bin} folder (generated sources and folders in the build tree), in source paths and in the words. Exit code 0 only when everything is the same.
Written by us for this repository (no code from other projects). Not part of CMake, CI or releases."""
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

def exact_words(args):
    out, i = [], 1
    while i < len(args):
        a = args[i]
        if a in ("-o", "-MF", "-MT", "-MQ", "-c"): i += 2; continue
        if a in ("-MD", "-MMD", "-MP") or a.startswith("-ffile-prefix-map="): i += 1; continue
        if a.startswith("-DOKAY_DEFAULT_ACCOUNT_"): a = a.split("=")[0] + "=<value>"
        out.append(a); i += 1
    return out

def exact_load(path, ninja_style, maps=()):
    """{(target, source): words} from a compile database; the target comes from the object path."""
    def mapped(x):
        for frm, to in maps:
            x = x.replace(frm, to)
        return x
    out = {}
    for e in json.load(open(path)):
        d = e.get("directory", ".")
        args = e.get("arguments") or shlex.split(e["command"])
        obj = e.get("output") or (args[args.index("-o") + 1] if "-o" in args else "")
        if ninja_style:
            if ".dir/" not in obj: continue
            target = obj.split(".dir/")[0].split("/")[-1]
        else:
            if "/obj/" not in obj: continue
            target = obj.split("/obj/")[1].split("/")[0]
        real = os.path.realpath(os.path.join(d, e["file"]))
        src = mapped(real)
        # a resource compile (windres -O coff ... FILE OUT.res) names its source and output without -c / -o: leave both out too
        words = [w for w in exact_words(args) if w != obj and os.path.realpath(os.path.join(d, w)) != real]
        out[(target, src)] = [mapped(w) for w in words]
    return out

def exact_main(argv):
    targets, maps = [], []
    while argv and argv[0] in ("--target", "--map"):
        if argv[0] == "--target": targets.append(argv[1])
        else: maps.append(tuple(os.path.realpath(x) for x in argv[1].split("=", 1)))
        argv = argv[2:]
    cm, ok, root = argv[0], argv[1], os.path.realpath(argv[2])
    a, b = exact_load(cm, True, maps), exact_load(ok, False)
    if targets:
        a = {k: v for k, v in a.items() if k[0] in targets}; b = {k: v for k, v in b.items() if k[0] in targets}
    both = sorted(k for k in a if k in b)
    same = [k for k in both if a[k] == b[k]]
    print("objects: CMake %d, okay-build %d, in both %d; identical command lines %d, different %d" % (len(a), len(b), len(both), len(same), len(both) - len(same)))
    shown = 0
    for k in both:
        if a[k] == b[k] or shown >= 10: continue
        shown += 1
        print("DIFF %s %s" % (k[0], k[1].replace(root + "/", "")))
        print("   CMake:      " + " ".join(a[k])); print("   okay-build: " + " ".join(b[k]))
    only_a = sorted(set(a) - set(b)); only_b = sorted(set(b) - set(a))
    if targets or not (only_a or only_b): print("only CMake: %d, only okay-build: %d" % (len(only_a), len(only_b)))
    else: print("only CMake: %d, only okay-build: %d (no --target given: targets the other build did not plan are listed too)" % (len(only_a), len(only_b)))
    for k in only_a[:5]: print("   only CMake:", k[0], k[1].replace(root + "/", ""))
    for k in only_b[:5]: print("   only okay-build:", k[0], k[1].replace(root + "/", ""))
    ok_all = len(same) == len(both) and (not targets or (not only_a and not only_b))
    sys.exit(0 if ok_all else 1)

def main():
    if sys.argv[1:2] == ["--exact"]: exact_main(sys.argv[2:]); return
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
