#!/usr/bin/env python3
"""Writes the sample model files in examples/own_readers/: at least one file for every own model reader, so each
OKAY_OWN_<FMT> switch can be tried in the Windows download (docs/try_it.md, section 8; examples/own_readers/README.txt).

    python3 scripts/gen_own_reader_samples.py            write the files into examples/own_readers/
    python3 scripts/gen_own_reader_samples.py --check    write nothing; exit 1 if a file there differs from what this writes

Two kinds of file:
  * COPIES of our own committed test fixtures. Each was written by one of our scripts/gen_*_fixtures.py writers, or by
    hand for our tests (named in COPIES below), so nothing here comes from an outside tool or download.
  * Files WRITTEN HERE from the published structure of each format (the layouts our own readers and their tests use),
    with the standard library only. The shapes are ours: a cube (edge 2, centred on the origin) for most formats, a
    small hill for the two terrain formats, a few moving markers for motion capture, and a two-bone column that bends
    for the formats with skins and animation. No code, text or data was taken from any other project.

Deterministic: the same bytes on every run and every machine. No time stamps, no random numbers, and every float that
goes into a file is a short decimal written in this script or the result of exact arithmetic on such numbers.
The test tests/test_own_reader_samples.cpp imports every file with our own reader forced on.
"""
import os
import shutil
import struct
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(ROOT, "examples", "own_readers")

# ----------------------------------------------------------------------------------------------------------------------
# Copied fixtures: sample name -> (source in the repository, the script that wrote the source)
COPIES = {
    "skinned_strip.dae": ("tests/data/models/skinned_strip.dae", "written by hand for tests/test_collada_reader.cpp (see the comment at the top of the file)"),
    "cube.3ds": ("tests/data/models/cube.3ds", "scripts/gen_3ds_fixtures.py"),
    "cube.off": ("tests/data/models/cube.off", "scripts/gen_off_fixtures.py"),
    "walk.bvh": ("tests/data/models/walk.bvh", "scripts/gen_bvh_fixtures.py"),
    "rig.smd": ("tests/data/models/arm.smd", "scripts/gen_smd_fixtures.py"),
    "rig.md5mesh": ("tests/data/models/skel.md5mesh", "scripts/gen_md5_fixtures.py"),
    "rig.md5anim": ("tests/data/models/skel.md5anim", "scripts/gen_md5_fixtures.py"),
    "rig.ms3d": ("tests/data/models/rig.ms3d", "scripts/gen_ms3d_fixtures.py"),
    "scene.ase": ("tests/data/models/scene.ase", "scripts/gen_ase_fixtures.py"),
    "shapes.blend": ("tests/data/models/blend_attr_41.blend", "scripts/gen_blend_fixtures.py"),
    "rig.blend": ("tests/data/models/blend_skin_anim.blend", "scripts/gen_blend_fixtures.py"),
    "cube.fbx": ("tests/assets/fbx/cube_zup_cm_bin.fbx", "scripts/gen_fbx_fixtures.py"),
    "rig.fbx": ("tests/assets/fbx/rig_bin75.fbx", "scripts/gen_fbx_fixtures.py"),
    "cube.stl": ("tests/data/models/cube_ascii.stl", "written by hand for our own STL reader (PR #660)"),
    "cube.ply": ("tests/data/models/cube_ascii.ply", "written by hand for our own PLY reader (PR #660)"),
}

# ----------------------------------------------------------------------------------------------------------------------
# Shapes

# The cube: 8 corners and 6 quads, each quad counter-clockwise seen from outside in a right-handed frame (so the cross
# product of its first two edges points out of the cube). NORMALS[i] is the outward normal of QUADS[i].
CORNERS = [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
QUADS = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (2, 3, 7, 6), (1, 2, 6, 5), (0, 4, 7, 3)]
NORMALS = [(0, 0, -1), (0, 0, 1), (0, -1, 0), (0, 1, 0), (1, 0, 0), (-1, 0, 0)]
QUAD_UV = [(0, 0), (1, 0), (1, 1), (0, 1)]


def flip(poly):
    """The same polygon wound the other way round (first corner kept)."""
    return (poly[0],) + tuple(reversed(poly[1:]))


def quad_tris(q):
    return [(q[0], q[1], q[2]), (q[0], q[2], q[3])]


def cube_tris(reverse=False):
    """The 12 triangles of the cube as corner indices."""
    out = []
    for q in QUADS:
        for t in quad_tris(q):
            out.append(flip(t) if reverse else t)
    return out


def split_cube():
    """24 vertices (4 per face, so each face has its own normal and texture coordinates) and 6 quads over them.
    Returns (positions, normals, uvs, quads)."""
    pos, nrm, uv, quads = [], [], [], []
    for f, q in enumerate(QUADS):
        base = len(pos)
        for k, c in enumerate(q):
            pos.append(CORNERS[c])
            nrm.append(NORMALS[f])
            uv.append(QUAD_UV[k])
        quads.append((base, base + 1, base + 2, base + 3))
    return pos, nrm, uv, quads


def split_tris(quads, reverse=False):
    out = []
    for q in quads:
        for t in quad_tris(q):
            out.append(flip(t) if reverse else t)
    return out


# The rig: a square column of half-width 0.25 from y = 0 to y = 2 in three rings of four corners (Y up). Joint 0 "base"
# sits at the origin, joint 1 "top" one unit above it. Ring 0 follows the base, ring 2 the top, ring 1 half and half.
# The animation turns "top" about Z: 0 degrees, 45 degrees, back to 0, over one second.
COL_W = 0.25
COL_RING = [(-COL_W, -COL_W), (COL_W, -COL_W), (COL_W, COL_W), (-COL_W, COL_W)]   # (x, z)
COL_POS = [(x, float(r), z) for r in range(3) for (x, z) in COL_RING]
COL_WEIGHTS = [[(0, 1.0)]] * 4 + [[(0, 0.5), (1, 0.5)]] * 4 + [[(1, 1.0)]] * 4
H = 0.70710678   # cos 45 degrees
COL_NRM = [(-H, 0.0, -H), (H, 0.0, -H), (H, 0.0, H), (-H, 0.0, H)] * 3   # the corner normals, out from the axis
COL_UV = [(c / 4.0, r / 2.0) for r in range(3) for c in range(4)]
SIN22, COS22 = 0.38268343, 0.92387953   # half of a 45 degree turn
BEND_KEYS = [(0.0, (0.0, 0.0, 0.0, 1.0)), (0.5, (0.0, 0.0, SIN22, COS22)), (1.0, (0.0, 0.0, 0.0, 1.0))]   # time, (x y z w)


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def column_quads():
    """The 10 quads of the column (8 sides, bottom, top), each turned so it is counter-clockwise from outside."""
    quads = []
    for r in range(2):
        for c in range(4):
            d = (c + 1) % 4
            quads.append((4 * r + c, 4 * r + d, 4 * (r + 1) + d, 4 * (r + 1) + c))
    quads.append((0, 1, 2, 3))
    quads.append((8, 9, 10, 11))
    centre = (0.0, 1.0, 0.0)
    out = []
    for q in quads:
        p = [COL_POS[i] for i in q]
        n = _cross(_sub(p[1], p[0]), _sub(p[2], p[0]))
        mid = tuple(sum(v[k] for v in p) / 4.0 for k in range(3))
        out.append(q if _dot(n, _sub(mid, centre)) > 0 else flip(q))
    return out


