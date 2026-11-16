# Our own Blender reader (`.blend`)

Status: **side by side, off by default.** Assimp (when the build has it, `OKAY_USE_ASSIMP`) is still the only other reader of `.blend`. Without Assimp and without switching ours on, a `.blend` file does not import. Nothing was removed and no default was changed. This is the "write the Blender reader" step of `docs/own_code_roadmap.md`, written from the public description of the file layout with no code from any other importer.

## What it is

| File | What |
|---|---|
| `engine/src/Render/BlendReader.cpp` | The reader: `okay::ReadBlendBuffer` / `okay::ReadBlendFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/BlendModifiers.cpp` / `.hpp` | The Mirror and Subdivision Surface modifiers (section "Modifiers") |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 6 of the per-format backend switch (same code as COLLADA, 3DS, BVH) |
| `scripts/gen_blend_fixtures.py` | Writes the **synthetic** fixtures `tests/data/models/blend_*.blend` and the copies in `examples/own_readers` (see "What was and was not tested"); `--check` writes nothing and fails when a committed file differs (ctest `blend_fixtures_check`) |
| `tests/test_blend_reader.cpp` | Hand-computed checks on the fixtures and the seam (`blend_reader`) |
| `tests/test_blend_modifiers.cpp` | Modifiers: hand-computed Catmull-Clark and Mirror results, the fixture, the corpus files with modifiers, a fuzz (`blend_modifiers`, standalone) |
| `tests/test_blend_hostile.cpp` | Hostile input and mutation fuzz, built on its own under sanitizers (`blend_hostile`) |
| `tests/test_model_corpus.cpp` | Real-file corpus rows for reader `blend` (`model_corpus`, needs `OKAY_MODEL_CORPUS`) |

The reader file uses the standard library, `okay/Render/ModelData.hpp` (header-only types) and the in-tree inflate (`okay::Inflate`, a thin wrapper in `Core/ImageDecode.cpp` over the one decoder in `Core/Inflate.hpp`), so the hostile test builds it without the engine library.

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
- **Modifiers**: Mirror and Subdivision Surface are applied, in stack order (next section).
- The root node carries the Z-up to Y-up rotation `(x, y, z) -> (x, z, -y)`; flattened import (`ImportModel`) bakes each object's world matrix and that rotation into the vertices and returns one node and no skins or clips.

## Modifiers

Each mesh object's `modifiers` list is walked; every item is a block whose SDNA struct names its kind (`MirrorModifierData`,
`SubsurfModifierData`, ...), and every setting is read by field name from the file's own layout, like everything else. The
modifiers below are applied to a copy of the mesh, in stack order; the stored mesh (shared by objects using the same mesh)
is not changed. A modifier switched off for the viewport (`ModifierData.mode` without bit 1) is skipped and counted. Every
other kind (Array, Boolean, Solidify, Bevel, Decimate, Multires, ...) is skipped with one note line per kind and name, for
example `modifier 'Array' (Array) not applied (not supported) on 1 object(s)`. The Armature modifier is the skin (read
as weights, as before).

**Mirror** (`MirrorModifierData`): axes from the flag bits X 8, Y 16, Z 32 (files that set none use the old `axis` field);
each axis in turn doubles the mesh (X, then Y, then Z). The copy has its winding reversed so it faces outward. Merge (on
unless flag 128): a vertex whose mirror image is within `tolerance` of it is not copied but moved onto the mirror plane, and
a face lying entirely in the plane is not doubled. A mirror object (`mirror_ob`) makes the mirror plane that object's own
planes (mesh -> mirror object space -> flip -> back). U / V flip (flag 2 / 4): the copy's `u` becomes `1 - u` (plus
`uv_offset` and `uv_offset_copy` when the file has them). Vertex groups (flag 64): the copy's weights move to the group with
the other side's name (`Arm.L` / `Arm.R`, `L_arm`, `left` / `right`). Clipping (flag 1) only matters while editing in
Blender and changes nothing here. Bisect (flags 256 to 1024) is not done (named).

