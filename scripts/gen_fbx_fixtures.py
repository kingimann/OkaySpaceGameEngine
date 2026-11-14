#!/usr/bin/env python3
"""Writes the small FBX test fixtures in tests/assets/fbx/ (binary 7.4, binary 7.5 with 64-bit
offsets, and ASCII), from one scene description, using only the standard library.

  python3 scripts/gen_fbx_fixtures.py [outdir]

The files are committed; this script is how they were made and how to change them. It is our own
writer (public format knowledge), so the reader tests do not depend on any other FBX tool.
"""
import math
import os
import struct
import sys
import zlib

KTIME = 46186158000


class P:
    """A typed property: kind is one of Y C I F D L S R or array kinds f d l i b."""
    def __init__(self, kind, value):
        self.kind, self.value = kind, value


def I(v): return P('I', v)
def L(v): return P('L', v)
def D(v): return P('D', v)
def S(v): return P('S', v)
def R(v): return P('R', v)
def AD(v): return P('d', list(v))
def AI(v): return P('i', list(v))
def AL(v): return P('l', list(v))
def AF(v): return P('f', list(v))
def AC(v): return P('c', list(v))   # byte array ('c', written by newer SDKs for thumbnails)


class N:
    def __init__(self, name, props=(), children=()):
        self.name, self.props, self.children = name, list(props), list(children)


# ---------------------------------------------------------------- binary writer
def bin_prop(p, compress):
    k, v = p.kind, p.value
    if k == 'I': return b'I' + struct.pack('<i', v)
    if k == 'L': return b'L' + struct.pack('<q', v)
    if k == 'D': return b'D' + struct.pack('<d', v)
    if k == 'F': return b'F' + struct.pack('<f', v)
    if k == 'C': return b'C' + struct.pack('<B', 1 if v else 0)
    if k == 'Y': return b'Y' + struct.pack('<h', v)
    if k == 'S':
        b = v.encode('latin-1')
        return b'S' + struct.pack('<I', len(b)) + b
    if k == 'R': return b'R' + struct.pack('<I', len(v)) + v
    fmt = {'d': 'd', 'f': 'f', 'l': 'q', 'i': 'i', 'b': 'B', 'c': 'B'}[k]
    raw = struct.pack('<%d%s' % (len(v), fmt), *v)
    if compress and len(raw) >= 64:
        z = zlib.compress(raw, 9)
        return k.encode() + struct.pack('<III', len(v), 1, len(z)) + z
    return k.encode() + struct.pack('<III', len(v), 0, len(raw)) + raw


def bin_node(n, pos, wide, compress):
    """Serialises node n assuming it starts at absolute offset pos."""
    props = b''.join(bin_prop(p, compress) for p in n.props)
    name = n.name.encode()
    hdr = 25 if wide else 13
    body_start = pos + hdr + len(name) + len(props)
    kids = b''
    cur = body_start
    for c in n.children:
        cb = bin_node(c, cur, wide, compress)
        kids += cb
        cur += len(cb)
    if n.children:
        kids += b'\0' * hdr
        cur += hdr
    if wide:
        head = struct.pack('<QQQB', cur, len(n.props), len(props), len(name))
    else:
        head = struct.pack('<IIIB', cur, len(n.props), len(props), len(name))
    return head + name + props + kids


def write_binary(path, top, version, compress=True):
    wide = version >= 7500
    out = b'Kaydara FBX Binary  \x00\x1a\x00' + struct.pack('<I', version)
    for n in top:
        out += bin_node(n, len(out), wide, compress)
    out += b'\0' * (25 if wide else 13)
    out += b'\0' * 16 + bytes(range(16)) + b'\0' * 4   # a plausible footer (the reader ignores it)
    with open(path, 'wb') as f:
        f.write(out)


# ---------------------------------------------------------------- ascii writer
def num(x):
    if isinstance(x, float):
        return repr(x)
    return str(x)


