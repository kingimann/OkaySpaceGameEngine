# Our own OGRE mesh readers (`.mesh.xml` and binary `.mesh`)

Status: **side by side, off by default**: the XML form behind `OKAY_OWN_OGRE` (slot 23), the binary `.mesh` behind `OKAY_OWN_OGREBIN` (slot 43, added 2026-11-16 in the "Assimp gap" batch, `docs/own_code_roadmap.md`, section "What still needs Assimp"); same rules as `docs/own_3mf_reader.md`. Assimp (when built) is still the only other reader of OGRE meshes. Nothing was flipped. **Skeletons: the XML form (`.skeleton.xml`) is read since 2026-11-20 (bones, bind pose, animations, skin weights; see "Skeletons"); the binary `.skeleton` is not. The binary `.mesh` is skinned against the XML skeleton since 2026-11-20 (see "Skeletons").**

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

Read: the mesh chunk (`0x3000`); shared geometry (`0x5000`) and each submesh's own geometry: vertex count, the vertex declaration (`0x5100`, elements `0x5110`: buffer, type, meaning, offset, index) and the vertex buffers (`0x5200` with data `0x5210`); positions, normals and the first texture coordinate set when they are 32-bit floats; submeshes (`0x4000`: material name, shared flag, 16- or 32-bit indices, operation `0x4010`: list, strip or fan, as in the XML form; lines and points are skipped); the submesh name table (`0xA000` / `0xA100`); the skeleton link (`0x6000`, reported, not read). Bone assignments (`0x4100` per submesh, `0x7000` for the shared geometry; each record is vertex u32, bone u16, weight f32, which is the 10 bytes this reader always stepped over) are read when an XML skeleton is given (see "Skeletons"), otherwise counted and left. Little-endian files only: a big-endian file (header bytes `10 00`) is refused with that reason. Versions other than 1.8, 1.10 and 1.100 are read with a note that says they were not checked against a real file.

Before anything is allocated from a vertex count, the positions must sit in a vertex buffer whose data lies inside the file (12 bytes per vertex), so a small file cannot ask for big arrays; index lists must lie inside the file too. A position, normal or texture coordinate that is not a finite number refuses the file.

## Not handled (each is a line in `out.notes`)

The binary mesh without an XML skeleton (it is imported rigid, at its bind pose, with a note), poses and morph animation, levels of detail, vertex colours, tangents, second and later texture coordinate sets, everything in material scripts beyond the first pass colours and first texture (parents, other techniques, shaders, blend modes).

## Not done

- **The editor does not recognise `.mesh.xml` by itself.** Its project panel and drag-and-drop look at the last extension (`.xml`), so only the Import Model dialog (whose file filter lists `*.mesh.xml`) and `ImportModelData` / `ImportModel` take the file. Teaching the editor's `IsModelExt` the two-part extension is not done.
- **Binary `.mesh`: done** (2026-11-16, above). Not read in it: vertex elements that are not 32-bit floats (16-bit texture coordinates, packed normals), big-endian files.
- **Binary `.skeleton`**: not started: its chunk layout is not recorded in this document, so it is not citable here. A binary `.mesh` is skinned only with the XML skeleton (`name.skeleton.xml`, as the OGRE XML converter writes it); with only a binary `.skeleton` it stays rigid with a note.

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

## Memory (2026-11-16)

The reader held 1.03x to 1.14x Assimp's peak heap on three of the four corpus files. Two changes, output unchanged
(`bench_model_readers --digest` and `--import-digest` identical on every corpus file):

- The XML reader (`XmlLite.hpp`) has a `keepText` switch (on by default, so the 3MF, AMF, X3D, XGL and Irrlicht readers are
  unchanged). The OGRE reader turns it off: character data is still checked (the limits, entity references) but not stored.
  An OGRE mesh is all elements and attributes, and the white space of a pretty-printed file gave every parent element its own
  text string on the heap.
- The file's bytes are freed once the document is parsed, before the meshes are built.

Peak heap of one read, ours / Assimp (MB): `fish` 1.79 -> 1.35 / 1.73, `ninja` 2.47 -> 1.89 / 4.20, `razor` 1.17 -> 0.91 /
1.02, `TheThing/Mesh` 3.49 -> 2.78 / 3.37. Every file is now below Assimp (`docs/own_readers_speed.md`).

