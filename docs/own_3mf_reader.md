# Our own 3MF reader (`.3mf`)

Status: **side by side, off by default.** Assimp (when the build has it, `OKAY_USE_ASSIMP`) is still the only other reader of `.3mf`. Without Assimp and without switching ours on, a `.3mf` file does not import. Nothing was removed. This is the first of the five readers (3MF, AMF, X3D, DXF, LWO) that the "drop-the-rest" step of `docs/own_code_roadmap.md` needed before Assimp can go.

## What it is

Written from the public 3MF Core Specification (the zip package, the `3D/3dmodel.model` XML part), with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/ThreeMfReader.cpp`, `engine/include/okay/Render/ThreeMfReader.hpp` | `okay::Read3mfBuffer` / `okay::Read3mfFile` |
| `engine/src/Render/ZipLite.hpp` | The zip reader (also used by AMF): central directory, stored and deflate entries, the engine's own inflate (`okay::Inflate`, `Core/ImageDecode.cpp`) |
| `engine/src/Render/XmlLite.hpp` | The strict XML reader shared by 3MF, AMF and X3D |
| `engine/src/Render/ReaderCommon.hpp` | Shared by all five batch-B readers: locale-independent number parsing (`NumParse.hpp`), transforms, the triangle soup and its smooth-normal builder, the Z-up root |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 11 of the per-format backend switch (the batch-B table) |
| `tests/test_3mf_reader.cpp` | Hand-computed checks on synthetic packages built in memory, and the seam (`3mf_reader`) |
| `tests/test_batchb_hostile.cpp` | Hostile input and mutation fuzz for all five readers, built on its own under sanitizers (`batchb_hostile`) |

## Turning it on

Exactly like `OKAY_OWN_3DS`: `-DOKAY_OWN_3MF=ON` (default `OFF`), environment variable `OKAY_OWN_3MF=1` / `0`, or `okay::SetImportBackend(".3mf", ImportBackend::Own)` (the run-time choice wins). Off with no Assimp in the build: `ImportModelData` fails with a message saying how to switch it on, and `ImportableExtensions()` does not list `.3mf`. On with Assimp built: ours is tried first, Assimp is the fallback for files ours rejects.

## What it reads

- **Package**: the zip's `_rels/.rels` names the model part (`.../3dmodel` relationship); without it `3D/3dmodel.model` is used. Only that part is read (the production extension's other model files are not).
- **Meshes**: `object` / `mesh` / `vertices` / `triangles`. Triangles keep the file's winding (counter-clockwise from outside).
- **Components and build**: `components` / `component` trees (nested up to 16 deep, cycles refused) and `build` / `item`, each with its 12-number `transform` (row-vector convention: `x' = x*m0 + y*m3 + z*m6 + m9`). A transform with a negative determinant (a mirror) swaps two corners of every triangle so faces keep pointing outward. Each build item is one node (named by `partnumber`, else the object name) with one part per material; the item's transform is baked into the vertices. A file without a `build` section uses every object that is not a component of another one, with a note.
- **Materials**: `basematerials` (`base` name and `displaycolor`) and `colorgroup`; the object's `pid` / `pindex` and the triangle's `pid` / `p1`. One part per distinct (group, index); the colour (with alpha) is the material's diffuse colour. A triangle whose three corners name different colours takes the first corner's colour (noted).
- **Object types**: `support` and `solidsupport` objects are not output (noted); `model`, `surface` and `other` are.
- **Axes and units**: the package is Z-up; the root node carries the Z-up to Y-up rotation `(x, y, z) -> (x, z, -y)` like the 3DS reader, and `ImportModel`'s flattened path bakes it into the vertices. Units are kept as written (3MF default: millimetre); a unit other than millimetre is reported in the notes.
- **Normals**: 3MF has none. Smooth normals are made with a 45 degree crease angle: faces whose normals differ by less stay smooth across a shared vertex, harder edges stay sharp.

## Limits (hostile files)

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

## Not handled

Each is also reported in `ImportedModel::notes` where the file has it:

- Textures (`texture2d`, `texture2dgroup`), composite materials and multi-properties: triangles that use them get no material.
- The beam lattice, slice and other extensions; components that point to another model file (`p:path`); `requiredextensions` are listed in the note.
- Metadata (title, designer, ...), thumbnails, print tickets, the `printable` flag.
- Transform hierarchy: transforms are baked, so the scene has one node per build item, not one per component.
- The 3MF zip is limited to 3,000,000 XML elements (about 2.5 million triangles); larger files are refused with a message. The reader keeps the XML tree in memory, it does not stream.

## Tests and what was checked

- `3mf_reader` (`tests/test_3mf_reader.cpp`): **synthetic** packages built in memory (a stored and a deflated zip with `_rels/.rels` and the model part): a 10 x 7 x 5 box (vertex and triangle counts, flat normals on the 90 degree edges, the signed volume 350, the Z-up turn with and without `flatten`), base materials (names, colours, alpha, one part per material), colour groups, texture groups (noted, no material), component trees with transforms (the row-vector convention checked with a 90 degree turn, scale, a mirror keeping the volume positive), cycles, missing objects, support objects, units, a missing `build`, an out-of-range index, a decimal-comma locale, every refusal (not a zip, no model part, no triangles, bad transform, a NaN, DTD entities, an encrypted entry), and the seam (opt-in switch, `ImportModel`, a UTF-8 folder and file name).
- `batchb_hostile` (3MF section, standalone, no engine): every truncation of a package; zip bombs (a declared size past the cap is refused before inflating; a small declared size on a stream that inflates to far more stops at the declared size; an honest 100 MB entry is read in bounded time); a wrong entry count, directory offset, end record, checksum and method; XML nested 20,000 deep, 5,000 attributes on one element, billion-laughs and external-entity DOCTYPEs, UTF-16; bad numbers and indices (`1e999`, `inf`, `nan`, hex, `1e16`, negative, 2^32); a 100-deep component chain and a 30 x 30 x 30 x 30 x 30 fan-out (instance cap, no hang); 2,000 materials; then a deterministic mutation fuzz (byte flips, inserts, deletes, number swaps, truncation of the XML with a valid checksum, and of the container). Built with `-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all`: clean.

### Real files

**One real 3MF file exists in Assimp's test tree** (`models/3MF/box.3mf`, a 10 x 20 x 30 millimetre box in a deflate-compressed package from a 2015 sample), so that is all the real-file validation there is; everything else above is synthetic. Against Assimp 5.4.3 built from the same sources: both read it; 1 mesh, 12 triangles, surface area 2200 and bounding box equal (Assimp's box turned the same Z-up way; Assimp keeps the file's 8 shared vertices, ours has 24 because the 90 degree edges stay sharp). Not verified on real files: colour groups and base materials (a real 3MF written by a slicer or CAD tool), the production extension, components across files, textures, anything over 8 vertices.