def asc_node(n, ind, out):
    pad = '\t' * ind
    parts = []
    arr = None
    for p in n.props:
        if p.kind in ('d', 'f', 'l', 'i', 'b'):
            arr = p
        elif p.kind in ('S',):
            parts.append('"%s"' % p.value)
        elif p.kind == 'R':
            parts.append('"%s"' % __import__('base64').b64encode(p.value).decode())
        else:
            parts.append(num(p.value))
    line = '%s%s: ' % (pad, n.name)
    if arr is not None:
        out.append(line + '*%d {' % len(arr.value))
        out.append('%s\ta: %s' % (pad, ','.join(num(x) for x in arr.value)))
        out.append(pad + '}')
        return
    line += ', '.join(parts)
    if n.children:
        out.append(line + ' {')
        for c in n.children:
            asc_node(c, ind + 1, out)
        out.append(pad + '}')
    else:
        out.append(line)


def write_ascii(path, top, version):
    out = ['; FBX %d.%d.0 project file' % (version // 1000, (version % 1000) // 100), '; ------------------', '']
    for n in top:
        asc_node(n, 0, out)
        out.append('')
    with open(path, 'w') as f:
        f.write('\n'.join(out))


# ---------------------------------------------------------------- scene description
def prop70(name, typ, label, flags, *vals):
    ps = [S(name), S(typ), S(label), S(flags)]
    for v in vals:
        ps.append(S(v) if isinstance(v, str) else D(float(v)))
    return N('P', ps)


def props70(*ps):
    return N('Properties70', [], ps)


def settings(up=1, upsign=1, front=2, frontsign=1, coord=0, coordsign=1, unit=1.0):
    return N('GlobalSettings', [], [
        N('Version', [I(1000)]),
        props70(
            prop70('UpAxis', 'int', 'Integer', '', up), prop70('UpAxisSign', 'int', 'Integer', '', upsign),
            prop70('FrontAxis', 'int', 'Integer', '', front), prop70('FrontAxisSign', 'int', 'Integer', '', frontsign),
            prop70('CoordAxis', 'int', 'Integer', '', coord), prop70('CoordAxisSign', 'int', 'Integer', '', coordsign),
            prop70('UnitScaleFactor', 'double', 'Number', '', unit))])


def objname(cls, name):
    return S('%s::%s' % (cls, name)) if False else S(name + '\x00\x01' + cls)


class Scene:
    def __init__(self, ascii_names=False):
        self.objects, self.conns = [], []
        self.ascii = ascii_names
        self._id = 1000

    def nid(self):
        self._id += 1
        return self._id

    def nm(self, cls, name):
        return S('%s::%s' % (cls, name)) if self.ascii else S(name + '\x00\x01' + cls)

    def connect(self, child, parent, prop=None):
        if prop is None:
            self.conns.append(N('C', [S('OO'), L(child), L(parent)]))
        else:
            self.conns.append(N('C', [S('OP'), L(child), L(parent), S(prop)]))

    def model(self, name, parent=0, t=(0, 0, 0), r=(0, 0, 0), s=(1, 1, 1), kind='Mesh', extra=()):
        i = self.nid()
        pl = [prop70('Lcl Translation', 'Lcl Translation', '', 'A', *t),
              prop70('Lcl Rotation', 'Lcl Rotation', '', 'A', *r),
              prop70('Lcl Scaling', 'Lcl Scaling', '', 'A', *s)] + list(extra)
        self.objects.append(N('Model', [L(i), self.nm('Model', name), S(kind)], [N('Version', [I(232)]), props70(*pl)]))
        self.connect(i, parent)
        return i

    def geometry(self, name, children):
        i = self.nid()
        self.objects.append(N('Geometry', [L(i), self.nm('Geometry', name), S('Mesh')], children))
        return i

    def material(self, name, diffuse=(0.8, 0.8, 0.8), emissive=None, shininess=None):
        i = self.nid()
        pl = [prop70('DiffuseColor', 'Color', '', 'A', *diffuse)]
        if emissive: pl.append(prop70('EmissiveColor', 'Color', '', 'A', *emissive))
        if shininess is not None: pl.append(prop70('ShininessExponent', 'Number', '', 'A', shininess))
        self.objects.append(N('Material', [L(i), self.nm('Material', name), S('')], [N('Version', [I(102)]), N('ShadingModel', [S('phong')]), props70(*pl)]))
        return i

    def texture(self, name, filename, content=None):
        i = self.nid()
        self.objects.append(N('Texture', [L(i), self.nm('Texture', name), S('')], [
            N('Type', [S('TextureVideoClip')]), N('FileName', [S(filename)]), N('RelativeFilename', [S(filename.replace('\\', '/').split('/')[-1])])]))
        if content is not None:
            v = self.nid()
            self.objects.append(N('Video', [L(v), self.nm('Video', name), S('Clip')], [
                N('Type', [S('Clip')]), N('FileName', [S(filename)]), N('Content', [R(content)])]))
            self.connect(v, i)
        return i

    def top(self, up=1, upsign=1, front=2, frontsign=1, coord=0, coordsign=1, unit=1.0):
        defs = N('Definitions', [], [N('Version', [I(100)]), N('Count', [I(len(self.objects))])])
        return [
            N('FBXHeaderExtension', [], [N('FBXHeaderVersion', [I(1003)]), N('FBXVersion', [I(7400)]),
                                         N('Creator', [S('okay gen_fbx_fixtures.py')])]),
            settings(up, upsign, front, frontsign, coord, coordsign, unit),
            defs,
            N('Objects', [], self.objects),
            N('Connections', [], self.conns),
        ]


def geo_children(verts, polys, normals=None, nmap='ByPolygonVertex', uvs=None, uvidx=None, mats=None, mmap='AllSame', edges=None):
    ch = [N('Vertices', [AD([c for v in verts for c in v])]),
          N('PolygonVertexIndex', [AI(polys)])]
    if edges is not None:
        ch.append(N('Edges', [AI(edges)]))
    ch.append(N('GeometryVersion', [I(124)]))
    if normals is not None:
        ch.append(N('LayerElementNormal', [I(0)], [N('Version', [I(101)]), N('Name', [S('')]),
                  N('MappingInformationType', [S(nmap)]), N('ReferenceInformationType', [S('Direct')]),
                  N('Normals', [AD([c for n in normals for c in n])])]))
    if uvs is not None:
        kids = [N('Version', [I(101)]), N('Name', [S('UVMap')]), N('MappingInformationType', [S('ByPolygonVertex')]),
                N('ReferenceInformationType', [S('IndexToDirect' if uvidx is not None else 'Direct')]),
                N('UV', [AD([c for u in uvs for c in u])])]
        if uvidx is not None: kids.append(N('UVIndex', [AI(uvidx)]))
        ch.append(N('LayerElementUV', [I(0)], kids))
    if mats is not None:
        ch.append(N('LayerElementMaterial', [I(0)], [N('Version', [I(101)]), N('Name', [S('')]),
                  N('MappingInformationType', [S(mmap)]), N('ReferenceInformationType', [S('IndexToDirect')]),
                  N('Materials', [AI(mats)])]))
    ch.append(N('Layer', [I(0)], [N('Version', [I(100)]), N('LayerElement', [], [N('Type', [S('LayerElementNormal')]), N('TypedIndex', [I(0)])])]))
    return ch


CUBE_V = [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1), (-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1)]
CUBE_Q = [(0, 1, 2, 3), (5, 4, 7, 6), (4, 0, 3, 7), (1, 5, 6, 2), (3, 2, 6, 7), (4, 5, 1, 0)]
CUBE_N = [(0, 0, 1), (0, 0, -1), (-1, 0, 0), (1, 0, 0), (0, 1, 0), (0, -1, 0)]


def quad_polys(quads):
    out = []
    for q in quads:
        out += list(q[:-1]) + [~q[-1]]
    return out


def cube_scene(ascii_names, up, upsign, front, frontsign, unit, tex_content=None):
    sc = Scene(ascii_names)
    polys = quad_polys(CUBE_Q)
    normals = [n for n in CUBE_N for _ in range(4)]
    uvs = [(0, 0), (1, 0), (1, 1), (0, 1)]
    uvidx = [i % 4 for i in range(24)]
    g = sc.geometry('CubeGeo', geo_children(CUBE_V, polys, normals, 'ByPolygonVertex', uvs, uvidx, [0], 'AllSame'))
    m = sc.model('Cube', 0, t=(10, 20, 30), r=(0, 30, 0), s=(1, 2, 1))
    mat = sc.material('Red', (0.9, 0.1, 0.1), emissive=(0.1, 0.0, 0.0), shininess=25)
    tex = sc.texture('Diffuse', 'C:\\art\\red.png', tex_content)
    sc.connect(tex, mat, 'DiffuseColor')
    sc.connect(g, m)
    sc.connect(mat, m)
    return sc.top(up, upsign, front, frontsign, 0, 1, unit)


def ngon_scene():
    sc = Scene(False)
    # An L-shaped (concave) hexagon in the XY plane, a pentagon fan, and a triangle: normals ByVertice.
    verts = [(0, 0, 0), (2, 0, 0), (2, 1, 0), (1, 1, 0), (1, 2, 0), (0, 2, 0),
             (3, 0, 0), (4, 0, 0), (4.5, 1, 0), (3.5, 1.8, 0), (2.8, 1, 0),
             (5, 0, 0), (6, 0, 0), (5.5, 1, 0)]
    polys = [0, 1, 2, 3, 4, ~5, 6, 7, 8, 9, ~10, 11, 12, ~13]
    normals = [(0, 0, 1)] * len(verts)
    g = sc.geometry('NGon', geo_children(verts, polys, normals, 'ByVertice', None, None, [0, 1, 1], 'ByPolygon'))
    m = sc.model('NGon', 0)
    a = sc.material('A', (1, 0, 0)); b = sc.material('B', (0, 0, 1))
    sc.connect(g, m); sc.connect(a, m); sc.connect(b, m)
    return sc.top()


def plain_scene():
    """No normals, no UVs, no materials: the reader has to generate normals; unfinished polygons are skipped."""
    sc = Scene(False)
    verts = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0.5, 0.5, 1)]
    polys = [0, 1, ~2, 0, 2, ~3, 0, 3, ~4, 1, ~4]   # last "polygon" has only 2 corners -> skipped
    g = sc.geometry('Pyramid', geo_children(verts, polys))
    parent = sc.model('Parent', 0, t=(1, 0, 0), kind='Null')
    child = sc.model('Pyr', parent, t=(0, 2, 0), r=(10, 20, 30), s=(2, 2, 2))
    sc.connect(g, child)
    return sc.top()