Later (the integration of `claude2/own-readers-gap-2`): the reader moved to `xml::ParseFlat`, which stores offsets into the
file text instead of copies, so the file's bytes now stay alive while the meshes are built and the `keepText` switch is no
longer used by OGRE (it stays in `XmlLite.hpp`, default on). That parser keeps less again: `TheThing` 1.82 / 3.37 MB.


## Skeletons (`.skeleton.xml`, 2026-11-20)

Written from the public description of the OGRE skeleton XML (the bone, bone hierarchy and animation elements of the OGRE manual); no code from OGRE or any importer. `ReadOgreSkeletonXml` (`ModelData.hpp`) reads a skeleton alone: the root node `OGRE` and the bones as nodes under it, parent first (children listed before their parents are reordered; a parent loop, a duplicate bone id or name, or an unknown parent is refused), the local bind pose from `<position>`, `<rotation angle>` with an `<axis>` (radians) and `<scale>` (`x y z` or `factor`), and each `<animation>` as a clip with one channel per `<track>`. A keyframe is relative to the bind pose, as OGRE applies it (reset to the bind pose, then translate, rotate, scale): position = bind + translate, rotation = bind * rotate, scale = bind * scale. Tracks for a bone the skeleton does not have are skipped with a note.

`ReadOgreXmlBuffer` takes an optional skeleton text (`ReadOgreXmlFile` loads `<skeletonlink name>.xml` next to the mesh, kept inside that folder). With a skeleton every part is skinned: each `<vertexboneassignment>` (the submesh's own list, or the mesh-level list for shared vertices) maps a bone id to a bone, the four strongest weights per vertex are kept and renormalised (the same bone twice adds up), and the inverse bind of a bone is the inverse of its world bind matrix. A vertex with no usable weight follows the root node through an extra bone `OGRE`. Bad assignments (a vertex or bone that does not exist, a weight that is not positive), dropped fifth weights and flat-scale bones are counted in `out.notes`. A skeleton that cannot be read leaves the mesh rigid and says why. Without a skeleton the output is exactly as before.

Tested by `ogre_skeleton_reader` (`tests/test_ogre_skeleton_reader.cpp`, 1,462 checks): a five-bone fixture whose world bind positions are worked out by hand (Hand at (2,2,0)), inverse binds checked by sending that point back to the bone's origin, a three-key clip (translate, a further 90 degree turn, doubled scale), the four-strongest rule (0.4, 0.3, 0.2, 0.06 over 0.96), the root-bone fallback, bad assignments, refusals, a loop, and every truncation of the file. **Not verified:** a real OGRE skeleton (fish has 4 bones and a clip, ninja 21 bones and 20 clips in Assimp's folder, not in this repository, so the comparison with Assimp's output has not been run); the keyframe composition rule is from the documented behaviour, not checked against a real exporter's file; no Windows run. Binary `.skeleton` (and so skinned binary meshes) is not done.

### Binary `.mesh` skinned with the XML skeleton (2026-11-20)

`ReadOgreBinaryBuffer` takes an optional skeleton text (`ReadOgreBinaryFile` loads `<link name>.xml` next to the mesh, kept inside that folder; the link is the `0x6000` chunk). The `0x4100` (submesh) and `0x7000` (mesh, for submeshes on shared vertices) records, each vertex u32, bone u16, weight f32 (the 10 bytes after the chunk header that the reader already stepped over), go through the same skinning as the XML form: the bone id maps to a bone of the skeleton, the four strongest weights per vertex are kept and renormalised, a vertex with no usable weight follows the root through the bone `OGRE`, bad records are counted in the notes. Written from the public chunk description recorded above; the skeleton and skin code is the XML reader's own (shared, not copied from anywhere). A record cut off by the end of the file refuses the file; weights that are NaN, zero or negative count as bad.

Tested by `ogre_skinbin` (`tests/test_ogre_skinbin_reader.cpp`, 389 checks, off by default like the rest): hand-built binary meshes with the same skeleton, vertices and assignments as the XML skeleton test (same hand-computed weights 0.4, 0.3, 0.2, 0.06 over 0.96, inverse binds, root fallback, two bad records), assignments on the submesh and on the mesh for shared vertices giving the same skin, no skeleton (rigid, old note), an unreadable skeleton, a mesh with no link, every truncation, and the file reader with and without the `.skeleton.xml` next to the mesh. **Not verified:** a real binary `.mesh` with assignments (the three real binary meshes in Assimp's folder are not in this repository, so the skin was not compared with the XML twins or with Assimp), the 16-byte record layout beyond what the stepped-over size implies, bone ids above what the skeleton lists, no Windows run. Binary `.skeleton` is still not read.
