# Our own OGRE mesh readers (`.mesh.xml` and binary `.mesh`)

Status: **side by side, off by default**: the XML form behind `OKAY_OWN_OGRE` (slot 23), the binary `.mesh` behind `OKAY_OWN_OGREBIN` (slot 43, added 2026-11-16 in the "Assimp gap" batch, `docs/own_code_roadmap.md`, section "What still needs Assimp"); same rules as `docs/own_3mf_reader.md`. Assimp (when built) is still the only other reader of OGRE meshes. Nothing was flipped. **Skeletons are not read** in either form (`.skeleton`, `.skeleton.xml`; see "Not done").

## What it is

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

## What it reads

- **Geometry.** `<sharedgeometry>` and each submesh's `<geometry>`: `vertexcount` and any number of `<vertexbuffer>` elements. Vertex `k` of every buffer is vertex `k` of the geometry, so positions and normals in one buffer and texture coordinates in another work (the usual layout). `<position>`, `<normal>`, the first `<texcoord>`. The `vertexcount` must equal the number of `<vertex>` elements, or the file is refused before anything is allocated.
- **Submeshes.** One part per `<submesh>` that has faces. `usesharedvertices` picks the shared or the own geometry; `operationtype` picks how the `<face>` elements are read: `triangle_list` (default) needs `v1 v2 v3` on every face; `triangle_strip` and `triangle_fan` read an index sequence (the first face gives its indices, every later face adds its last one, which is `v3` when a face carries all three and `v1` when it carries only the new index) and unroll it: a strip alternates its winding (triangle i is `s[i] s[i+1] s[i+2]` for even i and `s[i+1] s[i] s[i+2]` for odd i), a fan shares the first vertex (`s[0] s[i+1] s[i+2]`); repeated indices make degenerate triangles, which are dropped as before. (An earlier version read every face as an independent triangle whatever the operation type, which flipped every second triangle of a strip written as sliding triples; `tests/test_batchd_budget.cpp` now checks strips and fans against hand-computed triangles in both ways of writing them.) An index past the vertex list skips that triangle. Line and point lists are skipped. The part is named after `<submeshname>` or the material.
- **Materials.** The submesh's `material` name is the material name. When `.material` scripts are given (`ReadOgreXmlFile` reads the ones next to the mesh: the one named like the mesh first, at most 32 files of 4 MB) the first pass's `diffuse`, `emissive` and `specular`'s shininess and the first texture unit's `texture` are read (the script language's `material name : parent { technique { pass { texture_unit { ... } } } }`; names after block keywords, comments and quoted strings are handled).
- Coordinates, winding and texture coordinates **as written** (OGRE is right-handed and Y-up; the engine's Assimp path flips v on top of Assimp's own flip, which is the file's value).

## The binary `.mesh`

The file is a 16-bit header id (`0x1000`) and a version line (`[MeshSerializer_v1.8]` in the three real files), then chunks: a 16-bit id and a 32-bit length. **The lengths are not reliable for skipping**: in the real files the length of the mesh chunk is 117 bytes short of what it holds (the serializer leaves text fields out of some of its sums; the skeleton files show the same for bone names), so the reader reads the chunks it knows field by field in the order the format writes them, and treats a chunk id that cannot be a child of the chunk being read as the end of that chunk. Only chunks it does not read (edge lists, levels of detail, poses, animations, extremes) are skipped by their length; a length that runs past the end stops the reading there with a note, and the submeshes before it are kept.

Read: the mesh chunk (`0x3000`); shared geometry (`0x5000`) and each submesh's own geometry: vertex count, the vertex declaration (`0x5100`, elements `0x5110`: buffer, type, meaning, offset, index) and the vertex buffers (`0x5200` with data `0x5210`); positions, normals and the first texture coordinate set when they are 32-bit floats; submeshes (`0x4000`: material name, shared flag, 16- or 32-bit indices, operation `0x4010`: list, strip or fan, as in the XML form; lines and points are skipped); the submesh name table (`0xA000` / `0xA100`); the skeleton link (`0x6000`, reported, not read). Bone assignments (`0x4100`, `0x7000`) are stepped over. Little-endian files only: a big-endian file (header bytes `10 00`) is refused with that reason. Versions other than 1.8, 1.10 and 1.100 are read with a note that says they were not checked against a real file.

Before anything is allocated from a vertex count, the positions must sit in a vertex buffer whose data lies inside the file (12 bytes per vertex), so a small file cannot ask for big arrays; index lists must lie inside the file too. A position, normal or texture coordinate that is not a finite number refuses the file.

## Not handled (each is a line in `out.notes`)

The skeleton link and `<boneassignments>` (the mesh is imported rigid, at its bind pose), poses and morph animation, levels of detail, vertex colours, tangents, second and later texture coordinate sets, everything in material scripts beyond the first pass colours and first texture (parents, other techniques, shaders, blend modes).

## Not done

- **The editor does not recognise `.mesh.xml` by itself.** Its project panel and drag-and-drop look at the last extension (`.xml`), so only the Import Model dialog (whose file filter lists `*.mesh.xml`) and `ImportModelData` / `ImportModel` take the file. Teaching the editor's `IsModelExt` the two-part extension is not done.
- **Binary `.mesh`: done** (2026-11-16, above). Not read in it: vertex elements that are not 32-bit floats (16-bit texture coordinates, packed normals), big-endian files.
- **`.skeleton` and `.skeleton.xml`**: bones, bind pose, animations, weights. Not started in either form; it needs the skin path the other skinned readers use. Assimp reads them (fish has 4 bones and a clip, ninja 21 bones and 20 clips), so a skinned OGRE mesh still imports rigid with ours.

## Limits

File 256 MiB; 8,000,000 vertices per geometry; 20,000,000 faces; 100,000 submeshes; 40,000,000 output vertices; the XML reader's caps (2,000,000 elements, nesting 256, 256 attributes per element). Numbers go through `NumParse.hpp` (finite, within 1e15). Material scripts: 4 MB each and 2,000,000 words.

## Tests and what was checked

- `ogre_reader`: **synthetic** mesh (shared geometry over two vertex buffers with normals and uv, a second submesh with its own geometry and no uv, `<submeshnames>`, a skeleton link and bone assignments) and a synthetic material script (parent, two passes, texture unit with a name): triangles, area 10 (by hand), uv and normal numbers, names, material colours, shininess and texture path, notes, refusals (empty, wrong root, no submeshes, a wrong `vertexcount`, a bad number, shared vertices that do not exist, an index past the vertices, line lists, a DTD entity) and **every truncation**, with `\n` and `\r\n`.
- `batchd_hostile` (OGRE section): counts at, past and far past the caps with tiny text, `nan`, `inf`, `1e999` in positions, texture coordinates and indices, an entity bomb, 5,000 levels of nesting, 100,000 empty submeshes, a 3 MB number, 50,000 submeshes on shared vertices, a valid 60,000-triangle mesh, and material scripts of 100,000 `{`, 100,000 `}`, 20,000 `material` blocks, a 2 MB name; then 3,000 mutations of the file (30,000 in the sanitizer run).

### Real files

Assimp's tree has `fish`, `ninja`, `razor` (`.mesh.xml` and binary `.mesh`, `OgreSDK`) and `TheThing/Mesh.mesh.xml`. **All 4 `.mesh.xml` files read and equal Assimp on triangles, mesh count, surface area and bounding box** (fish 815 triangles area 34.8947; ninja 1,008, 2 meshes, 25,219.6177; razor 468, 29,764.2994; TheThing 2,656, 1.3468), and each gets the texture its `.material` names (`fish.jpg`, `Wrinkles.jpg`, ...). Not compared: skeletons and animation (Assimp reads them, ours does not).

**Speed of the XML reader (2026-11-16, branch `claude2/own-readers-gap-2`).** The XML form is now parsed by `xml::ParseFlat`
(`XmlLite.hpp`): the same accept / refuse rules and messages as `xml::Parse`, but element and attribute names and values are
offsets into the file text instead of a `std::string` each, and character data is checked, not kept. Same output (digest
equal on all four files); 0.42x Assimp's read time and 0.54x its largest peak heap over the files both read (was 1.03x and
1.04x); every file faster and lighter (`docs/own_readers_speed.md`, third round).

**Binary `.mesh` (2026-11-16).** All 3 binary meshes read and give **exactly the parts, triangles and box of their XML twins**: razor's model is bit for bit the same as the XML reader's (same digest); fish and ninja differ only in the last digits of positions (the XML text was rounded to about 6 significant digits; the binary floats are the exporter's values, at most 4.6e-5 apart) and in the skeleton name in the note. Against Assimp: fish 815 / 815 triangles, 530 / 530 vertices, ninja 1,008 / 1,008 and 842 / 842, razor 468 / 468 and 588 / 588, boxes equal; ninja has 1 material to Assimp's 2 (ours shares a material between submeshes with the same name, as the XML reader does); Assimp also gives the skeleton (nodes, bones, clips), ours does not. Speed and memory (`docs/own_readers_speed.md`, row `mesh`): 0.12x Assimp's read time and 0.25x its largest peak heap; every file faster and lighter.

Unverified: a mesh with several uv sets, 32-bit-index meshes, strips and fans on a real exporter file (the two ways of writing the face elements are an assumption from the format description, checked on synthetic data only), files from exporters other than the SDK converter and the Blender exporter, a run on real Windows.

## Provenance (what this file was written from)

The binary part was written from the public description of the OGRE binary mesh file (the chunk ids and field order of the mesh serializer as the OGRE manual and its file-format notes describe them) and checked byte by byte against the three binary `.mesh` files of Assimp's test folder with a small dump script written for this work; the vertex element type and meaning numbers were checked on those files (float3 position, float3 normal, float2 texture coordinates). No OGRE or Assimp source was read.

`OgreXmlReader.cpp` was written from scratch for this project from the public description of the OGRE mesh XML format (the element and attribute names of the `OgreXMLConverter` output as documented in the OGRE manual) and of the OGRE material script language, and from reading the four sample `.mesh.xml` files and the `.material` files of Assimp's `test/models*` as data (never the Assimp source). Assimp was used only as a **black box** (counts, area, box and texture names on the sample files). No code, comment, table or test from any other importer was read, copied or ported.
