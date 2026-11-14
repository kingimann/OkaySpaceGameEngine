# Our own AC3D reader (`.ac` `.acc` `.ac3d`)

Status: **side by side, off by default** (`OKAY_OWN_AC3D`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of AC3D files. Nothing was flipped.

## What it is

AC3D is a small 3D modeller; its text format ("AC3Db") is the exchange format of FlightGear, many flight and racing simulators and a lot of hobby models. A file has a header line, `MATERIAL` lines and a tree of `OBJECT` blocks; a `poly` object holds vertices and surfaces (`SURF`) that point at them.

| File | What |
|---|---|
| `engine/src/Render/Ac3dReader.cpp` | `okay::ReadAc3dBuffer` / `okay::ReadAc3dFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/TextReaderUtil.hpp`, `ReaderCommon.hpp`, `PolygonTriangulate.hpp` | Shared pieces: UTF-16 / BOM text, line and token reading, number parsing, mesh builder, polygon triangulation |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 22 of the opt-in table (extensions `.ac`, `.acc`, `.ac3d`) |
| `tests/test_ac3d_reader.cpp`, `tests/batchd_fixtures.hpp` | Hand-computed checks on synthetic files (`ac3d_reader`) |
| `tests/test_batchd_hostile.cpp` | Hostile input and mutation fuzz (`batchd_hostile`) |

## What it reads

- **Text.** UTF-8, UTF-8 with a byte order mark and UTF-16 with a byte order mark (the files in the wild come in all three).
- **Objects.** `world`, `group`, `poly` and `light` objects as nodes (a synthetic root node, then the file's own tree), with `loc` as the node's position and `rot` (9 numbers, a 3x3 matrix read as rows, `p' = M p + loc`) as its rotation and scale (the scale from the column lengths; a shear is dropped and said so). `name` names the node and the part. The tree is walked with an explicit stack, 512 levels at most.
- **Vertices.** `numvert N` lines of `x y z`, and the extended files (`.acc`, written by newer AC3D versions) with `x y z nx ny nz`: the normals are kept.
- **Surfaces.** `SURF` flags (hex): type 0 polygon (any number of corners, triangulated: convex ones as a fan, concave ones ear-clipped), type 4 triangle strip (unrolled, every second triangle turned over), types 1 and 2 (closed and open lines) are skipped and counted. Bit `0x10` (shaded) asks for smooth normals made with the object's `crease` angle (45 degrees when the file gives none); flat surfaces keep face normals; the two-sided bit `0x20` has no counterpart in the engine's meshes and is ignored. One part per (object, material, shaded or flat).
- **Materials.** `MATERIAL "name" rgb ... emis ... shi ... trans ...`: `rgb` is the diffuse colour, `emis` the emissive colour, `shi` the shininess, `trans` becomes alpha (1 - trans). `amb` and `spec` are read and not used. A surface's `mat` picks one; the object's `texture` is joined to the model's folder and becomes the material's texture (one material per (colour set, texture) pair).
- **Texture coordinates.** Each `refs` line is `index u v` (the extended files add more pairs for the extra layers; only the first is read). `texrep` and `texoff` are applied as `u * rep + off`. An untextured object gets no coordinates (the files write zeros there).
- **Subdivision.** `subdiv N` is applied: **Catmull-Clark subdivision** (Catmull and Clark, 1978) of the object's polygons, N levels (at most 8, and it stops early before the object would pass 1,000,000 corners). Texture coordinates are subdivided inside each face so seams stay. Stored per-vertex normals belong to the control mesh and are replaced by smooth ones.

## Not handled (each is a line in `out.notes`)

Line surfaces (open and closed lines) and files that have **only** lines or no surfaces (the file is refused: the engine's meshes hold triangles), the extra texture layers of `.acc` files (`tiled`, `skids`, `shad`), lights (the node stays, the light does not), `url`, `data` blocks (skipped), unknown keywords (counted, first one named), surfaces with fewer than 3 corners, surfaces that point past the vertex list.

## Limits

File 256 MiB; per object 5,000,000 vertices, 5,000,000 surfaces, 1,000,000 corners per surface, 40,000,000 corners in all; 1,000,000 objects; 100,000 materials; nesting 512. Counts (`numvert`, `numsurf`, `refs`, `kids`) must be whole numbers within the caps and the text must really hold that many lines, or the file is refused before anything large is allocated. Numbers go through `NumParse.hpp` (finite, within 1e15, locale independent). Subdivision stops at 1,000,000 corners.

## Tests and what was checked

- `ac3d_reader`: **synthetic** file (two materials, a textured quad with `texrep` / `texoff` and a `loc`, a group with a 90 degree `rot` and a child triangle plus a line surface, a triangle strip with stored normals) with `\n` and `\r\n` line ends, as UTF-16 and with a byte order mark: parts, triangles, area 7.5 (by hand), node tree, the rotation quaternion, a vertex seen through the group's matrix, texture and uv numbers, colours, notes; the Catmull-Clark step on a cube (48 triangles, corners at 5/9, edge points at 0.75, face points at the face centres) at levels 0, 1 and 2; refusals (empty, wrong header, no object, bad counts and numbers, bad flags, an index past the vertices, lines only) and **every truncation**.
- `batchd_hostile` (AC3D section): counts at, past and far past the caps with tiny text, `nan`, `inf`, `1e999`, hex floats and overflowing numbers in `loc`, `rot`, `texrep`, vertices and materials, singular, mirrored and huge `rot` matrices, 5,000 levels of nesting, 20,000 objects and materials, a 3 MB number, a 60,002-corner strip, `subdiv 1000000`, then 3,000 mutations of the file (30,000 in the sanitizer run). Sanitizers (`address,undefined,float-cast-overflow`): see `docs/model_corpus.md`.

### Real files

Assimp's tree has 11 AC3D files with an extension (`models/AC`: 9 `.ac` and 2 `.acc`) and `TestFormatDetection` without one (not routed by extension). **Ours reads 8 of the 11 and refuses 3; Assimp reads 10 and fails on 1.** On the 7 files both read, the **mesh count, surface area and bounding box agree**, and so do the triangle counts of the 5 `.ac` files, including the subdivided ones: `SphereWithLight.ac` 17,664 triangles in both (area 0.0361 both), `SphereWithLightUvScaling4X.ac` 70,656 in both, `sample_subdiv.ac` 245,760 in both (area 12.9811 ours, 12.9756 Assimp: 0.04 percent apart, the one difference in the subdivision, whose cause is not known), `Wuson.ac` 3,732, the UTF-8 BOM file 264. Differences: ours drops zero-area triangles (the `.acc` strips contain degenerate ones: 264 against Assimp's 286 on `SphereWithLight.acc`, 3,732 against 4,633 on `Wuson.acc`, equal area and box); Assimp does not read the UTF-16 file (`SphereWithLight_UTF16LE.ac`: "magic sequence not found"), ours does and gets the same result as the UTF-8 twin; Assimp reads `openLine.ac`, `closedLine.ac` and `nosurfaces.ac` as meshes without triangles, ours refuses them (nothing to draw); Assimp ignores `texrep` and `texoff` (probed with a synthetic file), ours applies them; Assimp puts the object's `loc` into the vertices and the `rot` matrix on the node, ours keeps both on the node (the same when `rot` is the identity, which is every file in the tree).

Unverified: `rot` on a real file (none in the tree has one; the convention `p' = M p + loc` is the reading of the specification and of one synthetic probe, not a second reader's agreement on a real model), the `texrep` / `texoff` formula against the AC3D program itself, files with several `kids` levels from FlightGear, `crease` and normals on a real file, a run on real Windows.

## Provenance (what this file was written from)

`Ac3dReader.cpp` was written from scratch for this project from the public description of the AC3D file format (the header, `MATERIAL`, `OBJECT`, `SURF` and the keywords above, as documented by the AC3D program's authors) and from reading the sample files of Assimp's `test/models/AC` as data (never the Assimp source). Subdivision follows the paper by Catmull and Clark (1978). Assimp was used only as a **black box**: run on the test files and on small synthetic files made for this purpose, its output (counts, area, box, node matrices, texture coordinates) showed the `.acc` layout (one `index u v` row per corner and extra layers), that triangle strips are type 4, that `subdiv` is Catmull-Clark (the triangle counts are exactly 4^N times the polygon count) and how `rot` and `loc` are placed. No code, comment, table or test from any other importer was read, copied or ported.
