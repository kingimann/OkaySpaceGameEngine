# Our own MD2 reader (`.md2`)

Status: **side by side, off by default** (`OKAY_OWN_MD2`, same rules as `docs/own_3mf_reader.md`: CMake option, environment variable, `SetImportBackend`). Assimp (when built) is still the only other reader of `.md2`. Nothing was flipped.

## What it is

MD2 is the model format of Quake II (1997). A file holds a list of vertex positions per animation frame, one list of triangles and one list of texture coordinates. Written from the public description of the format with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/Md2Reader.cpp` | `okay::ReadMd2Buffer` / `okay::ReadMd2File` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp` | Bounds-checked little-endian cursor, shared by the batch-C binary readers |
| `engine/src/Render/ReaderCommon.hpp` | Shared triangle soup to mesh (welding, smooth normals) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 17 of the opt-in table |
| `tests/test_md2_reader.cpp`, `tests/batchc_fixtures.hpp` | Hand-computed checks on a synthetic file (`md2_reader`) |
| `tests/test_batchc_hostile.cpp` | Hostile input and mutation fuzz (`batchc_hostile`) |

## What it reads

- The header (`IDP2`, version 8), the **first frame** (scale, offset and one byte per axis per vertex), the triangle list, the texture coordinates (pixels, divided by the skin size, top-down as written: the engine's Assimp path applies `FlipUVs` to Assimp's already flipped value, so the end result is the file's own, the same as the MD5 and OBJ readers give) and the name of the **first skin** as the texture.
- Quake winds triangles clockwise: the corner order is reversed. Quake is Z-up: the root node carries the turn to Y-up.
- Vertices are welded per position and texture coordinate; normals are **generated** (smooth).

## Not handled (each is a line in `out.notes`)

- **Frames after the first.** An MD2 animates by swapping vertex positions every frame (vertex animation). The engine's `ImportedModel` has skeletal clips only, so the other frames are not imported. This is the biggest gap: an animated Quake II character imports as its first pose. Assimp reads only the first frame as well (its importer does the same).
- The stored vertex normals (an index into Quake's built-in table of 162 directions): not read, because the table is not reproduced here. Smooth normals are made instead.
- Further skins, the OpenGL command list (the triangle list is used instead).

## Limits

File 256 MiB; vertices 1,000,000; triangles 2,000,000; texture coordinates 4,000,000. Every table offset and count is checked against the real file size before anything is allocated. Scale and offset must be finite and within 1e9. A triangle that names a missing vertex or texture coordinate is skipped and counted.

## Tests and what was checked

- `md2_reader`: **synthetic** file built in memory (4 vertices, scale (1,2,3), offset (10,20,30), 2 triangles; area 1 + 3 = 4 by hand): positions, welding, reversed winding and normal direction, texture coordinates as written, skin path with and without a base folder, the Z-up root, a multi-frame file (note), a missing skin, bad vertex and texture indices (skipped, noted), refusals (empty, wrong magic, wrong version, zero vertices, NaN scale), and **every truncation** of the file.
- `batchc_hostile` (MD2 section): every header field set to 0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, the caps and one past them; NaN, infinity and 3.4e38 in the scale and offset; 1e9 scale; every truncation; then 200,000 mutations (byte flips, insertions, deletions, number-like text, truncation). Run under `-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all`: clean.

### Real files

Assimp's tree has 5 MD2 files (`models/MD2`: `faerie`, `sydney`; `models-nonbsd/MD2`: `dolphin`, `flag`, `horse`). **All 5 read and equal Assimp v5.4.3 (built from the same sources, only the MD2 importer) on surface area (to 1e-7) and bounding box**, with the Z-up root applied to both. Triangle counts are equal on four; `horse.md2` has 683 to Assimp's 690 (ours drops 7 zero-area triangles, as the other readers do). Differences by design: Assimp always adds a texture (a dummy one when the file names none); ours has none; Assimp keeps three vertices per triangle (1,962 for `faerie`), ours welds (503).

Not verified: models with more than one skin, files from tools other than the Quake II toolchain, a run on real Windows.
