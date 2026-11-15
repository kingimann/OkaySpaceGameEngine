# Our own HMP reader (`.hmp`, version HMP7)

Status: **side by side, off by default** (`OKAY_OWN_HMP`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of HMP files. Nothing was flipped. **Only the HMP7 flavour is read. The layout was worked out from two real files, no written specification was available: read the "How sure" part.**

## What it is

HMP is the terrain model of 3D GameStudio (Conitec). The model is a **height grid**: a rectangle of grid points, each with a 16-bit height, plus optionally an embedded skin picture. It shares its first 84 bytes of header with the Quake / GameStudio MDL model header.

| File | What |
|---|---|
| `engine/src/Render/HmpReader.cpp` | `okay::ReadHmpBuffer` / `okay::ReadHmpFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp`, `ReaderCommon.hpp` | Shared pieces: the bounds-checked cursor, range check |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 29 (extension `.hmp`) |
| `tests/test_hmp_reader.cpp`, `tests/batche_fixtures.hpp` | Hand-computed checks on synthetic files (`hmp_reader`) |
| `tests/test_batche_hostile.cpp` | Mutation fuzz, the worst allowed file (time and memory), the real files (`batche_hostile`) |
| `tests/test_own_dispatch_e.cpp` | The import seam routes `.hmp` to this reader (`own_dispatch_e`) |

## The layout (HMP7), and how sure each part is

All numbers little endian. Header, 84 bytes:

| Offset | Field | How sure |
|---|---|---|
| 0 | `"HMP7"` | seen in both files |
| 4 | an integer, 0 in both files | not used |
| 8 | scale x, y, z (floats): `0.00244, 0.00244, 0.000793` | **z scale is certain** (see below); x and y are not used |
| 20 | origin x, y, z: `-80, -80, -34` | certain for z; x and y used as the grid's start (see below) |
| 32 | a float, 0 (a bounding radius in the MDL header it follows) | not used |
| 36 | cell size x and y (`5.0, 5.0`) and **grid points per row** as a float (`33.0`) | certain: `33 * 33 = 1089` = the vertex count, and `-80 + 32 * 5 = 80` |
| 48 | number of skins | certain: 0 in the plain file, 1 in the textured one |
| 52, 56 | skin width and height of the MDL header: 0 in both | not used |
| 60 | number of vertices (1089) | certain |
| 64 | number of triangles: 0 | not used |
| 68 | number of frames: 1 | read; only the first frame is read |
| 72 to 83 | three more MDL fields, all 0 | not used |

After the header: for each skin, `type` (4 bytes), `width`, `height` and `width * height * 3` bytes of pixels (**type 4 = 3 bytes per pixel**; the textured file has one skin of 400 x 501: 601,200 bytes, which makes the file size add up exactly). Then a **36-byte frame header** (not used: `02 00 00 00` and zeros in both files), then the grid points, **4 bytes each**: a 16-bit height and two signed bytes (a stored normal, which looks like `(0, +-64)` or `(-21, -21)`, in both files; **not read**, the normals are computed). Both files add up to the byte: `84 + skins + 36 + 4 * 1089`.

Position of grid point (row r, column c): `x = origin.x + c * cellX`, `y = origin.y + r * cellY`, `z = origin.z + height * scale.z`. With the sample file's numbers the heights run from -34 (height 0) to 18 (height 65535), and a height of 42,850 (the commonest value in the file, a flat level) is exactly 0, which is what a modeller's "ground level" would be; that is the evidence for the z formula. `x` and `y` run from -80 to 80, centred on the origin.

Mesh: one vertex per grid point, two triangles per cell, counter clockwise seen from above (+Z), smooth normals (area weighted sum of the face normals at each point), texture coordinates `u = c / (columns - 1)`, `v = 1 - r / (rows - 1)` (generated: the file has none). The root node turns Z-up into Y-up.

**Guessed, not verified:** which end of the rows is "up" in the skin picture (so `v` could be flipped), whether a row is a line of constant y (the sample is square, so the other reading cannot be told apart by size; the symmetric -80..80 range cannot tell either), and the meaning of the stored normal bytes.

## Not handled

Other versions (**HMP4, HMP5 and others are refused** with a message naming the version: their layouts are not known to me); skin types other than 4 (refused, because the pixel size is not known and the grid cannot be found past the skin); the embedded skin picture itself (skipped and noted: a material here names a texture file, it cannot carry pixels); the stored normals; more than one frame (the first is read, a note says so); terrain holes and water (if the format has any).

## Limits

File 128 MiB. At most **1,048,576 grid points** (a 1024 x 1024 grid, about 2.1 million triangles), 64 skins of at most 64 million pixels each. The row length must be a whole number of 2 or more that divides the point count, with at least two rows; the cell size must be a positive finite number; scale and origin must be finite and below 1e9. All counts are checked against the file's real size before anything is allocated. The worst allowed file (1024 x 1024, 4 MB) reads in under 0.1 s and holds about 55 MB at once (`batche_hostile` fails above 128 MB or 2 s).

## Tests and what was checked

- `hmp_reader` (528 checks): a synthetic 3 x 3 plane with hand-computed vertices (heights `z = 10 + c + 3r`), UVs, the plane's normal `(-1/3, -2/3, 2/3)` at every point, the triangle indices of the first and last cell, the area (`24 * sqrt(2.25)`), counter-clockwise winding, the root rotation; a 4 x 2 grid with free heights; a skin (skipped, noted); extra frames; more than 30 refusal cases (other versions, counts of `0xFFFFFFFF`, over the budget, not a multiple of the row length, a row length of NaN, infinity, 2.5 or 1, one row, bad skin counts and types, a skin of 2^62 pixels, a skin larger than the file, NaN and infinity in every float field, a zero or negative cell size); every truncation; every header byte set to 0x00, 0xFF and 0x80.
- `batche_hostile`: mutations of two fixtures, the worst allowed file and a grid one column over the budget, the two real files.
- `own_dispatch_e`: `.hmp` reaches this reader; HMP, MDC and CSM files under each other's extension are refused by the reader of that extension.
- **Mutation checks:** reading the z scale from the wrong field (21 failures), reversing the triangle winding (25), removing the row-length check (2).

### Real files

`terrain.hmp` (a plain 33 x 33 terrain) and `terrain_withtexture.hmp` (the same terrain with a 400 x 501 skin), from the `test/models/HMP` and `test/models-nonbsd/HMP` folders of an Assimp 5.4.3 checkout (`docs/model_corpus.md`; not in this repository). Read as data only. Both read: 1,089 vertices, 2,048 triangles, x and y from -80 to 80, z from -34 to 18; the textured one notes the skipped skin. The shape of the terrain was looked at as a picture next to its skin (a stylised scene); the picture did not settle the orientation question above. **The reader was not compared with Assimp's output.**

## Provenance (what this file was written from)

`HmpReader.cpp` was written from the byte-by-byte analysis of the two sample files above and from the layout they share with the Quake / GameStudio MDL header (counts, origin and scale as the model's coordinate transform). I found no written specification of the HMP7 header; where a field is called something in the table it is my reading of the data. No importer source (Assimp or any other project) was read, copied, ported or followed, and Assimp was not run on these files either.

Unverified: files from other GameStudio versions and other skins, the orientation and normals questions above, files larger than the sample, a run on real Windows.