**Subdivision Surface** (`SubsurfModifierData`): Catmull-Clark (`subdivType` 0) or simple (1). Written from the
Catmull-Clark paper (1978) and, for creases, DeRose, Kass and Truong (1998): face points (mean of the corners), edge points
(the two ends and the two face points, / 4), vertex points `(Q + 2R + (n - 3) S) / n`; boundary and non-manifold edges use
the midpoint and boundary vertices `(a + 6 S + b) / 8` (corners with three or more sharp edges stay). Every n-gon becomes n
quads; material and smooth flag are kept per face. The level is the **higher of the viewport and render levels**: that is
what Assimp shows on the corpus (`yxa_1` viewport 3 > render 2, `HUMAN` render 2 > viewport 1; checked by the triangle
counts, which then match exactly). Corners at bit-identical positions are joined before subdividing, so a mesh stored split
along its edges (the corpus Suzanne has 1,198 vertices at 505 positions) is smoothed as one surface; Assimp does the same,
Blender itself would keep the pieces apart (unverified: no Blender was run). UVs: with "subdivide UVs" (old flag 8, newer
`uv_smooth` not 0) they are subdivided by the same rules on their own topology (corners joined by vertex and UV, so seams
are boundaries); without it they are interpolated linearly (2.8+ files: the "UV Smooth" mode, next subsection). Edge
creases: `MEdge.crease` (a byte) or the 4.x `crease_edge` attribute, turned into a DeRose sharpness `s` (the number of
levels an edge stays sharp): crease 1 is sharp at every level; a partial crease `c` gives `s = 10 c^2` in files from
Blender 2.80 on (the OpenSubdiv mapping, from memory, unverified) and `s = c x levels` in older files (our choice, as
before). From 2.90 on creases are used only when the modifier's "use creases" bit (16) is set. The crease rules follow
**DeRose, Kass and Truong (1998)**: an edge with `s >= 1` uses the sharp rule (midpoint) and `0 < s < 1` blends the smooth
and the sharp edge point by `s`; a vertex takes the smooth, crease or corner rule by its number of sharp edges (fewer than
2, 2, more), and when that rule changes between this level and the next it blends the two by the vertex sharpness, the
mean of its creased edges' sharpnesses, clamped to 1 (boundary edges, which are infinitely sharp, are left out of that
mean); each half of a creased edge gets the paper's variable-sharpness (Chaikin) rule `max(0, (s_neighbour + 3 s) / 4 - 1)`,
where the neighbour is the other creased edge at that end (the edge itself when the end has not exactly two). Along a
uniform crease that is `s - 1` per level, the rule we had before; it differs only where creases of different sharpness
meet. (Before this change a vertex clamped each edge to 1 before averaging; the paper averages first: edges 2 and 0.5 give
a full crease, not a 0.75 blend. Both checked by hand in `blend_modifiers`.) Vertex weights: vertex points keep theirs,
edge and face points take the mean of their sources. Output is capped at 4 M subdivision faces per file (a level that would pass it is not done, named).

**Subdivision Surface options of Blender 2.8+.** Each is found by its field name in the file's own `SubsurfModifierData`
(so a file that does not have the field keeps the old behaviour). The SDNA stores struct and field names and types, but
**no enum or flag values**, so no file can confirm what the numbers mean; the values below are from memory of Blender's
DNA headers and are **unverified** (no Blender 2.8+ file with these options was available, and no Blender was run).

| Field (setting) | Values read | What we do |
|---|---|---|
| `uv_smooth` (UV Smooth) | 0 none, 1 keep corners, 2 keep corners + junctions, 3 + concave, 4 keep boundaries, 5 all | UVs are smoothed on their own topology (corners joined by vertex and UV, so a seam is a boundary); the mode says which UV vertices are held where they are: **corner** = on a UV boundary with one face around it; **junction** = its mesh vertex has 3 or more different UVs; **concave** = on a UV boundary with corner angles (in UV space) adding up to more than 180 degrees; **boundaries** = every UV-boundary vertex. 0 = linear UVs. Another value: smoothed everywhere, named in a note. Without the field (up to 2.7x): flag 8 "Subdivide UVs" = all, else linear, as before. These definitions are ours, from the mode names. |
| `boundary_smooth` (Boundary Smooth) | 0 all, 1 keep corners | 1: a boundary vertex with only one face around it is a corner and stays at every level (and at the limit). |
| `flags` bit 64 | set = "use limit surface" OFF | Read only when the file has the `quality` field (2.90+ DNA). Off (the default in Blender): after the last level every point moves to its **limit position**, with the masks of Halstead, Kass and DeRose (1993): smooth vertex of valence n `(n^2 S + 4 sum(edge neighbours) + sum(diagonal corners)) / (n (n + 5))`, crease or boundary `(a + 4 S + b) / 6`, corner stays; smoothed UVs too, with the mode's held vertices. A crease that is still only partly sharp after the last level blends the sharp and the smooth limit by its sharpness (an approximation; named here). |
| `flags` bit 16 | use creases | From 2.90 (or when `quality` is present). |
| `quality` | 1 to 6 | Read, no effect: in Blender it sets the accuracy of its limit evaluation on the GPU; our limit positions are exact for the masks above. |

