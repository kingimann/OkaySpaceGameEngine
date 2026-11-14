#!/usr/bin/env python3
"""Writes the MS3D fixtures for the own MS3D reader into tests/data/models/ (synthetic: written by hand, not by MilkShape).
Deterministic. Run from the repository root:  python3 scripts/gen_ms3d_fixtures.py
Expected values are checked in tests/test_ms3d_reader.cpp."""
import math
import os
import struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "data", "models")
HALF_PI = math.pi / 2.0

def name(s, n):
    b = s.encode("ascii")
    return b + b"\0" * (n - len(b))

def vertex(x, y, z, bone):
    return struct.pack("<BfffbB", 0, x, y, z, bone, 0)

def triangle(idx, group=0):
    # normals all (0, 0, 1); texture coordinates are (x, y) of the vertex so shared vertices weld
    uv = {0: (0, 0), 1: (1, 0), 2: (0, 1), 3: (1, 1), 4: (0, 2), 5: (1, 2)}
    s = [uv.get(i, (0, 0))[0] for i in idx]
    t = [uv.get(i, (0, 0))[1] for i in idx]
    b = struct.pack("<H3H", 0, *idx)
    for _ in range(3):
        b += struct.pack("<fff", 0, 0, 1)
    b += struct.pack("<3f", *s) + struct.pack("<3f", *t)
    b += struct.pack("<BB", 1, group)
    return b

def group(nm, tris, mat):
    return struct.pack("<B", 0) + name(nm, 32) + struct.pack("<H", len(tris)) + b"".join(struct.pack("<H", t) for t in tris) + struct.pack("<b", mat)

def material(nm, diffuse, emissive, shin, transp, tex, alpha):
    b = name(nm, 32)
    b += struct.pack("<4f", 0.2, 0.2, 0.2, 1.0)   # ambient
    b += struct.pack("<4f", *diffuse)
    b += struct.pack("<4f", 0.9, 0.9, 0.9, 1.0)   # specular
    b += struct.pack("<4f", *emissive)
    b += struct.pack("<ff", shin, transp)
    b += struct.pack("<B", 0)
    b += name(tex, 128) + name(alpha, 128)
    return b

def joint(nm, parent, rot, pos, rkeys, pkeys):
    b = struct.pack("<B", 0) + name(nm, 32) + name(parent, 32) + struct.pack("<3f", *rot) + struct.pack("<3f", *pos)
    b += struct.pack("<HH", len(rkeys), len(pkeys))
    for t, v in rkeys: b += struct.pack("<f3f", t, *v)
    for t, v in pkeys: b += struct.pack("<f3f", t, *v)
    return b

def build(version=4, extras=True):
    b = b"MS3D000000" + struct.pack("<i", version)
    verts = [vertex(0, 0, 0, 0), vertex(1, 0, 0, 0), vertex(0, 1, 0, 1), vertex(1, 1, 0, 1), vertex(0, 2, 0, -1), vertex(1, 2, 0, 1)]
    b += struct.pack("<H", len(verts)) + b"".join(verts)
    tris = [triangle((0, 1, 2)), triangle((1, 3, 2)), triangle((2, 3, 5)), triangle((2, 5, 4)), triangle((0, 0, 1)), triangle((0, 1, 99))]
    b += struct.pack("<H", len(tris)) + b"".join(tris)
    groups = [group("body", [0, 1], 0), group("head", [2, 3], 1), group("extra", [5, 4, 7], -1)]
    b += struct.pack("<H", len(groups)) + b"".join(groups)
    mats = [material("skin", (0.5, 0.25, 0.125, 1.0), (0, 0, 0, 1), 0.0, 1.0, ".\\skin.jpg", ""),
            material("glass", (0.1, 0.2, 0.3, 1.0), (0.1, 0.0, 0.0, 1.0), 32.0, 0.5, "glass.png", "a.png")]
    b += struct.pack("<H", len(mats)) + b"".join(mats)
    b += struct.pack("<ffI", 24.0, 0.0, 48)
    joints = [joint("root", "", (0, 0, 0), (0, 0, 0), [(0.0, (0, 0, 0)), (1.0, (0, 0, HALF_PI))], [(0.0, (0, 0, 0)), (1.0, (1, 0, 0))]),
              joint("arm", "root", (0, 0, HALF_PI), (0, 1, 0), [(0.5, (0, 0, HALF_PI))], [])]
    b += struct.pack("<H", len(joints)) + b"".join(joints)
    if extras:
        b += struct.pack("<I", 1)                                  # sub version of the comment block
        b += struct.pack("<I", 0)                                  # group comments
        b += struct.pack("<I", 0)                                  # material comments
        b += struct.pack("<I", 1) + struct.pack("<ii", 0, 5) + b"hello"   # joint comments: one
        b += struct.pack("<I", 0)                                  # model comment
        b += struct.pack("<I", 2)                                  # sub version of the vertex weight block
        for i in range(6):
            if i == 2:   # vertex 2: 25 percent on joint 0 besides its main joint 1
                b += struct.pack("<3b3BI", 0, -1, -1, 25, 0, 0, 0)
            else:
                b += struct.pack("<3b3BI", -1, -1, -1, 0, 0, 0, 0)
    return b

def w(nm, data):
    with open(os.path.join(OUT, nm), "wb") as f:
        f.write(data)

w("rig.ms3d", build(4, True))
w("rig_v3.ms3d", build(3, False))
