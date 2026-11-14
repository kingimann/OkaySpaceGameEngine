# Our own Irrlicht scene reader (`.irr`)

Status: **side by side, off by default** (`OKAY_OWN_IRR`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of Irrlicht scenes. Nothing was flipped. This completes the Irrlicht pair started by `docs/own_irrmesh_reader.md` (the mesh half).

## What it is

`.irr` is the scene file of the Irrlicht engine, written by the irrEdit scene editor and by `ISceneManager::saveScene`: an XML file with `<irr_scene>`, nested `<node type="...">` elements that carry typed attributes (`Position`, `Rotation`, `Scale`, `Mesh`, `Size`, `Radius`, ...), a `<materials>` list and `<animators>`.

| File | What |
|---|---|
| `engine/src/Render/IrrSceneReader.cpp` | `okay::ReadIrrSceneBuffer` / `okay::ReadIrrSceneFile` (declared in `ModelData.hpp`, with the `IrrMeshLoader` type) |
| `engine/src/Render/XmlLite.hpp`, `TextReaderUtil.hpp`, `ReaderCommon.hpp` | Shared strict XML reader, UTF-16 conversion, number parsing |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 25 (extension `.irr`); it also gives the reader its **mesh loader** (below) |
| `tests/test_irr_reader.cpp`, `tests/batchd_fixtures.hpp` | Hand-computed checks on synthetic files with a test loader (`irr_reader`) |
| `tests/test_batchd_hostile.cpp` | Hostile input and mutation fuzz (`batchd_hostile`) |

## What it reads

- **Text.** UTF-8 and UTF-16 with a byte order mark (irrEdit writes UTF-16 on Windows).
- **Nodes.** Every `<node>` becomes a node of ours (the tree is walked with an explicit work list, 256 levels), with `Name`, `Position`, `Rotation` (Euler degrees, `R = Rz * Ry * Rx`, positive angles counter-clockwise: the convention was found by running Assimp on small synthetic scenes, **not** checked against Irrlicht itself) and `Scale`. A bad vector is replaced by the identity and counted.
- **`cube` and `sphere` nodes** become meshes of their own: a cube of edge `Size` centred on the node (outward counter-clockwise faces, flat normals, uv 0..1 per face); a UV sphere of `Radius` with `PolyCountX` segments and `PolyCountY` rings (clamped to 3..512 and 2..512). The node's first `<materials>` entry gives the material (`Diffuse`, `Emissive` as `aarrggbb`, `Shininess`, `Texture1`, and `Texture2` as the normal map for normal and parallax map types; paths joined to the scene's folder).
- **`mesh` and `animatedMesh` nodes** name another model file in `Mesh`. The reader does not read model files itself: it calls the **loader** its caller supplies. The import seam supplies one that runs the file through `ImportModelData` with the default choice of backend (so an `.irrmesh` or `.x` goes to our reader when it is switched on, else to Assimp when the build has it, and `.obj` always to ours), one level deep: a scene that names a scene gets that node refused and a note. The loaded model's nodes go under the scene's node, its parts keep their skins; a file used by several nodes is loaded once and its materials are shared.
- All other node types (`camera`, `light`, `skyBox`, `terrain`, `billboard`, `empty`, ...) stay as empty nodes, so the hierarchy is kept; the kinds are listed in a note.

## Not handled (each is a line in `out.notes`)

Animators (rotation, fly circle, fly straight, follow spline, ...: Assimp makes clips from them, ours does not), the animation clips of the referenced meshes (dropped), the node's own material list on a mesh node (the mesh file's materials are used), material types, flags and wrap modes, cameras, lights, skyboxes, terrains. A scene with no geometry (only cameras, lights, a skybox) is refused. `.xml` files in this format (`skybox.xml` in Assimp's tree) are not routed here; the extension is too common.

## Differences from Assimp that are on purpose

Assimp's cube reaches `Size / sqrt(3)` from the centre (a half-edge of 5.774 for `Size 10`; Irrlicht's cube of edge 10 reaches 5), and its sphere has half the radius (`Radius 5` gives 2.5) and is an icosphere of 1,280 triangles. Both are Assimp's own scaling of its built-in meshes; ours follow the parameters as Irrlicht documents them (`Size` is the edge, `Radius` the radius) and the sphere is a 16 x 16 UV sphere of 480 triangles.

