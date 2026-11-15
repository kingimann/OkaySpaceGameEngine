# Our own Terragen reader (`.ter`)

Status: **side by side, off by default** (`OKAY_OWN_TER`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of `.ter`. Nothing was flipped.

## What it is

A Terragen terrain file is a height field: a regular grid of 16-bit elevations with a few settings (grid size, metres per terrain unit, planet radius, curve mode). Terragen (Planetside Software) writes it, and many terrain tools export it.

**Where it was written from.** Planetside's published *Terragen Terrain file specification* (the wiki page "Terragen .TER Format") and nothing else: the 16-byte identifier, the chunks (`SIZE`, `XPTS`, `YPTS`, `SCAL`, `CRAD`, `CRVM`, `ALTW`, `EOF `), the elevation formula and the curve-mode-1 sphere are taken from that page. No importer source was read: not Assimp's, not GDAL's, and not the reference library the page links to (rule 19a). The comparison with Assimp below ran Assimp as a black box.

| File | What |
|---|---|
| `engine/src/Render/TerragenReader.cpp` | `okay::ReadTerragenBuffer` / `okay::ReadTerragenFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 27 of the opt-in table, run through `RunReaderGuarded` like every own reader (an allocation failure or other exception becomes a refusal) |
| `tests/test_terragen_reader.cpp`, `tests/terragen_test_build.hpp` | Hand-computed checks on files built in memory, and the seam (`terragen_reader`) |
| `tests/test_terragen_hostile.cpp` | Hostile input, mutation fuzz, time and memory limits; builds without the engine library (`terragen_hostile`) |

Switch it on like the other readers: `SetImportBackend(".ter", ImportBackend::Own)`, the environment variable `OKAY_OWN_TER=1` (or `0` to force it off), or `-DOKAY_OWN_TER=ON` at build time.

## What it reads

- **The chunks**, in any order (the specification asks readers to allow that), each 4-byte aligned and without a length field. `SIZE`, `XPTS` and `YPTS` must come before the elevation data (`ALTW`), as the specification requires.
- **Grid size.** `XPTS` by `YPTS` points when the file has them; otherwise `SIZE + 1` on each side (`SIZE` holds the number of points on the shortest side minus one). With only one of `XPTS` / `YPTS`, the other side is `SIZE + 1` (reported).
- **Elevations.** `ALTW` holds `HeightScale` and `BaseHeight` (signed 16-bit) and then the elevations row by row, first row `y = 0`, left to right. The altitude of a point in terrain units is `BaseHeight + Elevation * HeightScale / 65536`, as the specification gives it.
- **Scale.** `SCAL` is metres per terrain unit on x, y and z (30 when the file has none). The mesh is in **metres**: point `(i, j)` is at `i * xscale`, `j * yscale`, its altitude times `zscale`. Terragen itself needs the three to be equal; a file where they differ is scaled per axis and reported.
- **Curve mode** (`CRVM`). Mode 0 is flat. Mode 1 drapes the terrain over a sphere of radius `CRAD * 1000 / zscale` terrain units (`CRAD` in km, 6370 by default) whose top is at the middle of the grid. Following the specification, x and y are not moved; each point drops by the sphere's fall-off at its distance from the middle (`sqrt(r^2 - d^2) - r`). Other modes are undefined in the specification: the terrain is drawn flat and the mode is reported.
- **Axes.** Terragen is Z up; the engine is Y up. As for the other Z-up formats, `(x, y, z)` becomes `(x, z, -y)`. The corner `(0, 0)` stays at the origin.
- **The mesh.** One part with one vertex per grid point and two triangles per grid cell, wound so that they face up (+Y). Normals come straight from the height grid (central differences, one-sided at the edges), not from the engine's smooth-normal pass, which is far slower on millions of points. Texture coordinates run from 0 to 1 across the grid: `u` along x, `v` along y (first row `v = 0`). There is no material (the format has none).

## Reported, not read (each is a line in `out.notes`)

A missing `EOF ` marker (only old versions of Terragen need it), bytes after `EOF `, unknown data after the elevations, `SCAL` / `CRAD` / `CRVM` placed after the elevations (still applied), no padding before `EOF `, a `SIZE` that disagrees with `XPTS` / `YPTS` (`XPTS` and `YPTS` win), an unknown curve mode, a scale that differs per axis.

## Refused (an error, nothing imported)

A file that does not start with `TERRAGENTERRAIN `; no `SIZE` or no `ALTW`; `ALTW` before `SIZE`; `SIZE` / `XPTS` / `YPTS` after `ALTW`; two `ALTW` chunks; an unknown chunk before the elevation data (terrain chunks carry no length, so it cannot be skipped); fewer than 2 points on a side; a file that ends inside a chunk or inside the elevation data; a `SCAL` value outside 1e-6 to 1e6 metres per unit or not a number; a curved terrain (mode 1) with a bad `CRAD` or a sphere too small to hold the terrain.

## Limits

File 64 MiB; at most 4,097 points on a side and 2049 x 2049 points (about 4.2 million) in all. The largest grid takes about 270 MB while it is read: an independent review measured a 267 MB peak including the 8.4 MB file, and `terragen_hostile` holds it under 320 MB with a counting allocator.

Why 8.4 million triangles, when the XGL and Irrlicht readers stop at 4 million: a terrain's triangle count is fixed by the grid it stores (two per cell). Nothing in the format multiplies it: no references, no instancing, no subdivision. So the mesh can only be as big as the elevation data the file really contains (2 bytes per point; a file that claims a bigger grid than it holds is refused before anything is built). 2049 x 2049 is a common Terragen export size, and memory grows in step with the file, about 32 bytes of mesh per byte of file. The scale limits (1e-6 to 1e6) keep every slope, and so every normal, inside float range.

## Tests and what was checked

- `terragen_reader`: files built in memory (`tests/terragen_test_build.hpp`) with every number worked out by hand: positions of a 3 by 2 grid with `HeightScale` 16384 and `BaseHeight` 10, the Z-up to Y-up turn, texture coordinates, the winding (every triangle faces up, and the engine's own smooth normals agree with ours on a flat grid), the normals of planes tilted along x and along y, the 30 m default, `SIZE` alone, chunks in any order, odd point counts and their padding, non-square grids, a scale that differs per axis, curve mode 1 (the middle stays, a corner `sqrt(8)` units out drops by `100 - sqrt(9992)` units at radius 100), an undefined curve mode, every tolerated case above, 27 refusals with their messages, the largest grid (2049 by 2049), a UTF-8 path, and the seam (off by default, `OKAY_OWN_TER`, `SetImportBackend`, `ImportModelScene`, `ImportModel`, the one-static-mesh mode).
- `terragen_hostile`: every truncation of three seed files, sizes at and over every cap, extreme scale, radius and curve-mode values, 200,000 chunks before the data, one point over the grid cap, and a mutation fuzz (8,000 by default, `OKAY_READER_FUZZ_ITERS` for more). Every result is checked: refusals leave nothing behind, a read gives finite values, normals that face up, and indices inside the mesh. With `OKAY_MODEL_CORPUS` set, the two real files are fuzzed too. Run here under `address,undefined,float-cast-overflow` with 40,000 fuzz runs and the real files: clean. The run found one thing, which is fixed: a scale of 1e-30 on x and 1e6 on z gave slopes near 1e40, whose normals underflowed to zero in float; scales below 1e-6 are now refused.

### Real files

Assimp's tree has 2 Terragen files (`models/TER/RealisticTerrain.ter`, 257 x 257 points at 30 m, and `RealisticTerrain_Large.ter`, 513 x 513 points at 19.53125 m; both flat, curve mode 0). They are not stored in this repository; `docs/model_corpus.md` says how to point the tests at them. **Both read, with no notes, and equal Assimp v5.4.3** (built from the same sources with only its Terragen importer, run as a black box through its C API) **on mesh count, triangle count (131,072 and 524,288) and bounding box** once Assimp's Z-up is turned to our Y-up: x 0 to 7,680 m, height -78.99 to 874.53 m, y 0 to 7,680 m for the first; x and y 0 to 10,000 m, height -226.74 to 1,170.39 m for the second. **The surface area agrees to 2 parts in a million** (75,029,650 against Assimp's 75,029,777 m² and 138,372,471 against 138,373,075 m²). Splitting each cell along the other diagonal would give 75,054,167 m², a 3 in 10,000 difference, so both readers cut the cells the same way; the small rest is consistent with float rounding (Assimp's vectors are single precision). Differences that are by design: Assimp keeps four vertices per cell (262,144 and 1,048,576), ours shares one vertex per grid point (66,049 and 263,169); with the flags used Assimp gives no normals or texture coordinates, ours does.

Not verified: Terragen files from other writers (World Machine and other tools), curve mode 1 on a real file (neither real file uses it), a run on real Windows.