def pivot_scene():
    sc = Scene(False)
    polys = quad_polys(CUBE_Q)
    g = sc.geometry('Geo', geo_children(CUBE_V, polys, [n for n in CUBE_N for _ in range(4)]))
    extra = [prop70('RotationPivot', 'Vector3D', 'Vector', '', 0.5, 0.25, 0), prop70('ScalingPivot', 'Vector3D', 'Vector', '', 0.2, 0, 0),
             prop70('RotationOffset', 'Vector3D', 'Vector', '', 0, 0.1, 0), prop70('ScalingOffset', 'Vector3D', 'Vector', '', 0, 0, 0.3),
             prop70('PreRotation', 'Vector3D', 'Vector', '', 0, 0, 20), prop70('PostRotation', 'Vector3D', 'Vector', '', 5, 0, 0),
             prop70('RotationOrder', 'enum', '', '', 4),
             prop70('GeometricTranslation', 'Vector3D', 'Vector', '', 0.5, 0, 0), prop70('GeometricScaling', 'Vector3D', 'Vector', '', 1, 1, 2)]
    m = sc.model('Piv', 0, t=(1, 2, 3), r=(15, 25, 35), s=(1, 2, 1), extra=extra)
    sc.connect(g, m)
    return sc.top()


def curve(sc, name, keys, flags, slopes=None):
    i = sc.nid()
    n = len(keys)
    ch = [N('Default', [D(keys[0][1])]), N('KeyVer', [I(4008)]),
          N('KeyTime', [AL([int(round(t * KTIME)) for t, _ in keys])]),
          N('KeyValueFloat', [AF([v for _, v in keys])]),
          N('KeyAttrFlags', [AI([flags])]),
          N('KeyAttrDataFloat', [AF([slopes[0], slopes[1], 0, 0] if slopes else [0, 0, 0, 0])]),
          N('KeyAttrRefCount', [AI([n])])]
    sc.objects.append(N('AnimationCurve', [L(i), sc.nm('AnimCurve', name), S('')], ch))
    return i


