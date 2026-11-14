# Our own OGRE XML mesh reader (`.mesh.xml`)

Status: **side by side, off by default** (`OKAY_OWN_OGRE`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of OGRE meshes. Nothing was flipped. **Only the XML form is done**: the binary `.mesh` and `.skeleton` files are not read (see "Not done").

## What it is

OGRE is an open 3D engine; its tools write meshes as a binary `.mesh` file and, through the `OgreXMLConverter`, as XML (`name.mesh.xml`, with the skeleton in `name.skeleton.xml`). Exporters for Blender, 3ds Max and Maya write the XML form. A `<mesh>` holds shared vertex data, a list of `<submesh>` elements (each with faces and optionally its own vertex data) and links to a skeleton.

| File | What |
|---|---|
| `engine/src/Render/OgreXmlReader.cpp` | `okay::ReadOgreXmlBuffer` / `okay::ReadOgreXmlFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/XmlLite.hpp`, `TextReaderUtil.hpp`, `ReaderCommon.hpp` | Shared strict XML reader (caps; DOCTYPE skipped, entities refused), UTF-16 conversion, number parsing, mesh builder |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 23 of the opt-in table; the extension is the last **two** parts, `.mesh.xml` (`ExtensionOf` was taught this; other `.xml` files are not routed here) |
| `tests/test_ogre_reader.cpp`, `tests/batchd_fixtures.hpp` | Hand-computed checks on synthetic files (`ogre_reader`) |
| `tests/test_batchd_hostile.cpp` | Hostile input and mutation fuzz (`batchd_hostile`) |

## What it reads

- **Geometry.** `<sharedgeometry>` and each submesh's `<geometry>`: `vertexcount` and any number of `<vertexbuffer>` elements. Vertex `k` of every buffer is vertex `k` of the geometry, so positions and normals in one buffer and texture coordinates in another work (the usual layout). `<position>`, `<normal>`, the first `<texcoord>`. The `vertexcount` must equal the number of `<vertex>` elements, or the file is refused before anything is allocated.
- **Submeshes.** One part per `<submesh>` that has faces. `usesharedvertices` picks the shared or the own geometry; `<face v1 v2 v3>` are triangles (lists, strips and fans are all written as triangles by the converter); an index past the vertex list skips that triangle. Line and point lists are skipped. The part is named after `<submeshname>` or the material.
- **Materials.** The submesh's `material` name is the material name. When `.material` scripts are given (`ReadOgreXmlFile` reads the ones next to the mesh: the one named like the mesh first, at most 32 files of 4 MB) the first pass's `diffuse`, `emissive` and `specular`'s shininess and the first texture unit's `texture` are read (the script language's `material name : parent { technique { pass { texture_unit { ... } } } }`; names after block keywords, comments and quoted strings are handled).
- Coordinates, winding and texture coordinates **as written** (OGRE is right-handed and Y-up; the engine's Assimp path flips v on top of Assimp's own flip, which is the file's value).

## Not handled (each is a line in `out.notes`)

The skeleton link and `<boneassignments>` (the mesh is imported rigid, at its bind pose), poses and morph animation, levels of detail, vertex colours, tangents, second and later texture coordinate sets, everything in material scripts beyond the first pass colours and first texture (parents, other techniques, shaders, blend modes).

## Not done

- **The editor does not recognise `.mesh.xml` by itself.** Its project panel and drag-and-drop look at the last extension (`.xml`), so only the Import Model dialog (whose file filter lists `*.mesh.xml`) and `ImportModelData` / `ImportModel` take the file. Teaching the editor's `IsModelExt` the two-part extension is not done.
- **Binary `.mesh` and `.skeleton`**: a separate reader (a chunked binary format). Real files exist in Assimp's tree (`fish`, `ninja`, `razor`). Not started.
- **`.skeleton.xml`**: bones, bind pose, animations, weights. Not started; it needs the skin path the other skinned readers use.

## Limits

File 256 MiB; 8,000,000 vertices per geometry; 20,000,000 faces; 100,000 submeshes; 40,000,000 output vertices; the XML reader's caps (2,000,000 elements, nesting 256, 256 attributes per element). Numbers go through `NumParse.hpp` (finite, within 1e15). Material scripts: 4 MB each and 2,000,000 words.

## Tests and what was checked

- `ogre_reader`: **synthetic** mesh (shared geometry over two vertex buffers with normals and uv, a second submesh with its own geometry and no uv, `<submeshnames>`, a skeleton link and bone assignments) and a synthetic material script (parent, two passes, texture unit with a name): triangles, area 10 (by hand), uv and normal numbers, names, material colours, shininess and texture path, notes, refusals (empty, wrong root, no submeshes, a wrong `vertexcount`, a bad number, shared vertices that do not exist, an index past the vertices, line lists, a DTD entity) and **every truncation**, with `\n` and `\r\n`.
- `batchd_hostile` (OGRE section): counts at, past and far past the caps with tiny text, `nan`, `inf`, `1e999` in positions, texture coordinates and indices, an entity bomb, 5,000 levels of nesting, 100,000 empty submeshes, a 3 MB number, 50,000 submeshes on shared vertices, a valid 60,000-triangle mesh, and material scripts of 100,000 `{`, 100,000 `}`, 20,000 `material` blocks, a 2 MB name; then 3,000 mutations of the file (30,000 in the sanitizer run).

### Real files

Assimp's tree has `fish`, `ninja`, `razor` (`.mesh.xml` and binary `.mesh`, `OgreSDK`) and `TheThing/Mesh.mesh.xml`. **All 4 `.mesh.xml` files read and equal Assimp on triangles, mesh count, surface area and bounding box** (fish 815 triangles area 34.8947; ninja 1,008, 2 meshes, 25,219.6177; razor 468, 29,764.2994; TheThing 2,656, 1.3468), and each gets the texture its `.material` names (`fish.jpg`, `Wrinkles.jpg`, ...). Not compared: skeletons and animation (Assimp reads them, ours does not).

Unverified: a mesh with several uv sets, 32-bit-index meshes, strips and fans on a real file, files from exporters other than the SDK converter and the Blender exporter, a run on real Windows.

## Provenance (what this file was written from)

`OgreXmlReader.cpp` was written from scratch for this project from the public description of the OGRE mesh XML format (the element and attribute names of the `OgreXMLConverter` output as documented in the OGRE manual) and of the OGRE material script language, and from reading the four sample `.mesh.xml` files and the `.material` files of Assimp's `test/models*` as data (never the Assimp source). Assimp was used only as a **black box** (counts, area, box and texture names on the sample files). No code, comment, table or test from any other importer was read, copied or ported.
