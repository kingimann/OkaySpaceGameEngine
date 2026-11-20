# Our own IQM reader (`.iqm`)

Status: **side by side, off by default** (`OKAY_OWN_IQM`, slot 34 of the table in `ModelImport.cpp`; same rules as `docs/content/model_readers/own_3mf_reader.md`). Assimp (when built) is still the other reader of IQM files. Nothing was flipped.

## What it is

IQM, the Inter-Quake Model, is a compact binary format (Lee Salzman) for skinned, animated models, used by Cube 2 / Sauerbraten and other open engines. One file holds the meshes, the skeleton and the animations.

| File | What |
|---|---|
| `engine/src/Render/IqmReader.cpp` | `okay::ReadIqmBuffer` / `okay::ReadIqmFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp`, `TextReaderUtil.hpp`, `SceneRefPath.hpp` | Shared: bounds-checked cursor, file read, parent ordering, folder rule for textures |
| `tests/test_iqm_reader.cpp`, `tests/e3_fixtures.hpp` | The test (`iqm_reader`), hostile input, budgets, memory, real file |
| `tests/test_own_dispatch_e3.cpp`, `tests/test_own_default_e3.cpp` | The seam and the build default |

## What it reads (version 2)

- Meshes: one skinned part per mesh. Positions, normals, the first texture-coordinate set, four joint indices and weights per vertex (any integer or float format the vertex-array record names; byte and short weights are normalised, then each vertex's weights are divided by their sum). Smooth normals are made when the file has none. Triangles use the file's own order (counter clockwise).
- Skeleton: the joint table (translate, rotation quaternion, scale) as nodes under a root node `IQM` that turns Z-up into Y-up; parents first (a parent loop, a self parent or a parent index out of range is cut and noted). Inverse bind matrices come from the joints' rest pose.
- Materials: the mesh's material name; a name that ends in an image extension is also the texture, resolved with `ResolveInsideDir` inside the model's folder (an absolute path or a `..` that climbs out is refused and noted).
- Animation: each animation is one clip. The pose records' channel masks and offsets / scales and the 16-bit frame data give a translation, rotation and scale per joint and frame, at the animation's frame rate (a bad rate becomes 30). A channel group the mask does not animate is constant: it gets keys (first and last frame) only if it differs from the joint's rest value. Quaternions are normalised and kept on one hemisphere.

## Not handled (lines in `out.notes`)

Version 1 files (Euler angles; refused with a message), tangents, colours, custom vertex arrays, adjacency, bounds, comment text, extensions, the loop flag of an animation (the engine's clip type has none), frame data without an animation entry. Texture coordinates and winding are kept as written. **Whether IQM's v axis needs a flip was not settled**: the sample file's textures cannot show it automatically, so the coordinates are as in the file.

## Limits

File 128 MiB. Per file: 4,000,000 vertices and 4,000,000 triangles (counted over all meshes, since two meshes may name the same range), 10,000 joints / poses / meshes / animations, 64 vertex arrays, **500,000 joint frames** (frames times poses, over all animations; each costs up to three keys, about 52 bytes). **File-wide output budget**: the vertices and the triangles are each counted over all meshes at most 1,000,000 (a range named by several meshes counts every time, so a 240 KB file cannot ask for 4,000,000 output vertices), meshes times joints at most 100,000 and meshes times the joint-name bytes at most 16 MiB (every part carries a copy of the whole bone list). Every table is checked to lie inside the file, with 64-bit arithmetic, before anything is sized from its count. Names are cut at 256 characters.

## Tests and what was checked

- `iqm_reader`: a hand-built file (one quad, two joints, one 3-frame animation with hand-computed keys: translate x 0, 1, 2, scale x 1, 2, 3, times 0, 0.1, 0.2), skin weights 128/255 and 127/255, inverse bind points, material and texture; a file without normals or blend arrays; cyclic, out-of-range and self parents; each refusal with its reason; texture paths; every header field set to `0xFFFFFFFF`, `0x7FFFFFFF`, 0x80000000 and other extremes (140 files: 118 refused, 22 read and sane); truncation at every length; 4,000 random byte flips (set `OKAY_BATCHD_FUZZ_ITERS` for more); every budget refused just over and read just under; the worst file the budgets allow (500,000 joint frames, every channel animated, 9 MB): **peak 24 MB beyond the input, 0.07 s**, measured with a counting `operator new` (the test caps it at 50 MB). Also: 10,000 meshes of 400 vertices reusing one range (239 KB file) are refused with a peak of under 1 MB (the old per-mesh budget let it through at about 100 MB); the largest allowed such file (2,500 meshes, 1,000,000 output vertices) reads with a peak of 24 MB. A joint with zero scale cannot be inverted: its inverse bind is the identity and a note says so (tested).
- **A real file** (`mrfixit.iqm`, in the Assimp tree, which is not in this repository; the test reads it when `OKAY_E3_SAMPLES` names a folder that has it): header numbers match (1,861 vertices, 2,988 triangles, 75 joints, 2 meshes, 101 frames), all skin weights sum to one, and a separate check of the frame packing (all 75 x 101 decoded rotation quaternions have length 1 within 3e-5, which they would not if the channel order were wrong). **Not verified**: how it looks in the renderer, the v flip, other exporters' files.

## Provenance

Written from **my memory of the IQM specification text** (`iqm.txt`; it was not fetched or re-read during the work: header, `iqmmesh`, `iqmtriangle`, `iqmjoint`, `iqmpose`, `iqmanim`, `iqmvertexarray`, the vertex-array types and formats, and the rule that frame data holds one 16-bit value per set mask bit, `offset + value * scale`), and every layout detail was then checked against a hex dump of the one real file (`mrfixit.iqm`: header numbers, record sizes, the quaternion-length check). Where memory and the file could disagree, the file decided. No importer or engine source was read, copied or ported (rule 19a).
