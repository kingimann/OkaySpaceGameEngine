#!/usr/bin/env python3
"""Writes the 3D Studio (.3ds) fixtures for the own 3DS reader into tests/data/models/.

Every file is a small chunk tree (u16 id, u32 length including the 6 byte header, little endian)
written from the public description of the format. Deterministic: running it twice gives the
same bytes. Run from the repository root:  python3 scripts/gen_3ds_fixtures.py
The expected values are checked in tests/test_3ds_reader.cpp.
"""
import math
import os
import struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "data", "models")


def chunk(cid, payload=b""):
    return struct.pack("<HI", cid, 6 + len(payload)) + payload


def cstr(s):
    return s.encode("ascii") + b"\0"


def rgb_bytes(r, g, b):
    return chunk(0x0011, bytes([r, g, b]))


def rgb_float(r, g, b):
    return chunk(0x0010, struct.pack("<3f", r, g, b))


def pct(cid, v):
    return chunk(cid, chunk(0x0030, struct.pack("<H", v)))


def material(name, diffuse=None, specular=None, shin=None, transp=None, selfillum=None,
             tex=None, bump=None, specmap=None, opac=None, tex_uscale=None):
    p = chunk(0xA000, cstr(name))
    p += chunk(0xA010, rgb_bytes(20, 20, 20))
    if diffuse is not None:
        p += chunk(0xA020, diffuse)
    if specular is not None:
        p += chunk(0xA030, specular)
    if shin is not None:
        p += pct(0xA040, shin)
    if transp is not None:
        p += pct(0xA050, transp)
    if selfillum is not None:
        p += pct(0xA084, selfillum)
    if tex is not None:
        sub = chunk(0xA300, cstr(tex))
        if tex_uscale is not None:
            sub += chunk(0xA354, struct.pack("<f", tex_uscale))
        p += chunk(0xA200, sub)
    if specmap is not None:
        p += chunk(0xA204, chunk(0xA300, cstr(specmap)))
    if opac is not None:
        p += chunk(0xA210, chunk(0xA300, cstr(opac)))
    if bump is not None:
        p += chunk(0xA230, chunk(0xA300, cstr(bump)))
    return chunk(0xAFFF, p)


def tri_object(name, pts, faces, uvs=None, sg=None, groups=None, matrix=None):
    body = chunk(0x4110, struct.pack("<H", len(pts)) + b"".join(struct.pack("<3f", *p) for p in pts))
    fd = struct.pack("<H", len(faces)) + b"".join(struct.pack("<4H", a, b, c, 0x0007) for a, b, c in faces)
    sub = b""
    for mname, idx in (groups or []):
        sub += chunk(0x4130, cstr(mname) + struct.pack("<H", len(idx)) + b"".join(struct.pack("<H", i) for i in idx))
    if sg is not None:
        sub += chunk(0x4150, b"".join(struct.pack("<I", s) for s in sg))
    body += chunk(0x4120, fd + sub)
    if uvs is not None:
        body += chunk(0x4140, struct.pack("<H", len(uvs)) + b"".join(struct.pack("<2f", *t) for t in uvs))
    if matrix is not None:
        body += chunk(0x4160, struct.pack("<12f", *matrix))
    return chunk(0x4000, cstr(name) + chunk(0x4100, body))


def track(cid, keys, rot=False):
    """keys: list of (frame, value, flags). value = 3 floats, or (angle, ax, ay, az) for rotation."""
    p = struct.pack("<HII", 0, 0, 0) + struct.pack("<I", len(keys))
    for frame, val, flags in keys:
        p += struct.pack("<IH", frame, flags)
        for bit in range(5):
            if flags & (1 << bit):
                p += struct.pack("<f", 0.5)          # tension / continuity / bias / ease: skipped by the reader
        p += struct.pack("<4f" if rot else "<3f", *val)
    return chunk(cid, p)


def kf_node(name, nid, parent, pivot=(0, 0, 0), pos=None, rot=None, scl=None, instance=None):
    p = chunk(0xB010, cstr(name) + struct.pack("<HHH", 0, 0, parent))
    if instance:
        p += chunk(0xB011, cstr(instance))
    p += chunk(0xB013, struct.pack("<3f", *pivot))
    if pos:
        p += track(0xB020, pos)
    if rot:
        p += track(0xB021, rot, rot=True)
    if scl:
        p += track(0xB022, scl)
    p += chunk(0xB030, struct.pack("<H", nid))
    return chunk(0xB002, p)


def main_chunk(edit, kf=None, version=3):
    body = chunk(0x0002, struct.pack("<I", version)) + chunk(0x3D3D, edit)
    if kf is not None:
        body += chunk(0xB000, kf)
    return chunk(0x4D4D, body)


