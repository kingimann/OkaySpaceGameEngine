# Our own COB reader (`.cob`)

Status: **side by side, off by default** (`OKAY_OWN_COB`, slot 35 of the table in `ModelImport.cpp`). Assimp (when built) is the other reader. Nothing was flipped.

## What it is

COB is the Caligari trueSpace scene format, in a binary ("B") and an ASCII ("A") flavour with the same chunks. Header: 32 bytes `Caligari V00.01<A|B>LH`. Then chunks: type (4 letters), version, id, parent id, size, data. Binary data is little endian.

| File | What |
|---|---|
| `engine/src/Render/CobReader.cpp` | `okay::ReadCobBuffer` / `okay::ReadCobFile` |
| `engine/src/Render/ReaderCommon.hpp`, `PolygonTriangulate.hpp`, `TextReaderUtil.hpp`, `BinReader.hpp`, `SceneRefPath.hpp` | Shared mesh builder (welding, smoothing), triangulation, line reading, folder rule |
| `tests/test_cob_reader.cpp`, `tests/e3_fixtures.hpp` | The test (`cob_reader`) |
| `tests/test_own_dispatch_e3.cpp`, `tests/test_own_default_e3.cpp` | The seam and the build default |

## What it reads

- `Grou` (group) and `PolH` (polygon object): name (with the duplicate number), the stored world matrix (3 rows of 4 numbers), and for `PolH` the vertex list, the texture-vertex list and the faces (`<vertex, texture vertex>` corners, a material number). Each object is a node; its **stored world matrix is made relative to its parent's** (checked on the real molecule: the nitrogen atom and its three hydrogens only touch when the matrices are taken as world matrices).
- Faces: triangles, quads and larger polygons (convex fanned, concave ear clipped). One part per (object, material). Texture coordinates per corner, so seams are welded per (vertex, texture vertex).
- `Mat1`: material number, colour, alpha and the facet setting (`faceted` = flat, `smooth` = fully smooth, `auto N` = N degrees) which sets the smoothing of the part's normals; `ShBx`: the `file name` of a "texture map" shader, resolved inside the model's folder (a full path from the artist's machine is reduced to its file name; an absolute path or `..` is never followed).
- **Handedness**: trueSpace is left-handed and Z-up. y and z are swapped (a reflection: it turns left-handed into right-handed and Z-up into Y-up at once), and the winding is kept as written. On the real molecule file every triangle then faces outward. This is an inference from the data and from what I know of trueSpace, not a checked statement about it.
- Texture v is flipped (1 - v), assuming trueSpace's v runs up. **Unverified.**

## Not handled (lines in `out.notes`)

Animation channels (`Chan`: object keyframes, and the per-vertex ones in `dwarf.cob`), bones and characters, cameras, lights, splines, the unit setting, ambient / specular / exponent / refraction, transparency, reflectance and displacement shaders, texture repeat and offset, bitmaps (the thumbnail), faces with non-zero flags are read as plain polygons (hole flags are **not** interpreted: unverified), objects with a mirroring matrix (winding not corrected). Big-endian files (header `...HL`, if they exist) are refused; none was available.

## Limits

File 128 MiB. Per file: 100,000 objects, 2,000,000 vertices, 2,000,000 texture vertices, 1,000,000 faces, 4,000,000 face corners, **1,000,000 triangles**, 4,096 corners per face, 100,000 materials. Counts are checked against the bytes really there (binary) or the lines really there (ASCII), and against the budgets, before anything is sized from them.

## Tests and what was checked

- `cob_reader`: the same hand-built scene in ASCII and binary (a group and a polygon object, a quad and a triangle, two materials, a texture; node positions and scales, areas 1 and 0.5, the flat normal, a hand-computed texture coordinate, colours and alpha), CRLF line ends; refusals; binary counts at `0x7FFFFFFF`, negative and 65,535-corner faces; ASCII counts that lie; NaN and out-of-range numbers; cyclic, missing and self parents (ASCII and binary); texture paths (`../..`, absolute, nothing left after dropping the folder); truncation at every length of both files; 4,000 random byte flips of each; each budget; the worst file the budgets allow (1,000,000 triangles, 38 MB of ASCII): **peak 478 MB beyond the input, 3.4 s** (the test fails above 700 MB).
- **Real files** (the 8 `.cob` files of Assimp's test tree, not in this repository; read when `OKAY_E3_SAMPLES` names a folder with them): all 8 read. Binary and ASCII of the same model agree on parts, triangles and nodes: molecule 4 parts / 896 triangles / 6 nodes, spider 4 / 1,312 / 2 (56 degenerate triangles skipped in both), dwarf 2 / 1,896 / 3. The molecule's triangles all face outward. **Not verified**: how any of it looks in the renderer, the texture v flip, the left-handed assumption on a file with a clear front, hole flags, big-endian files, other trueSpace versions.

## Provenance

Written from the public description of the chunk structure and from my own reading of the 8 real files with a hex dump and a text viewer (that settled the byte layout of `Name`, the matrices, vertex and face lists, `Mat1` and `ShBx`, and which numbers mean what). No importer or engine source was read, copied or ported (rule 19a).
