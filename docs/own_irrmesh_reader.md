# Our own Irrlicht mesh reader (`.irrmesh`)

Status: **side by side, off by default** (`OKAY_OWN_IRRMESH`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of `.irrmesh`. Nothing was flipped.

## What it is

`.irrmesh` is the static mesh format of the Irrlicht engine and its tools (irrEdit, the exporters built on it): an XML file with one or more `<buffer>` elements, each holding a material, a list of vertices (one per line: position, normal, colour, texture coordinates) and a list of triangle indices. Written from the public description of the format and the files its tools write, with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/IrrMeshReader.cpp` | `okay::ReadIrrMeshBuffer` / `okay::ReadIrrMeshFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/XmlLite.hpp`, `TextReaderUtil.hpp`, `ReaderCommon.hpp` | Shared strict XML reader (caps; DOCTYPE skipped, entities refused), UTF-16 conversion, number parsing, mesh builder |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 19 of the opt-in table (extension `.irrmesh` only) |
| `tests/test_irrmesh_reader.cpp`, `tests/batchc_fixtures.hpp` | Hand-computed checks on a synthetic file (`irrmesh_reader`) |
| `tests/test_batchc_hostile.cpp` | Hostile input and mutation fuzz (`batchc_hostile`) |

## What it reads

- One part and one material per `<buffer>` that has vertices and triangles. Vertex types `standard`, `2tcoords` and `tangents`; positions, normals and the first texture coordinates as written (no rotation, no flip); triangles as written.
- **Handedness.** Irrlicht is left-handed. The reader does not convert it, as Assimp does not: the model is a mirror image of what a right-handed viewer expects, and its normals and winding stay consistent (checked on the real files: the stored normals agree with the winding on 99 percent of the triangles; the few that disagree are in the files). A note says so on every import.
- Material: `Diffuse` and `Emissive` colours (`aarrggbb` hex), `Shininess`, `Texture1` as the texture, `Texture2` as the normal map when the type is `normalmap_*` or `parallaxmap_*`. Texture paths use `/`, relative paths are joined to the model's folder.
- UTF-16 files (with a byte order mark) are converted to UTF-8 first, as Irrlicht's tools write them.

## Not handled (each is a line in `out.notes`)

Second texture coordinates (type `2tcoords`), tangents and bitangents, vertex colours (the engine's meshes have none per vertex), lightmaps and other second textures, texture 3 and 4, material types, blending, wrap modes and flags, bounding boxes. **The Irrlicht scene format `.irr`** (nodes, transforms, references to meshes) is a different reader and is not done. Assimp also sends `.xml` files to its Irrlicht readers by looking at the root element; this reader is only reached by the extension `.irrmesh`.

## Limits

File 256 MiB (and the XML reader's caps: 2,000,000 elements, nesting 256, 256 attributes per element); vertices per buffer 4,000,000; indices per buffer 12,000,000; output vertices 20,000,000. `vertexCount` and `indexCount` must be whole numbers within the caps and the text must really hold that many values, or the file is refused before anything large is allocated. Numbers go through `NumParse.hpp` (finite, within 1e15, locale independent); colours must be 1 to 8 hex digits; an index past the vertex list skips that triangle.

## Tests and what was checked

- `irrmesh_reader`: **synthetic** file in three buffers built in memory (a triangle with edges 2 and 3, a 2tcoords quad of 4 vertices and 2 triangles, a tangents triangle with a stray index; total area 4.5 by hand), with `\n` and `\r\n` line ends and as UTF-16: counts, the material colours and texture paths computed by hand, the normal map, texture coordinates, notes, refusals (empty, wrong root, no buffer, a vertex count that does not match, a negative count, bad number, bad colour, unknown vertex type, an index past the vertices, a DTD entity) and **every truncation**.
- `batchc_hostile` (IRRMESH section): counts at, past and far past the caps with tiny text, `nan`, `inf`, `1e999`, hex floats and overflowing numbers in every kind of field, bad colours, an entity bomb, 5,000 levels of nesting, 20,000 empty buffers, a 3 MB number, 50,000 material attributes, a valid 60,000-vertex mesh (0.7 s), then 100,000 mutations of the file and 25,000 of its UTF-16 form. Sanitizers (`address,undefined,float-cast-overflow`): clean.

### Real files

Assimp's tree has 4 `.irrmesh` files (`cellar` and `spider`, each as UTF-8 and UTF-16LE). **All 4 read and equal Assimp v5.4.3 (built from the same sources, only the IRRMESH importer) on surface area, bounding box and mesh count**; triangles are equal on `cellar` (272) and on `spider` ours has 1,312 to Assimp's 1,368 (the 56 extra are zero-area triangles, which ours drops, as the other readers do). Assimp counts the lightmap among the textures (3 for `cellar`, ours 2). Both UTF-16 files give the same result as their UTF-8 twins. The two `testFormatDetection*.xml` files in the tree are the same format under the extension `.xml` and are not reached by this reader.

Not verified: `tangents`-type buffers on a real file (synthetic only), files from tools other than irrEdit, the Irrlicht-written `.irrmesh` of other versions of the format, a run on real Windows.
