#!/usr/bin/env python3
"""Writes the hand-made .3ds fixtures next to this script (python3 make_3ds_fixtures.py).
Written from the public chunk description: 2-byte id, 4-byte little-endian length (including the
6 header bytes), data, then child chunks."""
import math, os, struct

def chunk(cid, payload=b""):
    return struct.pack("<HI", cid, 6 + len(payload)) + payload

def cstr(s):
    return s.encode("ascii") + b"\0"

def verts(vs):
    return chunk(0x4110, struct.pack("<H", len(vs)) + b"".join(struct.pack("<3f", *v) for v in vs))

def uvs(us):
    return chunk(0x4140, struct.pack("<H", len(us)) + b"".join(struct.pack("<2f", *u) for u in us))

def faces(fs, groups=(), smooth=None):
    data = struct.pack("<H", len(fs)) + b"".join(struct.pack("<4H", a, b, c, 7) for a, b, c in fs)
    for name, idx in groups:
        data += chunk(0x4130, cstr(name) + struct.pack("<H", len(idx)) + b"".join(struct.pack("<H", i) for i in idx))
    if smooth is not None:
        data += chunk(0x4150, b"".join(struct.pack("<I", s) for s in smooth))
    return chunk(0x4120, data)

def matrix(rows, origin):
    return chunk(0x4160, b"".join(struct.pack("<3f", *r) for r in rows) + struct.pack("<3f", *origin))

def obj(name, *parts):
    return chunk(0x4000, cstr(name) + chunk(0x4100, b"".join(parts)))

def color_f(cid, rgb):
    return chunk(cid, chunk(0x0010, struct.pack("<3f", *rgb)))

def color_b(cid, rgb):
    return chunk(cid, chunk(0x0011, bytes(rgb)) + chunk(0x0012, bytes(rgb)))

def pct(cid, v):
    return chunk(cid, chunk(0x0030, struct.pack("<H", v)))

def material(name, *parts):
    return chunk(0xAFFF, chunk(0xA000, cstr(name)) + b"".join(parts))

def main(*parts, scale=None, keyframes=False):
    ed = chunk(0x3D3E, struct.pack("<I", 3))
    if scale is not None:
        ed += chunk(0x0100, struct.pack("<f", scale))
    ed += b"".join(parts)
    body = chunk(0x0002, struct.pack("<I", 3)) + chunk(0x3D3D, ed)
    if keyframes:
        body += chunk(0xB000, chunk(0xB00A, struct.pack("<I", 1) + cstr("x") + struct.pack("<I", 0)))
    return chunk(0x4D4D, body)

def write(name, data):
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), name), "wb") as f:
        f.write(data)

# One triangle on the ground (Z-up: x right, y forward, z up).
write("one_triangle.3ds", main(obj("Tri", verts([(0, 0, 0), (2, 0, 0), (0, 2, 0)]), faces([(0, 1, 2)]))))

# A quad of two triangles with two materials, UVs and a texture with tiling.
write("two_materials.3ds", main(
    material("Red", color_f(0xA010, (0.1, 0.0, 0.0)), color_f(0xA020, (1.0, 0.0, 0.0)), color_b(0xA030, (255, 255, 255)),
             pct(0xA040, 25)),
    material("Wood", color_b(0xA020, (51, 102, 204)), pct(0xA040, 50), pct(0xA050, 25),
             chunk(0xA200, chunk(0xA300, cstr("tex\\WOOD.BMP")) + chunk(0xA354, struct.pack("<f", 2.0)) +
                   chunk(0xA356, struct.pack("<f", 3.0)) + chunk(0xA358, struct.pack("<f", 0.25)) +
                   chunk(0xA35A, struct.pack("<f", 0.5)))),
    obj("Quad", verts([(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]),
        faces([(0, 1, 2), (0, 2, 3)], groups=[("Red", [0]), ("Wood", [1])]),
        uvs([(0, 0), (1, 0), (1, 1), (0, 1)]))))

# A box tilted 90 degrees about Z and moved to x = 10, with the local-axes matrix; vertices are in world space.
# Local frame: x axis -> world +y, y axis -> world -x, z axis -> world +z. Local unit square (0..1, 0..1, 0).
loc = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
world = [(10 + (-ly), lx, lz) for lx, ly, lz in loc]
write("transformed.3ds", main(obj("Moved", verts(world), faces([(0, 1, 2), (0, 2, 3)]),
                                  matrix([(0, 1, 0), (-1, 0, 0), (0, 0, 1)], (10, 0, 0))), scale=0.5))

# Smoothing groups: two triangles folded 90 degrees along a shared edge. "Soft": both in group 1 (shared
# vertices get one averaged normal); "Hard": groups 1 and 2 (every corner keeps its face normal).
fold = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1)]   # (0,1,2) lies in z=0, (0,1,3) in y=0, both share edge 0-1
write("smoothing.3ds", main(
    obj("Soft", verts(fold), faces([(0, 1, 2), (1, 0, 3)], smooth=[1, 1])),
    obj("Hard", verts([(v[0] + 5, v[1], v[2]) for v in fold]), faces([(0, 1, 2), (1, 0, 3)], smooth=[1, 2]))))

# Things we do not read: a light, a camera, keyframes, an unknown chunk, a hidden-object flag, an object with
# vertices but no faces, a material map we do not use, ambient colour.
write("unsupported.3ds", main(
    obj("Tri", verts([(0, 0, 0), (1, 0, 0), (0, 1, 0)]), faces([(0, 1, 2)]), chunk(0x4165, b"\x01")),
    chunk(0x4000, cstr("Lamp") + chunk(0x4600, struct.pack("<3f", 0, 0, 5))),
    chunk(0x4000, cstr("Cam") + chunk(0x4700, struct.pack("<6f", 0, 0, 5, 0, 0, 0))),
    obj("Points", verts([(0, 0, 0), (1, 1, 1)])),
    chunk(0x1100, cstr("bg.bmp")),
    chunk(0x7777, b"zz"),
    material("Glossy", color_f(0xA010, (0.2, 0.2, 0.2)), chunk(0xA220, chunk(0xA300, cstr("env.bmp")))),
    keyframes=True))
