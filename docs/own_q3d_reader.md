# Our own Q3D reader (`.q3o` `.q3s`)

Status: **side by side, off by default** (`OKAY_OWN_Q3D`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is the only other reader. Nothing was flipped. **Checked on the four Quick3D files of the model corpus** (`docs/model_corpus.md`; not in this repository): the description left three points of the layout open, and the first version of the reader guessed two of them wrong, so it refused all four files. The layout below is the one those files have (see "Checked against real files"); triangle counts, material counts and bounding boxes now equal Assimp's on all four.

## What it is

Quick3D is an old Mac and Windows modeller. A `.q3o` file is one object, a `.q3s` file is a scene (the same plus a scene chunk). Both are binary.

| File | What |
|---|---|
| `engine/src/Render/Q3dReader.cpp` | `okay::ReadQ3dBuffer` / `okay::ReadQ3dFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 31 (extensions `.q3o`, `.q3s`) |
| `tests/test_q3d_reader.cpp`, `tests/batche2_fixtures.hpp` | Hand-computed checks on a hand-built file (`q3d_reader`) |
| `tests/test_batche2_hostile.cpp` | Truncations, count and index overwrites, bit flips (`batche2_hostile`) |
| `tests/test_batche2_budget.cpp` | Budgets and memory with a counting `operator new` (`batche2_budget`) |
| `tests/test_own_dispatch_e2.cpp` | The seam routes `.q3o` / `.q3s` to this reader (`own_dispatch_e2`) |

## What it reads

The 22 byte header (signature `quick3Do` or `quick3Ds`, a 2 character version, the number of meshes, materials and textures), then chunks, each one type byte: `m` meshes, `c` materials, `t` textures, `s` scene. Each mesh: vertices, faces (a 16-bit corner count per face, then the vertex indices), a material index per face (-1 = none), normals, a **texture coordinate count (always there)**, and, only when the header names textures, the texture coordinates and per-corner texture indices; then a centre of mass and a bounding box (skipped). Each material: a NUL-ended name, ambient, diffuse and specular colours, a transparency and a **texture index** (-1 = none). A scene chunk is taken in any file and ends the read (the corpus file `WusonOrange.q3s` says `quick3Do` and has one; bytes after it are ignored). Faces are triangulated: up to 32 corners convex ones are fanned and concave ones ear clipped; bigger ones, and every polygon once the per-file ear clipping work (sum of corners cubed) passes 400,000,000, are fanned from their first corner (a note says so; a concave one may then come out wrong). One part per mesh and material; materials carry the diffuse colour, and the transparency as `alpha = 1 - transparency`, except that a transparency of 1 (fully clear) is read as opaque, with a note: the glass materials of the corpus carry 0.12 and 0.18 there, but the only material of `WusonOrange` carries exactly 1, which would make the whole model invisible (Assimp's materials come out opaque on all four files). Stored normals are used when there is one per vertex and all are usable; otherwise smooth normals are made.

## Not handled (each is a line in `out.notes`)

Ambient and specular colours; the embedded textures (skipped, not decoded; the material's texture index is read past); a texture coordinate count in a file whose header names no texture (`E-AT-AT.q3o` has one of 230 with no coordinates after it: counted in a note, no UVs); the scene chunk (placement, camera, lights, background, which is only skipped); faces with fewer than 3 corners; faces with an out-of-range index (skipped and counted).

## Checked against real files (2026-11-16)

The four corpus files (`models/Q3D/earth.q3o`, `E-AT-AT.q3o`, `WusonOrange.q3o`, `WusonOrange.q3s`) were walked byte by byte with a throw-away script that follows the layout above, and every one ends exactly at its last byte (the `.q3s` at its scene chunk). What the bytes settled:

1. **Byte order:** little endian in all four (the big endian fallback stays for other files).
2. **The texture coordinate count is always there**, also when the header names no texture; the coordinates and their indices are not. The first reader left the count out, read it as the next mesh's vertex count and refused `earth` and `E-AT-AT` ("bad vertex count").
3. **A material ends with a 4-byte texture index** after the transparency (-1 in all four files). The first reader did not read it, so after the first material it was 4 bytes off: "an unexpected or repeated chunk type 158" on both `WusonOrange` files.
4. **The scene chunk in a `quick3Do` file** (`WusonOrange.q3s`): read like in a `quick3Ds` file.
5. **Normals:** one per vertex in every mesh of the four files (used when all are usable; `earth` and `E-AT-AT` each have a mesh with zero normals, so normals are made there, noted).
6. **Transparency:** 0 = opaque, small values for glass, and 1 on `WusonOrange`'s only material, read as opaque (above). **Up axis, winding and V direction** are kept as written.

`bench_model_readers --compare` (Assimp run from the same build, its code not read) on the four files: triangles 11,156 / 2,937 / 3,732 / 3,732, materials 7 / 2 / 1 / 1 and the bounding boxes equal Assimp's, except the `.q3s` box: Assimp applies the scene chunk's placement and ours does not read it (documented above). Parts and vertices differ by design: ours makes one part per mesh and material (35 on `E-AT-AT`, Assimp merges per material into 7) and welds vertices per mesh (6,262 against 6,238).

Still open: a file **with** textures (none in the corpus): whether its texture coordinates, per-corner indices and texture records are laid out as the description says is unverified.

## Limits

File 256 MiB; 100,000 meshes, materials and textures each; 4,000,000 vertices and 4,000,000 faces per mesh; 4,096 corners per face; 16,000,000 face corners, 400,000,000 units of ear clipping work, 4,000,000 triangles and 4,000,000 output vertices per file. Every count is checked against the bytes really left in the file before anything is allocated (a 300 byte file that claims 2,000,000 vertices is refused with no allocation). Floats must be finite and within 1e15. The file is parsed as little endian and, only if that fails, as big endian; a header read in the wrong order gives counts over the limits and fails at once, so the second parse costs more than the first only for a header whose bytes read the same both ways, and the total is at most twice one parse. No recursion. A reader that throws is turned into a refusal by `RunReaderGuarded`. The reader names no other file, so `ResolveInsideDir` is not needed.

## Tests and what was checked

- `q3d_reader` (131 checks): the fixture follows the real layout (texture coordinate count always written, a texture index per material); also the three corpus cases (a count of 230 with no coordinates, a scene chunk in a `quick3Do` file with bytes after it, transparency 1 read as opaque).
- `model_corpus`: the four real files (rows `q3d` in `tests/data/model_corpus_manifest.tsv`).
- Earlier checks, still in `q3d_reader`: a hand-built file (a unit quad and a triangle, two materials, four texture coordinates), in little and big endian, as object and as scene: parts, vertex and triangle counts, areas (1 + 1), every UV against its vertex, alpha 0.75, notes; the same without textures; refusals. Mutation checks (code broken, test red, code restored): alpha formula, texture index.
- `batche2_hostile` (shared with RAW): every truncation, every 4-byte window of the file set to 0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF and 0x00FFFFFF, 5,000 bit flips per variant, 300 garbage files; 26,279 inputs read and 15,120 refused in all; every result checked for sanity (finite, indices in range). Also clean under `-fsanitize=address,undefined,float-cast-overflow`. This test found a real bug (texture coordinates that were NaN or infinite reached the mesh); it is fixed.
- `batche2_budget`: 4,000,000 triangles read (peak 446 MB, 1 s); one quad more refused naming the budget; 4,200,000 output vertices refused (peak 538 MB); counts that claim more than the bytes hold refused with no allocation (a face-size claim of 16,000,000 corners in 8 KB peaks under 1 MB; removing that check turns the test red).

## Provenance (what this file was written from)

`Q3dReader.cpp` was written from the public description "Quick3D Object File" on Paul Bourke's format pages (the header, the chunk types and the field lists). No importer source, and no code or snippet from any other project, was read. The 2026-11-16 fixes come from the bytes of the four corpus files (above); the description page itself could not be reached from the machine then. Assimp was only run (through `bench_model_readers --compare`) to compare counts and boxes.