## Limits

File 256 MiB; 1,000,000 nodes; 40,000,000 output vertices (counting every instance of a loaded mesh); 1,000 different mesh files per scene; nesting 256; the XML reader's caps.

## Tests and what was checked

- `irr_reader`: **synthetic** scene (a camera, a cube with position, a 90 degree rotation, a green material with texture and an animator, a group holding a sphere and a mesh node) with a test loader: nodes and parents, positions, the rotation quaternion, cube vertices (all at +-1), outward winding, sphere radius, normals and area, the material, the loaded triangle seen through four nested nodes, notes; with `\n`, `\r\n` and UTF-16; no loader; a loader that fails; the same file used twice (loaded once, material shared); a bad vector; refusals (empty, wrong root, no node, no geometry, a DTD entity, absurd poly counts) and **every truncation**.
- `own_dispatch_d`: through the seam: a scene naming an `.irrmesh` (ours), an `.obj`, and a scene naming a scene (refused, the scene still reads).
- `batchd_hostile` (Irrlicht section): `nan`, `inf`, `1e999`, hex floats and overflowing numbers in positions, rotations, scales, sizes, radii, poly counts, colours; 5,000 nested nodes, 3,000 cubes, 1,200 mesh nodes whose files fail, 60,000 mesh nodes that use one mesh (the output cap), a 3 MB mesh path, an entity bomb, loaders that return nothing or wrong indices; then 3,000 mutations of the file and 750 of its UTF-16 form (30,000 and 7,500 in the sanitizer run).

### Real files

Assimp's tree has 21 `.irr` files in `models/IRR` (UTF-8 and UTF-16LE) and `skybox.xml` (UTF-8 and UTF-16LE, extension `.xml`, no geometry). **All 21 `.irr` files read in ours and in Assimp** (Assimp built with the X, IRRMESH and OBJ importers as well, so the meshes the scenes name load there too). **The 9 files that only place referenced meshes (`dwarf.x`, `cellar.irrmesh`, `spider.obj`: `EpisodeI_ANewDwarf`, `animMesh`, `dawfInCellar_ChildOfCellar`, `dawfInCellar_SameHierarchy`, `instancing`, each in both encodings except `instancing`) agree with Assimp on mesh count, triangle count, surface area and bounding box** (`EpisodeI_ANewDwarf.irr`: 9,752 triangles, area 369,750.5798 ours and 369,750.5807 Assimp, same box; `dawfInCellar_ChildOfCellar.irr`: 2,168 triangles, 575,948.295 both; `instancing.irr`: 9,480 triangles, 42,214.7085 against 42,214.7084). The other 12 (`box`, `sphere`, `multipleAnimators`, `scenegraphAnim`, `scenegraphAnimMod`, `EpisodeII_TheDwarfesStrikeBack`, in both encodings) have cube or sphere nodes and differ by the scaling above (for example `EpisodeII_TheDwarfesStrikeBack.irr`: area 4,071.09 ours against 4,871.09; same triangle count 7,632 and same mesh count). Both UTF-16 files of each pair give the same result as their UTF-8 twins. Assimp makes animation clips (1 to 4 per file) from the animators; ours has none. The `dwarf.x` files are read through our own `.x` reader (`OKAY_OWN_X=1`) and the `.irrmesh` through ours (`OKAY_OWN_IRRMESH=1`).

Unverified: the rotation convention against Irrlicht, scenes from the engine's `saveScene` with other node types, `lightmap` materials on a mesh node, a run on real Windows.

## Provenance (what this file was written from)

`IrrSceneReader.cpp` was written from scratch for this project from the public description of the Irrlicht scene format (the node and attribute names that irrEdit and `ISceneManager::saveScene` write, as documented in the engine's API docs) and from reading the sample scenes of Assimp's `test/models/IRR` as data (never the Assimp source). Assimp was used only as a **black box**: run on the sample scenes and on small synthetic scenes made for this purpose, its output (node matrices, counts, area, box) showed the rotation order and signs and how the built-in cube and sphere are sized. No code, comment, table or test from any other importer was read, copied or ported.
