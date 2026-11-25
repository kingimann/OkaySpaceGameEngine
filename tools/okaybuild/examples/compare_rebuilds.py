#!/usr/bin/env python3
"""Check that okay-build rebuilds exactly the objects Ninja rebuilds after a change (round 17's method, kept here).

usage: compare_rebuilds.py --ninja-dir DIR --okay-build EXE --manifest FILE --build-dir DIR [--jobs N] [--target NAME]... CHANGE...
  --ninja-dir   a CMake+Ninja build folder of the same sources (configured with CMAKE_EXPORT_COMPILE_COMMANDS=ON)
  --okay-build  the okay-build program; --manifest its manifest (in the source root); --build-dir its build folder
  --target      what both tools build (Ninja target names; okay-build takes the same names); none: everything
  CHANGE        noop          build again with nothing changed
                edit:PATH     append a comment line to the file (path relative to the manifest's folder), build, then put the
                              file's bytes back (with a new time, as an editor would) and build again ("undo"); both are compared
                touch:PATH    give the file a new time and the same bytes
Both tools build once first, so the first change starts from an up-to-date tree. After each build the objects whose file changed
(time or size) are listed by target and source file and the two lists are compared. A line per build:
  edit:x.hpp: ninja rebuilt 5 objects (rc 0, 3 s); okay-build rebuilt 5 (rc 0, 2 s); same set: yes
Exit code 0 when every content change gave the same set in both tools. A touch gives "planned difference" when only Ninja rebuilds
(okay-build compares the bytes of a file whose time changed) and does not count as a failure.
Files' bytes are always put back, also when a build fails or the script is stopped with Ctrl-C.
Written by us for this repository (no code from other projects). Not part of CMake, CI or releases. Linux only.
"""
import argparse, json, os, subprocess, sys, time


def ninja_objects(ninja_dir):
    """object path -> (target, source) from CMake's compile database."""
    m = {}
    for e in json.load(open(os.path.join(ninja_dir, "compile_commands.json"))):
        out = e.get("output")
        if not out or ".dir/" not in out:
            continue
        d = e.get("directory", ninja_dir)
        target = out.split(".dir/")[0].split("/")[-1]
        src = os.path.realpath(os.path.join(d, e["file"]))
        if src.startswith(os.path.realpath(ninja_dir) + "/"):
            src = "{bin}/" + src[len(os.path.realpath(ninja_dir)) + 1:]
        m[os.path.normpath(os.path.join(d, out))] = (target, src)
    return m


def okb_source(rel, root):
    """okay-build's object path below .okaybuild/obj/<target>/ back to the source: '__' stands for '..', '_bin/' for the build tree."""
    rel = rel[: -len(".o")] if rel.endswith(".o") else rel
    if rel.startswith("_bin/"):
        return "{bin}/" + rel[5:]
    parts = [".." if p == "__" else p for p in rel.split("/")]
    p = "/".join(parts)
    return os.path.realpath(p if p.startswith("/") else os.path.join(root, p))


def snapshot(kind, where, root, nmap):
    """{(target, source): (mtime_ns, size)} of every object file the tool has made."""
    r = {}
    if kind == "ninja":
        for path, key in nmap.items():
            try:
                st = os.stat(path)
            except FileNotFoundError:
                continue
            r[key] = (st.st_mtime_ns, st.st_size)
        return r
    base = os.path.join(where, ".okaybuild", "obj")
    for d, _, files in os.walk(base):
        for f in files:
            if not f.endswith(".o"):
                continue
            full = os.path.join(d, f)
            rel = os.path.relpath(full, base)
            target, rest = rel.split("/", 1)
            st = os.stat(full)
            r[(target, okb_source(rest, root))] = (st.st_mtime_ns, st.st_size)
    return r


