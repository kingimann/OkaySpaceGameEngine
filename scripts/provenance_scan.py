#!/usr/bin/env python3
"""Provenance scan: a text scan for signs that code was copied or ported (docs/provenance_audit.md).

Two modes, both read only:
  python3 scripts/provenance_scan.py markers
      Lists comments and strings in our own sources that name another project or say "ported from",
      "based on", "adapted from" and so on.
  python3 scripts/provenance_scan.py overlap REFDIR [REFDIR ...] [--min-len N] [--min-run N]
      Compares our own sources with a local checkout of another project (for example an Assimp source tree
      that you cloned yourself into a scratch folder). It reports runs of consecutive lines that match after
      whitespace and comments are removed. A match of several lines in a row is a lead, not proof; a clean
      result is not proof of originality. It prints file names and line numbers only, never the other code.
"""
import os, re, subprocess, sys

ROOTS = ["engine", "shared", "editor", "launcher", "player", "okayui", "relay", "tools", "scripts", "tests"]
SKIP = ("editor/vendor", "engine/third_party", "tests/wycheproof", "tests/data")
EXT = (".cpp", ".hpp", ".h", ".c", ".py", ".sh", ".cmake", ".glsl", ".hlsl", ".js")
MARK = re.compile(r"ported (from|to)|port of|based on [A-Z]|adapted from|taken from [A-Z]|derived from [A-Z]|translated from|copied from|"
                  r"a la |after (Assimp|stb|SDL|ImGui|minimp3|TweetNaCl)|the way (Assimp|stb|SDL|Godot|Unreal)|"
                  r"\b(Assimp|meshoptimizer|minimp3|stb_[a-z]+|dr_(mp3|flac|wav)|TweetNaCl|libsodium|miniz|lodepng|tinyobj|Box2D|"
                  r"PhysX|Bullet|Recast|Detour|Godot|Unreal|miniaudio|FreeType|libjpeg|libpng|zlib|Freeverb|mikktspace)\b", re.I)

def files():
    top = subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip()
    out = subprocess.check_output(["git", "-C", top, "ls-files"], text=True).splitlines()
    for f in out:
        if f.split("/")[0] in ROOTS and f.endswith(EXT) and not f.startswith(SKIP):
            yield os.path.join(top, f), f

def markers():
    n = 0
    for p, rel in files():
        try: lines = open(p, errors="replace").read().splitlines()
        except OSError: continue
        for i, l in enumerate(lines, 1):
            if MARK.search(l):
                n += 1; print("%s:%d: %s" % (rel, i, l.strip()[:200]))
    print("# %d marker lines" % n, file=sys.stderr)

def norm(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    out = []
    for i, l in enumerate(text.splitlines(), 1):
        l = re.sub(r"//.*$", "", l)
        l = re.sub(r"\s+", "", l)
        if l.startswith("#"): l = ""          # includes, defines and pragmas are not evidence
        out.append((i, l))
    return out

def overlap(refs, min_len, min_run):
    index = {}
    for ref in refs:
        for d, _, fs in os.walk(ref):
            for f in fs:
                if f.endswith((".cpp", ".h", ".hpp", ".c", ".inl", ".cc")):
                    p = os.path.join(d, f)
                    try: t = open(p, errors="replace").read()
                    except OSError: continue
                    for i, l in norm(t):
                        if len(l) >= min_len: index.setdefault(l, set()).add(os.path.relpath(p, ref))
    for p, rel in files():
        if not rel.endswith((".cpp", ".hpp", ".h", ".c")): continue
        try: t = open(p, errors="replace").read()
        except OSError: continue
        run, start, hit = 0, 0, set()
        for i, l in norm(t):
            if len(l) >= min_len and l in index:
                if run == 0: start = i; hit = set()
                run += 1; hit |= index[l]
            elif l == "" or len(l) < min_len:
                continue          # short or blank lines do not break a run
            else:
                if run >= min_run: print("%s:%d-%d: %d matching lines, in %s" % (rel, start, i - 1, run, ", ".join(sorted(hit)[:3])))
                run = 0
        if run >= min_run: print("%s:%d-: %d matching lines, in %s" % (rel, start, run, ", ".join(sorted(hit)[:3])))

if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["markers"]: markers()
    elif a[:1] == ["overlap"] and len(a) > 1:
        mn, mr, refs, k = 12, 3, [], 1
        while k < len(a):
            if a[k] == "--min-len": mn = int(a[k + 1]); k += 2
            elif a[k] == "--min-run": mr = int(a[k + 1]); k += 2
            else: refs.append(a[k]); k += 1
        overlap(refs, mn, mr)
    else: print(__doc__); sys.exit(2)
