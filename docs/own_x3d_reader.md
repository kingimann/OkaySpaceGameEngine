# Our own X3D reader (`.x3d`)

Status: **side by side, off by default** (`OKAY_OWN_X3D`, same rules as `docs/own_3mf_reader.md`, whose XML reader it uses). Assimp (when built) is still the only other reader of `.x3d`.

## What it is

Written from the public X3D abstract and XML-encoding specifications (ISO/IEC 19775 / 19776), with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/X3dReader.cpp`, `engine/include/okay/Render/X3dReader.hpp` | `okay::ReadX3dBuffer` / `okay::ReadX3dFile` |
| `engine/src/Render/XmlLite.hpp`, `ReaderCommon.hpp`, `PolygonTriangulate.hpp` | Shared XML reader, number parsing and normals (see `docs/own_3mf_reader.md`), polygon ear clipping (shared with OBJ, PLY, COLLADA) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 13 of the batch-B table |
| `tests/test_x3d_reader.cpp` | Hand-computed checks on synthetic files and the seam (`x3d_reader`) |
| `tests/test_batchb_hostile.cpp` | Hostile input and mutation fuzz (`batchb_hostile`) |

## What it reads

- **Scene graph**: `Group`, `Transform` (`translation`, `rotation` axis-angle, `scale`, `scaleOrientation`, `center`, composed `T C R SR S SR^-1 C^-1`), `Collision`, `Anchor`, `StaticGroup`, `Billboard` (as a static group, noted), `Switch` (`whichChoice`, default -1 shows nothing, noted), `LOD` (the first, finest level), with `DEF` / `USE` reuse. A `USE` that points back into its own parent is skipped (noted); the number of nodes visited is capped so an exponential `USE` expansion stops. Every transform is baked into the vertices: one node and one part per `Shape` (named by its `DEF`).
- **Geometry**: `IndexedFaceSet` (`coordIndex` polygons, concave ones ear clipped; `ccw`; `creaseAngle` as the crease angle, default 0 = flat; `Normal` with `normalIndex` and `normalPerVertex`; `TextureCoordinate` with `texCoordIndex`), `IndexedTriangleSet`, `TriangleSet`, the primitives `Box`, `Sphere`, `Cylinder` (`side`, `top`, `bottom`) and `Cone`, tessellated with 24 segments (a sphere has 12 bands), and `Extrusion` (cross-section, spine, per-point `scale` and `orientation`, caps, `ccw`, `creaseAngle`; the spine-aligned planes are built as the specification describes, a straight spine turns the Y axis onto it, a closed spine has no caps; no UVs). UV `v` is flipped like the other bottom-up formats. Without `Normal`, normals come from `creaseAngle`. A mirroring transform flips the winding so faces keep their side; normals are transformed by the inverse transpose.
- **Appearance**: `Material` (`diffuseColor` with `1 - transparency` as alpha, `emissiveColor`, `shininess * 128` as the Phong exponent) and `ImageTexture` (the first `url`, resolved next to the file; `http` and `data:` urls are not read). The material is shared by `DEF` / `USE`.
- **Axes**: X3D is Y-up like the engine, so no axis change. Units are not applied (X3D has a `unit` statement for angle and length that is ignored).

## Not handled (each is also named in `notes`)

Nodes not read are counted by name: `ElevationGrid`, `IndexedLineSet`, `PointSet`, `Text`, strip / fan / quad sets, NURBS, geospatial, `Inline`, `ProtoDeclare` / `ProtoInstance`, scripts, sensors, interpolators, `ROUTE`s (no animation is read), lights, `Viewpoint`, `NavigationInfo`, `Background`, ... A file whose only geometry is of such a kind fails with a message that names what it has. Also: per-vertex and per-face `Color`, `TextureTransform`, texture nodes other than `ImageTexture`, `TwoSidedMaterial`, `PhysicalMaterial`. Only the XML encoding is read: the classic VRML encoding (`.x3dv`, `.wrl`) and the binary encoding (`.x3db`) are not. The XML tree is limited to 3,000,000 elements, the scene graph to 1,000,000 visited nodes and 128 levels, index lists to 60,000,000 values.

## Tests and what was checked

- `x3d_reader` (`tests/test_x3d_reader.cpp`): **synthetic** documents: `IndexedFaceSet` quads, a concave L-shaped polygon (area 3), faces with an out-of-range or missing closing index, `ccw`, materials (alpha, emission, shininess), explicit normals and texture coordinates (the `v` flip), per-face normals, bad normal indices, the crease angle (a 90 degree fold stays flat at 0 and at 57 degrees, is welded at 100), `IndexedTriangleSet` and `TriangleSet`, a `Transform` computed by hand (translation, scale, a 90 degree rotation, centre, a mirror), `DEF` / `USE` (also of a `Transform`), a `USE` of its own parent, an undefined `USE`, `Switch`, `Box` (volume 48), `Sphere`, `Cylinder` and `Cone` (volumes and areas within the tessellation), `Extrusion` (the default square swept along +Y, volume 4; a clockwise cross-section with `ccw="false"`; a straight spine along x; per-point scale; a bent and a closed spine), the notes for nodes not read, a texture path, every refusal (no geometry, bad index, non-multiple of three, `1e999`, a two-number translation, DTD entities), a decimal-comma locale, and the seam.
- `batchb_hostile` (X3D section): every truncation, 20,000-deep and 200-deep groups, billion-laughs, an exponential `USE` expansion (each level uses the previous one twice, 40 levels: stops at the node cap), a shape reused 3,000 times, a `USE` of the group it sits in, bad numbers, bad and huge indices, a 100,000-corner polygon, a 250-corner concave star (ear clipping bound), 20,000 faces on three points, a vertex with 5,000 incident triangles at different angles (crease clustering, time-checked), singular, huge and negative transforms, absurd primitive and extrusion sizes (a 6,000,000-point extrusion is refused), then the mutation fuzz. Sanitizers: clean.

### Real files

Assimp's tree has 4 X3D files (three in `models/X3D`, one in `models-nonbsd/X3D`). **Ours reads 3; one fails by design**, and Assimp reads all 4. None of the 3 agrees with Assimp on every count, and each difference was checked against the file:

- `IndexedLineSet.x3d`: only a line set. Ours fails ("the file has no triangle geometry (it has only: IndexedLineSet ...)"), as the OBJ and PLY readers do for files with no faces; Assimp returns a mesh with 0 triangles.
- `ChevyTahoe.x3d` (a real model, 17 shapes, 8 textures, the wheel is a `Transform` `USE`d three more times): ours 1298 triangles in 20 parts. **An independent count from the file's own index lists** (a script that walks `DEF` / `USE` and sums `n - 2` per face) gives 1298; Assimp gives 1234, one wheel (64 triangles) fewer. Bounding box equal.
- `ComputerKeyboard.x3d` (an `Extrusion` for the base and two key shapes, one `Transform` per key, 98 keys use a `USE`): ours 100 shapes and 2020 triangles; the same independent count (cross-section, spine and caps worked out per shape from the file) gives 100 shapes and 2020. Assimp returns 4 meshes and 100 triangles (it does not instance the `USE`d keys) and a different box.
- `HelloX3dTrademark.x3d`: a `Sphere`. Ours is tessellated with 24 x 12 segments (528 triangles, surface area 12.39), Assimp's is finer (1280, 12.51); ours also gives the sphere UVs, Assimp's mesh has none. The `Text`, interpolators and `ROUTE`s are named in the notes.

Not verified on real files: `Switch` and `LOD`, normals and texture coordinates with index arrays other than the Chevy ones, `IndexedTriangleSet`, the primitives other than the sphere, `ImageTexture` paths beyond the Chevy file's, large files.
