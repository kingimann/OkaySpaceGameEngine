# Our own OFF reader (`.off`)

Status: **side by side, off by default.** Until now `.off` only imported in a build with Assimp. Nothing was removed. This is one of the "formats Assimp reads that we would otherwise drop" steps of `docs/own_code_roadmap.md`.

## What it is

Written from the public description of the Object File Format (the Geomview `off` manual page).

| File | What |
|---|---|
| `engine/src/Render/OffReader.cpp` | `okay::ReadOffBuffer` / `okay::ReadOffFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/TextReaderUtil.hpp` | shared by the text readers added with it: capped file read through a UTF-8 path, UTF-8 / UTF-16 text handling, line splitter, strict locale-independent numbers (`NumParse.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 6 of the backend switch); the one-static-mesh (`flatten`) step for the readers added after Collada / 3DS |
| `scripts/gen_off_fixtures.py` | Writes `tests/data/models/cube.off`, `cube_crlf.off`, `square_stnoff.off`, `mixed_coff.off` (synthetic) |
| `tests/test_off_reader.cpp` | Hand-computed checks, the seam, UTF-8 paths, locale (`off_reader`) |
| `tests/test_off_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`off_hostile`) |

## Turning it on

Same rules as the 3DS and BVH readers: `-DOKAY_OWN_OFF=ON`, environment variable `OKAY_OWN_OFF=1` / `0`, or `SetImportBackend(".off", ImportBackend::Own)` (the run-time choice wins). Off by default. With Assimp built, Assimp stays the default `.off` reader and ours is the choice when asked for.

## What it reads

- Header words `OFF`, `NOFF` (per-vertex normals), `STOFF` (per-vertex texture coordinates), `COFF` and the combinations (`STCNOFF`), any case. The counts (`vertices faces edges`) may follow on the header line; the edge count is not needed. `#` comments and blank lines are skipped; line breaks may be `\n`, `\r\n` or `\r`; a UTF-8 or UTF-16 byte order mark is handled.
- Vertices: `x y z`, then the normal, the colour and the texture coordinates in that order when the header announces them. A colour has 3 or 4 values.
- Faces: `n i0 ... in-1`, any number of corners (up to 65,536). Convex polygons are fanned, concave ones ear clipped (the same step the OBJ, PLY and COLLADA readers use). Anything after the indices (a face colour) is dropped with a note.
- One part, one root node. Without normals, smooth normals are generated. Units, axes, winding and texture coordinates are kept as written.

## Limits

| Limit | Value |
|---|---|
| File size | 512 MiB |
| Vertices / faces / output triangles | 10,000,000 / 10,000,000 / 30,000,000 |
| Corners per face | 65,536 |
| Coordinates | finite, within +-1e15 |

The vertex and face counts are checked against the bytes left in the file (every vertex or face needs at least 6 bytes) before anything is allocated, so `OutOfMemory.off` from Assimp's test files (which claims 353,535,235,358 vertices) is refused with a message instead of an out-of-memory kill. A vertex or face list that ends early fails the read.

## Named warnings (`ImportedModel::notes`, also logged)

Vertex colours not imported; face colours not imported; vertex lines with more values than the header announces; faces with fewer than 3 corners; faces with an out-of-range index; degenerate triangles; texture coordinates kept as written; UTF-16 converted.

## Not handled

- `4OFF` (homogeneous coordinates) and `nOFF` (n dimensions) fail the read with a message.
- Colours are not imported (the mesh type has no per-vertex colour).
- Vertex and face records must be one per line (the usual layout); a face split over lines is read as two broken records and fails.
- Not benchmarked beyond: `Wuson.off` (150 KB, 3,205 vertices) reads and builds a scene in about 5 ms.

## Tests and what was checked

- `off_reader`: both line-ending variants, comments, the header variants, normals and texture coordinates, a concave polygon, short and out-of-range faces, 22 kinds of malformed input, UTF-16, a German decimal-comma locale (skipped when none is installed), a UTF-8 path with characters outside the ANSI code page, the seam, scene import and the merged-mesh path.
- `off_hostile`: every truncation of every fixture, counts at and over the caps, 60,000-corner polygon, zig-zag concave polygon, NUL bytes, non-finite and overflowing numbers, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 400,000 mutations.
- Real files: two from an outside corpus (`docs/model_corpus.md`). Against Assimp built from source: `Cube.off` and `Wuson.off` agree on triangle count, area and bounding box. `invalid.off` (declares 4 faces, has fewer) is read by Assimp as 2 triangles of zero area and refused by ours; `OutOfMemory.off` kills Assimp's process and is refused by ours.

## Before it can become the default

Run on OFF files from more than one source (we have two); Windows run; owner approval. Removing Assimp drops nothing for `.off` once this reader is on.
