# Our own SMD reader (`.smd`)

Status: **side by side, off by default.** Until now `.smd` (Valve "Studiomdl Data") only imported in a build with Assimp. Nothing was removed.

## What it is

Written from the public description of the format (the Valve Developer Community's SMD page) and from Valve's published math library for the Euler angle convention.

| File | What |
|---|---|
| `engine/src/Render/SmdReader.cpp` | `okay::ReadSmdBuffer` / `okay::ReadSmdFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 7 of the backend switch) |
| `scripts/gen_smd_fixtures.py` | Writes `tests/data/models/arm.smd`, `arm_crlf.smd`, `arm_anim.smd` (synthetic) |
| `tests/test_smd_reader.cpp` | Hand-computed checks, the seam, scene import, UTF-8 paths, locale (`smd_reader`) |
| `tests/test_smd_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`smd_hostile`) |

## Turning it on

`-DOKAY_OWN_SMD=ON`, environment variable `OKAY_OWN_SMD=1` / `0`, or `SetImportBackend(".smd", ImportBackend::Own)` (the run-time choice wins). Off by default; with Assimp built, Assimp stays the default `.smd` reader.

## What it reads

- `nodes`: bone id, name (quoted, may contain spaces), parent. Bones become nodes under a root node `SMD` that turns Z-up into Y-up (`(x, y, z) -> (x, z, -y)`). Parent loops and missing parents are cut loose to the root, repeated names get a suffix; every case is a note.
- `skeleton`: `time N` blocks of `bone x y z rx ry rz` (radians). The first (smallest) time is the bind pose; Euler angles follow Valve's `AngleQuaternion` (roll X, pitch Y, yaw Z, rotation = Rz * Ry * Rx). With more than one frame the file also gives one clip, `animation`, with a position and a rotation key per frame and bone; frame numbers become seconds at an assumed 30 frames per second (SMD stores none). Frames out of order are sorted, repeated ones merged; both are notes.
- `triangles`: a material line, then three vertices `parent x y z nx ny nz u v [links n (bone weight)*]`. One part per material (the material name is the texture file, prefixed with the model's folder when it has no path). Vertices are welded on position, normal, UV and skin. Up to four weights per vertex (the four strongest; a vertex with fewer than 1.0 of listed weight gives the rest to its own bone, as Assimp does). The part is skinned (`ImportedPart::bones`, inverse bind = inverse of the bone's bind pose) so the animation drives it. V is flipped (`v = 1 - v`), winding and units are kept as written (the real files' winding agrees with their normals).
- `vertexanimation` sections (flex / morph frames) and unknown sections are skipped with a note. `//` comments, a UTF-8 or UTF-16 byte order mark, `\n` / `\r\n` / `\r` line breaks are handled.
- `flatten` (`ImportModel`, one merged static mesh): the root's turn is baked into the vertices, skins and clips are dropped.

## Limits

| Limit | Value |
|---|---|
| File size | 512 MiB |
| Bones | 4,096 |
| Frames / bone poses over all frames | 100,000 / 20,000,000 |
| Triangles / materials | 5,000,000 / 4,096 |
| Links per vertex line | 64 (four kept) |
| Numbers | finite, within +-1e15 |

No recursion (bones are ordered with an explicit pass; a 4,000-deep chain reads). A bad number, a vertex line with too few values, a file that ends inside a triangle, a version other than 1, `nan` / `inf` / hex numbers fail the read with a one-line message that names the line.

## Not handled

- `.vta` (vertex animation files, which Assimp also lists) and `vertexanimation` sections.
- The frame rate is an assumption (30). Assimp's SMD reader also loads every other `.smd` in the same folder as animations; ours reads the one file it is given.
- Animation-only files give nodes and one clip, no mesh. Assimp builds a stick-figure mesh for them; ours does not.
- Rotation order follows Valve's published convention; no real file with rotated bones in an animation was available to confirm it end to end (the three real files carry only a bind pose).

## Tests and what was checked

- `smd_reader`: both line-ending variants, a bone name with a space, parent order, the Euler order (a roll of 90 degrees with a yaw of 90 degrees maps +Y to +Z), the Z-up turn, the welded vertex counts, weights (listed, implied, six links reduced to four), the inverse bind of a turned bone, UV flip, the animation file (frame times, keys), 14 kinds of malformed input, parent loops, repeated names, a `vertexanimation` section, German decimal-comma locale (skipped when none is installed), a UTF-8 path with characters outside the ANSI code page, the seam, a skinned scene import and an animation-only scene.
- `smd_hostile`: every truncation of every fixture, bone and material counts at and over the caps, a 4,000-bone chain, a 3,000-bone parent loop, 100,001 frames, NaN / infinite / overflowing numbers, link lists with weight zero, NUL bytes, UTF-16, 60,000 identical triangles, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 150,000 mutations.
- Real files: three from an outside corpus (`docs/model_corpus.md`): `triangle.smd`, `holy_grailref.smd` and `WusonSMD.smd`. Against Assimp built from source: triangle count and surface area agree on all three (area to 6 significant digits), and the bounding boxes agree once Assimp's Z-up is turned the way our root node turns it. Vertex counts differ because ours are welded (2,117 against Assimp's 11,196 unwelded on Wuson).

## Before it can become the default

Real files with an animation and with rotated bones; more than three real files; Windows run; owner approval. Removing Assimp drops the `.vta` extension and the stick-figure mesh for animation-only `.smd`.
