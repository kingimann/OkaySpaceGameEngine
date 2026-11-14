# Our own XGL / ZGL reader (`.xgl` `.zgl`)

Status: **side by side, off by default** (`OKAY_OWN_XGL`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of XGL files. Nothing was flipped.

## What it is

XGL is a small XML scene format (written by 3D Studio exporters, Truespace and others, and used by scientific and CAD tools). A `<WORLD>` holds `<MESH>` blocks (points `<P>`, normals `<N>`, texture coordinates `<TC>`, materials `<MAT>` and faces `<F>` whose corners `<FV1>` `<FV2>` `<FV3>` point at them) and `<OBJECT>` blocks that place a mesh with a `<TRANSFORM>`. **ZGL** is the same text compressed as one zlib stream.

| File | What |
|---|---|
| `engine/src/Render/XglReader.cpp` | `okay::ReadXglBuffer` / `okay::ReadXglFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/XmlLite.hpp`, `TextReaderUtil.hpp`, `ReaderCommon.hpp` | Shared strict XML reader, UTF-16 conversion, number parsing, mesh builder |
| `engine/src/Core/ImageDecode.cpp` | The engine's own inflate (`okay::Inflate`), used for ZGL |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 24 (extensions `.xgl`, `.zgl`) |
| `tests/test_xgl_reader.cpp`, `tests/batchd_fixtures.hpp` | Hand-computed checks on synthetic files (`xgl_reader`) |
| `tests/test_batchd_hostile.cpp` | Hostile input and mutation fuzz (`batchd_hostile`) |

## What it reads

- **Compression.** A file that starts with a zlib header is inflated (at most 256 MiB of text; a damaged stream or a bomb is refused), anything else is read as XML. So a `.zgl` renamed `.xgl` and the other way round both work.
- **Meshes.** `<P ID>` x,y,z, `<N ID>`, `<TC ID>` u,v (comma separated); the ids need not be dense. `<F>` faces with `<MATREF>` and corners `<FV1>`..`<FVn>` (`<PREF>`, `<NREF>`, `<TCREF>`); more than three corners are fanned. Faces are grouped by material into parts; corners with a normal and a texture coordinate keep them (a part is welded on position, normal and uv).
- **Materials.** `<MAT ID>` at the world level or inside a mesh (the mesh's own first): `<DIFF>` (or `<AMB>`) as the diffuse colour, `<EMISS>`, `<SHINE>`, `<ALPHA>` as the diffuse alpha.
- **Objects.** Nested `<OBJECT>` blocks become nodes (explicit work list, 256 levels); `<TRANSFORM>`: `<POSITION>`, `<SCALE>` (one number, or the first of three), `<FORWARD>` and `<UP>` as the object's axes (z = forward, y = up, x = up x forward: right-handed; a turn of 90 degrees about Y is forward (1,0,0), up (0,1,0)). A `<MESH>` inside the object, or `<MESHREF>` by id, puts that mesh's parts on the object's node. Without any object the world-level meshes are read once at the root.

## Not handled (each is a line in `out.notes`)

Faces that point at missing points (skipped and counted), line and patch elements, lights, the background and scene lighting, textures, the per-object data blocks, an unknown `<MESHREF>`.

## Limits

File and inflated text 256 MiB; 8,000,000 points, normals or texture coordinates per mesh; 20,000,000 faces; 10,000 corners per face; 1,000,000 objects; 40,000,000 output vertices; the XML reader's caps. Numbers go through `NumParse.hpp` (finite, within 1e15). (While testing this reader the shared XML reader was found to be quadratic on files with many elements and no `&` in them; fixed in `XmlLite.hpp`: a 40,000-face file went from 4.3 s to 0.1 s. Every XML-based reader benefits.)

## Tests and what was checked

- `xgl_reader`: **synthetic** world (a world material, a mesh with its own material, 5 points, one normal, 4 texture coordinates, 3 faces, an object with forward (1,0,0), up (0,1,0), position (5,0,0), scale 2 and a child object that uses the mesh again), as text, as ZGL (stored zlib blocks) and with a byte order mark: parts, triangles, world area 16 (by hand), node tree, the quaternion of the 90 degree turn, a vertex seen through the matrix, materials, uv and normals; meshes without objects, objects without meshes; refusals (empty, wrong root, no mesh, a bad point, a face with an unknown point, a DTD entity, a damaged zlib stream) and **every truncation** of the text and of the ZGL.
- `batchd_hostile` (XGL section): zlib bombs (300 MB of `<`), ids and references at the extremes and `nan` / `inf` / `1e999` in points, references, scale, position, forward and shine, zero, negative and huge scales and parallel forward and up vectors, 5,000 nested objects, 20,000 objects that use one mesh, a mesh of 100,000 faces, a face of 20,000 corners, an entity bomb, a 3 MB number; then 3,000 mutations of the text and 750 of the ZGL (30,000 and 7,500 in the sanitizer run).

### Real files

Assimp's tree has 7 files (`models/XGL`: 6 `.zgl` and `sample_official.xgl`, plus the same sample as `.xml`). **All 7 `.zgl` / `.xgl` files read in both; 6 equal Assimp on triangles, mesh count, surface area and bounding box** (`Wuson.zgl` 3,732 triangles area 9.0258; `Spider_ascii.zgl` 1,312, 56.9476; `BCN_Epileptic.zgl` 5,126, 3 meshes, 2.0697; `cubes_with_alpha.zgl` 60, 5 meshes; `sphere_with_mat_gloss_10pc.zgl` 528; `wuson_dxf.zgl` 3,732), and the 7th, `sample_official.xgl`, differs **by design**: it has two objects that place one mesh at (0, 0.9, 0) and (-0.9, 0, 0); ours applies the positions (bounding box x -1.9 to 0, y -0.5 to 1.4), Assimp's output has both meshes at the origin (x -1 to 0, y -0.5 to 0.5). Material counts differ on two files (`BCN_Epileptic` 3 against 6, `sphere_with_mat_gloss_10pc` 1 against 3): ours keeps only the materials a face uses. `sample_official_asxml.xml` (the same text) fails in Assimp ("Unknown exception"); ours would read it, but the extension `.xml` is not routed to this reader (too many other formats use it).

Unverified: textured XGL (none in the tree has `<TEXTURE>`), patches and lines, the `<FORWARD>` / `<UP>` convention on a real file with a rotation (none in the tree has one), a run on real Windows.

## Provenance (what this file was written from)

`XglReader.cpp` was written from scratch for this project from the public description of the XGL scene format (the element names above, as published with the format) and the zlib container (RFC 1950, with the engine's own inflate), and from reading the sample files of Assimp's `test/models/XGL` as data (never the Assimp source). Assimp was used only as a **black box** (counts, area, box). No code, comment, table or test from any other importer was read, copied or ported.
