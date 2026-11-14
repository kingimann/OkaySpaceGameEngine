# Our own MD5 reader (`.md5mesh`, `.md5anim`)

Status: **side by side, off by default.** Until now the id Tech 4 (Doom 3) model format only imported in a build with Assimp. Nothing was removed.

## What it is

Written from the public description of the format.

| File | What |
|---|---|
| `engine/src/Render/Md5Reader.cpp` | `okay::ReadMd5Buffer(meshText, animText, baseDir, ...)` / `okay::ReadMd5File` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 8 of the backend switch; `.md5mesh` and `.md5anim` share it) |
| `scripts/gen_md5_fixtures.py` | Writes `tests/data/models/skel.md5mesh` + `skel.md5anim` and CRLF copies (synthetic) |
| `tests/test_md5_reader.cpp` | Hand-computed checks, the seam, scene import, UTF-8 paths, locale (`md5_reader`) |
| `tests/test_md5_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`md5_hostile`) |

## Turning it on

`-DOKAY_OWN_MD5=ON`, environment variable `OKAY_OWN_MD5=1` / `0`, or `SetImportBackend(".md5mesh", ImportBackend::Own)` (the run-time choice wins; one switch covers both extensions). Off by default; with Assimp built, Assimp stays the default reader.

## What it reads

- **md5mesh**: `MD5Version 10`, `numJoints`, `numMeshes`, `joints` (name, parent, model-space position, orientation as x y z with w computed), and the `mesh` blocks (`shader`, `numverts` / `vert`, `numtris` / `tri`, `numweights` / `weight`). Joints become nodes under a root `MD5` that turns Z-up into Y-up; a joint's local transform is worked out from the model-space ones. Each mesh is one skinned part: the vertex positions are computed at the bind pose from the weights (the biases are normalised), the four strongest joints per vertex are kept, the inverse bind is the inverse of the joint's model-space transform. Triangle winding is reversed (id Tech 4 is clockwise). Smooth normals are generated (the file has none). Texture coordinates are kept as written (the file's origin is top left, like the engine's).
- **md5anim**: `numFrames`, `numJoints`, `frameRate`, `numAnimatedComponents`, `hierarchy` (name, parent, flags, start index), `bounds` (skipped), `baseframe` and `frame` blocks. Flag bits 1, 2, 4 select the position components that vary, 8, 16, 32 the orientation components; the rest come from the base frame. One clip, `animation`, with a position and a rotation key per frame and joint at `frame / frameRate` seconds. Values are relative to the parent joint, as the nodes are.
- `ReadMd5File` on an `.md5mesh` also reads the `.md5anim` with the same name next to it when there is one; a broken animation next to a good mesh is a note, the mesh is still imported. On an `.md5anim` alone it builds the nodes from the animation's hierarchy and base frame (no mesh). A joint in the animation that is not in the mesh drops its keys with a note.
- A `shader` that is an image file (`.tga`, `.png`, `.jpg`, `.bmp`, `.dds`) sets the texture; any other shader is a Doom 3 material declaration, not an image: the material gets that name and **no texture is guessed** (Assimp guesses `_d.tga`, `_local.tga`, `_s.tga`, `_h.tga` for these). Meshes without vertices or triangles are skipped with a note. `//` comments, a byte order mark and any line break style are handled.
- `flatten` (`ImportModel`, one merged static mesh): the root's turn is baked in, skins and clips are dropped.

## Limits

| Limit | Value |
|---|---|
| File size | 512 MiB each |
| Joints / meshes | 4,096 / 4,096 |
| Vertices / triangles / weights over all meshes | 10,000,000 / 20,000,000 / 40,000,000 |
| Frames / frames x joints / frames x components | 100,000 / 8,000,000 / 48,000,000 |
| Numbers | finite, within +-1e15 |

Counts are checked against the bytes left in the file before anything is allocated; no recursion (joints are ordered by an explicit pass, a 4,000-deep chain reads, parent loops are cut). A `MD5Version` other than 10, a joint flag that needs more components than the frame holds, a bad number, a weight that names a missing joint fail the read with a message naming the line.

## Not handled

- `.md5camera`.
- The mesh's `numMeshes` is only checked (a mismatch is a note).
- Vertices whose weights sum to zero sit at the origin (note). No texture guessing for shader names.

## Tests and what was checked

- `md5_reader`: both line-ending variants, the Z-up turn, joint local transforms from model-space ones, vertex positions from weights worked out by hand, winding, joint weights, inverse bind, materials and the shader note, an empty mesh, the animation (frame times, flags, base frame, -45 and -90 degree keys), a mesh alone, an animation alone, joints that are not in the mesh, a broken animation next to a good mesh, six joints on one vertex reduced to four, 24 kinds of malformed input, German decimal-comma locale (skipped when none is installed), a UTF-8 path with the animation found next to the mesh, the seam, scene import with clip.
- `md5_hostile`: every truncation of the mesh and of the animation (alone and paired), counts at and over every cap, a 4,000-joint chain, a 3,000-joint loop, 2,000 frames x 30 joints, NaN / infinite / overflowing numbers, an orientation with x^2+y^2+z^2 over 1, NUL bytes, UTF-16, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 150,000 mutations.
- Real files: `SimpleCube.md5mesh`, `Bob.md5mesh` + `Bob.md5anim` and `BoarMan.md5mesh` from an outside corpus (`docs/model_corpus.md`). Against Assimp built from source: triangle count, surface area, bounding box (Assimp also turns Z-up the same way) and clip duration (5.792 s, 140 frames at 24) agree on all three; Bob's 33 joint tracks (140 position and rotation keys each) agree with Assimp's on first and last key to four decimals except for two sign-of-zero / last-digit differences (`-0.0000` against `0.0000`, `0.5041` against `0.5042`). `BoarMan` has 13 empty meshes, ours says so. Vertex counts differ because ours are welded (875 against 3,081 on Bob).

## Before it can become the default

More than three real files (the animation was only checked on one skeleton); Windows run; owner approval. Removing Assimp drops `.md5camera` and the stick-figure mesh for an animation read on its own.
