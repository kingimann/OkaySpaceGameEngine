# Our own LWO reader (`.lwo`)

Status: **side by side, off by default** (`OKAY_OWN_LWO`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of `.lwo`.

## What it is

Written from the public LightWave object format descriptions (LWO2, and the older LWO1 chunks `LWOB`), with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/LwoReader.cpp`, `engine/include/okay/Render/LwoReader.hpp` | `okay::ReadLwoBuffer` / `okay::ReadLwoFile` |
| `engine/src/Render/ReaderCommon.hpp`, `PolygonTriangulate.hpp` | Shared number checks, smooth normals, ear clipping |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 15 of the batch-B table |
| `tests/test_lwo_reader.cpp` | Hand-computed checks on synthetic LWO2 and LWOB files and the seam (`lwo_reader`) |
| `tests/test_batchb_hostile.cpp` | Hostile input and mutation fuzz (`batchb_hostile`) |

## What it reads

Big-endian IFF chunks, every read bounds-checked; a chunk longer than the file is read as far as it goes.

- **LWO2** (`FORM LWO2`, and `LWO3`, which has the same chunks plus ones that are skipped, noted): `LAYR` (one node per layer; a parent layer that came first is the node's parent; hidden layers and pivots are reported), `PNTS`, `POLS` (`FACE` and `PTCH` as plain polygons, 4-byte `VX` indices; concave polygons are ear clipped), `TAGS` + `PTAG SURF` (a polygon's surface), `VMAP` / `VMAD` `TXUV` (per-point UVs and per-polygon-corner UVs; the surface's own UV map or the first one), `SURF` (`COLR` times `DIFF` as the diffuse colour, `LUMI` times the colour as emission, `GLOS` as a Phong exponent `2^(10*g + 2)` when `SPEC` is above 0, `1 - TRAN` as alpha, `SMAN` as the crease angle in radians (default 0 = flat), the first `BLOK` / `IMAP` colour image map (UV, planar, cylindrical, spherical, cubic or front projection: `PROJ`, `AXIS`, `WRPW` / `WRPH`) and its `CLIP` / `STIL` file name; `\` becomes `/`, a `Name:` content-directory prefix is dropped, the path is relative to the model file).
- **LWO1** (`FORM LWOB`): `PNTS`, `SRFS`, `POLS` (detail polygons after a negative surface number are skipped, noted), `SURF` (`COLR` bytes, `DIFF` / `LUMI` / `SPEC` / `TRAN` in units of 1/256, `GLOS` as the Phong exponent, `SMAN`). Image maps of LWOB are projected (`CTEX` names the projection, `TFLG` the main axis): the texture is kept and UVs are generated like LWO2 projections (below).
- **Projected image maps (LWO2 and LWOB).** Planar, cylindrical and spherical maps get UVs generated from the surface's vertices the way Assimp's `GenUVCoords` step does: planar maps the two axes other than the main one over the bounding box, cylindrical uses the angle round the main axis (from the box centre) and the position along it, spherical uses longitude and latitude from the box centre; U values near the seam are rounded to the side of their triangle; the wrap amounts (`WRPW` / `WRPH`) scale U and V of cylindrical and spherical maps. Not applied: the map's own centre, size and rotation (`TMAP`), as in Assimp. Cubic and front projections keep the texture but get no UVs (Assimp has no code for them either). The reader says how many maps got generated UVs.
- One part per (layer, surface). UV `v` is flipped like the other bottom-up formats. **Handedness**: LightWave is left-handed (+Z forward), the engine right-handed. The reader negates every point's Z and reverses each polygon's corner order, so a model is not mirrored and its faces still point out (the signed volume of every closed real file is positive; on a real file the Z range equals Assimp's, which does the same). Y-up needs no change. Other coordinates are kept as written.

## Not handled (each is also in `notes`)

Envelopes (animated surface values: the base value is used), shader, procedural and gradient texture blocks, texture channels other than colour and second colour maps, cubic and front projections (the texture is kept, no UVs), non-UV vertex maps (weights, colours, morphs), smoothing groups (`PTAG SMGP`; the surface angle is used), curves, bones and metaballs, subdivision patches (read as plain polygons, not subdivided), `LWLO`, layer pivots, `SURF` inheritance, other chunks (named). Smoothing happens within one surface of one layer only.

## Limits

File 512 MiB; chunks 5,000,000; layers 10,000; surfaces 100,000; points and polygons 20,000,000; triangles 20,000,000; UV values 40,000,000; strings 4,096 bytes; points finite within +-1e15.

## Tests and what was checked

- `lwo_reader` (`tests/test_lwo_reader.cpp`): **synthetic** LWO2 and LWOB files built in memory with a big-endian chunk writer: a cube with two surfaces (parts, colour times diffuse, luminosity, `2^(10*0.2+2) = 16` shininess, transparency as alpha, the smoothing angle welding the corners, the Z range `[0, 1]` coming out `[-1, 0]`, the signed volume 1), a textured surface (the `CLIP` path made relative, the `v` flip, a `VMAD` override of one corner), layers with a parent, hidden layers and pivots, `PTCH` and `CURV` blocks, a concave 6-point polygon (area 3), the 4-byte index form, a shader block, LWOB surfaces in 1/256 units, a projected LWOB image map, detail polygons, refusals (empty, `FORM` only, wrong form type, no polygons, NaN and 1e20 points, an index past the points, a truncated file), and the seam.
- `batchb_hostile` (LWO section): every truncation of an LWO2 and an LWOB file; a `FORM` and a chunk length of `0xFFFFFFFF`; a polygon claiming 1,023 corners without room; 200,000 polygons; a 4-byte index of 16.7 million; 10,001 layers, 100,001 surfaces and a million tags (refused or read in bounded time); strings without a terminator; UV maps with `NaN` and `1e30`; a `SURF` sub-chunk that overruns; NaN and huge surface values; LWOB with a negative surface and absurd detail counts; then the mutation fuzz over both forms. Sanitizers: clean (the runs found a `NaN` UV that reached the mesh, now 0, and, in the 150,000-mutation run, a `FORM` length below 4 that made the chunk loop start past its end, now refused).

### Real files

Assimp's tree has 54 `.lwo` files under `models` and `models-nonbsd` (LWO2 apart from 5 LWOB files and one empty file). **Ours reads 53; the 54th (`invalid/empty.lwo`, no bytes) fails in both readers. Assimp reads the same 53.** **29 agree on everything compared** (meshes, triangles, materials, surface area, bounding box), 25 differ:

- **16 differ only in `tex`** (Assimp lists a texture file, ours none): the `earth_*` files in `MappingModes` and `bluewithcylindrictexz.lwo` (LWOB); `UglyVertexColors.lwo` differs in `tex` and in triangles (ours 3180, Assimp 3202: the file's polygons give `sum(n - 2) = 3202`, Assimp keeps 22 zero-area triangles that ours drops). Their image maps use planar, cylindrical or spherical projection, which Assimp turns into UVs; ours does not apply them and says so in the notes (named warning, no UVs). This is the one real feature gap against Assimp on this corpus.
- **8 differ in triangles (and sometimes area)**, none in the bounding box: `concave_polygon.lwo` and the LWOB `ConcavePolygon.lwo` (64 triangles, area 0.2455, the polygon's exact area; Assimp drops two zero-area slivers, 58 and 0.2339: the same story as OBJ and COLLADA), `concave_self_intersecting.lwo` and `nonplanar_polygon.lwo` (an 18-corner non-planar polygon: ours 15 triangles and Assimp 11, no reference triangulation exists for such polygons), `box_2vc_1unused.lwo` (ours 419, Assimp 432: Assimp keeps 13 zero-area triangles, ours drops them), and `QuickDraw--Arm-Shoulder`, `Arm-Tip`, `CabinPortals` (ours 640, 2388 and 632; for the first, 640 is `sum(n - 2)` over the file's polygons, and Assimp drops 24 triangles; 4 of its polygons have repeated indices, which may be why; the other two were not examined).
- **Axes and handedness**: ours negates Z and reverses the polygons; the bounding boxes equal Assimp's on all 53 files, which is how this was settled (the first version kept the file's Z, mirrored every model, and showed on `rifle.lwo`, which is the same model as the `rifle.dxf` above).
- Notes our reader printed: 17 non-UV image maps, 13 files with shader, procedural or gradient blocks, 7 with non-UV vertex maps (vertex colours, weights), 3 with envelopes, 2 with layer pivots, 1 with subdivision patches.

Not verified on real files: LWOB surfaces other than the gloss ones (there are only five LWOB files: two spheres, a cylinder-mapped box, the concave polygon and one more), `VMAD` (the real files that have one were compared only by counts), multi-layer files with parents (the `hierarchy*.lwo` files agree on counts and box), LWO3 (no real file).