def write(name, data):
    path = os.path.normpath(os.path.join(OUT, name))
    with open(path, "wb") as f:
        f.write(data)
    print("wrote", path, len(data), "bytes")


# ---- a cube: 8 points, 12 triangles, outward normals --------------------------------------------
CUBE_PTS = [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
CUBE_FACES = [  # counter-clockwise seen from outside
    (0, 3, 2), (0, 2, 1),      # -z
    (4, 5, 6), (4, 6, 7),      # +z
    (0, 1, 5), (0, 5, 4),      # -y
    (3, 7, 6), (3, 6, 2),      # +y
    (0, 4, 7), (0, 7, 3),      # -x
    (1, 2, 6), (1, 6, 5),      # +x
]
CUBE_UV = [(0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1)]


def cube_3ds():
    mat = material("Red", diffuse=rgb_bytes(255, 0, 0), specular=rgb_float(1.0, 1.0, 1.0), shin=50,
                   selfillum=20, tex="RED.TGA")
    obj = tri_object("Cube", CUBE_PTS, CUBE_FACES, uvs=CUBE_UV, sg=[1] * 12,
                     groups=[("Red", list(range(12)))],
                     matrix=(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0))
    write("cube.3ds", main_chunk(mat + obj))


def box_3ds():
    """Flat shaded (smoothing group 0), two materials, master scale 0.5, a light and a camera,
    maps the reader does not use (opacity), a texture with tiling, material transparency."""
    wood = material("Wood", diffuse=rgb_float(0.5, 0.25, 0.0), shin=10, transp=40, tex="wood.png", tex_uscale=2.0,
                    opac="OPAC.TGA")
    metal = material("Metal", diffuse=rgb_bytes(128, 128, 128), bump="BUMP.PNG", specmap="SPEC.PNG")
    obj = tri_object("Box", CUBE_PTS, CUBE_FACES, uvs=CUBE_UV, sg=[0] * 12,
                     groups=[("Wood", list(range(0, 6))), ("Metal", list(range(6, 12)))])
    light = chunk(0x4000, cstr("Lamp") + chunk(0x4600, struct.pack("<3f", 0, 0, 5)))
    cam = chunk(0x4000, cstr("Cam") + chunk(0x4700, struct.pack("<3f", 0, -10, 2) + struct.pack("<3f", 0, 0, 0)))
    edit = chunk(0x0100, struct.pack("<f", 0.5)) + wood + metal + obj + light + cam
    write("box_flat.3ds", main_chunk(edit))


def hierarchy_3ds():
    """Two meshes in a keyframe hierarchy plus a dummy. Vertices are stored in scene space (as the
    format does); the mesh matrix (translation here) says where the object's own space sits, the
    nodes' tracks place it, and Arm has a pivot of 0.5 along x."""
    base_pts = [(10, 0, 0), (12, 0, 0), (12, 2, 0), (10, 2, 0)]
    base = tri_object("Base", base_pts, [(0, 1, 2), (0, 2, 3)], sg=[1, 1], matrix=(1, 0, 0, 0, 1, 0, 0, 0, 1, 10, 0, 0))
    arm_pts = [(10, 0, 3), (10, 0, 4), (11, 0, 3)]
    arm = tri_object("Arm", arm_pts, [(0, 1, 2)], sg=[0], matrix=(1, 0, 0, 0, 1, 0, 0, 0, 1, 10, 0, 3))
    hdr = chunk(0xB00A, struct.pack("<H", 5) + cstr("hierarchy") + struct.pack("<I", 30))
    seg = chunk(0x7001, struct.pack("<II", 0, 30))
    n_base = kf_node("Base", 0, 0xFFFF, pivot=(0, 0, 0),
                     pos=[(0, (10, 0, 0), 0), (30, (10, 0, 5), 0x03)],
                     rot=[(0, (0.0, 0, 0, 1), 0), (30, (math.pi / 2, 0, 0, 1), 0)],
                     scl=[(0, (1, 1, 1), 0)])
    n_arm = kf_node("Arm", 1, 0, pivot=(0.5, 0, 0), pos=[(0, (0, 0, 3), 0), (30, (0, 0, 3), 0)])
    n_dummy = kf_node("$$$DUMMY", 2, 1, pos=[(0, (1, 0, 0), 0)])
    n_inst = kf_node("Arm", 3, 0, instance="copy", pos=[(0, (-4, 0, 3), 0)])   # an instance of the mesh "Arm"
    write("hierarchy.3ds", main_chunk(base + arm, kf=hdr + seg + n_base + n_arm + n_dummy + n_inst))


if __name__ == "__main__":
    cube_3ds()
    box_3ds()
    hierarchy_3ds()
