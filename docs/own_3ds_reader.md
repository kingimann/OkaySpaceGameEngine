# Our own 3DS reader (`.3ds`)

Status: **the default `.3ds` reader** (flipped on the owner's approval). Assimp (when the build has it) is the fallback for files ours rejects. Rollback: `OKAY_OWN_3DS=0` in the environment (or `-DOKAY_OWN_3DS=OFF`); with ours off and no Assimp a `.3ds` file does not import. Nothing was removed; Assimp goes one release later if no problems. This is the "write the 3DS reader" step of `docs/own_code_roadmap.md`.

## What it is

Written from the public description of the 3D Studio chunk format (little-endian `u16 id, u32 length` chunks, the length includes the 6-byte header), with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/Max3dsReader.cpp` | The reader: `okay::Read3dsBuffer` / `okay::Read3dsFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 4 of the per-format backend switch (same code as OBJ, STL, PLY, COLLADA) |
| `scripts/gen_3ds_fixtures.py` | Writes `tests/data/models/cube.3ds`, `box_flat.3ds`, `hierarchy.3ds` |
| `tests/test_3ds_reader.cpp` | Hand-computed checks on the fixtures and the seam (`3ds_reader`) |
| `tests/test_3ds_bvh_hostile.cpp` | Hostile input and mutation fuzz, built on its own under sanitizers (`3ds_bvh_hostile`) |

The reader file uses only the standard library and `okay/Render/ModelData.hpp` (header-only types), so the hostile test builds it without the engine library.

## Turning it on

Exactly like `OKAY_OWN_COLLADA`:

- CMake: `-DOKAY_OWN_3DS=ON` makes it the default `.3ds` reader for that build (default `ON`; `OFF` builds with ours off).
- Run time: environment variable `OKAY_OWN_3DS=1` (or `0`) overrides the build default; `okay::SetImportBackend(".3ds", ImportBackend::Own)` beats both; `SetImportBackend("*", ...)` covers every format with an own reader.
- Off, with no Assimp in the build: `ImportModelData` fails with a message that says how to switch it on, and `ImportableExtensions()` does not list `.3ds`.
- On, and the build has Assimp: ours is tried first, Assimp is the fallback for files ours rejects.

## What it reads

- **Meshes** (`NAMED_OBJECT` > `TRI_OBJECT`): points, faces (three `u16` indices and a flags word), texture coordinates, smoothing groups, per-face material groups, the mesh matrix. One part per (object, material). Vertices are welded per point index and normal. UV `v` is flipped (`v = 1 - v`) like the other bottom-up formats.
- **Normals** from the smoothing groups: faces whose groups share a bit share the vertex's angle-weighted normal; group 0 is flat; a file without a smoothing-group chunk is smoothed as one group.
- **Materials**: name, diffuse and specular colour (24-bit or float, linear or not), shininess (percent, passed on as the shininess value), self illumination (becomes an emissive colour: diffuse times the percentage), diffuse, specular and bump texture file names (3DS names are 8.3 upper case; the reader finds the file next to the model ignoring case).
- **Master scale** (`0x0100`) goes on the root node.
- **Keyframes** (`KFDATA`): the node hierarchy (parent ids, instance names, dummy objects, and camera / light / target nodes as plain nodes, because they share the node numbering), pivots, position, rotation and scale tracks, 30 frames per second (3DS stores frames, not seconds; 30 is the usual rate), the take starts at 0. Rotation keys are axis and angle, stored **relative to the previous key** and **clockwise for a positive angle**; the reader accumulates them and flips the sign. Instances (several nodes naming the same mesh) share the mesh.
- A node that has only one key per track is just a placement: it becomes the node's transform and no clip is made for it.

### Coordinates

Checked against real files (see below), not assumed:

- The file's vertices are in scene space (Z up). The mesh matrix (3 rows of the 3x3, then the translation) maps the object's own space to scene space for **row vectors**: `world = local * R + t`. The importer therefore stores each vertex relative to its node as `local = (stored - t) * R^-1 - pivot`, and the node places it with its keyframes. A mesh without a keyframe node hangs off the root as stored.
- The root node carries the master scale and the Z-up to Y-up rotation `(x, y, z) -> (x, z, -y)`. Flattened import (`ImportModel`, the merged-mesh path) bakes that into the vertices and returns one node and no clips.

## Limits (hostile files)

Every read is bounds-checked; a chunk longer than its parent is cut to the parent (and reported), a chunk shorter than its header fails the file.

| Limit | Value |
|---|---|
| File size | 512 MiB |
| Chunk nesting depth | 32 |
| Chunks per file | 10,000,000 |
| Objects / materials / keyframe nodes | 100,000 each |
| Points, faces in total | 20,000,000 each |
| Keys in total | 20,000,000 |
| Output vertices / mesh parts | 40,000,000 / 200,000 |
| Distinct smoothing groups tracked per vertex | 128 (the rest merge into the first) |
| Coordinates, key values, pivots | within +-1e15 and finite; the master scale in (0, 1e6) |

Counts (points, faces, keys, group lists) are checked against the bytes left in their chunk before anything is allocated. Face indices are range-checked, degenerate faces skipped, non-finite points refuse the file, a UV list that does not match the point count is dropped, a cycle in the keyframe parents is broken, a singular mesh matrix falls back to "as stored". Results that overflow in the matrix arithmetic fail the read.

## Not handled (what a user might trip over)

Each of these is also reported in `ImportedModel::notes` where the file has it:

- Lights and cameras are not imported as objects; their keyframe nodes are plain nodes and only the position track is read (lens, roll, colour, cone and target tracks are not).
- Material: transparency, two-sided, opacity / reflection / shininess / self-illumination / second maps, texture tiling, offset and rotation, the shading mode, the ambient colour. Colour is taken from the diffuse chunk only.
- Spline parameters on keys (tension, continuity, bias, ease) are skipped, every key is played linearly; the clip's quaternions are interpolated by the engine, not along the file's spline.
- Frame rate is assumed to be 30 per second. A file made at another rate plays too fast or too slow.
- Mesh matrix is used for placement only; mirrored (negative determinant) objects are not specially handled; the "mesh texture info" chunk, the "proc" chunks, the viewport and layout chunks, the keyframe "current time" and "scene" chunks are ignored.
- 3DS faces with edge-visibility flags, polygon (n-gon) information and hidden-object flags are ignored. Quads are two triangles as stored.
- Object `hidden` / `frozen` flags are not read.
- The 64K limit of the format is the 64K limit here: 65,535 points and 65,535 faces per object.
- Smoothing normals use angle weighting; another importer may weight by area, so normals can differ by a few degrees on irregular meshes.
- Not benchmarked. Largest real file read: 31,460 triangles in 31 ms.

## Tests and what was checked

- `3ds_reader`: the three fixtures against hand-computed numbers (welded and flat vertex counts, normals, UV flip, root rotation and scale, master scale, material fields and the note for each skipped thing, hierarchy and parent order, an instance, pivot, mesh matrix, rotation sign and accumulation, 30 fps times), then the seam (switch order, merged-mesh path, scene import with a `ModelAnimator` clip, flattened import, failure messages).
- `3ds_bvh_hostile` (standalone, no engine): every truncation of every fixture; bad lengths, 5000-deep nesting, point and face arrays that claim more than they hold, indices out of range, non-finite and huge values, a vertex with thousands of distinct smoothing groups (time-checked), 65,535 faces on three points, 3000 objects, 100,001 materials, keyframe cycles, a 2000-deep keyframe chain, track key counts of 4 billion, NaN keys, singular scales; then a deterministic mutation fuzz (byte flips, length stomps, inserts, deletes). Built with `-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all`: clean, including 150,000 mutations per format (`OKAY_3DS_FUZZ_ITERS`).
- Real files: 16 files from an outside corpus, see `docs/model_corpus.md`. Fifteen are read; the sixteenth is an empty file (rejected, as Assimp rejects it). Against Assimp built from source on the same files: mesh count, triangle count, bounding box and surface area match on all 15 static or animated meshes, including the two files with a mesh matrix and an animated rotation, which is how the row-vector convention and the clockwise rotation were found; the animation key values at the last key match to four decimals. Differences: Assimp reports the animation of camera-only files and of a camera parent that we only keep as a node; Assimp adds a default material; durations differ by the frame-rate assumption (Assimp sets no rate for this format; the comparison divided its frame count by 25, we divide by 30; the frame counts are equal).

## Before it can become the default

The default stays off until: the reader has been run on files from more than one real exporter (the corpus above is a handful of files from a few tools), the Windows distribution has run it, and the owner approves. After that `ModelImportAssimp.cpp` can lose its 3DS role (`docs/own_code_roadmap.md`).