def curve_node(sc, name, target, prop, defaults, curves):
    i = sc.nid()
    sc.objects.append(N('AnimationCurveNode', [L(i), sc.nm('AnimCurveNode', name), S('')], [
        props70(prop70('d|X', 'Number', '', 'A', defaults[0]), prop70('d|Y', 'Number', '', 'A', defaults[1]), prop70('d|Z', 'Number', '', 'A', defaults[2]))]))
    sc.connect(i, target, prop)
    for axis, c in zip('XYZ', curves):
        if c is not None:
            sc.connect(c, i, 'd|' + axis)
    return i


def rig_scene(ascii_names):
    """A 3-segment tube skinned to a 3-bone chain, with a blend shape and a 2 s animation."""
    sc = Scene(ascii_names)
    rings = 4
    verts, polys = [], []
    for r in range(rings):
        for (x, z) in [(-0.5, 0.5), (0.5, 0.5), (0.5, -0.5), (-0.5, -0.5)]:
            verts.append((x, float(r), z))
    for r in range(rings - 1):
        for k in range(4):
            a, b = r * 4 + k, r * 4 + (k + 1) % 4
            polys += [a, b, (r + 1) * 4 + (k + 1) % 4, ~((r + 1) * 4 + k)]
    g = sc.geometry('Tube', geo_children(verts, polys, None, None, [(0, 0), (1, 0), (1, 1), (0, 1)], [i % 4 for i in range(len(polys))]))
    mesh = sc.model('Tube', 0, t=(0, 0, 0))
    mat = sc.material('Skin', (0.3, 0.6, 0.9))
    sc.connect(g, mesh); sc.connect(mat, mesh)
    pre = [prop70('PreRotation', 'Vector3D', 'Vector', '', 0, 0, 10)]
    b0 = sc.model('Bone0', 0, t=(0, 0, 0), kind='LimbNode')
    b1 = sc.model('Bone1', b0, t=(0, 1, 0), kind='LimbNode', extra=pre)
    b2 = sc.model('Bone2', b1, t=(0, 1, 0), kind='LimbNode', extra=[prop70('RotationOrder', 'enum', '', '', 5)])
    bones = [b0, b1, b2]
    # skin deformer + clusters
    skin = sc.nid()
    sc.objects.append(N('Deformer', [L(skin), sc.nm('Deformer', 'Skin'), S('Skin')], [N('Version', [I(101)])]))
    sc.connect(skin, g)
    weights = [[(0, 1.0)], [(0, 0.5), (1, 0.5)], [(1, 0.5), (2, 0.5)], [(2, 1.0)]]
    for bi, bone in enumerate(bones):
        idx, w = [], []
        for r in range(rings):
            for (b, wt) in weights[r]:
                if b == bi:
                    for k in range(4):
                        idx.append(r * 4 + k); w.append(wt)
        cl = sc.nid()
        meshtobone = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -float(bi), 0, 1]   # Transform = mesh node relative to the bone (what Blender writes)
        link = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, float(bi), 0, 1]   # bone bind world: translation (0, bi, 0), column-major
        sc.objects.append(N('Deformer', [L(cl), sc.nm('SubDeformer', 'Cluster%d' % bi), S('Cluster')], [
            N('Version', [I(100)]), N('Indexes', [AI(idx)]), N('Weights', [AD(w)]),
            N('Transform', [AD(meshtobone)]), N('TransformLink', [AD(link)])]))
        sc.connect(cl, skin)
        sc.connect(bone, cl)
    # blend shape moving the top ring outwards
    bs = sc.nid()
    sc.objects.append(N('Deformer', [L(bs), sc.nm('Deformer', 'Morph'), S('BlendShape')], [N('Version', [I(100)])]))
    sc.connect(bs, g)
    ch = sc.nid()
    sc.objects.append(N('Deformer', [L(ch), sc.nm('SubDeformer', 'Fat'), S('BlendShapeChannel')], [
        N('Version', [I(100)]), N('DeformPercent', [D(0.0)]), N('FullWeights', [AD([100.0])]),
        props70(prop70('DeformPercent', 'Number', '', 'A', 25.0))]))
    sc.connect(ch, bs)
    shp = sc.nid()
    sc.objects.append(N('Geometry', [L(shp), sc.nm('Geometry', 'FatShape'), S('Shape')], [
        N('Version', [I(100)]), N('Indexes', [AI([12, 13, 14, 15])]),
        N('Vertices', [AD([-0.2, 0, 0.2, 0.2, 0, 0.2, 0.2, 0, -0.2, -0.2, 0, -0.2])]),
        N('Normals', [AD([0] * 12)])]))
    sc.connect(shp, ch)
    # animation: stack -> layer -> curve nodes -> curves
    stack = sc.nid()
    sc.objects.append(N('AnimationStack', [L(stack), sc.nm('AnimStack', 'Take 001'), S('')], [props70()]))
    layer = sc.nid()
    sc.objects.append(N('AnimationLayer', [L(layer), sc.nm('AnimLayer', 'BaseLayer'), S('')], []))
    sc.connect(layer, stack)
    cz = curve(sc, 'b1rz', [(0.0, 0.0), (0.5, 45.0), (1.0, 0.0), (1.5, -45.0)], 0x408, (30.0, 30.0))   # cubic, user tangents
    cn1 = curve_node(sc, 'R', b1, 'Lcl Rotation', (0, 0, 0), [None, None, cz])
    cy = curve(sc, 'b2ty', [(0.0, 1.0), (2.0, 1.5)], 0x4)                                              # linear
    cn2 = curve_node(sc, 'T', b2, 'Lcl Translation', (0, 1, 0), [None, cy, None])
    cs = curve(sc, 'b0sx', [(0.0, 1.0), (1.0, 1.0), (2.0, 2.0)], 0x2)                                  # constant steps
    cn3 = curve_node(sc, 'S', b0, 'Lcl Scaling', (1, 1, 1), [cs, None, None])
    cb = curve(sc, 'fat', [(0.0, 0.0), (2.0, 100.0)], 0x4)
    cn4 = curve_node(sc, 'Fat', ch, 'DeformPercent', (0, 0, 0), [cb, None, None])
    for cn in (cn1, cn2, cn3, cn4):
        sc.connect(cn, layer)
    return sc.top(up=1, unit=1.0)