def column_tris(reverse=False):
    return split_tris(column_quads(), reverse)


def hill(n, peak, fall):
    """An n x n grid (n odd) of whole-number heights: a round hill, peak in the middle, 0 at the edges."""
    h = n // 2
    return [[max(0, peak - fall * ((x - h) ** 2 + (y - h) ** 2)) for x in range(n)] for y in range(n)]


def num(v):
    """A short decimal for a text format."""
    if isinstance(v, int) or float(v).is_integer():
        return str(int(v))
    return ("%.8f" % v).rstrip("0")


# ----------------------------------------------------------------------------------------------------------------------
# Byte writers

def u8(v): return struct.pack("<B", v)
def u16(v): return struct.pack("<H", v & 0xFFFF)
def i16(v): return struct.pack("<h", v)
def u32(v): return struct.pack("<I", v & 0xFFFFFFFF)
def i32(v): return struct.pack("<i", v)
def f32(v): return struct.pack("<f", v)
def fixed(text, size): return text.encode("ascii")[:size].ljust(size, b"\0")
def be16(v): return struct.pack(">H", v & 0xFFFF)
def be32(v): return struct.pack(">I", v & 0xFFFFFFFF)
def bef32(v): return struct.pack(">f", v)


def crc32(data):
    c = 0xFFFFFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
    return c ^ 0xFFFFFFFF


def zip_stored(entries):
    """A zip archive with every entry stored (not compressed), a fixed date and no extra fields."""
    out, central = b"", b""
    for name, data in entries:
        n = name.encode("ascii")
        crc = crc32(data)
        offset = len(out)
        # local header: signature, version 2.0, flags 0, method 0 (stored), time 0, date 1980-01-01, crc, sizes, name length, extra 0
        out += be32(0x504B0304) + u16(20) + u16(0) + u16(0) + u16(0) + u16(0x21) + u32(crc) + u32(len(data)) + u32(len(data)) + u16(len(n)) + u16(0) + n + data
        central += (be32(0x504B0102) + u16(20) + u16(20) + u16(0) + u16(0) + u16(0) + u16(0x21) + u32(crc) + u32(len(data)) + u32(len(data)) +
                    u16(len(n)) + u16(0) + u16(0) + u16(0) + u16(0) + u32(0) + u32(offset) + n)
    end = be32(0x504B0506) + u16(0) + u16(0) + u16(len(entries)) + u16(len(entries)) + u32(len(central)) + u32(len(out)) + u16(0)
    return out + central + end


# ----------------------------------------------------------------------------------------------------------------------
# One writer per format. Each returns the file's bytes.

def threemf_cube():
    """3MF: a zip package with the content types, the relationship to the model part, and the model (millimetres, one blue
    base material, 8 vertices and 12 triangles counter-clockwise from outside)."""
    verts = "".join('<vertex x="%s" y="%s" z="%s"/>' % tuple(num(c) for c in p) for p in CORNERS)
    tris = "".join('<triangle v1="%d" v2="%d" v3="%d"/>' % t for t in cube_tris())
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">\n'
             ' <metadata name="Title">OkaySpace sample cube</metadata>\n'
             ' <resources>\n'
             '  <basematerials id="1"><base name="Blue" displaycolor="#3366CCFF"/></basematerials>\n'
             '  <object id="2" name="Cube" type="model" pid="1" pindex="0">\n'
             '   <mesh><vertices>' + verts + '</vertices>\n'
             '   <triangles>' + tris + '</triangles></mesh>\n'
             '  </object>\n'
             ' </resources>\n'
             ' <build><item objectid="2"/></build>\n'
             '</model>\n')
    types = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
             '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
             '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>\n')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>\n')
    return zip_stored([("[Content_Types].xml", types.encode()), ("_rels/.rels", rels.encode()), ("3D/3dmodel.model", model.encode())])


def amf_cube():
    """AMF: one orange material and one object (8 vertices, one volume of 12 triangles on that material)."""
    lines = ['<?xml version="1.0" encoding="utf-8"?>', '<amf unit="millimeter" version="1.1">',
             ' <metadata type="name">OkaySpace sample cube</metadata>',
             ' <material id="1"><metadata type="name">Orange</metadata><color><r>1</r><g>0.5</g><b>0.1</b></color></material>',
             ' <object id="0">', '  <metadata type="name">Cube</metadata>', '  <mesh>', '   <vertices>']
    for p in CORNERS:
        lines.append('    <vertex><coordinates><x>%s</x><y>%s</y><z>%s</z></coordinates></vertex>' % tuple(num(c) for c in p))
    lines += ['   </vertices>', '   <volume materialid="1">']
    for t in cube_tris():
        lines.append('    <triangle><v1>%d</v1><v2>%d</v2><v3>%d</v3></triangle>' % t)
    lines += ['   </volume>', '  </mesh>', ' </object>', '</amf>']
    return ("\n".join(lines) + "\n").encode("ascii")


def x3d_cube():
    """X3D (XML encoding): one Shape with a green Material and an IndexedFaceSet of six quads (counter-clockwise, the default)."""
    idx = " ".join(" ".join(str(i) for i in q) + " -1" for q in QUADS)
    pts = ", ".join(" ".join(num(c) for c in p) for p in CORNERS)
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<X3D profile="Interchange" version="3.3">\n'
            ' <head><meta name="title" content="OkaySpace sample cube"/></head>\n'
            ' <Scene>\n'
            '  <Transform DEF="Cube">\n'
            '   <Shape>\n'
            '    <Appearance><Material diffuseColor="0.2 0.8 0.3"/></Appearance>\n'
            '    <IndexedFaceSet solid="true" coordIndex="' + idx + '">\n'
            '     <Coordinate point="' + pts + '"/>\n'
            '    </IndexedFaceSet>\n'
            '   </Shape>\n'
            '  </Transform>\n'
            ' </Scene>\n'
            '</X3D>\n').encode("ascii")


