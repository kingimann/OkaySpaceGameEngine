# Our own glTF 1.0 reader (`.gltf`, `.glb` of version 1)

Status: **side by side, off by default** (`OKAY_OWN_GLTF1`). Without it, glTF 1.0 files are refused as before, with a message that names the switch. glTF 2.0 files are read by our glTF 2.0 reader (`okay/Render/Gltf.hpp`) whether this switch is on or off. Nothing was flipped.

## What it is

glTF 1.0 is the first version of Khronos's transmission format (2015). It was replaced by glTF 2.0 in 2017, but real 1.0 models still exist (the model corpus has 7). It differs from 2.0 enough that it is a separate reader, not a mode of the 2.0 one:
- every top-level collection is an object keyed by string ids, not an array;
- a node lists several meshes;
- accessors carry their own `byteStride`;
- materials are a shader technique plus a dictionary of values;
- the binary form (the `KHR_binary_glTF` extension) is a 20-byte header, one JSON block and one body, not 2.0's chunks.

**Where it was written from.** The Khronos glTF 1.0 specification only: `specification/1.0/README.md` and its JSON schemas, and the `KHR_binary_glTF` and `KHR_materials_common` extension descriptions, all from the KhronosGroup/glTF repository. No importer source was read: not Assimp's, not any other program's (rule 19a). The JSON parser and the base64 decoder are the engine's own, the ones the glTF 2.0 reader uses. **Assimp was used only as a black box for comparison**: built from its v5.4.3 sources with only its glTF importer and run on the real data files through its public C API, to compare counts, areas and bounding boxes. None of its source and none of its test code was read or used; its test directory supplied data files only (`docs/model_corpus.md`).

