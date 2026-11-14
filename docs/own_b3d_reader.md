# Our own B3D reader (`.b3d`)

Status: **side by side, off by default** (`OKAY_OWN_B3D`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of `.b3d`. Nothing was flipped.

## What it is

B3D is the model format of Blitz3D (and BlitzMax / Blitz3D-derived tools): a tree of chunks (`BB3D`, `TEXS`, `BRUS`, `NODE`, `MESH`, `VRTS`, `TRIS`, `BONE`, `KEYS`, `ANIM`). Written from the public description of the format (Mark Sibly's `b3dfile_specs.txt`) with no code from any other importer. Unlike MD2 and MD3 it has real skeletal animation, which the engine can play.

| File | What |
|---|---|
| `engine/src/Render/B3dReader.cpp` | `okay::ReadB3dBuffer` / `okay::ReadB3dFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp`, `ReaderCommon.hpp` | Shared bounds-checked cursor and number checks |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 20 of the opt-in table |
| `tests/test_b3d_reader.cpp`, `tests/batchc_fixtures.hpp` | Hand-computed checks on a synthetic file (`b3d_reader`) |
| `tests/test_batchc_hostile.cpp` | Hostile input and mutation fuzz (`batchc_hostile`) |

## What it reads

- The chunk tree, walked **with an explicit stack** (no recursion, nesting capped at 10,000), every chunk size checked against its parent. Unknown chunks are skipped by their size and counted.
- **Nodes** (name, position, scale, rotation), parent first. One top-level node is the root; several get a new root named `B3D`.
- **Meshes**: one part per `TRIS` chunk; vertices welded per vertex number; normals; the first texture-coordinate set (as written); the brush as material (diffuse colour and alpha, shininess = file value x 128, first texture, joined to the model's folder); a mesh without normals gets smooth ones.
- **Skin**: every node with a `BONE` chunk is a joint; the four strongest weights per vertex (weights are normalised); the inverse bind matrix maps the mesh's own coordinates to the joint's (the inverse of the joint's rest-pose world matrix times the mesh node's world matrix, the identity in files that follow Blitz3D's rules; the engine skins a vertex as joint world x inverse bind x vertex). Vertices with no weight follow the root node. `BONE` vertex numbers count over the whole file, as the format's notes say.
- **Animation**: the `KEYS` chunks (position, scale, rotation) of each node as one clip, times = frame / frames per second of the `ANIM` chunk (60 when the file has none or a bad rate).
- **Handedness.** Blitz3D is left-handed. Z is mirrored in positions, normals, translations and rotation keys, and every triangle's winding is reversed. A rotation `(w, x, y, z)` in the file becomes `(x, y, -z, w)` (the file's quaternions follow Blitz3D's matrix convention, and the mirror turns it a second time). On the real files this gives the same node rotations and key rotations as Assimp's importer (see below).

## Not handled (each is a line in `out.notes`)

Vertex colours, texture blend modes, flags, positions, scales and rotations (texture transforms), further texture-coordinate sets, brush effects (two-sided, fullbright, ...), the brush's specular, more than one `ANIM` chunk (the first gives the frame rate; Blitz3D files have one), sprites and other node kinds that have no mesh.

## Limits

File 256 MiB; nodes 100,000; vertices 20,000,000; triangles 20,000,000; keys 20,000,000; textures and brushes 100,000 each; nesting 10,000. All counts that come from the file are checked against the bytes left in their chunk before anything is allocated. Positions, scales, colours and key values must be finite and within 1e15; a bad value in a normal or texture coordinate becomes 0 and is counted.

## Tests and what was checked

- `b3d_reader`: **synthetic** file built in memory (a root node with a one-triangle mesh, two bone nodes, weights, keys, an animation), every number computed by hand: the mirror (positions, normals, rotation `(h 0 h 0)` to `(0 h 0 h)`), vertices in the mesh node's coordinates, area 0.5, winding, weights and their normalisation (0.25 to 1.0), the inverse bind of a bone with scale and rotation `(-2, -0.5, 0)`, animation times (frame 30 at 30 per second is 1 s, at the default 60 per second 0.5 s), an unweighted vertex, two top-level nodes, no bones, refusals and **every truncation**.
- `batchc_hostile` (B3D section): every truncation; **every four-byte position of the file set to 0, 1, 3, 255, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFE, NaN, infinity, the largest float and 2^62** (sizes, counts, ids, floats); 30,000 nested nodes; 150,000 flat nodes; a valid 50,000-vertex mesh with 5 bones per vertex (0.2 s); 100,000 mutations. Sanitizers (`address,undefined,float-cast-overflow`): clean. The run found one thing, which is fixed: a brush shininess near the largest float became infinity after the multiplication by 128.

### Real files

Assimp's tree has 3 B3D files (`models/B3D/WusonBlitz.b3d`; `models-nonbsd/B3D/dwarf2.b3d` and `turtle1.b3d`, both skinned and animated). **All 3 read and equal Assimp v5.4.3 (built from the same sources, only the B3D importer) on mesh count, triangles, surface area, bounding box, node count, clip count and clip length** (15.083 s and 6.25 s). Beyond what the corpus test compares: on `dwarf2` (47 nodes, 14,148 keys) and `turtle1` (25 nodes, 1,724 keys), the **local position, scale and rotation of every node and every key value** equal Assimp's after its own handedness conversion (position keys exactly; rotation to the 4th decimal), **the sum of the skin weights of every bone** and the number of weight entries equal Assimp's (40 and 22 bones with weights, difference 0), and **the inverse bind matrices equal Assimp's bone offset matrices** (largest element difference 4e-6). Assimp keeps three vertices per triangle (5,688 for `dwarf2`), ours welds (1,479).

Not verified: `TEXS` / `BRUS` fields other than the first texture and colours on files with several textures per brush, vertex colours, files from Blitz3D exporters other than the three in the tree, a run on real Windows.
