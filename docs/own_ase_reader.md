# Our own ASE reader (`.ase`)

Status: **side by side, off by default.** Until now 3ds Max ASCII Scene Export files only imported in a build with Assimp (and only with its 3DS importer also built: Assimp's ASE code is compiled out without it). Nothing was removed.

## What it is

Written from the public description of the format and from files 3ds Max wrote.

| File | What |
|---|---|
| `engine/src/Render/AseReader.cpp` | `okay::ReadAseBuffer` / `okay::ReadAseFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 10 of the backend switch) |
| `scripts/gen_ase_fixtures.py` | Writes `tests/data/models/scene.ase`, `scene_crlf.ase`, `scene_utf16le.ase` (synthetic) |
| `tests/test_ase_reader.cpp` | Hand-computed checks, the seam, scene import, UTF-8 paths, UTF-16, locale (`ase_reader`) |
| `tests/test_ase_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`ase_hostile`) |

## Turning it on

`-DOKAY_OWN_ASE=ON`, environment variable `OKAY_OWN_ASE=1` / `0`, or `SetImportBackend(".ase", ImportBackend::Own)` (the run-time choice wins). Off by default; with Assimp built, Assimp stays the default `.ase` reader.

## What it reads

- Text, one keyword per line, blocks in `{ }` tracked on an explicit stack (no recursion, depth 64). UTF-8, UTF-8 with a byte order mark, and UTF-16 with a byte order mark (little or big endian: both exist among Assimp's test files and Assimp reads neither correctly).
- `*3DSMAX_ASCIIEXPORT` header (required), `*SCENE_FRAMESPEED` and `*SCENE_TICKSPERFRAME`.
- Materials: name, diffuse colour, shininess (0..1 of 128), self illumination (as emission of the diffuse colour), transparency (into the diffuse alpha), the diffuse map's `*BITMAP` (a Windows drive path keeps only its file name; the rest is relative to the model's folder), sub-materials of a Multi/Sub-Object material by the face's `*MESH_MTLID` (ids past the count wrap, a note). A material is made when a face first uses it.
- `*GEOMOBJECT`: `*NODE_NAME`, `*NODE_PARENT`, `*NODE_TM` (`TM_ROW0..2` are the axes, `TM_ROW3` the position, in world space), `*MESH` with vertices (stored in world space: turned back into the node's local space), faces (`A: B: C:`, smoothing groups including `1,2,3` lists, `*MESH_MTLID`), texture vertices and faces (v flipped), and normals: `*MESH_NORMALS` per corner when the file has them, otherwise built from the smoothing groups. One part per object and material; vertices welded on position, normal and UV.
- The hierarchy as nodes under a root `ASE` that turns Z-up into Y-up; a node's transform is relative to its parent; helper, camera, light and shape objects are kept as plain empty nodes (their settings are not imported), so their animation still plays. A target camera's second `*NODE_TM` and its `.Target` animation block are ignored with a note.
- Animation: `*TM_ANIMATION` position, scale and rotation tracks as one clip, `animation` (times are ticks / (frame speed x ticks per frame)). Rotation samples are axis-angle, **relative to the previous sample** (files newer than version 110) and in 3ds Max's sign (the rotation is the inverse of the right-handed one): checked against the node's own `*TM_ROW` values and against Assimp's keys. TCB and Bezier keys are read as plain keys (tangents and tension are not used; a note). A track with a single key is not animation and is skipped (Assimp does the same).
- `flatten` (`ImportModel`, one merged static mesh): node transforms and the up turn are baked in, clips dropped.

## Limits

| Limit | Value |
|---|---|
| File size | 512 MiB |
| Block depth | 64 |
| Objects (nodes) / materials | 100,000 / 20,000 |
| Vertices + texture vertices / faces / animation keys | 10,000,000 each |
| Faces taking part in smoothing at one vertex | 256 (a note when exceeded) |
| Numbers | finite, within +-1e15 |

Counts are checked before allocating; every index is range-checked. A missing header, an unclosed block, a bad number in a vertex or transform, a vertex or face index outside its list fail the read, naming the line (a face whose vertex index is outside the vertices is skipped with a note instead).

## Not handled

- Cameras, lights, shapes and splines as objects; vertex colours; extra texture mapping channels; opacity, bump, specular, reflection maps; texture tiling, offset and rotation (counted in a note); ambient and specular colours; the shading mode.
- Skin data (`*MESH_SOFTSKINVERTS` / `*MESH_BONE_LIST` / `*MESH_BONE_VERTEX`, which Assimp reads into bones and weights) is not read; a mesh with it imports as a rigid mesh. None of the 16 real files has any, so this difference is unmeasured.
- Non-uniform scale under a rotated parent has shear that a transform cannot hold: the shear is dropped and counted in a note.
- Smoothing group numbers above 32 are folded into 1..32 (a note).

## Tests and what was checked

- `ase_reader`: the UTF-8, CRLF and UTF-16 versions of one scene, world-to-local vertices, a child under a parent, sub-materials chosen per face, material fields and the path fix, UV flip, smoothing, normals from the file, the animation (ticks, relative rotation in 3ds Max's sign, the target camera's block ignored), every note, malformed input, parent loops, missing parents, repeated names, German decimal-comma locale (skipped when none is installed), a UTF-8 path, the seam, scene import and the merged-mesh path.
- `ase_hostile`: every truncation of all three fixtures, non-finite / overflowing / singular / mirrored / sheared transforms, counts at and over the caps, 100,000 nested blocks, 20,000 objects in a chain, a 5,000-object parent loop, a vertex shared by 3,000 faces with varying smoothing groups, sub-material ids out of range, keys out of order / at negative times / NaN / 200,000 in a track, UTF-16 with a lone surrogate, binary junk, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 150,000 mutations.
- Real files: 16 from an outside corpus (`docs/model_corpus.md`; 14 plain, 2 UTF-16, plus one empty file both refuse). Against Assimp built from source (with its 3DS importer, which its ASE code needs): triangle count, surface area, bounding box and clip duration agree on the 14 UTF-8 files, including `MotionCaptureROM.ase` (26 meshes, 2,016 triangles, 45.167 s, 23 animated nodes) and the camera animation files; rotation keys at first and last sample equal to four decimals on the three files compared (`RotatingCube`, `anim2`, `CameraRollAnimWithChildObject`). On the two UTF-16 files Assimp returns an empty skeleton (1 mesh, 8 triangles, area 0); ours reads the full scene, the same as the UTF-8 original. Assimp makes a `.Target` node and channel for target cameras; ours does not.

## Before it can become the default

3ds Max exports from more than one version; Windows run; owner approval. Removing Assimp drops ASE skin data (not read here) and Assimp's camera and light objects (here plain empty nodes).
