# Our own MS3D reader (`.ms3d`)

Status: **side by side, off by default.** Until now MilkShape 3D files only imported in a build with Assimp. Nothing was removed.

## What it is

Written from the public description of the binary format (Mete Ciragan's file format notes, versions 3 and 4).

| File | What |
|---|---|
| `engine/src/Render/Ms3dReader.cpp` | `okay::ReadMs3dBuffer` / `okay::ReadMs3dFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 9 of the backend switch) |
| `scripts/gen_ms3d_fixtures.py` | Writes `tests/data/models/rig.ms3d` (version 4) and `rig_v3.ms3d` (version 3), synthetic |
| `tests/test_ms3d_reader.cpp` | Hand-computed checks, the seam, scene import, UTF-8 paths (`ms3d_reader`) |
| `tests/test_ms3d_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`ms3d_hostile`) |

## Turning it on

`-DOKAY_OWN_MS3D=ON`, environment variable `OKAY_OWN_MS3D=1` / `0`, or `SetImportBackend(".ms3d", ImportBackend::Own)` (the run-time choice wins). Off by default; with Assimp built, Assimp stays the default `.ms3d` reader.

## What it reads

- Header `MS3D000000` and version 3 or 4 (Assimp reads only 4; a version 3 file is read with the version 4 layout and says so in a note). Little endian throughout, every read bounds-checked.
- Vertices, triangles (three vertex indices, **per-corner** normals and texture coordinates), groups (name, triangle list, material index), materials (diffuse and emissive colour, shininess, transparency, texture and alpha map names), joints (name, parent name, rest rotation as radians in X, Y, Z order `Rz * Ry * Rx`, rest position, rotation and translation keys).
- One part per group; vertices are welded on position, normal and texture coordinate. Texture coordinates and the winding are kept as written (Y-up like the engine; Assimp's net result is the same). Materials: diffuse colour, emissive colour, shininess; the transparency is put in the diffuse alpha (and noted); texture paths have `\` turned into `/` and a leading `.\` dropped, and are relative to the model's folder.
- Joints become nodes under a root `MS3D`; parents are found by name (the first joint of that name; repeats get a suffix and a note). Vertices are bound to their joint; when the version 4 extra block is present (comments, then the per-vertex extra bone ids and weights in percent, sub-versions 1 to 3) up to four joints with weights are kept. Vertices with no joint are tied to the root node, so the skin does not pull them to the origin. The inverse bind is the inverse of the joint's absolute rest transform.
- Animation: one clip, `animation`; times are the file's seconds; each key is applied on top of the joint's rest values (`rest * key` for rotation, `rest + key` for position), the way MilkShape and Assimp do. Keys at negative or non-finite times are skipped, out-of-order lists sorted, both with notes.
- `flatten` (`ImportModel`, one merged static mesh): skins and clips are dropped.

## Limits

| Limit | Value |
|---|---|
| File size | 256 MiB |
| Vertices, triangles, groups, materials, joints | 65,535 each (the format's own limit) |
| Keys over all joints | 20,000,000 |
| Output vertices | 3,145,680 |
| Numbers | finite, within +-1e15 |

Every record count is checked against the bytes left before anything is read; an index past its list skips the triangle or the reference with a note (never reads outside); no recursion. A wrong magic, a version other than 3 or 4, a truncated list, a non-finite position fail the read.

## Not handled

- Comments are skipped (counted in a note); ambient and specular colours, the material mode, smoothing groups (normals come per corner anyway), the alpha map are not imported.
- The extra bone weights are read as percent (0 to 100) per the format notes; Assimp divides them by 255. No real file with the extra block was available to settle which is right, so this is **unverified**.
- Joint colour, group flags, and the extra per-vertex data after the weights are not read.
- No real file with joints was available: the four real files have none, so the skeleton, weights and animation paths were checked on synthetic fixtures only.

## Tests and what was checked

- `ms3d_reader`: a version 4 and a version 3 file, group parts and welding, per-corner normals, UVs as written, material fields and the path fix, joint nodes with rest rotation, inverse bind, weights from the extra block (75 / 25 percent), a vertex tied to the root, key times and `rest * key` composition, 11 kinds of malformed input, every truncation, a NaN position, a UTF-8 path, the seam, scene import with skin and clip.
- `ms3d_hostile`: every truncation of both fixtures, counts claiming 65,535 records in a tiny file, index values at and over the ranges, NaN / infinite / huge coordinates, keys at negative, NaN and 1e30 times, 3,000 keys per joint, a 5,000-joint chain, 60,000 groups, comment and weight blocks with counts of billions, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 150,000 mutations.
- Real files: four from an outside corpus (`docs/model_corpus.md`): `Wuson.ms3d`, `jeep1.ms3d`, `twospheres.ms3d`, `twospheres_withmats.ms3d`. Against Assimp built from source: mesh count, triangle count, surface area and bounding box agree on all four. Vertex counts differ because ours are welded (178 against 720 on twospheres).

## Before it can become the default

A real file with joints, weights and animation; more than four real files; Windows run; owner approval. Removing Assimp drops nothing for `.ms3d` once this reader is on, except version-4-only quirks we did not meet.
