# Our own DXF reader (`.dxf`)

Status: **side by side, off by default** (`OKAY_OWN_DXF`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of `.dxf`.

## What it is

Written from the public description of the DXF group-code text format, with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/DxfReader.cpp`, `engine/include/okay/Render/DxfReader.hpp` | `okay::ReadDxfBuffer` / `okay::ReadDxfFile` |
| `engine/src/Render/ReaderCommon.hpp`, `PolygonTriangulate.hpp` | Shared number parsing, smooth normals, ear clipping |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 14 of the batch-B table |
| `tests/test_dxf_reader.cpp` | Hand-computed checks on synthetic files and the seam (`dxf_reader`) |
| `tests/test_batchb_hostile.cpp` | Hostile input and mutation fuzz (`batchb_hostile`) |

## What it reads

ASCII DXF only (a binary DXF is refused with a message). The file is read as group code / value line pairs; code 999 comments are skipped; `\n` and `\r\n` line ends both work.

- **3DFACE**: triangles and quads (a fourth corner equal to the third is a triangle). Equal coordinates of different 3DFACEs are welded so smooth normals work across them.
- **POLYLINE** polyface meshes (flag 64: vertex records with flags 128 + 64 are points, records with flags 128 are faces of 3 or 4 vertex numbers, 1-based, a negative number is a hidden edge and counts as visible) and M x N polygon meshes (flag 16, closed in M with flag 1 and in N with flag 32). A `POLYLINE` that lost its `SEQEND` does not swallow the next entity. 2D and 3D line polylines are not surfaces.
- **MESH** (AC1021 and later): the vertex list (92 + coordinates) and the face list (93 + `n, i1 .. in` groups, polygons triangulated).
- **Blocks**: the `BLOCKS` section, placed by **INSERT** with the position, `41/42/43` scale and `50` rotation about Z (the block's base point is subtracted first); nested up to 8 deep, 100,000 inserts; content on layer `0` takes the INSERT's layer.
- One node and one part per **layer** (the layer name is the node name). Smooth normals with a 45 degree crease angle. **Axes**: DXF world coordinates are Z-up, so the root node carries the Z-up to Y-up rotation `(x, y, z) -> (x, z, -y)` (and `ImportModel`'s flattened path bakes it in): Assimp's DXF importer does the same, checked on the real files below. A DXF written by a Y-up tool (LightWave, for example) therefore comes out turned, exactly as with Assimp. No unit conversion (`$INSUNITS` is not read).

## Not handled (each is also counted by entity name in `notes`)

`LINE`, `POINT`, `CIRCLE`, `ARC`, `LWPOLYLINE`, 2D / 3D line `POLYLINE`, `TEXT`, `SPLINE`, `HATCH`, `DIMENSION`, `3DSOLID` / `BODY` / `REGION` (ACIS data), `VIEWPORT` and every other entity are skipped and listed; a file with no surface geometry fails with a message that names what it has. Also: colours (`62`, `420`: layers become nodes, there are no materials), `INSERT` arrays (only the first copy is placed) and extrusion directions other than +Z, tables (`LAYER` colours and `LTYPE`s), the header.

## Limits

File 512 MiB; entities, vertices and triangles 20,000,000; layers 10,000; blocks 100,000; INSERT entities 100,000, nested 8 deep, 200,000 block placements after nesting is expanded; coordinates finite within +-1e15 (also after INSERT transforms); a group code is at most 6 digits. Every number goes through `NumParse.hpp`.

## Tests and what was checked

- `dxf_reader` (`tests/test_dxf_reader.cpp`): **synthetic** ASCII DXF text: `3DFACE` triangles and quads on one and two layers (layer nodes), welding and smoothing across faces (a coplanar pair shares 4 vertices, a 90 degree fold stays at 6), CRLF line ends, a missing layer, polyface meshes (hidden-edge indices, a face naming a missing vertex, a polyline without `SEQEND`), 3 x 2 polygon meshes open and closed, `MESH` with a quad face and one that claims 1,000 vertices, blocks with a base point and an `INSERT` with scale and a 90 degree rotation computed by hand, mirrored and nested inserts (a block that inserts itself stops at the nesting limit), unknown blocks, arrays and extrusion directions (noted), the notes for `LINE`, `CIRCLE`, `TEXT` and colours, refusals (binary DXF, not DXF, a file that ends after a code, `abc`, `1e999`, `1e300`), a decimal-comma locale, the Z-up root with and without `flatten`, and the seam.
- `batchb_hostile` (DXF section): every truncation, bad numbers and counts (`1e999`, `-1`, `4000000000`, `20000001`, ...) in `3DFACE`, `INSERT`, `MESH` and `POLYLINE`, face indices out of range, a polyface of 50,000 vertices and 50,000 faces, `INSERT` bombs (20 levels of 10 inserts, 7 levels of 12: the 200,000 placement cap and the nesting limit stop them), 100,001 inserts, 5,000 layers, a 5 MB layer name, random binary, 2 MB of blank lines, then the mutation fuzz. Sanitizers: clean (this run found two things that are fixed: a `double` outside `int` range cast for a polyline flag, found by `float-cast-overflow`, and an `INSERT` expansion that was bounded only by the output size).

### Real files

Assimp's tree has 5 DXF files (four in `models/DXF`, `rifle.dxf` in `models-nonbsd`). **All 5 read, and Assimp reads the same 5. 4 agree on everything compared** (triangles, surface area, bounding box, materials): `issue_2229.dxf` (polyline meshes inside blocks, 2,058 vertex records), `lineTest.dxf` (a `3DFACE` pair plus a `LINE`; `999` comments before the first section), `wuson.dxf` (a polyface in a block placed by an `INSERT`, 6,937 vertex records), `rifle.dxf` (572 `3DFACE`s). The fifth, `PinkEggFromLW.dxf` (288 `3DFACE`s), has the same area and box, and ours has 528 triangles to Assimp's 576: the 48 extra are the zero-area triangles of the pole quads, which ours drops (the readers of OBJ, PLY and COLLADA do the same). **Axes**: Assimp turns every DXF Z-up to Y-up as `(x, y, z) -> (x, z, -y)`; ours does the same and the boxes are equal on all five (the file does not say which axis is up; both readers assume Z-up, so a Y-up model such as `PinkEggFromLW.dxf` comes out turned in both).

Not verified on real files: `MESH` (no real file has one; synthetic only), polygon meshes (flag 16), `INSERT` scale and rotation (one real insert, unscaled), colours, files from AutoCAD itself (the five come from other tools).