| File | What |
|---|---|
| `engine/src/Render/Gltf1Reader.cpp` | `okay::ReadGltf1Buffer` / `ReadGltf1File`, `IsGltf1`, `GltfFileIsVersion1` (declared in `ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam: the switch is slot 41 of the opt-in table under the pseudo-extension `.gltf1` (1.0 and 2.0 share `.gltf` / `.glb`); `ImportModelData`, `ImportModel` and the scene importer send version-1 files to this reader when the switch is on, through `RunReaderGuarded` |
| `tests/test_gltf1_reader.cpp`, `tests/gltf1_test_build.hpp` | Hand-computed checks on files built in memory, files on disk, refusals and the seam (`gltf1_reader`) |
| `tests/test_gltf1_hostile.cpp` | Hostile input, a memory and time budget, mutation fuzz; builds without the engine library (`gltf1_hostile`) |
| `tests/test_own_gltf1_default.cpp` | Fails if `-DOKAY_OWN_GLTF1=ON` stops making this reader the default (`own_gltf1_default`) |

Switch it on with `SetImportBackend(".gltf1", ImportBackend::Own)`, the environment variable `OKAY_OWN_GLTF1=1` (or `0` to force it off), or `-DOKAY_OWN_GLTF1=ON` at build time. Only when it is on does the importer parse a `.gltf` / `.glb` file a second time to see its version, so a default import costs nothing more.

## How to try it

Full steps: `docs/try_it.md`, section 8. Short form: import `examples\own_readers\box_v1.gltf` (shipped in the Windows release; made by `scripts/gen_gltf1_samples.py`) with **File > Import Model...** in the editor, once with no variable (an error that names `OKAY_OWN_GLTF1=1`, nothing is added) and once after `set OKAY_OWN_GLTF1=1` (an orange box stands on the ground); `box_v1.glb` is the same box in the binary form. `own_gltf1_samples` checks both files through the import seam, and `own_gltf1_default` checks `-DOKAY_OWN_GLTF1=ON`. Not verified: a run on Windows.

## What it reads

- **The container.** JSON (a UTF-8 byte order mark is dropped), or the binary form:
  - The header must be magic `glTF`, version 1, then the total length, the content length (more than 0) and content format 0 (JSON).
  - The body is the buffer with id `binary_glTF`.
  - A binary file shorter than its header's length is read with a note, and every accessor is still checked against the bytes that exist. One of the real sample files is short by 33 bytes, at the end of an embedded image.
- **Buffers.** The binary body, base64 `data:` URIs, or files. A file must lie inside the model's own folder (`SceneRefPath.hpp`): absolute paths, drive letters, `..` climbing out, and links out are refused. A buffer shorter than its `byteLength` is refused. Buffers of type `text` hold no geometry and are refused when an accessor uses them.
- **Accessors.** The types `SCALAR` to `MAT4` and the component types byte, unsigned byte, short, unsigned short and float, plus unsigned int for indices. The rest:
  - `byteStride` 0 means tightly packed; otherwise it must be at least one element (at most 255, as the schema says).
  - The last element must end inside the bufferView, and a bufferView of `byteLength` 0 runs to the end of its buffer.
  - Every float must be finite.
- **The scene.** The node tree under one root node named `glTF`, walked with an explicit stack. Which roots:
  - the `scene` named by the file;
  - with no default scene, the first scene by id (noted when there are several);
  - with no scenes at all, every node that is nobody's child.
  
  A node reached twice (a loop, or two parents) is refused. Each node's transform is its `matrix` (column-major; shear is dropped, with a note) or its `translation`, `rotation` (x, y, z, w; normalised) and `scale`. The axes and units are glTF's, the same as the engine's (Y up, metres), so nothing is converted.
- **Meshes.** Each node's `meshes` list. Each triangle primitive becomes one part:
  - **Modes:** triangles (4), strips (5) and fans (6), unrolled as the specification describes. A strip alternates the order of every other triangle so they all face the same way.
  - **Attributes:** `POSITION` (VEC3, required), `NORMAL` (VEC3, made from the faces when missing or zero), `TEXCOORD_0` (VEC2, kept as written, like the 2.0 reader), and indices (unsigned byte, short or int, each inside the primitive's vertices).
  - Points and lines are skipped with a note.
- **Materials.** One engine material per glTF material, from its `values` and from `extensions.KHR_materials_common.values`:
  - `diffuse` as a colour, or as a texture id that leads, through `textures` and `images`, to an image file inside the model's folder;
  - `emission`;
  - `shininess`.

## Reported, not read (each is a line in `out.notes`)

Skins (joints and weights: skinned meshes import in their bind pose), animations, cameras, the material techniques and shaders (only the values above are used), embedded images (data URIs and images in the binary body: the material keeps its colour), image paths outside the model's folder, points and lines, primitives of an unknown mode, NORMAL or TEXCOORD_0 accessors that are not one VEC3 / VEC2 per vertex, degenerate triangles, bytes after a binary file's length, a binary file shorter than its length.

## Refused (an error, nothing imported)

Not JSON; JSON nested more than 128 deep (the JSON parser is recursive, so this is checked first); `asset.version` 2.x, or collections that are arrays (glTF 2.0); a binary header with another version or format, or a content block that does not fit; a missing accessor, bufferView, buffer, mesh, node or default scene; an accessor with a bad count, offset, stride, type or component type, or that reads past its bufferView; a bufferView past its buffer; a non-finite value; an index past the vertices; a node loop or a node with two parents; a bad matrix, translation, rotation or scale; a buffer file outside the folder or missing; a scene with no triangles.

## Limits

File 512 MiB; all buffers together 512 MiB; 20,000,000 elements in one accessor; **64,000,000 accessor values decoded in the whole file** (attributes are decoded to 4-byte floats, indices to 32-bit integers; a node may list one mesh many times and each listing decodes it again, so this budget is for the file, and it is checked before anything is allocated); 20,000,000 vertices; 20,000,000 triangle slots, **degenerate triangles included** (so equal indices cannot make free work); 100,000 nodes; 100,000 primitives (skipped ones included). JSON nesting 128. Counts and offsets are checked in 64-bit arithmetic before anything is read or allocated.

Measured by `gltf1_hostile` with a counting allocator:
- A refused 50-million-element accessor over a 12-byte view: 20 KB.
- One mesh listed 4 times, each with 20,000,000 equal (degenerate) indices: refused at 95 MB in 0.3 s. Before the fix: 324 MB.
- A 20,000,000-element byte accessor of 4 components: refused before any allocation, at 76 MB, which is the copied body. Before the fix: 686 MB.
- A 1,000,000-triangle file: read in under a second with a 58 MB peak.

## Tests and what was checked

- `gltf1_reader`: files built in memory (`tests/gltf1_test_build.hpp`) with every value worked out by hand:
  - **The quad scene:** the node tree, the matrix (scale 2, translation 0 0 5) and translation transforms, positions, normals, texture coordinates, indices, and the material's colour and shininess.
  - **The same scene in other forms:** the binary form (and a binary file cut short), translation / rotation / scale nodes, and a file with no scenes.
  - **Materials:** `KHR_materials_common` colours, a texture file inside the folder, and one outside it (refused, noted).
  - **Strips, fans, points and lines.**
  - **Refusals:** 24, each with its message, plus a NaN position.
  - **Files on disk:** an external buffer in a subfolder of a UTF-8 folder name, a missing buffer, and a glTF 2.0 file that is not taken by this reader even with it switched on.
  - **The seam:** off by default (refused, with the message naming the switch), `OKAY_OWN_GLTF1`, `SetImportBackend(".gltf1")`, `ImportModelScene` for the JSON and binary forms, `ImportModel`, and flatten (the corner `(1 1 0)` lands at `(3 4 8)`).
- `gltf1_hostile`, run under ASan, UBSan and float-cast-overflow with the real files: 25,160 checks, 0 failures. It covers:
  - every prefix of every seed (every JSON prefix that ends before the closing brace is refused);
  - a million nested brackets, huge counts, offsets and lengths, bad strides and types;
  - a node under itself, a 20,000-node chain, and a NUL byte;
  - the memory and time budget;
  - a mutation fuzz (8,000 by default).
- **Mutation check:** each of these breaks makes a test fail: removing the accessor bounds check (heap overflow in the sanitized hostile test), removing the JSON nesting limit (stack overflow), and not alternating the strip winding (the reader test).
- `own_gltf1_default`: passes with the option off. Run here with the engine configured `-DOKAY_OWN_GLTF1=ON` (Linux, GCC 13, Release): `own_gltf1_default` 3 checks, 0 failures (the file is read by our reader with no environment variable set); `gltf1_reader` 155 checks, 0 failures; `gltf1_hostile` 10,025 checks, 0 failures. Mutation check: forcing the compiled default off (as a dropped CMake line would) makes `own_gltf1_default` fail all 3 checks. **No CI job builds with `-DOKAY_OWN_GLTF1=ON`**, so in CI this test only checks the off case.

### Real files

Assimp's test tree has 7 glTF 1.0 files under `models/glTF`: the textured box as separate files, embedded (data URIs), binary and with `KHR_materials_common`; `TwoBoxes`; `IncorrectVertexArrays/Cube_v1` (triangles plus point and line primitives); and `CesiumMilkTruck` (5 meshes, 4 materials, 2 animations). They are not stored in this repository; `docs/model_corpus.md` says how to point the tests at them. **All 7 read**: the `gltf1` rows of `tests/data/model_corpus_manifest.tsv` record them, while the `gltf` rows record the same files failing with the switch off.

Compared with Assimp v5.4.3, built from the same sources with only its glTF importer and run as a black box through its C API, with node transforms applied:
- **The four textured boxes and `TwoBoxes`:** equal on meshes, vertices, triangles, area (6 and 7.5) and bounding box. Assimp does not read the binary box at all ("No suitable reader"); ours does, from the bytes present (the file is 33 bytes short).
- **`Cube_v1`:** the same 46 triangles and the same area (92). Assimp counts the 4 point and line primitives as extra meshes, and its bounding box includes their vertices (z from -3.5); ours skips them with a note, so its box is that of the triangles (z from -1.0).
- **`CesiumMilkTruck`:** the same meshes, vertices (8,348), triangles (3,624), area (64.8164) and materials (4), and the same box except its lowest point (ours 0.001, Assimp -0.258).
  - **The difference is the wheels.** Their nodes have a translation (1.43, 0.43, 0) and a rotation of about -10 degrees round Z, given as (x, y, z, w) as the specification says. With them, the wheels (radius 0.428 around their own origin) touch the ground at y = 0.001 under the body, which starts at 0.267. Assimp's lowest point is -0.258, below the ground, which does not fit the model. This is our reading of why the two differ; we did not look into Assimp's code.


Fixed when the branch was brought up to date (2026-11-15): **file names written as URIs.** A buffer or an image named `my%20model.bin` (Blender and other exporters write a space as `%20`) was looked up with the `%20` in it and refused with "cannot open file"; the glTF 2.0 reader already decoded such names. The reader now decodes them (`gltf_detail::UriDecode`) before the folder rule runs, so an escaped `..` or NUL is still refused (`gltf1_reader`, the "percent-escaped file name" checks; reproduced before the fix with a file named `my quad.bin`).

Known limits (low severity, from the review of this branch):
- **Escaped characters.** The shared JSON parser (`okay/Render/Gltf.hpp`, the glTF 2.0 one too) turns `\u` escapes into `?`. A non-ASCII uri or id written with escapes then names the wrong file or id.
- **Material values.** They are matched by the names `diffuse`, `emission` and `shininess`. A technique that uses other parameter names (such as `u_diffuse`) loses its colour.
- **Not read:** technique defaults and transparency, and embedded images (so a binary file's textures).
- **The version check.** When the switch is on, `GltfFileIsVersion1` reads the whole file to see its version; it runs inside `RunReaderGuarded`, so a file too large for memory means "not version 1" instead of a crash. With the switch on, a file is held twice while it is read (the version check reads it, then the reader reads it again), so the peak is about twice the file's size plus the decoded mesh.
- **Practical size ceiling.** The 64,000,000-value budget is for the whole file. A mesh with a position, a normal and one UV per vertex uses 8 values a vertex, so about 8,000,000 vertices fit when there are no indices; indices (3 per triangle), more UV sets, colours, or a mesh listed by several nodes use the same budget and lower that number. A larger file is refused with a note, not cut short.
- **JSON element count.** The shared JSON parser has a nesting limit in this reader (128) but no limit on how many values one array or object holds. A 512 MiB file of `0,` would build hundreds of millions of values before the reader looks at them (the glTF 2.0 reader shares this). A failed allocation is caught by `RunReaderGuarded` and reported as a failed import, but the attempt itself can use a lot of memory first. A count cap in the parser is still to do.

Not verified: glTF 1.0 files from exporters other than those in the corpus, skinned or animated 1.0 files beyond the milk truck's two animations (not read), a run on real Windows.
