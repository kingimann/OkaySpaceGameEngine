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

File 128 MiB. Per file: 100,000 objects, 2,000,000 vertices, 2,000,000 texture vertices, 500,000 faces, 2,000,000 face corners, 1,000,000 chunks (binary and ASCII), at most 1,000 distinct texture names looked up on disk, **500,000 triangles**, 4,096 corners per face, 100,000 materials. Counts are checked against the bytes really there (binary) or the lines really there (ASCII), and against the budgets, before anything is sized from them.

## Tests and what was checked

- `cob_reader`: the same hand-built scene in ASCII and binary (a group and a polygon object, a quad and a triangle, two materials, a texture; node positions and scales, areas 1 and 0.5, the flat normal, a hand-computed texture coordinate, colours and alpha), CRLF line ends; refusals; binary counts at `0x7FFFFFFF`, negative and 65,535-corner faces; ASCII counts that lie; NaN and out-of-range numbers; cyclic, missing and self parents (ASCII and binary); texture paths (`../..`, absolute, nothing left after dropping the folder); truncation at every length of both files; 4,000 random byte flips of each; each budget; the worst file the budgets allow (500,000 triangles, 19 MB of ASCII): **peak 240 MB beyond the input, 2.0 s** (the test fails above 480 MB). Hostile work: 100,000 materials with 300,000 shader chunks (25 MB) now read in 0.3 s (the first version searched every material for every shader chunk: 108 s), binary the same in 0.05 s; 1,900 concave 256-corner faces in 0.3 s (ear clipping, which is cubic, is only used up to 48 corners, larger polygons are fanned). A zero-scale parent cannot be undone: the child keeps its world matrix and a note says so.
- **Real files** (the 8 `.cob` files of Assimp's test tree, not in this repository; read when `OKAY_E3_SAMPLES` names a folder with them): all 8 read. Binary and ASCII of the same model agree on parts, triangles and nodes: molecule 4 parts / 896 triangles / 6 nodes, spider 4 / 1,312 / 2 (56 degenerate triangles skipped in both), dwarf 2 / 1,896 / 3. The molecule's triangles all face outward. **Not verified**: how any of it looks in the renderer, the texture v flip, the left-handed assumption on a file with a clear front, hole flags, big-endian files, other trueSpace versions.

## Provenance

Where each fact came from (rule 19a). **From memory** (general knowledge of the format, not a document read during the work): that a COB file has a 32 byte header `Caligari V00.01<A|B>LH` and chunks named `Grou`, `PolH`, `Mat1`, `ShBx`, `Unit`, `Chan`, `END `, and the ASCII keywords (`Name`, `center`, `x axis`, `Transform`, `World Vertices`, `Texture Vertices`, `Faces`, `Face verts N flags F mat M`, `mat#`, `shader:`, `rgb`, `alpha ka ks exp ior`). **Settled by a hex dump and a text view of the 8 sample files** (and nothing else): the chunk header (type, version, id, parent id, size = 20 bytes), the object chunk layout (short duplicate count, short name length, name, 48 bytes of centre and axes, 48 bytes of 3x4 matrix, counts and arrays), the face record (1 byte flags, 2 bytes corner count, 2 bytes material, then 8 bytes per corner), `Mat1` (short material number, shader and facet characters, 1 byte facet angle, rgb and alpha, ka, ks, exp, ior as floats), `ShBx` (class byte, short-length name, parameter count, parameters with type codes 3 float, 6 bool, 7 colour, 10 string, flags; the parameter named `file name` holds the texture), and that the stored matrices are world matrices (the molecule's atoms touch only that way). The type code 4 (read as a 4 byte integer) and the hole meaning of face flags were **not** seen in any sample and are guesses. There is no document we can cite for the format. No importer source was read, copied or ported; the Assimp test tree was open in the scratchpad and only its data files were copied out.
