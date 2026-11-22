#!/usr/bin/env python3
"""Writes large synthetic FBX files for the FBX reader benchmark (tests/bench_fbx_reader.cpp).

  python3 scripts/gen_fbx_bench.py <outdir>

The files are NOT committed (they are tens of megabytes); this script makes them again in a few seconds. It uses
our own fixture writer (scripts/gen_fbx_fixtures.py), so nothing here comes from another FBX tool.

  bench_grid_200k_bin75.fbx   one 320 x 320 quad grid: 204,800 triangles, a normal per corner, UVs with an index
                              array, zlib-compressed arrays, binary 7.5 (64-bit offsets)
  bench_grid_51k_ascii.fbx    the same kind of grid, 160 x 160 (51,200 triangles), in ASCII
  bench_nodes_3000_bin.fbx    3,000 models in a 4-way tree, each with its own small box geometry, 16 materials
  bench_anim_rig_bin75.fbx    a 64-bone chain skinning a 64 x 16 tube, X/Y/Z rotation curves on every bone with
                              1,800 auto-tangent keys each (60 seconds at 30 frames per second), 345,600 keys in all
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_fbx_fixtures import (N, I, D, S, L, AD, AI, AL, AF, Scene, KTIME, prop70, props70, geo_children,  # noqa: E402
                              quad_polys, write_binary, write_ascii, CUBE_V, CUBE_Q, CUBE_N)


def grid_scene(cells, ascii_names):
    """A cells x cells grid of unit quads in the XZ plane with a gentle height wave."""
    sc = Scene(ascii_names)
    n = cells + 1
    verts = []
    for j in range(n):
        for i in range(n):
            verts.append((float(i), 0.25 * math.sin(i * 0.1) * math.cos(j * 0.1), float(j)))
    polys, normals, uvidx = [], [], []
    for j in range(cells):
        for i in range(cells):
            a = j * n + i
            polys += [a, a + n, a + n + 1, ~(a + 1)]
            normals += [(0.0, 1.0, 0.0)] * 4
            uvidx += [a, a + n, a + n + 1, a + 1]
    uvs = [(i / cells, j / cells) for j in range(n) for i in range(n)]
    g = sc.geometry('GridGeo', geo_children(verts, polys, normals, 'ByPolygonVertex', uvs, uvidx, [0], 'AllSame'))
    m = sc.model('Grid', 0)
    mat = sc.material('Ground', (0.4, 0.6, 0.3))
    sc.connect(g, m)
    sc.connect(mat, m)
    return sc.top()


def nodes_scene(count, fanout):
    sc = Scene(False)
    mats = [sc.material('M%d' % k, ((k % 4) / 3.0, ((k // 4) % 4) / 3.0, 0.5)) for k in range(16)]
    polys = quad_polys(CUBE_Q)
    normals = [nn for nn in CUBE_N for _ in range(4)]
    ids = []
    for k in range(count):
        parent = ids[(k - 1) // fanout] if k else 0
        m = sc.model('Node%d' % k, parent, t=(1.0, 0.5, 0.0), r=(0.0, 10.0 * (k % 7), 0.0), s=(0.9, 0.9, 0.9))
        g = sc.geometry('Box%d' % k, geo_children(CUBE_V, polys, normals, 'ByPolygonVertex', None, None, [0], 'AllSame'))
        sc.connect(g, m)
        sc.connect(mats[k % 16], m)
        ids.append(m)
    return sc.top()


def anim_curve(sc, name, values, fps):
    """One curve with a key per frame, cubic interpolation with auto tangents (flags 0x108), one shared flag entry."""
    i = sc.nid()
    n = len(values)
    times = [int(round(k * KTIME / fps)) for k in range(n)]
    sc.objects.append(N('AnimationCurve', [L(i), sc.nm('AnimCurve', name), S('')], [
        N('Default', [D(values[0])]), N('KeyVer', [I(4008)]),
        N('KeyTime', [AL(times)]), N('KeyValueFloat', [AF(values)]),
        N('KeyAttrFlags', [AI([0x108])]), N('KeyAttrDataFloat', [AF([0, 0, 0, 0])]), N('KeyAttrRefCount', [AI([n])])]))
    return i


def anim_rig_scene(bones, frames, fps, segments):
    sc = Scene(False)
    rings = bones
    verts, polys = [], []
    for r in range(rings):
        for k in range(segments):
            a = 2 * math.pi * k / segments
            verts.append((0.3 * math.cos(a), float(r), 0.3 * math.sin(a)))
    for r in range(rings - 1):
        for k in range(segments):
            a, b = r * segments + k, r * segments + (k + 1) % segments
            polys += [a, b, (r + 1) * segments + (k + 1) % segments, ~((r + 1) * segments + k)]
    g = sc.geometry('Tube', geo_children(verts, polys))
    mesh = sc.model('Tube', 0)
    mat = sc.material('Skin', (0.8, 0.6, 0.5))
    sc.connect(g, mesh)
    sc.connect(mat, mesh)
    ids = []
    for b in range(bones):
        ids.append(sc.model('Bone%d' % b, ids[-1] if ids else 0, t=(0.0, 1.0 if b else 0.0, 0.0), kind='LimbNode'))
    skin = sc.nid()
    sc.objects.append(N('Deformer', [L(skin), sc.nm('Deformer', 'Skin'), S('Skin')], [N('Version', [I(101)])]))
    sc.connect(skin, g)
    for b in range(bones):   # ring r follows bone r fully, and half of bone r-1
        idx, w = [], []
        for r in (b, b + 1):
            if r >= rings:
                continue
            wt = 1.0 if r == 0 else 0.5
            for k in range(segments):
                idx.append(r * segments + k)
                w.append(wt)
        cl = sc.nid()
        bind = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -float(b), 0, 1]
        link = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, float(b), 0, 1]
        sc.objects.append(N('Deformer', [L(cl), sc.nm('SubDeformer', 'Cluster%d' % b), S('Cluster')], [
            N('Version', [I(100)]), N('Indexes', [AI(idx)]), N('Weights', [AD(w)]),
            N('Transform', [AD(bind)]), N('TransformLink', [AD(link)])]))
        sc.connect(cl, skin)
        sc.connect(ids[b], cl)
    stack = sc.nid()
    span = int(round((frames - 1) * KTIME / fps))
    sc.objects.append(N('AnimationStack', [L(stack), sc.nm('AnimStack', 'Long'), S('')], [
        props70(prop70('LocalStart', 'KTime', 'Time', '', 0), prop70('LocalStop', 'KTime', 'Time', '', span))]))
    layer = sc.nid()
    sc.objects.append(N('AnimationLayer', [L(layer), sc.nm('AnimLayer', 'Base'), S('')], []))
    sc.connect(layer, stack)
    for b in range(bones):
        curves = []
        for ax in range(3):
            vals = [20.0 * math.sin(0.05 * f * (ax + 1) + b * 0.3) for f in range(frames)]
            curves.append(anim_curve(sc, 'b%d_r%d' % (b, ax), vals, fps))
        cn = sc.nid()
        sc.objects.append(N('AnimationCurveNode', [L(cn), sc.nm('AnimCurveNode', 'R'), S('')], [
            props70(prop70('d|X', 'Number', '', 'A', 0), prop70('d|Y', 'Number', '', 'A', 0), prop70('d|Z', 'Number', '', 'A', 0))]))
        sc.connect(cn, ids[b], 'Lcl Rotation')
        sc.connect(cn, layer)
        for axis, c in zip('XYZ', curves):
            sc.connect(c, cn, 'd|' + axis)
    return sc.top()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    write_binary(os.path.join(out, 'bench_grid_200k_bin75.fbx'), grid_scene(320, False), 7500)
    write_ascii(os.path.join(out, 'bench_grid_51k_ascii.fbx'), grid_scene(160, True), 7400)
    write_binary(os.path.join(out, 'bench_nodes_3000_bin.fbx'), nodes_scene(3000, 4), 7400)
    write_binary(os.path.join(out, 'bench_anim_rig_bin75.fbx'), anim_rig_scene(64, 1800, 30, 16), 7500)
    for f in sorted(os.listdir(out)):
        if f.startswith('bench_') and f.endswith('.fbx'):
            print(f, os.path.getsize(os.path.join(out, f)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