def takes_scene():
    """Three takes that share one timeline (found in a real exporter file): each has its own layer, curve node and
    copy of the curves, and a time span (LocalStart / LocalStop) saying which part of them it plays.
    The header carries a byte-array ('c') thumbnail, which newer SDKs write."""
    sc = Scene(False)
    g = sc.geometry('Geo', geo_children(CUBE_V, quad_polys(CUBE_Q), [n for n in CUBE_N for _ in range(4)]))
    m = sc.model('Mover', 0)
    sc.connect(g, m)
    for name, span in (('first', (0.0, 1.0)), ('second', (1.0, 2.0)), ('whole', None)):
        stack = sc.nid()
        pl = [prop70('LocalStart', 'KTime', 'Time', '', span[0] * KTIME), prop70('LocalStop', 'KTime', 'Time', '', span[1] * KTIME)] if span else []
        sc.objects.append(N('AnimationStack', [L(stack), sc.nm('AnimStack', name), S('')], [props70(*pl)]))
        layer = sc.nid()
        sc.objects.append(N('AnimationLayer', [L(layer), sc.nm('AnimLayer', 'BaseLayer'), S('')], []))
        sc.connect(layer, stack)
        cx = curve(sc, 'x', [(0.0, 0.0), (1.0, 10.0), (2.0, 0.0)], 0x4)   # linear: up to 10 at t=1, back to 0 at t=2
        cn = curve_node(sc, 'T', m, 'Lcl Translation', (0, 0, 0), [cx, None, None])
        sc.connect(cn, layer)
    top = sc.top()
    top[0].children.append(N('Thumbnail', [], [N('Version', [I(100)]), N('Format', [I(0)]), N('ImageData', [AC([1, 2, 3, 4, 250, 251, 252, 253] * 20)])]))
    return top


