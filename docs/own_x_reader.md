# Our own DirectX `.x` reader (`.x`)

Status: **side by side, off by default** (`OKAY_OWN_X`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of `.x`. Nothing was flipped.

## What it is

The DirectX `.x` file is the older model format of Direct3D (Microsoft's DirectX SDK, 1997 to about 2008), used by many hobby and indie games and by Irrlicht, OGRE and Blender exporters for years. It exists in three encodings: **text** (`txt`), **binary** (`bin`) and **compressed** (`tzip`, `bzip`: text or binary packed with MSZIP), with 32 or 64 bit floats. Written from the public description of the format (the DirectX SDK documentation of the file format and of its standard templates) with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/XFileReader.cpp` | `okay::ReadXBuffer` / `okay::ReadXFile` (declared in `ModelData.hpp`) |
| `engine/src/Core/ImageDecode.cpp` | The engine's inflate (`okay::Inflate`), used for MSZIP |
| `engine/src/Render/ReaderCommon.hpp`, `TextReaderUtil.hpp` | Shared number parsing and file reading |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 21 of the opt-in table |
| `tests/test_x_reader.cpp`, `tests/batchc_fixtures.hpp` | Hand-computed checks on synthetic files (`x_reader`) |
| `tests/test_batchc_hostile.cpp` | Hostile input and mutation fuzz (`batchc_hostile`) |

## What it reads

- **One token stream for text and binary.** The parser asks for names, braces, integers and floats; text and binary differ only in how those are cut out of the bytes (in text, `;` and `,` are separators and carry no meaning of their own, strings are quoted, comments are `//` and `#`; in binary, tokens are 16-bit ids with payloads, and lists of numbers come as `INTEGER_LIST` / `FLOAT_LIST` tokens). Templates are skipped; unknown objects are skipped by their braces (text) or tokens (binary), and counted by name in the notes.
- **Compressed files**: MSZIP blocks (a 2-byte unpacked size, a 2-byte packed size, `CK`, a deflate stream), each of which may refer back into the previous 32 KiB of output. The reader puts that window in front of the block as a stored deflate block, inflates, and drops the window again (this uses the engine's inflate unchanged).
- **Frames** become nodes (parent first), with the `FrameTransformMatrix`. One top-level frame is the root; several frames, or meshes outside any frame, get a new root named `X`.
- **Meshes**: positions, polygons of any size (fan-triangulated), `MeshNormals` (as stored, per face corner), the first `MeshTextureCoords` set (as written, top-down), `MeshMaterialList` (per-face material, inline `Material` objects and `{ name }` references). One part per mesh and material; vertices welded per position and normal; smooth normals when the file has none.
- **Materials**: `faceColor` (diffuse, with alpha), `power` (shininess), `emissiveColor`, `TextureFilename` and `NormalmapFilename`. Paths: backslashes (written twice in text) become `/`, relative paths are joined to the model's folder.
- **Skin**: `SkinWeights` (bone name = frame name; the four strongest weights per vertex are kept and normalised; the offset matrix is the inverse bind). Vertices with no weight in a skinned mesh follow the root node.
- **Animation**: each `AnimationSet` is one clip (named after the set); each `Animation` names a frame; key types 0 (rotation, `w x y z`), 1 (scale), 2 (position), 3 and 4 (a whole matrix, split into position, rotation and scale). Key times are ticks; seconds = ticks / `AnimTicksPerSecond`, and **25 when the file has no `AnimTicksPerSecond`** (the format's own default is 4800, but game tools write frame numbers and the engine's Assimp path uses 25 for such files; a note says so).
- **Handedness.** DirectX is left-handed and writes matrices for row vectors. Z is mirrored in positions, normals and matrices (`A' = M A M`, `M = diag(1, 1, -1)`), every triangle's winding is reversed, a rotation key `(w, x, y, z)` becomes `(x, y, -z, w)`, and a matrix is transposed before it is split into position, rotation and scale (a mirrored matrix gets one negative scale; a shear cannot be stored and is dropped with a note).

## Not handled (each is a line in `out.notes`)

Vertex colours (`MeshVertexColors`), further texture-coordinate sets, the specular colour, `VertexDuplicationIndices` and other mesh tables, animation options, **shared meshes** (a `{ name }` reference to a mesh inside a frame is not followed), patch meshes and progressive meshes, `.x` files whose text is UTF-16.

## Limits

File 256 MiB (and 256 MiB unpacked for compressed files); vertices and normals 20,000,000; faces 20,000,000; frames and meshes 100,000; materials 100,000; bones 100,000; animation keys 20,000,000; nesting 100,000. A count read from the file must fit in the bytes that remain (a number takes at least 2 bytes in text and 4 in binary) before anything is reserved for it; names and strings 64 KiB. Numbers are finite and within 1e15 (`NumParse.hpp` in text); each MSZIP block must unpack to exactly the size it states, at most 32 KiB.

## Tests and what was checked

- `x_reader`: **synthetic** files built in memory. A text file with a skinned two-material mesh (a triangle, a quad, a degenerate triangle and a face that names a missing vertex), a skeleton of three frames, an `AnimationSet` with position, rotation and matrix keys; every number by hand: the mirrored node transforms, material values and the texture path, area 3 + 6 = 9, winding and normal directions, weights and their normalisation, the root-bone fallback, the inverse bind, animation times (30 ticks per second) and mirrored keys, two top-level frames. Binary files with 32 and 64 bit floats, with and without separator tokens, and a template to skip. MSZIP files, one with a second block that is a single match into the first (so it only reads if the window is passed on). Refusals and **every truncation** of the four kinds of file.
- `batchc_hostile` (X section): every truncation; every four-byte position of the binary and compressed files at 0, 1, 2, 3, 255, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFE, 65536, 65537, NaN, infinity and the largest float; hostile numbers (`nan`, `inf`, `1e999`, `0x10`, `1.#QNAN`, 2^31 and more) in the counts and fields of a text file; 150,000 nested frames; a 1,000,000-deep unknown object; an unclosed string; a 70 KB name; 200,000 matrix numbers; 200,000 materials; a valid text mesh of 100,000 vertices and faces (0.4 s); MSZIP bombs (2,000 blocks of 32 KB from about 140 bytes each, a block that unpacks to more than it says, a total that lies); 120,000 mutations over the four fixtures. Sanitizers (`address,undefined,float-cast-overflow`): clean.

### Real files

Assimp's tree has 12 files with the extension `.x` or `.X`: 11 models (`models/X`: `BCN_Epileptic.X`, `Testwuson.X`, `anim_test.x`, `fromtruespace_bin32.x` (binary), `kwxport_test_cubewithvcolors.x`, `test.x`, `test_cube_text.x`, `test_cube_binary.x`, `test_cube_compressed.x` (MSZIP); `models-nonbsd/X/dwarf.x` and a copy of it in `models/IRR/assets`) and one empty file (`models/invalid/empty.x`: refused by both readers). Two more files without an extension (`TestFormatDetection`, `OV_GetNextToken`) are format-detection tests and are not reached by extension. **All 11 read and equal Assimp v5.4.3 (built from the same sources, only the X importer) on mesh count, triangles, surface area, bounding box, node count and clip count.** Beyond that: on the five animated or skinned files I checked (`dwarf.x`, `BCN_Epileptic.X`, `Testwuson.X`, `anim_test.x`, `test_cube_binary.x`) the **local position, scale and rotation of every node, every key value, every inverse bind matrix (4 to 6 decimals) and every bone's total weight** equal Assimp's after its own handedness conversion (largest differences: position 5e-7, rotation keys 1e-4, weight sums 2e-5). Rotation and scale of a node whose matrix has a negative determinant (the root of the cube files swaps Y and Z) are a different but equivalent split into rotation and scale.

Differences: `anim_test.x` (the file's own notes say its mesh names four bones of which only two exist as frames) has one bone more for us (vertices whose bones do not exist follow the root; Assimp keeps four bone names with no node); Assimp keeps three vertices per triangle, ours welds (e.g. 1,479 against 5,688 for `dwarf.x`); `dwarf.x` has no `AnimTicksPerSecond`, so its 55 ticks are 2.2 s (25 per second), as in the engine's Assimp path (the scratch tool that printed Assimp's side showed 55: ticks).

Not verified: 64 bit binary files (synthetic only), `bzip` (compressed binary) files, compressed files of more than one MSZIP block (synthetic only: the window handover is tested, but no real multi-block file was available), files with several meshes sharing a frame by reference, a run on real Windows.
