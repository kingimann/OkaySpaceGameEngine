#!/usr/bin/env python3
"""Compare the link command lines okay-build plans for some programs with the ones CMake+Ninja would run.
Written by us for this repository (no code from other projects). Not part of CMake, CI or releases.

usage: compare_link_commands.py CMAKE_BUILD_DIR OKB_DRYRUN_LOG OKB_COMPILE_COMMANDS.json REPO CMAKE_OUT=OKB_OUT ...
  CMAKE_BUILD_DIR       a configured Ninja build folder (compile_commands.json with "output" entries; ninja -t commands is asked)
  OKB_DRYRUN_LOG        output of `okay-build -f M --build-dir D --dry-run -v TARGETS...` (every planned command is printed)
  OKB_COMPILE_COMMANDS  okay-build's compile_commands.json for the same manifest and build folder
  CMAKE_OUT=OKB_OUT     a program as CMake names its output (bin/OkayEngine) and as okay-build does (build/OkayEngine)

Each object is turned back into the source file it was compiled from (through each tool's compile database), so the two
object lists can be compared although the object paths differ. Libraries are compared by file name, in link order.
"""
import json, os, shlex, subprocess, sys


def objects_to_sources(path, base):
    m = {}
    for e in json.load(open(path)):
        d = e.get("directory", base)
        args = e.get("arguments") or shlex.split(e["command"])
        out = e.get("output")
        if not out and "-o" in args:
            out = args[args.index("-o") + 1]
        if out:
            m[os.path.normpath(os.path.join(d, out))] = os.path.realpath(os.path.join(d, e["file"]))
    return m


def is_system(path):
    return path.startswith("/usr/") or path.startswith("/lib/") or path.startswith("/lib64/")


def split(words, base, objmap, archives=None):
    objs, libs, syslibs, flags, out = [], [], [], [], None
    whole = False
    i = 1
    while i < len(words):
        w = words[i]
        if w == "-o":
            out = words[i + 1]; i += 2; continue
        if w in ("-Wl,--whole-archive", "-Wl,--no-whole-archive"):
            whole = w == "-Wl,--whole-archive"; i += 1; continue
        if w.endswith(".o") or w.endswith(".obj"):
            p = os.path.normpath(os.path.join(base, w))
            objs.append(objmap.get(p, "?" + w))
        elif w.endswith(".a") or w.endswith(".so") or ".so." in w:
            n = os.path.basename(w)
            if whole and archives is not None and n in archives:  # an OBJECT library linked whole: its objects are the program's
                for o in archives[n]:
                    objs.append(objmap.get(os.path.normpath(os.path.join(base, o)), "?" + o))
            elif is_system(w) and n.startswith("lib"):
                # a system library given by path (CMake spells SDL2 as /usr/lib/.../libSDL2.so) is the same as -lSDL2
                syslibs.append("-l" + n[3:].split(".")[0])
            else:
                libs.append(n)
        elif w.startswith("-l"):
            syslibs.append(w)
        else:
            flags.append(w)
        i += 1
    return objs, libs, syslibs, flags, out


def dedupe_tail(seq):  # the last time each item appears is what decides symbol resolution order for static archives
    seen, r = set(), []
    for x in reversed(seq):
        if x not in seen:
            seen.add(x); r.append(x)
    return list(reversed(r))


def main():
    cm, log, okcc, repo = sys.argv[1:5]
    pairs = [a.split("=", 1) for a in sys.argv[5:]]
    cm_objs = objects_to_sources(os.path.join(cm, "compile_commands.json"), cm)
    ok_objs = objects_to_sources(okcc, repo)
    lines = open(log).read().splitlines()
    archives = {}  # archive file name -> its member objects, from okay-build's AR commands
    for k, l in enumerate(lines):
        if l.lstrip().startswith("[") and " AR " in l and k + 1 < len(lines):
            aw = shlex.split(lines[k + 1].strip())
            if len(aw) > 3:
                archives[os.path.basename(aw[2])] = [x for x in aw[3:] if x.endswith(".o")]
    worst = 0
    for cm_out, ok_out in pairs:
        cmd = subprocess.run(["ninja", "-C", cm, "-t", "commands", cm_out], capture_output=True, text=True, check=True).stdout.splitlines()[-1]
        cw = shlex.split(cmd)
        if "&&" in cw:  # ": && c++ ... && :"
            cw = cw[cw.index("&&") + 1:]
            if "&&" in cw:
                cw = cw[:cw.index("&&")]
        ow = None
        for k, l in enumerate(lines):
            if l.lstrip().startswith("[") and (" LD " in l) and l.rstrip().endswith("/" + ok_out):
                ow = shlex.split(lines[k + 1].strip()); break
        if ow is None:
            print("%s: no link command for %s in the okay-build log" % (cm_out, ok_out)); worst = 1; continue
        co, cl, cs, cf, _ = split(cw, cm, cm_objs)
        oo, ol, osys, of, _ = split(ow, repo, ok_objs, archives)
        print("== %s (CMake) vs %s (okay-build)" % (cm_out, ok_out))
        print("  objects: CMake %d, okay-build %d, same set: %s, same order: %s" % (len(co), len(oo), set(co) == set(oo), co == oo))
        for x in sorted(set(co) - set(oo))[:10]: print("    only CMake:", os.path.relpath(x, repo) if x[0] != "?" else x)
        for x in sorted(set(oo) - set(co))[:10]: print("    only okay-build:", os.path.relpath(x, repo) if x[0] != "?" else x)
        print("  our libraries, CMake order (last use):     ", " ".join(dedupe_tail(cl)))
        print("  our libraries, okay-build order (last use):", " ".join(dedupe_tail(ol)))
        print("  same set of our libraries: %s" % (set(cl) == set(ol)))
        print("  system libraries: CMake %s | okay-build %s" % (" ".join(dedupe_tail(cs)), " ".join(dedupe_tail(osys))))
        print("  other link flags: CMake %s | okay-build %s" % (" ".join(cf) or "-", " ".join(of) or "-"))
        if set(co) != set(oo) or set(cl) != set(ol): worst = 1
    return worst


if __name__ == "__main__":
    sys.exit(main())
