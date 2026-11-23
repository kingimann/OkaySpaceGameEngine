#!/usr/bin/env python3
"""Writes the two Quake III map sample files shipped for our own Q3BSP reader:
  examples/own_readers/tiny_arena.bsp   a bare IBSP version 46 map
  examples/own_readers/tiny_arena.pk3   the same map inside a zip (deflated), as maps/tiny_arena.bsp

The map is a 512 x 512 x 256 room (floor, ceiling, four walls: polygon faces), a bump in the middle of the floor (one 3 x 3
Bezier patch, its centre control point 160 units up, so the surface peaks 40 units up) and a 80 x 80 platform at z = 64 in the
corner, which is a second model (a child node "model_1"). Everything is computed here from the public description of the
format (144 byte header with 17 lumps, records of 72 / 44 / 4 / 104 / 40 bytes); no outside file or text is used. The triangles
are stored clockwise, the way Quake III stores them. Run from the repository root:
  python3 scripts/gen_q3bsp_samples.py
(tests/test_own_q3bsp_samples.cpp checks the committed files.) The textures are only named (textures/own/...): no picture is
shipped, so the map shows untextured in the engine; the geometry, nodes, materials and the Z-up turn are what is on show."""
import io, os, struct, sys, zipfile

tex = [("textures/own/floor", 0), ("textures/own/wall", 0), ("textures/own/bump", 0)]
verts, meshverts, faces = [], [], []   # vertex: (pos, st, normal)

def quad(t, p, n):
    """A polygon face from four corners given counter clockwise seen from the front; stored clockwise, the file's way."""
    base = len(verts)
    st = [(0, 0), (1, 0), (1, 1), (0, 1)]
    for i in range(4):
        verts.append((p[i], st[i], n))
    mv = len(meshverts)
    meshverts.extend([0, 2, 1, 0, 3, 2])
    faces.append(dict(tex=t, type=1, vertex=base, nverts=4, meshvert=mv, nmv=6, size=(0, 0)))

quad(0, [(0, 0, 0), (512, 0, 0), (512, 512, 0), (0, 512, 0)], (0, 0, 1))                      # floor
quad(1, [(0, 0, 256), (0, 512, 256), (512, 512, 256), (512, 0, 256)], (0, 0, -1))              # ceiling
quad(1, [(512, 0, 0), (0, 0, 0), (0, 0, 256), (512, 0, 256)], (0, 1, 0))                       # wall y = 0
quad(1, [(0, 512, 0), (512, 512, 0), (512, 512, 256), (0, 512, 256)], (0, -1, 0))              # wall y = 512
quad(1, [(0, 0, 0), (0, 512, 0), (0, 512, 256), (0, 0, 256)], (1, 0, 0))                       # wall x = 0
quad(1, [(512, 512, 0), (512, 0, 0), (512, 0, 256), (512, 512, 256)], (-1, 0, 0))              # wall x = 512
# the bump: control grid, rows along y, columns along x
base = len(verts)
for r in range(3):
    for c in range(3):
        z = 160 if (r == 1 and c == 1) else 0
        verts.append(((192 + 64 * c, 192 + 64 * r, z), (c / 2.0, r / 2.0), (0, 0, 1)))
faces.append(dict(tex=2, type=2, vertex=base, nverts=9, meshvert=len(meshverts), nmv=0, size=(3, 3)))
# model 1: the platform
quad(0, [(400, 400, 64), (480, 400, 64), (480, 480, 64), (400, 480, 64)], (0, 0, 1))
models = [(0, 7), (7, 1)]

def build():
    l = [b""] * 17
    l[1] = b"".join(n.encode().ljust(64, b"\0") + struct.pack("<ii", f, 0) for n, f in tex)
    l[10] = b"".join(struct.pack("<3f2f2f3f4B", *p, *st, 0, 0, *n, 255, 255, 255, 255) for p, st, n in verts)
    l[11] = b"".join(struct.pack("<i", m) for m in meshverts)
    FACE = "<8i2i2i3f6f3f2i"   # texture, effect, type, vertex, n_vertexes, meshvert, n_meshverts, lm_index, lm_start, lm_size, lm_origin, lm_vecs, normal, size
    l[13] = b"".join(struct.pack(FACE, f["tex"], -1, f["type"], f["vertex"], f["nverts"], f["meshvert"], f["nmv"], -1,
                                 *([0] * 4), *([0.0] * 3), *([0.0] * 6), *([0.0] * 3), *f["size"]) for f in faces)
    l[7] = b"".join(struct.pack("<6f4i", 0, 0, 0, 0, 0, 0, f, n, 0, 0) for f, n in models)
    out = b"IBSP" + struct.pack("<i", 46)
    off = 8 + 17 * 8
    for x in l:
        out += struct.pack("<ii", off, len(x)); off += len(x)
    return out + b"".join(l)

assert struct.calcsize("<8i2i2i3f6f3f2i") == 104
bsp = build()
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("examples", "own_readers")
os.makedirs(out, exist_ok=True)
with open(os.path.join(out, "tiny_arena.bsp"), "wb") as f:
    f.write(bsp)
buf = io.BytesIO()
with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
    zi = zipfile.ZipInfo("maps/tiny_arena.bsp", (2026, 1, 1, 0, 0, 0)); zi.compress_type = zipfile.ZIP_DEFLATED
    z.writestr(zi, bsp)
with open(os.path.join(out, "tiny_arena.pk3"), "wb") as f:
    f.write(buf.getvalue())
print("wrote tiny_arena.bsp (%d bytes) and tiny_arena.pk3 (%d bytes) to %s" % (len(bsp), len(buf.getvalue()), out))
