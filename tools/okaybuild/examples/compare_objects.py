#!/usr/bin/env python3
# Compare the object files okay-build makes from `okay-build native` with the ones CMake+Ninja makes, byte for byte.
# Written by us for this repository (no code from other projects). Not part of CMake, CI or releases.
#
# usage: compare_objects.py OKAY_BUILD REPO CMAKE_BUILD_DIR WORK [BATCH]
#   OKAY_BUILD       the okay-build program
#   REPO             the source tree (a manifest file is written in it temporarily and removed again)
#   CMAKE_BUILD_DIR  a configured Ninja build dir, ideally -DCMAKE_BUILD_TYPE=None -DCMAKE_CXX_FLAGS="-O0 -g0" (no debug info:
#                    debug info records paths and would differ)
#   WORK             scratch folder for okay-build's objects
#   Environment OKB_ABS_INCLUDES=1 writes the include folders and the source paths as absolute paths, the way CMake does. Without it, files whose
#   headers use __FILE__ (assert-like macros) differ in that string only (seen: Render/ModelImport.cpp).
#   Environment OKB_ONLY=FILE limits the run to the source paths listed in FILE.
#   BATCH            files per round (default 60). Each round builds with both tools, compares, then deletes the objects, so
#                    the disk needs only a few hundred MB, not the whole library.
# Environment: the account defines come from OKAY_DEFAULT_ACCOUNT_URL/KEY or config/public-account.json for both tools, as usual.
import os, re, subprocess, sys, shutil

okb, repo, cm, work = [os.path.abspath(a) for a in sys.argv[1:5]]
batch = int(sys.argv[5]) if len(sys.argv) > 5 else 60
man = os.path.join(repo, "okay-native-compare.okay")
subprocess.run([okb, "native", repo, "-o", man], check=True, stdout=subprocess.DEVNULL)
text = open(man).read()
head = text.split("target ")[0]
blocks = {}
for m in re.finditer(r"target (\S+) (lib|exe)\n(.*?)\nend\n", text, re.S):
    blocks[m.group(1)] = m.group(3)
def field(block, key):
    m = re.search(r"^  %s =(.*)$" % key, block, re.M); return m.group(1) if m else ""
files = []   # (source, flags block lines)
for name in ("okay_engine", "okay_platform", "ufbx"):
    if name in blocks:
        for src in re.findall(r'(?:"[^"]*"|\S+)', field(blocks[name], "sources")):
            files.append((src.strip('"'), name))
only = os.environ.get("OKB_ONLY")   # optional: a file with one source path per line, to re-check just those
if only:
    names = set(l.strip() for l in open(only) if l.strip())
    files = [f for f in files if f[0] in names]
def obj_cmake(src, target="okay_engine"):
    rel = src[len("engine/"):] if src.startswith("engine/") else src
    return os.path.join(cm, "engine/CMakeFiles/%s.dir" % ("okay_platform" if target == "okay_platform" else "okay_engine"), rel + ".o")
def obj_ours(src, target):
    s2 = os.path.join(repo, src) if os.environ.get("OKB_ABS_INCLUDES") else src
    return os.path.join(work, ".okaybuild/obj", target, s2.lstrip("/") + ".o") if os.path.exists(os.path.join(work, ".okaybuild/obj", target, s2.lstrip("/") + ".o")) else os.path.join(work, ".okaybuild/obj", target, s2 + ".o")
same, diff, missing = 0, [], []
try:
    for i in range(0, len(files), batch):
        part = files[i:i + batch]
        out = head
        for name in ("okay_engine", "okay_platform", "ufbx"):
            srcs = [s for s, n in part if n == name]
            if not srcs: continue
            b = blocks[name]
            keep = [l for l in b.split("\n") if not l.startswith("  sources =") and not l.startswith("  deps =")]
            if os.environ.get("OKB_ABS_INCLUDES"):  # CMake passes absolute -I paths, and a header's __FILE__ shows that spelling
                keep = [("  include = " + " ".join(t if t.startswith("/") else os.path.join(repo, t) for t in l[len("  include ="):].split())) if l.startswith("  include =") else l for l in keep]
            if os.environ.get("OKB_ABS_INCLUDES"): srcs = [os.path.join(repo, x) for x in srcs]   # __FILE__ in the .c/.cpp itself (ufbx records it)
            out += "target %s lib\n  sources = %s\n%s\nend\n" % (name, " ".join(srcs), "\n".join(keep))
        open(man, "w").write(out)
        shutil.rmtree(work, ignore_errors=True)
        r = subprocess.run([okb, "-f", man, "--build-dir", work, "-j2", "--no-cache"], cwd=repo, capture_output=True, text=True)
        if r.returncode != 0: print("okay-build failed in batch %d:\n%s" % (i // batch, r.stderr[-2000:]))
        want = [obj_cmake(s, t) for s, t in part]
        n = subprocess.run(["ninja", "-C", cm, "-j2"] + [os.path.relpath(w, cm) for w in want], capture_output=True, text=True)
        if n.returncode != 0: print("ninja failed in batch %d:\n%s" % (i // batch, n.stdout[-2000:]))
        for (s, t), c in zip(part, want):
            o = obj_ours(s, t)
            if not os.path.exists(o) or not os.path.exists(c): missing.append(s); continue
            if open(o, "rb").read() == open(c, "rb").read(): same += 1
            else: diff.append(s)
        for _, c in zip(part, want):
            if os.path.exists(c): os.remove(c)
        shutil.rmtree(work, ignore_errors=True)
        print("batch %d: %d files so far, %d identical, %d different, %d missing" % (i // batch, min(i + batch, len(files)), same, len(diff), len(missing)), flush=True)
finally:
    if os.path.exists(man): os.remove(man)
print("identical: %d of %d" % (same, len(files)))
for d in diff: print("DIFFERENT", d)
for d in missing: print("MISSING", d)
