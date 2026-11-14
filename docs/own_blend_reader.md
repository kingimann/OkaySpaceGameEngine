# Our own Blender reader (`.blend`)

Status: **side by side, off by default.** Assimp (when the build has it, `OKAY_USE_ASSIMP`) is still the only other reader of `.blend`. Without Assimp and without switching ours on, a `.blend` file does not import. Nothing was removed and no default was changed. This is the "write the Blender reader" step of `docs/own_code_roadmap.md`, written from the public description of the file layout with no code from any other importer.

## What it is

| File | What |
|---|---|
| `engine/src/Render/BlendReader.cpp` | The reader: `okay::ReadBlendBuffer` / `okay::ReadBlendFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 6 of the per-format backend switch (same code as COLLADA, 3DS, BVH) |
| `scripts/gen_blend_fixtures.py` | Writes the **synthetic** fixtures `tests/data/models/blend_*.blend` (see "What was and was not tested") |
| `tests/test_blend_reader.cpp` | Hand-computed checks on the fixtures and the seam (`blend_reader`) |
| `tests/test_blend_hostile.cpp` | Hostile input and mutation fuzz, built on its own under sanitizers (`blend_hostile`) |
| `tests/test_model_corpus.cpp` | Real-file corpus rows for reader `blend` (`model_corpus`, needs `OKAY_MODEL_CORPUS`) |

The reader file uses the standard library, `okay/Render/ModelData.hpp` (header-only types) and the in-tree inflate (`okay::Inflate`, `Core/ImageDecode.cpp`), so the hostile test builds it without the engine library.

## Turning it on

Exactly like `OKAY_OWN_3DS`: CMake `-DOKAY_OWN_BLEND=ON` (default `OFF`); environment variable `OKAY_OWN_BLEND=1` / `0`; `okay::SetImportBackend(".blend", ImportBackend::Own)`; `SetImportBackend("*", ...)` covers every format with an own reader. Off, with no Assimp in the build: `ImportModelData` fails with a message saying how to switch it on and `ImportableExtensions()` does not list `.blend`. On, with Assimp in the build: ours is tried first, Assimp is the fallback for files ours rejects.

## How it reads a file

A `.blend` file is `BLENDER` + pointer size (`_` 4 bytes, `-` 8) + endianness (`v` little, `V` big) + 3 version digits, then blocks: a 4 byte code, a size, the writer's memory address of the block ("old pointer"), the index of the struct the block holds, and an element count. One block, `DNA1`, is the SDNA: every struct the writer knew, with field names, types and order. The reader parses that table, **computes every struct layout from it** (and checks the sum against the file's own struct size), and reads every field **by name** from the file's own layout. It never assumes a Blender version's layout, so the same code reads 2.4x to 4.x files and 32/64 bit, little/big endian ones. Field names that changed between versions are looked up under both names (`totvert`/`verts_num`, `vdata`/`vert_data`, `poly_offset_indices`/`face_offset_indices`, `defbase`/`vertex_group_names`, `size`/`scale`, ...). Old pointers are resolved through a sorted table of block start addresses; a pointer is valid only inside a block, and every field read is checked against the bytes left in its block.

- **Compression**: gzip (Blender 2.5x to 2.9x "Compress File") is read with the in-tree inflate (output cap 768 MiB, size trailer checked). **zstd (Blender 3.0+) is not supported**: a zstd decoder is feasible (FSE and Huffman tables, about 600 lines) but there was no zstd encoder on the build machine to produce test data and a decoder with no real test data was judged worse than none; the file is rejected with a message that says so. The **Blender 5.0 container** (`BLENDER17-01v....`, large block headers) is likewise rejected by name.
- **Objects**: every object block that the current scene (`FileGlobal.curscene`, else the first scene) reaches through its base list (2.7x) or its master collection tree (2.8+), plus their parents; else, with a note, all objects. Local transform = `parentinv * T(loc + dloc) * R * S(size * dscale)`; rotation modes: Euler (all six orders), quaternion, axis-angle. Parent types: object, armature, **bone** (attached at the bone tail like Blender), vertex / curve parents (attached to the parent's origin, named). The result is checked against the **saved `obmat` of every object** and a mismatch is reported (this is how a "track to" camera was found).
- **Meshes**, three storages, chosen from what the file has: (1) `MFace` + `MTFace`/`TFace` (up to 2.6x, and 2.4x); (2) `MPoly` + `MLoop` + `MLoopUV` (2.63 to 3.5, as pointers or custom-data layers); (3) the attribute storage of 3.6+/4.x: `position`, `.corner_vert`, face offsets, `material_index`, `sharp_face`, float2 UV layers. Polygons are triangulated (quads along the diagonal that keeps both triangles facing the polygon normal; larger polygons by ear clipping on the Newell plane, fanned only when a work budget or the 256-corner cap is hit). Normals: per face flat or smooth as flagged; smooth corners get the angle-weighted average of the smooth faces around the vertex. Vertices are welded per (vertex, normal, UV). UV `v` is flipped like the other bottom-up formats. One part per material slot.
- **Materials**: diffuse colour, emission (`emit`), hardness as shininess, metallic / roughness (2.8+), alpha only when the material is set to be transparent; legacy image texture slots (colour, normal, specular maps); node materials: Principled BSDF base colour, metallic, roughness, alpha, emission, the image texture linked into Base Color, and a normal map node's image. Image paths: `//` is relative to the .blend, backslashes become `/`.
- **Armatures**: bones as nodes under the armature object (rest from `arm_mat`, the stored pose channel values applied); vertex groups matched to bones by name give the 4 strongest weights per vertex (normalised); the inverse bind is `inverse(armatureWorld * bone.arm_mat) * meshWorld`.
- **Animation**: the active action of each object (`AnimData.action`): F-Curves on `location`, `scale`, `rotation_euler`, `rotation_quaternion`, `rotation_axis_angle` of the object or of `pose.bones["X"]`. Constant, linear and bezier keys (bezier is evaluated with Blender's handle clamping and sampled at every whole frame; a held key gets a step just before the next); frame rate from the scene; a clip starts at its first key. Channels with a single key are not made (the node transform already is that placement). Each sample is a full local matrix decomposed to position / rotation / scale, so parent inverses and Euler orders come out right.
- The root node carries the Z-up to Y-up rotation `(x, y, z) -> (x, z, -y)`; flattened import (`ImportModel`) bakes each object's world matrix and that rotation into the vertices and returns one node and no skins or clips.

## Named, not silent

Everything below is reported once in `ImportedModel::notes` (also logged as a warning) when the file has it: lights, cameras and other object types (kept as empty nodes); modifiers (not applied); constraints, track-to targets, pose-bone constraints (not evaluated); group / collection instances (not expanded); vertex colours (the engine mesh has no vertex colour channel); auto-smooth angle, custom split normals, sharp edges; shape keys; vertex parents; packed images (name only, pixels not extracted); node materials without a Principled BSDF; pre-2.5 Ipo animation; NLA tracks, drivers, F-Curve modifiers; easing interpolation (treated as linear); F-Curves on any other property; animation of materials / cameras / lights / shape keys; shear in a node transform; unit scale other than 1; meshes with truncated arrays; faces with out-of-range indices; computed world matrix differing from the saved one.

## Limits (hostile files)

| Limit | Value |
|---|---|
| File size / after gunzip | 512 MiB / 768 MiB |
| Blocks / structs / struct fields / names | 4,000,000 / 20,000 / 4,000 / 200,000 |
| Items followed in one list | 1,000,000 |
| Objects / bones / materials | 100,000 each |
| Vertices / faces / corners in one mesh | 20 M / 20 M / 60 M |
| Output vertices / parts | 40 M / 200,000 |
| F-Curves / keys per curve / output keys | 400,000 / 4 M / 20 M |
| Whole frames sampled per animated node | 40,000 |
| Bone and collection depth, parent chain | 512, 64, 512 |
| Ear clipping | 256 corners, 300 M inner steps per file, then fan |
| Coordinates, keys | finite and within +-1e15 |

Counts are checked against the bytes in their block before anything is allocated; cycles in parent chains are cut and named; bone and collection recursion is iterative or depth-capped.

## What was and was not tested

**Real files** (28, from the `test/models/BLEND` and `test/models-nonbsd/BLEND` directories of an Assimp v5.4.3 checkout, `docs/model_corpus.md`): Blender 2.41, 2.48, 2.49, 2.50 / 2.52 (gzip), 2.60 (big endian, 32 bit), 2.62, 2.69, 2.71, 2.76, 2.79, 2.93; 64-bit little endian, plus one 32-bit big-endian file. **No real Blender 3.x or 4.x file was available.** The 3.6+/4.x attribute storage (`position`, `.corner_vert`, face offsets, `sharp_face`, float2 UV, numeric custom-data type ids 11, 45, 47, 48, 49, 50) is written from memory of Blender's DNA headers and is covered only by the **synthetic** fixtures, which were written by `scripts/gen_blend_fixtures.py` and not by Blender: they prove the code agrees with the script's reading of the layout, not with Blender.

- All 28 files read (Assimp: 27; it fails `BlenderDefault_262.blend`). Compared with Assimp built from the same sources (only its Blender importer): triangle position sets, UV sets, bounding boxes, areas and material colours match on 22 of the 28 files (every corner of ours has the same position and UV in Assimp's output; vertex counts differ because Assimp does not weld). The remaining six are the modifier / dropped-object differences below, plus the file Assimp cannot read. Diffuse colours, shininess and texture names were compared by eye on eight files with materials and agree (paths differ only in `//` resolution); `BlenderMaterial_269` differs in alpha (below).
- Differences, all understood: Assimp **applies Mirror and Subsurf modifiers** (`HUMAN`, `MirroredCube_252`, `SuzanneSubdiv_252`, `yxa_1`: we show the stored base mesh and say so); Assimp **loses three of the ten cubes** of `CubeHierarchy_248` (all ten are on the visible layer; ours has all); Assimp uses the stored smooth vertex normal even on flat-shaded faces, ours honours the face flag (the default cube is flat); on smooth meshes our normals agree with Blender's own stored vertex normals on 100 % of corners for the simple meshes and 92 to 98 % on the two armature files (stored normals include auto-smooth / edge splits); Assimp reports diffuse alpha 1.0 for `BlenderMaterial_269`, whose alpha is 0.01 with the transparency bit set in `mode` (ours reports 0.01; the bit meanings are from memory of the DNA headers); Assimp has no armatures or animation at all.
- Ground truth from the files themselves: the saved world matrix of every object equals our computed one in 27 of 28 files (the exception is a camera aimed by an old-style track-to, now named); for `fleurOptonl.blend` all 71 pose bones equal Blender's own stored `pose_mat` to 1e-5; in `Bob.blend` 31 of 33 differ because two bones have constraints that we do not evaluate (named). `arm_head` equals the translation of `arm_mat` for all 104 bones.
- **Animation was not checked against any real animated file**: the corpus has one trivial one-key action and the armature files carry pre-2.5 Ipo animation (named, not read). F-Curve evaluation (bezier with flat handles, linear quaternion, whole-frame sampling, time base) is covered by the synthetic fixture only.
- `blend_reader` (engine linked): four encodings of the attribute fixture (4.1 names, 3.6 names, big endian + 32 bit, gzip) must give identical meshes; hand-computed world positions, concave polygon area, UV flip, materials, collections, orphan object excluded, skin weights, inverse binds, bezier values (1.04 at frame 3, 5.0 at frame 6), flatten, the seam, container errors.
- `blend_hostile` (standalone, no engine): every truncation of every fixture; bad headers; a block size of -1 and 2 GB; SDNA claiming billions of names or structs, bad type indices, arrays of 2^31 and 99999x99999 elements, mismatching struct sizes; an object whose parent is itself; a 40 MiB stored-block gzip; every block header field set to boundary values; pointer scrambling; then a deterministic mutation fuzz (byte flips, length stomps, inserts, deletes, inf / NaN / 2^31 / 2^63 floats). Built with `-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all`: clean, including 60,000 mutations of the fixtures and 25,000 mutations of ten real files (`OKAY_BLEND_FUZZ_ITERS`, `OKAY_BLEND_FUZZ_REAL_ITERS`), slowest single read 0.05 s. (One finding: a mutated bone matrix gave a non-finite inverse bind; fixed and named.)

## Not handled

Everything in "Named, not silent", plus: zstd and Blender 5.0 files; library-linked data from other .blend files; layer, visibility and hide flags (all objects of the scene are imported); curves, text, metaballs, lattices, point clouds, volumes, grease pencil; dupli / particle instancing; bone inherit-rotation / scale flags, B-Bone segments, IK; non-Principled shader nodes; texture coordinate nodes and mapping; images packed in the file; per-vertex delta rotations in non-Euler modes; multiple scenes (only the current one); sub-frame times; custom split normals; Blender versions after 4.x not seen (field renames not in the alias lists would show as a missing mesh and a note).

## Before it can become the default

The default stays off until: a real Blender 3.x and 4.x file has been read and compared (the largest gap), animated real files have been checked, the Windows distribution has run it, zstd is either supported or accepted as a documented limit, and the owner approves. After that `ModelImportAssimp.cpp` can lose its Blender role (`docs/own_code_roadmap.md`).