Tested (`blend_modifiers`): the limit of every vertex is the same whether projected at level 1 or level 2 (cube, open grid
with and without keep corners, all-sharp cube; to 2e-6), the cube's face point goes to 68/81 = 0.839506 (worked by hand)
and plain subdivision approaches it about four times closer per level; each UV mode on fans of quads with one, two, three
and two-unequal UV islands (which centre UVs move and which stay); keep corners on grids. Three synthetic fixtures in the
2.9x / 3.6 / 4.x layouts (`blend_subsurf_293`, `_36`, `_41`, written by `scripts/gen_blend_fixtures.py`) carry the new
fields; the reader's mesh equals the same steps done by hand with the settings named in the script and differs from the
old defaults. They prove the reader reads the fields our script writes, **not** that Blender writes them that way.

How it is built: per level, every corner's edge is found by bucketing corners under their lower vertex index (a counting
sort, then a sort of each small bucket): flat arrays, no hash map, no allocation per element. On the corpus Suzanne at
level 4 (251,904 triangles) the whole read takes about 56 ms against Assimp's 270 ms (`docs/own_readers_speed.md`).

## Named, not silent

Everything below is reported once in `ImportedModel::notes` (also logged as a warning) when the file has it: lights, cameras and other object types (kept as empty nodes); modifiers other than Mirror and Subdivision Surface (one line per kind and name), modifiers on objects that are not meshes, modifiers switched off for the viewport, subdivision stopped at the face cap, mirror bisect, a mirror object that is not in the scene; constraints, track-to targets, pose-bone constraints (not evaluated); group / collection instances (not expanded); vertex colours (the engine mesh has no vertex colour channel); auto-smooth angle, custom split normals, sharp edges; shape keys; vertex parents; packed images (name only, pixels not extracted); node materials without a Principled BSDF; pre-2.5 Ipo animation; NLA tracks, drivers, F-Curve modifiers; easing interpolation (treated as linear); F-Curves on any other property; animation of materials / cameras / lights / shape keys; shear in a node transform; unit scale other than 1; meshes with truncated arrays; faces with out-of-range indices; computed world matrix differing from the saved one.

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
| Faces made by Subdivision Surface, whole file | 4 M (a level that would pass it is not done) |
| Modifiers followed per object | 1,000 |
| Whole frames sampled per animated node | 40,000 |
| Bone and collection depth, parent chain | 512, 64, 512 |
| Ear clipping | 256 corners, 300 M inner steps per file, then fan |
| Coordinates, keys | finite and within +-1e15 |

Counts are checked against the bytes in their block before anything is allocated; cycles in parent chains are cut and named; bone and collection recursion is iterative or depth-capped.

## What was and was not tested

**Real files** (28, from the `test/models/BLEND` and `test/models-nonbsd/BLEND` directories of an Assimp v5.4.3 checkout, `docs/model_corpus.md`): Blender 2.41, 2.48, 2.49, 2.50 / 2.52 (gzip), 2.60 (big endian, 32 bit), 2.62, 2.69, 2.71, 2.76, 2.79, 2.93; 64-bit little endian, plus one 32-bit big-endian file. **No real Blender 3.x or 4.x file was available.** The 3.6+/4.x attribute storage (`position`, `.corner_vert`, face offsets, `sharp_face`, float2 UV, numeric custom-data type ids 11, 45, 47, 48, 49, 50) is written from memory of Blender's DNA headers and is covered only by the **synthetic** fixtures, which were written by `scripts/gen_blend_fixtures.py` and not by Blender: they prove the code agrees with the script's reading of the layout, not with Blender.