def bridge_scene():
    """A square with a square hole cut into it, written as ONE polygon with a bridge: the outer ring, back to its
    start, the inner ring (opposite direction), back to its start. Corners repeat; area 16 - 4 = 12."""
    sc = Scene(False)
    verts = [(0, 0, 0), (4, 0, 0), (4, 4, 0), (0, 4, 0), (1, 1, 0), (1, 3, 0), (3, 3, 0), (3, 1, 0)]
    polys = [0, 1, 2, 3, 0, 4, 5, 6, 7, ~4]   # inner ring ends on its first corner, then the polygon closes back to corner 0
    g = sc.geometry('Annulus', geo_children(verts, polys, [(0, 0, 1)] * len(verts), 'ByVertice'))
    m = sc.model('Annulus', 0)
    sc.connect(g, m)
    return sc.top()


PBR_SETS = [
    # (name, class id a, class id b, prefix, base colour prop, texture prop for the base colour, normal prop, extra props)
    ('Physical', 0x3d6b1cec, 0xdeadc001, '3dsMax|Parameters|', 'base_color', 'bump_map', [('roughness', 0.3), ('metalness', 0.9), ('emission', 2.0)], 'emit_color'),
    ('MetalRough', 0xd00f1e00, 0xbe77e500, '3dsMax|main|', 'basecolor', 'norm_map', [('roughness', 0.25), ('metalness', 1.0), ('useGlossiness', 2.0)], 'emit_color'),
    ('SpecGloss', 0xd00f1e00, 0x01dbad33, '3dsMax|main|', 'basecolor', 'norm_map', [('glossiness', 0.8), ('useGlossiness', 1.0)], 'emit_color'),
    ('Gltf', 0x38420192, 0x45fe4e1b, '3dsMax|', 'main|baseColor', 'main|normal', [('main|roughness', 0.6), ('main|metalness', 0.5)], 'main|emissionColor'),
]