def dxf_cube():
    """DXF (ASCII): an ENTITIES section of six 3DFACE entities (corners 10/20/30 to 13/23/33) on layer "Cube"."""
    def g(code, value):
        return "%3d\n%s\n" % (code, value)
    s = g(0, "SECTION") + g(2, "ENTITIES")
    for q in QUADS:
        s += g(0, "3DFACE") + g(8, "Cube")
        for k, c in enumerate(q):
            for axis in range(3):
                s += g(10 + k + 10 * axis, num(CORNERS[c][axis]))
    s += g(0, "ENDSEC") + g(0, "EOF")
    return s.encode("ascii")


def lwo_cube():
    """LightWave LWO2 (big-endian IFF): TAGS, LAYR, PNTS, POLS (FACE), PTAG (SURF) and a SURF with a colour. LightWave is
    left-handed with polygons clockwise from the visible side, which is the right-handed outward order of QUADS."""
    def chunk(cid, body):
        return cid + be32(len(body)) + body + (b"\0" if len(body) & 1 else b"")

    def sub(cid, body):
        return cid + be16(len(body)) + body + (b"\0" if len(body) & 1 else b"")

    def name(text):
        b = text.encode("ascii") + b"\0"
        return b + (b"\0" if len(b) & 1 else b"")
    tags = name("Cube")
    layr = be16(0) + be16(0) + bef32(0) + bef32(0) + bef32(0) + name("Cube")
    pnts = b"".join(bef32(c) for p in CORNERS for c in p)
    pols = b"FACE" + b"".join(be16(4) + b"".join(be16(i) for i in q) for q in QUADS)
    ptag = b"SURF" + b"".join(be16(i) + be16(0) for i in range(len(QUADS)))
    surf = name("Cube") + name("") + sub(b"COLR", bef32(0.9) + bef32(0.3) + bef32(0.2) + be16(0)) + sub(b"DIFF", bef32(1.0) + be16(0))
    body = b"LWO2" + chunk(b"TAGS", tags) + chunk(b"LAYR", layr) + chunk(b"PNTS", pnts) + chunk(b"POLS", pols) + chunk(b"PTAG", ptag) + chunk(b"SURF", surf)
    return b"FORM" + be32(len(body)) + body


