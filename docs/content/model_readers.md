# Our own model readers

> **What this is:** One section per model file format we read with our own code (written to replace Assimp). Speed, real-file tests and the formats still missing are in model_import.md, model_corpus.md, own_readers_speed.md and own_readers_remaining.md next to this page.
>
> **Status:** combined on 2026-11-20 from 38 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
>
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Our own 3DS reader (`.3ds`)](#our-own-3ds-reader-3ds) (was `docs/own_3ds_reader.md`)
- [Our own 3MF reader (`.3mf`)](#our-own-3mf-reader-3mf) (was `docs/own_3mf_reader.md`)
- [Our own AC3D reader (`.ac` `.acc` `.ac3d`)](#our-own-ac3d-reader-ac-acc-ac3d) (was `docs/own_ac3d_reader.md`)
- [Our own AMF reader (`.amf`)](#our-own-amf-reader-amf) (was `docs/own_amf_reader.md`)
- [Our own ASE reader (`.ase`)](#our-own-ase-reader-ase) (was `docs/own_ase_reader.md`)
- [Our own B3D reader (`.b3d`)](#our-own-b3d-reader-b3d) (was `docs/own_b3d_reader.md`)
- [Our own Blender reader (`.blend`)](#our-own-blender-reader-blend) (was `docs/own_blend_reader.md`)
- [Our own BVH reader (`.bvh`)](#our-own-bvh-reader-bvh) (was `docs/own_bvh_reader.md`)
- [Our own COB reader (`.cob`)](#our-own-cob-reader-cob) (was `docs/own_cob_reader.md`)
- [Our own CSM reader (`.csm`)](#our-own-csm-reader-csm) (was `docs/own_csm_reader.md`)
- [Our own DXF reader (`.dxf`)](#our-own-dxf-reader-dxf) (was `docs/own_dxf_reader.md`)
- [Our own FBX reader (`okay::fbx`)](#our-own-fbx-reader-okayfbx) (was `docs/own_fbx_reader.md`)
- [Our own HMP reader (`.hmp`, version HMP7)](#our-own-hmp-reader-hmp-version-hmp7) (was `docs/own_hmp_reader.md`)
- [Our own IQM reader (`.iqm`)](#our-own-iqm-reader-iqm) (was `docs/own_iqm_reader.md`)
- [Our own Irrlicht scene reader (`.irr`)](#our-own-irrlicht-scene-reader-irr) (was `docs/own_irr_reader.md`)
- [Our own Irrlicht mesh reader (`.irrmesh`)](#our-own-irrlicht-mesh-reader-irrmesh) (was `docs/own_irrmesh_reader.md`)
- [Our own LWO reader (`.lwo`, and Modo's `.lxo`)](#our-own-lwo-reader-lwo-and-modos-lxo) (was `docs/own_lwo_reader.md`)
- [Our own LWS / MOT reader (`.lws` `.mot`)](#our-own-lws--mot-reader-lws-mot) (was `docs/own_lws_reader.md`)
- [Our own MD2 reader (`.md2`)](#our-own-md2-reader-md2) (was `docs/own_md2_reader.md`)
- [Our own MD3 reader (`.md3`)](#our-own-md3-reader-md3) (was `docs/own_md3_reader.md`)
- [Our own MD5 reader (`.md5mesh`, `.md5anim`)](#our-own-md5-reader-md5mesh-md5anim) (was `docs/own_md5_reader.md`)
- [Our own MDC reader (`.mdc`)](#our-own-mdc-reader-mdc) (was `docs/own_mdc_reader.md`)
- [Our own MS3D reader (`.ms3d`)](#our-own-ms3d-reader-ms3d) (was `docs/own_ms3d_reader.md`)
- [Our own NDO reader (`.ndo`): a seam only, no geometry](#our-own-ndo-reader-ndo-a-seam-only-no-geometry) (was `docs/own_ndo_reader.md`)
- [Our own NFF reader (`.nff` `.enff`)](#our-own-nff-reader-nff-enff) (was `docs/own_nff_reader.md`)
- [Our own OFF reader (`.off`)](#our-own-off-reader-off) (was `docs/own_off_reader.md`)
- [Our own OGRE mesh readers (`.mesh.xml` and binary `.mesh`)](#our-own-ogre-mesh-readers-meshxml-and-binary-mesh) (was `docs/own_ogre_reader.md`)
- [Our own PMX reader (`.pmx`)](#our-own-pmx-reader-pmx) (was `docs/own_pmx_reader.md`)
- [Our own Q3D reader (`.q3o` `.q3s`)](#our-own-q3d-reader-q3o-q3s) (was `docs/own_q3d_reader.md`)
- [Our own RAW reader (`.raw`)](#our-own-raw-reader-raw) (was `docs/own_raw_reader.md`)
- [Our own SIB reader (`.sib`): NOT WRITTEN](#our-own-sib-reader-sib-not-written) (was `docs/own_sib_reader.md`)
- [Our own SMD reader (`.smd`)](#our-own-smd-reader-smd) (was `docs/own_smd_reader.md`)
- [Our own Terragen reader (`.ter`)](#our-own-terragen-reader-ter) (was `docs/own_terragen_reader.md`)
- [Our own X3D reader (`.x3d`)](#our-own-x3d-reader-x3d) (was `docs/own_x3d_reader.md`)
- [Our own DirectX `.x` reader (`.x`)](#our-own-directx-x-reader-x) (was `docs/own_x_reader.md`)
- [Our own XGL / ZGL reader (`.xgl` `.zgl`)](#our-own-xgl--zgl-reader-xgl-zgl) (was `docs/own_xgl_reader.md`)
- [`.xml` model files: picked by their root element](#xml-model-files-picked-by-their-root-element) (was `docs/own_xml_reader.md`)
- [Our own ZAE reader (`.zae`: COLLADA in a ZIP archive)](#our-own-zae-reader-zae-collada-in-a-zip-archive) (was `docs/own_zae_reader.md`)

## Our own 3DS reader (`.3ds`)

*Was `docs/own_3ds_reader.md` until 2026-11-20.*

Status: **side by side, off by default.** Assimp (when the build has it, `OKAY_USE_ASSIMP`) is still the only other reader of `.3ds`. Without Assimp and without switching ours on, a `.3ds` file does not import. Nothing was removed. This is the "write the 3DS reader" step of `docs/status/own_code_roadmap.md`.

### What it is

Written from the public description of the 3D Studio chunk format (little-endian `u16 id, u32 length` chunks, the length includes the 6-byte header), with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/Max3dsReader.cpp` | The reader: `okay::Read3dsBuffer` / `okay::Read3dsFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 4 of the per-format backend switch (same code as OBJ, STL, PLY, COLLADA) |
| `scripts/gen_3ds_fixtures.py` | Writes `tests/data/models/cube.3ds`, `box_flat.3ds`, `hierarchy.3ds` |
| `tests/test_3ds_reader.cpp` | Hand-computed checks on the fixtures and the seam (`3ds_reader`) |
| `tests/test_3ds_bvh_hostile.cpp` | Hostile input and mutation fuzz, built on its own under sanitizers (`3ds_bvh_hostile`) |

The reader file uses only the standard library and `okay/Render/ModelData.hpp` (header-only types), so the hostile test builds it without the engine library.

### Turning it on

Exactly like `OKAY_OWN_COLLADA`:

- CMake: `-DOKAY_OWN_3DS=ON` makes it the default `.3ds` reader for that build (default `OFF`).
- Run time: environment variable `OKAY_OWN_3DS=1` (or `0`) overrides the build default; `okay::SetImportBackend(".3ds", ImportBackend::Own)` beats both; `SetImportBackend("*", ...)` covers every format with an own reader.
- Off, with no Assimp in the build: `ImportModelData` fails with a message that says how to switch it on, and `ImportableExtensions()` does not list `.3ds`.
- On, and the build has Assimp: ours is tried first, Assimp is the fallback for files ours rejects.

### What it reads

- **Meshes** (`NAMED_OBJECT` > `TRI_OBJECT`): points, faces (three `u16` indices and a flags word), texture coordinates, smoothing groups, per-face material groups, the mesh matrix. One part per (object, material). Vertices are welded per point index and normal. UV `v` is flipped (`v = 1 - v`) like the other bottom-up formats.
- **Normals** from the smoothing groups: faces whose groups share a bit share the vertex's angle-weighted normal; group 0 is flat; a file without a smoothing-group chunk is smoothed as one group.
- **Materials**: name, diffuse and specular colour (24-bit or float, linear or not), shininess (percent, passed on as the shininess value), self illumination (becomes an emissive colour: diffuse times the percentage), diffuse, specular and bump texture file names (3DS names are 8.3 upper case; the reader finds the file next to the model ignoring case).
- **Master scale** (`0x0100`) goes on the root node.
- **Keyframes** (`KFDATA`): the node hierarchy (parent ids, instance names, dummy objects, and camera / light / target nodes as plain nodes, because they share the node numbering), pivots, position, rotation and scale tracks, 30 frames per second (3DS stores frames, not seconds; 30 is the usual rate), the take starts at 0. Rotation keys are axis and angle, stored **relative to the previous key** and **clockwise for a positive angle**; the reader accumulates them and flips the sign. Instances (several nodes naming the same mesh) share the mesh.
- A node that has only one key per track is just a placement: it becomes the node's transform and no clip is made for it.

#### Coordinates

Checked against real files (see below), not assumed:

- The file's vertices are in scene space (Z up). The mesh matrix (3 rows of the 3x3, then the translation) maps the object's own space to scene space for **row vectors**: `world = local * R + t`. The importer therefore stores each vertex relative to its node as `local = (stored - t) * R^-1 - pivot`, and the node places it with its keyframes. A mesh without a keyframe node hangs off the root as stored.
- The root node carries the master scale and the Z-up to Y-up rotation `(x, y, z) -> (x, z, -y)`. Flattened import (`ImportModel`, the merged-mesh path) bakes that into the vertices and returns one node and no clips.

### Limits (hostile files)

Every read is bounds-checked; a chunk longer than its parent is cut to the parent (and reported), a chunk shorter than its header fails the file.

| Limit | Value |
|---|---|
| File size | 512 MiB |
| Chunk nesting depth | 32 |
| Chunks per file | 10,000,000 |
| Objects / materials / keyframe nodes | 100,000 each |
| Points, faces in total | 20,000,000 each |
| Keys in total | 20,000,000 |
| Output vertices / mesh parts | 40,000,000 / 200,000 |
| Distinct smoothing groups tracked per vertex | 128 (the rest merge into the first) |
| Coordinates, key values, pivots | within +-1e15 and finite; the master scale in (0, 1e6) |

Counts (points, faces, keys, group lists) are checked against the bytes left in their chunk before anything is allocated. Face indices are range-checked, degenerate faces skipped, non-finite points refuse the file, a UV list that does not match the point count is dropped, a cycle in the keyframe parents is broken, a singular mesh matrix falls back to "as stored". Results that overflow in the matrix arithmetic fail the read.

### Not handled (what a user might trip over)

Each of these is also reported in `ImportedModel::notes` where the file has it:

- Lights and cameras are not imported as objects; their keyframe nodes are plain nodes and only the position track is read (lens, roll, colour, cone and target tracks are not).
- Material: transparency, two-sided, opacity / reflection / shininess / self-illumination / second maps, texture tiling, offset and rotation, the shading mode, the ambient colour. Colour is taken from the diffuse chunk only.
- Spline parameters on keys (tension, continuity, bias, ease) are skipped, every key is played linearly; the clip's quaternions are interpolated by the engine, not along the file's spline.
- Frame rate is assumed to be 30 per second. A file made at another rate plays too fast or too slow.
- Mesh matrix is used for placement only; mirrored (negative determinant) objects are not specially handled; the "mesh texture info" chunk, the "proc" chunks, the viewport and layout chunks, the keyframe "current time" and "scene" chunks are ignored.
- 3DS faces with edge-visibility flags, polygon (n-gon) information and hidden-object flags are ignored. Quads are two triangles as stored.
- Object `hidden` / `frozen` flags are not read.
- The 64K limit of the format is the 64K limit here: 65,535 points and 65,535 faces per object.
- Smoothing normals use angle weighting; another importer may weight by area, so normals can differ by a few degrees on irregular meshes.
- Not benchmarked. Largest real file read: 31,460 triangles in 31 ms.

### Tests and what was checked

- `3ds_reader`: the three fixtures against hand-computed numbers (welded and flat vertex counts, normals, UV flip, root rotation and scale, master scale, material fields and the note for each skipped thing, hierarchy and parent order, an instance, pivot, mesh matrix, rotation sign and accumulation, 30 fps times), then the seam (switch order, merged-mesh path, scene import with a `ModelAnimator` clip, flattened import, failure messages).
- `3ds_bvh_hostile` (standalone, no engine): every truncation of every fixture; bad lengths, 5000-deep nesting, point and face arrays that claim more than they hold, indices out of range, non-finite and huge values, a vertex with thousands of distinct smoothing groups (time-checked), 65,535 faces on three points, 3000 objects, 100,001 materials, keyframe cycles, a 2000-deep keyframe chain, track key counts of 4 billion, NaN keys, singular scales; then a deterministic mutation fuzz (byte flips, length stomps, inserts, deletes). Built with `-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all`: clean, including 150,000 mutations per format (`OKAY_3DS_FUZZ_ITERS`).
- Real files: 16 files from an outside corpus, see `docs/content/model_corpus.md`. Fifteen are read; the sixteenth is an empty file (rejected, as Assimp rejects it). Against Assimp built from source on the same files: mesh count, triangle count, bounding box and surface area match on all 15 static or animated meshes, including the two files with a mesh matrix and an animated rotation, which is how the row-vector convention and the clockwise rotation were found; the animation key values at the last key match to four decimals. Differences: Assimp reports the animation of camera-only files and of a camera parent that we only keep as a node; Assimp adds a default material; durations differ by the frame-rate assumption (Assimp sets no rate for this format; the comparison divided its frame count by 25, we divide by 30; the frame counts are equal).

### Before it can become the default

The default stays off until: the reader has been run on files from more than one real exporter (the corpus above is a handful of files from a few tools), the Windows distribution has run it, and the owner approves. After that `ModelImportAssimp.cpp` can lose its 3DS role (`docs/status/own_code_roadmap.md`).

## Our own 3MF reader (`.3mf`)

*Was `docs/own_3mf_reader.md` until 2026-11-20.*

Status: **side by side, off by default.** Assimp (when the build has it, `OKAY_USE_ASSIMP`) is still the only other reader of `.3mf`. Without Assimp and without switching ours on, a `.3mf` file does not import. Nothing was removed. This is the first of the five readers (3MF, AMF, X3D, DXF, LWO) that the "drop-the-rest" step of `docs/status/own_code_roadmap.md` needed before Assimp can go.

### What it is

Written from the public 3MF Core Specification (the zip package, the `3D/3dmodel.model` XML part), with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/ThreeMfReader.cpp`, `engine/include/okay/Render/ThreeMfReader.hpp` | `okay::Read3mfBuffer` / `okay::Read3mfFile` |
| `engine/src/Render/ZipLite.hpp` | The zip reader (also used by AMF): central directory, stored and deflate entries, the engine's own inflate (`okay::Inflate`, a wrapper in `Core/ImageDecode.cpp` over the one decoder in `Core/Inflate.hpp`) |
| `engine/src/Render/XmlLite.hpp` | The strict XML reader shared by 3MF, AMF and X3D |
| `engine/src/Render/ReaderCommon.hpp` | Shared by all five batch-B readers: locale-independent number parsing (`NumParse.hpp`), transforms, the triangle soup and its smooth-normal builder, the Z-up root |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 11 of the per-format backend switch (the batch-B table) |
| `tests/test_3mf_reader.cpp` | Hand-computed checks on synthetic packages built in memory, and the seam (`3mf_reader`) |
| `tests/test_batchb_hostile.cpp` | Hostile input and mutation fuzz for all five readers, built on its own under sanitizers (`batchb_hostile`) |

### Turning it on

Exactly like `OKAY_OWN_3DS`: `-DOKAY_OWN_3MF=ON` (default `OFF`), environment variable `OKAY_OWN_3MF=1` / `0`, or `okay::SetImportBackend(".3mf", ImportBackend::Own)` (the run-time choice wins). Off with no Assimp in the build: `ImportModelData` fails with a message saying how to switch it on, and `ImportableExtensions()` does not list `.3mf`. On with Assimp built: ours is tried first, Assimp is the fallback for files ours rejects.

### What it reads

- **Package**: the zip's `_rels/.rels` names the model part (`.../3dmodel` relationship); without it `3D/3dmodel.model` is used. Only that part is read (the production extension's other model files are not).
- **Meshes**: `object` / `mesh` / `vertices` / `triangles`. Triangles keep the file's winding (counter-clockwise from outside).
- **Components and build**: `components` / `component` trees (nested up to 16 deep, cycles refused) and `build` / `item`, each with its 12-number `transform` (row-vector convention: `x' = x*m0 + y*m3 + z*m6 + m9`). A transform with a negative determinant (a mirror) swaps two corners of every triangle so faces keep pointing outward. Each build item is one node (named by `partnumber`, else the object name) with one part per material; the item's transform is baked into the vertices. A file without a `build` section uses every object that is not a component of another one, with a note.
- **Materials**: `basematerials` (`base` name and `displaycolor`) and `colorgroup`; the object's `pid` / `pindex` and the triangle's `pid` / `p1`. One part per distinct (group, index); the colour (with alpha) is the material's diffuse colour. A triangle whose three corners name different colours takes the first corner's colour (noted).
- **Object types**: `support` and `solidsupport` objects are not output (noted); `model`, `surface` and `other` are.
- **Axes and units**: the package is Z-up; the root node carries the Z-up to Y-up rotation `(x, y, z) -> (x, z, -y)` like the 3DS reader, and `ImportModel`'s flattened path bakes it into the vertices. Units are kept as written (3MF default: millimetre); a unit other than millimetre is reported in the notes.
- **Normals**: 3MF has none. Smooth normals are made with a 45 degree crease angle: faces whose normals differ by less stay smooth across a shared vertex, harder edges stay sharp.

### Limits (hostile files)

| Limit | Value |
|---|---|
| Package size / one inflated entry / everything inflated from one archive | 512 MiB / 256 MiB / 384 MiB |
| Zip entries | 20,000 |
| XML elements / nesting / attributes per element / name length | 3,000,000 / 256 / 256 / 256 bytes |
| Vertices / triangles | 20,000,000 each |
| Objects / build items / components per object / component instances | 1,000,000 / 100,000 / 100,000 / 200,000 |
| Component nesting | 16 |
| Output parts | 200,000 |
| Coordinates | finite, within +-1e15 (also after every transform) |

The zip reader refuses encrypted entries, ZIP64, multi-disk archives and any method other than stored and deflate; it checks the directory against the local header, requires the inflated size to equal the declared size (the inflater is given that size as its output limit, so a bomb stops at it) and the CRC-32 to match. The XML reader never reads a DTD: a `DOCTYPE` is skipped, and one that declares an `<!ENTITY` refuses the file (no entity is ever expanded, so there is no billion-laughs and no external entity); only the five predefined entities and numeric references are decoded; UTF-16 is refused. Numbers are read through `NumParse.hpp` (a `.` is the decimal point whatever `LC_NUMERIC` says), only plain decimal numbers are accepted (`inf`, `nan` and hex floats are not), and a vertex with a bad number fails the file.

### Not handled

Each is also reported in `ImportedModel::notes` where the file has it:

- Textures (`texture2d`, `texture2dgroup`), composite materials and multi-properties: triangles that use them get no material.
- The beam lattice, slice and other extensions; components that point to another model file (`p:path`); `requiredextensions` are listed in the note.
- Metadata (title, designer, ...), thumbnails, print tickets, the `printable` flag.
- Transform hierarchy: transforms are baked, so the scene has one node per build item, not one per component.
- The 3MF zip is limited to 3,000,000 XML elements (about 2.5 million triangles); larger files are refused with a message. The reader keeps the XML tree in memory, it does not stream.

### Tests and what was checked

- `3mf_reader` (`tests/test_3mf_reader.cpp`): **synthetic** packages built in memory (a stored and a deflated zip with `_rels/.rels` and the model part): a 10 x 7 x 5 box (vertex and triangle counts, flat normals on the 90 degree edges, the signed volume 350, the Z-up turn with and without `flatten`), base materials (names, colours, alpha, one part per material), colour groups, texture groups (noted, no material), component trees with transforms (the row-vector convention checked with a 90 degree turn, scale, a mirror keeping the volume positive), cycles, missing objects, support objects, units, a missing `build`, an out-of-range index, a decimal-comma locale, every refusal (not a zip, no model part, no triangles, bad transform, a NaN, DTD entities, an encrypted entry), and the seam (opt-in switch, `ImportModel`, a UTF-8 folder and file name).
- `batchb_hostile` (3MF section, standalone, no engine): every truncation of a package; zip bombs (a declared size past the cap is refused before inflating; a small declared size on a stream that inflates to far more stops at the declared size; an honest 100 MB entry is read in bounded time); a wrong entry count, directory offset, end record, checksum and method; XML nested 20,000 deep, 5,000 attributes on one element, billion-laughs and external-entity DOCTYPEs, UTF-16; bad numbers and indices (`1e999`, `inf`, `nan`, hex, `1e16`, negative, 2^32); a 100-deep component chain and a 30 x 30 x 30 x 30 x 30 fan-out (instance cap, no hang); 2,000 materials; then a deterministic mutation fuzz (byte flips, inserts, deletes, number swaps, truncation of the XML with a valid checksum, and of the container). Built with `-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all`: clean.

#### Real files

**One real 3MF file exists in Assimp's test tree** (`models/3MF/box.3mf`, a 10 x 20 x 30 millimetre box in a deflate-compressed package from a 2015 sample), so that is all the real-file validation there is; everything else above is synthetic. Against Assimp 5.4.3 built from the same sources: both read it; 1 mesh, 12 triangles, surface area 2200 and bounding box equal (Assimp's box turned the same Z-up way; Assimp keeps the file's 8 shared vertices, ours has 24 because the 90 degree edges stay sharp). Not verified on real files: colour groups and base materials (a real 3MF written by a slicer or CAD tool), the production extension, components across files, textures, anything over 8 vertices.

## Our own AC3D reader (`.ac` `.acc` `.ac3d`)

*Was `docs/own_ac3d_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_AC3D`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of AC3D files. Nothing was flipped.

### What it is

AC3D is a small 3D modeller; its text format ("AC3Db") is the exchange format of FlightGear, many flight and racing simulators and a lot of hobby models. A file has a header line, `MATERIAL` lines and a tree of `OBJECT` blocks; a `poly` object holds vertices and surfaces (`SURF`) that point at them.

| File | What |
|---|---|
| `engine/src/Render/Ac3dReader.cpp` | `okay::ReadAc3dBuffer` / `okay::ReadAc3dFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/TextReaderUtil.hpp`, `ReaderCommon.hpp`, `PolygonTriangulate.hpp` | Shared pieces: UTF-16 / BOM text, line and token reading, number parsing, mesh builder, polygon triangulation |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 22 of the opt-in table (extensions `.ac`, `.acc`, `.ac3d`) |
| `tests/test_ac3d_reader.cpp`, `tests/batchd_fixtures.hpp` | Hand-computed checks on synthetic files (`ac3d_reader`) |
| `tests/test_batchd_hostile.cpp` | Hostile input and mutation fuzz (`batchd_hostile`) |
| `tests/test_batchd_budget.cpp` | The subdivision budget on a small hostile file (`batchd_budget`) |

### What it reads

- **Text.** UTF-8, UTF-8 with a byte order mark and UTF-16 with a byte order mark (the files in the wild come in all three).
- **Objects.** `world`, `group`, `poly` and `light` objects as nodes (a synthetic root node, then the file's own tree), with `loc` as the node's position and `rot` (9 numbers, a 3x3 matrix read as rows, `p' = M p + loc`) as its rotation and scale (the scale from the column lengths; a shear is dropped and said so). `name` names the node and the part. The tree is walked with an explicit stack, 512 levels at most.
- **Vertices.** `numvert N` lines of `x y z`, and the extended files (`.acc`, written by newer AC3D versions) with `x y z nx ny nz`: the normals are kept.
- **Surfaces.** `SURF` flags (hex): type 0 polygon (any number of corners, triangulated: convex ones as a fan, concave ones ear-clipped), type 4 triangle strip (unrolled, every second triangle turned over), types 1 and 2 (closed and open lines) are skipped and counted. Bit `0x10` (shaded) asks for smooth normals made with the object's `crease` angle (45 degrees when the file gives none); flat surfaces keep face normals; the two-sided bit `0x20` has no counterpart in the engine's meshes and is ignored. One part per (object, material, shaded or flat).
- **Materials.** `MATERIAL "name" rgb ... emis ... shi ... trans ...`: `rgb` is the diffuse colour, `emis` the emissive colour, `shi` the shininess, `trans` becomes alpha (1 - trans). `amb` and `spec` are read and not used. A surface's `mat` picks one; the object's `texture` is joined to the model's folder and becomes the material's texture (one material per (colour set, texture) pair).
- **Texture coordinates.** Each `refs` line is `index u v` (the extended files add more pairs for the extra layers; only the first is read). `texrep` and `texoff` are applied as `u * rep + off`. An untextured object gets no coordinates (the files write zeros there).
- **Subdivision.** `subdiv N` is applied: **Catmull-Clark subdivision** (Catmull and Clark, 1978) of the object's polygons, N levels (at most 8, and it stops early before the result would pass the file-wide budget of 2,000,000 corners). Texture coordinates are subdivided inside each face so seams stay. Stored per-vertex normals belong to the control mesh and are replaced by smooth ones.

### Not handled (each is a line in `out.notes`)

Line surfaces (open and closed lines) and files that have **only** lines or no surfaces (the file is refused: the engine's meshes hold triangles), the extra texture layers of `.acc` files (`tiled`, `skids`, `shad`), lights (the node stays, the light does not), `url`, `data` blocks (skipped), unknown keywords (counted, first one named), surfaces with fewer than 3 corners, surfaces that point past the vertex list.

### Limits

File 256 MiB; per object 5,000,000 vertices, 5,000,000 surfaces, 1,000,000 corners per surface, 40,000,000 corners in all; 1,000,000 objects; 100,000 materials; nesting 512. Counts (`numvert`, `numsurf`, `refs`, `kids`) must be whole numbers within the caps and the text must really hold that many lines, or the file is refused before anything large is allocated. Numbers go through `NumParse.hpp` (finite, within 1e15, locale independent). Subdivision has a budget for the whole file of 2,000,000 output corners (each level makes four corners out of one) and no object may pass it alone; an object that would pass it keeps the levels it has and a note says so (a 8 KB file of 60 objects with `subdiv 8` took 3.2 s and 327 MB before the budget; now about 0.5 s).

### Tests and what was checked

- `ac3d_reader`: **synthetic** file (two materials, a textured quad with `texrep` / `texoff` and a `loc`, a group with a 90 degree `rot` and a child triangle plus a line surface, a triangle strip with stored normals) with `\n` and `\r\n` line ends, as UTF-16 and with a byte order mark: parts, triangles, area 7.5 (by hand), node tree, the rotation quaternion, a vertex seen through the group's matrix, texture and uv numbers, colours, notes; the Catmull-Clark step on a cube (48 triangles, corners at 5/9, edge points at 0.75, face points at the face centres) at levels 0, 1 and 2; refusals (empty, wrong header, no object, bad counts and numbers, bad flags, an index past the vertices, lines only) and **every truncation**.
- `batchd_hostile` (AC3D section): counts at, past and far past the caps with tiny text, `nan`, `inf`, `1e999`, hex floats and overflowing numbers in `loc`, `rot`, `texrep`, vertices and materials, singular, mirrored and huge `rot` matrices, 5,000 levels of nesting, 20,000 objects and materials, a 3 MB number, a 60,002-corner strip, `subdiv 1000000`, then 3,000 mutations of the file (30,000 in the sanitizer run). Sanitizers (`address,undefined,float-cast-overflow`): see `docs/content/model_corpus.md`.

#### Real files

Assimp's tree has 11 AC3D files with an extension (`models/AC`: 9 `.ac` and 2 `.acc`) and `TestFormatDetection` without one (not routed by extension). **Ours reads 8 of the 11 and refuses 3; Assimp reads 10 and fails on 1.** On the 7 files both read, the **mesh count, surface area and bounding box agree**, and so do the triangle counts of the 5 `.ac` files, including the subdivided ones: `SphereWithLight.ac` 17,664 triangles in both (area 0.0361 both), `SphereWithLightUvScaling4X.ac` 70,656 in both, `sample_subdiv.ac` 245,760 in both (area 12.9811 ours, 12.9756 Assimp: 0.04 percent apart, the one difference in the subdivision, whose cause is not known), `Wuson.ac` 3,732, the UTF-8 BOM file 264. Differences: ours drops zero-area triangles (the `.acc` strips contain degenerate ones: 264 against Assimp's 286 on `SphereWithLight.acc`, 3,732 against 4,633 on `Wuson.acc`, equal area and box); Assimp does not read the UTF-16 file (`SphereWithLight_UTF16LE.ac`: "magic sequence not found"), ours does and gets the same result as the UTF-8 twin; Assimp reads `openLine.ac`, `closedLine.ac` and `nosurfaces.ac` as meshes without triangles, ours refuses them (nothing to draw); Assimp ignores `texrep` and `texoff` (probed with a synthetic file), ours applies them; Assimp puts the object's `loc` into the vertices and the `rot` matrix on the node, ours keeps both on the node (the same when `rot` is the identity, which is every file in the tree).

Unverified: `rot` on a real file (none in the tree has one; the convention `p' = M p + loc` is the reading of the specification and of one synthetic probe, not a second reader's agreement on a real model), the `texrep` / `texoff` formula against the AC3D program itself, files with several `kids` levels from FlightGear, `crease` and normals on a real file, a run on real Windows.

### Provenance (what this file was written from)

`Ac3dReader.cpp` was written from scratch for this project from the public description of the AC3D file format (the header, `MATERIAL`, `OBJECT`, `SURF` and the keywords above, as documented by the AC3D program's authors) and from reading the sample files of Assimp's `test/models/AC` as data (never the Assimp source). Subdivision follows the paper by Catmull and Clark (1978). Assimp was used only as a **black box**: run on the test files and on small synthetic files made for this purpose, its output (counts, area, box, node matrices, texture coordinates) showed the `.acc` layout (one `index u v` row per corner and extra layers), that triangle strips are type 4, that `subdiv` is Catmull-Clark (the triangle counts are exactly 4^N times the polygon count) and how `rot` and `loc` are placed. No code, comment, table or test from any other importer was read, copied or ported.

## Our own AMF reader (`.amf`)

*Was `docs/own_amf_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_AMF`, same rules as `docs/content/model_readers.md`, which also describes the shared XML and zip readers and their limits). Assimp (when built) is still the only other reader of `.amf`.

### What it is

Written from the public description of the ISO/ASTM 52915 Additive Manufacturing File Format, with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/AmfReader.cpp`, `engine/include/okay/Render/AmfReader.hpp` | `okay::ReadAmfBuffer` / `okay::ReadAmfFile` |
| `engine/src/Render/XmlLite.hpp`, `ZipLite.hpp`, `ReaderCommon.hpp` | Shared with 3MF (see `docs/content/model_readers.md`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 12 of the batch-B table |
| `tests/test_amf_reader.cpp` | Hand-computed checks on synthetic files and the seam (`amf_reader`) |
| `tests/test_batchb_hostile.cpp` | Hostile input and mutation fuzz (`batchb_hostile`) |

### What it reads

- **Container**: plain XML, or a zip archive (the first entry ending `.amf`, else the first file). Zip entries are checked as for 3MF (size caps against bombs, CRC, no encryption).
- **Objects**: `object` / `mesh` / `vertices` / `vertex` / `coordinates` and `volume` / `triangle` (`v1`, `v2`, `v3`). One node per object instance (named by its `name` metadata, else `object<id>`) and one part per resolved colour.
- **Colours**, in the format's order of priority: a triangle's `color`, else the volume's `materialid` material, else the volume's `color`, else the object's `color`. A `material` gives its `color` and its `name` metadata. Channels are clamped to 0..1; alpha becomes the diffuse colour's alpha. Colours that are formulas or composites are not read (noted).
- **Constellations**: the first `constellation`: each `instance` places an object with `deltax/y/z` and a rotation `rx`, `ry`, `rz` in degrees (x first, then y, then z, then the move); a negative determinant mirrors (winding follows). Objects not in the constellation are not output (noted). Without a constellation every object is output where it is. A second constellation and nested constellations are not read (noted).
- **Axes and units**: Z-up like 3MF (root node rotation, baked by the flattened path); units kept as written, a unit other than millimetre noted. Smooth normals with a 45 degree crease angle (the format's own vertex normals are not used, noted).

### Not handled (each is also in `notes`)

Textures (`texture`, `texmap`, `map`), vertex colours (the format puts them above every other colour; `Mesh` has no vertex colours, so they are reported), vertex normals, curved edges (`edge`), colour formulas and composite materials, further and nested constellations, INSERT-style arrays. The XML tree is limited to 5,000,000 elements. Limits and the hostile-input rules are those of `docs/content/model_readers.md`; the instance count is capped at 100,000.

### Tests and what was checked

- `amf_reader` (`tests/test_amf_reader.cpp`): **synthetic** documents: a tetrahedron (area, flattened Z-up bounds, flat normals), materials and the colour priority (triangle over volume material over object colour), formula colours and textures (noted), a constellation with a rotation and a move computed by hand (`rz = 90`, delta 10: `(x, y) -> (-y + 10, x)`), the x-then-y-then-z rotation order, two instances of one object, units, a zip-compressed AMF (also with a leading unrelated entry), an out-of-range index, every refusal, and the seam.
- `batchb_hostile` (AMF section): every truncation, XML bombs, bad numbers and indices, absurd constellation values (`1e300`, `nan`), 100,001 instances (refused) and 3,000 (read), 3,000 distinct triangle colours, zip bombs and truncated zips, then the mutation fuzz over plain and zipped files. Sanitizers: clean.

#### Real files

The 11 files in Assimp's `test/models/AMF` (hand-made by its authors, under 3 kB each) all read; Assimp 5.4.3 reads the same 11. **5 agree on everything compared** (meshes, triangles, materials, surface area, bounding box after the Z-up turn): `test1`, `test2`, `test3`, `test5`, `test5a`. The other 6 differ, and the reasons were checked in the files:

- `test4`, `test6`: one volume with a volume colour and a triangle with its own colour (the format puts the triangle's colour first); ours makes one part and one material per colour (2 and 2), Assimp one mesh with one material.
- `test7`, `test_with_mat`: an object with two volumes. Assimp returns only the first volume's triangles (4 of 8; the area is exactly half); ours reads both (a bipyramid, area 18.22).
- `test8`, `test9`: constellations. Ours places every instance with its delta and rotation (`test8`: two pyramids at x = -5 and 5, turned -45 and 45 degrees about x, box x in [-5.5, 6]). Assimp's box is 10 times wider (x in [-45.5, 46]); the offset it uses equals the rotation value, not the delta, so it looks like an Assimp bug; we did not debug it. `test9` has two constellations, ours uses the first and says so.

Notes our reader printed on the real files: 6 files with vertex colours (not read), `test_with_mat` has unit `inch` (kept), `test9` a second constellation. Not verified on real files: zip-compressed AMF, textures, curved edges, files from a slicer or CAD tool.

## Our own ASE reader (`.ase`)

*Was `docs/own_ase_reader.md` until 2026-11-20.*

Status: **side by side, off by default.** Until now 3ds Max ASCII Scene Export files only imported in a build with Assimp (and only with its 3DS importer also built: Assimp's ASE code is compiled out without it). Nothing was removed.

### What it is

Written from the public description of the format and from files 3ds Max wrote.

| File | What |
|---|---|
| `engine/src/Render/AseReader.cpp` | `okay::ReadAseBuffer` / `okay::ReadAseFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 10 of the backend switch) |
| `scripts/gen_ase_fixtures.py` | Writes `tests/data/models/scene.ase`, `scene_crlf.ase`, `scene_utf16le.ase` (synthetic) |
| `tests/test_ase_reader.cpp` | Hand-computed checks, the seam, scene import, UTF-8 paths, UTF-16, locale (`ase_reader`) |
| `tests/test_ase_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`ase_hostile`) |

### Turning it on

`-DOKAY_OWN_ASE=ON`, environment variable `OKAY_OWN_ASE=1` / `0`, or `SetImportBackend(".ase", ImportBackend::Own)` (the run-time choice wins). Off by default; with Assimp built, Assimp stays the default `.ase` reader.

### What it reads

- Text, one keyword per line, blocks in `{ }` tracked on an explicit stack (no recursion, depth 64). UTF-8, UTF-8 with a byte order mark, and UTF-16 with a byte order mark (little or big endian: both exist among Assimp's test files and Assimp reads neither correctly).
- `*3DSMAX_ASCIIEXPORT` header (required), `*SCENE_FRAMESPEED` and `*SCENE_TICKSPERFRAME`.
- Materials: name, diffuse colour, shininess (0..1 of 128), self illumination (as emission of the diffuse colour), transparency (into the diffuse alpha), the diffuse map's `*BITMAP` (a Windows drive path keeps only its file name; the rest is relative to the model's folder), sub-materials of a Multi/Sub-Object material by the face's `*MESH_MTLID` (ids past the count wrap, a note). A material is made when a face first uses it.
- `*GEOMOBJECT`: `*NODE_NAME`, `*NODE_PARENT`, `*NODE_TM` (`TM_ROW0..2` are the axes, `TM_ROW3` the position, in world space), `*MESH` with vertices (stored in world space: turned back into the node's local space), faces (`A: B: C:`, smoothing groups including `1,2,3` lists, `*MESH_MTLID`), texture vertices and faces (v flipped), and normals: `*MESH_NORMALS` per corner when the file has them, otherwise built from the smoothing groups. One part per object and material; vertices welded on position, normal and UV.
- The hierarchy as nodes under a root `ASE` that turns Z-up into Y-up; a node's transform is relative to its parent; helper, camera, light and shape objects are kept as plain empty nodes (their settings are not imported), so their animation still plays. A target camera's second `*NODE_TM` and its `.Target` animation block are ignored with a note.
- Animation: `*TM_ANIMATION` position, scale and rotation tracks as one clip, `animation` (times are ticks / (frame speed x ticks per frame)). Rotation samples are axis-angle, **relative to the previous sample** (files newer than version 110) and in 3ds Max's sign (the rotation is the inverse of the right-handed one): checked against the node's own `*TM_ROW` values and against Assimp's keys. TCB and Bezier keys are read as plain keys (tangents and tension are not used; a note). A track with a single key is not animation and is skipped (Assimp does the same).
- `flatten` (`ImportModel`, one merged static mesh): node transforms and the up turn are baked in, clips dropped.

### Limits

| Limit | Value |
|---|---|
| File size | 512 MiB |
| Block depth | 64 |
| Objects (nodes) / materials | 100,000 / 20,000 |
| Vertices + texture vertices / faces / animation keys | 10,000,000 each |
| Faces taking part in smoothing at one vertex | 256 (a note when exceeded) |
| Numbers | finite, within +-1e15 |

Counts are checked before allocating; every index is range-checked. A missing header, an unclosed block, a bad number in a vertex or transform, a vertex or face index outside its list fail the read, naming the line (a face whose vertex index is outside the vertices is skipped with a note instead).

### Not handled

- Cameras, lights, shapes and splines as objects; vertex colours; extra texture mapping channels; opacity, bump, specular, reflection maps; texture tiling, offset and rotation (counted in a note); ambient and specular colours; the shading mode.
- Skin data (`*MESH_SOFTSKINVERTS` / `*MESH_BONE_LIST` / `*MESH_BONE_VERTEX`, which Assimp reads into bones and weights) is not read; a mesh with it imports as a rigid mesh. None of the 16 real files has any, so this difference is unmeasured.
- Non-uniform scale under a rotated parent has shear that a transform cannot hold: the shear is dropped and counted in a note.
- Smoothing group numbers above 32 are folded into 1..32 (a note).

### Tests and what was checked

- `ase_reader`: the UTF-8, CRLF and UTF-16 versions of one scene, world-to-local vertices, a child under a parent, sub-materials chosen per face, material fields and the path fix, UV flip, smoothing, normals from the file, the animation (ticks, relative rotation in 3ds Max's sign, the target camera's block ignored), every note, malformed input, parent loops, missing parents, repeated names, German decimal-comma locale (skipped when none is installed), a UTF-8 path, the seam, scene import and the merged-mesh path.
- `ase_hostile`: every truncation of all three fixtures, non-finite / overflowing / singular / mirrored / sheared transforms, counts at and over the caps, 100,000 nested blocks, 20,000 objects in a chain, a 5,000-object parent loop, a vertex shared by 3,000 faces with varying smoothing groups, sub-material ids out of range, keys out of order / at negative times / NaN / 200,000 in a track, UTF-16 with a lone surrogate, binary junk, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 150,000 mutations.
- Real files: 16 from an outside corpus (`docs/content/model_corpus.md`; 14 plain, 2 UTF-16, plus one empty file both refuse). Against Assimp built from source (with its 3DS importer, which its ASE code needs): triangle count, surface area, bounding box and clip duration agree on the 14 UTF-8 files, including `MotionCaptureROM.ase` (26 meshes, 2,016 triangles, 45.167 s, 23 animated nodes) and the camera animation files; rotation keys at first and last sample equal to four decimals on the three files compared (`RotatingCube`, `anim2`, `CameraRollAnimWithChildObject`). On the two UTF-16 files Assimp returns an empty skeleton (1 mesh, 8 triangles, area 0); ours reads the full scene, the same as the UTF-8 original. Assimp makes a `.Target` node and channel for target cameras; ours does not.

### Before it can become the default

3ds Max exports from more than one version; Windows run; owner approval. Removing Assimp drops ASE skin data (not read here) and Assimp's camera and light objects (here plain empty nodes).

## Our own B3D reader (`.b3d`)

*Was `docs/own_b3d_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_B3D`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of `.b3d`. Nothing was flipped.

### What it is

B3D is the model format of Blitz3D (and BlitzMax / Blitz3D-derived tools): a tree of chunks (`BB3D`, `TEXS`, `BRUS`, `NODE`, `MESH`, `VRTS`, `TRIS`, `BONE`, `KEYS`, `ANIM`). Written from the public description of the format (Mark Sibly's `b3dfile_specs.txt`) with no code from any other importer. Unlike MD2 and MD3 it has real skeletal animation, which the engine can play.

| File | What |
|---|---|
| `engine/src/Render/B3dReader.cpp` | `okay::ReadB3dBuffer` / `okay::ReadB3dFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp`, `ReaderCommon.hpp` | Shared bounds-checked cursor and number checks |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 20 of the opt-in table |
| `tests/test_b3d_reader.cpp`, `tests/batchc_fixtures.hpp` | Hand-computed checks on a synthetic file (`b3d_reader`) |
| `tests/test_batchc_hostile.cpp` | Hostile input and mutation fuzz (`batchc_hostile`) |

### What it reads

- The chunk tree, walked **with an explicit stack** (no recursion, nesting capped at 10,000), every chunk size checked against its parent. Unknown chunks are skipped by their size and counted.
- **Nodes** (name, position, scale, rotation), parent first. One top-level node is the root; several get a new root named `B3D`.
- **Meshes**: one part per `TRIS` chunk; vertices welded per vertex number; normals; the first texture-coordinate set (as written); the brush as material (diffuse colour and alpha, shininess = file value x 128, first texture, joined to the model's folder); a mesh without normals gets smooth ones.
- **Skin**: every node with a `BONE` chunk is a joint; the four strongest weights per vertex (weights are normalised); the inverse bind matrix maps the mesh's own coordinates to the joint's (the inverse of the joint's rest-pose world matrix times the mesh node's world matrix, the identity in files that follow Blitz3D's rules; the engine skins a vertex as joint world x inverse bind x vertex). Vertices with no weight follow the root node. `BONE` vertex numbers count over the whole file, as the format's notes say.
- **Animation**: the `KEYS` chunks (position, scale, rotation) of each node as one clip, times = frame / frames per second of the `ANIM` chunk (60 when the file has none or a bad rate).
- **Handedness.** Blitz3D is left-handed. Z is mirrored in positions, normals, translations and rotation keys, and every triangle's winding is reversed. A rotation `(w, x, y, z)` in the file becomes `(x, y, -z, w)` (the file's quaternions follow Blitz3D's matrix convention, and the mirror turns it a second time). On the real files this gives the same node rotations and key rotations as Assimp's importer (see below).

### Not handled (each is a line in `out.notes`)

Vertex colours, texture blend modes, flags, positions, scales and rotations (texture transforms), further texture-coordinate sets, brush effects (two-sided, fullbright, ...), the brush's specular, more than one `ANIM` chunk (the first gives the frame rate; Blitz3D files have one), sprites and other node kinds that have no mesh.

### Limits

File 256 MiB; nodes 100,000; vertices 20,000,000; triangles 20,000,000; keys 20,000,000; textures and brushes 100,000 each; nesting 10,000. All counts that come from the file are checked against the bytes left in their chunk before anything is allocated. Positions, scales, colours and key values must be finite and within 1e15; a bad value in a normal or texture coordinate becomes 0 and is counted.

### Tests and what was checked

- `b3d_reader`: **synthetic** file built in memory (a root node with a one-triangle mesh, two bone nodes, weights, keys, an animation), every number computed by hand: the mirror (positions, normals, rotation `(h 0 h 0)` to `(0 h 0 h)`), vertices in the mesh node's coordinates, area 0.5, winding, weights and their normalisation (0.25 to 1.0), the inverse bind of a bone with scale and rotation `(-2, -0.5, 0)`, animation times (frame 30 at 30 per second is 1 s, at the default 60 per second 0.5 s), an unweighted vertex, two top-level nodes, no bones, refusals and **every truncation**.
- `batchc_hostile` (B3D section): every truncation; **every four-byte position of the file set to 0, 1, 3, 255, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFE, NaN, infinity, the largest float and 2^62** (sizes, counts, ids, floats); 30,000 nested nodes; 150,000 flat nodes; a valid 50,000-vertex mesh with 5 bones per vertex (0.2 s); 100,000 mutations. Sanitizers (`address,undefined,float-cast-overflow`): clean. The run found one thing, which is fixed: a brush shininess near the largest float became infinity after the multiplication by 128.

#### Real files

Assimp's tree has 3 B3D files (`models/B3D/WusonBlitz.b3d`; `models-nonbsd/B3D/dwarf2.b3d` and `turtle1.b3d`, both skinned and animated). **All 3 read and equal Assimp v5.4.3 (built from the same sources, only the B3D importer) on mesh count, triangles, surface area, bounding box, node count, clip count and clip length** (15.083 s and 6.25 s). Beyond what the corpus test compares: on `dwarf2` (47 nodes, 14,148 keys) and `turtle1` (25 nodes, 1,724 keys), the **local position, scale and rotation of every node and every key value** equal Assimp's after its own handedness conversion (position keys exactly; rotation to the 4th decimal), **the sum of the skin weights of every bone** and the number of weight entries equal Assimp's (40 and 22 bones with weights, difference 0), and **the inverse bind matrices equal Assimp's bone offset matrices** (largest element difference 4e-6). Assimp keeps three vertices per triangle (5,688 for `dwarf2`), ours welds (1,479).

Not verified: `TEXS` / `BRUS` fields other than the first texture and colours on files with several textures per brush, vertex colours, files from Blitz3D exporters other than the three in the tree, a run on real Windows.

## Our own Blender reader (`.blend`)

*Was `docs/own_blend_reader.md` until 2026-11-20.*

Status: **side by side, off by default.** Assimp (when the build has it, `OKAY_USE_ASSIMP`) is still the only other reader of `.blend`. Without Assimp and without switching ours on, a `.blend` file does not import. Nothing was removed and no default was changed. This is the "write the Blender reader" step of `docs/status/own_code_roadmap.md`, written from the public description of the file layout with no code from any other importer.

### What it is

| File | What |
|---|---|
| `engine/src/Render/BlendReader.cpp` | The reader: `okay::ReadBlendBuffer` / `okay::ReadBlendFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/BlendModifiers.cpp` / `.hpp` | The Mirror and Subdivision Surface modifiers (section "Modifiers") |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 6 of the per-format backend switch (same code as COLLADA, 3DS, BVH) |
| `scripts/gen_blend_fixtures.py` | Writes the **synthetic** fixtures `tests/data/models/blend_*.blend` and the copies in `examples/own_readers` (see "What was and was not tested"); `--check` writes nothing and fails when a committed file differs (ctest `blend_fixtures_check`) |
| `tests/test_blend_reader.cpp` | Hand-computed checks on the fixtures and the seam (`blend_reader`) |
| `tests/test_blend_modifiers.cpp` | Modifiers: hand-computed Catmull-Clark and Mirror results, the fixture, the corpus files with modifiers, a fuzz (`blend_modifiers`, standalone) |
| `tests/test_blend_hostile.cpp` | Hostile input and mutation fuzz, built on its own under sanitizers (`blend_hostile`) |
| `tests/test_model_corpus.cpp` | Real-file corpus rows for reader `blend` (`model_corpus`, needs `OKAY_MODEL_CORPUS`) |

The reader file uses the standard library, `okay/Render/ModelData.hpp` (header-only types) and the in-tree inflate (`okay::Inflate`, a thin wrapper in `Core/ImageDecode.cpp` over the one decoder in `Core/Inflate.hpp`), so the hostile test builds it without the engine library.

### Turning it on

Exactly like `OKAY_OWN_3DS`: CMake `-DOKAY_OWN_BLEND=ON` (default `OFF`); environment variable `OKAY_OWN_BLEND=1` / `0`; `okay::SetImportBackend(".blend", ImportBackend::Own)`; `SetImportBackend("*", ...)` covers every format with an own reader. Off, with no Assimp in the build: `ImportModelData` fails with a message saying how to switch it on and `ImportableExtensions()` does not list `.blend`. On, with Assimp in the build: ours is tried first, Assimp is the fallback for files ours rejects.

### How it reads a file

A `.blend` file is `BLENDER` + pointer size (`_` 4 bytes, `-` 8) + endianness (`v` little, `V` big) + 3 version digits, then blocks: a 4 byte code, a size, the writer's memory address of the block ("old pointer"), the index of the struct the block holds, and an element count. One block, `DNA1`, is the SDNA: every struct the writer knew, with field names, types and order. The reader parses that table, **computes every struct layout from it** (and checks the sum against the file's own struct size), and reads every field **by name** from the file's own layout. It never assumes a Blender version's layout, so the same code reads 2.4x to 4.x files and 32/64 bit, little/big endian ones. Field names that changed between versions are looked up under both names (`totvert`/`verts_num`, `vdata`/`vert_data`, `poly_offset_indices`/`face_offset_indices`, `defbase`/`vertex_group_names`, `size`/`scale`, ...). Old pointers are resolved through a sorted table of block start addresses; a pointer is valid only inside a block, and every field read is checked against the bytes left in its block.

- **Compression**: gzip (Blender 2.5x to 2.9x "Compress File") is read with the in-tree inflate (output cap 768 MiB, size trailer checked). **zstd (Blender 3.0+) is not supported**: a zstd decoder is feasible (FSE and Huffman tables, about 600 lines) but there was no zstd encoder on the build machine to produce test data and a decoder with no real test data was judged worse than none; the file is rejected with a message that says so. The **Blender 5.0 container** (`BLENDER17-01v....`, large block headers) is likewise rejected by name.
- **Objects**: every object block that the current scene (`FileGlobal.curscene`, else the first scene) reaches through its base list (2.7x) or its master collection tree (2.8+), plus their parents; else, with a note, all objects. Local transform = `parentinv * T(loc + dloc) * R * S(size * dscale)`; rotation modes: Euler (all six orders), quaternion, axis-angle. Parent types: object, armature, **bone** (attached at the bone tail like Blender), vertex / curve parents (attached to the parent's origin, named). The result is checked against the **saved `obmat` of every object** and a mismatch is reported (this is how a "track to" camera was found).
- **Meshes**, three storages, chosen from what the file has: (1) `MFace` + `MTFace`/`TFace` (up to 2.6x, and 2.4x); (2) `MPoly` + `MLoop` + `MLoopUV` (2.63 to 3.5, as pointers or custom-data layers); (3) the attribute storage of 3.6+/4.x: `position`, `.corner_vert`, face offsets, `material_index`, `sharp_face`, float2 UV layers. Polygons are triangulated (quads along the diagonal that keeps both triangles facing the polygon normal; larger polygons by ear clipping on the Newell plane, fanned only when a work budget or the 256-corner cap is hit). Normals: per face flat or smooth as flagged; smooth corners get the angle-weighted average of the smooth faces around the vertex. Vertices are welded per (vertex, normal, UV). UV `v` is flipped like the other bottom-up formats. One part per material slot.
- **Materials**: diffuse colour, emission (`emit`), hardness as shininess, metallic / roughness (2.8+), alpha only when the material is set to be transparent; legacy image texture slots (colour, normal, specular maps); node materials: Principled BSDF base colour, metallic, roughness, alpha, emission, the image texture linked into Base Color, and a normal map node's image. Image paths: `//` is relative to the .blend, backslashes become `/`.
- **Armatures**: bones as nodes under the armature object (rest from `arm_mat`, the stored pose channel values applied); vertex groups matched to bones by name give the 4 strongest weights per vertex (normalised); the inverse bind is `inverse(armatureWorld * bone.arm_mat) * meshWorld`.
- **Animation**: the active action of each object (`AnimData.action`): F-Curves on `location`, `scale`, `rotation_euler`, `rotation_quaternion`, `rotation_axis_angle` of the object or of `pose.bones["X"]`. Constant, linear and bezier keys (bezier is evaluated with Blender's handle clamping and sampled at every whole frame; a held key gets a step just before the next); frame rate from the scene; a clip starts at its first key. Channels with a single key are not made (the node transform already is that placement). Each sample is a full local matrix decomposed to position / rotation / scale, so parent inverses and Euler orders come out right.
- **Modifiers**: Mirror and Subdivision Surface are applied, in stack order (next section).
- The root node carries the Z-up to Y-up rotation `(x, y, z) -> (x, z, -y)`; flattened import (`ImportModel`) bakes each object's world matrix and that rotation into the vertices and returns one node and no skins or clips.

### Modifiers

Each mesh object's `modifiers` list is walked; every item is a block whose SDNA struct names its kind (`MirrorModifierData`,
`SubsurfModifierData`, ...), and every setting is read by field name from the file's own layout, like everything else. The
modifiers below are applied to a copy of the mesh, in stack order; the stored mesh (shared by objects using the same mesh)
is not changed. A modifier switched off for the viewport (`ModifierData.mode` without bit 1) is skipped and counted. Every
other kind (Array, Boolean, Solidify, Bevel, Decimate, Multires, ...) is skipped with one note line per kind and name, for
example `modifier 'Array' (Array) not applied (not supported) on 1 object(s)`. The Armature modifier is the skin (read
as weights, as before).

**Mirror** (`MirrorModifierData`): axes from the flag bits X 8, Y 16, Z 32 (files that set none use the old `axis` field);
each axis in turn doubles the mesh (X, then Y, then Z). The copy has its winding reversed so it faces outward. Merge (on
unless flag 128): a vertex whose mirror image is within `tolerance` of it is not copied but moved onto the mirror plane, and
a face lying entirely in the plane is not doubled. A mirror object (`mirror_ob`) makes the mirror plane that object's own
planes (mesh -> mirror object space -> flip -> back). U / V flip (flag 2 / 4): the copy's `u` becomes `1 - u` (plus
`uv_offset` and `uv_offset_copy` when the file has them). Vertex groups (flag 64): the copy's weights move to the group with
the other side's name (`Arm.L` / `Arm.R`, `L_arm`, `left` / `right`). Clipping (flag 1) only matters while editing in
Blender and changes nothing here. Bisect (flags 256 to 1024) is not done (named).

**Subdivision Surface** (`SubsurfModifierData`): Catmull-Clark (`subdivType` 0) or simple (1). Written from the
Catmull-Clark paper (1978) and, for creases, DeRose, Kass and Truong (1998): face points (mean of the corners), edge points
(the two ends and the two face points, / 4), vertex points `(Q + 2R + (n - 3) S) / n`; boundary and non-manifold edges use
the midpoint and boundary vertices `(a + 6 S + b) / 8` (corners with three or more sharp edges stay). Every n-gon becomes n
quads; material and smooth flag are kept per face. The level is the **higher of the viewport and render levels**: that is
what Assimp shows on the corpus (`yxa_1` viewport 3 > render 2, `HUMAN` render 2 > viewport 1; checked by the triangle
counts, which then match exactly). Corners at bit-identical positions are joined before subdividing, so a mesh stored split
along its edges (the corpus Suzanne has 1,198 vertices at 505 positions) is smoothed as one surface; Assimp does the same,
Blender itself would keep the pieces apart (unverified: no Blender was run). UVs: with "subdivide UVs" (old flag 8, newer
`uv_smooth` not 0) they are subdivided by the same rules on their own topology (corners joined by vertex and UV, so seams
are boundaries); without it they are interpolated linearly (2.8+ files: the "UV Smooth" mode, next subsection). Edge
creases: `MEdge.crease` (a byte) or the 4.x `crease_edge` attribute, turned into a DeRose sharpness `s` (the number of
levels an edge stays sharp): crease 1 is sharp at every level; a partial crease `c` gives `s = 10 c^2` in files from
Blender 2.80 on (the OpenSubdiv mapping, from memory, unverified) and `s = c x levels` in older files (our choice, as
before). From 2.90 on creases are used only when the modifier's "use creases" bit (16) is set. The crease rules follow
**DeRose, Kass and Truong (1998)**: an edge with `s >= 1` uses the sharp rule (midpoint) and `0 < s < 1` blends the smooth
and the sharp edge point by `s`; a vertex takes the smooth, crease or corner rule by its number of sharp edges (fewer than
2, 2, more), and when that rule changes between this level and the next it blends the two by the vertex sharpness, the
mean of its creased edges' sharpnesses, clamped to 1 (boundary edges, which are infinitely sharp, are left out of that
mean); each half of a creased edge gets the paper's variable-sharpness (Chaikin) rule `max(0, (s_neighbour + 3 s) / 4 - 1)`,
where the neighbour is the other creased edge at that end (the edge itself when the end has not exactly two). Along a
uniform crease that is `s - 1` per level, the rule we had before; it differs only where creases of different sharpness
meet. (Before this change a vertex clamped each edge to 1 before averaging; the paper averages first: edges 2 and 0.5 give
a full crease, not a 0.75 blend. Both checked by hand in `blend_modifiers`.) Vertex weights: vertex points keep theirs,
edge and face points take the mean of their sources. Output is capped at 4 M subdivision faces per file (a level that would pass it is not done, named).

**Subdivision Surface options of Blender 2.8+.** Each is found by its field name in the file's own `SubsurfModifierData`
(so a file that does not have the field keeps the old behaviour). The SDNA stores struct and field names and types, but
**no enum or flag values**, so no file can confirm what the numbers mean; the values below are from memory of Blender's
DNA headers and are **unverified** (no Blender 2.8+ file with these options was available, and no Blender was run).

| Field (setting) | Values read | What we do |
|---|---|---|
| `uv_smooth` (UV Smooth) | 0 none, 1 keep corners, 2 keep corners + junctions, 3 + concave, 4 keep boundaries, 5 all | UVs are smoothed on their own topology (corners joined by vertex and UV, so a seam is a boundary); the mode says which UV vertices are held where they are: **corner** = on a UV boundary with one face around it; **junction** = its mesh vertex has 3 or more different UVs; **concave** = on a UV boundary with corner angles (in UV space) adding up to more than 180 degrees; **boundaries** = every UV-boundary vertex. 0 = linear UVs. Another value: smoothed everywhere, named in a note. Without the field (up to 2.7x): flag 8 "Subdivide UVs" = all, else linear, as before. These definitions are ours, from the mode names. |
| `boundary_smooth` (Boundary Smooth) | 0 all, 1 keep corners | 1: a boundary vertex with only one face around it is a corner and stays at every level (and at the limit). |
| `flags` bit 64 | set = "use limit surface" OFF | Read only when the file has the `quality` field (2.90+ DNA). Off (the default in Blender): after the last level every point moves to its **limit position**, with the masks of Halstead, Kass and DeRose (1993): smooth vertex of valence n `(n^2 S + 4 sum(edge neighbours) + sum(diagonal corners)) / (n (n + 5))`, crease or boundary `(a + 4 S + b) / 6`, corner stays; smoothed UVs too, with the mode's held vertices. A crease that is still only partly sharp after the last level blends the sharp and the smooth limit by its sharpness (an approximation; named here). |
| `flags` bit 16 | use creases | From 2.90 (or when `quality` is present). |
| `quality` | 1 to 6 | Read, no effect: in Blender it sets the accuracy of its limit evaluation on the GPU; our limit positions are exact for the masks above. |

Tested (`blend_modifiers`): the limit of every vertex is the same whether projected at level 1 or level 2 (cube, open grid
with and without keep corners, all-sharp cube; to 2e-6), the cube's face point goes to 68/81 = 0.839506 (worked by hand)
and plain subdivision approaches it about four times closer per level; each UV mode on fans of quads with one, two, three
and two-unequal UV islands (which centre UVs move and which stay); keep corners on grids. Three synthetic fixtures in the
2.9x / 3.6 / 4.x layouts (`blend_subsurf_293`, `_36`, `_41`, written by `scripts/gen_blend_fixtures.py`) carry the new
fields; the reader's mesh equals the same steps done by hand with the settings named in the script and differs from the
old defaults. They prove the reader reads the fields our script writes, **not** that Blender writes them that way.

How it is built: per level, every corner's edge is found by bucketing corners under their lower vertex index (a counting
sort, then a sort of each small bucket): flat arrays, no hash map, no allocation per element. On the corpus Suzanne at
level 4 (251,904 triangles) the whole read takes about 56 ms against Assimp's 270 ms (`docs/content/own_readers_speed.md`).

### Named, not silent

Everything below is reported once in `ImportedModel::notes` (also logged as a warning) when the file has it: lights, cameras and other object types (kept as empty nodes); modifiers other than Mirror and Subdivision Surface (one line per kind and name), modifiers on objects that are not meshes, modifiers switched off for the viewport, subdivision stopped at the face cap, mirror bisect, a mirror object that is not in the scene; constraints, track-to targets, pose-bone constraints (not evaluated); group / collection instances (not expanded); vertex colours (the engine mesh has no vertex colour channel); auto-smooth angle, custom split normals, sharp edges; shape keys; vertex parents; packed images (name only, pixels not extracted); node materials without a Principled BSDF; pre-2.5 Ipo animation; NLA tracks, drivers, F-Curve modifiers; easing interpolation (treated as linear); F-Curves on any other property; animation of materials / cameras / lights / shape keys; shear in a node transform; unit scale other than 1; meshes with truncated arrays; faces with out-of-range indices; computed world matrix differing from the saved one.

### Limits (hostile files)

| Limit | Value |
|---|---|
| File size / after gunzip | 512 MiB / 768 MiB |
| Blocks / structs / struct fields / names | 4,000,000 / 20,000 / 4,000 / 200,000 |
| Items followed in one list | 1,000,000 |
| Objects / bones / materials | 100,000 each |
| Vertices / faces / corners in one mesh | 20 M / 20 M / 60 M |
| Output vertices / parts | 40 M / 200,000 |
| F-Curves / keys per curve / output keys | 400,000 / 4 M / 20 M |
| Faces made by Subdivision Surface, whole file | 4 M (a level that would pass it is not done) |
| Modifiers followed per object | 1,000 |
| Whole frames sampled per animated node | 40,000 |
| Bone and collection depth, parent chain | 512, 64, 512 |
| Ear clipping | 256 corners, 300 M inner steps per file, then fan |
| Coordinates, keys | finite and within +-1e15 |

Counts are checked against the bytes in their block before anything is allocated; cycles in parent chains are cut and named; bone and collection recursion is iterative or depth-capped.

### What was and was not tested

**Real files** (28, from the `test/models/BLEND` and `test/models-nonbsd/BLEND` directories of an Assimp v5.4.3 checkout, `docs/content/model_corpus.md`): Blender 2.41, 2.48, 2.49, 2.50 / 2.52 (gzip), 2.60 (big endian, 32 bit), 2.62, 2.69, 2.71, 2.76, 2.79, 2.93; 64-bit little endian, plus one 32-bit big-endian file. **No real Blender 3.x or 4.x file was available.** The 3.6+/4.x attribute storage (`position`, `.corner_vert`, face offsets, `sharp_face`, float2 UV, numeric custom-data type ids 11, 45, 47, 48, 49, 50) is written from memory of Blender's DNA headers and is covered only by the **synthetic** fixtures, which were written by `scripts/gen_blend_fixtures.py` and not by Blender: they prove the code agrees with the script's reading of the layout, not with Blender.

- All 28 files read (Assimp: 27; it fails `BlenderDefault_262.blend`). Compared with Assimp built from the same sources (only its Blender importer): triangle position sets, UV sets, bounding boxes, areas and material colours match on 22 of the 28 files (every corner of ours has the same position and UV in Assimp's output; vertex counts differ because Assimp does not weld). The remaining six are the modifier / dropped-object differences below, plus the file Assimp cannot read. Diffuse colours, shininess and texture names were compared by eye on eight files with materials and agree (paths differ only in `//` resolution); `BlenderMaterial_269` differs in alpha (below).
- **Modifiers, against Assimp** (counts ours / Assimp's): `SuzanneSubdiv_252` (Subsurf level 4) 251,904 / 251,904 triangles; `yxa_1` (Subsurf, viewport 3) 115,712 / 115,712; `HUMAN` (Mirror X with merge, then Subsurf render level 2) 8,560 / 8,560. Bounding boxes equal to the printed 4 digits on all three. Vertex counts differ because these meshes are flat shaded (ours splits vertices per face normal, Assimp uses smooth normals; see below). On the Suzanne, 85 of 2,010 points at level 1 differ (up to 0.02): exactly the 42 vertices and 42 edge points on the open boundaries (the eye holes). Assimp keeps boundary vertices where they are and puts a boundary edge point at the mean of its two ends and its one face point; that is not the Catmull-Clark boundary rule, which ours follows (checked by writing both rules in a scratch script and matching Assimp's points). `MirroredCube_252` 108 / 36: the Mirror has all three axes set and a mirror object named `CenterOfTheWorld` at (0, 0.495, 0); ours makes the eight copies around that object's planes (12 + 96 triangles, box -5.1..5.1 by -4.95..4.95 by -5.76..4.77); Assimp adds one copy that stays inside the original box, which matches neither the three axes nor the mirror object. Before this change ours showed the stored meshes only (968, 1,808, 267 and 24 triangles).
- Other differences, all understood: Assimp **loses three of the ten cubes** of `CubeHierarchy_248` (all ten are on the visible layer; ours has all); Assimp uses the stored smooth vertex normal even on flat-shaded faces, ours honours the face flag (the default cube is flat); on smooth meshes our normals agree with Blender's own stored vertex normals on 100 % of corners for the simple meshes and 92 to 98 % on the two armature files (stored normals include auto-smooth / edge splits); Assimp reports diffuse alpha 1.0 for `BlenderMaterial_269`, whose alpha is 0.01 with the transparency bit set in `mode` (ours reports 0.01; the bit meanings are from memory of the DNA headers); Assimp has no armatures or animation at all.
- Ground truth from the files themselves: the saved world matrix of every object equals our computed one in 27 of 28 files (the exception is a camera aimed by an old-style track-to, now named); for `fleurOptonl.blend` all 71 pose bones equal Blender's own stored `pose_mat` to 1e-5; in `Bob.blend` 31 of 33 differ because two bones have constraints that we do not evaluate (named). `arm_head` equals the translation of `arm_mat` for all 104 bones.
- **Animation was not checked against any real animated file**: the corpus has one trivial one-key action and the armature files carry pre-2.5 Ipo animation (named, not read). F-Curve evaluation (bezier with flat handles, linear quaternion, whole-frame sampling, time base) is covered by the synthetic fixture only.
- `blend_reader` (engine linked): four encodings of the attribute fixture (4.1 names, 3.6 names, big endian + 32 bit, gzip) must give identical meshes; hand-computed world positions, concave polygon area, UV flip, materials, collections, orphan object excluded, skin weights, inverse binds, bezier values (1.04 at frame 3, 5.0 at frame 6), flatten, the seam, container errors.
- `blend_modifiers` (standalone, no engine library): Catmull-Clark on a cube at level 1 gives 24 quads and exactly the 26 hand-computed points (corners 5/9, edge points 3/4, face centres), 96 quads and 98 vertices at level 2, the face cap, simple subdivision, an open quad's boundary rule, creases (all sharp keeps the cube; sharpness 0.5 blends to 0.875), linear and seam-aware smooth UVs, Mirror merge (onto the plane) and no merge, three axes, a face in the plane, a mirror object, U flip, vertex group swap, weights through subdivision, the left / right name swap; the fixture `blend_modifiers.blend` (open box, Mirror X with merge and U flip, an Array that must be named, Subsurf level 1, one creased edge) through the reader against the same steps done by hand (80 triangles, every point equal); the four corpus files above when `OKAY_MODEL_CORPUS` is set; and a fuzz of 3,000 random meshes (repeated corners, non-manifold edges, loose vertices, 1e12 coordinates, random creases and weights) through Mirror and Subdivide. Clean under ASan + UBSan + float-cast-overflow.
- **Fixture drift (fixed 2026-11-16).** The script had stopped reproducing the four `blend_attr_*` files byte for byte. Cause:
  Python 3.12 made `sum()` of floats compensated (Neumaier), which changed one float of the Child object's saved matrix
  (`-1.2e-16` instead of `0.0`), and Python 3.13's `gzip.compress` writes OS byte 255 where the committed gzip file has 3.
  The script now adds matrix products left to right and writes the gzip header itself; it reproduces every committed
  fixture exactly again (the committed files did not change), and `--check` (ctest `blend_fixtures_check`) fails on drift
  (a gzip file whose deflate bytes differ only because of another zlib is accepted when it inflates to the same bytes).
- `blend_hostile` (standalone, no engine): every truncation of every fixture; bad headers; a block size of -1 and 2 GB; SDNA claiming billions of names or structs, bad type indices, arrays of 2^31 and 99999x99999 elements, mismatching struct sizes; an object whose parent is itself; a 40 MiB stored-block gzip; every block header field set to boundary values; pointer scrambling; then a deterministic mutation fuzz (byte flips, length stomps, inserts, deletes, inf / NaN / 2^31 / 2^63 floats). Built with `-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all`: clean, including 60,000 mutations of the fixtures and 25,000 mutations of ten real files (`OKAY_BLEND_FUZZ_ITERS`, `OKAY_BLEND_FUZZ_REAL_ITERS`), slowest single read 0.05 s. (One finding: a mutated bone matrix gave a non-finite inverse bind; fixed and named.) With the modifiers (2026-11-16) the fuzz also mutates `blend_modifiers.blend`, `MirroredCube_252` and `SuzanneSubdiv_252`; run under ASan + UBSan with 20,000 fixture and 3,600 real-file mutations: clean after two fixes it found (the modifier code met loops of faces the reader had dropped, now compacted first; and, in the 3.6+ face-offset path that predates the modifiers, an offset near 2^32 wrapped a 32-bit sum into a negative face start, now compared in 64 bits). Slowest single read under ASan 8.6 s (a mutated subdivision level), with the face cap then at 8 M; it is 4 M now. With the 2.9+ options (2026-11-16) the three `blend_subsurf_*` fixtures were added to the fuzz; ASan + UBSan, 20,000
fixture and 3,600 real-file mutations: clean, slowest single read 14.7 s under ASan on a shared, loaded machine. The
`blend_modifiers` fuzz now also draws the UV mode, keep corners, limit surface and use creases: 20,000 random meshes
under ASan + UBSan, clean.

### Not handled

Everything in "Named, not silent", plus: zstd and Blender 5.0 files; library-linked data from other .blend files; layer, visibility and hide flags (all objects of the scene are imported); curves, text, metaballs, lattices, point clouds, volumes, grease pencil; dupli / particle instancing; bone inherit-rotation / scale flags, B-Bone segments, IK; non-Principled shader nodes; texture coordinate nodes and mapping; images packed in the file; per-vertex delta rotations in non-Euler modes; multiple scenes (only the current one); sub-frame times; custom split normals; the separate render-only level choice of the Subdivision Surface (we always use the higher level), mesh creases on UVs
(UV smoothing ignores edge creases), OpenSubdiv's exact results in 2.8+ (ours is the textbook Catmull-Clark plus the rules
above, which they are meant to equal, unverified), edge creases stored in Blender 3.5 / 3.6 files (if they are kept in a
layer that is neither the `MEdge` struct nor the 4.x `crease_edge` attribute, they are not read; unverified which), modifiers applied to animated shapes; Blender versions after 4.x not seen (field renames not in the alias lists would show as a missing mesh and a note).

### Before it can become the default

The default stays off until: a real Blender 3.x and 4.x file has been read and compared (the largest gap), animated real files have been checked, the Windows distribution has run it, zstd is either supported or accepted as a documented limit, and the owner approves. After that `ModelImportAssimp.cpp` can lose its Blender role (`docs/status/own_code_roadmap.md`).

## Our own BVH reader (`.bvh`)

*Was `docs/own_bvh_reader.md` until 2026-11-20.*

Status: **side by side, off by default.** Until now `.bvh` only imported in a build with Assimp. Assimp builds a stick-figure mesh out of the skeleton; ours reads the skeleton and the motion and makes no mesh. Nothing was removed. This is the "BVH animation reader" step of `docs/status/own_code_roadmap.md`.

### What it is

Written from the public description of the Biovision hierarchy text format.

| File | What |
|---|---|
| `engine/src/Render/BvhReader.cpp` | `okay::ReadBvhBuffer` / `okay::ReadBvhFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 5 of the backend switch); `BuildSceneFromModel` also accepts a model with nodes and clips but no mesh |
| `scripts/gen_bvh_fixtures.py` | Writes `tests/data/models/walk.bvh`, `walk_crlf.bvh`, `two_roots.bvh` |
| `tests/test_bvh_reader.cpp` | Hand-computed checks and the seam (`bvh_reader`) |
| `tests/test_3ds_bvh_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`3ds_bvh_hostile`) |

### Turning it on

Same rules as the 3DS reader: `-DOKAY_OWN_BVH=ON`, environment variable `OKAY_OWN_BVH=1` / `0`, or `SetImportBackend(".bvh", ImportBackend::Own)` (the run-time choice wins). Off by default. With Assimp built, Assimp stays the default `.bvh` reader and ours is the choice when asked for.

### What it reads

- **Hierarchy**: `ROOT`, `JOINT` (names may contain spaces; a `{` may follow the name on the same line), `OFFSET`, `CHANNELS` (up to 24 per joint; `Xposition`, `Yposition`, `Zposition`, `Xrotation`, `Yrotation`, `Zrotation`, any case), `End Site`. Line breaks may be `\n`, `\r\n` or `\r`. Every joint becomes a node (`position` = `OFFSET`, no rotation, scale 1). End sites are not nodes (they are counted in the notes). A file with several `ROOT` joints gets a synthetic parent node `Skeleton`.
- **Motion**: `Frames:`, `Frame Time:` and one row of values per frame. One clip, `motion`, with a channel per joint that has channels: a position key per frame when the joint has position channels (`OFFSET` plus the channel values), a rotation key per frame when it has rotation channels. Times are `frame * frame time`.
- **Rotation order**: channels are composed in the order listed, the first listed outermost (`Zrotation Xrotation Yrotation` is `Rz * Rx * Ry` applied to column vectors), angles in degrees, positive angles counter-clockwise about each axis. This was checked against another importer's output on real files (below).
- **Scene import** (`ImportModelScene`) builds a hierarchy of empty objects with a `ModelAnimator` holding the clip, no mesh. The merged-mesh path (`ImportModel`) has nothing to merge and reports failure.

Units are kept as written (BVH has none); no rescaling and no axis change is done: the usual Y-up right-handed convention of the common exporters is assumed.

### Limits

| Limit | Value |
|---|---|
| File size | 512 MiB |
| Joints | 10,000 |
| Joint nesting depth | 256 |
| Frames | 1,000,000 |
| Samples (frames x channels) | 60,000,000, and never more than the text can hold (every value is at least two bytes), checked before allocating |
| Output keys | 30,000,000 |
| Values (offsets, channels) | finite, within +-1e15 |
| Frame time | greater than 0 and at most 1e6 s |

No recursion (joints use an explicit stack). A bad channel count or name, a channel line that disagrees with its count, a second `CHANNELS` line, an unclosed joint, a missing `MOTION`, too few values, `nan` / `inf` / hex numbers, text glued to a number all fail the read with a one-line message.

### Not handled

- No mesh, no skin: a BVH file has none. Another importer's stick figure is not reproduced.
- No resampling: one key per frame per channel. The engine's clip optimiser runs afterwards (`NormalizeImportedClips`).
- Rotation channels are applied as given; a file whose angles are clockwise, or whose axes differ from the common convention, plays mirrored.
- `End Site` offsets, `Frame Time` given with a comma decimal and files with several `MOTION` blocks are not read.
- A joint with more than three rotation channels or the same rotation channel twice is composed as listed without complaint.
- Not benchmarked beyond: a 3.4 MB file with 4,511 frames reads in about half a second.

### Tests and what was checked

- `bvh_reader`: both line-ending variants, joint names with spaces, parents, offsets, channel order (`Zrotation` before `Xrotation`), rotation-only and position-only joints, two roots, a brace on the name line, a file with no frames, `\r`-only breaks, eleven kinds of malformed input, the seam and the scene import.
- `3ds_bvh_hostile`: every truncation of every fixture, frame counts that claim a million rows, 100,000-deep and 20,000-wide skeletons, over-long names, non-finite and overflowing numbers, NUL bytes, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 150,000 mutations.
- Real files: three motion capture files from an outside corpus (`docs/content/model_corpus.md`), 2,752 to 4,511 frames, 20 to 30 joints, channel orders differing between files. Against Assimp built from source, on two of the three files (01_01 and Boxing_Toes) the root's position and rotation keys, other joints' rotation keys and the duration agree to four decimals at the first and last frame; the third file was checked by frame count times frame time (22.925 s, 37.583 s, 30.680 s for the three).

### Before it can become the default

Run on motion capture from more than one source; Windows run; owner approval. There is nothing else for a BVH importer in the roadmap to replace: removing Assimp drops its stick-figure mesh for `.bvh`, which is announced with the other dropped formats.

## Our own COB reader (`.cob`)

*Was `docs/own_cob_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_COB`, slot 35 of the table in `ModelImport.cpp`). Assimp (when built) is the other reader. Nothing was flipped.

### What it is

COB is the Caligari trueSpace scene format, in a binary ("B") and an ASCII ("A") flavour with the same chunks. Header: 32 bytes `Caligari V00.01<A|B>LH`. Then chunks: type (4 letters), version, id, parent id, size, data. Binary data is little endian.

| File | What |
|---|---|
| `engine/src/Render/CobReader.cpp` | `okay::ReadCobBuffer` / `okay::ReadCobFile` |
| `engine/src/Render/ReaderCommon.hpp`, `PolygonTriangulate.hpp`, `TextReaderUtil.hpp`, `BinReader.hpp`, `SceneRefPath.hpp` | Shared mesh builder (welding, smoothing), triangulation, line reading, folder rule |
| `tests/test_cob_reader.cpp`, `tests/e3_fixtures.hpp` | The test (`cob_reader`) |
| `tests/test_own_dispatch_e3.cpp`, `tests/test_own_default_e3.cpp` | The seam and the build default |

### What it reads

- `Grou` (group) and `PolH` (polygon object): name (with the duplicate number), the stored world matrix (3 rows of 4 numbers), and for `PolH` the vertex list, the texture-vertex list and the faces (`<vertex, texture vertex>` corners, a material number). Each object is a node; its **stored world matrix is made relative to its parent's** (checked on the real molecule: the nitrogen atom and its three hydrogens only touch when the matrices are taken as world matrices).
- Faces: triangles, quads and larger polygons (convex fanned, concave ear clipped). One part per (object, material). Texture coordinates per corner, so seams are welded per (vertex, texture vertex).
- `Mat1`: material number, colour, alpha and the facet setting (`faceted` = flat, `smooth` = fully smooth, `auto N` = N degrees) which sets the smoothing of the part's normals; `ShBx`: the `file name` of a "texture map" shader, resolved inside the model's folder (a full path from the artist's machine is reduced to its file name; an absolute path or `..` is never followed).
- **Handedness**: trueSpace is left-handed and Z-up. y and z are swapped (a reflection: it turns left-handed into right-handed and Z-up into Y-up at once), and the winding is kept as written. On the real molecule file every triangle then faces outward. This is an inference from the data and from what I know of trueSpace, not a checked statement about it.
- Texture v is flipped (1 - v), assuming trueSpace's v runs up. **Unverified.**

### Not handled (lines in `out.notes`)

Animation channels (`Chan`: object keyframes, and the per-vertex ones in `dwarf.cob`), bones and characters, cameras, lights, splines, the unit setting, ambient / specular / exponent / refraction, transparency, reflectance and displacement shaders, texture repeat and offset, bitmaps (the thumbnail), faces with non-zero flags are read as plain polygons (hole flags are **not** interpreted: unverified), objects with a mirroring matrix (winding not corrected). Big-endian files (header `...HL`, if they exist) are refused; none was available.

### Limits

File 128 MiB. Per file: 100,000 objects, 2,000,000 vertices, 2,000,000 texture vertices, 500,000 faces, 2,000,000 face corners, 1,000,000 chunks (binary and ASCII), at most 1,000 distinct texture names looked up on disk, **500,000 triangles**, 4,096 corners per face, 100,000 materials. Counts are checked against the bytes really there (binary) or the lines really there (ASCII), and against the budgets, before anything is sized from them.

### Tests and what was checked

- `cob_reader`: the same hand-built scene in ASCII and binary (a group and a polygon object, a quad and a triangle, two materials, a texture; node positions and scales, areas 1 and 0.5, the flat normal, a hand-computed texture coordinate, colours and alpha), CRLF line ends; refusals; binary counts at `0x7FFFFFFF`, negative and 65,535-corner faces; ASCII counts that lie; NaN and out-of-range numbers; cyclic, missing and self parents (ASCII and binary); texture paths (`../..`, absolute, nothing left after dropping the folder); truncation at every length of both files; 4,000 random byte flips of each; each budget; the worst file the budgets allow (500,000 triangles, 19 MB of ASCII): **peak 240 MB beyond the input, 2.0 s** (the test fails above 480 MB). Hostile work: 100,000 materials with 300,000 shader chunks (25 MB) now read in 0.3 s (the first version searched every material for every shader chunk: 108 s), binary the same in 0.05 s; 1,900 concave 256-corner faces in 0.3 s (ear clipping, which is cubic, is only used up to 48 corners, larger polygons are fanned). A zero-scale parent cannot be undone: the child keeps its world matrix and a note says so.
- **Real files** (the 8 `.cob` files of Assimp's test tree, not in this repository; read when `OKAY_E3_SAMPLES` names a folder with them): all 8 read. Binary and ASCII of the same model agree on parts, triangles and nodes: molecule 4 parts / 896 triangles / 6 nodes, spider 4 / 1,312 / 2 (56 degenerate triangles skipped in both), dwarf 2 / 1,896 / 3. The molecule's triangles all face outward. **Not verified**: how any of it looks in the renderer, the texture v flip, the left-handed assumption on a file with a clear front, hole flags, big-endian files, other trueSpace versions.

### Provenance

Where each fact came from (rule 19a). **From memory** (general knowledge of the format, not a document read during the work): that a COB file has a 32 byte header `Caligari V00.01<A|B>LH` and chunks named `Grou`, `PolH`, `Mat1`, `ShBx`, `Unit`, `Chan`, `END `, and the ASCII keywords (`Name`, `center`, `x axis`, `Transform`, `World Vertices`, `Texture Vertices`, `Faces`, `Face verts N flags F mat M`, `mat#`, `shader:`, `rgb`, `alpha ka ks exp ior`). **Settled by a hex dump and a text view of the 8 sample files** (and nothing else): the chunk header (type, version, id, parent id, size = 20 bytes), the object chunk layout (short duplicate count, short name length, name, 48 bytes of centre and axes, 48 bytes of 3x4 matrix, counts and arrays), the face record (1 byte flags, 2 bytes corner count, 2 bytes material, then 8 bytes per corner), `Mat1` (short material number, shader and facet characters, 1 byte facet angle, rgb and alpha, ka, ks, exp, ior as floats), `ShBx` (class byte, short-length name, parameter count, parameters with type codes 3 float, 6 bool, 7 colour, 10 string, flags; the parameter named `file name` holds the texture), and that the stored matrices are world matrices (the molecule's atoms touch only that way). The type code 4 (read as a 4 byte integer) and the hole meaning of face flags were **not** seen in any sample and are guesses. There is no document we can cite for the format. No importer source was read, copied or ported; the Assimp test tree was open in the scratchpad and only its data files were copied out.

## Our own CSM reader (`.csm`)

*Was `docs/own_csm_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_CSM`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of CSM files. Nothing was flipped. **Checked on one real file (against numbers taken from the file by a separate script, not against Assimp), never on real Windows.**

### What it is

CSM is the optical motion-capture text export of Character Studio (3ds Max) and of capture systems such as Vicon: a header of `$` lines, the **names of the markers**, and one line per frame with the 3D position of every marker. There is no mesh and no skeleton: **markers only, like BVH without the joints' rotations.** The reader gives the markers as empty nodes and the motion as one clip of position keys.

| File | What |
|---|---|
| `engine/src/Render/CsmReader.cpp` | `okay::ReadCsmBuffer` / `okay::ReadCsmFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/TextReaderUtil.hpp`, `ReaderCommon.hpp` | Shared pieces: line reading, encoding fix, strict locale-independent number parsing |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 30 (extension `.csm`) |
| `tests/test_csm_reader.cpp`, `tests/batche_fixtures.hpp` | Hand-computed checks on synthetic files (`csm_reader`) |
| `tests/test_batche_hostile.cpp` | Mutation fuzz, the worst allowed file (time and memory), the real file (`batche_hostile`) |
| `tests/test_own_dispatch_e.cpp` | The import seam routes `.csm` to this reader (`own_dispatch_e`) |

### What it reads

- `$Filename`, `$Date`, `$Time`: skipped. `$Actor name`: named in `notes`. `$Comments`: the following lines are skipped. `$FirstFrame n`, `$LastFrame n`: only used to warn when they do not match the number of frame lines. `$Rate r`: frames per second (a number above 0 and up to 100,000, else the file is refused; without a `$Rate`, 30 is assumed and noted).
- `$Order`: the marker names, separated by white space, on this line and the following ones until the next `$` keyword. Names must be unique and at most 64 characters (a clip names its nodes, so a duplicate would be ambiguous).
- `$Points`: one line per frame: the frame number, then `x y z` for every marker in `$Order` sequence. A line is used only if it holds **exactly** `1 + 3 * markers` plain decimal numbers (no `nan`, no `inf`, no hex); otherwise it is skipped and counted (missing markers are **not** interpolated). A frame number that does not increase over the last used one is skipped and counted. Key time = `(frame - first frame) / rate` seconds.
- Result: root node `CSM` (renamed `CSM_root` if a marker has that name), one child node per marker positioned at its first-frame position, and one clip `csm` with a position key per used frame and marker. **Units are kept as written** (the sample file is in millimetres; head markers are at about 1,600). **The root node turns Z-up into Y-up**, because optical capture data is Z-up (in the sample the head markers' z is about 1,650 and the feet's about 60). Both are assumptions from that file, said in `notes`.
- UTF-8 (with or without a byte order mark) and UTF-16 with a byte order mark; lines end with `\n`, `\r\n` or `\r`.

### Not handled

Marker visibility flags or "missing" codes (if the format has any: none in the sample; a line with such a token is skipped); per-marker rotations (the format has none); a second `$Order` or `$Points` section (refused); unknown `$` keywords (counted and ignored; a `$` keyword after `$Points` ends the frame lines).

### Limits

File 128 MiB; at most **1,024 markers** and **2,000,000 position keys** (markers times frames); names up to 64 characters. A frame line is parsed in place and nothing is stored until the whole line is good, so a line with a million tokens costs no allocation. The worst allowed file (1,000 markers, 2,000 frames, 25 MB) reads in about 0.3 s and holds about 31 MB at once (`batche_hostile` fails above 96 MB or 2 s).

### Tests and what was checked

- `csm_reader` (311 checks): a synthetic file with three markers over two `$Order` lines and three frame lines with a gap in the frame numbers (frames 1, 2, 4 at 50 per second: key times 0, 0.02, 0.06), with hand-computed positions per marker and key, node positions, root rotation, in `\n`, `\r\n` and `\r` forms; the first / last frame mismatch note present and absent; UTF-16 and UTF-8 with a byte order mark; no `$Rate`; skipped lines (too short, `x`, `inf`, too long, backwards); a marker named like the root; about 15 refusal cases (empty, `$Points` before `$Order`, no `$Points`, no frame line, duplicate names, a second section, bad rates including `nan` and `1e99`, a name of 65 characters, odd-length UTF-16, 1,025 markers, the key budget); exactly 1,024 markers still reads; every truncation; a line with a million tokens.
- `batche_hostile`: mutations of two forms of the fixture, the worst allowed file and one frame over the budget, the real file.
- **Mutation checks:** time `* rate` instead of `/ rate` (19 failures), key budget removed (1), the backwards-frame check removed (2).

#### Real file

`ThomasFechten.csm` (a Vicon IQ capture, July 2008, "free for any purpose" by its own note) from the `test/models/CSM` folder of an Assimp 5.4.3 checkout (`docs/content/model_corpus.md`; not in this repository). Read as data only. 43 markers (`LFHD` ... `RMT5`), 2,439 frames at 120 per second; every frame line has exactly 130 numbers (counted by a separate script). The reader gives 44 nodes (root and 43 markers), one clip, 43 x 2,439 = 104,877 keys, no skipped line, the first key of the first marker `(26.327553, 157.341293, 1656.915527)` at time 0 and the last at 2438 / 120 s. **Not compared with Assimp's output.**

### Provenance (what this file was written from)

`CsmReader.cpp` was written from the byte-level and line-level analysis of the one sample file, and from my knowledge of the Character Studio `.csm` header keywords (`$Order`, `$Points`, `$Rate`, ...), recalled and then checked against that file; I found no written specification to follow line by line. No importer source (Assimp or any other project) was read, copied, ported or followed, and Assimp was not run on this file either.

Unverified: files from other capture software or from Character Studio itself (any marker "missing" convention), other frame-number styles, a run on real Windows.

## Our own DXF reader (`.dxf`)

*Was `docs/own_dxf_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_DXF`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of `.dxf`.

### What it is

Written from the public description of the DXF group-code text format, with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/DxfReader.cpp`, `engine/include/okay/Render/DxfReader.hpp` | `okay::ReadDxfBuffer` / `okay::ReadDxfFile` |
| `engine/src/Render/ReaderCommon.hpp`, `PolygonTriangulate.hpp` | Shared number parsing, smooth normals, ear clipping |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 14 of the batch-B table |
| `tests/test_dxf_reader.cpp` | Hand-computed checks on synthetic files and the seam (`dxf_reader`) |
| `tests/test_batchb_hostile.cpp` | Hostile input and mutation fuzz (`batchb_hostile`) |

### What it reads

ASCII DXF only (a binary DXF is refused with a message). The file is read as group code / value line pairs; code 999 comments are skipped; `\n` and `\r\n` line ends both work.

- **3DFACE**: triangles and quads (a fourth corner equal to the third is a triangle). Equal coordinates of different 3DFACEs are welded so smooth normals work across them.
- **POLYLINE** polyface meshes (flag 64: vertex records with flags 128 + 64 are points, records with flags 128 are faces of 3 or 4 vertex numbers, 1-based, a negative number is a hidden edge and counts as visible) and M x N polygon meshes (flag 16, closed in M with flag 1 and in N with flag 32). A `POLYLINE` that lost its `SEQEND` does not swallow the next entity. 2D and 3D line polylines are not surfaces.
- **MESH** (AC1021 and later): the vertex list (92 + coordinates) and the face list (93 + `n, i1 .. in` groups, polygons triangulated).
- **Blocks**: the `BLOCKS` section, placed by **INSERT** with the position, `41/42/43` scale and `50` rotation about Z (the block's base point is subtracted first); nested up to 8 deep, 100,000 inserts; content on layer `0` takes the INSERT's layer.
- One node and one part per **layer** (the layer name is the node name). Smooth normals with a 45 degree crease angle. **Axes**: DXF world coordinates are Z-up, so the root node carries the Z-up to Y-up rotation `(x, y, z) -> (x, z, -y)` (and `ImportModel`'s flattened path bakes it in): Assimp's DXF importer does the same, checked on the real files below. A DXF written by a Y-up tool (LightWave, for example) therefore comes out turned, exactly as with Assimp. No unit conversion (`$INSUNITS` is not read).

### Not handled (each is also counted by entity name in `notes`)

`LINE`, `POINT`, `CIRCLE`, `ARC`, `LWPOLYLINE`, 2D / 3D line `POLYLINE`, `TEXT`, `SPLINE`, `HATCH`, `DIMENSION`, `3DSOLID` / `BODY` / `REGION` (ACIS data), `VIEWPORT` and every other entity are skipped and listed; a file with no surface geometry fails with a message that names what it has. Also: colours (`62`, `420`: layers become nodes, there are no materials), `INSERT` arrays (only the first copy is placed) and extrusion directions other than +Z, tables (`LAYER` colours and `LTYPE`s), the header.

### Limits

File 512 MiB; entities, vertices and triangles 20,000,000; layers 10,000; blocks 100,000; INSERT entities 100,000, nested 8 deep, 200,000 block placements after nesting is expanded; coordinates finite within +-1e15 (also after INSERT transforms); a group code is at most 6 digits. Every number goes through `NumParse.hpp`.

### Tests and what was checked

- `dxf_reader` (`tests/test_dxf_reader.cpp`): **synthetic** ASCII DXF text: `3DFACE` triangles and quads on one and two layers (layer nodes), welding and smoothing across faces (a coplanar pair shares 4 vertices, a 90 degree fold stays at 6), CRLF line ends, a missing layer, polyface meshes (hidden-edge indices, a face naming a missing vertex, a polyline without `SEQEND`), 3 x 2 polygon meshes open and closed, `MESH` with a quad face and one that claims 1,000 vertices, blocks with a base point and an `INSERT` with scale and a 90 degree rotation computed by hand, mirrored and nested inserts (a block that inserts itself stops at the nesting limit), unknown blocks, arrays and extrusion directions (noted), the notes for `LINE`, `CIRCLE`, `TEXT` and colours, refusals (binary DXF, not DXF, a file that ends after a code, `abc`, `1e999`, `1e300`), a decimal-comma locale, the Z-up root with and without `flatten`, and the seam.
- `batchb_hostile` (DXF section): every truncation, bad numbers and counts (`1e999`, `-1`, `4000000000`, `20000001`, ...) in `3DFACE`, `INSERT`, `MESH` and `POLYLINE`, face indices out of range, a polyface of 50,000 vertices and 50,000 faces, `INSERT` bombs (20 levels of 10 inserts, 7 levels of 12: the 200,000 placement cap and the nesting limit stop them), 100,001 inserts, 5,000 layers, a 5 MB layer name, random binary, 2 MB of blank lines, then the mutation fuzz. Sanitizers: clean (this run found two things that are fixed: a `double` outside `int` range cast for a polyline flag, found by `float-cast-overflow`, and an `INSERT` expansion that was bounded only by the output size).

#### Real files

Assimp's tree has 5 DXF files (four in `models/DXF`, `rifle.dxf` in `models-nonbsd`). **All 5 read, and Assimp reads the same 5. 4 agree on everything compared** (triangles, surface area, bounding box, materials): `issue_2229.dxf` (polyline meshes inside blocks, 2,058 vertex records), `lineTest.dxf` (a `3DFACE` pair plus a `LINE`; `999` comments before the first section), `wuson.dxf` (a polyface in a block placed by an `INSERT`, 6,937 vertex records), `rifle.dxf` (572 `3DFACE`s). The fifth, `PinkEggFromLW.dxf` (288 `3DFACE`s), has the same area and box, and ours has 528 triangles to Assimp's 576: the 48 extra are the zero-area triangles of the pole quads, which ours drops (the readers of OBJ, PLY and COLLADA do the same). **Axes**: Assimp turns every DXF Z-up to Y-up as `(x, y, z) -> (x, z, -y)`; ours does the same and the boxes are equal on all five (the file does not say which axis is up; both readers assume Z-up, so a Y-up model such as `PinkEggFromLW.dxf` comes out turned in both).

Not verified on real files: `MESH` (no real file has one; synthetic only), polygon meshes (flag 16), `INSERT` scale and rotation (one real insert, unscaled), colours, files from AutoCAD itself (the five come from other tools).

## Our own FBX reader (`okay::fbx`)

*Was `docs/own_fbx_reader.md` until 2026-11-20.*

Status: **side by side, off by default.** ufbx (`engine/third_party/ufbx`) is still the default FBX reader and is still compiled in. Nothing has been removed. This follows stage 2 of `docs/status/own_code_roadmap.md`: our own backend behind the existing seam, selectable, and promoted only after it has earned it.

### What it is

Written from the public description of the FBX format, with no code from ufbx, the FBX SDK or Assimp.

| File | What |
|---|---|
| `engine/include/okay/Render/FbxReader.hpp` | Public API: `okay::fbx::Parse`, `Build`, `LoadFile`, `Inflate`, the `Scene` result structs, the on/off switch |
| `engine/src/Render/FbxReader.cpp` | Our own zlib/deflate decoder, binary FBX 7.x reader (32-bit and 64-bit record offsets), ASCII FBX reader |
| `engine/src/Render/FbxScene.cpp` | Element tree to scene: nodes, transforms, meshes, skins, blend shapes, materials, textures, baked animation |
| `engine/src/Render/ModelImport.cpp` | The adapter (`ImportFbxSceneGraphOwn`, `ImportFbxMergedOwn`): builds the same GameObjects, `MeshRenderer`, `SkinnedMesh`, `BlendShapes` and `ModelAnimator` clips the ufbx path builds |
| `scripts/gen_fbx_fixtures.py` | Writes the fixtures in `tests/assets/fbx/` (binary 7.4, binary 7.5, ASCII) |

The reader files use the standard library and one header-only file of our own, `engine/include/okay/Core/Inflate.hpp` (the engine's shared inflate; no `<mutex>`/`<thread>`, no engine library), so the hostile-input test still builds them on their own under sanitizers (it only needs `engine/include` on the include path).

### Turning it on

- CMake: `-DOKAY_OWN_FBX=ON` makes it the default reader for that build (default `OFF`).
- Run time: environment variable `OKAY_OWN_FBX=1` (or `0`) overrides the build default; `okay::fbx::SetOwnReaderEnabled(true)` does the same from code and is what the tests use.
- When our reader is selected and cannot read a file, the importer falls back to ufbx for that file.

### What it handles

- Binary FBX 7.1 to 7.7 (32-bit offsets below 7500, 64-bit from 7500) and ASCII FBX 7.x. All array property types (`f d l i b` and the byte array `c`), raw and zlib-compressed. The zlib arrays are inflated by the engine's ONE inflate (`engine/include/okay/Core/Inflate.hpp`: stored, fixed and dynamic Huffman blocks); `okay::fbx::Inflate` is a thin wrapper that keeps the forgiving behaviour this reader always had (the Adler-32 and bytes after the stream are not checked). The FBX reader no longer has a decoder of its own.
- Embedded textures (`Video` / `Content`, raw bytes in binary, base64 in ASCII) and texture file names.
- Geometry (`Mesh`): positions, polygon vertex indices with the negative end marker, normals, UVs, vertex colors, per-polygon material ids. Mapping modes `ByPolygonVertex`, `ByVertice`, `ByPolygon`, `AllSame` and `ByEdge`; reference modes `Direct` and `IndexToDirect`. Missing normals are generated flat, one per polygon (ufbx does the same without smoothing data; smoothing groups are not used). Polygons with fewer than 3 corners or out-of-range indices are skipped.
- Triangulation: triangles directly, quads and n-gons by ear clipping on the polygon's dominant plane (concave polygons keep their winding), fan fallback for degenerate input and for polygons over 256 corners.
- Meshes split per material slot, in ascending slot order.
- Materials: diffuse color, emissive color and factor, shininess mapped to roughness (`1 - sqrt(shininess) * 0.1`, the mapping ufbx uses), diffuse and normal-map texture connections. PBR property sets written by 3ds Max (Physical Material, PBR metal/rough, PBR spec/gloss, the glTF material, OpenPBR), recognised by the class id in the material the way ufbx does (a material that says lambert or phong keeps the classic reading): base colour, roughness (glossiness inverted when `useGlossiness` is about 1), metalness, emission colour and factor, and the colour, normal and ambient-occlusion texture slots, which 3ds Max connects as `<name>_map`. A texture whose own Video has no picture takes it from another Video with the same file name.
- Node hierarchy and transforms: Lcl translation, rotation, scaling, rotation order (all six), pre- and post-rotation, rotation and scaling pivots and offsets, geometric translation/rotation/scaling, local and world matrices, decomposed to translation, quaternion and scale.
- Axis and unit settings from `GlobalSettings` (up, front, coord axes and signs, unit scale) are converted to right-handed Y-up, meters, the same way ufbx's "modify geometry" mode does it: the unit scale multiplies every translation, vertex, inverse bind and blend offset, and only the root nodes get the axis rotation, so every node below keeps the local axes the file was authored in (important for retargeting). Checked against ufbx on the fixtures (Z-up/cm and Y-up/m).
- Skins: first skin deformer, clusters, per-control-point weights sorted strongest first, the cluster's `Transform` (mesh node relative to the bone, as Blender writes it and as ufbx reads it) times the geometric transform as the inverse bind (`geometryToBone`), bone node per cluster. `TransformLink` is not used.
- Blend shapes: blend deformers, channels, `DeformPercent`, the channel's last target shape with position and normal offsets.
- Animation: stacks, layers, curve nodes, curves (`KeyTime`, `KeyValueFloat`, `KeyAttrFlags`, `KeyAttrDataFloat`, `KeyAttrRefCount`). Constant, linear and cubic interpolation (Hermite from the slopes the file stores, or neighbour-derived for auto keys when it stores none). Translation, rotation and scaling of nodes plus blend `DeformPercent` are baked at 30 Hz (capped at 20000 samples), start trimmed to 0, one clip per stack.
- Limits against hostile files (`okay::fbx::Limits`). Going past one fails the load with an error string (never a crash); parent cycles among models are broken instead of failing. The ufbx path in `ModelImport` enforces the same node and animation numbers (see `docs/content/model_import.md`).

  | Limit | Default | What it bounds |
  |---|---|---|
  | `maxDepth` | 96 | element nesting while parsing |
  | `maxElements` | 4 Mi | elements in the whole tree, and objects in a scene |
  | `maxArrayBytes` | 512 MiB | decoded array bytes, whole file |
  | `maxStringBytes` | 256 MiB | one string or raw blob |
  | `maxCorners` | 8 Mi | triangle corners built, whole scene |
  | `maxNodes` | 1 Mi | scene nodes (models) |
  | `maxNodeDepth` | 2048 | scene hierarchy depth (the walk is iterative, so depth is never call depth) |
  | `maxBakedSamples` | 20000 | baked samples per clip |
  | `maxAnimStacks` | 128 | `AnimationStack` objects |
  | `maxAnimLayers` | 1024 | `AnimationLayer` objects |
  | `maxAnimCurves` | 128 Ki | `AnimationCurve` objects, and `AnimationCurveNode` objects, each |
  | `maxAnimKeys` | 4 Mi | keys over all curves |
  | `maxTotalBakedSamples` | 4 Mi | baked evaluations over all clips (samples times animated curve nodes). Several stacks can point at the same curves, so each clip is charged against this one budget before it is baked |

### What is NOT handled yet

Say plainly what a user might trip over:

- **Real files: one outside corpus only.** 43 files from the test directories of an Assimp 5.4.3 checkout were read (`docs/content/model_corpus.md`): 42 read and agree with ufbx on counts, names, bounding boxes, surface area, skin weights, inverse binds and clip samples (within the tolerances listed there); one damaged file is rejected by every reader. Five bugs came out of it and are fixed (byte-array properties, takes sharing a timeline, stored slopes of auto keys, bridged polygons, integer conversions of hostile doubles). That is a handful of exporters and several tiny test cases: a file from another exporter can still break us. Keep ufbx as the default.
- FBX 6.x and older (no `Objects` section / different layout) and binary versions below 7100. `Properties60` is not read.
- `InheritType` other than the default (`RrSs`, `Rrs` scale inheritance) is treated as the default. ufbx can compensate for these.
- Only `Mesh` geometry. NURBS, patches, lines, cameras, lights, skeleton node attributes, `Pose`/`BindPose` objects, templates in `Definitions` and visibility are ignored (clusters carry the bind matrices we use).
- Mesh data: only the first UV set; vertex colors are read but the engine does not use them; tangent, binormal and smoothing-group layers are ignored; `ByEdge` mapping is approximate (a corner takes the edge that starts at it).
- Materials: no `Bump`, specular, opacity, transmission, coat, sheen or subsurface values, no Maya / Arnold / OSL standard-surface or ShaderFX / Stingray graph materials (they sit in separate `Shader` objects the reader does not follow), no textures for emission, metalness, roughness or opacity (the importer takes the colour and normal-map files), UV set names are not read (first UV set only, as in the ufbx path). Material values agree with ufbx on the 42 real files and the 12 fixtures except where the picture file of a textured colour or glossiness slot is missing (`docs/content/model_corpus.md`).
- Skinning: only the first skin deformer; dual-quaternion skinning type and additive cluster link modes are ignored; for a geometry shared by several nodes the first node's geometric transform is used for the inverse binds.
- Blend shapes: in-between shapes (`FullWeights` with several targets) are not interpolated, only the last target is used.
- Animation: constant (step) curves are baked at 30 Hz, so a jump becomes a one-frame ramp (ufbx emits a key pair at the jump); we emit every channel of an animated node at every sample (ufbx emits only animated channels with fewer keys); custom tangent weights and velocities are ignored; TCB tangents are treated like auto; auto-clamp is approximated; layer blending is simplified (first layer overrides, later layers add or lerp by weight, rotations are blended as Euler angles); only translation, rotation, scaling and `DeformPercent` are animated (not pivots, pre/post rotation or other properties); a take plays the span its `LocalStart` / `LocalStop` give (several takes can share one set of curves), else the range of its keys; ufbx keeps keys beyond the span in its clip, ours cuts there; a take whose curves animate only custom properties gives no clip (ufbx gives a constant one); no key reduction (ufbx reduces keys; the engine's `OptimizeClips` runs afterwards on both paths).
- Left-handed source axis systems: the root node gets the (mirroring) axis matrix and nothing else is done about winding; untested against ufbx. `Transform` is assumed to be bone-relative as above; an exporter that writes the mesh's world matrix there instead would skin wrongly (ufbx makes the same assumption, but this has only been checked on our own fixtures).
- No repair of damaged files. ufbx recovers from many malformed files; ours rejects them. Big-endian files, zlib checksums (Adler-32 is not verified), encrypted FBX and streaming are not supported.
- Not benchmarked. Nothing has been measured against ufbx for speed or memory.

### Tests

- `fbx_reader` (`tests/test_fbx_reader.cpp`): inflater against zlib output (stored, fixed, dynamic, limits, truncation); hand-computed checks on each fixture (axis/unit conversion, ear clipping area and winding, materials, skin weights and inverse binds, blend shape, baked rotation/translation/scale values, pivot and pre/post-rotation transform against an independent evaluation); a hand-computed Hermite check (two keys with different outgoing and incoming slopes, sampled at several times, against the spline formula, through our reader and through ufbx); then **every fixture in `tests/assets/fbx` imported through ufbx and through our reader** and compared: object, mesh, skinned-mesh, joint, vertex, triangle, blend shape, clip and track counts, object names, world-space bounding box and surface area, skin weights, joint indices and inverse binds, clip samples within tolerance, and the single-mesh `ImportModel` path.
- `fbx_import_limits` (`tests/test_fbx_import_limits.cpp`): hostile files through `ImportModelScene` on the ufbx path and with our reader switched on: a 100000-model parent chain, a chain just past the depth cap, too many stacks, layers and curves, and many takes sharing one curve; each must fail with `ok == false` and a legal deep chain must still import. Files go to the temp directory.
- `fbx_hostile` (`tests/test_fbx_hostile.cpp`): standalone, no engine code. Every truncation of every fixture, bad end offsets, huge property/array/string counts, bad encodings, 200000-deep nesting in binary and ASCII, a decompression bomb, an array-byte budget shared across the file, a 5000-deep, a 100000-deep and a cyclic model hierarchy, animation counts (stacks, layers, curves, keys, a claimed 10-million-key curve) and the whole-file bake budget with many takes sharing one curve, a 100000-corner polygon, out-of-range polygon indices, and a deterministic mutation fuzz (`OKAY_FBX_FUZZ_ITERS=20000` for a longer run). Run under `-fsanitize=address,undefined` (standalone build) it was clean, including 200000 mutations. The engine-linked `fbx_reader` and `fbx_import_limits` tests were later run under the same sanitizers built with the whole engine, and `fbx_reader` under valgrind, with no finding (`docs/content/model_corpus.md`).
- `fbx_reader` also compares, for every fixture and real file, what both paths put on each mesh object as material (colour, emission, metalness, glossiness, texture and normal-map file name), and has reader-level checks for the PBR property sets (`pbr_bin.fbx`) and the unconnected-Video picture (`orphan_video_bin.fbx`).
- `fbx_reader` also reads every real file of the outside corpus when `OKAY_MODEL_CORPUS` is set (`docs/content/model_corpus.md`), with slightly looser tolerances for vertex count (0.5 %), surface area (0.2 %) and clip samples (0.015) than for our fixtures, and `model_corpus` records the counts of each real file in `tests/data/model_corpus_manifest.tsv`.
- Fixtures `pbr_bin.fbx` and `orphan_video_bin.fbx` came from the pre-flip verification. Fixtures `takes_bin.fbx` (three takes with their own spans, a `c` thumbnail) and `bridge_bin.fbx` (a polygon with a bridged hole) were added for the bugs the real files showed.
- The hostile test was re-run with `-fsanitize=address,undefined,float-cast-overflow` (standalone build, 30,000 mutations per fixture): clean after the integer-conversion fix; the engine-linked tests were then run too (see above).
- Regenerate fixtures with `python3 scripts/gen_fbx_fixtures.py`. They are 2 to 12 KB each.

## Our own HMP reader (`.hmp`, version HMP7)

*Was `docs/own_hmp_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_HMP`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of HMP files. Nothing was flipped. **Only the HMP7 flavour is read. The layout was worked out from two real files, no written specification was available: read the "How sure" part.**

### What it is

HMP is the terrain model of 3D GameStudio (Conitec). The model is a **height grid**: a rectangle of grid points, each with a 16-bit height, plus optionally an embedded skin picture. It shares its first 84 bytes of header with the Quake / GameStudio MDL model header.

| File | What |
|---|---|
| `engine/src/Render/HmpReader.cpp` | `okay::ReadHmpBuffer` / `okay::ReadHmpFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp`, `ReaderCommon.hpp` | Shared pieces: the bounds-checked cursor, range check |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 29 (extension `.hmp`) |
| `tests/test_hmp_reader.cpp`, `tests/batche_fixtures.hpp` | Hand-computed checks on synthetic files (`hmp_reader`) |
| `tests/test_batche_hostile.cpp` | Mutation fuzz, the worst allowed file (time and memory), the real files (`batche_hostile`) |
| `tests/test_own_dispatch_e.cpp` | The import seam routes `.hmp` to this reader (`own_dispatch_e`) |

### The layout (HMP7), and how sure each part is

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

**Guessed, not verified:** which end of the rows is "up" in the skin picture (so `v` could be flipped), whether a row is a line of constant y (the sample is square, so the other reading cannot be told apart by size; the symmetric -80..80 range cannot tell either), the meaning of the stored normal bytes, that origin x and y are the start of the grid (the symmetric -80..80 range supports it but does not prove it), that the first integer of a skin is a type and that type 4 means 3 bytes per pixel (only one skin, of one size, was seen), and that the 36-byte frame header can be skipped unread (it is the same in both files).

### Not handled

Other versions (**HMP4, HMP5 and others are refused** with a message naming the version: their layouts are not known to me); skin types other than 4 (refused, because the pixel size is not known and the grid cannot be found past the skin); the embedded skin picture itself (skipped and noted: a material here names a texture file, it cannot carry pixels); the stored normals; more than one frame (the first is read, a note says so); terrain holes and water (if the format has any).

### Limits

File 128 MiB. At most **1,048,576 grid points** (a 1024 x 1024 grid, about 2.1 million triangles), 64 skins of at most 64 million pixels each. The row length must be a whole number of 2 or more that divides the point count, with at least two rows; the cell size must be a positive finite number; scale and origin must be finite and below 1e9. All counts are checked against the file's real size before anything is allocated. The worst allowed file (1024 x 1024, 4 MB) reads in under 0.1 s and holds about 55 MB at once (`batche_hostile` fails above 128 MB or 2 s).

### Tests and what was checked

- `hmp_reader` (528 checks): a synthetic 3 x 3 plane with hand-computed vertices (heights `z = 10 + c + 3r`), UVs, the plane's normal `(-1/3, -2/3, 2/3)` at every point, the triangle indices of the first and last cell, the area (`24 * sqrt(2.25)`), counter-clockwise winding, the root rotation; a 4 x 2 grid with free heights; a skin (skipped, noted); extra frames; more than 30 refusal cases (other versions, counts of `0xFFFFFFFF`, over the budget, not a multiple of the row length, a row length of NaN, infinity, 2.5 or 1, one row, bad skin counts and types, a skin of 2^62 pixels, a skin larger than the file, NaN and infinity in every float field, a zero or negative cell size); every truncation; every header byte set to 0x00, 0xFF and 0x80.
- `batche_hostile`: mutations of two fixtures, the worst allowed file and a grid one column over the budget, the two real files.
- `own_dispatch_e`: `.hmp` reaches this reader; HMP, MDC and CSM files under each other's extension are refused by the reader of that extension.
- **Mutation checks:** reading the z scale from the wrong field (21 failures), reversing the triangle winding (25), removing the row-length check (2).

#### Real files

`terrain.hmp` (a plain 33 x 33 terrain) and `terrain_withtexture.hmp` (the same terrain with a 400 x 501 skin), from the `test/models/HMP` and `test/models-nonbsd/HMP` folders of an Assimp 5.4.3 checkout (`docs/content/model_corpus.md`; not in this repository). Read as data only. Both read: 1,089 vertices, 2,048 triangles, x and y from -80 to 80, z from -34 to 18; the textured one notes the skipped skin. The shape of the terrain was looked at as a picture next to its skin (a stylised scene); the picture did not settle the orientation question above. **The reader was not compared with Assimp's output.**

### Provenance (what this file was written from)

`HmpReader.cpp` was written from the byte-by-byte analysis of the two sample files above and from the layout they share with the Quake / GameStudio MDL header (counts, origin and scale as the model's coordinate transform). I found no written specification of the HMP7 header; where a field is called something in the table it is my reading of the data. No importer source (Assimp or any other project) was read, copied, ported or followed, and Assimp was not run on these files either.

Unverified: files from other GameStudio versions and other skins, the orientation and normals questions above, files larger than the sample, a run on real Windows.

## Our own IQM reader (`.iqm`)

*Was `docs/own_iqm_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_IQM`, slot 34 of the table in `ModelImport.cpp`; same rules as `docs/content/model_readers.md`). Assimp (when built) is still the other reader of IQM files. Nothing was flipped.

### What it is

IQM, the Inter-Quake Model, is a compact binary format (Lee Salzman) for skinned, animated models, used by Cube 2 / Sauerbraten and other open engines. One file holds the meshes, the skeleton and the animations.

| File | What |
|---|---|
| `engine/src/Render/IqmReader.cpp` | `okay::ReadIqmBuffer` / `okay::ReadIqmFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp`, `TextReaderUtil.hpp`, `SceneRefPath.hpp` | Shared: bounds-checked cursor, file read, parent ordering, folder rule for textures |
| `tests/test_iqm_reader.cpp`, `tests/e3_fixtures.hpp` | The test (`iqm_reader`), hostile input, budgets, memory, real file |
| `tests/test_own_dispatch_e3.cpp`, `tests/test_own_default_e3.cpp` | The seam and the build default |

### What it reads (version 2)

- Meshes: one skinned part per mesh. Positions, normals, the first texture-coordinate set, four joint indices and weights per vertex (any integer or float format the vertex-array record names; byte and short weights are normalised, then each vertex's weights are divided by their sum). Smooth normals are made when the file has none. Triangles use the file's own order (counter clockwise).
- Skeleton: the joint table (translate, rotation quaternion, scale) as nodes under a root node `IQM` that turns Z-up into Y-up; parents first (a parent loop, a self parent or a parent index out of range is cut and noted). Inverse bind matrices come from the joints' rest pose.
- Materials: the mesh's material name; a name that ends in an image extension is also the texture, resolved with `ResolveInsideDir` inside the model's folder (an absolute path or a `..` that climbs out is refused and noted).
- Animation: each animation is one clip. The pose records' channel masks and offsets / scales and the 16-bit frame data give a translation, rotation and scale per joint and frame, at the animation's frame rate (a bad rate becomes 30). A channel group the mask does not animate is constant: it gets keys (first and last frame) only if it differs from the joint's rest value. Quaternions are normalised and kept on one hemisphere.

### Not handled (lines in `out.notes`)

Version 1 files (Euler angles; refused with a message), tangents, colours, custom vertex arrays, adjacency, bounds, comment text, extensions, the loop flag of an animation (the engine's clip type has none), frame data without an animation entry. Texture coordinates and winding are kept as written. **Whether IQM's v axis needs a flip was not settled**: the sample file's textures cannot show it automatically, so the coordinates are as in the file.

### Limits

File 128 MiB. Per file: 4,000,000 vertices and 4,000,000 triangles (counted over all meshes, since two meshes may name the same range), 10,000 joints / poses / meshes / animations, 64 vertex arrays, **500,000 joint frames** (frames times poses, over all animations; each costs up to three keys, about 52 bytes). **File-wide output budget**: the vertices and the triangles are each counted over all meshes at most 1,000,000 (a range named by several meshes counts every time, so a 240 KB file cannot ask for 4,000,000 output vertices), meshes times joints at most 100,000 and meshes times the joint-name bytes at most 16 MiB (every part carries a copy of the whole bone list). Every table is checked to lie inside the file, with 64-bit arithmetic, before anything is sized from its count. Names are cut at 256 characters.

### Tests and what was checked

- `iqm_reader`: a hand-built file (one quad, two joints, one 3-frame animation with hand-computed keys: translate x 0, 1, 2, scale x 1, 2, 3, times 0, 0.1, 0.2), skin weights 128/255 and 127/255, inverse bind points, material and texture; a file without normals or blend arrays; cyclic, out-of-range and self parents; each refusal with its reason; texture paths; every header field set to `0xFFFFFFFF`, `0x7FFFFFFF`, 0x80000000 and other extremes (140 files: 118 refused, 22 read and sane); truncation at every length; 4,000 random byte flips (set `OKAY_BATCHD_FUZZ_ITERS` for more); every budget refused just over and read just under; the worst file the budgets allow (500,000 joint frames, every channel animated, 9 MB): **peak 24 MB beyond the input, 0.07 s**, measured with a counting `operator new` (the test caps it at 50 MB). Also: 10,000 meshes of 400 vertices reusing one range (239 KB file) are refused with a peak of under 1 MB (the old per-mesh budget let it through at about 100 MB); the largest allowed such file (2,500 meshes, 1,000,000 output vertices) reads with a peak of 24 MB. A joint with zero scale cannot be inverted: its inverse bind is the identity and a note says so (tested).
- **A real file** (`mrfixit.iqm`, in the Assimp tree, which is not in this repository; the test reads it when `OKAY_E3_SAMPLES` names a folder that has it): header numbers match (1,861 vertices, 2,988 triangles, 75 joints, 2 meshes, 101 frames), all skin weights sum to one, and a separate check of the frame packing (all 75 x 101 decoded rotation quaternions have length 1 within 3e-5, which they would not if the channel order were wrong). **Not verified**: how it looks in the renderer, the v flip, other exporters' files.

### Provenance

Written from **my memory of the IQM specification text** (`iqm.txt`; it was not fetched or re-read during the work: header, `iqmmesh`, `iqmtriangle`, `iqmjoint`, `iqmpose`, `iqmanim`, `iqmvertexarray`, the vertex-array types and formats, and the rule that frame data holds one 16-bit value per set mask bit, `offset + value * scale`), and every layout detail was then checked against a hex dump of the one real file (`mrfixit.iqm`: header numbers, record sizes, the quaternion-length check). Where memory and the file could disagree, the file decided. No importer or engine source was read, copied or ported (rule 19a).

## Our own Irrlicht scene reader (`.irr`)

*Was `docs/own_irr_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_IRR`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of Irrlicht scenes. Nothing was flipped. This completes the Irrlicht pair started by `docs/content/model_readers.md` (the mesh half).

### What it is

`.irr` is the scene file of the Irrlicht engine, written by the irrEdit scene editor and by `ISceneManager::saveScene`: an XML file with `<irr_scene>`, nested `<node type="...">` elements that carry typed attributes (`Position`, `Rotation`, `Scale`, `Mesh`, `Size`, `Radius`, ...), a `<materials>` list and `<animators>`.

| File | What |
|---|---|
| `engine/src/Render/IrrSceneReader.cpp` | `okay::ReadIrrSceneBuffer` / `okay::ReadIrrSceneFile` (declared in `ModelData.hpp`, with the `IrrMeshLoader` type) |
| `engine/src/Render/XmlLite.hpp`, `TextReaderUtil.hpp`, `ReaderCommon.hpp` | Shared strict XML reader, UTF-16 conversion, number parsing |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 25 (extension `.irr`); it also gives the reader its **mesh loader** (below) |
| `tests/test_irr_reader.cpp`, `tests/batchd_fixtures.hpp` | Hand-computed checks on synthetic files with a test loader (`irr_reader`) |
| `tests/test_batchd_hostile.cpp` | Hostile input and mutation fuzz (`batchd_hostile`) |
| `tests/test_batchd_budget.cpp` | Budgets (100 big spheres, one mesh copied 3,000 times, 400 big files) and the path rules (`batchd_budget`) |

### What it reads

- **Text.** UTF-8 and UTF-16 with a byte order mark (irrEdit writes UTF-16 on Windows).
- **Nodes.** Every `<node>` becomes a node of ours (the tree is walked with an explicit work list, 256 levels), with `Name`, `Position`, `Rotation` (Euler degrees, `R = Rz * Ry * Rx`, positive angles counter-clockwise: the convention was found by running Assimp on small synthetic scenes, **not** checked against Irrlicht itself) and `Scale`. A bad vector is replaced by the identity and counted.
- **`cube` and `sphere` nodes** become meshes of their own: a cube of edge `Size` centred on the node (outward counter-clockwise faces, flat normals, uv 0..1 per face); a UV sphere of `Radius` with `PolyCountX` segments and `PolyCountY` rings (clamped to 3..512 and 2..512). The node's first `<materials>` entry gives the material (`Diffuse`, `Emissive` as `aarrggbb`, `Shininess`, `Texture1`, and `Texture2` as the normal map for normal and parallax map types; paths joined to the scene's folder).
- **`mesh` and `animatedMesh` nodes** name another model file in `Mesh`. The reader does not read model files itself: it calls the **loader** its caller supplies. The import seam supplies one that runs the file through `ImportModelData` with the default choice of backend (so an `.irrmesh` or `.x` goes to our reader when it is switched on, else to Assimp when the build has it, and `.obj` always to ours), one level deep: a scene that names a scene gets that node refused and a note. The loaded model's nodes go under the scene's node, its parts keep their skins; a file used by several nodes is loaded once and its materials are shared.
- All other node types (`camera`, `light`, `skyBox`, `terrain`, `billboard`, `empty`, ...) stay as empty nodes, so the hierarchy is kept; the kinds are listed in a note.

### Not handled (each is a line in `out.notes`)

Animators (rotation, fly circle, fly straight, follow spline, ...: Assimp makes clips from them, ours does not), the animation clips of the referenced meshes (dropped), the node's own material list on a mesh node (the mesh file's materials are used), material types, flags and wrap modes, cameras, lights, skyboxes, terrains. A scene with no geometry (only cameras, lights, a skybox) is refused. `.xml` files in this format (`skybox.xml` in Assimp's tree) are not routed here; the extension is too common.

### Differences from Assimp that are on purpose

Assimp's cube reaches `Size / sqrt(3)` from the centre (a half-edge of 5.774 for `Size 10`; Irrlicht's cube of edge 10 reaches 5), and its sphere has half the radius (`Radius 5` gives 2.5) and is an icosphere of 1,280 triangles. Both are Assimp's own scaling of its built-in meshes; ours follow the parameters as Irrlicht documents them (`Size` is the edge, `Radius` the radius) and the sphere is a 16 x 16 UV sphere of 480 triangles.

### Limits

File 256 MiB; 1,000,000 nodes; per file 4,000,000 output triangles and 4,000,000 output vertices (spheres, and every copy of a loaded mesh, one per node that names it) and 4,000,000 triangles / vertices in all the mesh files loaded (they are kept until the end); a file over a budget is refused with a message that names it; 1,000 different mesh files per scene; nesting 256; the XML reader's caps. **A scene may only name mesh files inside its own folder:** an absolute path, a drive letter, `//host`, a `..` that climbs out, and a path that goes through a link (symbolic link, junction) to somewhere outside are refused for that node (a note says so; if nothing else gives geometry the scene is refused with the reason). Texture names are not read by the reader and are kept as written. `PolyCountX` and `PolyCountY` are clamped as doubles into 3..512 and held in 64-bit integers (a `long` is 32 bits on Windows).

### Tests and what was checked

- `irr_reader`: **synthetic** scene (a camera, a cube with position, a 90 degree rotation, a green material with texture and an animator, a group holding a sphere and a mesh node) with a test loader: nodes and parents, positions, the rotation quaternion, cube vertices (all at +-1), outward winding, sphere radius, normals and area, the material, the loaded triangle seen through four nested nodes, notes; with `\n`, `\r\n` and UTF-16; no loader; a loader that fails; the same file used twice (loaded once, material shared); a bad vector; refusals (empty, wrong root, no node, no geometry, a DTD entity, absurd poly counts) and **every truncation**.
- `own_dispatch_d`: through the seam: a scene naming an `.irrmesh` (ours), an `.obj`, and a scene naming a scene (refused, the scene still reads).
- `batchd_hostile` (Irrlicht section): `nan`, `inf`, `1e999`, hex floats and overflowing numbers in positions, rotations, scales, sizes, radii, poly counts, colours; 5,000 nested nodes, 3,000 cubes, 1,200 mesh nodes whose files fail, 60,000 mesh nodes that use one mesh (the output cap), a 3 MB mesh path, an entity bomb, loaders that return nothing or wrong indices; then 3,000 mutations of the file and 750 of its UTF-16 form (30,000 and 7,500 in the sanitizer run).

#### Real files

Assimp's tree has 21 `.irr` files in `models/IRR` (UTF-8 and UTF-16LE) and `skybox.xml` (UTF-8 and UTF-16LE, extension `.xml`, no geometry). **All 21 `.irr` files read in ours and in Assimp** (Assimp built with the X, IRRMESH and OBJ importers as well, so the meshes the scenes name load there too). **The 9 files that only place referenced meshes (`dwarf.x`, `cellar.irrmesh`, `spider.obj`: `EpisodeI_ANewDwarf`, `animMesh`, `dawfInCellar_ChildOfCellar`, `dawfInCellar_SameHierarchy`, `instancing`, each in both encodings except `instancing`) agree with Assimp on mesh count, triangle count, surface area and bounding box** (`EpisodeI_ANewDwarf.irr`: 9,752 triangles, area 369,750.5798 ours and 369,750.5807 Assimp, same box; `dawfInCellar_ChildOfCellar.irr`: 2,168 triangles, 575,948.295 both; `instancing.irr`: 9,480 triangles, 42,214.7085 against 42,214.7084). The other 12 (`box`, `sphere`, `multipleAnimators`, `scenegraphAnim`, `scenegraphAnimMod`, `EpisodeII_TheDwarfesStrikeBack`, in both encodings) have cube or sphere nodes and differ by the scaling above (for example `EpisodeII_TheDwarfesStrikeBack.irr`: area 4,071.09 ours against 4,871.09; same triangle count 7,632 and same mesh count). Both UTF-16 files of each pair give the same result as their UTF-8 twins. Assimp makes animation clips (1 to 4 per file) from the animators; ours has none. The `dwarf.x` files are read through our own `.x` reader (`OKAY_OWN_X=1`) and the `.irrmesh` through ours (`OKAY_OWN_IRRMESH=1`).

Unverified: the rotation convention against Irrlicht, scenes from the engine's `saveScene` with other node types, `lightmap` materials on a mesh node, a run on real Windows.

### Provenance (what this file was written from)

`IrrSceneReader.cpp` was written from scratch for this project from the public description of the Irrlicht scene format (the node and attribute names that irrEdit and `ISceneManager::saveScene` write, as documented in the engine's API docs) and from reading the sample scenes of Assimp's `test/models/IRR` as data (never the Assimp source). Assimp was used only as a **black box**: run on the sample scenes and on small synthetic scenes made for this purpose, its output (node matrices, counts, area, box) showed the rotation order and signs and how the built-in cube and sphere are sized. No code, comment, table or test from any other importer was read, copied or ported.

## Our own Irrlicht mesh reader (`.irrmesh`)

*Was `docs/own_irrmesh_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_IRRMESH`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of `.irrmesh`. Nothing was flipped.

### What it is

`.irrmesh` is the static mesh format of the Irrlicht engine and its tools (irrEdit, the exporters built on it): an XML file with one or more `<buffer>` elements, each holding a material, a list of vertices (one per line: position, normal, colour, texture coordinates) and a list of triangle indices. Written from the public description of the format and the files its tools write, with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/IrrMeshReader.cpp` | `okay::ReadIrrMeshBuffer` / `okay::ReadIrrMeshFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/XmlLite.hpp`, `TextReaderUtil.hpp`, `ReaderCommon.hpp` | Shared strict XML reader (caps; DOCTYPE skipped, entities refused), UTF-16 conversion, number parsing, mesh builder |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 19 of the opt-in table (extension `.irrmesh` only) |
| `tests/test_irrmesh_reader.cpp`, `tests/batchc_fixtures.hpp` | Hand-computed checks on a synthetic file (`irrmesh_reader`) |
| `tests/test_batchc_hostile.cpp` | Hostile input and mutation fuzz (`batchc_hostile`) |

### What it reads

- One part and one material per `<buffer>` that has vertices and triangles. Vertex types `standard`, `2tcoords` and `tangents`; positions, normals and the first texture coordinates as written (no rotation, no flip); triangles as written.
- **Handedness.** Irrlicht is left-handed. The reader does not convert it, as Assimp does not: the model is a mirror image of what a right-handed viewer expects, and its normals and winding stay consistent (checked on the real files: the stored normals agree with the winding on 99 percent of the triangles; the few that disagree are in the files). A note says so on every import.
- Material: `Diffuse` and `Emissive` colours (`aarrggbb` hex), `Shininess`, `Texture1` as the texture, `Texture2` as the normal map when the type is `normalmap_*` or `parallaxmap_*`. Texture paths use `/`, relative paths are joined to the model's folder.
- UTF-16 files (with a byte order mark) are converted to UTF-8 first, as Irrlicht's tools write them.

### Not handled (each is a line in `out.notes`)

Second texture coordinates (type `2tcoords`), tangents and bitangents, vertex colours (the engine's meshes have none per vertex), lightmaps and other second textures, texture 3 and 4, material types, blending, wrap modes and flags, bounding boxes. **The Irrlicht scene format `.irr`** (nodes, transforms, references to meshes) is a different reader and is not done. Assimp also sends `.xml` files to its Irrlicht readers by looking at the root element; this reader is only reached by the extension `.irrmesh`.

### Limits

File 256 MiB (and the XML reader's caps: 2,000,000 elements, nesting 256, 256 attributes per element); vertices per buffer 4,000,000; indices per buffer 12,000,000; output vertices 20,000,000. `vertexCount` and `indexCount` must be whole numbers within the caps and the text must really hold that many values, or the file is refused before anything large is allocated. Numbers go through `NumParse.hpp` (finite, within 1e15, locale independent); colours must be 1 to 8 hex digits; an index past the vertex list skips that triangle.

### Tests and what was checked

- `irrmesh_reader`: **synthetic** file in three buffers built in memory (a triangle with edges 2 and 3, a 2tcoords quad of 4 vertices and 2 triangles, a tangents triangle with a stray index; total area 4.5 by hand), with `\n` and `\r\n` line ends and as UTF-16: counts, the material colours and texture paths computed by hand, the normal map, texture coordinates, notes, refusals (empty, wrong root, no buffer, a vertex count that does not match, a negative count, bad number, bad colour, unknown vertex type, an index past the vertices, a DTD entity) and **every truncation**.
- `batchc_hostile` (IRRMESH section): counts at, past and far past the caps with tiny text, `nan`, `inf`, `1e999`, hex floats and overflowing numbers in every kind of field, bad colours, an entity bomb, 5,000 levels of nesting, 20,000 empty buffers, a 3 MB number, 50,000 material attributes, a valid 60,000-vertex mesh (0.7 s), then 100,000 mutations of the file and 25,000 of its UTF-16 form. Sanitizers (`address,undefined,float-cast-overflow`): clean.

#### Real files

Assimp's tree has 4 `.irrmesh` files (`cellar` and `spider`, each as UTF-8 and UTF-16LE). **All 4 read and equal Assimp v5.4.3 (built from the same sources, only the IRRMESH importer) on surface area, bounding box and mesh count**; triangles are equal on `cellar` (272) and on `spider` ours has 1,312 to Assimp's 1,368 (the 56 extra are zero-area triangles, which ours drops, as the other readers do). Assimp counts the lightmap among the textures (3 for `cellar`, ours 2). Both UTF-16 files give the same result as their UTF-8 twins. The two `testFormatDetection*.xml` files in the tree are the same format under the extension `.xml` and are not reached by this reader.

Not verified: `tangents`-type buffers on a real file (synthetic only), files from tools other than irrEdit, the Irrlicht-written `.irrmesh` of other versions of the format, a run on real Windows.

## Our own LWO reader (`.lwo`, and Modo's `.lxo`)

*Was `docs/own_lwo_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_LWO`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of `.lwo`.
Modo's `.lxo` is read by the same code with its own switch, `OKAY_OWN_LXO` (slot 46, off by default; section [LXO](#lxo-modo) below).

### What it is

Written from the public LightWave object format descriptions (LWO2, and the older LWO1 chunks `LWOB`), with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/LwoReader.cpp`, `engine/include/okay/Render/LwoReader.hpp` | `okay::ReadLwoBuffer` / `okay::ReadLwoFile` |
| `engine/src/Render/ReaderCommon.hpp`, `PolygonTriangulate.hpp` | Shared number checks, smooth normals, ear clipping |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 15 of the batch-B table (`.lwo`), slot 46 (`.lxo`) |
| `tests/lxo_fixture.hpp` | A synthetic `.lxo` (cube, two material tags, NORM maps, a shader tree), used by `lwo_reader`, `batchb_hostile` and `own_dispatch_gap` |
| `tests/test_lwo_reader.cpp` | Hand-computed checks on synthetic LWO2 and LWOB files and the seam (`lwo_reader`) |
| `tests/test_batchb_hostile.cpp` | Hostile input and mutation fuzz (`batchb_hostile`) |

### What it reads

Big-endian IFF chunks, every read bounds-checked; a chunk longer than the file is read as far as it goes.

- **LWO2** (`FORM LWO2`, and `LWO3`, which has the same chunks plus ones that are skipped, noted): `LAYR` (one node per layer; a parent layer that came first is the node's parent; hidden layers and pivots are reported), `PNTS`, `POLS` (`FACE` and `PTCH` as plain polygons, 4-byte `VX` indices; concave polygons are ear clipped), `TAGS` + `PTAG SURF` (a polygon's surface), `VMAP` / `VMAD` `TXUV` (per-point UVs and per-polygon-corner UVs; the surface's own UV map or the first one), `SURF` (`COLR` times `DIFF` as the diffuse colour, `LUMI` times the colour as emission, `GLOS` as a Phong exponent `2^(10*g + 2)` when `SPEC` is above 0, `1 - TRAN` as alpha, `SMAN` as the crease angle in radians (default 0 = flat), the first `BLOK` / `IMAP` colour image map (UV, planar, cylindrical, spherical, cubic or front projection: `PROJ`, `AXIS`, `WRPW` / `WRPH`) and its `CLIP` / `STIL` file name; `\` becomes `/`, a `Name:` content-directory prefix is dropped, the path is relative to the model file).
- **LWO1** (`FORM LWOB`): `PNTS`, `SRFS`, `POLS` (detail polygons after a negative surface number are skipped, noted), `SURF` (`COLR` bytes, `DIFF` / `LUMI` / `SPEC` / `TRAN` in units of 1/256, `GLOS` as the Phong exponent, `SMAN`). Image maps of LWOB are projected (`CTEX` names the projection, `TFLG` the main axis): the texture is kept and UVs are generated like LWO2 projections (below).
- **Projected image maps (LWO2 and LWOB).** Planar, cylindrical and spherical maps get UVs generated from the surface's vertices by `ProjectUvs`.
  - **Provenance.** It was reimplemented on 2026-11-14 after the provenance audit found the first version was a port of Assimp's code (`docs/status/provenance_audit.md` F1). The code is new. The placement convention is deliberately kept identical to the old output (owner decision); the seam rule is new.
  - **Placement.** The projection sits on the surface's own bounding box. Each main axis has a fixed pair of other axes (X: Z and Y; Y: X and Z; Z: X and Y).
  - **Planar:** U and V are where a point sits across the box along those two axes. A flat box gives 0.
  - **Cylindrical:** U is the angle round the main axis seen from the box centre, as a fraction of a turn; V is the position along the axis.
  - **Spherical:** U is the same angle; V is the latitude from -90 degrees (0) to +90 degrees (1). A point on the axis (a pole) gets the turn's starting value.
  - **Wrap amounts.** `WRPW` scales U of cylindrical and spherical maps; `WRPH` scales V of cylindrical and spherical maps (kept as the old reader did; the LightWave spec only names spherical, so this is a deliberate match, not a rule).
  - **Where the turn starts** and which way it runs are set per axis (`TurnRuleFor`), fitted to the old output so textures stay where they were.
  - **The seam (new).** A triangle crosses the seam only when the gap between its U values that runs through the seam is not the largest. Then the corners on one side of the largest gap move onto the seam (the low ones up to the wrap amount, or the high ones down to 0, whichever moves less). Big triangles that do not cross, such as pole fans, are left alone.
  - **Measured against the old reader** on the corpus: planar files identical. On each cylindrical or spherical file every position keeps a UV it had before, and 2 to 8 seam positions get a different set of UVs.
  - **Not applied:** the map's own centre, size and rotation (`TMAP`). Reading them needs the LWO2 specification, which this rewrite did not use. Cubic and front projections keep the texture but get no UVs.
- One part per (layer, surface). UV `v` is flipped like the other bottom-up formats. **Handedness**: LightWave is left-handed (+Z forward), the engine right-handed. The reader negates every point's Z and reverses each polygon's corner order, so a model is not mirrored and its faces still point out (the signed volume of every closed real file is positive; on a real file the Z range equals Assimp's, which does the same). Y-up needs no change. Other coordinates are kept as written.

### Not handled (each is also in `notes`)

Envelopes (animated surface values: the base value is used), shader, procedural and gradient texture blocks, texture channels other than colour and second colour maps, cubic and front projections (the texture is kept, no UVs), non-UV vertex maps (weights, colours, morphs), smoothing groups (`PTAG SMGP`; the surface angle is used), curves, bones and metaballs, subdivision patches (read as plain polygons, not subdivided), `LWLO`, layer pivots, `SURF` inheritance, other chunks (named). Smoothing happens within one surface of one layer only.

### Limits

File 512 MiB; chunks 5,000,000; layers 10,000; surfaces 100,000; points and polygons 20,000,000; triangles 20,000,000; UV values 40,000,000; strings 4,096 bytes; points finite within +-1e15.

### Tests and what was checked

- `lwo_reader` (`tests/test_lwo_reader.cpp`): **synthetic** LWO2 and LWOB files built in memory with a big-endian chunk writer: a cube with two surfaces (parts, colour times diffuse, luminosity, `2^(10*0.2+2) = 16` shininess, transparency as alpha, the smoothing angle welding the corners, the Z range `[0, 1]` coming out `[-1, 0]`, the signed volume 1), a textured surface (the `CLIP` path made relative, the `v` flip, a `VMAD` override of one corner), layers with a parent, hidden layers and pivots, `PTCH` and `CURV` blocks, a concave 6-point polygon (area 3), the 4-byte index form, a shader block, LWOB surfaces in 1/256 units, a projected LWOB image map, detail polygons, refusals (empty, `FORM` only, wrong form type, no polygons, NaN and 1e20 points, an index past the points, a truncated file), and the seam.
- `batchb_hostile` (LWO section): every truncation of an LWO2 and an LWOB file; a `FORM` and a chunk length of `0xFFFFFFFF`; a polygon claiming 1,023 corners without room; 200,000 polygons; a 4-byte index of 16.7 million; 10,001 layers, 100,001 surfaces and a million tags (refused or read in bounded time); strings without a terminator; UV maps with `NaN` and `1e30`; a `SURF` sub-chunk that overruns; NaN and huge surface values; LWOB with a negative surface and absurd detail counts; then the mutation fuzz over both forms. Sanitizers: clean (the runs found a `NaN` UV that reached the mesh, now 0, and, in the 150,000-mutation run, a `FORM` length below 4 that made the chunk loop start past its end, now refused).

#### Real files

Assimp's tree has 54 `.lwo` files under `models` and `models-nonbsd` (LWO2 apart from 5 LWOB files and one empty file). **Ours reads 53; the 54th (`invalid/empty.lwo`, no bytes) fails in both readers. Assimp reads the same 53.** **29 agree on everything compared** (meshes, triangles, materials, surface area, bounding box), 25 differ:

- **16 differ only in `tex`** (Assimp lists a texture file, ours none): the `earth_*` files in `MappingModes` and `bluewithcylindrictexz.lwo` (LWOB); `UglyVertexColors.lwo` differs in `tex` and in triangles (ours 3180, Assimp 3202: the file's polygons give `sum(n - 2) = 3202`, Assimp keeps 22 zero-area triangles that ours drops). Their image maps use planar, cylindrical or spherical projection, which Assimp turns into UVs; ours does not apply them and says so in the notes (named warning, no UVs). This is the one real feature gap against Assimp on this corpus.
- **8 differ in triangles (and sometimes area)**, none in the bounding box: `concave_polygon.lwo` and the LWOB `ConcavePolygon.lwo` (64 triangles, area 0.2455, the polygon's exact area; Assimp drops two zero-area slivers, 58 and 0.2339: the same story as OBJ and COLLADA), `concave_self_intersecting.lwo` and `nonplanar_polygon.lwo` (an 18-corner non-planar polygon: ours 15 triangles and Assimp 11, no reference triangulation exists for such polygons), `box_2vc_1unused.lwo` (ours 419, Assimp 432: Assimp keeps 13 zero-area triangles, ours drops them), and `QuickDraw--Arm-Shoulder`, `Arm-Tip`, `CabinPortals` (ours 640, 2388 and 632; for the first, 640 is `sum(n - 2)` over the file's polygons, and Assimp drops 24 triangles; 4 of its polygons have repeated indices, which may be why; the other two were not examined).
- **Axes and handedness**: ours negates Z and reverses the polygons; the bounding boxes equal Assimp's on all 53 files, which is how this was settled (the first version kept the file's Z, mirrored every model, and showed on `rifle.lwo`, which is the same model as the `rifle.dxf` above).
- Notes our reader printed: 17 non-UV image maps, 13 files with shader, procedural or gradient blocks, 7 with non-UV vertex maps (vertex colours, weights), 3 with envelopes, 2 with layer pivots, 1 with subdivision patches.

### LXO (Modo)

Added 2026-11-16 (branch `claude2/own-readers-gap-2`). Written from the public description of Modo's LXO file format: an IFF
`FORM` of type `LXOB` whose geometry chunks are LWO2's, plus Modo's scene chunks. No code from any other importer was read.

- **Shared with LWO2:** `TAGS`, `LAYR`, `PNTS`, `POLS`, `PTAG`, `VMAP` / `VMAD` `TXUV`, `BBOX`, the handedness change, the
  triangulation and one part per (layer, material).
- **Different in LXO:**
  - `PTAG MATR` (not `SURF`) gives a polygon's material tag; `PTAG PART` and others are skipped.
  - There are no `SURF` chunks. Each tag becomes a material of that name. Its values come from Modo's shader tree: the `ITEM`
    of type `mask` whose `CHNS ptag` is the tag (and `ptyp` is `Material` or empty), then the `advancedMaterial` item whose
    `LINK parent` is that mask; when no mask selects the tag, the first `advancedMaterial` that is not under a mask (Modo's
    "Base Material"). Read from it: `CHNV diffCol` (R, G, B) times `diffAmt` as the diffuse colour, `1 - tranAmt` as alpha,
    and `smAngle` (radians) as the crease angle when `smooth` is above 0. `CHAN` channels are numbered into the `CHNM` name
    table (U4 count, then the names). Without a shader tree the LWO defaults apply (grey 0.78, flat).
  - `VMAP` / `VMAD` of type `NORM` (dimension 3): Modo's own vertex normals, per point and per polygon corner (the corner
    value wins). Z is negated like the points. When every corner of a part has one, the part uses them (no smoothing);
    otherwise the smoothing angle decides.
  - `LAYR`: bit 0 of the flags does **not** hide a layer (both corpus files set it on their only, visible, layer; Modo keeps
    visibility on the mesh item). The parent is a signed 16-bit number (-1 = none). The fields after it (subdivision and
    curve refinement, scale pivot, item reference) are not read.
  - Skipped and counted in one note: every other `ITEM` (the mesh item's settings, cameras, lights, transforms, render
    outputs, environment) and `ACTN` (actions). Skipped silently: `VRSN`, `UNIQ`, `UCPT`, `APPV`, `THUM`.
- **Not read:** textures and image maps of the shader tree, specular, reflection, roughness, luminosity, bump, the item
  transforms (a mesh moved in Modo comes in at its layer's coordinates), subdivision, morphs and weight maps.
- **Notes** start with `LXO:`; the first one says which material values were read.

**Real files** (Assimp's tree: `models/LWO/LXOB_Modo/CrazyEngine.lxo` and `sphereWithVertMap.lxo`, both "modo 301"):
both read in ours and in Assimp, with the same triangles (2460, 528), parts (1) and bounding box. What differs, checked with
`bench_model_readers --dump`:

- **Vertices** (CrazyEngine 3427 ours / 3602 Assimp; sphere 1104 / 266). Ours follows the file: the sphere carries a `NORM`
  vertex map with per-corner values (838 of them) and ours keeps them, so corners with different normals are different
  vertices; Assimp ignores the map and smooths everything (266 = one vertex per point). CrazyEngine has no `NORM` map: ours
  smooths at its base material's 40 degrees, Assimp smooths differently and splits more at UV seams. In the sphere file,
  about one corner in six has a normal that points against its polygon's winding (checked in the raw file with a small
  script, before any conversion), so the sphere is shaded oddly in places in ours; that is what the file says.
- **Materials**: ours names the material after the tag (`Default`) and reads its colour from the shader tree (CrazyEngine
  0.8 grey; the sphere's mask material 0.502 / 0 / 0.502, purple); Assimp gives `LWODefaultSurface` at 0.6 grey in both.
- **Node names**: ours `RootNode` / `Layer1` (as for `.lwo`), Assimp `Pivot-Layer_0` / `Layer_0`.
- **Speed and memory** (`docs/content/own_readers_speed.md`): 0.51x Assimp's time and 0.44x its largest peak over the two files;
  the sphere alone peaks a little above Assimp (0.21 MB against 0.19 MB, its per-corner normals make 4 times the vertices).

**Tests** (all synthetic, `tests/lxo_fixture.hpp`): `lwo_reader` suite "lxo (Modo)" (two tags, the mask material and the base
material with hand-computed colours and alpha, a `NORM` corner override giving 10 + 8 vertices, the smoothing angles without
`NORM` maps, the LWO defaults without a shader tree, the flag-bit-0 rule kept for LWO2) and "lxo: the import seam" (the
`OKAY_OWN_LXO` switch, the extension list, and the import log line); `batchb_hostile` every truncation of the LXO fixture and
a mutation fuzz (40,000 iterations per reader under ASan, UBSan and float-cast-overflow: 271,754 checks, no failure);
`own_dispatch_gap` (slot 46, the switch, the sample `examples/own_readers/cube.lxo`); `model_corpus` (the two real files).

Not verified: other Modo versions than 301, files with several layers or material masks nested in groups, `ptyp` values
other than `Material`, image maps (not read), a run on Windows.

Not verified on real files: LWOB surfaces other than the gloss ones (there are only five LWOB files: two spheres, a cylinder-mapped box, the concave polygon and one more), `VMAD` (the real files that have one were compared only by counts), multi-layer files with parents (the `hierarchy*.lwo` files agree on counts and box), LWO3 (no real file).

## Our own LWS / MOT reader (`.lws` `.mot`)

*Was `docs/own_lws_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_LWS`, same rules as `docs/content/model_readers.md`; one switch for both extensions). Assimp (when built) is still the only other reader of these files. Nothing was flipped. **Checked on the 15 real scenes of the model corpus** (2026-11-16, `docs/content/model_corpus.md`; not in this repository): all 15 read, and their rows are in `tests/data/model_corpus_manifest.tsv`. The 14 small test scenes read from the start; `QuickDraw v.2.2.lws` (11 objects, 9 nulls, a light and a camera, objects in `../LWO/LWO2/LWSReferences/`) was refused until the two fixes below, and now gives Assimp's bounding box. The rotation conventions marked "assumed" below are still unchecked: none of the corpus scenes has a rotated part at rest.

### What it is

An LWS file is a LightWave **scene**: a text file that lists items (objects by file name, null objects, cameras, lights, bones), their parents and their motion as envelopes of keys. It holds no geometry: the objects are `.lwo` files named by path, which our LWO reader (`docs/content/model_readers.md`) reads. A MOT file is a LightWave **motion**: the same envelope text for one item, with `LWMO` in place of `LWSC`.

| File | What |
|---|---|
| `engine/src/Render/LwsReader.cpp` | `okay::ReadLwsBuffer` / `okay::ReadLwsFile` (declared in `ModelData.hpp`; the loader type is the Irrlicht scene's `IrrMeshLoader`) |
| `engine/src/Render/SceneRefPath.hpp`, `TextReaderUtil.hpp`, `ReaderCommon.hpp` | The folder rule for file names, line reading, number parsing, the parent-loop cutter |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 40 (extensions `.lws`, `.mot`); the loader it passes reads each object through `ImportModelData`, one level deep |
| `tests/test_lws_reader.cpp`, `tests/pmxlws_fixtures.hpp` | Hand-computed checks on synthetic scenes (`lws_reader`) |
| `tests/test_pmxlws_hostile.cpp` | Budgets, memory (counting `operator new`) and the path rules on disk (`pmxlws_hostile`) |
| `tests/test_own_dispatch_e5.cpp`, `tests/test_own_pmx_lws_default.cpp` | The seam routes both extensions; `-DOKAY_OWN_LWS=ON` really makes it the default |

### What it reads

- **Header.** `LWSC` (or `LWMO`) then the version (1 to 6 accepted). UTF-8 with or without a byte order mark, and UTF-16 with one, are accepted; lines end in `\n`, `\r\n` or `\r`.
- **Items**, in file order: `LoadObject path`, `LoadObjectLayer layer [id] path` (a file name may hold spaces: the rest of the line is the path; the layer number is ignored, the whole object file is read), `AddNullObject name`, `AddCamera [id]`, `AddLight [id]`, `AddBone [id]`; `LightName`, `CameraName`, `BoneName` and `ObjectName` rename the current item. Each item is a node. Items get the ids LightWave gives them (objects and nulls from `10000000`, lights `20000000`, cameras `30000000`, bones `40000000`, counted in file order) unless the line gives an explicit 8-digit hexadecimal id. `ParentItem id` makes the current item a child of the item with that id; parents are listed first, and a loop, an item that is its own parent, an id that names nothing and a bad id are all cut (the item becomes a root, counted in the notes). Names are made unique.
- **Motion.** After an item, `Channel n` then `{ Envelope`, a key count, `Key value time span p1..p6` lines, `Behaviors pre post` and `}`. Channels 0 to 8 are position x y z, heading, pitch, bank, scale x y z. The node's rest pose is each channel's value at time 0 (before the first key: the first value; no key: 0, or 1 for scale). **One clip, "scene"**, with one channel per animated item: position keys at the union of the key times of the three position channels, rotation keys at the union of the three angle channels' times (the other channels are interpolated linearly at those times; quaternions are flipped to the short way round), scale likewise. Key times are the file's own (seconds, as written); the scene's frame rate is only reported.
- **Objects.** The object files go through the loader the caller gives (the import seam gives one that uses `ImportModelData`, so an `.lwo` is read by our LWO reader when it is switched on, else by Assimp, and an `.obj` by ours). A file named by several items is read once and its parts are copied under each item's node (as the Irrlicht scene does). A loaded object's own clips are dropped (counted). Without a loader, the objects are empty nodes (counted); with one, a scene that ends with no geometry is refused and names the first file that could not be read.
- **MOT.** One node "motion" under a root "LWMO", one clip with its channel; no parts.
- **The older motion layout (versions 1 and 2), 2026-11-16.** `move_x_oldformat_56.lws` (version 2) is `move_x.lws` saved in the older layout, and ours read no motion from it (no clip, and the rest pose left at the origin) while Assimp made a clip. The file itself shows the layout: after `ObjectMotion (unnamed)` (or `LightMotion`, `CameraMotion`, `BoneMotion`) a line with the channel count, then for each channel a line with its key count and one line per key of six numbers: the value, the time in seconds, and three numbers of the span's shape; then `Pre/PostBehavior` with two numbers per channel. The values are the version 5 file's values, key for key, except that **angles are degrees** (the light's heading is 45 there and 0.785398 in the version 5 file). Ours reads this layout now when the line after the motion command is a single number (in versions 3 and up it is `NumChannels n`, so those files are read as before), turns channels 3 to 5 into radians and reads the spans as linear (counted in a note). A block that breaks the layout (a key line that is not six numbers, a missing count, the file ending inside it) is dropped for its item and counted. Result: `move_x_oldformat_56.lws` now gives exactly what `move_x.lws` gives (box, clip length 1.333 s, area; its `model_corpus` row is the same as `move_x.lws`'s), and matches Assimp. Not read: the LightWave 5 layout in which one key line holds every channel's value followed by a frame / linear / tension / continuity / bias line (no sample file and no description at hand to check it against); such a block is dropped as above.
- **Pivots.** `PivotPosition x y z` (an item's own point that its position, rotation and scale act on): the item's matrix is `T(position) R S T(-pivot)`, and a child's position is in that space. The pivot is applied by moving the placed object's root nodes by `-pivot` and every child's position (rest pose and keys) by `-parent pivot`; nothing is baked into vertices. Checked on `QuickDraw v.2.2.lws`, where every part is modelled where it sits (position = pivot) and children carry the same absolute positions: without the pivot the parts were placed twice as far out (box up to y 12.2 and z 22.0), with it the box equals Assimp's (`-2.358 -0.097 -3.647` to `2.358 4.607 6.791`). Mirrored in Z like positions.
- **Conventions (assumed, see below).** LightWave is left-handed (+Z forward), as is its object format, and our LWO reader mirrors Z. So positions are `(x, y, -z)`. A rotation is built as bank (about Z) first, then pitch (about X), then heading (about Y), each with the standard rotation formula, angles **in radians**, and then mirrored by the same Z flip (the quaternion `(x, y, z, w)` becomes `(-x, -y, z, w)`).

### Not read (each is a line in `out.notes`)

Envelope shapes other than linear (TCB, Hermite, Bezier and stepped spans are read as **linear between keys**, counted); pre and post behaviours (repeat, oscillate, offset repeat: only the keys are imported, counted); envelopes of anything but the nine motion channels (light intensity, colour, camera zoom); camera and light settings (they stay empty nodes, named `camera0`, `light0`, or by `CameraName` / `LightName`); bones' rest length and weight maps; plugins (`Plugin ...` blocks are skipped line by line); render, frame and fog settings; an object's layer pivots and layer parents. A scene that names a **scene** (`.lws`, `.mot` or `.irr`) is refused for that item, so a scene cannot load itself or another.

### Where an object file may be

Every object path goes through `SceneRefPath.hpp::ResolveInsideDir` **before the loader sees it**: an absolute path (`/x`, `\x`, `C:\x`, `\\host\x`), any `..` that climbs out of the scene's own folder, and a path that leads out once links on the disk are followed are refused (the other items still read, and the note and the error name the first refused path).

**One exception, for LightWave's content directory (2026-11-16).** A real LightWave scene usually sits one folder below the content directory and names its objects relative to it (`Content/Scenes/x.lws` naming `Objects/y.lwo`), or, like the corpus scene `QuickDraw v.2.2.lws`, as `../LWO/LWO2/LWSReferences/y.lwo`. A path that is refused only because it climbs out is given one more chance when it names a **LightWave object** (`.lwo` or `.lxo`, never another format and never a scene) and starts with **exactly one** `../`: the rest is looked up in the scene folder's parent with the same rule there (no further `..`, no absolute path, no link out of that folder). So a scene can reach at most one folder above its own, and only LightWave objects in it; a note counts such paths. This widens what a downloaded scene can make the importer read (any `.lwo` / `.lxo` in the folder above the scene, still parsed only as a model); it is a choice made so real LightWave scenes read, and it can be taken back by removing `ResolveInContentDir` in `LwsReader.cpp` (the tests in `lws_reader` and `pmxlws_hostile` say what changes). `Objects/y.lwo` relative to the content directory (with the scene in `Scenes/`) is still not searched.

### Limits (per file)

Scene file 64 MiB; a line over 65,536 bytes is skipped, never split into tokens; 100,000 items; 4,000,000 envelope keys (read or not; a declared key count over that is refused at once, so a count of 0xFFFFFFFF reserves nothing); 256 different object files; 4,000,000 triangles and 4,000,000 vertices loaded, and the same placed in the scene (a file named by many items counts once per item, and **vertices are not welded across placements**: a 100,000-triangle object with 300,000 vertices can be placed 13 times); 1,000,000 nodes; key times within 1e7 and values within 1e15 (bad keys are skipped and counted); an unterminated envelope (the file ends inside one) is refused. No recursion; no allocation from a count in the file; exceptions become refusals at the seam.

### Tests and what was checked

- `model_corpus`: the 15 corpus scenes (rows `lws`; their objects are read with our LWO reader).
- `lws_reader` (2,990 checks): the content-directory rule (`../LWO/a.lwo` and `..\LWO\b.LXO` asked for in the folder above, `../x.obj` and `../../x.lwo` refused); and, from before, a **synthetic** scene of four items (an object that names a null that comes later in the file as its parent, a null, a camera, a light with a stray envelope) with `\n` and `\r\n`: node order and parents, rest positions with Z mirrored, scale, the clip's position keys at 0 and 1 (`0,5,-1` and `2,5,-1`), the heading key `pi/2` giving the quaternion `(0, -0.7071, 0, 0.7071)`, the scale key, the path the loader was asked for, the notes; the scene without a loader; a MOT with three position keys; interpolation between channels with different key times (hand-computed `2.5`, `15` and the held ends), unsorted keys, two keys at one time, a TCB span (counted); parent loops, self parents, unknown parents, a bad id, a three-deep chain listed child first, duplicate names; every header problem (empty, wrong magic, versions 0 and 9, `3.5`, `abc`, no items), a byte order mark, UTF-16, the version on the first line; a scene cut inside an envelope, key counts of 99999999999 and 4294967295, a scene cut every 7 bytes and 3,000 random bit flips (only a refusal or a sane model), keys that are `abc`, `nan`, `1e400` or out of range; and the paths `../secret.lwo`, `/etc/passwd`, `C:\x.lwo`, `Objects/../../x.lwo`, `\\host\s\x.lwo` (refused: the loader is never asked), `self.lws` and `sub/more.MOT` (a scene may not load a scene), against `My Objects/a b.lwo`, `good/ok.lwo`, `./up.lwo` and a missing file (asked).
- `pmxlws_hostile` (LWS part): 4,000,001 envelope keys (a 40 MB file) refused in 0.68 s with a peak of 134 MB; a declared 4294967295 keys refused in 0.00 s; 100,001 items refused and 100,000 read (0.09 s, 66 MB); 257 object files refused; one 100,000-triangle object placed 50 times refused with 67 MB peak (12 placements are read); a 20 MB line and a 65,537-byte line skipped; values of 1e15 everywhere stay finite; a scene naming itself in three spellings never reaches the loader; and on disk, with a real folder and a **symbolic link that leads out of it**, eleven outside paths are refused (an absolute path, two folders up in both slash styles, a climb out of a subfolder, another format and a scene in the folder above, a climb inside the folder above, devices and hosts) and the one inside is asked for once; a link inside the folder above that leads out of it is refused too, and `../outside/x.lwo` (one folder up, both slash styles) is asked for. Clean under AddressSanitizer, UndefinedBehaviorSanitizer and `float-cast-overflow`.
- `own_dispatch_e5` and `own_pmx_lws_default`: the seam routes `.lws` and `.mot`; the environment variable `OKAY_OWN_LWS` switches both; through `ImportModelData` a scene reads a real `.obj` next to it, a scene naming itself or another scene is refused for that item, and an object outside the folder is refused.
- **Mutation checks** (each a deliberate bug in a copy of the reader; the named test fails): the scene-file check removed; `ResolveInsideDir` replaced by a plain join; Z not mirrored in a node position; the quaternion not mirrored; the key budget raised to 4,000,002; the long-line skip removed; the unterminated-envelope check removed; the parent ordering replaced by a forced one.

### Unverified and assumed

- **Real files: only the corpus.** 15 scenes from one library's test folder, 14 of them tiny test cases; no `.mot` file. Beyond them, the `Behaviors`, span-type and `Key` layouts, the item ids, the `LoadObjectLayer` id form and the version 1 and 2 layouts are from the format description as I know it.
- **Angles in radians, bank then pitch then heading, and the signs of pitch and bank** are an assumption: they follow the usual reading of the LightWave scene format and the standard rotation formulas, not a comparison with LightWave or with a real scene. If a real scene shows rotated objects wrongly, this is the first place to look.
- Key times as seconds in versions 1 and 2 (true for the version 2 corpus file; LightWave 5's one-line-per-key layout may use frames, and is not read).
- That the Z mirror of positions and rotations matches the LWO reader's mirror on a real object with a pivot or a parent chain.
- A run on real Windows. `bench_model_readers --compare` on the 15 corpus scenes (Assimp run, its code not read): `QuickDraw v.2.2.lws` 22,144 / 22,073 triangles (the LWO reader's known concave-polygon differences in three of its objects), 31 / 31 parts and materials, box equal; the 14 small scenes agree (2026-11-16, after the older-layout fix below); node names differ by design. An earlier note here said `move_x.lws` gave Assimp 36 triangles against our 12: that was not reproduced in 12 runs (single file, folder and whole corpus; both give 12 triangles, 24 vertices, the same box and one clip), and 12 is right by the format: the scene loads layer 1 of `simple_cube.lwo`, which has one layer of 8 points and 6 quads.

### Provenance (what this file was written from)

`LwsReader.cpp` was written from scratch for this project from the **public description of the LightWave scene file format** (the `LWSC` / `LWMO` header and version, the item commands, `ParentItem`, and the `NumChannels` / `Channel` / `{ Envelope` / `Key` / `Behaviors` blocks with channels 0 to 8) as I know it; **no network access was used and no file, document or source was opened while writing it**. **No importer source code, snippet or table of Assimp or of any other project was read, copied or ported**, and no sample file was read. The test scenes were written by hand from the structure above. Assimp was not run.

The older motion layout (versions 1 and 2, added 2026-11-16) was worked out from the corpus file `move_x_oldformat_56.lws` set beside `move_x.lws` (the same scene in the version 5 layout): the order of the numbers and the degrees were read off the two files. No importer source was read for it; two public format pages that were searched for could not be reached from this machine.

## Our own MD2 reader (`.md2`)

*Was `docs/own_md2_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_MD2`, same rules as `docs/content/model_readers.md`: CMake option, environment variable, `SetImportBackend`). Assimp (when built) is still the only other reader of `.md2`. Nothing was flipped.

### What it is

MD2 is the model format of Quake II (1997). A file holds a list of vertex positions per animation frame, one list of triangles and one list of texture coordinates. Written from the public description of the format with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/Md2Reader.cpp` | `okay::ReadMd2Buffer` / `okay::ReadMd2File` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp` | Bounds-checked little-endian cursor, shared by the batch-C binary readers |
| `engine/src/Render/ReaderCommon.hpp` | Shared triangle soup to mesh (welding, smooth normals) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 17 of the opt-in table |
| `tests/test_md2_reader.cpp`, `tests/batchc_fixtures.hpp` | Hand-computed checks on a synthetic file (`md2_reader`) |
| `tests/test_batchc_hostile.cpp` | Hostile input and mutation fuzz (`batchc_hostile`) |

### What it reads

- The header (`IDP2`, version 8), the **first frame** (scale, offset and one byte per axis per vertex), the triangle list, the texture coordinates (pixels, divided by the skin size, top-down as written: the engine's Assimp path applies `FlipUVs` to Assimp's already flipped value, so the end result is the file's own, the same as the MD5 and OBJ readers give) and the name of the **first skin** as the texture.
- Quake winds triangles clockwise: the corner order is reversed. Quake is Z-up: the root node carries the turn to Y-up.
- Vertices are welded per position and texture coordinate; normals are **generated** (smooth).

### Not handled (each is a line in `out.notes`)

- **Frames after the first.** An MD2 animates by swapping vertex positions every frame (vertex animation). The engine's `ImportedModel` has skeletal clips only, so the other frames are not imported. This is the biggest gap: an animated Quake II character imports as its first pose. Assimp reads only the first frame as well (its importer does the same).
- The stored vertex normals (an index into Quake's built-in table of 162 directions): not read, because the table is not reproduced here. Smooth normals are made instead.
- Further skins, the OpenGL command list (the triangle list is used instead).

### Limits

File 256 MiB; vertices 1,000,000; triangles 2,000,000; texture coordinates 4,000,000. Every table offset and count is checked against the real file size before anything is allocated. Scale and offset must be finite and within 1e9. A triangle that names a missing vertex or texture coordinate is skipped and counted.

### Tests and what was checked

- `md2_reader`: **synthetic** file built in memory (4 vertices, scale (1,2,3), offset (10,20,30), 2 triangles; area 1 + 3 = 4 by hand): positions, welding, reversed winding and normal direction, texture coordinates as written, skin path with and without a base folder, the Z-up root, a multi-frame file (note), a missing skin, bad vertex and texture indices (skipped, noted), refusals (empty, wrong magic, wrong version, zero vertices, NaN scale), and **every truncation** of the file.
- `batchc_hostile` (MD2 section): every header field set to 0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, the caps and one past them; NaN, infinity and 3.4e38 in the scale and offset; 1e9 scale; every truncation; then 200,000 mutations (byte flips, insertions, deletions, number-like text, truncation). Run under `-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all`: clean.

#### Real files

Assimp's tree has 5 MD2 files (`models/MD2`: `faerie`, `sydney`; `models-nonbsd/MD2`: `dolphin`, `flag`, `horse`). **All 5 read and equal Assimp v5.4.3 (built from the same sources, only the MD2 importer) on surface area (to 1e-7) and bounding box**, with the Z-up root applied to both. Triangle counts are equal on four; `horse.md2` has 683 to Assimp's 690 (ours drops 7 zero-area triangles, as the other readers do). Differences by design: Assimp always adds a texture (a dummy one when the file names none); ours has none; Assimp keeps three vertices per triangle (1,962 for `faerie`), ours welds (503).

Not verified: models with more than one skin, files from tools other than the Quake II toolchain, a run on real Windows.

## Our own MD3 reader (`.md3`)

*Was `docs/own_md3_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_MD3`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of `.md3`. Nothing was flipped.

### What it is

MD3 is the model format of Quake III Arena (1999) and the many games built on its engine. A file has several **surfaces** (each with its own shader and triangles), per-frame vertex positions, and **tags** (named attachment points that a weapon or a head is fixed to). Written from the public description of the format with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/Md3Reader.cpp` | `okay::ReadMd3Buffer` / `okay::ReadMd3File` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp`, `ReaderCommon.hpp` | Shared cursor and mesh builder (see `docs/content/model_readers.md`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 18 of the opt-in table |
| `tests/test_md3_reader.cpp`, `tests/batchc_fixtures.hpp` | Hand-computed checks on a synthetic file (`md3_reader`) |
| `tests/test_batchc_hostile.cpp` | Hostile input and mutation fuzz (`batchc_hostile`) |

### What it reads

- Header (`IDP3`, version 15), every surface: **frame 0** positions (16-bit values in 1/64 units), the **stored normals** (two bytes: latitude and longitude), texture coordinates (as written, top-down, see `docs/content/model_readers.md`), triangles (winding reversed), the first shader name as the texture. One part and one material per surface.
- **Tags** of frame 0 become empty child nodes of the root (position, and a rotation made from the three axis vectors; axes that are not a rotation, for example scaled or mirrored, leave the node unrotated and say so). The root turns Z-up into Y-up.

### Not handled (each is a line in `out.notes`)

- Frames after the first (vertex animation) and tag animation: the engine has no vertex animation clip.
- `.skin` and `.shader` files. Texture names are the shader names as written; Quake paths are relative to the game folder, not to the model, so they are **not** joined to the model's folder.
- Multi-part player models (`lower.md3`, `upper.md3`, `head.md3` joined by tags) are read one file at a time; they are not assembled.

### Limits

File 256 MiB; surfaces 4,096; vertices per surface 2,000,000; triangles per surface 4,000,000; tags 100,000; output vertices 20,000,000. Every offset (relative to the file or to its surface) and count is checked against the real file size before anything is allocated. Texture coordinates that are not finite become 0 and are counted.

### Tests and what was checked

- `md3_reader`: **synthetic** file built in memory (one triangle with edges 1 and 2, area 1 by hand; decoded normals (0,0,1), (1,0,0), (0,1,0); an empty second surface; a second frame that must be ignored; a tag at (5,6,7) turned 90 degrees about Z), texture coordinates, winding, node tree, notes, a skewed tag axis, refusals and **every truncation**.
- `batchc_hostile` (MD3 section): every header and surface-header field at 0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF and at and over each cap; NaN, infinity and 3.4e38 in tags and texture coordinates; every truncation; 200,000 mutations. Sanitizers (`address,undefined,float-cast-overflow`): clean.

#### Real files

Assimp's tree has 3 MD3 files (`watercan.md3`, `watercan_dmg.md3`, and `european_fnt_v2.md3`, a five-surface map object). **All 3 read and equal Assimp v5.4.3 on triangles, surface area (to 2e-7 relative) and bounding box.** Assimp keeps three vertices per triangle, ours welds (2,034 against 703 on the map object).

Not verified: files with tags (none of the three has one: the tag path rests on the synthetic fixture), player models, files from tools other than Quake III's, a run on real Windows.

## Our own MD5 reader (`.md5mesh`, `.md5anim`)

*Was `docs/own_md5_reader.md` until 2026-11-20.*

Status: **side by side, off by default.** Until now the id Tech 4 (Doom 3) model format only imported in a build with Assimp. Nothing was removed.

### What it is

Written from the public description of the format.

| File | What |
|---|---|
| `engine/src/Render/Md5Reader.cpp` | `okay::ReadMd5Buffer(meshText, animText, baseDir, ...)` / `okay::ReadMd5File` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 8 of the backend switch; `.md5mesh` and `.md5anim` share it) |
| `scripts/gen_md5_fixtures.py` | Writes `tests/data/models/skel.md5mesh` + `skel.md5anim` and CRLF copies (synthetic) |
| `tests/test_md5_reader.cpp` | Hand-computed checks, the seam, scene import, UTF-8 paths, locale (`md5_reader`) |
| `tests/test_md5_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`md5_hostile`) |

### Turning it on

`-DOKAY_OWN_MD5=ON`, environment variable `OKAY_OWN_MD5=1` / `0`, or `SetImportBackend(".md5mesh", ImportBackend::Own)` (the run-time choice wins; one switch covers both extensions). Off by default; with Assimp built, Assimp stays the default reader.

### What it reads

- **md5mesh**: `MD5Version 10`, `numJoints`, `numMeshes`, `joints` (name, parent, model-space position, orientation as x y z with w computed), and the `mesh` blocks (`shader`, `numverts` / `vert`, `numtris` / `tri`, `numweights` / `weight`). Joints become nodes under a root `MD5` that turns Z-up into Y-up; a joint's local transform is worked out from the model-space ones. Each mesh is one skinned part: the vertex positions are computed at the bind pose from the weights (the biases are normalised), the four strongest joints per vertex are kept, the inverse bind is the inverse of the joint's model-space transform. Triangle winding is reversed (id Tech 4 is clockwise). Smooth normals are generated (the file has none). Texture coordinates are kept as written (the file's origin is top left, like the engine's).
- **md5anim**: `numFrames`, `numJoints`, `frameRate`, `numAnimatedComponents`, `hierarchy` (name, parent, flags, start index), `bounds` (skipped), `baseframe` and `frame` blocks. Flag bits 1, 2, 4 select the position components that vary, 8, 16, 32 the orientation components; the rest come from the base frame. One clip, `animation`, with a position and a rotation key per frame and joint at `frame / frameRate` seconds. Values are relative to the parent joint, as the nodes are.
- `ReadMd5File` on an `.md5mesh` also reads the `.md5anim` with the same name next to it when there is one; a broken animation next to a good mesh is a note, the mesh is still imported. On an `.md5anim` alone it builds the nodes from the animation's hierarchy and base frame (no mesh). A joint in the animation that is not in the mesh drops its keys with a note.
- A `shader` that is an image file (`.tga`, `.png`, `.jpg`, `.bmp`, `.dds`) sets the texture; any other shader is a Doom 3 material declaration, not an image: the material gets that name and **no texture is guessed** (Assimp guesses `_d.tga`, `_local.tga`, `_s.tga`, `_h.tga` for these). Meshes without vertices or triangles are skipped with a note. `//` comments, a byte order mark and any line break style are handled.
- `flatten` (`ImportModel`, one merged static mesh): the root's turn is baked in, skins and clips are dropped.

### Limits

| Limit | Value |
|---|---|
| File size | 512 MiB each |
| Joints / meshes | 4,096 / 4,096 |
| Vertices / triangles / weights over all meshes | 10,000,000 / 20,000,000 / 40,000,000 |
| Frames / frames x joints / frames x components | 100,000 / 8,000,000 / 48,000,000 |
| Numbers | finite, within +-1e15 |

Counts are checked against the bytes left in the file before anything is allocated; no recursion (joints are ordered by an explicit pass, a 4,000-deep chain reads, parent loops are cut). A `MD5Version` other than 10, a joint flag that needs more components than the frame holds, a bad number, a weight that names a missing joint fail the read with a message naming the line.

### Not handled

- `.md5camera`.
- The mesh's `numMeshes` is only checked (a mismatch is a note).
- Vertices whose weights sum to zero sit at the origin (note). No texture guessing for shader names.

### Tests and what was checked

- `md5_reader`: both line-ending variants, the Z-up turn, joint local transforms from model-space ones, vertex positions from weights worked out by hand, winding, joint weights, inverse bind, materials and the shader note, an empty mesh, the animation (frame times, flags, base frame, -45 and -90 degree keys), a mesh alone, an animation alone, joints that are not in the mesh, a broken animation next to a good mesh, six joints on one vertex reduced to four, 24 kinds of malformed input, German decimal-comma locale (skipped when none is installed), a UTF-8 path with the animation found next to the mesh, the seam, scene import with clip.
- `md5_hostile`: every truncation of the mesh and of the animation (alone and paired), counts at and over every cap, a 4,000-joint chain, a 3,000-joint loop, 2,000 frames x 30 joints, NaN / infinite / overflowing numbers, an orientation with x^2+y^2+z^2 over 1, NUL bytes, UTF-16, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 150,000 mutations.
- Real files: `SimpleCube.md5mesh`, `Bob.md5mesh` + `Bob.md5anim` and `BoarMan.md5mesh` from an outside corpus (`docs/content/model_corpus.md`). Against Assimp built from source: triangle count, surface area, bounding box (Assimp also turns Z-up the same way) and clip duration (5.792 s, 140 frames at 24) agree on all three; Bob's 33 joint tracks (140 position and rotation keys each) agree with Assimp's on first and last key to four decimals except for two sign-of-zero / last-digit differences (`-0.0000` against `0.0000`, `0.5041` against `0.5042`). `BoarMan` has 13 empty meshes, ours says so. Vertex counts differ because ours are welded (875 against 3,081 on Bob).

### Before it can become the default

More than three real files (the animation was only checked on one skeleton); Windows run; owner approval. Removing Assimp drops `.md5camera` and the stick-figure mesh for an animation read on its own.

## Our own MDC reader (`.mdc`)

*Was `docs/own_mdc_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_MDC`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of MDC files. Nothing was flipped. **Checked on one real file (field by field, not against Assimp), never on real Windows.**

### What it is

MDC is the model format of Return to Castle Wolfenstein (id Tech 3). It is MD3 (`docs/content/model_readers.md`) with "compressed" frames: a few full-precision **base frames** and, for the other frames, one small offset per vertex from a base frame. Like MD3 it is a vertex-animation format: every frame is a full set of vertex positions, there are no bones. The engine has no vertex-animation clip, so **only the first frame is read**, as a static mesh (the same choice as MD2 and MD3).

| File | What |
|---|---|
| `engine/src/Render/MdcReader.cpp` | `okay::ReadMdcBuffer` / `okay::ReadMdcFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp`, `ReaderCommon.hpp` | Shared pieces: the bounds-checked cursor, the mesh builder |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 28 (extension `.mdc`) |
| `tests/test_mdc_reader.cpp`, `tests/batche_fixtures.hpp` | Hand-computed checks on synthetic files (`mdc_reader`) |
| `tests/test_batche_hostile.cpp` | Mutation fuzz, the worst allowed file (time and memory), the real file (`batche_hostile`) |
| `tests/test_own_dispatch_e.cpp` | The import seam routes `.mdc` to this reader (`own_dispatch_e`) |

### What it reads

- **Header** (112 bytes): `IDPC`, version 2 (only 2), a 64-byte name (the root node's name), flags, the counts of frames, tags, surfaces and skins, and five offsets.
- **Surfaces** (one part each, header 124 bytes): name, the counts (compressed frames, base frames, shaders, vertices, triangles) and eight offsets, relative to the surface. The surface's own 4-byte `ident` field is **not checked**: the real file has 7 there, not a magic text.
- **Vertices of frame 0:** the frame table of the surface names, for frame 0, one base frame and optionally one compressed frame (`-1` = none). Base-frame vertices are `x y z` as signed 16-bit numbers in 1/64 units and a packed normal (latitude and longitude as two bytes, 1/256 of a turn, same packing as MD3; the decoded normals agree with the triangle winding on 99.4 percent of the corners of the real file). If frame 0 uses a compressed frame, its per-vertex offsets (three bytes, **units of 0.05 with 127 as zero**, and a normal-table index) are added to the base position; the compressed normal index points into a fixed table of 256 directions that is not read, so such a surface gets **smooth normals computed from the geometry** (a note says so).
- **Texture coordinates** one pair per vertex, as written. **Triangles** three vertex numbers each, stored clockwise (Quake III convention): two corners are swapped to counter clockwise. A vertex number past the vertex list skips that triangle (counted in `notes`); degenerate triangles are dropped and counted.
- **Shader:** the first shader name of the surface (a 64-byte name) is the material's texture, **as written** (backslashes become slashes). Quake paths are relative to the game folder, not to the model. No file is opened, so there is no path to resolve; but the name is cleaned: empty, `.`, `..` and drive-letter segments and a leading slash are removed (a note says so), so a hostile shader name cannot climb out of the folder if some later code joins it to one.
- **Tags** (attachment points, such as `tag_weapon`): each tag of frame 0 becomes an empty child node of the root, with the stored position (3 x 16-bit, 1/64 units). **The tag's rotation angles are not read** and neither is tag animation.
- The root node carries the Z-up to Y-up turn (id Tech 3 is Z-up); `flatten` bakes it into the vertices.

### Not handled (each is a line in `out.notes` where it matters)

Frames after the first (vertex animation); the tag angles; the compressed normal table; `.skin` and `.shader` files; the frame table (bounds, origin, radius, name), which must lie inside the file but is not used; skins (`numSkins`); versions other than 2.

### Limits

File 128 MiB. At most **500,000 vertices and 500,000 triangles per file** (the game itself allowed 4,096 and 8,192 per surface and 32 surfaces), 1,024 surfaces, 4,096 tags, 65,535 frames. Every count is checked against the real size of the file with overflow-safe arithmetic **before** anything is allocated (`Br::Fits`: a count of `0xFFFFFFFF`, or counts whose product would wrap, are refused), every offset must lie inside the file, the surface chain moves forward by at least the 124-byte header so it cannot loop, and the whole read is bounded in time and memory: the worst allowed file (one surface of 500,000 vertices and 500,000 triangles, 14 MB) reads in about 0.4 s and holds at most about 126 MB at once (`batche_hostile` measures this with a counting allocator and fails above 256 MB or 2 s).

### Tests and what was checked

- `mdc_reader` (1,594 checks): a synthetic file (one quad, three frames, two tags, shader with backslashes) with hand-computed vertices, triangle order and winding, UVs, normals, area, root rotation, tag positions at frame 0 (frame 1's tags are different on purpose) and notes; the same file with frame 0 as a compressed frame (offsets +0.5, -1.0, +0.1 worked out by hand); two surfaces; one frame and no tags; more than 30 refusal cases (bad magic, an MD3 file, version, counts of 0, `0xFFFFFFFF`, over each budget, offsets past the end and into the header, base and compressed frame numbers that do not exist, `numBase * numVerts * 8` that would overflow, a surface chain that points back, triangles with bad vertex numbers); every truncation; every byte of both headers set to 0x00, 0xFF and 0x80.
- `batche_hostile`: 3,000 mutations of two fixtures per run (20,000 each in the sanitizer run: ASan, UBSan and float-cast-overflow, clean), the worst allowed file and the file one triangle over the budget.
- `own_dispatch_e`: `.mdc` (any letter case) reaches this reader and no other, an MD3 file named `.mdc` is refused by this reader and an MDC file named `.md3` by the MD3 reader.
- **Mutation checks** (a change to the reader, red test, restore): swapping the winding fix (12 failures), the offset scale 0.05 to 0.5 (6), removing the vertex budget (1), removing the base-frame term of the overflow check (1), reading the wrong tag frame (1). The memory cap in `batche_hostile` was lowered to see it fail.

#### Real file

The only real file is `spider.mdc` from the `test/models/MDC` folder of an Assimp 5.4.3 checkout (`docs/content/model_corpus.md`; not in this repository; `OKAY_MODEL_CORPUS` points at it). It is read **as data only**. It has 19 surfaces, 250 frames, 1 base frame and 249 compressed frames per surface, no tags. The structure above was derived by working through its bytes (every table offset adds up exactly: triangles, shader, texture coordinates, base vertices, compressed vertices, the two frame tables, the surface end). The reader gives 19 parts, 1,312 triangles (the file holds 1,368; 56 have exactly zero area and are dropped, counted independently with a script over the raw bytes) and the first vertex `(-3805, 1100, 1160) / 64` where the file has it; the first surface's shader is `Augentex`. Frame 0 of this file uses **no** compressed frame, so the compressed-offset path (scale 0.05, bias 127) is checked only on a synthetic file made from the same description, not on a real one. **The reader was not compared with Assimp's output** (no Assimp was run for this reader); there is no second reader to agree with.

### Provenance (what this file was written from)

`MdcReader.cpp` was written from **my own memory** of the layout of the format (header, surface, frame, tag and vertex records: field order and sizes, the latitude / longitude normal packing), from this repository's MD3 reader as the structural pattern, and from the byte-by-byte analysis of `spider.mdc`, which confirmed or corrected each field. **The compressed-offset constants (0.05 scale, bias 127, and the 1/64 position scale) were typed from memory of the game's headers. They are NOT verified on any real file** (frame 0 of `spider.mdc` is not compressed; its compressed frames are all 127, i.e. zero offsets, and no test of the reader reads them), **and the mutation test only shows that the code agrees with itself.** The 1/64 position scale and the normal packing, by contrast, are confirmed by the real file (positions and the 99.4 percent normal agreement). No code or struct names of the game or of any importer are used or quoted here, and no importer source (Assimp or any other project) was read, copied, ported or followed. Assimp was not run on these files either.

**Decision for the owner:** keep the compressed-first-frame path (unverified, but it only matters for files whose frame 0 is compressed, which the game's own tools did not seem to write), or refuse such surfaces until a real file shows the constants. Currently it reads them and says so in `notes`.

Unverified: a compressed first frame (no real example), the compressed offset scale, tag rotation, other MDC versions, files from games other than Return to Castle Wolfenstein, a run on real Windows.

## Our own MS3D reader (`.ms3d`)

*Was `docs/own_ms3d_reader.md` until 2026-11-20.*

Status: **side by side, off by default.** Until now MilkShape 3D files only imported in a build with Assimp. Nothing was removed.

### What it is

Written from the public description of the binary format (Mete Ciragan's file format notes, versions 3 and 4).

| File | What |
|---|---|
| `engine/src/Render/Ms3dReader.cpp` | `okay::ReadMs3dBuffer` / `okay::ReadMs3dFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 9 of the backend switch) |
| `scripts/gen_ms3d_fixtures.py` | Writes `tests/data/models/rig.ms3d` (version 4) and `rig_v3.ms3d` (version 3), synthetic |
| `tests/test_ms3d_reader.cpp` | Hand-computed checks, the seam, scene import, UTF-8 paths (`ms3d_reader`) |
| `tests/test_ms3d_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`ms3d_hostile`) |

### Turning it on

`-DOKAY_OWN_MS3D=ON`, environment variable `OKAY_OWN_MS3D=1` / `0`, or `SetImportBackend(".ms3d", ImportBackend::Own)` (the run-time choice wins). Off by default; with Assimp built, Assimp stays the default `.ms3d` reader.

### What it reads

- Header `MS3D000000` and version 3 or 4 (Assimp reads only 4; a version 3 file is read with the version 4 layout and says so in a note). Little endian throughout, every read bounds-checked.
- Vertices, triangles (three vertex indices, **per-corner** normals and texture coordinates), groups (name, triangle list, material index), materials (diffuse and emissive colour, shininess, transparency, texture and alpha map names), joints (name, parent name, rest rotation as radians in X, Y, Z order `Rz * Ry * Rx`, rest position, rotation and translation keys).
- One part per group; vertices are welded on position, normal and texture coordinate. Texture coordinates and the winding are kept as written (Y-up like the engine; Assimp's net result is the same). Materials: diffuse colour, emissive colour, shininess; the transparency is put in the diffuse alpha (and noted); texture paths have `\` turned into `/` and a leading `.\` dropped, and are relative to the model's folder.
- Joints become nodes under a root `MS3D`; parents are found by name (the first joint of that name; repeats get a suffix and a note). Vertices are bound to their joint; when the version 4 extra block is present (comments, then the per-vertex extra bone ids and weights in percent, sub-versions 1 to 3) up to four joints with weights are kept. Vertices with no joint are tied to the root node, so the skin does not pull them to the origin. The inverse bind is the inverse of the joint's absolute rest transform.
- Animation: one clip, `animation`; times are the file's seconds; each key is applied on top of the joint's rest values (`rest * key` for rotation, `rest + key` for position), the way MilkShape and Assimp do. Keys at negative or non-finite times are skipped, out-of-order lists sorted, both with notes.
- `flatten` (`ImportModel`, one merged static mesh): skins and clips are dropped.

### Limits

| Limit | Value |
|---|---|
| File size | 256 MiB |
| Vertices, triangles, groups, materials, joints | 65,535 each (the format's own limit) |
| Keys over all joints | 20,000,000 |
| Output vertices | 3,145,680 |
| Numbers | finite, within +-1e15 |

Every record count is checked against the bytes left before anything is read; an index past its list skips the triangle or the reference with a note (never reads outside); no recursion. A wrong magic, a version other than 3 or 4, a truncated list, a non-finite position fail the read.

### Not handled

- Comments are skipped (counted in a note); ambient and specular colours, the material mode, smoothing groups (normals come per corner anyway), the alpha map are not imported.
- The extra bone weights are read as percent (0 to 100) per the format notes; Assimp divides them by 255. No real file with the extra block was available to settle which is right, so this is **unverified**.
- Joint colour, group flags, and the extra per-vertex data after the weights are not read.
- No real file with joints was available: the four real files have none, so the skeleton, weights and animation paths were checked on synthetic fixtures only.

### Tests and what was checked

- `ms3d_reader`: a version 4 and a version 3 file, group parts and welding, per-corner normals, UVs as written, material fields and the path fix, joint nodes with rest rotation, inverse bind, weights from the extra block (75 / 25 percent), a vertex tied to the root, key times and `rest * key` composition, 11 kinds of malformed input, every truncation, a NaN position, a UTF-8 path, the seam, scene import with skin and clip.
- `ms3d_hostile`: every truncation of both fixtures, counts claiming 65,535 records in a tiny file, index values at and over the ranges, NaN / infinite / huge coordinates, keys at negative, NaN and 1e30 times, 3,000 keys per joint, a 5,000-joint chain, 60,000 groups, comment and weight blocks with counts of billions, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 150,000 mutations.
- Real files: four from an outside corpus (`docs/content/model_corpus.md`): `Wuson.ms3d`, `jeep1.ms3d`, `twospheres.ms3d`, `twospheres_withmats.ms3d`. Against Assimp built from source: mesh count, triangle count, surface area and bounding box agree on all four. Vertex counts differ because ours are welded (178 against 720 on twospheres).

### Before it can become the default

A real file with joints, weights and animation; more than four real files; Windows run; owner approval. Removing Assimp drops nothing for `.ms3d` once this reader is on, except version-4-only quirks we did not meet.

## Our own NDO reader (`.ndo`): a seam only, no geometry

*Was `docs/own_ndo_reader.md` until 2026-11-20.*

Status: **header recognised, body NOT read.** Off by default (`OKAY_OWN_NDO`, slot 36). Do not switch it on: it refuses every file.

### What exists

`engine/src/Render/NdoReader.cpp` checks that a file starts with the text `nendo ` and reports the version text that follows; then it refuses the file with the message "Nendo file, version ..., recognised but not read". A file without that start is refused as "not a Nendo file". Nothing is allocated from the file; the file read is capped at 256 MiB. The seam (slot 36, extension `.ndo`, `OKAY_OWN_NDO`, CMake option, build-default test) is in place so a real reader can be dropped in. Tests: `ndo_reader`, `own_dispatch_e3`, `own_default_e3`.

### Why there is no geometry code

- No real `.ndo` file exists in this repository, on this machine, or in Assimp's test tree.
- The only descriptions of the Nendo body that exist are other projects' importer source code. Rule 19a forbids reading or following it. A web search for a specification found only that, so **no description of the body was used and none was guessed** (the search summary's passing facts are listed in the disclosure below). Inventing a layout would produce a reader that looks finished and is not.
- **Disclosure**: that search's summary text mentioned, in passing, that the header is `nendo 1.x`, that the file is big endian and that counts changed width at version 1.2. Only the `nendo ` header test uses that. Nothing else of it is used, but those three facts were seen, so the body is not "clean-room" untouched: whoever writes the body later should know that.
- `ImportableExtensions()` does not list `.ndo`, even with `OKAY_OWN_NDO=1`, because the reader refuses everything (tested in `own_dispatch_e3`).

### What is needed to finish it

A format description we may follow (Izware's own, or a clean-room note by someone who did not copy code) or real files with a way to say what they contain (a Nendo install, to export the same model as OBJ and compare). Until then the format stays on Assimp. Honest status: **0% of the body.** Counting this row as done anywhere would be wrong.

### Provenance

Nothing beyond the header text `nendo ` was taken from anywhere. No importer source was read, copied or ported.

## Our own NFF reader (`.nff` `.enff`)

*Was `docs/own_nff_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_NFF`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of NFF files. Nothing was flipped.

### What it is

NFF, the "Neutral File Format", is Eric Haines' text format for ray-tracing test scenes (the Standard Procedural Databases): a viewpoint, lights, fill colours and then polygons, polygonal patches, spheres and cones or cylinders. Two flavours are in use and both are read:

- **Classic NFF** (the SPD scenes, many small demo files), with the common extensions: ellipsoids, `tess`, and the regular solids `tet`, `hex`, `oct`, `dod`, `ico`.
- **Sense8 "nff" object files** (WorldToolKit): an `nff` header line, a name, a vertex list (optionally with `uv`), polygons by index with `matid`, `both` and a texture name, and an `mtable` file of materials (`.mat`).

| File | What |
|---|---|
| `engine/src/Render/NffReader.cpp` | `okay::ReadNffBuffer` / `okay::ReadNffFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/TextReaderUtil.hpp`, `ReaderCommon.hpp`, `PolygonTriangulate.hpp` | Shared pieces: line and token reading, number parsing, mesh builder, polygon triangulation |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 26 (extensions `.nff`, `.enff`) |
| `tests/test_nff_reader.cpp`, `tests/batchd_fixtures.hpp` | Hand-computed checks on synthetic files (`nff_reader`) |
| `tests/test_batchd_hostile.cpp` | Hostile input and mutation fuzz (`batchd_hostile`) |
| `tests/test_batchd_budget.cpp` | The `mtable` path rules (`batchd_budget`) |

### What it reads

**Classic.** `f r g b [Kd Ks Shine T IOR] [texture]` sets the current material (the colour, the shininess when given, and a texture name; the other coefficients are read and not applied: `T` in the sample files is a stray `1`, which would make everything invisible). `p n` (n vertex lines) is a polygon, `pp n` a polygonal patch (`x y z nx ny nz`: the normals are kept); consecutive polygons with the same material become one part, flat shaded. `s x y z r [ry rz]` is a sphere, or an ellipsoid when two more radii follow (`r` is the x radius). `c` with two lines (`base x y z radius`, `apex x y z radius`) is a cone or cylinder with both end caps (an end of radius 0 is a point). `tet`, `hex`, `oct`, `dod`, `ico` take `cx cy cz r [ry rz]`: a regular solid of circumradius `r` (per axis when three are given), centred there. `tess n` (default 4, at most 6) sets the fineness: a sphere is an icosahedron subdivided `n` times (20 x 4^n triangles, smooth normals), a cone or cylinder has 4^n segments. `v` / `from` / `at` / `up` / `angle` / `hither` / `resolution`, `b` and `l` lines are skipped and counted. Each sphere, cone and solid is a part of its own.

**Sense8.** The first line `nff` (a `//` comment after it is allowed; comments are `//`). `version`, `viewpos`, `viewdir` and similar header lines are skipped; then the object name, optionally `mtable file.mat` and the vertex count; vertex lines `x y z [uv u v]`; the polygon count; polygon lines `n i0 i1 ... [matid k] [0xcolour] [both] [texture]` (0-based indices). Polygons with the same `matid` and texture make one part. The `.mat` file next to the model supplies the materials (`matdef` blocks: `ambientdiffuse` as the colour, `emission`, `shininess`, `opacity` as alpha). More than one object in a file are read one after the other.

### Not handled (each is a line in `out.notes`)

Lights, the viewpoint, the background and the resolution; the diffuse, specular, transmission and refraction coefficients of `f`; polygon colour words (`0x...`); the two-sided flag `both` (the engine's meshes are one-sided); polygons with fewer than 3 vertices; solids of zero or negative size; unknown keywords (counted, the first named). **The exact tessellation of other programs is not reproduced**: ours is the rule above.

### Limits

File 256 MiB; 1,000,000 vertices per polygon; 10,000,000 vertices in a Sense8 list and 10,000,000 polygons; 2,000,000 primitives; 40,000,000 output vertices; `tess` at most 6. Counts must be whole numbers within the caps and the text must really hold that many lines, or the file is refused before anything large is allocated. Numbers go through `NumParse.hpp` (finite, within 1e15, locale independent). The `mtable` file is read from the model's folder tree only (16 MiB at most): an absolute path, a drive letter, a `..` that climbs out and a path through a link that leads outside are refused (the polygons still read, a note says the table was refused).

### Tests and what was checked

- `nff_reader`: **synthetic** classic file (viewpoint, background, light, a red fill with shine 20, a polygon, a patch with normals, a `tess 1` cylinder, a green fill, a sphere and a cube solid): parts in file order, areas by hand (polygon 4, patch 4.5, cylinder `8 sqrt 2 + 4`, cube 8, sphere between the icosahedron's 9.5746 and 4 pi), triangle counts (cylinder 16, sphere 80, cube 12), unit-sphere vertices and normals, materials and shininess, notes; an ellipsoid with radii 1, 2, 3 (box and 320 triangles), the other solids (areas of the regular tetrahedron and dodecahedron by formula), `tess 0`; a synthetic Sense8 file with a real `.mat` file in a temporary folder (uv, matid, texture, alpha, both) with `\n` and `\r\n`; no table; refusals and **every truncation** of both flavours.
- `batchd_hostile` (NFF section): counts at, past and far past the caps with tiny text, `nan`, `inf`, `1e999` and friends in every kind of line, degenerate and huge cones and spheres, 600 spheres at `tess 6` and 300 cylinders at `tess 6` (the output cap), 100,000 polygons, a polygon of 100,000 vertices, a Sense8 file of 100,000 polygons, 5,000 fills, 3 MB tokens, `mtable` paths that climb out, name a folder or are 3 MB long; then 3,000 mutations of each flavour (30,000 in the sanitizer run).

#### Real files

Assimp's tree has 13 `.nff` files (`models/NFF/NFF`: the platonic solids, spheres, cones, a camera file; `models-nonbsd/NFF/NFFSense8`: `teapot`, `HOME4`, `cokecan` with its `.mat`). **All 13 read in ours; 12 read in Assimp (`cone.nff` fails there: it does not accept the texture name on its fill line) and on those 12 the area and bounding box agree**: the platonic solids `tetrahedron`, `hexahedron`, `octahedron`, `dodecahedron` exactly (same areas, same boxes, same triangle counts), `spheres.nff` (area 734.92, box, 15,380 triangles: the same), `cylinder.nff` (1,024 triangles, area 1,532.0748 against 1,532.0757), `positionTest.nff` (72,704 triangles, area 2,690.3634 against 2,690.3630), `ManyEarthsNotJustOne.nff` (51,200 triangles, 10 parts) and `WithCamera.nff`, and the Sense8 files `teapot.nff` (3,752 triangles, area 44.9005 both), `HOME4.NFF` (12,174 triangles, area 104,514.4052 both) and `cokecan.nff` (28 triangles, area 2,831.1551 both, with its `.mat`). The tessellation rule above was worked out from Assimp's output on small synthetic probes (sphere 20 x 4^n triangles; tube segments 4^n); the one place it differs is `tess 3`, where Assimp produces 260 triangles for a cylinder (not 4^3 x 4 = 256 and not a regular shape: its own quirk, not copied). The material counts differ (Assimp makes one per primitive, ours merges equal ones), ours groups the Sense8 polygons of `cokecan` into 9 parts and Assimp into 6, and `HOME4` is 1 mesh in ours and 2 in Assimp (triangles, area and box are equal on all 12).

Unverified: the NFF of other programs (SPD scenes beyond the sample files), `.enff` files (none in the tree), per-vertex colours, a run on real Windows.

### Provenance (what this file was written from)

`NffReader.cpp` was written from scratch for this project from the public description of the Neutral File Format (E. Haines, "A Proposal for Standard Graphics Environments", IEEE CG&A 1987, and the SPD distribution's NFF page), the ellipsoid and regular-solid extensions as they appear in the sample files, and the Sense8 WorldToolKit "nff" and "mat" file descriptions as far as the sample files show them; the platonic solids are generated from their definitions (the icosahedron's edge length and the dodecahedron's face directions are computed, not typed in). Sample files of Assimp's `test/models*/NFF*` were read as data (never the Assimp source). Assimp was used only as a **black box**: run on the samples and on small synthetic files made for this purpose, its output showed the `tess` rule, that extra numbers after a sphere's radius are the y and z radii, and the orientation and size of the regular solids. No code, comment, table or test from any other importer was read, copied or ported.

## Our own OFF reader (`.off`)

*Was `docs/own_off_reader.md` until 2026-11-20.*

Status: **side by side, off by default.** Until now `.off` only imported in a build with Assimp. Nothing was removed. This is one of the "formats Assimp reads that we would otherwise drop" steps of `docs/status/own_code_roadmap.md`.

### What it is

Written from the public description of the Object File Format (the Geomview `off` manual page).

| File | What |
|---|---|
| `engine/src/Render/OffReader.cpp` | `okay::ReadOffBuffer` / `okay::ReadOffFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/TextReaderUtil.hpp` | shared by the text readers added with it: capped file read through a UTF-8 path, UTF-8 / UTF-16 text handling, line splitter, strict locale-independent numbers (`NumParse.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 6 of the backend switch); the one-static-mesh (`flatten`) step for the readers added after Collada / 3DS |
| `scripts/gen_off_fixtures.py` | Writes `tests/data/models/cube.off`, `cube_crlf.off`, `square_stnoff.off`, `mixed_coff.off` (synthetic) |
| `tests/test_off_reader.cpp` | Hand-computed checks, the seam, UTF-8 paths, locale (`off_reader`) |
| `tests/test_off_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`off_hostile`) |

### Turning it on

Same rules as the 3DS and BVH readers: `-DOKAY_OWN_OFF=ON`, environment variable `OKAY_OWN_OFF=1` / `0`, or `SetImportBackend(".off", ImportBackend::Own)` (the run-time choice wins). Off by default. With Assimp built, Assimp stays the default `.off` reader and ours is the choice when asked for.

### What it reads

- Header words `OFF`, `NOFF` (per-vertex normals), `STOFF` (per-vertex texture coordinates), `COFF` and the combinations (`STCNOFF`), any case. The counts (`vertices faces edges`) may follow on the header line; the edge count is not needed. `#` comments and blank lines are skipped; line breaks may be `\n`, `\r\n` or `\r`; a UTF-8 or UTF-16 byte order mark is handled.
- Vertices: `x y z`, then the normal, the colour and the texture coordinates in that order when the header announces them. A colour has 3 or 4 values.
- Faces: `n i0 ... in-1`, any number of corners (up to 65,536). Convex polygons are fanned, concave ones ear clipped (the same step the OBJ, PLY and COLLADA readers use). Anything after the indices (a face colour) is dropped with a note.
- One part, one root node. Without normals, smooth normals are generated. Units, axes, winding and texture coordinates are kept as written.

### Limits

| Limit | Value |
|---|---|
| File size | 512 MiB |
| Vertices / faces / output triangles | 10,000,000 / 10,000,000 / 30,000,000 |
| Corners per face | 65,536 |
| Coordinates | finite, within +-1e15 |

The vertex and face counts are checked against the bytes left in the file (every vertex or face needs at least 6 bytes) before anything is allocated, so `OutOfMemory.off` from Assimp's test files (which claims 353,535,235,358 vertices) is refused with a message instead of an out-of-memory kill. A vertex or face list that ends early fails the read.

### Named warnings (`ImportedModel::notes`, also logged)

Vertex colours not imported; face colours not imported; vertex lines with more values than the header announces; faces with fewer than 3 corners; faces with an out-of-range index; degenerate triangles; texture coordinates kept as written; UTF-16 converted.

### Not handled

- `4OFF` (homogeneous coordinates) and `nOFF` (n dimensions) fail the read with a message.
- Colours are not imported (the mesh type has no per-vertex colour).
- Vertex and face records must be one per line (the usual layout); a face split over lines is read as two broken records and fails.
- Not benchmarked beyond: `Wuson.off` (150 KB, 3,205 vertices) reads and builds a scene in about 5 ms.

### Tests and what was checked

- `off_reader`: both line-ending variants, comments, the header variants, normals and texture coordinates, a concave polygon, short and out-of-range faces, 22 kinds of malformed input, UTF-16, a German decimal-comma locale (skipped when none is installed), a UTF-8 path with characters outside the ANSI code page, the seam, scene import and the merged-mesh path.
- `off_hostile`: every truncation of every fixture, counts at and over the caps, 60,000-corner polygon, zig-zag concave polygon, NUL bytes, non-finite and overflowing numbers, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 400,000 mutations.
- Real files: two from an outside corpus (`docs/content/model_corpus.md`). Against Assimp built from source: `Cube.off` and `Wuson.off` agree on triangle count, area and bounding box. `invalid.off` (declares 4 faces, has fewer) is read by Assimp as 2 triangles of zero area and refused by ours; `OutOfMemory.off` kills Assimp's process and is refused by ours.

### Before it can become the default

Run on OFF files from more than one source (we have two); Windows run; owner approval. Removing Assimp drops nothing for `.off` once this reader is on.

## Our own OGRE mesh readers (`.mesh.xml` and binary `.mesh`)

*Was `docs/own_ogre_reader.md` until 2026-11-20.*

Status: **side by side, off by default**: the XML form behind `OKAY_OWN_OGRE` (slot 23), the binary `.mesh` behind `OKAY_OWN_OGREBIN` (slot 43, added 2026-11-16 in the "Assimp gap" batch, `docs/status/own_code_roadmap.md`, section "What still needs Assimp"); same rules as `docs/content/model_readers.md`. Assimp (when built) is still the only other reader of OGRE meshes. Nothing was flipped. **Skeletons are not read** in either form (`.skeleton`, `.skeleton.xml`; see "Not done").

### What it is

OGRE is an open 3D engine; its tools write meshes as a binary `.mesh` file and, through the `OgreXMLConverter`, as XML (`name.mesh.xml`, with the skeleton in `name.skeleton.xml`). Exporters for Blender, 3ds Max and Maya write the XML form. A `<mesh>` holds shared vertex data, a list of `<submesh>` elements (each with faces and optionally its own vertex data) and links to a skeleton.

| File | What |
|---|---|
| `engine/src/Render/OgreXmlReader.cpp` | `okay::ReadOgreXmlBuffer` / `okay::ReadOgreXmlFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/XmlLite.hpp`, `TextReaderUtil.hpp`, `ReaderCommon.hpp` | Shared strict XML reader (caps; DOCTYPE skipped, entities refused), UTF-16 conversion, number parsing, mesh builder |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 23 of the opt-in table; the extension is the last **two** parts, `.mesh.xml` (`ExtensionOf` was taught this; other `.xml` files are not routed here) |
| `tests/test_ogre_reader.cpp`, `tests/batchd_fixtures.hpp` | Hand-computed checks on synthetic files (`ogre_reader`) |
| `tests/test_batchd_hostile.cpp` | Hostile input and mutation fuzz (`batchd_hostile`) |
| `tests/test_batchd_budget.cpp` | Strips and fans with hand-computed triangles, per-file budgets, scene paths (`batchd_budget`) |
| `engine/src/Render/OgreXmlReader.cpp` (second half) | `okay::ReadOgreBinaryBuffer` / `okay::ReadOgreBinaryFile`: the binary `.mesh`. The submesh-to-part step, the material scripts and the notes are **shared** with the XML reader, so both forms of one mesh give the same model |
| `tests/test_gap_readers.cpp`, `tests/gap_fixtures.hpp` | Binary fixtures (the binary twin of the XML fixture), truncation, mutation sweep, memory and time caps (`gap_readers`) |
| `tests/test_own_dispatch_gap.cpp` | `.mesh` reaches slot 43, `.mesh.xml` stays on slot 23, the switch `OKAY_OWN_OGREBIN` (`own_dispatch_gap`) |

### What it reads

- **Geometry.** `<sharedgeometry>` and each submesh's `<geometry>`: `vertexcount` and any number of `<vertexbuffer>` elements. Vertex `k` of every buffer is vertex `k` of the geometry, so positions and normals in one buffer and texture coordinates in another work (the usual layout). `<position>`, `<normal>`, the first `<texcoord>`. The `vertexcount` must equal the number of `<vertex>` elements, or the file is refused before anything is allocated.
- **Submeshes.** One part per `<submesh>` that has faces. `usesharedvertices` picks the shared or the own geometry; `operationtype` picks how the `<face>` elements are read: `triangle_list` (default) needs `v1 v2 v3` on every face; `triangle_strip` and `triangle_fan` read an index sequence (the first face gives its indices, every later face adds its last one, which is `v3` when a face carries all three and `v1` when it carries only the new index) and unroll it: a strip alternates its winding (triangle i is `s[i] s[i+1] s[i+2]` for even i and `s[i+1] s[i] s[i+2]` for odd i), a fan shares the first vertex (`s[0] s[i+1] s[i+2]`); repeated indices make degenerate triangles, which are dropped as before. (An earlier version read every face as an independent triangle whatever the operation type, which flipped every second triangle of a strip written as sliding triples; `tests/test_batchd_budget.cpp` now checks strips and fans against hand-computed triangles in both ways of writing them.) An index past the vertex list skips that triangle. Line and point lists are skipped. The part is named after `<submeshname>` or the material.
- **Materials.** The submesh's `material` name is the material name. When `.material` scripts are given (`ReadOgreXmlFile` reads the ones next to the mesh: the one named like the mesh first, at most 32 files of 4 MB) the first pass's `diffuse`, `emissive` and `specular`'s shininess and the first texture unit's `texture` are read (the script language's `material name : parent { technique { pass { texture_unit { ... } } } }`; names after block keywords, comments and quoted strings are handled).
- Coordinates, winding and texture coordinates **as written** (OGRE is right-handed and Y-up; the engine's Assimp path flips v on top of Assimp's own flip, which is the file's value).

### The binary `.mesh`

The file is a 16-bit header id (`0x1000`) and a version line (`[MeshSerializer_v1.8]` in the three real files), then chunks: a 16-bit id and a 32-bit length. **The lengths are not reliable for skipping**: in the real files the length of the mesh chunk is 117 bytes short of what it holds (the serializer leaves text fields out of some of its sums; the skeleton files show the same for bone names), so the reader reads the chunks it knows field by field in the order the format writes them, and treats a chunk id that cannot be a child of the chunk being read as the end of that chunk. Only chunks it does not read (edge lists, levels of detail, poses, animations, extremes) are skipped by their length; a length that runs past the end stops the reading there with a note, and the submeshes before it are kept.

Read: the mesh chunk (`0x3000`); shared geometry (`0x5000`) and each submesh's own geometry: vertex count, the vertex declaration (`0x5100`, elements `0x5110`: buffer, type, meaning, offset, index) and the vertex buffers (`0x5200` with data `0x5210`); positions, normals and the first texture coordinate set when they are 32-bit floats; submeshes (`0x4000`: material name, shared flag, 16- or 32-bit indices, operation `0x4010`: list, strip or fan, as in the XML form; lines and points are skipped); the submesh name table (`0xA000` / `0xA100`); the skeleton link (`0x6000`, reported, not read). Bone assignments (`0x4100`, `0x7000`) are stepped over. Little-endian files only: a big-endian file (header bytes `10 00`) is refused with that reason. Versions other than 1.8, 1.10 and 1.100 are read with a note that says they were not checked against a real file.

Before anything is allocated from a vertex count, the positions must sit in a vertex buffer whose data lies inside the file (12 bytes per vertex), so a small file cannot ask for big arrays; index lists must lie inside the file too. A position, normal or texture coordinate that is not a finite number refuses the file.

### Not handled (each is a line in `out.notes`)

The skeleton link and `<boneassignments>` (the mesh is imported rigid, at its bind pose), poses and morph animation, levels of detail, vertex colours, tangents, second and later texture coordinate sets, everything in material scripts beyond the first pass colours and first texture (parents, other techniques, shaders, blend modes).

### Not done

- **The editor does not recognise `.mesh.xml` by itself.** Its project panel and drag-and-drop look at the last extension (`.xml`), so only the Import Model dialog (whose file filter lists `*.mesh.xml`) and `ImportModelData` / `ImportModel` take the file. Teaching the editor's `IsModelExt` the two-part extension is not done.
- **Binary `.mesh`: done** (2026-11-16, above). Not read in it: vertex elements that are not 32-bit floats (16-bit texture coordinates, packed normals), big-endian files.
- **`.skeleton` and `.skeleton.xml`**: bones, bind pose, animations, weights. Not started in either form; it needs the skin path the other skinned readers use. Assimp reads them (fish has 4 bones and a clip, ninja 21 bones and 20 clips), so a skinned OGRE mesh still imports rigid with ours.

### Limits

File 256 MiB; 8,000,000 vertices per geometry; 20,000,000 faces; 100,000 submeshes; 40,000,000 output vertices; the XML reader's caps (2,000,000 elements, nesting 256, 256 attributes per element). Numbers go through `NumParse.hpp` (finite, within 1e15). Material scripts: 4 MB each and 2,000,000 words.

### Tests and what was checked

- `ogre_reader`: **synthetic** mesh (shared geometry over two vertex buffers with normals and uv, a second submesh with its own geometry and no uv, `<submeshnames>`, a skeleton link and bone assignments) and a synthetic material script (parent, two passes, texture unit with a name): triangles, area 10 (by hand), uv and normal numbers, names, material colours, shininess and texture path, notes, refusals (empty, wrong root, no submeshes, a wrong `vertexcount`, a bad number, shared vertices that do not exist, an index past the vertices, line lists, a DTD entity) and **every truncation**, with `\n` and `\r\n`.
- `batchd_hostile` (OGRE section): counts at, past and far past the caps with tiny text, `nan`, `inf`, `1e999` in positions, texture coordinates and indices, an entity bomb, 5,000 levels of nesting, 100,000 empty submeshes, a 3 MB number, 50,000 submeshes on shared vertices, a valid 60,000-triangle mesh, and material scripts of 100,000 `{`, 100,000 `}`, 20,000 `material` blocks, a 2 MB name; then 3,000 mutations of the file (30,000 in the sanitizer run).

#### Real files

Assimp's tree has `fish`, `ninja`, `razor` (`.mesh.xml` and binary `.mesh`, `OgreSDK`) and `TheThing/Mesh.mesh.xml`. **All 4 `.mesh.xml` files read and equal Assimp on triangles, mesh count, surface area and bounding box** (fish 815 triangles area 34.8947; ninja 1,008, 2 meshes, 25,219.6177; razor 468, 29,764.2994; TheThing 2,656, 1.3468), and each gets the texture its `.material` names (`fish.jpg`, `Wrinkles.jpg`, ...). Not compared: skeletons and animation (Assimp reads them, ours does not).

**Speed of the XML reader (2026-11-16, branch `claude2/own-readers-gap-2`).** The XML form is now parsed by `xml::ParseFlat`
(`XmlLite.hpp`): the same accept / refuse rules and messages as `xml::Parse`, but element and attribute names and values are
offsets into the file text instead of a `std::string` each, and character data is checked, not kept. Same output (digest
equal on all four files); 0.42x Assimp's read time and 0.54x its largest peak heap over the files both read (was 1.03x and
1.04x); every file faster and lighter (`docs/content/own_readers_speed.md`, third round).

**Binary `.mesh` (2026-11-16).** All 3 binary meshes read and give **exactly the parts, triangles and box of their XML twins**: razor's model is bit for bit the same as the XML reader's (same digest); fish and ninja differ only in the last digits of positions (the XML text was rounded to about 6 significant digits; the binary floats are the exporter's values, at most 4.6e-5 apart) and in the skeleton name in the note. Against Assimp: fish 815 / 815 triangles, 530 / 530 vertices, ninja 1,008 / 1,008 and 842 / 842, razor 468 / 468 and 588 / 588, boxes equal; ninja has 1 material to Assimp's 2 (ours shares a material between submeshes with the same name, as the XML reader does); Assimp also gives the skeleton (nodes, bones, clips), ours does not. Speed and memory (`docs/content/own_readers_speed.md`, row `mesh`): 0.12x Assimp's read time and 0.25x its largest peak heap; every file faster and lighter.

Unverified: a mesh with several uv sets, 32-bit-index meshes, strips and fans on a real exporter file (the two ways of writing the face elements are an assumption from the format description, checked on synthetic data only), files from exporters other than the SDK converter and the Blender exporter, a run on real Windows.

### Provenance (what this file was written from)

The binary part was written from the public description of the OGRE binary mesh file (the chunk ids and field order of the mesh serializer as the OGRE manual and its file-format notes describe them) and checked byte by byte against the three binary `.mesh` files of Assimp's test folder with a small dump script written for this work; the vertex element type and meaning numbers were checked on those files (float3 position, float3 normal, float2 texture coordinates). No OGRE or Assimp source was read.

`OgreXmlReader.cpp` was written from scratch for this project from the public description of the OGRE mesh XML format (the element and attribute names of the `OgreXMLConverter` output as documented in the OGRE manual) and of the OGRE material script language, and from reading the four sample `.mesh.xml` files and the `.material` files of Assimp's `test/models*` as data (never the Assimp source). Assimp was used only as a **black box** (counts, area, box and texture names on the sample files). No code, comment, table or test from any other importer was read, copied or ported.

### Memory (2026-11-16)

The reader held 1.03x to 1.14x Assimp's peak heap on three of the four corpus files. Two changes, output unchanged
(`bench_model_readers --digest` and `--import-digest` identical on every corpus file):

- The XML reader (`XmlLite.hpp`) has a `keepText` switch (on by default, so the 3MF, AMF, X3D, XGL and Irrlicht readers are
  unchanged). The OGRE reader turns it off: character data is still checked (the limits, entity references) but not stored.
  An OGRE mesh is all elements and attributes, and the white space of a pretty-printed file gave every parent element its own
  text string on the heap.
- The file's bytes are freed once the document is parsed, before the meshes are built.

Peak heap of one read, ours / Assimp (MB): `fish` 1.79 -> 1.35 / 1.73, `ninja` 2.47 -> 1.89 / 4.20, `razor` 1.17 -> 0.91 /
1.02, `TheThing/Mesh` 3.49 -> 2.78 / 3.37. Every file is now below Assimp (`docs/content/own_readers_speed.md`).

Later (the integration of `claude2/own-readers-gap-2`): the reader moved to `xml::ParseFlat`, which stores offsets into the
file text instead of copies, so the file's bytes now stay alive while the meshes are built and the `keepText` switch is no
longer used by OGRE (it stays in `XmlLite.hpp`, default on). That parser keeps less again: `TheThing` 1.82 / 3.37 MB.

## Our own PMX reader (`.pmx`)

*Was `docs/own_pmx_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_PMX`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of PMX files. Nothing was flipped. **Unverified against any real file**: there is no `.pmx` file in this repository (Assimp's `Alicia_blade.pmx` is not in the tree), so every check below is on files built by hand from the structure of the format.

### What it is

PMX is the MikuMikuDance (MMD) model format, version 2.0 and 2.1: a little-endian binary file with a header that picks the text encoding and the size (1, 2 or 4 bytes) of each kind of index, then vertices, faces, a texture list, materials, bones and morphs (and then display frames, rigid bodies, joints and, in 2.1, soft bodies).

| File | What |
|---|---|
| `engine/src/Render/PmxReader.cpp` | `okay::ReadPmxBuffer` / `okay::ReadPmxFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp`, `SceneRefPath.hpp`, `TextReaderUtil.hpp` | Shared pieces: the bounds-checked cursor, the folder rule for file names, the parent-loop cutter |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 39 (extension `.pmx`). Slots 27 to 38 belong to other branches (see below) |
| `tests/test_pmx_reader.cpp`, `tests/pmxlws_fixtures.hpp` | Hand-computed checks on synthetic files (`pmx_reader`) |
| `tests/test_pmxlws_hostile.cpp` | Budgets, memory (counting `operator new`) and big files (`pmxlws_hostile`) |
| `tests/test_own_dispatch_e5.cpp`, `tests/test_own_pmx_lws_default.cpp` | The seam routes `.pmx` to this reader; `-DOKAY_OWN_PMX=ON` really makes it the default |

### What it reads

- **Header.** The signature `"PMX "`, the version (2.0 or 2.1; anything else, and NaN, is refused), the globals count (at least 8; extra globals from a later version are skipped), text encoding (UTF-16LE or UTF-8), the number of extra UV sets (0 to 4, skipped per vertex), and six index sizes (each 1, 2 or 4; anything else is refused). The four model texts (names and comments) are read to find the next field; the model's name becomes the name of the root node.
- **Vertices.** Position, normal, UV; weights of type BDEF1, BDEF2, BDEF4, SDEF (read as a two-bone linear blend: the C, R0 and R1 vectors are skipped and counted in the notes) and QDEF (type 4, only from version 2.1, read as four weights). The four strongest influences are kept, normalised; a vertex with no valid bone stays in place and follows an extra root bone (named like the root node).
- **Faces and materials.** One part per material, in the order of the face list; the vertices a part uses are welded into the part. The materials' surface counts must add up to no more than the face list (more is refused; less leaves the rest as one part without a material). Diffuse colour and alpha, and the specular power as the shininess. **Only the diffuse texture** is read: the ambient and specular colours, the edge, the sphere maps, the toon textures and the double-sided flag are counted in a note.
- **Coordinates.** PMX is left-handed with clockwise faces. Z is mirrored and the winding reversed (the same rule as the B3D and DirectX readers), so the result is right-handed and counter clockwise. UVs are kept as written (top-left origin, like DirectX). Units are kept (MMD models are small, about 0.08 metre per unit).
- **Bones.** Each bone becomes a node under a root node: the position is the bone's world position (z mirrored) minus its parent's; no bone has a rest rotation. Every part carries the whole bone list with inverse bind matrices that take the bone's own position to the origin. Parents are ordered parent-first; a bone that names itself, a parent index that does not exist, and loops are all cut (the bone becomes a root, counted in the notes). Names are made unique.
- **Morphs.** Group, vertex, bone, UV (and UV 1 to 4), material and, in 2.1, flip and impulse morphs are **walked and every index checked** but not imported: the engine has no morph type. The notes say how many of each kind were seen and how many offsets point at a vertex, bone, material or morph that does not exist. A morph of an unknown type stops the walk (its size is unknown); the model is still read.
- **IK, inherit-rotation, inherit-move, fixed and local axes and external parents** are read (so the record sizes are right) and counted, not applied.

### Not read (each is a line in `out.notes`)

Display frames, rigid bodies, joints and soft bodies (not even walked: nothing after the morphs is parsed, so damage there is not noticed); the texture of a sphere map or toon; edge drawing; SDEF's C / R0 / R1; morph effects; IK and inheritance. PMD (`.pmd`, the older format) is another format and is not routed here. There is no animation in a PMX file (motions are separate `.vmd` files, also not read).

### Limits (per file)

File 256 MiB; 2,000,000 vertices; 12,000,000 face indices (4,000,000 triangles); 100,000 each of bones, morphs, materials and textures; 8,000,000 morph offsets in all; 4,096 IK links per bone; a text at most 1 MiB; at most 4,000,000 output vertices; **bones times parts at most 4,000,000** (every part carries the whole bone list, so 100,000 bones in 100,000 materials would be ten billion entries). Every count is read as a signed 32-bit number: a negative one (0xFFFFFFFF) is refused; a count over its limit is refused with the word "(budget)" in the message; a count the rest of the file cannot hold (the records have a known smallest size) is refused **before anything is reserved**. Every float that matters is checked finite and within 1e15; positions that are not are refused, normals and UVs that are not are replaced and counted. Index sizes are checked against the header and every index against its list (vertex indices in faces, bone indices in weights, texture indices in materials); an index that is out of range drops that influence, that triangle or that texture and is counted. UTF-16 text must have an even length; a lone surrogate becomes U+FFFD. Texture file names go through `SceneRefPath.hpp::ResolveInsideDir`: an absolute path, a drive letter, `..` that climbs out of the model's folder and a link that leads out are refused (the model is still read, without that texture, and a note names the first refused name). Arithmetic on counts is done in 64 bits (`long` is 32-bit on Windows); there is no recursion; any exception is turned into a refusal by `RunReaderGuarded` at the seam.

### Tests and what was checked

- `pmx_reader` (5,841 checks): a **synthetic** model of 6 vertices, 2 materials, 3 bones and 5 morphs built in five variants (UTF-16 and UTF-8; index sizes 2/1/1/2/1, 1/1/1/1/1 and 4 for all; extra UV sets 0, 2 and 4; versions 2.0 and 2.1, with QDEF and the flip morph in 2.1). Every number is checked by hand: the node positions (z mirrored, relative to the parent), the welded vertices, the reversed triangles (0,2,1) (0,3,2), the counter clockwise normal of the first triangle, each weight type's joint indices and weights (BDEF1 1; BDEF2 0.75 / 0.25; BDEF4 0.5 / 0.5; SDEF 0.25 / 0.75; QDEF 3 and 1 normalised to 0.75 / 0.25; no bone follows the root bone), the inverse bind matrices, the material colour, alpha, shininess and texture path, and the notes. Then: every header field (signature, versions 1.0, 3.0 and NaN, 7 globals, encoding 2, 6 index sizes at 0, 3 and 8, texts of -1, 0x7FFFFFFF, an odd length and one past the end); a header with extra globals; a lone surrogate; **every truncation** before the end of the morphs (both encodings), all refused with no model left behind, and the file cut exactly after the morphs, which is read; **every byte replaced by 0xFF, 0x00 and 0x7F** and 3,000 random bit flips (only a refusal or a sane model); counts of 0xFFFFFFFF, over the budget, and more than the file holds for each of the six lists; offset counts in a morph; an unknown morph type; surface counts that are not multiples of 3, negative or too many; NaN and 2e15 positions; a bad normal; weight types 9 and QDEF in a 2.0 file; a bone index and a face index past their lists; every triangle bad; bone parent loops (two bones, three bones), self parents and parents that do not exist; IK link counts; and the texture paths `../../etc/passwd`, `..\up.png`, `/etc/passwd`, `\\host\share\x.png`, `C:\win\x.png`, `c:/x.png` and two that climb out through a subfolder (all dropped, the model read) against `a.png`, `sub\dir\b.png`, `./c.png` and `sub/../d.png` (kept).
- `pmxlws_hostile` (PMX part, 85 checks in all with the LWS part): nine small files that claim up to 12,000,000 indices or 100,000 records, refused in 0.00 s with a peak under 1 MB; 300,000 vertices and 500,000 triangles (17.7 MB, read in 0.06 s, peak 59 MB); the largest legal face list (4,000,000 triangles, 48 MB: peak 141 MB); 4,000,001 triangles refused; 2,000 bones in 2,500 parts refused (and 100 in 100 read). Clean under AddressSanitizer, UndefinedBehaviorSanitizer and `float-cast-overflow`.
- `own_dispatch_e5` and `own_pmx_lws_default`: the seam sends `.pmx` to this reader; the environment variable and `SetImportBackend` switch it; with `-DOKAY_OWN_PMX=ON` it is the default (and the test fails if the engine did not get `OKAY_OWN_PMX_DEFAULT`).
- **Mutation checks** (each a deliberate bug in a copy of the reader; the named test fails): the winding left as written; z not mirrored; the vertex budget raised to 3,000,000; the "multiple of 3" check removed; the texture path joined to the folder without `ResolveInsideDir`; the parent-loop cutter replaced by the identity.

### Unverified

Real `.pmx` files (none here); PMX 2.1 soft bodies and the 2.1 joint types (not parsed); that the winding and mirror rule matches what MMD shows (it follows the left-handed, clockwise convention of the format, reasoned, not compared with a viewer); SDEF as a plain two-bone blend (a simplification: the real SDEF deformation is nonlinear); a run on real Windows. The comparison with Assimp that the other readers have was **not done** (no real file and no Assimp build here).

### Provenance (what this file was written from)

`PmxReader.cpp` was written from scratch for this project from the **public PMX 2.0 / 2.1 file format description** (the header with its eight globals, the text type, the vertex, face, texture, material, bone and morph records and the index-size rules), as I know that description; **no network access was used and no file, document or source was opened while writing it**. **No importer source code, snippet or table of Assimp or of any other project was read, copied or ported**, and no sample file was read. The test files were written by hand from the structure above. Assimp was not run.

## Our own Q3D reader (`.q3o` `.q3s`)

*Was `docs/own_q3d_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_Q3D`, same rules as `docs/content/model_readers.md`). Assimp (when built) is the only other reader. Nothing was flipped. **Checked on the four Quick3D files of the model corpus** (`docs/content/model_corpus.md`; not in this repository): the description left three points of the layout open, and the first version of the reader guessed two of them wrong, so it refused all four files. The layout below is the one those files have (see "Checked against real files"); triangle counts, material counts and bounding boxes now equal Assimp's on all four.

### What it is

Quick3D is an old Mac and Windows modeller. A `.q3o` file is one object, a `.q3s` file is a scene (the same plus a scene chunk). Both are binary.

| File | What |
|---|---|
| `engine/src/Render/Q3dReader.cpp` | `okay::ReadQ3dBuffer` / `okay::ReadQ3dFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 31 (extensions `.q3o`, `.q3s`) |
| `tests/test_q3d_reader.cpp`, `tests/batche2_fixtures.hpp` | Hand-computed checks on a hand-built file (`q3d_reader`) |
| `tests/test_batche2_hostile.cpp` | Truncations, count and index overwrites, bit flips (`batche2_hostile`) |
| `tests/test_batche2_budget.cpp` | Budgets and memory with a counting `operator new` (`batche2_budget`) |
| `tests/test_own_dispatch_e2.cpp` | The seam routes `.q3o` / `.q3s` to this reader (`own_dispatch_e2`) |

### What it reads

The 22 byte header (signature `quick3Do` or `quick3Ds`, a 2 character version, the number of meshes, materials and textures), then chunks, each one type byte: `m` meshes, `c` materials, `t` textures, `s` scene. Each mesh: vertices, faces (a 16-bit corner count per face, then the vertex indices), a material index per face (-1 = none), normals, a **texture coordinate count (always there)**, and, only when the header names textures, the texture coordinates and per-corner texture indices; then a centre of mass and a bounding box (skipped). Each material: a NUL-ended name, ambient, diffuse and specular colours, a transparency and a **texture index** (-1 = none). A scene chunk is taken in any file and ends the read (the corpus file `WusonOrange.q3s` says `quick3Do` and has one; bytes after it are ignored). Faces are triangulated: up to 32 corners convex ones are fanned and concave ones ear clipped; bigger ones, and every polygon once the per-file ear clipping work (sum of corners cubed) passes 400,000,000, are fanned from their first corner (a note says so; a concave one may then come out wrong). One part per mesh and material; materials carry the diffuse colour, and the transparency as `alpha = 1 - transparency`, except that a transparency of 1 (fully clear) is read as opaque, with a note: the glass materials of the corpus carry 0.12 and 0.18 there, but the only material of `WusonOrange` carries exactly 1, which would make the whole model invisible (Assimp's materials come out opaque on all four files). Stored normals are used when there is one per vertex and all are usable; otherwise smooth normals are made.

### Not handled (each is a line in `out.notes`)

Ambient and specular colours; the embedded textures (skipped, not decoded; the material's texture index is read past); a texture coordinate count in a file whose header names no texture (`E-AT-AT.q3o` has one of 230 with no coordinates after it: counted in a note, no UVs); the scene chunk (placement, camera, lights, background, which is only skipped); faces with fewer than 3 corners; faces with an out-of-range index (skipped and counted).

### Checked against real files (2026-11-16)

The four corpus files (`models/Q3D/earth.q3o`, `E-AT-AT.q3o`, `WusonOrange.q3o`, `WusonOrange.q3s`) were walked byte by byte with a throw-away script that follows the layout above, and every one ends exactly at its last byte (the `.q3s` at its scene chunk). What the bytes settled:

1. **Byte order:** little endian in all four (the big endian fallback stays for other files).
2. **The texture coordinate count is always there**, also when the header names no texture; the coordinates and their indices are not. The first reader left the count out, read it as the next mesh's vertex count and refused `earth` and `E-AT-AT` ("bad vertex count").
3. **A material ends with a 4-byte texture index** after the transparency (-1 in all four files). The first reader did not read it, so after the first material it was 4 bytes off: "an unexpected or repeated chunk type 158" on both `WusonOrange` files.
4. **The scene chunk in a `quick3Do` file** (`WusonOrange.q3s`): read like in a `quick3Ds` file.
5. **Normals:** one per vertex in every mesh of the four files (used when all are usable; `earth` and `E-AT-AT` each have a mesh with zero normals, so normals are made there, noted).
6. **Transparency:** 0 = opaque, small values for glass, and 1 on `WusonOrange`'s only material, read as opaque (above). **Up axis, winding and V direction** are kept as written.

`bench_model_readers --compare` (Assimp run from the same build, its code not read) on the four files: triangles 11,156 / 2,937 / 3,732 / 3,732, materials 7 / 2 / 1 / 1 and the bounding boxes equal Assimp's, except the `.q3s` box: Assimp applies the scene chunk's placement and ours does not read it (documented above). Parts and vertices differ by design: ours makes one part per mesh and material (35 on `E-AT-AT`, Assimp merges per material into 7) and welds vertices per mesh (6,262 against 6,238).

Still open: a file **with** textures (none in the corpus): whether its texture coordinates, per-corner indices and texture records are laid out as the description says is unverified.

### Limits

File 256 MiB; 100,000 meshes, materials and textures each; 4,000,000 vertices and 4,000,000 faces per mesh; 4,096 corners per face; 16,000,000 face corners, 400,000,000 units of ear clipping work, 4,000,000 triangles and 4,000,000 output vertices per file. Every count is checked against the bytes really left in the file before anything is allocated (a 300 byte file that claims 2,000,000 vertices is refused with no allocation). Floats must be finite and within 1e15. The file is parsed as little endian and, only if that fails, as big endian; a header read in the wrong order gives counts over the limits and fails at once, so the second parse costs more than the first only for a header whose bytes read the same both ways, and the total is at most twice one parse. No recursion. A reader that throws is turned into a refusal by `RunReaderGuarded`. The reader names no other file, so `ResolveInsideDir` is not needed.

### Tests and what was checked

- `q3d_reader` (131 checks): the fixture follows the real layout (texture coordinate count always written, a texture index per material); also the three corpus cases (a count of 230 with no coordinates, a scene chunk in a `quick3Do` file with bytes after it, transparency 1 read as opaque).
- `model_corpus`: the four real files (rows `q3d` in `tests/data/model_corpus_manifest.tsv`).
- Earlier checks, still in `q3d_reader`: a hand-built file (a unit quad and a triangle, two materials, four texture coordinates), in little and big endian, as object and as scene: parts, vertex and triangle counts, areas (1 + 1), every UV against its vertex, alpha 0.75, notes; the same without textures; refusals. Mutation checks (code broken, test red, code restored): alpha formula, texture index.
- `batche2_hostile` (shared with RAW): every truncation, every 4-byte window of the file set to 0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF and 0x00FFFFFF, 5,000 bit flips per variant, 300 garbage files; 26,279 inputs read and 15,120 refused in all; every result checked for sanity (finite, indices in range). Also clean under `-fsanitize=address,undefined,float-cast-overflow`. This test found a real bug (texture coordinates that were NaN or infinite reached the mesh); it is fixed.
- `batche2_budget`: 4,000,000 triangles read (peak 446 MB, 1 s); one quad more refused naming the budget; 4,200,000 output vertices refused (peak 538 MB); counts that claim more than the bytes hold refused with no allocation (a face-size claim of 16,000,000 corners in 8 KB peaks under 1 MB; removing that check turns the test red).

### Provenance (what this file was written from)

`Q3dReader.cpp` was written from the public description "Quick3D Object File" on Paul Bourke's format pages (the header, the chunk types and the field lists). No importer source, and no code or snippet from any other project, was read. The 2026-11-16 fixes come from the bytes of the four corpus files (above); the description page itself could not be reached from the machine then. Assimp was only run (through `bench_model_readers --compare`) to compare counts and boxes.

## Our own RAW reader (`.raw`)

*Was `docs/own_raw_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_RAW`, same rules as `docs/content/model_readers.md`). Assimp (when built) is the only other reader. Nothing was flipped. **Unverified against real files**: no `.raw` file is in this repository (Assimp's tree has 4, one empty; they are not available here).

### What it is

The "raw triangle" text format (for example the output of POV-Ray tools): one triangle per line, nine numbers, the x y z of the three corners.

| File | What |
|---|---|
| `engine/src/Render/RawReader.cpp` | `okay::ReadRawBuffer` / `okay::ReadRawFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 33 (extension `.raw`) |
| `tests/test_raw_reader.cpp`, `tests/batche2_fixtures.hpp` | Hand-computed checks (`raw_reader`) |
| `tests/test_batche2_hostile.cpp`, `tests/test_batche2_budget.cpp`, `tests/test_own_dispatch_e2.cpp` | Hostile input, budgets and memory, the seam |

### Files Assimp is not given (2026-11-16)

Assimp 5.4.3's RAW importer builds a broken scene when a file has **two or more groups that hold triangles**: the root node says
it has one child per such group, but only the first child pointer is set and the others are null. Our scene walk then crashed
(the corpus files `WithColor.raw` and `WithTexture.raw`, and the test fixture of `own_dispatch_e2`, crashed every build with
`OKAY_USE_ASSIMP=ON`, which includes the shipped Windows build; not tried on Windows). Found from the crash's backtrace and by
running Assimp on small files.

`RawFileUnsafeForAssimp(path)` (`RawReader.cpp`, declared in `ModelData.hpp`) says whether a file is such a file. It reads the
file once, line by line, the way the importer splits it into groups, and errs on the side of "unsafe": UTF-16 text, a line of
4,000 characters or more (the importer copies lines into a 4,096-byte buffer) and more than 1,000 group names also count.
The import seam (`ImportModelData`) uses it whenever the choice for `.raw` is Assimp:

- default choice, or the per-format switch set to Assimp (`OKAY_OWN_RAW=0`, `SetImportBackend(".raw", Assimp)`): **our RAW
  reader reads the file instead** and the log says so (`... cannot read safely; reading it with our own RAW reader`); if ours
  refuses it, Assimp is not tried;
- an explicit `ImportModelData(path, ..., ImportBackend::Assimp)` (the bench, tests that compare the two): refused with the
  message "RAW: this file has more than one group of triangles, which crashes Assimp 5.4.3's RAW importer ...".

A file with one group (`Wuson.raw`) still goes to Assimp by default. The Assimp scene walk also skips null children and mesh
indexes past the mesh list now, as a second guard. Checked: the 11 small cases in `own_dispatch_e2` were each run through
Assimp itself (a scratch program that reads the file and looks for null children) and the guard agrees with it on every one,
and on the 3 corpus files.

**How to try it** (Windows release, which has Assimp): import a `.raw` file with two groups, for example

```
first
0 0 0  1 0 0  0 1 0
second
0 0 1  1 0 1  0 1 1
```

saved as `two.raw`. Before this change the editor crashed; now it imports two parts and the log has the line above.

### What it reads

- A line of nine numbers is a triangle. Twelve numbers: a red green blue colour first, then the nine (the colour is not imported). Nine numbers and a name after them (the texture-name variant): the triangle goes to the group of that name.
- A line whose first word is not a number names a group (the whole line is the name); the triangles after it belong to it. Before any name, the group is `RAW`. Lines starting with `#` and empty lines are skipped.
- One part per group. Triangles with exactly equal corner positions are welded inside a group and smooth normals are made (60 degree crease). Degenerate triangles are dropped. UTF-8 and UTF-16 byte order marks are accepted.

### Not handled

Binary raw files (a NUL byte is refused with that reason; no description of one was found). Quads or any other line length. The twelve-number reading as colour-then-nine follows the description's colour variant; whether real files with twelve numbers mean that is a guess (unverified). A triangle spread over several lines (one per line is required, although the description says any white space may separate the numbers). Colours and texture images.

### Limits

File 256 MiB; 500,000 triangles, 100,000 groups, 4,000,000 output vertices per file; 64 values per line (a longer number line is refused after the 65th token is seen; lines are read in place, so one huge line costs no memory: a 64 MB line of single-character words peaks at 0 KB); numbers finite and within 1e15 (`nan`, `inf`, hex and `1e999` are refused). The worst accepted file (500,000 triangles with all-different corners) reads in 2 s with a peak of 284 MB. No recursion; no other file is named, so `ResolveInsideDir` is not needed.

### Tests and what was checked

`raw_reader` (43 checks): groups, the default group, the texture-name line, the colour line (area 2 only when the nine numbers are read after the colour; breaking that offset turns the test red), `\n` `\r\n` `\r`, welding, number syntax, refusals. `batche2_hostile` (truncations, bit flips, garbage, 2 MB numbers, 5,000 groups) and `batche2_budget` (500,000 read, 500,001 refused naming the budget; removing the budget turns the test red).

### Provenance

Written from the public description "POV-Ray RAW triangle format" (Paul Bourke): nine numbers per triangle, the twelve-number colour variant, the texture-name variant, non-numeric identifiers separating groups. The `#` comment is my own choice (the description gives no comment syntax). No importer source and no code from any other project was read.

## Our own SIB reader (`.sib`): NOT WRITTEN

*Was `docs/own_sib_reader.md` until 2026-11-20.*

Status: **no reader**. Slot 32 of the table in `ModelImport.cpp` is reserved for it (an empty row; `.sib` is routed nowhere, and `test_own_dispatch_e2` checks that), `OKAY_OWN_SIB` does not exist.

Why: SIB is the binary file of the Silo modeller (Nevercenter). The search for a public description of its structure found only that it exists (the binary twin of the ASCII `.sia`); no field list, no chunk list, no sample file (the only one, `heffalump.sib`, is in Assimp's test tree, not here). The rule for this work is to write from a format description or from our own analysis of sample files only, never from another importer's source. With neither a description nor a sample, a reader would be invented, so none was written. To go on: a published description, or the sample file to analyse (then `ModelData`, budgets and tests as for `docs/content/model_readers.md`).

## Our own SMD reader (`.smd`)

*Was `docs/own_smd_reader.md` until 2026-11-20.*

Status: **side by side, off by default.** Until now `.smd` (Valve "Studiomdl Data") only imported in a build with Assimp. Nothing was removed.

### What it is

Written from the public description of the format (the Valve Developer Community's SMD page) and from Valve's published math library for the Euler angle convention.

| File | What |
|---|---|
| `engine/src/Render/SmdReader.cpp` | `okay::ReadSmdBuffer` / `okay::ReadSmdFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 7 of the backend switch) |
| `scripts/gen_smd_fixtures.py` | Writes `tests/data/models/arm.smd`, `arm_crlf.smd`, `arm_anim.smd` (synthetic) |
| `tests/test_smd_reader.cpp` | Hand-computed checks, the seam, scene import, UTF-8 paths, locale (`smd_reader`) |
| `tests/test_smd_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`smd_hostile`) |

### Turning it on

`-DOKAY_OWN_SMD=ON`, environment variable `OKAY_OWN_SMD=1` / `0`, or `SetImportBackend(".smd", ImportBackend::Own)` (the run-time choice wins). Off by default; with Assimp built, Assimp stays the default `.smd` reader.

### What it reads

- `nodes`: bone id, name (quoted, may contain spaces), parent. Bones become nodes under a root node `SMD` that turns Z-up into Y-up (`(x, y, z) -> (x, z, -y)`). Parent loops and missing parents are cut loose to the root, repeated names get a suffix; every case is a note.
- `skeleton`: `time N` blocks of `bone x y z rx ry rz` (radians). The first (smallest) time is the bind pose; Euler angles follow Valve's `AngleQuaternion` (roll X, pitch Y, yaw Z, rotation = Rz * Ry * Rx). With more than one frame the file also gives one clip, `animation`, with a position and a rotation key per frame and bone; frame numbers become seconds at an assumed 30 frames per second (SMD stores none). Frames out of order are sorted, repeated ones merged; both are notes.
- `triangles`: a material line, then three vertices `parent x y z nx ny nz u v [links n (bone weight)*]`. One part per material (the material name is the texture file, prefixed with the model's folder when it has no path). Vertices are welded on position, normal, UV and skin. Up to four weights per vertex (the four strongest; a vertex with fewer than 1.0 of listed weight gives the rest to its own bone, as Assimp does). The part is skinned (`ImportedPart::bones`, inverse bind = inverse of the bone's bind pose) so the animation drives it. V is flipped (`v = 1 - v`), winding and units are kept as written (the real files' winding agrees with their normals).
- `vertexanimation` sections (flex / morph frames) and unknown sections are skipped with a note. `//` comments, a UTF-8 or UTF-16 byte order mark, `\n` / `\r\n` / `\r` line breaks are handled.
- `flatten` (`ImportModel`, one merged static mesh): the root's turn is baked into the vertices, skins and clips are dropped.

### Limits

| Limit | Value |
|---|---|
| File size | 512 MiB |
| Bones | 4,096 |
| Frames / bone poses over all frames | 100,000 / 20,000,000 |
| Triangles / materials | 5,000,000 / 4,096 |
| Links per vertex line | 64 (four kept) |
| Numbers | finite, within +-1e15 |

No recursion (bones are ordered with an explicit pass; a 4,000-deep chain reads). A bad number, a vertex line with too few values, a file that ends inside a triangle, a version other than 1, `nan` / `inf` / hex numbers fail the read with a one-line message that names the line.

### Not handled

- `.vta` (vertex animation files, which Assimp also lists) and `vertexanimation` sections.
- The frame rate is an assumption (30). Assimp's SMD reader also loads every other `.smd` in the same folder as animations; ours reads the one file it is given.
- Animation-only files give nodes and one clip, no mesh. Assimp builds a stick-figure mesh for them; ours does not.
- Rotation order follows Valve's published convention; no real file with rotated bones in an animation was available to confirm it end to end (the three real files carry only a bind pose).

### Tests and what was checked

- `smd_reader`: both line-ending variants, a bone name with a space, parent order, the Euler order (a roll of 90 degrees with a yaw of 90 degrees maps +Y to +Z), the Z-up turn, the welded vertex counts, weights (listed, implied, six links reduced to four), the inverse bind of a turned bone, UV flip, the animation file (frame times, keys), 14 kinds of malformed input, parent loops, repeated names, a `vertexanimation` section, German decimal-comma locale (skipped when none is installed), a UTF-8 path with characters outside the ANSI code page, the seam, a skinned scene import and an animation-only scene.
- `smd_hostile`: every truncation of every fixture, bone and material counts at and over the caps, a 4,000-bone chain, a 3,000-bone parent loop, 100,001 frames, NaN / infinite / overflowing numbers, link lists with weight zero, NUL bytes, UTF-16, 60,000 identical triangles, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 150,000 mutations.
- Real files: three from an outside corpus (`docs/content/model_corpus.md`): `triangle.smd`, `holy_grailref.smd` and `WusonSMD.smd`. Against Assimp built from source: triangle count and surface area agree on all three (area to 6 significant digits), and the bounding boxes agree once Assimp's Z-up is turned the way our root node turns it. Vertex counts differ because ours are welded (2,117 against Assimp's 11,196 unwelded on Wuson).

### Before it can become the default

Real files with an animation and with rotated bones; more than three real files; Windows run; owner approval. Removing Assimp drops the `.vta` extension and the stick-figure mesh for animation-only `.smd`.

## Our own Terragen reader (`.ter`)

*Was `docs/own_terragen_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_TER`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of `.ter`. Nothing was flipped.

### What it is

A Terragen terrain file is a height field: a regular grid of 16-bit elevations with a few settings (grid size, metres per terrain unit, planet radius, curve mode). Terragen (Planetside Software) writes it, and many terrain tools export it.

**Where it was written from.** Planetside's published *Terragen Terrain file specification* (the wiki page "Terragen .TER Format") and nothing else: the 16-byte identifier, the chunks (`SIZE`, `XPTS`, `YPTS`, `SCAL`, `CRAD`, `CRVM`, `ALTW`, `EOF `), the elevation formula and the curve-mode-1 sphere are taken from that page. No importer source was read: not Assimp's, not GDAL's, and not the reference library the page links to (rule 19a). The comparison with Assimp below ran Assimp as a black box.

| File | What |
|---|---|
| `engine/src/Render/TerragenReader.cpp` | `okay::ReadTerragenBuffer` / `okay::ReadTerragenFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 27 of the opt-in table, run through `RunReaderGuarded` like every own reader (an allocation failure or other exception becomes a refusal) |
| `tests/test_terragen_reader.cpp`, `tests/terragen_test_build.hpp` | Hand-computed checks on files built in memory, and the seam (`terragen_reader`) |
| `tests/test_terragen_hostile.cpp` | Hostile input, mutation fuzz, time and memory limits; builds without the engine library (`terragen_hostile`) |

Switch it on like the other readers: `SetImportBackend(".ter", ImportBackend::Own)`, the environment variable `OKAY_OWN_TER=1` (or `0` to force it off), or `-DOKAY_OWN_TER=ON` at build time.

### What it reads

- **The chunks**, in any order (the specification asks readers to allow that), each 4-byte aligned and without a length field. `SIZE`, `XPTS` and `YPTS` must come before the elevation data (`ALTW`), as the specification requires.
- **Grid size.** `XPTS` by `YPTS` points when the file has them; otherwise `SIZE + 1` on each side (`SIZE` holds the number of points on the shortest side minus one). With only one of `XPTS` / `YPTS`, the other side is `SIZE + 1` (reported).
- **Elevations.** `ALTW` holds `HeightScale` and `BaseHeight` (signed 16-bit) and then the elevations row by row, first row `y = 0`, left to right. The altitude of a point in terrain units is `BaseHeight + Elevation * HeightScale / 65536`, as the specification gives it.
- **Scale.** `SCAL` is metres per terrain unit on x, y and z (30 when the file has none). The mesh is in **metres**: point `(i, j)` is at `i * xscale`, `j * yscale`, its altitude times `zscale`. Terragen itself needs the three to be equal; a file where they differ is scaled per axis and reported.
- **Curve mode** (`CRVM`). Mode 0 is flat. Mode 1 drapes the terrain over a sphere of radius `CRAD * 1000 / zscale` terrain units (`CRAD` in km, 6370 by default) whose top is at the middle of the grid. Following the specification, x and y are not moved; each point drops by the sphere's fall-off at its distance from the middle (`sqrt(r^2 - d^2) - r`). Other modes are undefined in the specification: the terrain is drawn flat and the mode is reported.
- **Axes.** Terragen is Z up; the engine is Y up. As for the other Z-up formats, `(x, y, z)` becomes `(x, z, -y)`. The corner `(0, 0)` stays at the origin.
- **The mesh.** One part with one vertex per grid point and two triangles per grid cell, wound so that they face up (+Y). Normals come straight from the height grid (central differences, one-sided at the edges), not from the engine's smooth-normal pass, which is far slower on millions of points. Texture coordinates run from 0 to 1 across the grid: `u` along x, `v` along y (first row `v = 0`). There is no material (the format has none).

### Reported, not read (each is a line in `out.notes`)

A missing `EOF ` marker (only old versions of Terragen need it), bytes after `EOF `, unknown data after the elevations, `SCAL` / `CRAD` / `CRVM` placed after the elevations (still applied), no padding before `EOF `, a `SIZE` that disagrees with `XPTS` / `YPTS` (`XPTS` and `YPTS` win), an unknown curve mode, a scale that differs per axis.

### Refused (an error, nothing imported)

A file that does not start with `TERRAGENTERRAIN `; no `SIZE` or no `ALTW`; `ALTW` before `SIZE`; `SIZE` / `XPTS` / `YPTS` after `ALTW`; two `ALTW` chunks; an unknown chunk before the elevation data (terrain chunks carry no length, so it cannot be skipped); fewer than 2 points on a side; a file that ends inside a chunk or inside the elevation data; a `SCAL` value outside 1e-6 to 1e6 metres per unit or not a number; a curved terrain (mode 1) with a bad `CRAD` or a sphere too small to hold the terrain.

### Limits

File 64 MiB; at most 4,097 points on a side and 2049 x 2049 points (about 4.2 million) in all. The largest grid takes about 270 MB while it is read: an independent review measured a 267 MB peak including the 8.4 MB file, and `terragen_hostile` holds it under 320 MB with a counting allocator.

Why 8.4 million triangles, when the XGL and Irrlicht readers stop at 4 million: a terrain's triangle count is fixed by the grid it stores (two per cell). Nothing in the format multiplies it: no references, no instancing, no subdivision. So the mesh can only be as big as the elevation data the file really contains (2 bytes per point; a file that claims a bigger grid than it holds is refused before anything is built). 2049 x 2049 is a common Terragen export size, and memory grows in step with the file, about 32 bytes of mesh per byte of file. The scale limits (1e-6 to 1e6) keep every slope, and so every normal, inside float range.

### Tests and what was checked

- `terragen_reader`: files built in memory (`tests/terragen_test_build.hpp`) with every number worked out by hand: positions of a 3 by 2 grid with `HeightScale` 16384 and `BaseHeight` 10, the Z-up to Y-up turn, texture coordinates, the winding (every triangle faces up, and the engine's own smooth normals agree with ours on a flat grid), the normals of planes tilted along x and along y, the 30 m default, `SIZE` alone, chunks in any order, odd point counts and their padding, non-square grids, a scale that differs per axis, curve mode 1 (the middle stays, a corner `sqrt(8)` units out drops by `100 - sqrt(9992)` units at radius 100), an undefined curve mode, every tolerated case above, 27 refusals with their messages, the largest grid (2049 by 2049), a UTF-8 path, and the seam (off by default, `OKAY_OWN_TER`, `SetImportBackend`, `ImportModelScene`, `ImportModel`, the one-static-mesh mode).
- `terragen_hostile`: every truncation of three seed files, sizes at and over every cap, extreme scale, radius and curve-mode values, 200,000 chunks before the data, one point over the grid cap, and a mutation fuzz (8,000 by default, `OKAY_READER_FUZZ_ITERS` for more). Every result is checked: refusals leave nothing behind, a read gives finite values, normals that face up, and indices inside the mesh. With `OKAY_MODEL_CORPUS` set, the two real files are fuzzed too. Run here under `address,undefined,float-cast-overflow` with 40,000 fuzz runs and the real files: clean. The run found one thing, which is fixed: a scale of 1e-30 on x and 1e6 on z gave slopes near 1e40, whose normals underflowed to zero in float; scales below 1e-6 are now refused.

#### Real files

Assimp's tree has 2 Terragen files (`models/TER/RealisticTerrain.ter`, 257 x 257 points at 30 m, and `RealisticTerrain_Large.ter`, 513 x 513 points at 19.53125 m; both flat, curve mode 0). They are not stored in this repository; `docs/content/model_corpus.md` says how to point the tests at them. **Both read, with no notes, and equal Assimp v5.4.3** (built from the same sources with only its Terragen importer, run as a black box through its C API) **on mesh count, triangle count (131,072 and 524,288) and bounding box** once Assimp's Z-up is turned to our Y-up: x 0 to 7,680 m, height -78.99 to 874.53 m, y 0 to 7,680 m for the first; x and y 0 to 10,000 m, height -226.74 to 1,170.39 m for the second. **The surface area agrees to 2 parts in a million** (75,029,650 against Assimp's 75,029,777 m² and 138,372,471 against 138,373,075 m²). Splitting each cell along the other diagonal would give 75,054,167 m², a 3 in 10,000 difference, so both readers cut the cells the same way; the small rest is consistent with float rounding (Assimp's vectors are single precision). Differences that are by design: Assimp keeps four vertices per cell (262,144 and 1,048,576), ours shares one vertex per grid point (66,049 and 263,169); with the flags used Assimp gives no normals or texture coordinates, ours does.

Not verified: Terragen files from other writers (World Machine and other tools), curve mode 1 on a real file (neither real file uses it), a run on real Windows.

## Our own X3D reader (`.x3d`)

*Was `docs/own_x3d_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_X3D`, same rules as `docs/content/model_readers.md`, whose XML reader it uses). Assimp (when built) is still the only other reader of `.x3d`.

### What it is

Written from the public X3D abstract and XML-encoding specifications (ISO/IEC 19775 / 19776), with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/X3dReader.cpp`, `engine/include/okay/Render/X3dReader.hpp` | `okay::ReadX3dBuffer` / `okay::ReadX3dFile` |
| `engine/src/Render/XmlLite.hpp`, `ReaderCommon.hpp`, `PolygonTriangulate.hpp` | Shared XML reader, number parsing and normals (see `docs/content/model_readers.md`), polygon ear clipping (shared with OBJ, PLY, COLLADA) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 13 of the batch-B table |
| `tests/test_x3d_reader.cpp` | Hand-computed checks on synthetic files and the seam (`x3d_reader`) |
| `tests/test_batchb_hostile.cpp` | Hostile input and mutation fuzz (`batchb_hostile`) |

### What it reads

- **Scene graph**: `Group`, `Transform` (`translation`, `rotation` axis-angle, `scale`, `scaleOrientation`, `center`, composed `T C R SR S SR^-1 C^-1`), `Collision`, `Anchor`, `StaticGroup`, `Billboard` (as a static group, noted), `Switch` (`whichChoice`, default -1 shows nothing, noted), `LOD` (the first, finest level), with `DEF` / `USE` reuse. A `USE` that points back into its own parent is skipped (noted); the number of nodes visited is capped so an exponential `USE` expansion stops. Every transform is baked into the vertices: one node and one part per `Shape` (named by its `DEF`).
- **Geometry**: `IndexedFaceSet` (`coordIndex` polygons, concave ones ear clipped; `ccw`; `creaseAngle` as the crease angle, default 0 = flat; `Normal` with `normalIndex` and `normalPerVertex`; `TextureCoordinate` with `texCoordIndex`), `IndexedTriangleSet`, `TriangleSet`, the primitives `Box`, `Sphere`, `Cylinder` (`side`, `top`, `bottom`) and `Cone`, tessellated with 24 segments (a sphere has 12 bands), and `Extrusion` (cross-section, spine, per-point `scale` and `orientation`, caps, `ccw`, `creaseAngle`; the spine-aligned planes are built as the specification describes, a straight spine turns the Y axis onto it, a closed spine has no caps; no UVs). UV `v` is flipped like the other bottom-up formats. Without `Normal`, normals come from `creaseAngle`. A mirroring transform flips the winding so faces keep their side; normals are transformed by the inverse transpose.
- **Appearance**: `Material` (`diffuseColor` with `1 - transparency` as alpha, `emissiveColor`, `shininess * 128` as the Phong exponent) and `ImageTexture` (the first `url`, resolved next to the file; `http` and `data:` urls are not read). The material is shared by `DEF` / `USE`.
- **Axes**: X3D is Y-up like the engine, so no axis change. Units are not applied (X3D has a `unit` statement for angle and length that is ignored).

### Not handled (each is also named in `notes`)

Nodes not read are counted by name: `ElevationGrid`, `IndexedLineSet`, `PointSet`, `Text`, strip / fan / quad sets, NURBS, geospatial, `Inline`, `ProtoDeclare` / `ProtoInstance`, scripts, sensors, interpolators, `ROUTE`s (no animation is read), lights, `Viewpoint`, `NavigationInfo`, `Background`, ... A file whose only geometry is of such a kind fails with a message that names what it has. Also: per-vertex and per-face `Color`, `TextureTransform`, texture nodes other than `ImageTexture`, `TwoSidedMaterial`, `PhysicalMaterial`. Only the XML encoding is read: the classic VRML encoding (`.x3dv`, `.wrl`) and the binary encoding (`.x3db`) are not. The XML tree is limited to 3,000,000 elements, the scene graph to 1,000,000 visited nodes and 128 levels, index lists to 60,000,000 values.

### Tests and what was checked

- `x3d_reader` (`tests/test_x3d_reader.cpp`): **synthetic** documents: `IndexedFaceSet` quads, a concave L-shaped polygon (area 3), faces with an out-of-range or missing closing index, `ccw`, materials (alpha, emission, shininess), explicit normals and texture coordinates (the `v` flip), per-face normals, bad normal indices, the crease angle (a 90 degree fold stays flat at 0 and at 57 degrees, is welded at 100), `IndexedTriangleSet` and `TriangleSet`, a `Transform` computed by hand (translation, scale, a 90 degree rotation, centre, a mirror), `DEF` / `USE` (also of a `Transform`), a `USE` of its own parent, an undefined `USE`, `Switch`, `Box` (volume 48), `Sphere`, `Cylinder` and `Cone` (volumes and areas within the tessellation), `Extrusion` (the default square swept along +Y, volume 4; a clockwise cross-section with `ccw="false"`; a straight spine along x; per-point scale; a bent and a closed spine), the notes for nodes not read, a texture path, every refusal (no geometry, bad index, non-multiple of three, `1e999`, a two-number translation, DTD entities), a decimal-comma locale, and the seam.
- `batchb_hostile` (X3D section): every truncation, 20,000-deep and 200-deep groups, billion-laughs, an exponential `USE` expansion (each level uses the previous one twice, 40 levels: stops at the node cap), a shape reused 3,000 times, a `USE` of the group it sits in, bad numbers, bad and huge indices, a 100,000-corner polygon, a 250-corner concave star (ear clipping bound), 20,000 faces on three points, a vertex with 5,000 incident triangles at different angles (crease clustering, time-checked), singular, huge and negative transforms, absurd primitive and extrusion sizes (a 6,000,000-point extrusion is refused), then the mutation fuzz. Sanitizers: clean.

#### Real files

Assimp's tree has 4 X3D files (three in `models/X3D`, one in `models-nonbsd/X3D`). **Ours reads 3; one fails by design**, and Assimp reads all 4. None of the 3 agrees with Assimp on every count, and each difference was checked against the file:

- `IndexedLineSet.x3d`: only a line set. Ours fails ("the file has no triangle geometry (it has only: IndexedLineSet ...)"), as the OBJ and PLY readers do for files with no faces; Assimp returns a mesh with 0 triangles.
- `ChevyTahoe.x3d` (a real model, 17 shapes, 8 textures, the wheel is a `Transform` `USE`d three more times): ours 1298 triangles in 20 parts. **An independent count from the file's own index lists** (a script that walks `DEF` / `USE` and sums `n - 2` per face) gives 1298; Assimp gives 1234, one wheel (64 triangles) fewer. Bounding box equal.
- `ComputerKeyboard.x3d` (an `Extrusion` for the base and two key shapes, one `Transform` per key, 98 keys use a `USE`): ours 100 shapes and 2020 triangles; the same independent count (cross-section, spine and caps worked out per shape from the file) gives 100 shapes and 2020. Assimp returns 4 meshes and 100 triangles (it does not instance the `USE`d keys) and a different box. **Ours is right** (checked again 2026-11-16): in X3D a `USE` is another instance of the `DEF`ed node, drawn wherever it is used (ISO/IEC 19775-1, the `DEF` / `USE` rules of the scene graph), so every key appears where its `Transform` puts it. The file has one `DEF='EscapeButton'` key, 35 `USE`s of it outside the group `row`, 20 inside `row`, and `row` is drawn three times (once where it is defined, twice by `USE`), plus 2 more `USE`s at the end: 1 + 35 + 3 x 20 + 2 = 98 keys, each an extrusion of 20 triangles, plus the base (40) and the space bar (20) = 100 shapes and 2,020 triangles. Assimp's 4 meshes and 100 triangles are the base (40), the space bar (20) and two keys (20 each), so it drops 96 of the 98 keys. Its box (y up to 0.83, z down to -0.105) differs from ours (y up to 0.105: the base lies flat after its 90 degree turn and the keys stand on it); why Assimp's is larger was not looked into.
- `HelloX3dTrademark.x3d`: a `Sphere`. Ours is tessellated with 24 x 12 segments (528 triangles, surface area 12.39), Assimp's is finer (1280, 12.51); ours also gives the sphere UVs, Assimp's mesh has none. The `Text`, interpolators and `ROUTE`s are named in the notes.

Not verified on real files: `Switch` and `LOD`, normals and texture coordinates with index arrays other than the Chevy ones, `IndexedTriangleSet`, the primitives other than the sphere, `ImageTexture` paths beyond the Chevy file's, large files.

## Our own DirectX `.x` reader (`.x`)

*Was `docs/own_x_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_X`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of `.x`. Nothing was flipped.

### What it is

The DirectX `.x` file is the older model format of Direct3D (Microsoft's DirectX SDK, 1997 to about 2008), used by many hobby and indie games and by Irrlicht, OGRE and Blender exporters for years. It exists in three encodings: **text** (`txt`), **binary** (`bin`) and **compressed** (`tzip`, `bzip`: text or binary packed with MSZIP), with 32 or 64 bit floats. Written from the public description of the format (the DirectX SDK documentation of the file format and of its standard templates) with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/XFileReader.cpp` | `okay::ReadXBuffer` / `okay::ReadXFile` (declared in `ModelData.hpp`) |
| `engine/src/Core/ImageDecode.cpp` | `okay::Inflate`, a wrapper over the engine's one inflate (`engine/include/okay/Core/Inflate.hpp`), used for MSZIP |
| `engine/src/Render/ReaderCommon.hpp`, `TextReaderUtil.hpp` | Shared number parsing and file reading |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 21 of the opt-in table |
| `tests/test_x_reader.cpp`, `tests/batchc_fixtures.hpp` | Hand-computed checks on synthetic files (`x_reader`) |
| `tests/test_batchc_hostile.cpp` | Hostile input and mutation fuzz (`batchc_hostile`) |

### What it reads

- **One token stream for text and binary.** The parser asks for names, braces, integers and floats; text and binary differ only in how those are cut out of the bytes (in text, `;` and `,` are separators and carry no meaning of their own, strings are quoted, comments are `//` and `#`; in binary, tokens are 16-bit ids with payloads, and lists of numbers come as `INTEGER_LIST` / `FLOAT_LIST` tokens). Templates are skipped; unknown objects are skipped by their braces (text) or tokens (binary), and counted by name in the notes.
- **Compressed files**: MSZIP blocks (a 2-byte unpacked size, a 2-byte packed size, `CK`, a deflate stream), each of which may refer back into the previous 32 KiB of output. The reader puts that window in front of the block as a stored deflate block, inflates, and drops the window again (this uses the engine's inflate unchanged).
- **Frames** become nodes (parent first), with the `FrameTransformMatrix`. One top-level frame is the root; several frames, or meshes outside any frame, get a new root named `X`.
- **Meshes**: positions, polygons of any size (fan-triangulated), `MeshNormals` (as stored, per face corner), the first `MeshTextureCoords` set (as written, top-down), `MeshMaterialList` (per-face material, inline `Material` objects and `{ name }` references). One part per mesh and material; vertices welded per position and normal; smooth normals when the file has none.
- **Materials**: `faceColor` (diffuse, with alpha), `power` (shininess), `emissiveColor`, `TextureFilename` and `NormalmapFilename`. Paths: backslashes (written twice in text) become `/`, relative paths are joined to the model's folder.
- **Skin**: `SkinWeights` (bone name = frame name; the four strongest weights per vertex are kept and normalised; the offset matrix is the inverse bind). Vertices with no weight in a skinned mesh follow the root node.
- **Animation**: each `AnimationSet` is one clip (named after the set); each `Animation` names a frame; key types 0 (rotation, `w x y z`), 1 (scale), 2 (position), 3 and 4 (a whole matrix, split into position, rotation and scale). Key times are ticks; seconds = ticks / `AnimTicksPerSecond`, and **25 when the file has no `AnimTicksPerSecond`** (the format's own default is 4800, but game tools write frame numbers and the engine's Assimp path uses 25 for such files; a note says so).
- **Handedness.** DirectX is left-handed and writes matrices for row vectors. Z is mirrored in positions, normals and matrices (`A' = M A M`, `M = diag(1, 1, -1)`), every triangle's winding is reversed, a rotation key `(w, x, y, z)` becomes `(x, y, -z, w)`, and a matrix is transposed before it is split into position, rotation and scale (a mirrored matrix gets one negative scale; a shear cannot be stored and is dropped with a note).

### Not handled (each is a line in `out.notes`)

Vertex colours (`MeshVertexColors`), further texture-coordinate sets, the specular colour, `VertexDuplicationIndices` and other mesh tables, animation options, **shared meshes** (a `{ name }` reference to a mesh inside a frame is not followed), patch meshes and progressive meshes, `.x` files whose text is UTF-16.

### Limits

File 256 MiB (and 256 MiB unpacked for compressed files); vertices and normals 20,000,000; faces 20,000,000; frames and meshes 100,000; materials 100,000; bones 100,000; animation keys 20,000,000; nesting 100,000. A count read from the file must fit in the bytes that remain (a number takes at least 2 bytes in text and 4 in binary) before anything is reserved for it; names and strings 64 KiB. Numbers are finite and within 1e15 (`NumParse.hpp` in text); each MSZIP block must unpack to exactly the size it states, at most 32 KiB.

### Tests and what was checked

- `x_reader`: **synthetic** files built in memory. A text file with a skinned two-material mesh (a triangle, a quad, a degenerate triangle and a face that names a missing vertex), a skeleton of three frames, an `AnimationSet` with position, rotation and matrix keys; every number by hand: the mirrored node transforms, material values and the texture path, area 3 + 6 = 9, winding and normal directions, weights and their normalisation, the root-bone fallback, the inverse bind, animation times (30 ticks per second) and mirrored keys, two top-level frames. Binary files with 32 and 64 bit floats, with and without separator tokens, and a template to skip. MSZIP files, one with a second block that is a single match into the first (so it only reads if the window is passed on). Refusals and **every truncation** of the four kinds of file.
- `batchc_hostile` (X section): every truncation; every four-byte position of the binary and compressed files at 0, 1, 2, 3, 255, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFE, 65536, 65537, NaN, infinity and the largest float; hostile numbers (`nan`, `inf`, `1e999`, `0x10`, `1.#QNAN`, 2^31 and more) in the counts and fields of a text file; 150,000 nested frames; a 1,000,000-deep unknown object; an unclosed string; a 70 KB name; 200,000 matrix numbers; 200,000 materials; a valid text mesh of 100,000 vertices and faces (0.4 s); MSZIP bombs (2,000 blocks of 32 KB from about 140 bytes each, a block that unpacks to more than it says, a total that lies); 120,000 mutations over the four fixtures. Sanitizers (`address,undefined,float-cast-overflow`): clean.

#### Real files

Assimp's tree has 12 files with the extension `.x` or `.X`: 11 models (`models/X`: `BCN_Epileptic.X`, `Testwuson.X`, `anim_test.x`, `fromtruespace_bin32.x` (binary), `kwxport_test_cubewithvcolors.x`, `test.x`, `test_cube_text.x`, `test_cube_binary.x`, `test_cube_compressed.x` (MSZIP); `models-nonbsd/X/dwarf.x` and a copy of it in `models/IRR/assets`) and one empty file (`models/invalid/empty.x`: refused by both readers). Two more files without an extension (`TestFormatDetection`, `OV_GetNextToken`) are format-detection tests and are not reached by extension. **All 11 read and equal Assimp v5.4.3 (built from the same sources, only the X importer) on mesh count, triangles, surface area, bounding box, node count and clip count.** Beyond that: on the five animated or skinned files I checked (`dwarf.x`, `BCN_Epileptic.X`, `Testwuson.X`, `anim_test.x`, `test_cube_binary.x`) the **local position, scale and rotation of every node, every key value, every inverse bind matrix (4 to 6 decimals) and every bone's total weight** equal Assimp's after its own handedness conversion (largest differences: position 5e-7, rotation keys 1e-4, weight sums 2e-5). Rotation and scale of a node whose matrix has a negative determinant (the root of the cube files swaps Y and Z) are a different but equivalent split into rotation and scale.

Differences: `anim_test.x` (the file's own notes say its mesh names four bones of which only two exist as frames) has one bone more for us (vertices whose bones do not exist follow the root; Assimp keeps four bone names with no node); Assimp keeps three vertices per triangle, ours welds (e.g. 1,479 against 5,688 for `dwarf.x`); `dwarf.x` has no `AnimTicksPerSecond`, so its 55 ticks are 2.2 s (25 per second), as in the engine's Assimp path (the scratch tool that printed Assimp's side showed 55: ticks).

Not verified: 64 bit binary files (synthetic only), `bzip` (compressed binary) files, compressed files of more than one MSZIP block (synthetic only: the window handover is tested, but no real multi-block file was available), files with several meshes sharing a frame by reference, a run on real Windows.

## Our own XGL / ZGL reader (`.xgl` `.zgl`)

*Was `docs/own_xgl_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_XGL`, same rules as `docs/content/model_readers.md`). Assimp (when built) is still the only other reader of XGL files. Nothing was flipped.

### What it is

XGL is a small XML scene format (written by 3D Studio exporters, Truespace and others, and used by scientific and CAD tools). A `<WORLD>` holds `<MESH>` blocks (points `<P>`, normals `<N>`, texture coordinates `<TC>`, materials `<MAT>` and faces `<F>` whose corners `<FV1>` `<FV2>` `<FV3>` point at them) and `<OBJECT>` blocks that place a mesh with a `<TRANSFORM>`. **ZGL** is the same text compressed as one zlib stream.

| File | What |
|---|---|
| `engine/src/Render/XglReader.cpp` | `okay::ReadXglBuffer` / `okay::ReadXglFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/XmlLite.hpp`, `TextReaderUtil.hpp`, `ReaderCommon.hpp` | Shared strict XML reader, UTF-16 conversion, number parsing, mesh builder |
| `engine/src/Core/ImageDecode.cpp` | `okay::Inflate`, a wrapper over the engine's one inflate (`engine/include/okay/Core/Inflate.hpp`), used for ZGL |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 24 (extensions `.xgl`, `.zgl`) |
| `tests/test_xgl_reader.cpp`, `tests/batchd_fixtures.hpp` | Hand-computed checks on synthetic files (`xgl_reader`) |
| `tests/test_batchd_hostile.cpp` | Hostile input and mutation fuzz (`batchd_hostile`) |
| `tests/test_batchd_budget.cpp` | Budgets: a 0.45 MB file that names one 2,000-triangle mesh 6,000 times (`batchd_budget`) |

### What it reads

- **Compression.** A file that starts with a zlib header is inflated (at most 256 MiB of text; a damaged stream or a bomb is refused), anything else is read as XML. So a `.zgl` renamed `.xgl` and the other way round both work.
- **Meshes.** `<P ID>` x,y,z, `<N ID>`, `<TC ID>` u,v (comma separated); the ids need not be dense. `<F>` faces with `<MATREF>` and corners `<FV1>`..`<FVn>` (`<PREF>`, `<NREF>`, `<TCREF>`); more than three corners are fanned. Faces are grouped by material into parts; corners with a normal and a texture coordinate keep them (a part is welded on position, normal and uv).
- **Materials.** `<MAT ID>` at the world level or inside a mesh (the mesh's own first): `<DIFF>` (or `<AMB>`) as the diffuse colour, `<EMISS>`, `<SHINE>`, `<ALPHA>` as the diffuse alpha.
- **Objects.** Nested `<OBJECT>` blocks become nodes (explicit work list, 256 levels); `<TRANSFORM>`: `<POSITION>`, `<SCALE>` (one number, or the first of three), `<FORWARD>` and `<UP>` as the object's axes (z = forward, y = up, x = up x forward: right-handed; a turn of 90 degrees about Y is forward (1,0,0), up (0,1,0)). A `<MESH>` inside the object, or `<MESHREF>` by id, puts that mesh's parts on the object's node. Without any object the world-level meshes are read once at the root.

### Not handled (each is a line in `out.notes`)

Faces that point at missing points (skipped and counted), line and patch elements, lights, the background and scene lighting, textures, the per-object data blocks, an unknown `<MESHREF>`.

### Limits

File and inflated text 256 MiB; 8,000,000 points, normals or texture coordinates per mesh; 20,000,000 faces; 10,000 corners per face; 1,000,000 objects; per file 4,000,000 output vertices, 4,000,000 triangles in the meshes built and 4,000,000 triangles placed in the scene (each `<MESH>` is built once however many objects name it, and every placement is a copy that counts against the placed budget; a file over a budget is refused with a message that names it); the XML reader's caps. Numbers go through `NumParse.hpp` (finite, within 1e15). (While testing this reader the shared XML reader was found to be quadratic on files with many elements and no `&` in them; fixed in `XmlLite.hpp`: a 40,000-face file went from 4.3 s to 0.1 s. Every XML-based reader benefits.)

### Tests and what was checked

- `xgl_reader`: **synthetic** world (a world material, a mesh with its own material, 5 points, one normal, 4 texture coordinates, 3 faces, an object with forward (1,0,0), up (0,1,0), position (5,0,0), scale 2 and a child object that uses the mesh again), as text, as ZGL (stored zlib blocks) and with a byte order mark: parts, triangles, world area 16 (by hand), node tree, the quaternion of the 90 degree turn, a vertex seen through the matrix, materials, uv and normals; meshes without objects, objects without meshes; refusals (empty, wrong root, no mesh, a bad point, a face with an unknown point, a DTD entity, a damaged zlib stream) and **every truncation** of the text and of the ZGL.
- `batchd_hostile` (XGL section): zlib bombs (300 MB of `<`), ids and references at the extremes and `nan` / `inf` / `1e999` in points, references, scale, position, forward and shine, zero, negative and huge scales and parallel forward and up vectors, 5,000 nested objects, 20,000 objects that use one mesh, a mesh of 100,000 faces, a face of 20,000 corners, an entity bomb, a 3 MB number; then 3,000 mutations of the text and 750 of the ZGL (30,000 and 7,500 in the sanitizer run).

#### Real files

Assimp's tree has 7 files (`models/XGL`: 6 `.zgl` and `sample_official.xgl`, plus the same sample as `.xml`). **All 7 `.zgl` / `.xgl` files read in both; 6 equal Assimp on triangles, mesh count, surface area and bounding box** (`Wuson.zgl` 3,732 triangles area 9.0258; `Spider_ascii.zgl` 1,312, 56.9476; `BCN_Epileptic.zgl` 5,126, 3 meshes, 2.0697; `cubes_with_alpha.zgl` 60, 5 meshes; `sphere_with_mat_gloss_10pc.zgl` 528; `wuson_dxf.zgl` 3,732), and the 7th, `sample_official.xgl`, differs **by design**: it has two objects that place one mesh at (0, 0.9, 0) and (-0.9, 0, 0); ours applies the positions (bounding box x -1.9 to 0, y -0.5 to 1.4), Assimp's output has both meshes at the origin (x -1 to 0, y -0.5 to 0.5). Material counts differ on two files (`BCN_Epileptic` 3 against 6, `sphere_with_mat_gloss_10pc` 1 against 3): ours keeps only the materials a face uses. `sample_official_asxml.xml` (the same text) fails in Assimp ("Unknown exception"); ours would read it, but the extension `.xml` is not routed to this reader (too many other formats use it).

Unverified: textured XGL (none in the tree has `<TEXTURE>`), patches and lines, the `<FORWARD>` / `<UP>` convention on a real file with a rotation (none in the tree has one), a run on real Windows.

### Provenance (what this file was written from)

`XglReader.cpp` was written from scratch for this project from the public description of the XGL scene format (the element names above, as published with the format) and the zlib container (RFC 1950, with the engine's own inflate), and from reading the sample files of Assimp's `test/models/XGL` as data (never the Assimp source). Assimp was used only as a **black box** (counts, area, box). No code, comment, table or test from any other importer was read, copied or ported.

## `.xml` model files: picked by their root element

*Was `docs/own_xml_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_XML`, slot 45 of the import table in `ModelImport.cpp`; added 2026-11-16 in the
"Assimp gap" batch, `docs/status/own_code_roadmap.md`, section "What still needs Assimp"). Nothing was flipped.

### What it is

Assimp claims the extension `.xml` (the Irrlicht scene and mesh importers list it, and its format detection also reads COLLADA
and XGL files saved as `.xml`). Our readers for those formats existed, but only under their own extensions (`.irr`, `.irrmesh`,
`.dae`, `.xgl`, `.mesh.xml`), so an `.xml` model went to Assimp. Slot 45 is not a new parser: it looks at the root element and
hands the file to the reader we already have.

| Root element (namespace prefix ignored) | Read by | Its page |
|---|---|---|
| `irr_scene` | `ReadIrrSceneFile` (the meshes it names go through the import seam) | `docs/content/model_readers.md` |
| `mesh` whose first child element is `<sharedgeometry>`, `<submeshes>` or `<submesh>` | `ReadOgreXmlFile` | `docs/content/model_readers.md` |
| any other `mesh` | `ReadIrrMeshFile` | `docs/content/model_readers.md` |
| `COLLADA` | `ReadColladaFile` | `docs/content/model_corpus.md` |
| `WORLD` | `ReadXglFile` | `docs/content/model_readers.md` |
| anything else | refused: `XML: the root element <x> is not one this engine reads (irr_scene, mesh, COLLADA, WORLD)` | |

The first 8 KiB are looked at (the root element, and for `<mesh>` its first child element); UTF-16 text (a byte order mark or zero bytes) is looked at with its zero bytes dropped. The XML
declaration, comments and a DOCTYPE before the root are skipped. A `.mesh.xml` file still goes to slot 23 (the OGRE switch
`OKAY_OWN_OGRE`), not here. Code: `ReadXmlByRoot` in `engine/src/Render/ModelImport.cpp`.

### Tests and what was checked

- `own_dispatch_gap`: an `.xml` of each kind (COLLADA, XGL, OGRE XML, Irrlicht mesh, an Irrlicht scene that names a mesh) reads
  to the same model as that format's own reader; a UTF-16 COLLADA file; an unknown root (`<skeleton>` after a comment) is
  refused with its name; a binary file named `.xml` is refused; `OKAY_OWN_XML=1` / `0`; off by default; the shipped sample
  `examples/own_readers/cube_irrmesh.xml` gives a cube of 12 triangles and area 6.
- The readers behind it keep their own fixture, hostile and mutation tests.

#### Real files

The 9 `.xml` files of Assimp's test folder (not counting `.mesh.xml`). **Against Assimp v5.4.3 (`bench_model_readers --compare`)**:

| File | Ours | Assimp | |
|---|---|---|---|
| `Collada/AsXML.xml` | 1 part, 12 triangles | the same, same box | equal |
| `IRRMesh/testFormatDetection.xml` and its UTF-16 copy | 5 parts, 60 triangles, 180 vertices | 5, 60, 120 | the vertex count is the Irrlicht mesh reader's known difference (Assimp joins vertices); box equal |
| `XGL/sample_official_asxml.xml` | 2 parts, 24 triangles | 2, 24 | box differs, exactly as for the same file as `.xgl` (`docs/content/model_readers.md`, object positions) |
| `IRR/skybox.xml` and its UTF-16 copy | refused: the scene has no geometry | refused | both refuse |
| `Ogre/OgreSDK/fish.skeleton.xml`, `ninja.skeleton.xml` | refused: root `<skeleton>` | refused | both refuse (a skeleton alone is not a model) |
| `invalid/emptyIrrMesh.xml` | refused | refused | both refuse |

All in the `model_corpus` manifest (reader `xml`). Speed and memory (`docs/content/own_readers_speed.md`, row `xml`): 0.70x Assimp's read time and 0.73x its largest peak heap over the 4
files both read; the two Irrlicht mesh files are 0.99x and 1.01x the time, which is the Irrlicht mesh reader's own speed.

Unverified: `.xml` files from tools other than those of the test folder; a run on real Windows.

### Provenance

The root-element switch was written for this project from the element names of the formats' own descriptions (the same ones the
readers above were written from). No code from any other importer was read.

## Our own ZAE reader (`.zae`: COLLADA in a ZIP archive)

*Was `docs/own_zae_reader.md` until 2026-11-20.*

Status: **side by side, off by default** (`OKAY_OWN_ZAE`, slot 44 of the import table in `ModelImport.cpp`; added 2026-11-16 in the
"Assimp gap" batch, `docs/status/own_code_roadmap.md`, section "What still needs Assimp"). Assimp (when built) is the other reader of
`.zae` and the fallback when ours refuses a file. Nothing was flipped.

### What it is

A ZAE file is a ZIP archive that carries a COLLADA document and the files it uses (textures). The COLLADA 1.5 specification
describes it: the archive's root may hold `manifest.xml`, whose `<dae_root>` element names the root document; without a
manifest the document is the `.dae` in the archive.

| File | What |
|---|---|
| `engine/src/Render/ZaeReader.cpp` | `okay::ReadZaeBuffer` / `okay::ReadZaeFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/ZipLite.hpp` | The engine's own ZIP reader (stored and deflate, with the shared inflate; caps on archive size, entry count, entry and total sizes; CRC checked) |
| `engine/src/Render/ColladaReader.cpp` | Our COLLADA reader, which reads the document |
| `tests/test_gap_readers.cpp`, `tests/gap_fixtures.hpp` | Fixtures, truncation, mutation sweep, memory and time caps (`gap_readers`) |
| `tests/test_own_dispatch_gap.cpp` | `.zae` reaches slot 44, the switch, the shipped sample (`own_dispatch_gap`) |

### What it reads

- **The root document**: `<dae_root>` of `manifest.xml` (percent escapes such as `%20` decoded, `./` and `../` worked out inside
  the archive); when there is no manifest, or it names nothing that is in the archive, the `.dae` nearest the archive's top (then
  the first in name order), with a note. A name that climbs out of the archive is not looked up.
- **Everything in the document**, through the COLLADA reader (`docs/content/model_corpus.md`, Collada rows): meshes, nodes, materials,
  skins, animation; the `flatten` switch is passed on.
- **Textures inside the archive.** An image the document names that is in the archive is written next to the `.zae` as
  `<name>_zae_<file>` (for example `duck_zae_duck zaeCM.png`), once per image, and the material points at that file, so the
  renderer can load it. When a file of the entry's size is already there (an earlier read), it is used as it is: not extracted
  or written again. (Assimp's path writes embedded
  textures next to the model in the same way, `<name>_tex<n>.<ext>`.) The buffer version (`ReadZaeBuffer`) writes nothing:
  those textures are left empty, with a note. An image that is not in the archive is looked up next to the `.zae`.

### Not handled

Everything the COLLADA reader does not read (its notes are kept). Documents other than the root one (external references
between documents). ZIP64, encrypted entries and compression other than stored and deflate are refused by the ZIP reader.

### Limits

File 512 MiB; the ZIP reader's caps: 20,000 entries, 256 MiB per entry and 384 MiB per archive uncompressed, and the inflated
size must equal the size the directory declares (zip bombs are refused before they are inflated); `manifest.xml` at most 1,000
XML elements (more: it is ignored, with a note). Then the COLLADA reader's own caps.

### Tests and what was checked

- `gap_readers`: **synthetic** archives built in memory: a manifest that names `sub/doc%20one.dae` while a decoy `.dae` sits at
  the top (the decoy must not be read), no manifest (the top-most `.dae`), a manifest that names a missing file, a manifest that
  climbs out, not a ZIP, a ZIP without a `.dae`; the path version writes the image next to the file and a second read keeps it;
  a texture outside the archive. Then every 64th truncation, 1,500 mutations (20,000 in a longer run; `OKAY_GAP_FUZZ_ITERS`),
  an entry that claims 300 MB and a manifest of 200,000 elements, each under a counting `operator new` (under 64 MB) and a time
  limit. Clean under ASan + UBSan + float-cast-overflow (5,000 mutations).
- `own_dispatch_gap`: `.zae` through `ImportModelData`, `OKAY_OWN_ZAE=1` / `0`, off by default, the shipped sample.

#### Real files

The 3 `.zae` files of Assimp's test folder (`duck.zae`, `duck_nomanifest.zae`, `human.zae`) all read. **Against Assimp v5.4.3
(`bench_model_readers --compare`)**: the two duck files equal Assimp on parts (1), triangles (4,212), vertices (2,399), nodes (4),
materials (1) and box; `human.zae` equals Assimp on parts (2), triangles (28,796), vertices (15,593), nodes, materials (2) and
box. Until 2026-11-16 it gave 0 materials to Assimp's 2: its two `<polylist>` elements have no `material` attribute, and the
COLLADA reader only bound a material through that symbol. COLLADA leaves an unnamed primitive's material to the application;
the reader now uses the instance's material when its `<bind_material>` binds exactly one (both instances here do), and none
when it binds several (`docs/content/model_import.md`, Collada). That change moved only this file's digest among the corpus files;
the manifest row is now `mat=2 tex=1` (the eye texture `textures/brown_eye.png` is inside the archive and is written next to the
`.zae` as `human_zae_brown_eye.png`). Each duck's texture is written next to the file and the material points at it. In the `model_corpus` manifest (reader `zae`). Speed and memory (`docs/content/own_readers_speed.md`, row `zae`): 0.68x Assimp's read time and 0.54x its largest peak heap over the
three files; no file slower or heavier.

Unverified: archives from other exporters (SketchUp, Blender add-ons), a manifest with a namespace prefix, a run on real Windows
(the sidecar files are written with a UTF-8 path through `std::filesystem`, as the FBX texture path does).

### Provenance (what this file was written from)

`ZaeReader.cpp` was written from scratch for this project from the description of the ZAE archive in the COLLADA 1.5
specification (Khronos) and from looking at the entry lists of the three sample archives as data (with Python's `zipfile`, a
dump only). No code, comment or test from Assimp or any other importer was read, copied or ported. Assimp was run only as a
black box on the sample files for the comparison above.