def build(kind, a, root, nmap):
    before = snapshot(kind, a.ninja_dir if kind == "ninja" else a.build_dir, root, nmap)
    if kind == "ninja":
        cmd = ["ninja", "-C", a.ninja_dir, "-j", str(a.jobs)] + a.target
    else:
        cmd = [a.okay_build, "-f", a.manifest, "-j", str(a.jobs), "--no-cache", "--build-dir", a.build_dir] + a.target
    t = time.time()
    p = subprocess.run(cmd, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    took = time.time() - t
    after = snapshot(kind, a.ninja_dir if kind == "ninja" else a.build_dir, root, nmap)
    changed = sorted(k for k in after if before.get(k) != after[k])
    return p.returncode, changed, took, p.stdout[-2000:]


def compare(label, a, root, nmap, planned_touch=False):
    rn, cn, tn, on = build("ninja", a, root, nmap)
    ro, co, to, oo = build("okay-build", a, root, nmap)
    same = cn == co and rn == 0 and ro == 0
    verdict = "yes" if same else "no"
    if not same and planned_touch and rn == 0 and ro == 0 and not co:
        verdict = "planned difference (okay-build compares the bytes)"
    print("%s: ninja rebuilt %d objects (rc %d, %.0f s); okay-build rebuilt %d (rc %d, %.0f s); same set: %s"
          % (label, len(cn), rn, tn, len(co), ro, to, verdict), flush=True)
    if verdict == "no":
        show = lambda ks: ["%s %s" % (k[0], k[1].replace(root + "/", "")) for k in ks[:10]]
        print("  only ninja:", show(sorted(set(cn) - set(co))))
        print("  only okay-build:", show(sorted(set(co) - set(cn))))
        if rn: print("  ninja output:", on)
        if ro: print("  okay-build output:", oo)
    return verdict != "no"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--ninja-dir", required=True)
    ap.add_argument("--okay-build", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--target", action="append", default=[])
    ap.add_argument("changes", nargs="+")
    a = ap.parse_args()
    a.ninja_dir, a.build_dir, a.manifest = (os.path.abspath(x) for x in (a.ninja_dir, a.build_dir, a.manifest))
    a.okay_build = os.path.abspath(a.okay_build)
    root = os.path.realpath(os.path.dirname(a.manifest))
    for c in a.changes:
        if c != "noop" and not (c.startswith("edit:") or c.startswith("touch:")):
            sys.exit("compare_rebuilds: unknown change '%s' (noop, edit:PATH, touch:PATH)" % c)
        if c != "noop" and not os.path.isfile(os.path.join(root, c.split(":", 1)[1])):
            sys.exit("compare_rebuilds: no file '%s' below %s" % (c.split(":", 1)[1], root))
    nmap = ninja_objects(a.ninja_dir)
    for kind in ("ninja", "okay-build"):  # start from an up-to-date tree
        rc, _, took, out = build(kind, a, root, nmap)
        print("first %s build: rc %d, %.0f s" % (kind, rc, took), flush=True)
        if rc:
            print(out); sys.exit(2)
    ok = True
    for c in a.changes:
        if c == "noop":
            ok &= compare(c, a, root, nmap); continue
        kind, rel = c.split(":", 1)
        path = os.path.join(root, rel)
        data = open(path, "rb").read()
        time.sleep(0.01)  # a new time stamp that differs from the objects' even on a coarse clock
        if kind == "touch":
            os.utime(path)  # "now" as the kernel stamps file writes (a finer clock could put the file after the objects built next)
            ok &= compare(c, a, root, nmap, planned_touch=True)
            continue
        try:
            with open(path, "ab") as f:
                f.write(b"\n// compare_rebuilds.py: a comment to change the bytes\n")
            ok &= compare(c, a, root, nmap)
        finally:
            with open(path, "wb") as f:  # a new time: with the old one Ninja would not see the undo, okay-build would (it hashes)
                f.write(data)
        ok &= compare("undo " + c, a, root, nmap)  # the old bytes are back: both tools see a change again and must agree again
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
