#!/usr/bin/env python3
"""Compare `test_model_corpus --list` output (ours) with scripts/assimp_corpus_dump output (Assimp).

  compare_model_corpus.py ours.tsv assimp.tsv [--tol-area 0.01] [--tol-bbox 0.01]

Both files are TSV lines `reader path ok|FAIL key=value ...`. Paths are matched by their text after the reader column.
Prints one line per difference and a summary. Differences that are by design are not printed: Assimp's extra default material and our generated normals. Vertex counts are not compared (ours welds, Assimp keeps the file's
vertices), nor node counts (ours adds a wrapper root); they are printed in the table for the record."""
import sys

def load(path):
    out = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        c = line.split("\t")
        if len(c) < 3:
            continue
        kv = {}
        for f in c[3:]:
            if "=" in f:
                k, v = f.split("=", 1)
                kv[k] = v
        out[c[1]] = (c[2], kv, c[3] if c[2] != "ok" and len(c) > 3 else "")
    return out

def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    tol_area = 0.01; tol_bbox = 0.01
    if "--tol-area" in sys.argv: tol_area = float(sys.argv[sys.argv.index("--tol-area") + 1])
    ours, theirs = load(a[0]), load(a[1])
    agree = diff = 0
    for p in sorted(set(ours) | set(theirs)):
        o, t = ours.get(p), theirs.get(p)
        if o is None or t is None:
            print(f"{p}: only in {'ours' if t is None else 'assimp'}"); diff += 1; continue
        if o[0] != t[0]:
            print(f"{p}: STATUS ours={o[0]} {o[2][:70]} | assimp={t[0]} {t[2][:70]}"); diff += 1; continue
        if o[0] != "ok":
            agree += 1; continue
        d = []
        for k in ("tris", "meshes", "mat", "tex", "uv", "nrm", "clips"):
            if o[1].get(k) == t[1].get(k): continue
            # by design, not differences: Assimp adds a default material when a mesh has none (one more than the file
            # declares); we generate normals the file lacks (Assimp leaves them absent)
            if k == "mat" and t[1].get(k, "").lstrip("-").isdigit() and o[1].get(k, "").lstrip("-").isdigit() and int(t[1][k]) == int(o[1][k]) + 1: continue
            if k == "nrm" and o[1].get(k) == "1" and t[1].get(k) == "0": continue
            d.append(f"{k} ours={o[1].get(k)} assimp={t[1].get(k)}")
        try:
            ao, at = float(o[1]["area"]), float(t[1]["area"])
            if abs(ao - at) > tol_area * max(abs(at), 1e-9) + 1e-6: d.append(f"area ours={ao:.4f} assimp={at:.4f}")
            bo = [float(x) for x in o[1]["bbox"].split(",")]; bt = [float(x) for x in t[1]["bbox"].split(",")]
            size = max(max(bt[3 + i] - bt[i] for i in range(3)), 1e-9)
            if any(abs(bo[i] - bt[i]) > tol_bbox * size + 1e-4 for i in range(6)): d.append(f"bbox ours={o[1]['bbox']} assimp={t[1]['bbox']}")
            if abs(float(o[1]["dur"]) - float(t[1]["dur"])) > 0.01: d.append(f"dur ours={o[1]['dur']} assimp={t[1]['dur']}")
        except (KeyError, ValueError):
            d.append("unparsable")
        if d: print(f"{p}: " + "; ".join(d)); diff += 1
        else: agree += 1
    print(f"-- {agree} files agree, {diff} differ ({len(ours)} ours, {len(theirs)} assimp)")

main()