def pbr_scene():
    """One triangle per PBR property set a modelling tool writes (3ds Max Physical Material, the two 3ds Max PBR
    materials, the 3ds Max glTF material), each with a base colour, roughness / glossiness, metalness, emission, a
    base colour texture and a normal map plugged into the prefixed property; plus a classic Phong material. The
    class ids are written the way the SDK does, as signed 32-bit integers (so 0xdeadc001 is negative)."""
    sc = Scene(False)
    def signed(v):
        return v - (1 << 32) if v >= (1 << 31) else v
    verts = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    for k, (name, ca, cb, pre, base, norm, extra, emit) in enumerate(PBR_SETS):
        g = sc.geometry(name + 'Geo', geo_children(verts, [0, 1, ~2], [(0, 0, 1)] * 3, 'ByVertice', None, None, [0], 'AllSame'))
        m = sc.model(name, 0, t=(k * 2, 0, 0))
        mid = sc.nid()
        pl = [prop70('3dsMax|ClassIDa', 'int', 'Integer', '', signed(ca)), prop70('3dsMax|ClassIDb', 'int', 'Integer', '', signed(cb)),
              prop70(pre + base, 'Color', '', 'A', 0.2, 0.4, 0.6), prop70(pre + emit, 'Color', '', 'A', 0.5, 0.25, 0.125)]
        # a classic property that must NOT win over the PBR set
        pl.append(prop70('DiffuseColor', 'Color', '', 'A', 0.9, 0.9, 0.9))
        for pn, pv in extra:
            pl.append(prop70(pre + pn, 'Number', '', 'A', pv))
        sm = 'unknown'
        sc.objects.append(N('Material', [L(mid), sc.nm('Material', name + 'Mat'), S('')], [N('Version', [I(102)]), N('ShadingModel', [S(sm)]), props70(*pl)]))
        t0 = sc.texture(name + 'Albedo', 'C:\\art\\' + name + '_albedo.png')
        t1 = sc.texture(name + 'Normal', 'C:\\art\\' + name + '_normal.png')
        sc.connect(t0, mid, pre + ('base_color_map' if base == 'basecolor' else base + '_map' if base != 'main|baseColor' else base))
        sc.connect(t1, mid, pre + norm)
        sc.connect(g, m)
        sc.connect(mid, m)
    # control: the same class id on a material that says phong keeps the classic reading (ufbx's rule)
    g = sc.geometry('PhongGeo', geo_children(verts, [0, 1, ~2], [(0, 0, 1)] * 3, 'ByVertice', None, None, [0], 'AllSame'))
    m = sc.model('Phong', 0, t=(20, 0, 0))
    mid = sc.nid()
    pl = [prop70('3dsMax|ClassIDa', 'int', 'Integer', '', signed(PBR_SETS[0][1])), prop70('3dsMax|ClassIDb', 'int', 'Integer', '', signed(PBR_SETS[0][2])),
          prop70('3dsMax|Parameters|base_color', 'Color', '', 'A', 0.2, 0.4, 0.6), prop70('DiffuseColor', 'Color', '', 'A', 0.9, 0.8, 0.7),
          prop70('ShininessExponent', 'Number', '', 'A', 25.0)]
    sc.objects.append(N('Material', [L(mid), sc.nm('Material', 'PhongMat'), S('')], [N('Version', [I(102)]), N('ShadingModel', [S('phong')]), props70(*pl)]))
    sc.connect(g, m); sc.connect(mid, m)
    return sc.top()


