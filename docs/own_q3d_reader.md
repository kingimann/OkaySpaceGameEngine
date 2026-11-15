# Our own Q3D reader (`.q3o` `.q3s`)

Status: **side by side, off by default** (`OKAY_OWN_Q3D`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is the only other reader. Nothing was flipped. **Unverified against real Quick3D files**: there is no `.q3o` or `.q3s` file in this repository, and the layout below comes from one public description that leaves some points open (see "Open points").

## What it is

Quick3D is an old Mac and Windows modeller. A `.q3o` file is one object, a `.q3s` file is a scene (the same plus a scene chunk). Both are binary.

| File | What |
|---|---|
| `engine/src/Render/Q3dReader.cpp` | `okay::ReadQ3dBuffer` / `okay::ReadQ3dFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 31 (extensions `.q3o`, `.q3s`) |
| `tests/test_q3d_reader.cpp`, `tests/batche_fixtures.hpp` | Hand-computed checks on a hand-built file (`q3d_reader`) |
| `tests/test_batche_hostile.cpp` | Truncations, count and index overwrites, bit flips (`batche_hostile`) |
| `tests/test_batche_budget.cpp` | Budgets and memory with a counting `operator new` (`batche_budget`) |
| `tests/test_own_dispatch_e.cpp` | The seam routes `.q3o` / `.q3s` to this reader (`own_dispatch_e`) |

## What it reads

The 22 byte header (signature `quick3Do` or `quick3Ds`, a 2 character version, the number of meshes, materials and textures), then chunks, each one type byte: `m` meshes, `c` materials, `t` textures, `s` scene. Each mesh: vertices, faces (a corner count per face, then the vertex indices), a material index per face, normals, and, when the header names textures, texture coordinates and per-corner texture indices; then a centre of mass and a bounding box (skipped). Faces of any size are triangulated (convex ones fanned, concave ones ear clipped). One part per mesh and material; materials carry the diffuse colour, and the transparency as `alpha = 1 - transparency`. Stored normals are used when there is one per vertex and all are usable; otherwise smooth normals are made.

## Not handled (each is a line in `out.notes`)

Ambient and specular colours; the embedded textures (the description gives no link from a material to a texture, so they are skipped, not decoded); the scene chunk (placement, camera, lights, background, which is only skipped); faces with fewer than 3 corners; faces with an out-of-range index (skipped and counted).

## Open points (the description does not say; these are the choices made)

1. **Byte order.** Not stated. The reader tries little endian first and, if that does not parse, big endian; the note says which one was used.
2. **No textures in the header.** The description says the texture coordinate and texture index fields are left out then. The reader also leaves out the count of texture coordinates. If real files keep a zero count there, they would not read.
3. **Normals.** The description lists a normal count but no per-corner normal index; the reader uses them only when the count equals the vertex count.
4. **Transparency** is read as 0 = opaque, 1 = clear. **Up axis, winding and V direction** are kept as written (nothing is flipped).
5. **Chunk order.** Any order, each type at most once; the scene chunk ends the read.

## Limits

File 256 MiB; 100,000 meshes, materials and textures each; 4,000,000 vertices and 4,000,000 faces per mesh; 4,096 corners per face; 16,000,000 face corners, 4,000,000 triangles and 4,000,000 output vertices per file. Every count is checked against the bytes really left in the file before anything is allocated (a 300 byte file that claims 2,000,000 vertices is refused with no allocation). Floats must be finite and within 1e15. No recursion. A reader that throws is turned into a refusal by `RunReaderGuarded`. The reader names no other file, so `ResolveInsideDir` is not needed.

## Tests and what was checked

- `q3d_reader` (126 checks): a hand-built file (a unit quad and a triangle, two materials, four texture coordinates), in little and big endian, as object and as scene: parts, vertex and triangle counts, areas (1 + 1), every UV against its vertex, alpha 0.75, notes; the same without textures; refusals. Mutation checks (code broken, test red, code restored): alpha formula, texture index.
- `batche_hostile` (shared with RAW): every truncation, every 4-byte window of the file set to 0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF and 0x00FFFFFF, 5,000 bit flips per variant, 300 garbage files; 26,279 inputs read and 15,120 refused in all; every result checked for sanity (finite, indices in range). Also clean under `-fsanitize=address,undefined,float-cast-overflow`. This test found a real bug (texture coordinates that were NaN or infinite reached the mesh); it is fixed.
- `batche_budget`: 4,000,000 triangles read (peak 446 MB, 1 s); one quad more refused naming the budget; 4,200,000 output vertices refused (peak 538 MB); counts that claim more than the bytes hold refused with no allocation (a face-size claim of 16,000,000 corners in 8 KB peaks under 1 MB; removing that check turns the test red).

## Provenance (what this file was written from)

`Q3dReader.cpp` was written from the public description "Quick3D Object File" on Paul Bourke's format pages (the header, the chunk types and the field lists). No importer source, and no code or snippet from any other project, was read. No sample file was available, so nothing was compared with another reader.
