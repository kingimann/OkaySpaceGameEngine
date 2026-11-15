# Our own MDC reader (`.mdc`)

Status: **side by side, off by default** (`OKAY_OWN_MDC`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of MDC files. Nothing was flipped. **Checked on one real file (field by field, not against Assimp), never on real Windows.**

## What it is

MDC is the model format of Return to Castle Wolfenstein (id Tech 3). It is MD3 (`docs/own_md3_reader.md`) with "compressed" frames: a few full-precision **base frames** and, for the other frames, one small offset per vertex from a base frame. Like MD3 it is a vertex-animation format: every frame is a full set of vertex positions, there are no bones. The engine has no vertex-animation clip, so **only the first frame is read**, as a static mesh (the same choice as MD2 and MD3).

| File | What |
|---|---|
| `engine/src/Render/MdcReader.cpp` | `okay::ReadMdcBuffer` / `okay::ReadMdcFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp`, `ReaderCommon.hpp` | Shared pieces: the bounds-checked cursor, the mesh builder |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 28 (extension `.mdc`) |
| `tests/test_mdc_reader.cpp`, `tests/batche_fixtures.hpp` | Hand-computed checks on synthetic files (`mdc_reader`) |
| `tests/test_batche_hostile.cpp` | Mutation fuzz, the worst allowed file (time and memory), the real file (`batche_hostile`) |
| `tests/test_own_dispatch_e.cpp` | The import seam routes `.mdc` to this reader (`own_dispatch_e`) |

## What it reads

- **Header** (112 bytes): `IDPC`, version 2 (only 2), a 64-byte name (the root node's name), flags, the counts of frames, tags, surfaces and skins, and five offsets.
- **Surfaces** (one part each, header 124 bytes): name, the counts (compressed frames, base frames, shaders, vertices, triangles) and eight offsets, relative to the surface. The surface's own 4-byte `ident` field is **not checked**: the real file has 7 there, not a magic text.
- **Vertices of frame 0:** the frame table of the surface names, for frame 0, one base frame and optionally one compressed frame (`-1` = none). Base-frame vertices are `x y z` as signed 16-bit numbers in 1/64 units and a packed normal (latitude and longitude as two bytes, 1/256 of a turn, same packing as MD3; the decoded normals agree with the triangle winding on 99.4 percent of the corners of the real file). If frame 0 uses a compressed frame, its per-vertex offsets (three bytes, **units of 0.05 with 127 as zero**, and a normal-table index) are added to the base position; the compressed normal index points into a fixed table of 256 directions that is not read, so such a surface gets **smooth normals computed from the geometry** (a note says so).
- **Texture coordinates** one pair per vertex, as written. **Triangles** three vertex numbers each, stored clockwise (Quake III convention): two corners are swapped to counter clockwise. A vertex number past the vertex list skips that triangle (counted in `notes`); degenerate triangles are dropped and counted.
- **Shader:** the first shader name of the surface (a 64-byte name) is the material's texture, **as written** (backslashes become slashes). Quake paths are relative to the game folder, not to the model. No file is opened, so there is no path to resolve; but the name is cleaned: empty, `.`, `..` and drive-letter segments and a leading slash are removed (a note says so), so a hostile shader name cannot climb out of the folder if some later code joins it to one.
- **Tags** (attachment points, such as `tag_weapon`): each tag of frame 0 becomes an empty child node of the root, with the stored position (3 x 16-bit, 1/64 units). **The tag's rotation angles are not read** and neither is tag animation.
- The root node carries the Z-up to Y-up turn (id Tech 3 is Z-up); `flatten` bakes it into the vertices.

## Not handled (each is a line in `out.notes` where it matters)

Frames after the first (vertex animation); the tag angles; the compressed normal table; `.skin` and `.shader` files; the frame table (bounds, origin, radius, name), which must lie inside the file but is not used; skins (`numSkins`); versions other than 2.

## Limits

File 128 MiB. At most **500,000 vertices and 500,000 triangles per file** (the game itself allowed 4,096 and 8,192 per surface and 32 surfaces), 1,024 surfaces, 4,096 tags, 65,535 frames. Every count is checked against the real size of the file with overflow-safe arithmetic **before** anything is allocated (`Br::Fits`: a count of `0xFFFFFFFF`, or counts whose product would wrap, are refused), every offset must lie inside the file, the surface chain moves forward by at least the 124-byte header so it cannot loop, and the whole read is bounded in time and memory: the worst allowed file (one surface of 500,000 vertices and 500,000 triangles, 14 MB) reads in about 0.4 s and holds at most about 126 MB at once (`batche_hostile` measures this with a counting allocator and fails above 256 MB or 2 s).

## Tests and what was checked

- `mdc_reader` (1,594 checks): a synthetic file (one quad, three frames, two tags, shader with backslashes) with hand-computed vertices, triangle order and winding, UVs, normals, area, root rotation, tag positions at frame 0 (frame 1's tags are different on purpose) and notes; the same file with frame 0 as a compressed frame (offsets +0.5, -1.0, +0.1 worked out by hand); two surfaces; one frame and no tags; more than 30 refusal cases (bad magic, an MD3 file, version, counts of 0, `0xFFFFFFFF`, over each budget, offsets past the end and into the header, base and compressed frame numbers that do not exist, `numBase * numVerts * 8` that would overflow, a surface chain that points back, triangles with bad vertex numbers); every truncation; every byte of both headers set to 0x00, 0xFF and 0x80.
- `batche_hostile`: 3,000 mutations of two fixtures per run (20,000 each in the sanitizer run: ASan, UBSan and float-cast-overflow, clean), the worst allowed file and the file one triangle over the budget.
- `own_dispatch_e`: `.mdc` (any letter case) reaches this reader and no other, an MD3 file named `.mdc` is refused by this reader and an MDC file named `.md3` by the MD3 reader.
- **Mutation checks** (a change to the reader, red test, restore): swapping the winding fix (12 failures), the offset scale 0.05 to 0.5 (6), removing the vertex budget (1), removing the base-frame term of the overflow check (1), reading the wrong tag frame (1). The memory cap in `batche_hostile` was lowered to see it fail.

### Real file

The only real file is `spider.mdc` from the `test/models/MDC` folder of an Assimp 5.4.3 checkout (`docs/model_corpus.md`; not in this repository; `OKAY_MODEL_CORPUS` points at it). It is read **as data only**. It has 19 surfaces, 250 frames, 1 base frame and 249 compressed frames per surface, no tags. The structure above was derived by working through its bytes (every table offset adds up exactly: triangles, shader, texture coordinates, base vertices, compressed vertices, the two frame tables, the surface end). The reader gives 19 parts, 1,312 triangles (the file holds 1,368; 56 have exactly zero area and are dropped, counted independently with a script over the raw bytes) and the first vertex `(-3805, 1100, 1160) / 64` where the file has it; the first surface's shader is `Augentex`. Frame 0 of this file uses **no** compressed frame, so the compressed-offset path (scale 0.05, bias 127) is checked only on a synthetic file made from the same description, not on a real one. **The reader was not compared with Assimp's output** (no Assimp was run for this reader); there is no second reader to agree with.

## Provenance (what this file was written from)

`MdcReader.cpp` was written from **my own memory** of the layout of the format (header, surface, frame, tag and vertex records: field order and sizes, the latitude / longitude normal packing), from this repository's MD3 reader as the structural pattern, and from the byte-by-byte analysis of `spider.mdc`, which confirmed or corrected each field. **The compressed-offset constants (0.05 scale, bias 127, and the 1/64 position scale) were typed from memory of the game's headers. They are NOT verified on any real file** (frame 0 of `spider.mdc` is not compressed; its compressed frames are all 127, i.e. zero offsets, and no test of the reader reads them), **and the mutation test only shows that the code agrees with itself.** The 1/64 position scale and the normal packing, by contrast, are confirmed by the real file (positions and the 99.4 percent normal agreement). No code or struct names of the game or of any importer are used or quoted here, and no importer source (Assimp or any other project) was read, copied, ported or followed. Assimp was not run on these files either.

**Decision for the owner:** keep the compressed-first-frame path (unverified, but it only matters for files whose frame 0 is compressed, which the game's own tools did not seem to write), or refuse such surfaces until a real file shows the constants. Currently it reads them and says so in `notes`.

Unverified: a compressed first frame (no real example), the compressed offset scale, tag rotation, other MDC versions, files from games other than Return to Castle Wolfenstein, a run on real Windows.