- All 28 files read (Assimp: 27; it fails `BlenderDefault_262.blend`). Compared with Assimp built from the same sources (only its Blender importer): triangle position sets, UV sets, bounding boxes, areas and material colours match on 22 of the 28 files (every corner of ours has the same position and UV in Assimp's output; vertex counts differ because Assimp does not weld). The remaining six are the modifier / dropped-object differences below, plus the file Assimp cannot read. Diffuse colours, shininess and texture names were compared by eye on eight files with materials and agree (paths differ only in `//` resolution); `BlenderMaterial_269` differs in alpha (below).
- **Modifiers, against Assimp** (counts ours / Assimp's): `SuzanneSubdiv_252` (Subsurf level 4) 251,904 / 251,904 triangles; `yxa_1` (Subsurf, viewport 3) 115,712 / 115,712; `HUMAN` (Mirror X with merge, then Subsurf render level 2) 8,560 / 8,560. Bounding boxes equal to the printed 4 digits on all three. Vertex counts differ because these meshes are flat shaded (ours splits vertices per face normal, Assimp uses smooth normals; see below). On the Suzanne, 85 of 2,010 points at level 1 differ (up to 0.02): exactly the 42 vertices and 42 edge points on the open boundaries (the eye holes). Assimp keeps boundary vertices where they are and puts a boundary edge point at the mean of its two ends and its one face point; that is not the Catmull-Clark boundary rule, which ours follows (checked by writing both rules in a scratch script and matching Assimp's points). `MirroredCube_252` 108 / 36: the Mirror has all three axes set and a mirror object named `CenterOfTheWorld` at (0, 0.495, 0); ours makes the eight copies around that object's planes (12 + 96 triangles, box -5.1..5.1 by -4.95..4.95 by -5.76..4.77); Assimp adds one copy that stays inside the original box, which matches neither the three axes nor the mirror object. Before this change ours showed the stored meshes only (968, 1,808, 267 and 24 triangles).
- Other differences, all understood: Assimp **loses three of the ten cubes** of `CubeHierarchy_248` (all ten are on the visible layer; ours has all); Assimp uses the stored smooth vertex normal even on flat-shaded faces, ours honours the face flag (the default cube is flat); on smooth meshes our normals agree with Blender's own stored vertex normals on 100 % of corners for the simple meshes and 92 to 98 % on the two armature files (stored normals include auto-smooth / edge splits); Assimp reports diffuse alpha 1.0 for `BlenderMaterial_269`, whose alpha is 0.01 with the transparency bit set in `mode` (ours reports 0.01; the bit meanings are from memory of the DNA headers); Assimp has no armatures or animation at all.
- Ground truth from the files themselves: the saved world matrix of every object equals our computed one in 27 of 28 files (the exception is a camera aimed by an old-style track-to, now named); for `fleurOptonl.blend` all 71 pose bones equal Blender's own stored `pose_mat` to 1e-5; in `Bob.blend` 31 of 33 differ because two bones have constraints that we do not evaluate (named). `arm_head` equals the translation of `arm_mat` for all 104 bones.
- **Animation was not checked against any real animated file**: the corpus has one trivial one-key action and the armature files carry pre-2.5 Ipo animation (named, not read). F-Curve evaluation (bezier with flat handles, linear quaternion, whole-frame sampling, time base) is covered by the synthetic fixture only.
- `blend_reader` (engine linked): four encodings of the attribute fixture (4.1 names, 3.6 names, big endian + 32 bit, gzip) must give identical meshes; hand-computed world positions, concave polygon area, UV flip, materials, collections, orphan object excluded, skin weights, inverse binds, bezier values (1.04 at frame 3, 5.0 at frame 6), flatten, the seam, container errors.
- `blend_modifiers` (standalone, no engine library): Catmull-Clark on a cube at level 1 gives 24 quads and exactly the 26 hand-computed points (corners 5/9, edge points 3/4, face centres), 96 quads and 98 vertices at level 2, the face cap, simple subdivision, an open quad's boundary rule, creases (all sharp keeps the cube; sharpness 0.5 blends to 0.875), linear and seam-aware smooth UVs, Mirror merge (onto the plane) and no merge, three axes, a face in the plane, a mirror object, U flip, vertex group swap, weights through subdivision, the left / right name swap; the fixture `blend_modifiers.blend` (open box, Mirror X with merge and U flip, an Array that must be named, Subsurf level 1, one creased edge) through the reader against the same steps done by hand (80 triangles, every point equal); the four corpus files above when `OKAY_MODEL_CORPUS` is set; and a fuzz of 3,000 random meshes (repeated corners, non-manifold edges, loose vertices, 1e12 coordinates, random creases and weights) through Mirror and Subdivide. Clean under ASan + UBSan + float-cast-overflow.
- **Fixture drift (fixed 2026-11-16).** The script had stopped reproducing the four `blend_attr_*` files byte for byte. Cause:
  Python 3.12 made `sum()` of floats compensated (Neumaier), which changed one float of the Child object's saved matrix
  (`-1.2e-16` instead of `0.0`), and Python 3.13's `gzip.compress` writes OS byte 255 where the committed gzip file has 3.
  The script now adds matrix products left to right and writes the gzip header itself; it reproduces every committed
  fixture exactly again (the committed files did not change), and `--check` (ctest `blend_fixtures_check`) fails on drift
  (a gzip file whose deflate bytes differ only because of another zlib is accepted when it inflates to the same bytes).
- `blend_hostile` (standalone, no engine): every truncation of every fixture; bad headers; a block size of -1 and 2 GB; SDNA claiming billions of names or structs, bad type indices, arrays of 2^31 and 99999x99999 elements, mismatching struct sizes; an object whose parent is itself; a 40 MiB stored-block gzip; every block header field set to boundary values; pointer scrambling; then a deterministic mutation fuzz (byte flips, length stomps, inserts, deletes, inf / NaN / 2^31 / 2^63 floats). Built with `-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all`: clean, including 60,000 mutations of the fixtures and 25,000 mutations of ten real files (`OKAY_BLEND_FUZZ_ITERS`, `OKAY_BLEND_FUZZ_REAL_ITERS`), slowest single read 0.05 s. (One finding: a mutated bone matrix gave a non-finite inverse bind; fixed and named.) With the modifiers (2026-11-16) the fuzz also mutates `blend_modifiers.blend`, `MirroredCube_252` and `SuzanneSubdiv_252`; run under ASan + UBSan with 20,000 fixture and 3,600 real-file mutations: clean after two fixes it found (the modifier code met loops of faces the reader had dropped, now compacted first; and, in the 3.6+ face-offset path that predates the modifiers, an offset near 2^32 wrapped a 32-bit sum into a negative face start, now compared in 64 bits). Slowest single read under ASan 8.6 s (a mutated subdivision level), with the face cap then at 8 M; it is 4 M now. With the 2.9+ options (2026-11-16) the three `blend_subsurf_*` fixtures were added to the fuzz; ASan + UBSan, 20,000
fixture and 3,600 real-file mutations: clean, slowest single read 14.7 s under ASan on a shared, loaded machine. The
`blend_modifiers` fuzz now also draws the UV mode, keep corners, limit surface and use creases: 20,000 random meshes
under ASan + UBSan, clean.

## Not handled

Everything in "Named, not silent", plus: zstd and Blender 5.0 files; library-linked data from other .blend files; layer, visibility and hide flags (all objects of the scene are imported); curves, text, metaballs, lattices, point clouds, volumes, grease pencil; dupli / particle instancing; bone inherit-rotation / scale flags, B-Bone segments, IK; non-Principled shader nodes; texture coordinate nodes and mapping; images packed in the file; per-vertex delta rotations in non-Euler modes; multiple scenes (only the current one); sub-frame times; custom split normals; the separate render-only level choice of the Subdivision Surface (we always use the higher level), mesh creases on UVs
(UV smoothing ignores edge creases), OpenSubdiv's exact results in 2.8+ (ours is the textbook Catmull-Clark plus the rules
above, which they are meant to equal, unverified), edge creases stored in Blender 3.5 / 3.6 files (if they are kept in a
layer that is neither the `MEdge` struct nor the 4.x `crease_edge` attribute, they are not read; unverified which), modifiers applied to animated shapes; Blender versions after 4.x not seen (field renames not in the alias lists would show as a missing mesh and a note).

## Before it can become the default

The default stays off until: a real Blender 3.x and 4.x file has been read and compared (the largest gap), animated real files have been checked, the Windows distribution has run it, zstd is either supported or accepted as a documented limit, and the owner approves. After that `ModelImportAssimp.cpp` can lose its Blender role (`docs/own_code_roadmap.md`).