def orphan_video_scene(content):
    """A texture whose connected Video has no picture while another Video (not connected to anything) carries it: matched
    by file name. One triangle with a material that has the texture on DiffuseColor."""
    sc = Scene(False)
    g = sc.geometry('TriGeo', geo_children([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [0, 1, ~2], [(0, 0, 1)] * 3, 'ByVertice', None, None, [0], 'AllSame'))
    m = sc.model('Tri', 0)
    mat = sc.material('M', (0.5, 0.5, 0.5))
    tex = sc.texture('Map', 'C:\\art\\grid.png')
    own = sc.nid()   # the video connected to the texture: same file name, no picture
    sc.objects.append(N('Video', [L(own), sc.nm('Video', 'Map'), S('Clip')], [
        N('Type', [S('Clip')]), N('FileName', [S('C:\\art\\grid.png')]), N('RelativeFilename', [S('grid.png')])]))
    sc.connect(own, tex)
    v = sc.nid()
    sc.objects.append(N('Video', [L(v), sc.nm('Video', 'Map'), S('Clip')], [
        N('Type', [S('Clip')]), N('FileName', [S('C:\\art\\grid.png')]), N('RelativeFilename', [S('grid.png')]), N('Content', [R(content)])]))
    sc.connect(tex, mat, 'DiffuseColor')
    sc.connect(g, m)
    sc.connect(mat, m)
    return sc.top()


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'tests', 'assets', 'fbx')
    os.makedirs(out, exist_ok=True)
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0)) + \
        chunk(b'IDAT', zlib.compress(b'\x00\xe6\x19\x19\xff')) + chunk(b'IEND', b'')   # 1x1 red pixel
    # Blender-style: Z up, -Y front, centimeters
    write_binary(os.path.join(out, 'cube_zup_cm_bin.fbx'), cube_scene(False, 2, 1, 1, -1, 1.0, png), 7400)
    write_ascii(os.path.join(out, 'cube_zup_cm_ascii.fbx'), cube_scene(True, 2, 1, 1, -1, 1.0), 7400)
    write_binary(os.path.join(out, 'cube_yup_m_bin75.fbx'), cube_scene(False, 1, 1, 2, 1, 100.0), 7500)
    write_binary(os.path.join(out, 'ngon_bin.fbx'), ngon_scene(), 7400, compress=False)
    write_binary(os.path.join(out, 'plain_bin.fbx'), plain_scene(), 7400)
    write_binary(os.path.join(out, 'pivot_bin.fbx'), pivot_scene(), 7400)
    write_binary(os.path.join(out, 'rig_bin75.fbx'), rig_scene(False), 7500)
    write_ascii(os.path.join(out, 'rig_ascii.fbx'), rig_scene(True), 7400)
    write_binary(os.path.join(out, 'takes_bin.fbx'), takes_scene(), 7400)
    write_binary(os.path.join(out, 'bridge_bin.fbx'), bridge_scene(), 7400, compress=False)
    write_binary(os.path.join(out, 'orphan_video_bin.fbx'), orphan_video_scene(png), 7400, compress=False)
    write_binary(os.path.join(out, 'pbr_bin.fbx'), pbr_scene(), 7400, compress=False)
    for f in sorted(os.listdir(out)):
        print(f, os.path.getsize(os.path.join(out, f)))


if __name__ == '__main__':
    main()
