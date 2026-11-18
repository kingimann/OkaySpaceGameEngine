# `.xml` model files: picked by their root element

Status: **side by side, off by default** (`OKAY_OWN_XML`, slot 45 of the import table in `ModelImport.cpp`; added 2026-11-16 in the
"Assimp gap" batch, `docs/own_code_roadmap.md`, section "What still needs Assimp"). Nothing was flipped.

## What it is

Assimp claims the extension `.xml` (the Irrlicht scene and mesh importers list it, and its format detection also reads COLLADA
and XGL files saved as `.xml`). Our readers for those formats existed, but only under their own extensions (`.irr`, `.irrmesh`,
`.dae`, `.xgl`, `.mesh.xml`), so an `.xml` model went to Assimp. Slot 45 is not a new parser: it looks at the root element and
hands the file to the reader we already have.

| Root element (namespace prefix ignored) | Read by | Its page |
|---|---|---|
| `irr_scene` | `ReadIrrSceneFile` (the meshes it names go through the import seam) | `docs/own_irr_reader.md` |
| `mesh` whose first child element is `<sharedgeometry>`, `<submeshes>` or `<submesh>` | `ReadOgreXmlFile` | `docs/own_ogre_reader.md` |
| any other `mesh` | `ReadIrrMeshFile` | `docs/own_irrmesh_reader.md` |
| `COLLADA` | `ReadColladaFile` | `docs/model_corpus.md` |
| `WORLD` | `ReadXglFile` | `docs/own_xgl_reader.md` |
| anything else | refused: `XML: the root element <x> is not one this engine reads (irr_scene, mesh, COLLADA, WORLD)` | |

The first 8 KiB are looked at (the root element, and for `<mesh>` its first child element); UTF-16 text (a byte order mark or zero bytes) is looked at with its zero bytes dropped. The XML
declaration, comments and a DOCTYPE before the root are skipped. A `.mesh.xml` file still goes to slot 23 (the OGRE switch
`OKAY_OWN_OGRE`), not here. Code: `ReadXmlByRoot` in `engine/src/Render/ModelImport.cpp`.

## Tests and what was checked

- `own_dispatch_gap`: an `.xml` of each kind (COLLADA, XGL, OGRE XML, Irrlicht mesh, an Irrlicht scene that names a mesh) reads
  to the same model as that format's own reader; a UTF-16 COLLADA file; an unknown root (`<skeleton>` after a comment) is
  refused with its name; a binary file named `.xml` is refused; `OKAY_OWN_XML=1` / `0`; off by default; the shipped sample
  `examples/own_readers/cube_irrmesh.xml` gives a cube of 12 triangles and area 6.
- The readers behind it keep their own fixture, hostile and mutation tests.

### Real files

The 9 `.xml` files of Assimp's test folder (not counting `.mesh.xml`). **Against Assimp v5.4.3 (`bench_model_readers --compare`)**:

| File | Ours | Assimp | |
|---|---|---|---|
| `Collada/AsXML.xml` | 1 part, 12 triangles | the same, same box | equal |
| `IRRMesh/testFormatDetection.xml` and its UTF-16 copy | 5 parts, 60 triangles, 180 vertices | 5, 60, 120 | the vertex count is the Irrlicht mesh reader's known difference (Assimp joins vertices); box equal |
| `XGL/sample_official_asxml.xml` | 2 parts, 24 triangles | 2, 24 | box differs, exactly as for the same file as `.xgl` (`docs/own_xgl_reader.md`, object positions) |
| `IRR/skybox.xml` and its UTF-16 copy | refused: the scene has no geometry | refused | both refuse |
| `Ogre/OgreSDK/fish.skeleton.xml`, `ninja.skeleton.xml` | refused: root `<skeleton>` | refused | both refuse (a skeleton alone is not a model) |
| `invalid/emptyIrrMesh.xml` | refused | refused | both refuse |

All in the `model_corpus` manifest (reader `xml`). Speed and memory (`docs/own_readers_speed.md`, row `xml`): 0.70x Assimp's read time and 0.73x its largest peak heap over the 4
files both read; the two Irrlicht mesh files are 0.99x and 1.01x the time, which is the Irrlicht mesh reader's own speed.

Unverified: `.xml` files from tools other than those of the test folder; a run on real Windows.

## Provenance

The root-element switch was written for this project from the element names of the formats' own descriptions (the same ones the
readers above were written from). No code from any other importer was read.
