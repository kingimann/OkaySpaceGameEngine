#!/usr/bin/env python3
"""Writes the two glTF 1.0 sample files shipped for our own glTF 1.0 reader:
  examples/own_readers/box_v1.gltf   JSON, the geometry in a base64 data URI
  examples/own_readers/box_v1.glb    binary form (KHR_binary_glTF), the geometry in the body

Both hold the same thing: an orange 1 x 1 x 1 box (24 vertices with normals and texture coordinates, 12 triangles) on a node
that is moved up by half a unit, with one material (diffuse colour and shininess in `values`). Everything is computed here, from
the glTF 1.0 layout as our reader reads it; no outside file or text is used. Run from the repository root:
  python3 scripts/gen_gltf1_samples.py
(tests/test_own_gltf1_samples.cpp checks the committed files, so a change here must be followed by a regeneration.)"""
import base64, json, os, struct, sys

faces = [  # normal, four corners counter clockwise seen from outside
    ((0, 0, 1),  [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),
    ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]),
    ((1, 0, 0),  [(1, -1, 1), (1, -1, -1), (1, 1, -1), (1, 1, 1)]),
    ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)]),
    ((0, 1, 0),  [(-1, 1, 1), (1, 1, 1), (1, 1, -1), (-1, 1, -1)]),
    ((0, -1, 0), [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)]),
]
uvs = [(0, 0), (1, 0), (1, 1), (0, 1)]
pos, nor, uv, idx = [], [], [], []
for n, corners in faces:
    b = len(pos)
    for k, c in enumerate(corners):
        pos.append(tuple(0.5 * x for x in c)); nor.append(n); uv.append(uvs[k])
    idx += [b, b + 1, b + 2, b, b + 2, b + 3]

body = b""
def add(data):
    global body
    off = len(body); body += data
    while len(body) % 4: body += b"\0"
    return off
pos_off = add(b"".join(struct.pack("<3f", *p) for p in pos))
nor_off = add(b"".join(struct.pack("<3f", *p) for p in nor))
uv_off = add(b"".join(struct.pack("<2f", *p) for p in uv))
idx_off = add(b"".join(struct.pack("<H", i) for i in idx))

def doc(buffer_id, buffer_entry):
    mins = [min(p[i] for p in pos) for i in range(3)]; maxs = [max(p[i] for p in pos) for i in range(3)]
    return {
        "asset": {"version": "1.0", "generator": "okayspace scripts/gen_gltf1_samples.py"},
        "scene": "scene",
        "scenes": {"scene": {"nodes": ["box"]}},
        "nodes": {"box": {"name": "box", "translation": [0, 0.5, 0], "meshes": ["mesh"]}},
        "meshes": {"mesh": {"name": "box", "primitives": [{"attributes": {"POSITION": "acc_pos", "NORMAL": "acc_nor", "TEXCOORD_0": "acc_uv"},
                                                           "indices": "acc_idx", "material": "orange", "mode": 4}]}},
        "materials": {"orange": {"name": "orange", "values": {"diffuse": [0.95, 0.5, 0.1, 1.0], "shininess": 24}}},
        "accessors": {
            "acc_pos": {"bufferView": "bv_pos", "byteOffset": 0, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": mins, "max": maxs},
            "acc_nor": {"bufferView": "bv_nor", "byteOffset": 0, "componentType": 5126, "count": len(nor), "type": "VEC3"},
            "acc_uv": {"bufferView": "bv_uv", "byteOffset": 0, "componentType": 5126, "count": len(uv), "type": "VEC2"},
            "acc_idx": {"bufferView": "bv_idx", "byteOffset": 0, "componentType": 5123, "count": len(idx), "type": "SCALAR"},
        },
        "bufferViews": {
            "bv_pos": {"buffer": buffer_id, "byteOffset": pos_off, "byteLength": 12 * len(pos), "target": 34962},
            "bv_nor": {"buffer": buffer_id, "byteOffset": nor_off, "byteLength": 12 * len(nor), "target": 34962},
            "bv_uv": {"buffer": buffer_id, "byteOffset": uv_off, "byteLength": 8 * len(uv), "target": 34962},
            "bv_idx": {"buffer": buffer_id, "byteOffset": idx_off, "byteLength": 2 * len(idx), "target": 34963},
        },
        "buffers": {buffer_id: buffer_entry},
    }

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("examples", "own_readers")
os.makedirs(out, exist_ok=True)
uri = "data:application/octet-stream;base64," + base64.b64encode(body).decode()
j = doc("box_buffer", {"type": "arraybuffer", "byteLength": len(body), "uri": uri})
with open(os.path.join(out, "box_v1.gltf"), "w", newline="\n") as f:
    json.dump(j, f, indent=1); f.write("\n")

jb = doc("binary_glTF", {"type": "arraybuffer", "byteLength": len(body), "uri": "data:,"})
content = json.dumps(jb, separators=(",", ":")).encode()
while len(content) % 4: content += b" "
total = 20 + len(content) + len(body)
with open(os.path.join(out, "box_v1.glb"), "wb") as f:
    f.write(b"glTF" + struct.pack("<III", 1, total, len(content)) + struct.pack("<I", 0) + content + body)
print("wrote box_v1.gltf and box_v1.glb to", out, "(body", len(body), "bytes)")
