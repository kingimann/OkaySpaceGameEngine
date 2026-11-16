# Our own Quake III map reader (`.bsp`, `.pk3`)

Status: **side by side, off by default** (`OKAY_OWN_Q3BSP`, slot 42 of the table in `ModelImport.cpp`; same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of these files. Nothing was flipped.

## How to try it

Steps for the Windows release are in `docs/try_it.md`, section "Batch E6 readers". Short form: start the editor from a Command Prompt with `set OKAY_OWN_Q3BSP=1` (or build with `-DOKAY_OWN_Q3BSP=ON`), choose **File > Import Model...** and pick `examples\own_readers\tiny_arena.bsp` (or `tiny_arena.pk3`, the same map in a zip). You get a grey room 512 units wide with a bump in the floor and a small platform on its own node `model_1`. Our reader writes warning lines that start with `Q3BSP:` (for example `model import '...tiny_arena.bsp': Q3BSP: lightmaps, vertex colours, entities, brushes ... are not read ...`); Assimp does not write them, so with the variable unset (Assimp reads the file) there is no such line, and with it set there is.

## What it is

Quake III Arena maps are a "BSP" file (magic `IBSP`, version 46) of 17 lumps, normally shipped inside a `.pk3` (a zip) as `maps/<name>.bsp`. A map holds render geometry (polygon faces, "mesh" faces, Bezier patches, billboards), textures by name, lightmaps, the entity text, the BSP tree, visibility and collision brushes.

| File | What |
|---|---|
| `engine/src/Render/Q3BspReader.cpp` | `okay::ReadQ3bspFile` / `okay::ReadQ3bspBuffer` (declared in `ModelData.hpp`) |
| `engine/src/Render/ZipLite.hpp` | The zip reader (shared with 3MF / AMF; stored and deflated entries, zip-bomb caps) |
| `tests/test_q3bsp_reader.cpp`, `tests/q3bsp_fixtures.hpp` | Hand-computed checks on synthetic maps (`q3bsp_reader`) |
| `tests/test_q3bsp_hostile.cpp` | Budgets, hostile input, every aligned word at an extreme, mutation sweep, `.pk3` mutation sweep, zip bomb (`q3bsp_hostile`) |
| `tests/test_own_q3bsp_dispatch.cpp` | The seam routes `.bsp` and `.pk3`; `-DOKAY_OWN_Q3BSP=ON` really makes it the default (`own_q3bsp_dispatch`) |
| `tests/test_own_q3bsp_samples.cpp`, `scripts/gen_q3bsp_samples.py`, `examples/own_readers/tiny_arena.bsp` / `.pk3` | The shipped sample maps import through the seam (`own_q3bsp_samples`) |

## What it reads

* The 144 byte header (version 46 only; other versions are refused with their number), the lump directory (every lump must lie inside the file, every record lump must be a whole number of records).
* Textures (name, surface flags), vertexes (position, surface texture coordinate, normal), mesh vertexes, faces and models.
* **One part per (model, texture).** Model 0 (the world) hangs on the root node `Q3BSP`; every other model is a child node `model_N` (doors, lifts: their geometry is already in place in the file). A `.pk3` names the root node `Q3BSP maps/<name>.bsp`; the first map by name is read and a note says when there are more.
* Polygon (type 1) and mesh (type 3) faces: the triangles of their mesh-vertex list, **two corners swapped** (Quake III stores triangles clockwise; our MD3 reader does the same). Stored vertex normals are used; a zero normal is replaced by the one of the triangles around it. Texture coordinates are kept as written (top-down, the engine's Quake convention). Zero-area triangles are dropped.
* Patch (type 2) faces: the control grid `size[0] x size[1]` (odd, at least 3) is cut into 3 x 3 biquadratic Bezier patches, each tessellated into 6 x 6 quads (49 vertices, 72 triangles) with analytic normals and Bezier-interpolated texture coordinates.
* Materials: one per texture used, named by the texture name; in a `.pk3` the name gets the extension of the matching `.tga`, `.jpg`, `.jpeg` or `.png` entry when the archive has one. The pictures are **not extracted**: they are inside the zip.
* Faces whose texture has the nodraw, hint or skip surface flag are left out (counted in a note). Billboards are not read.
* Units stay Quake units. The root turns by -90 degrees about X (Z up to Y up), like the other Z-up readers; `flatten` bakes it in.

Not read (a note says so on every import): lightmaps and their coordinates, vertex colours, the entity text (so no spawn points or lights), brush and plane data, the BSP tree, visibility, effects (fog), shader scripts (so no blending, sky, animation).

## Budgets (a hostile file gets a refusal that names the budget)

File 512 MB; 4,000,000 triangles and 4,000,000 vertices built (a patch counts 72 triangles and 49 vertices); 8,000,000 face visits over all models (models may name the same face again); 100,000 parts; a patch grid at most 1025 points a side. Every count is tested against the bytes that are there before anything is allocated.

## What was checked

* **Synthetic maps** (`q3bsp_reader`, `q3bsp_fixtures.hpp`): every number is worked out by hand: the flat 3 x 3 patch with control points 0, 32, 64 is the identity parametrisation, so its area is exactly 64 x 64 and the vertex at u = 2/6, v = 3/6 is at (21.333, 32, 20) with texture coordinates (1/3, 1/2); a bent patch peaks at 16 x 1/2 above its middle row; the patch front side follows the stored normals (both ways) and the default is noted; models, bad indices, zero-area triangles, billboards, nodraw, version 47, wrong magic, lump outside the file, every truncation of the file (refused with a reason), `.pk3` stored and deflated, no map, two maps, a corrupt archive.
* **The one real map we have**, `maps/SGDTT3.bsp` inside `SGDTT3.pk3` in Assimp v5.4.3's test directory (`models-nonbsd/PK3`; not committed, see `docs/model_corpus.md`): numbers worked out from the raw bytes by a separate Python script: 17 textures, 2,084 vertexes, 336 faces (312 polygons, 24 meshes, no patch, no billboard), 28 models, 1,400 triangles, none of zero area, none skipped by flags. **All 1,400 triangles are stored clockwise against the stored vertex normals**, which is what the corner swap assumes (checked on every triangle). The reader reads the same 1,400 triangles. Compared with Assimp: **not run** (see `docs/model_corpus.md`, section Readers batch E6).
* **Hostile input** (`q3bsp_hostile`): budgets (1.5 million triangles named by 2 models read, by 3 refused; 8,002,000 face visits refused; an 801 x 801 patch grid refused, 201 x 201 read; extreme values in every field of the face record; NaN and infinity in the vertexes), a sweep that sets every aligned word of two seed maps to seven extreme values, 20,000 byte-level mutations per seed (set `OKAY_READER_FUZZ_ITERS` for more) and 1,000 mutations of the `.pk3`; each result is a sane model or a refusal that leaves nothing behind.

## Provenance (rule 19a)

Written from recalled knowledge of the public description of the format (the "Unofficial Quake 3 Map Specs": header, lump numbers, record sizes and field order of the textures, vertexes, mesh vertexes, faces and models lumps; the four face types; the rule that patches are 3 x 3 biquadratic Bezier surfaces) and from general knowledge of Bezier surfaces; the sample file's lumps were then read with our own script. No importer source of any other project was read, copied or ported; the Assimp checkout used for test data was a **sparse checkout of test files only** (no `code/` directory). The only fact taken from another reader of ours is the clockwise-storage rule, which the real map confirms.

## Known gaps and assumptions

* Patches: no real map with a patch was read, so the tessellation is checked only against the mathematics. **The front side of a patch is an assumption** when its control points carry no stored normals (noted on every import); with stored normals the side follows them.
* The level of the patch tessellation (6) is a choice, not the file's.
* Only version 46. RTCW and Quake Live variants (version 47 and others) are refused.
* No `.bsp` of another engine (Quake 1, Half-Life, Source) is read; they are refused with "IBSP is missing".
* Textures stay names; no shader script is read; nothing was run on Windows.