def md2_cube():
    """Quake II MD2: a 68-byte header, 4 texture coordinates, 12 triangles and one frame of 8 packed vertices
    (byte * scale + translate). MD2 stores triangles clockwise, so the cube's triangles are written reversed."""
    skin_w = skin_h = 64
    st = [(0, 0), (skin_w, 0), (skin_w, skin_h), (0, skin_h)]
    tris = []   # (corner indices, st indices): each triangle of a quad keeps its quad corners' st
    for q in QUADS:
        for k in ((0, 2, 1), (0, 3, 2)):
            tris.append((tuple(q[j] for j in k), k))
    frame_size = 40 + 4 * len(CORNERS)
    ofs_st = 68
    ofs_tris = ofs_st + 4 * len(st)
    ofs_frames = ofs_tris + 12 * len(tris)
    ofs_end = ofs_frames + frame_size
    out = b"IDP2" + b"".join(i32(v) for v in (8, skin_w, skin_h, frame_size, 0, len(CORNERS), len(st), len(tris), 0, 1,
                                                ofs_st, ofs_st, ofs_tris, ofs_frames, ofs_end, ofs_end))
    out += b"".join(i16(a) + i16(b) for a, b in st)
    for corners, sts in tris:
        out += b"".join(u16(i) for i in corners) + b"".join(u16(i) for i in sts)
    out += b"".join(f32(v) for v in (2.0, 2.0, 2.0, -1.0, -1.0, -1.0)) + fixed("cube", 16)
    for p in CORNERS:
        out += bytes([(p[0] + 1) // 2, (p[1] + 1) // 2, (p[2] + 1) // 2, 0])
    return out


def md3_normal(n):
    """The two-byte normal of MD3 and MDC: high byte = the turn about Z, low byte = the angle from +Z, both in 1/256 turns.
    Only the six axis directions are needed here, and those are exact."""
    table = {(0, 0, 1): (0, 0), (0, 0, -1): (0, 128), (1, 0, 0): (0, 64), (-1, 0, 0): (128, 64), (0, 1, 0): (64, 64), (0, -1, 0): (192, 64)}
    hi, lo = table[n]
    return (hi << 8) | lo


def md3_cube():
    """Quake III MD3: header, one frame, no tags, one surface of 24 vertices (1/64 units, packed normals) and 12 triangles,
    stored clockwise like MD2."""
    pos, nrm, uv, quads = split_cube()
    tris = split_tris(quads, reverse=True)
    n_verts = len(pos)
    s_ofs_tris = 108
    s_ofs_shaders = s_ofs_tris + 12 * len(tris)
    s_ofs_st = s_ofs_shaders
    s_ofs_xyz = s_ofs_st + 8 * n_verts
    s_ofs_end = s_ofs_xyz + 8 * n_verts
    surf = b"IDP3" + fixed("cube", 64) + b"".join(i32(v) for v in (0, 1, 0, n_verts, len(tris), s_ofs_tris, s_ofs_shaders, s_ofs_st, s_ofs_xyz, s_ofs_end))
    surf += b"".join(i32(i) for t in tris for i in t)
    surf += b"".join(f32(c) for t in uv for c in t)
    for p, n in zip(pos, nrm):
        surf += b"".join(i16(64 * c) for c in p) + u16(md3_normal(n))
    ofs_frames = 108
    ofs_tags = ofs_frames + 56
    ofs_surf = ofs_tags
    head = b"IDP3" + i32(15) + fixed("models/okayspace/cube.md3", 64) + b"".join(i32(v) for v in (0, 1, 0, 1, 0, ofs_frames, ofs_tags, ofs_surf, ofs_surf + len(surf)))
    frame = b"".join(f32(v) for v in (-1, -1, -1, 1, 1, 1, 0, 0, 0, 1.7320508)) + fixed("frame0", 16)
    return head + frame + surf


def mdc_cube():
    """Return to Castle Wolfenstein MDC (version 2): MD3's layout with a frame table per surface. One base frame and no
    compressed frames, no tags, one surface of 24 vertices and 12 clockwise triangles."""
    pos, nrm, uv, quads = split_cube()
    tris = split_tris(quads, reverse=True)
    n_verts = len(pos)
    o_tris = 124
    o_shaders = o_tris + 12 * len(tris)
    o_st = o_shaders
    o_xyz = o_st + 8 * n_verts
    o_comp = o_xyz + 8 * n_verts
    o_base_frames = o_comp
    o_comp_frames = o_base_frames + 2
    o_end = o_comp_frames + 2
    surf = u32(7) + fixed("cube", 64) + u32(0) + b"".join(u32(v) for v in (0, 1, 0, n_verts, len(tris)))
    surf += b"".join(u32(v) for v in (o_tris, o_shaders, o_st, o_xyz, o_comp, o_base_frames, o_comp_frames, o_end))
    surf += b"".join(u32(i) for t in tris for i in t)
    surf += b"".join(f32(c) for t in uv for c in t)
    for p, n in zip(pos, nrm):
        surf += b"".join(i16(64 * c) for c in p) + u16(md3_normal(n))
    surf += i16(0) + i16(-1)   # frame 0 uses base frame 0 and no compressed frame
    ofs_frames = 112
    ofs_tag_names = ofs_frames + 56
    ofs_tags = ofs_tag_names
    ofs_surf = ofs_tags
    head = b"IDPC" + u32(2) + fixed("cube", 64) + u32(0) + b"".join(u32(v) for v in (1, 0, 1, 0, ofs_frames, ofs_tag_names, ofs_tags, ofs_surf, ofs_surf + len(surf)))
    frame = b"".join(f32(v) for v in (-1, -1, -1, 1, 1, 1, 0, 0, 0, 1.7320508)) + fixed("frame0", 16)
    return head + frame + surf


def irrmesh_cube():
    """Irrlicht mesh (XML): one buffer of 24 "standard" vertices (position, normal, colour, uv) and 36 indices, with a
    light blue material. Irrlicht is left-handed with clockwise front faces, which is the right-handed outward order."""
    pos, nrm, uv, quads = split_cube()
    lines = ['<?xml version="1.0"?>', '<mesh xmlns="http://irrlicht.sourceforge.net/IRRMESH_09_2007" version="1.0">',
             '<!-- OkaySpace sample cube -->', '<buffer>',
             '<material><enum name="Type" value="solid" /><color name="Diffuse" value="ff66b3ff" />'
             '<color name="Emissive" value="00000000" /><float name="Shininess" value="0" /></material>',
             '<vertices type="standard" vertexCount="%d">' % len(pos)]
    for p, n, t in zip(pos, nrm, uv):
        lines.append(" ".join(num(v) for v in p + n) + " ffffffff " + " ".join(num(v) for v in t))
    lines.append('</vertices>')
    idx = [i for t in split_tris(quads) for i in t]
    lines.append('<indices indexCount="%d">' % len(idx))
    lines.append(" ".join(str(i) for i in idx))
    lines += ['</indices>', '</buffer>', '</mesh>']
    return ("\n".join(lines) + "\n").encode("ascii")


def irr_scene():
    """Irrlicht scene (XML): a built-in "cube" node on the left and a "mesh" node on the right that loads cube.irrmesh
    from the same folder."""
    lines = ['<?xml version="1.0"?>', '<irr_scene>',
             ' <attributes><string name="Name" value="root" /><int name="Id" value="-1" /></attributes>',
             ' <node type="cube">',
             '  <attributes><string name="Name" value="BuiltInCube" /><vector3d name="Position" value="-2.0, 0.0, 0.0" />'
             '<vector3d name="Rotation" value="0.0, 45.0, 0.0" /><vector3d name="Scale" value="1.0, 1.0, 1.0" /><float name="Size" value="2.0" /></attributes>',
             '  <materials><attributes><enum name="Type" value="solid" /><color name="Diffuse" value="ffff8020" /></attributes></materials>',
             ' </node>',
             ' <node type="mesh">',
             '  <attributes><string name="Name" value="MeshCube" /><vector3d name="Position" value="2.0, 0.0, 0.0" />'
             '<vector3d name="Rotation" value="0.0, 0.0, 0.0" /><vector3d name="Scale" value="1.0, 1.0, 1.0" />'
             '<string name="Mesh" value="cube.irrmesh" /></attributes>',
             ' </node>',
             '</irr_scene>']
    return ("\n".join(lines) + "\n").encode("ascii")


def b3d_rig():
    """Blitz3D B3D: chunks (4-byte tag, 4-byte length). BB3D version 1 holds one brush and a node tree: "Rig" carries the
    column mesh (positions and normals) and the ANIM chunk; its child "base" and grandchild "top" carry the BONE weights,
    and "top" has rotation KEYS at frames 0, 15 and 30 (30 frames per second). Blitz3D is left-handed with clockwise front
    faces: the right-handed outward order. Rotations are written w, x, y, z."""
    def chunk(tag, body):
        return tag + i32(len(body)) + body

    def z(text):
        return text.encode("ascii") + b"\0"

    def v3(a, b, c):
        return f32(a) + f32(b) + f32(c)

    def quat_wxyz(q):
        return f32(q[3]) + f32(q[0]) + f32(q[1]) + f32(q[2])
    brus = chunk(b"BRUS", i32(0) + z("Column") + v3(0.9, 0.7, 0.3) + f32(1.0) + f32(0.3) + i32(1) + i32(0))
    vrts = i32(1) + i32(0) + i32(2)   # flags: normals; no texture coordinate sets
    for p, n in zip(COL_POS, COL_NRM):
        vrts += v3(*p) + v3(*n)
    tris = i32(-1) + b"".join(i32(i) for t in column_tris() for i in t)
    mesh = chunk(b"MESH", i32(0) + chunk(b"VRTS", vrts) + chunk(b"TRIS", tris))

    def bone(joint):
        return chunk(b"BONE", b"".join(i32(v) + f32(w) for v, ws in enumerate(COL_WEIGHTS) for j, w in ws if j == joint))
    keys = i32(1 | 4)
    for t, q in BEND_KEYS:
        keys += i32(int(t * 30)) + v3(0.0, 1.0, 0.0) + quat_wxyz(q)
    ident = (0.0, 0.0, 0.0, 1.0)
    top = chunk(b"NODE", z("top") + v3(0.0, 1.0, 0.0) + v3(1, 1, 1) + quat_wxyz(ident) + bone(1) + chunk(b"KEYS", keys))
    base = chunk(b"NODE", z("base") + v3(0, 0, 0) + v3(1, 1, 1) + quat_wxyz(ident) + bone(0) + top)
    anim = chunk(b"ANIM", i32(0) + i32(30) + f32(30.0))
    rig = chunk(b"NODE", z("Rig") + v3(0, 0, 0) + v3(1, 1, 1) + quat_wxyz(ident) + mesh + base + anim)
    return chunk(b"BB3D", i32(1) + brus + rig)


def x_rig():
    """DirectX .x (text): a frame tree Rig > Base > Top, the column mesh with one material and SkinWeights for both bones
    (the offset matrix takes the mesh into the bone's space), and an AnimationSet "Bend" with rotation keys (w, x, y, z)
    on Top at ticks 0, 15 and 30 (AnimTicksPerSecond 30). DirectX is left-handed with clockwise front faces: the
    right-handed outward order. Matrices are written for row vectors (the translation is the last row)."""
    def mat(tx, ty, tz):
        return "1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,%s,%s,%s,1.0;;" % (num(tx), num(ty), num(tz))
    tris = column_tris()
    L = ["xof 0303txt 0032", "", "AnimTicksPerSecond {", " 30;", "}", "",
         "Material Column {", " 0.9;0.7;0.3;1.0;;", " 10.0;", " 0.0;0.0;0.0;;", " 0.0;0.0;0.0;;", "}", "",
         "Frame Rig {", " FrameTransformMatrix {", "  " + mat(0, 0, 0), " }",
         " Frame Base {", "  FrameTransformMatrix {", "   " + mat(0, 0, 0), "  }",
         "  Frame Top {", "   FrameTransformMatrix {", "    " + mat(0, 1, 0), "   }", "  }", " }",
         " Mesh Column {", "  %d;" % len(COL_POS)]
    for i, p in enumerate(COL_POS):
        L.append("  %s;%s;%s;%s" % (num(p[0]), num(p[1]), num(p[2]), ";" if i == len(COL_POS) - 1 else ","))
    L.append("  %d;" % len(tris))
    for i, t in enumerate(tris):
        L.append("  3;%d,%d,%d;%s" % (t[0], t[1], t[2], ";" if i == len(tris) - 1 else ","))
    L += ["  MeshNormals {", "   %d;" % len(COL_NRM)]
    for i, n in enumerate(COL_NRM):
        L.append("   %s;%s;%s;%s" % (num(n[0]), num(n[1]), num(n[2]), ";" if i == len(COL_NRM) - 1 else ","))
    L.append("   %d;" % len(tris))
    for i, t in enumerate(tris):
        L.append("   3;%d,%d,%d;%s" % (t[0], t[1], t[2], ";" if i == len(tris) - 1 else ","))
    L += ["  }", "  MeshMaterialList {", "   1;", "   %d;" % len(tris), "   " + ",".join("0" for _ in tris) + ";;", "   { Column }", "  }"]
    for joint, name, offset in ((0, "Base", (0, 0, 0)), (1, "Top", (0, -1, 0))):
        pairs = [(v, w) for v, ws in enumerate(COL_WEIGHTS) for j, w in ws if j == joint]
        L += ["  SkinWeights {", '   "%s";' % name, "   %d;" % len(pairs), "   " + ",".join(str(v) for v, _ in pairs) + ";",
              "   " + ",".join(num(w) for _, w in pairs) + ";", "   " + mat(*offset), "  }"]
    L += [" }", "}", "", "AnimationSet Bend {", " Animation {", "  { Top }", "  AnimationKey {", "   0;", "   %d;" % len(BEND_KEYS)]
    for i, (t, q) in enumerate(BEND_KEYS):
        L.append("   %d;4;%s,%s,%s,%s;;%s" % (int(t * 30), num(q[3]), num(q[0]), num(q[1]), num(q[2]), ";" if i == len(BEND_KEYS) - 1 else ","))
    L += ["  }", " }", "}"]
    return ("\n".join(L) + "\n").encode("ascii")


def ac3d_cube():
    """AC3D (AC3Db text): one material, a world object holding one poly object of 8 vertices and 6 flat quads (surface
    flags 0x00) with texture coordinates in the refs. AC3D is right-handed, counter-clockwise from outside."""
    L = ["AC3Db", 'MATERIAL "Purple" rgb 0.6 0.3 0.9  amb 0.2 0.2 0.2  emis 0 0 0  spec 0.5 0.5 0.5  shi 20  trans 0',
         "OBJECT world", 'name "Sample"', "kids 1", "OBJECT poly", 'name "Cube"', "numvert %d" % len(CORNERS)]
    L += [" ".join(num(c) for c in p) for p in CORNERS]
    L.append("numsurf %d" % len(QUADS))
    for q in QUADS:
        L += ["SURF 0x00", "mat 0", "refs 4"]
        L += ["%d %s %s" % (c, num(QUAD_UV[k][0]), num(QUAD_UV[k][1])) for k, c in enumerate(q)]
    L.append("kids 0")
    return ("\n".join(L) + "\n").encode("ascii")


def ogre_cube():
    """OGRE XML mesh: one submesh with its own geometry (24 vertices: position, normal, one 2D texture coordinate) and
    12 faces, material name "Cube" (no .material file: the reader keeps the name)."""
    pos, nrm, uv, quads = split_cube()
    L = ['<mesh>', ' <submeshes>',
         '  <submesh material="Cube" usesharedvertices="false" use32bitindexes="false" operationtype="triangle_list">']
    tris = split_tris(quads)
    L.append('   <faces count="%d">' % len(tris))
    L += ['    <face v1="%d" v2="%d" v3="%d"/>' % t for t in tris]
    L += ['   </faces>', '   <geometry vertexcount="%d">' % len(pos),
          '    <vertexbuffer positions="true" normals="true" texture_coords="1" texture_coord_dimensions_0="float2">']
    for p, n, t in zip(pos, nrm, uv):
        L.append('     <vertex><position x="%s" y="%s" z="%s"/><normal x="%s" y="%s" z="%s"/><texcoord u="%s" v="%s"/></vertex>'
                 % tuple(num(v) for v in p + n + t))
    L += ['    </vertexbuffer>', '   </geometry>', '  </submesh>', ' </submeshes>',
          ' <submeshnames><submeshname name="Cube" index="0"/></submeshnames>', '</mesh>']
    return ("\n".join(L) + "\n").encode("ascii")


def xgl_cube():
    """XGL (XML): a world material, one MESH with 8 points, 6 normals and 12 faces (each corner names its point and
    normal), placed by one OBJECT."""
    L = ["<WORLD>", "<NAME>OkaySpace sample cube</NAME>", "<BACKGROUND><BACKCOLOR>0.2,0.2,0.2</BACKCOLOR></BACKGROUND>",
         "<LIGHTING><AMBIENT>0.3,0.3,0.3</AMBIENT></LIGHTING>",
         '<MAT ID="0"><AMB>0.2,0.5,0.5</AMB><DIFF>0.2,0.8,0.8</DIFF><SPEC>0.5,0.5,0.5</SPEC><EMISS>0,0,0</EMISS><SHINE>32</SHINE><ALPHA>1</ALPHA></MAT>',
         '<MESH ID="0">']
    L += ['<P ID="%d">%s</P>' % (i, ",".join(num(c) for c in p)) for i, p in enumerate(CORNERS)]
    L += ['<N ID="%d">%s</N>' % (i, ",".join(num(c) for c in n)) for i, n in enumerate(NORMALS)]
    for f, q in enumerate(QUADS):
        for t in quad_tris(q):
            fv = "".join("<FV%d><PREF>%d</PREF><NREF>%d</NREF></FV%d>" % (k + 1, c, f, k + 1) for k, c in enumerate(t))
            L.append("<F><MATREF>0</MATREF>%s</F>" % fv)
    L += ["</MESH>", "<OBJECT><NAME>Cube</NAME><TRANSFORM><FORWARD>0,0,1</FORWARD><UP>0,1,0</UP><POSITION>0,0,0</POSITION></TRANSFORM><MESHREF>0</MESHREF></OBJECT>",
          "</WORLD>"]
    return ("\n".join(L) + "\n").encode("ascii")


def nff_cube():
    """NFF (Eric Haines' Neutral File Format): a fill colour, then six "p 4" polygons with their corners."""
    L = ["# OkaySpace sample cube", "f 0.9 0.8 0.1 1 0 30 0 1"]
    for q in QUADS:
        L.append("p 4")
        L += [" ".join(num(c) for c in CORNERS[i]) for i in q]
    return ("\n".join(L) + "\n").encode("ascii")


def raw_cube():
    """RAW: a group name line, then one triangle per line (nine numbers)."""
    L = ["Cube"]
    for t in cube_tris():
        L.append("  ".join(" ".join(num(c) for c in CORNERS[i]) for i in t))
    return ("\n".join(L) + "\n").encode("ascii")


def q3o_cube():
    """Quick3D object (little-endian): "quick3Do" "30", the mesh, material and texture counts, an "m" mesh chunk (8
    vertices, 6 four-sided faces, a material per face, no normals, no texture coordinates, the centre and bounding box)
    and a "c" material chunk (name, ambient, diffuse, specular, transparency)."""
    out = b"quick3Do" + b"30" + i32(1) + i32(1) + i32(0)
    out += b"m" + i32(len(CORNERS)) + b"".join(f32(c) for p in CORNERS for c in p)
    out += i32(len(QUADS)) + b"".join(u16(4) for _ in QUADS) + b"".join(i32(i) for q in QUADS for i in q)
    out += b"".join(i32(0) for _ in QUADS)   # material of each face
    out += i32(0)                            # no normals
    out += b"".join(f32(v) for v in (0, 0, 0, -1, -1, -1, 1, 1, 1))   # centre of mass, bounding box
    out += b"c" + b"Red\0" + b"".join(f32(v) for v in (0.2, 0.05, 0.05, 0.9, 0.2, 0.2, 1, 1, 1)) + f32(0.0)
    return out


def cob_cube():
    """trueSpace COB (ASCII): the 32-byte header, a PolH chunk (name, axes, an identity transform, 8 world vertices, 4
    texture vertices, 6 faces of <vertex,uv> pairs), a Mat1 material and the END chunk. trueSpace is left-handed and Z-up:
    our reader swaps y and z and keeps the winding, so the faces are written in the other order."""
    s = "Caligari V00.01ALH             \n"
    s += "PolH V0.08 Id 1 Parent 0 Size 00000999\nName Cube\ncenter 0 0 0\nx axis 1 0 0\ny axis 0 1 0\nz axis 0 0 1\n"
    s += "Transform\n1 0 0 0\n0 1 0 0\n0 0 1 0\n0 0 0 1\n"
    s += "World Vertices %d\n" % len(CORNERS) + "".join(" ".join(num(c) for c in p) + "\n" for p in CORNERS)
    s += "Texture Vertices 4\n" + "".join("%s %s\n" % (num(u), num(v)) for u, v in QUAD_UV)
    s += "Faces %d\n" % len(QUADS)
    for q in QUADS:
        corners = flip(tuple(zip(q, range(4))))
        s += "Face verts 4 flags 0 mat 0\n" + " ".join("<%d,%d>" % c for c in corners) + " \n"
    s += "DrawFlags 0\n"
    s += "Mat1 V0.08 Id 2 Parent 1 Size 00000100\nmat# 0\nshader: phong  facet: faceted\nrgb 0.3,0.9,0.4\nalpha 1  ka 0.1  ks 0.1  exp 0  ior 1\n"
    s += "END V1.00 Id 0 Parent 0 Size 0\n"
    return s.encode("ascii")


def csm_walk():
    """Character Studio motion capture (CSM text): three markers (head and both hands, in metres, Z up) over 21 frames at
    30 frames per second: the head bobs and the hands swing."""
    swing = [0, 3, 6, 8, 10, 10, 10, 8, 6, 3, 0, -3, -6, -8, -10, -10, -10, -8, -6, -3, 0]
    L = ["$Filename walk.csm", "$Date 2026/11/15", "$Time 12:00:00", "$Actor OkaySpace sample", "", "$Comments",
         "Made by scripts/gen_own_reader_samples.py", "", "$FirstFrame 1", "$LastFrame %d" % len(swing), "$Rate 30", "",
         "$Order", "Head LeftHand RightHand", "", "$Points"]
    for f, s in enumerate(swing):
        bob = abs(s) // 5
        head = (0, 0, 170 + bob)
        left = (-30, 2 * s, 100)
        right = (30, -2 * s, 100)
        L.append("%d  %s" % (f + 1, "  ".join(" ".join(num(c / 100.0) for c in m) for m in (head, left, right))))
    return ("\n".join(L) + "\n").encode("ascii")


def ter_hill():
    """Terragen terrain: "TERRAGENTERRAIN " then 4-byte aligned chunks: SIZE, XPTS, YPTS, SCAL (1 metre per unit), ALTW
    (height scale, base height, the 17 x 17 elevations row by row) and "EOF ". Height = base + elevation * scale / 65536."""
    n = 17
    grid = hill(n, 1024, 16)
    out = b"TERRAGENTERRAIN "
    out += b"SIZE" + u16(n - 1) + u16(0) + b"XPTS" + u16(n) + u16(0) + b"YPTS" + u16(n) + u16(0)
    out += b"SCAL" + f32(1.0) + f32(1.0) + f32(1.0)
    altw = b"ALTW" + i16(256) + i16(0) + b"".join(i16(h) for row in grid for h in row)
    altw += b"\0" * (-len(altw) % 4)
    return out + altw + b"EOF "


def hmp_hill():
    """3D GameStudio HMP7 terrain: the header (scale, origin, grid step, points per row, counts), one frame header and
    9 x 9 grid points (a 2-byte height, then 2 bytes the reader skips). z = origin + height * scale."""
    n = 9
    grid = hill(n, 300, 15)
    out = b"HMP7" + u32(0)
    out += b"".join(f32(v) for v in (1.0, 1.0, 0.01))    # scale (z is the one used for heights)
    out += b"".join(f32(v) for v in (-4.0, -4.0, 0.0))   # origin
    out += f32(0.0)                                        # bounding radius
    out += f32(1.0) + f32(1.0) + f32(float(n))             # grid step x, y; points per row
    out += u32(0) + u32(0) + u32(0) + u32(n * n) + u32(0) + u32(1) + u32(0) + u32(0) + u32(0)   # no skins; points; one frame
    out += u32(2) + b"\0" * 32                             # the frame header
    for row in grid:
        for h in row:
            out += u16(h) + u8(0) + u8(64)
    return out


def iqm_rig():
    """Inter-Quake Model, version 2: header, text, one mesh, five vertex arrays (position, texture coordinate, normal,
    blend indexes, blend weights), 20 triangles, two joints, two poses and one animation "bend" of 3 frames at 2 frames
    per second. IQM is Z up (the reader's root turns it to Y up): the column stands along +Z and "top" bends about X."""
    pos = [(x, -z, y) for (x, y, z) in COL_POS]              # Y up -> Z up (a turn: the winding stays outward)
    nrm = [(x, -z, y) for (x, y, z) in COL_NRM]
    tris = column_tris()
    text = b"\0"

    def add(t):
        nonlocal text
        o = len(text)
        text += t.encode("ascii") + b"\0"
        return o
    o_mesh, o_bend, o_base, o_top = add("column"), add("bend"), add("base"), add("top")
    while len(text) % 4:
        text += b"\0"
    blend_i, blend_w = b"", b""
    for ws in COL_WEIGHTS:
        idx = [j for j, _ in ws] + [0] * (4 - len(ws))
        wt = [255] if len(ws) == 1 else [128, 127]
        blend_i += bytes(idx)
        blend_w += bytes(wt + [0] * (4 - len(wt)))
    arrays = [(0, 7, 3, b"".join(f32(c) for p in pos for c in p)),
              (1, 7, 2, b"".join(f32(c) for t in COL_UV for c in t)),
              (2, 7, 3, b"".join(f32(c) for n in nrm for c in n)),
              (4, 1, 4, blend_i),
              (5, 1, 4, blend_w)]
    one = (0.0, 0.0, 0.0, 1.0)
    joints = (u32(o_base) + i32(-1) + b"".join(f32(v) for v in (0, 0, 0) + one + (1, 1, 1)) +
              u32(o_top) + i32(0) + b"".join(f32(v) for v in (0, 0, 1) + one + (1, 1, 1)))
    # Poses: channel values are offset + raw * scale; channels 0-2 translate, 3-6 rotate (x y z w), 7-9 scale.
    # "top" animates rotate x (bit 3) and rotate w (bit 6): raw 0, 1000, 0 gives the quaternions of 0, 45 and 0 degrees.
    poses = i32(-1) + u32(0) + b"".join(f32(v) for v in (0, 0, 0, 0, 0, 0, 1, 1, 1, 1)) + b"".join(f32(0.0) for _ in range(10))
    poses += i32(0) + u32((1 << 3) | (1 << 6)) + b"".join(f32(v) for v in (0, 0, 1, 0, 0, 0, 1, 1, 1, 1))
    poses += b"".join(f32(v) for v in (0, 0, 0, 0.00038268343, 0, 0, -0.00007612047, 0, 0, 0))
    frames = b"".join(u16(v) for v in (0, 0, 1000, 1000, 0, 0))
    anims = u32(o_bend) + u32(0) + u32(3) + f32(2.0) + u32(1)
    meshes = u32(o_mesh) + u32(o_mesh) + u32(0) + u32(len(pos)) + u32(0) + u32(len(tris))
    tri_data = b"".join(u32(i) for t in tris for i in t)
    # layout: header (124 bytes), text, meshes, vertex array records, triangles, joints, poses, anims, frames, vertex data
    ofs = 124
    ofs_text = ofs; ofs += len(text)
    ofs_meshes = ofs; ofs += len(meshes)
    ofs_va = ofs; ofs += 20 * len(arrays)
    ofs_tris = ofs; ofs += len(tri_data)
    ofs_joints = ofs; ofs += len(joints)
    ofs_poses = ofs; ofs += len(poses)
    ofs_anims = ofs; ofs += len(anims)
    ofs_frames = ofs; ofs += len(frames)
    ofs += -ofs % 4
    va, data = b"", b""
    for kind, fmt, size, block in arrays:
        va += u32(kind) + u32(0) + u32(fmt) + u32(size) + u32(ofs + len(data))
        data += block
    total = ofs + len(data)
    header = b"INTERQUAKEMODEL\0" + b"".join(u32(v) for v in (
        2, total, 0, len(text), ofs_text, 1, ofs_meshes, len(arrays), len(pos), ofs_va, len(tris), ofs_tris, 0,
        2, ofs_joints, 2, ofs_poses, 1, ofs_anims, 3, 2, ofs_frames, 0, 0, 0, 0, 0))
    body = header + text + meshes + va + tri_data + joints + poses + anims + frames
    body += b"\0" * (-len(body) % 4)
    assert len(body) == ofs
    return body + data


def pmx_rig():
    """PMX 2.0 (MikuMikuDance), UTF-16LE text: the header (8 globals: text encoding, extra uv 0, index sizes 2/1/1/2/1/1),
    12 vertices (BDEF1 on one bone or BDEF2 half and half), 60 face indices, no textures, one material, two bones
    ("base" and "top", tail given as an offset), and empty morph, display frame, rigid body and joint lists. PMX is
    left-handed with clockwise front faces: the right-handed outward order."""
    def text(t):
        b = t.encode("utf-16-le")
        return i32(len(b)) + b

    def v3(a, b, c):
        return f32(a) + f32(b) + f32(c)
    out = b"PMX " + f32(2.0) + u8(8) + bytes([0, 0, 2, 1, 1, 2, 1, 1])
    out += text("Column") + text("Column") + text("OkaySpace sample rig") + text("OkaySpace sample rig")
    out += i32(len(COL_POS))
    for p, n, t, ws in zip(COL_POS, COL_NRM, COL_UV, COL_WEIGHTS):
        out += v3(*p) + v3(*n) + f32(t[0]) + f32(t[1])
        if len(ws) == 1:
            out += u8(0) + i16(ws[0][0])
        else:
            out += u8(1) + i16(ws[0][0]) + i16(ws[1][0]) + f32(ws[0][1])
        out += f32(1.0)   # edge scale
    idx = [i for t in column_tris() for i in t]
    out += i32(len(idx)) + b"".join(u16(i) for i in idx)
    out += i32(0)   # textures
    out += i32(1)
    out += text("Column") + text("Column") + f32(0.9) + f32(0.7) + f32(0.3) + f32(1.0) + v3(0.1, 0.1, 0.1) + f32(10.0) + v3(0.4, 0.3, 0.1)
    out += u8(0) + f32(0) + f32(0) + f32(0) + f32(1) + f32(1.0)   # flags, edge colour, edge size
    out += bytes([0xFF]) + bytes([0xFF]) + u8(0) + u8(1) + u8(0)   # no texture, no sphere map, sphere mode, shared toon 0
    out += text("") + i32(len(idx))
    out += i32(2)
    for name, pos, parent in (("base", (0, 0, 0), -1), ("top", (0, 1, 0), 0)):
        out += text(name) + text(name) + v3(*pos) + i16(parent) + i32(0) + u16(0x001E) + v3(0, 1, 0)
    out += i32(0) + i32(0) + i32(0) + i32(0)   # morphs, display frames, rigid bodies, joints
    return out


def lws_scene():
    """LightWave scene (LWSC version 3): one object item that loads cube.lwo from the same folder and turns once about
    its vertical axis over 2 seconds (heading keys every half second; linear envelopes, shape 3)."""
    def env(keys):
        s = "{ Envelope\n  %d\n" % len(keys)
        for t, v in keys:
            s += "  Key %s %s 3 0 0 0 0 0 0\n" % (num(v), num(t))
        return s + "  Behaviors 1 1\n}\n"
    heading = [(0.0, 0.0), (0.5, 1.5707963), (1.0, 3.1415927), (1.5, 4.712389), (2.0, 6.2831853)]
    channels = [[(0.0, 0.0)], [(0.0, 0.0)], [(0.0, 0.0)], heading, [(0.0, 0.0)], [(0.0, 0.0)], [(0.0, 1.0)], [(0.0, 1.0)], [(0.0, 1.0)]]
    s = "LWSC\n3\n\nFirstFrame 0\nLastFrame 48\nFrameStep 1\nFramesPerSecond 24\n\n"
    s += "LoadObjectLayer 1 10000000 cube.lwo\nShowObject 7 -1\nObjectMotion\nNumChannels 9\n"
    for c, keys in enumerate(channels):
        s += "Channel %d\n" % c + env(keys)
    return s.encode("ascii")


GENERATED = {
    "cube.3mf": (threemf_cube, "3MF"),
    "cube.amf": (amf_cube, "AMF"),
    "cube.x3d": (x3d_cube, "X3D"),
    "cube.dxf": (dxf_cube, "DXF"),
    "cube.lwo": (lwo_cube, "LWO"),
    "cube.md2": (md2_cube, "MD2"),
    "cube.md3": (md3_cube, "MD3"),
    "cube.mdc": (mdc_cube, "MDC"),
    "cube.irrmesh": (irrmesh_cube, "IRRMESH"),
    "cube.irr": (irr_scene, "IRR"),
    "rig.b3d": (b3d_rig, "B3D"),
    "rig.x": (x_rig, "X"),
    "cube.ac": (ac3d_cube, "AC3D"),
    "cube.mesh.xml": (ogre_cube, "OGRE"),
    "cube.xgl": (xgl_cube, "XGL"),
    "cube.nff": (nff_cube, "NFF"),
    "cube.raw": (raw_cube, "RAW"),
    "cube.q3o": (q3o_cube, "Q3D"),
    "cube.cob": (cob_cube, "COB"),
    "walk.csm": (csm_walk, "CSM"),
    "hill.ter": (ter_hill, "TER"),
    "hill.hmp": (hmp_hill, "HMP"),
    "rig.iqm": (iqm_rig, "IQM"),
    "rig.pmx": (pmx_rig, "PMX"),
    "cube.lws": (lws_scene, "LWS"),
}


def all_files():
    files = {}
    for name, (src, _) in COPIES.items():
        with open(os.path.join(ROOT, src), "rb") as fh:
            files[name] = fh.read()
    for name, (fn, _) in GENERATED.items():
        files[name] = fn()
    return files


def main(argv):
    check = "--check" in argv
    files = all_files()
    if check:
        bad = 0
        for name, data in sorted(files.items()):
            path = os.path.join(OUT, name)
            if not os.path.isfile(path):
                print("missing", name); bad += 1
            elif open(path, "rb").read() != data:
                print("differs", name); bad += 1
        print("%d of %d sample files differ or are missing" % (bad, len(files)))
        return 1 if bad else 0
    os.makedirs(OUT, exist_ok=True)
    for name, data in sorted(files.items()):
        with open(os.path.join(OUT, name), "wb") as fh:
            fh.write(data)
        print("wrote %-16s %6d bytes" % (name, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
