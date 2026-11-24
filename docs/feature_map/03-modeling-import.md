# 3. 3D modeling & import

The built-in mesh editor (Modeling window, Edit Mesh mode, UV Editor), the procedural primitive and prop library, the `.okaymesh` polygon format, and everything that brings an outside 3D model in (glTF, FBX, OBJ, optional Assimp formats) up to making an imported model a playable character.
About 3,400 lines in `Modeling/*`, plus the 4,000-line header `Render/Mesh`, the 2,570-line `Render/ModelImport.cpp` and `Render/Gltf`; the editor side lives in `editor/main.cpp` (DrawModeling, DrawUVEditor, the import and character-model helpers).
No open pull request touches `Modeling/*`. The animation PR #554 (Grok's sessions, in flight) makes small edits to `Render/ModelImport` (see 3.7); open items marked "old map" come from the previous feature map and were not re-checked.

## 3. 3D modeling & import

### 3.1 Edit mode
- **System:** Modeling - Builds and edits 3D models inside the editor: shapes, cuts, bevels, modifiers, UVs, sculpting and painting.
  Home: `engine/include/okay/Modeling/`, `editor/`. Entry: `engine/include/okay/Modeling/PolyMesh.hpp`. Also called: edit mode, mesh editing, modeling window, ModelingEasy, sculpt.
- **What:** Blender-style mesh edit mode (Tab, G/R/S, axis lock, snapping, mirror) on the selected object's Mesh Renderer: Tab to enter, Vertex/Edge/Face selection, G/R/S modal transforms with axis locks, typed values and snapping, soft select, X symmetry and a set of face, edge and vertex tools. The mesh is kept welded while editing.
- **Parts:**
  - 3.1.1 Edit mode, selection and polygon stats - `editor/main.cpp`, `Render/Mesh` - Tab or the Edit Mesh tick, Object/Edit header, keys 1/2/3, click, Shift, Alt-click loop, Ctrl+Alt ring, Ctrl+click shortest path (ShortestVertPath / ShortestFacePath), Grow/Shrink/Linked/Invert, Checker Deselect, Select Boundary Loop, Select Open / Non-Manifold Edges (PolyOps: CheckerVerts / CheckerFaces, RegionOutline, ProblemEdges), stats line
  - 3.1.2 G/R/S modal transforms - `Modeling/EditTransform` - EditTransformSession, AxisLock (X/Y/Z, Shift = plane), NumericInput, Move/Rotate/ScaleMatrix, Ctrl snap, Esc restores exactly; pivot Median / Individual Origins / Active Vertex (EditPivot; Edit Mesh combo, `.` cycles); the move-handle drag uses the same session (soft select, symmetry X/Y/Z)
  - 3.1.3 Soft Select, Symmetry X and vertex snap - `Render/Mesh` - MoveVerticesSoft, SnapToGrid, hold V to snap to vertices
  - 3.1.4 Face tools - `Render/Mesh` - ExtrudeFaces, InsetFaces, SubdivideFaces, PokeFaces, Extrude/Inset Individual, DeleteFaces, FlipNormals (keeps flat or smooth shading), FlipFaces (selected faces only), OrientFacesOutward (Recalc Outside), DuplicateFaces, SeparateFaces
  - 3.1.5 Edge and vertex tools - `Render/Mesh` - CollapseEdges, SplitEdges, ExtrudeEdges, BevelVertices, SmoothVertices (Relax Sel), FlattenVertices, ShrinkFatten, SphereizeVertices, WeldVertices (Merge by Distance)
  - 3.1.6 Polygon mesh core - `Modeling/PolyMesh` - quads and n-gons with per-corner UVs, face colour and material slot; FromMesh/ToMesh, Check report, Euler characteristic
  - 3.1.7 Edit-session undo - `editor/main.cpp` - Ctrl+Z steps through single mesh edits; leaving edit mode makes the whole session one scene undo step (per docs)
- **Code:** `Modeling/EditTransform`, `Modeling/PolyMesh`, `Render/Mesh`, `editor/main.cpp`, `editor/EditorState.*`
- **API:** `EditTransformSession`, `AxisLock`, `NumericInput`, `PolyMesh::FromMesh/ToMesh`, `Mesh::ExtrudeFaces`, `InsetFaces`, `OrientFacesOutward`; keys Tab, G, R, S, X/Y/Z, Ctrl+Z
- **Editor:** Modeling window (View > Editors & Tools > Modeling), the 3D view's Object/Edit header and status line, Edit Mesh section
- **Docs:** `docs/editor/modeling.md`
- **Tests:** `edit_transform`, `meshedit`, `polymesh`, `modeling`, `poly_ops`
- **Status:** Works - four test suites cover the maths and mesh ops; I read the headers but not the editor key handling in main.cpp
- **Open:** tools that rebuild triangles outside the polygon core (the classic Extrude) fall back to one UV per vertex where they touch (per docs); no per-edge Mark Sharp or Edge Split tool (UV seams exist, 3.4; Auto Smooth covers shading by angle); no local / normal transform orientation, 3D cursor pivot, rip, loop-to-region or vertex slide (audit: `docs/status/modeling_tab_audit.md`); Ctrl+click path, pivot modes and the handle-drag symmetry are tested headless but not tried in a running editor
- **Related:** 3.2, 3.4, 3.8, 15.9
- **Words:** edit mode (Tab, G/R/S, axis lock, snapping, mirror), Edit Mesh, Tab, G R S, grab, rotate, scale, extrude, inset, poke, soft select, proportional editing, symmetry X, recalc normals, merge by distance, separate, duplicate faces
- **Seen as:** pressing Tab over the 3D view does nothing; a tool is greyed out; faces look dark or inside out after a weld or mirror; Esc does not put the vertices back; a typed distance goes the wrong way
- **Ask me like:** "make G snap to the grid while Ctrl is held"; "Extrude Faces leaves the collider behind"

### 3.2 Edge and loop tools
- **System:** Modeling
- **What:** Edge-loop modeling on the polygon mesh: select loops and rings, cut new loops, slide, bevel, knife, dissolve, bridge, fill. Each tool keeps the numbering of existing vertices so a selection survives it.
- **Parts:**
  - 3.2.1 Loop and ring selection - `Modeling/PolyOps` - EdgeLoop, EdgeRing, OrderChains, PolyTopology; Alt-click loop, Ctrl+Alt-click ring, Select Loop/Ring buttons
  - 3.2.2 Loop cut and edge slide - `Modeling/PolyOps` - LoopCut (Ctrl+R preview, scroll for more cuts), EdgeSlide (G then G; per-corner UVs follow, seams stay split)
  - 3.2.3 Bevel - `Modeling/PolyOps` - BevelEdges (Ctrl+B), segments and profile; where three or more bevels meet the corner is closed with one face
  - 3.2.4 Knife, split edge, connect - `Modeling/PolyOps` - KnifeCut (K), SplitEdge, SplitFace (Connect Verts)
  - 3.2.5 Dissolve, merge, bridge and fill - `Modeling/PolyOps` - DissolveEdges, DissolveVerts, MergeVerts, BridgeEdgeLoops, BridgeFaces (two face groups: delete, then bridge; Face mode in the editor), FillFaces, GridFill, CompactVerts
  - 3.2.6 Catmull-Clark subdivision - `Modeling/PolyOps` - CatmullClark(pm, levels) on real polygons (used by the Subdivision modifier)
- **Code:** `Modeling/PolyOps`, `Modeling/PolyMesh`, `editor/main.cpp`
- **API:** `EdgeLoop`, `EdgeRing`, `LoopCut`, `EdgeSlide`, `BevelEdges`, `KnifeCut`, `DissolveEdges`, `BridgeEdgeLoops`, `BridgeFaces`, `GridFill`, `CatmullClark`; keys Ctrl+R, Ctrl+B, K
- **Editor:** Modeling window > Edit Mesh > Edge loops group; Ctrl+R / Ctrl+B / K in the 3D view
- **Docs:** `docs/editor/modeling.md`
- **Tests:** `poly_ops`
- **Status:** Works - poly_ops checks counts, closed shells and winding on real geometry; limits listed in Open
- **Open:** Bevel needs edges with a face on both sides; Bridge face groups must each be one patch without holes, not touching, with outlines of the same edge count; Grid Fill needs an even number of rim edges (all per docs); edge slide UVs and Bridge in Face mode are tested in poly_ops but not tried in a running editor
- **Related:** 3.1, 3.3, 3.4
- **Words:** loop cut, edge loop, edge ring, edge slide, bevel, chamfer, knife, dissolve, bridge, fill, grid fill, merge at center, Catmull-Clark, subdivision surface
- **Seen as:** a loop cut previews but does not apply; a bevel leaves a hole at a corner; a bridge says the loops need the same edge count; a slid loop stretches the texture
- **Ask me like:** "add chamfer profile to the bevel"; "knife cut across two faces fails"

### 3.3 Modifiers
- **System:** Modeling
- **What:** Two kinds of modifiers: one-shot operations that change the mesh at once (array, remesh, decimate, boolean, hull, bisect...), and a non-destructive stack on a Mesh Modifiers component whose result is drawn, collided with and exported while the base mesh stays editable.
- **Parts:**
  - 3.3.1 One-shot modifiers - `Render/Mesh` - Array, VoxelRemesh/Remesh, Decimate, Boolean (Union/Subtract/Intersect), ConvexHull, Bisect, Split in two, ShrinkFatten, Wireframe, Displace, CastToCylinder, Stretch
  - 3.3.2 Non-destructive stack - `Modeling/Modifiers`, `Components/MeshModifiers` - Mirror, Array, Subdivision, Solidify, Bevel, Boolean, Decimate, Displace; enable, reorder, Apply, Apply All
  - 3.3.3 Stack evaluation and caching - `Modeling/Modifiers` - EvaluateModifiers, ApplyModifier, ModifierInputHash (base, settings, Boolean cutter mesh and placement), per-modifier error text; stack saved as `modstack` lines
  - 3.3.4 Whole-mesh deformers - `Render/Mesh` - Mirror (cuts at the plane first), Spherify, Twist, Taper, Bend, Solidify, Relax, Shear, Jitter, Symmetrize
- **Code:** `Modeling/Modifiers`, `Components/MeshModifiers`, `Render/Mesh`, `editor/main.cpp`
- **API:** `MeshModifier::Type`, `MeshModifiers::modifiers`, `Refresh()`, `EvaluateModifiers`, `SameModifiers`; Add Component > Rendering > Modifiers (non-destructive)
- **Editor:** Modeling window > Modifiers tab; Inspector Modifiers section with on/off tick, "..." menu (Move Up/Down, Apply, Remove), Show in edit mode
- **Docs:** `docs/editor/modeling.md`
- **Tests:** `modifiers`, `modeling`
- **Status:** Works - modifiers test covers each type, apply, reorder, Boolean targets and save/reload
- **Open:** Subdivision refuses a result above 1.5 million faces; Boolean is voxel based so cost and detail depend on voxel size (per docs); the modifier list is fixed at 8 types (enum is append-only because scenes store the number)
- **Related:** 3.2, 3.5, 6.2
- **Words:** modifier, Mirror, Array, Subdivision Surface, Solidify, Bevel, Boolean, Decimate, Displace, remesh, cutter, Mesh Modifiers
- **Seen as:** a boolean cut does not follow a moved cutter; a modifier shows red text and is skipped; the collider ignores the modified mesh; Apply bakes the wrong modifier
- **Ask me like:** "add a Weld modifier to the stack"; "the Boolean cutter stays visible"

### 3.4 UV tools
- **System:** Modeling
- **What:** Texture coordinates for modeled geometry: quick projections, per-corner UVs that survive editing, seams, a real unwrap (LSCM) with island packing, and a UV Editor window.
- **Parts:**
  - 3.4.1 Quick projections - `Render/Mesh` - ProjectUVBox, ProjectUVPlanar, ProjectUVCylinder, ProjectUVSphere with a tile count
  - 3.4.2 Per-corner UVs and seams - `Modeling/PolyMesh`, `Render/Mesh` - cornerUVs, SplitCornerUVs, seam edges, Mark/Clear Seam, Seams from Angle
  - 3.4.3 LSCM unwrap and Smart Unwrap - `Modeling/UVUnwrap` - Unwrap, MarkSeamsByAngle; cuts closed and multi-border parts open along shortest paths and marks them as seams
  - 3.4.4 Island packing and distortion report - `Modeling/UVUnwrap` - SeamIslands, UVIslands, PackUVIslands (margin, rescale), UnwrapReport, angle distortion
  - 3.4.5 UV Editor window - `editor/main.cpp` - DrawUVEditor: select (point, box, island), move, Rot 90, Flip U/V, scale, Fit, From 3D, Unwrap, Pack; one undo step per edit
- **Code:** `Modeling/UVUnwrap`, `Modeling/PolyMesh`, `Render/Mesh`, `editor/main.cpp`
- **API:** `modeling::Unwrap`, `MarkSeamsByAngle`, `PackUVIslands`, `UnwrapOptions`, `UnwrapReport`, `Mesh::ProjectUVBox`
- **Editor:** View > Editors & Tools > UV Editor; Edit Mesh > UVs & Seams section; Modeling > Edit > UV Unwrap buttons
- **Docs:** `docs/editor/modeling.md`
- **Tests:** `uv` (test_uv.cpp)
- **Status:** Works - uv test covers per-corner UVs, seams, closed-part cutting, angle seams and packing; docs say a 24k-vertex sphere unwraps in about 3 s
- **Open:** UV convention is v = 0 at the top of the image (documented); tools that rebuild triangles outside the polygon core fall back to one UV per vertex
- **Related:** 3.1, 3.5, 2.2
- **Words:** UV, unwrap, seam, LSCM, Smart Unwrap, pack islands, texel density, UV editor, planar, box projection, tile
- **Seen as:** texture stretched or mirrored on a modeled part; seams not where expected; islands overlap after packing; texture lost after loop cut
- **Ask me like:** "pack UV islands without shrinking them"; "the UV Editor should show a checker when there is no texture"

### 3.5 Mesh assets and primitives
- **System:** Meshes - The 3D shape data that drawing and physics share, the built-in shapes, and the `.okaymesh` file.
  Home: `engine/include/okay/Render/Mesh.hpp`, `engine/include/okay/Modeling/MeshAsset.hpp`. Entry: `engine/include/okay/Render/Mesh.hpp`. Also called: Mesh, MeshAsset, .okaymesh, primitives. Fix: Render/Mesh (drawn mesh) and Modeling/PolyMesh, MeshAsset (edited mesh, file): document the split in docs/editor/modeling.md, one converter between them.
- **What:** The triangle Mesh type the renderer and physics use, its built-in primitives, the `.okaymesh` text format that keeps real polygons, and how edited meshes are saved (by name, by file path, or verbatim in the scene).
- **Parts:**
  - 3.5.1 Triangle Mesh and primitives - `Render/Mesh` - Cube, Sphere, Cylinder, Cone, Pyramid, Wedge, Quad, Plane, Tube, Torus, TorusKnot, Capsule, Icosphere, Grid, Hemisphere, Stairs, Gear, Prism, Octahedron, Disc, Tetrahedron, Bipyramid, RoundedBox
  - 3.5.2 `.okaymesh` file format - `Modeling/PolyMesh` - Serialize/Parse/Save/Load: verts, faces with any corner count, optional per-corner UVs, colour, material slot
  - 3.5.3 Mesh asset load, save, hash - `Modeling/MeshAsset` - LoadMeshAsset (fallback to the scene folder in exported games), SaveMeshAsset, GeometryHash
  - 3.5.4 Link and write-back - `editor/EditorState.*`, `editor/main.cpp` - Save as Mesh Asset, shared files, SaveEditedMeshAssets on Ctrl+S, Build Game copies the file to Data
  - 3.5.5 Saved forms of a mesh - `Render/Mesh` - primitive by name, file by path, hand-edited verbatim (`meshgeo` in the scene), `.obj`-backed edits kept in the scene
  - 3.5.7 Material slots - `Render/Mesh`, `Modeling/PolyMesh` - per-triangle slot and a colour per slot (Mesh::AssignSlot/ApplySlotColors), saved in the scene (`meshslots`, `slotcolors`) and `.okaymesh` (`slot N r g b a`); Edit Mesh > Paint > Material Slots
  - 3.5.9 Spin, extrude along path, array along curve - `Modeling/PolyOps`, `Modeling/Modifiers` - SpinEdges, ExtrudeAlongPath, the Array Along Curve modifier (SamplePath); Edit Mesh buttons and the modifier stack
  - 3.5.10 Measure and snap - `Modeling/Measure` - MeasureMesh / MeasureSelection readouts, SnapToGeometry (vertex, edge, face) used by the G move; Edit Mesh > Snapping and measuring
  - 3.5.11 Material library and palettes - `Modeling/MaterialLibrary` - ready slot colours; Edit Mesh > Material Slots
  - 3.5.12 Slot pictures - `Components/MeshRenderer`, `Render/SoftwareRenderer` - slotTextures drawn per triangle slot (software renderer only), `slottex` scene record
  - 3.5.13 Faster modeling - `Modeling/SelectOps`, `editor/main.cpp` - GrowFaces / ShrinkFaces / SelectSimilar / SelectByNormal / SelectByArea, BakeTransform, recipes (MakeWindowHole, RoundEdges, MakeTable); one command list behind the quick toolbar, right-click menu, Ctrl+K palette, Shift+R, undo history
  - 3.5.14 Hide, drag-select, symmetry, arrange - `Render/Mesh` (HideTriangles / RevealHidden / IsolateTriangles), `Modeling/ScreenSelect`, `Modeling/Arrange`, `Modeling/EditTransform` (mirror Y / Z), `Modeling/Measure` (RayMeshHit), `Modeling/SelectOps` (LooseParts); H / Alt+H / slash, B / C / L, Shift+D, E, I, join / separate / align / distribute / origin, add at cursor
  - 3.5.15 Vertex colours, creases, shrinkwrap, mesh library - `Render/Mesh` (vertexColors, PaintVertices, creases), `Modeling/SelectOps` (Shrinkwrap), `Modeling/PolyOps` (creased Catmull-Clark), `Modeling/MeshLibrary`, the four renderers' vertex colour paths; Edit Mesh > Paint, Ctrl+K crease / shrinkwrap / reduce, Import > Mesh library
  - 3.5.16 Per-slot GPU drawing - `Render/GLRenderer`, `Render/D3D11Renderer`, `Render/D3D12Renderer`, `Render/Mesh` (GroupBySlot) - one draw range per material slot with its own picture; never run on a GPU
  - 3.5.17 Reference images, Bezier curves, bevel weights - `Modeling/Reference`, `Modeling/Curve`, `Modeling/Modifiers` (useBevelWeights), `Render/Mesh` (bevelWeights) - blueprint planes saved in the scene (`refimage`), pipe / ribbon / swept profile from a curve, weighted Bevel modifier, pink creased and green weighted edges in Edit Mesh
  - 3.5.18 Quad Remesh - `Modeling/Remesh` - TrisToQuads, QuadRemesh (simplify, pair, split, relax, snap); Edit Mesh > Quads target, Ctrl+K "Quad Remesh"
  - 3.5.8 Repair Mesh - `Render/Mesh` - Mesh::Repair: merge doubles, drop collapsed triangles, cap holes, orient outward; Edit Mesh > Repair Mesh
  - 3.5.6 OBJ export and import helpers - `Render/Mesh` - SaveOBJ, LoadOBJ (UVs, vn normals), RecenterToOrigin, ScaleToFit, NormalizeImportScale
- **Code:** `Render/Mesh`, `Modeling/MeshAsset`, `Modeling/PolyMesh`, `editor/EditorState.*`
- **API:** `Mesh::Cube/Sphere/...`, `Mesh::FromName`, `LoadMeshAsset`, `SaveMeshAsset`, `GeometryHash`, `MeshRenderer::meshPath`; Inspector "Mesh File" field
- **Editor:** GameObject > 3D Object menu (every primitive); Modeling > Import tab and Edit Mesh > Save as Mesh Asset; Mesh Renderer inspector
- **Docs:** `docs/editor/modeling.md`
- **Tests:** `mesh3d`, `mesh_asset`, `mesh_variant_hash`, `meshedit`, `polymesh`, `obj_import`, `modeling_more`, `modeling_more_scene`, `modeling_fuzz`, `modifiers`
- **Status:** Partial - primitives and file round-trips are tested; material slots (a colour per slot) are built; per-slot textures and renderer submeshes are not; the inside-out claim below is unconfirmed
- **Open:** M5b: slots carry a colour only, not a texture or shader, and are drawn through per-face colours, not separate renderer submeshes; some triangle-level Mesh tools drop slot membership; old map says Sphere, Cylinder, Cone, Tube, Torus, TorusKnot and Capsule can be inside out (possible bug, unverified: mesh3d checks normals "regardless of winding")
- **Related:** 3.1, 3.6, 2.1, 2.2, 1.5
- **Words:** mesh, primitive, okaymesh, Mesh File, meshPath, OBJ, wedge, gear, icosphere, quad, polygon mesh, mesh asset
- **Seen as:** a primitive is dark or inside out; an edited mesh reverts after reopening the scene; the exported game cannot find the mesh file; every object using the shared file changes at once
- **Ask me like:** "support per-face materials in .okaymesh"; "why is the Cone inside out"

### 3.6 Model import
- **System:** Model Import - Reads 3D model files from other programs (FBX, glTF, OBJ and more) with their materials, bones and animations.
  Home: `engine/include/okay/Render/`, `engine/src/Render/`. Entry: `engine/include/okay/Render/ModelImport.hpp`. Also called: own readers, OKAY_PREFER_OWN_READERS, OKAY_OWN_FBX (deleted), ImportBackend::Own, Assimp, ufbx (deleted), FBX import, glTF import.
- **What:** One entry point turns a model file into a merged mesh or a scene hierarchy with materials, skeleton, skins, clips and morph targets, then prints an import report. Built in: OBJ (+MTL), STL, PLY, glTF/GLB and FBX (our own okay::fbx; the vendored ufbx was deleted on 2026-11-23); Collada has a reader of ours that is off by default; other formats only when the build has Assimp. Every Assimp read goes through one seam that returns our own `ImportedModel` type (see `docs/content/model_import.md`).
- **Parts:**
  - 3.6.1 glTF 2.0 and GLB - `Render/Gltf`, `Render/ModelImport` - own JSON reader and the shared `Core/Base64` decoder (#422 audit item 24: one Base64 for glTF, FBX, voxel saves, .okf fonts and session tickets, test `base64`), accessors including sparse and quantized, up to 8 influences (4 kept), matrix nodes decomposed, STEP/LINEAR/CUBICSPLINE keys, multi-material skins
  - 3.6.2 FBX - `Render/ModelImport` - built in through our own reader (3.6.10; ufbx was deleted on 2026-11-23, a file ours refuses fails the import): Y-up metres, skins, blend-shape channels, takes as clips, PBR and FBX materials
  - 3.6.3 OBJ - `Render/Mesh`, `Render/ModelReaders`, `Render/ModelImport` - LoadOBJ (merged mesh) with UVs and authored normals; `ReadObjFile` for scene import: one MeshRenderer per `usemtl`, `.mtl` Kd/Ke/Ns/map_Kd/bump/map_Ks
  - 3.6.4 Optional Assimp formats - `Render/ModelImportAssimp` - DAE, 3DS, BLEND and more (and STL/PLY first, unless OKAY_PREFER_OWN_READERS), only with OKAY_USE_ASSIMP (ON for the Windows dist build, OFF by default and for the web build); the only file that includes Assimp, it returns `ImportedModel`
  - 3.6.9 Own STL and PLY readers, import seam - `Render/ModelReaders`, `Render/ModelData` - ReadStlFile (ascii + binary), ReadPlyFile (ascii, little and big endian), `ImportModelData`, per-format `SetImportBackend` / `EffectiveImportBackend`, CMake `OKAY_PREFER_OWN_READERS`; hand-written bounds-checked parsers, fuzzed
  - 3.6.11 Own Collada reader (side by side, off by default) - `Render/ColladaReader`, `Render/ModelData`, `Render/ModelImport` - `ReadColladaFile` / `ReadColladaBuffer`: own XML parser; triangles, polylist, polygons; effects and textures; node tree with unit and up-axis; skins; animation (per-component, float3 and whole-matrix channels merged per node, LINEAR and STEP); `ImportedModel::notes` names whatever was not read; selected with `SetImportBackend(".dae", Own)`, env `OKAY_OWN_COLLADA=1` or `-DOKAY_OWN_COLLADA=ON`; Assimp stays the default reader
  - 3.6.5 Scene import and report - `Render/ModelImport` - ImportModelScene (one object per node, skinned parts per material), AnalyzeModel/ModelReport (meshes, bones, weights, clips, warnings)
  - 3.6.6 Materials and textures - `Render/Gltf`, `Render/ModelImport` - albedo, normal, specular, AO, emissive; embedded images extracted to a sidecar file; packed metallic-roughness and emissive textures are not imported
  - 3.6.7 Blend shapes (morph targets) - `Components/BlendShapes` - sparse per-shape offsets, weight sliders, filter box, glTF targets and FBX channels, `weights.<n>` clip tracks, applied before skinning
  - 3.6.8 Import UI and extra animation files - `editor/main.cpp`, `editor/MainShared.hpp` - File > Import Model..., Project panel drop, Add animation files (Mixamo FBX, glTF; DAE/BVH are named in the dialog)
  - 3.6.10 Own FBX reader (the only FBX reader) - `Render/FbxReader`, `Render/FbxScene`, `Render/ModelImport` - `okay::fbx`: our own binary 7.x and ASCII FBX reader with its own inflater; no switch (`OKAY_OWN_FBX` and ufbx were deleted on 2026-11-23); tests `fbx_reader` (fixed expected values recorded when it last agreed with ufbx), `fbx_route`; limits in `docs/content/model_readers.md`
  - 3.6.12 Own 3DS reader (side by side, off by default) - `Render/Max3dsReader`, `Render/ModelData`, `Render/ModelImport` - `Read3dsFile` / `Read3dsBuffer`: chunk reader for meshes, smoothing-group normals, materials and texture names, master scale, keyframe node hierarchy with pivots and position / rotation / scale tracks; selected with `SetImportBackend(".3ds", Own)`, env `OKAY_OWN_3DS=1` or `-DOKAY_OWN_3DS=ON`; checked on 16 real files (`docs/content/model_readers.md`)
  - 3.6.13 Own BVH reader (side by side, off by default) - `Render/BvhReader`, `Render/ModelData`, `Render/ModelImport` - `ReadBvhFile` / `ReadBvhBuffer`: joint hierarchy as nodes and the motion as one clip, no mesh; selected with `SetImportBackend(".bvh", Own)`, env `OKAY_OWN_BVH=1` or `-DOKAY_OWN_BVH=ON` (`docs/content/model_readers.md`)
  - 3.6.21 Own 3MF reader (side by side, off by default) - `Render/ThreeMfReader`, `Render/ModelImport` - `Read3mfFile` / `Read3mfBuffer`: zip package with an XML model part: meshes, component trees and build items with transforms, base materials and colour groups, support objects left out; Z-up root; own zip reader on the engine inflate, strict XML reader; selected with `SetImportBackend(".3mf", Own)`, env `OKAY_OWN_3MF=1` or `-DOKAY_OWN_3MF=ON` (`docs/content/model_readers.md`)
  - 3.6.22 Own AMF reader (side by side, off by default) - `Render/AmfReader`, `Render/ModelImport` - `ReadAmfFile` / `ReadAmfBuffer`: XML, optionally zip-compressed: objects, volumes, materials and colours (triangle, volume, object priority), constellations; Z-up root; selected with `SetImportBackend(".amf", Own)`, env `OKAY_OWN_AMF=1` or `-DOKAY_OWN_AMF=ON` (`docs/content/model_readers.md`)
  - 3.6.23 Own X3D reader (side by side, off by default) - `Render/X3dReader`, `Render/ModelImport` - `ReadX3dFile` / `ReadX3dBuffer`: XML encoding: Transform / Group / Switch / LOD with DEF and USE, Shape with Material and ImageTexture, IndexedFaceSet (normals, UVs, crease angle), IndexedTriangleSet, TriangleSet, Box, Sphere, Cylinder, Cone, Extrusion; unread nodes named in the notes; selected with `SetImportBackend(".x3d", Own)`, env `OKAY_OWN_X3D=1` or `-DOKAY_OWN_X3D=ON` (`docs/content/model_readers.md`)
  - 3.6.24 Own DXF reader (side by side, off by default) - `Render/DxfReader`, `Render/ModelImport` - `ReadDxfFile` / `ReadDxfBuffer`: ASCII DXF: 3DFACE, polyface and polygon-mesh POLYLINE, MESH, blocks and INSERT; one node per layer; lines, arcs, text and the rest counted by name in the notes; selected with `SetImportBackend(".dxf", Own)`, env `OKAY_OWN_DXF=1` or `-DOKAY_OWN_DXF=ON` (`docs/content/model_readers.md`)
  - 3.6.25 Own LWO reader (side by side, off by default) - `Render/LwoReader`, `Render/ModelImport` - `ReadLwoFile` / `ReadLwoBuffer`: LWO2 and LWO1 (LWOB): layers, points, polygons, surfaces, per-point and per-corner UVs, the first UV image map; shaders, envelopes, projected maps and the rest named in the notes; selected with `SetImportBackend(".lwo", Own)`, env `OKAY_OWN_LWO=1` or `-DOKAY_OWN_LWO=ON`; Modo `.lxo` (FORM LXOB) through the same reader with its own switch: materials from the `MATR` tags and Modo's shader tree, `NORM` vertex normals, env `OKAY_OWN_LXO=1` or `-DOKAY_OWN_LXO=ON`, sample `examples/own_readers/cube.lxo` (`docs/content/model_readers.md`)
  - 3.6.26 Own MD2 reader (side by side, off by default) - `Render/Md2Reader`, `Render/ModelImport` - `ReadMd2File` / `ReadMd2Buffer`: Quake II models, first frame as one static part with texture coordinates and the first skin; vertex animation, stored normals and GL commands named in the notes; Z-up root; selected with `SetImportBackend(".md2", Own)`, env `OKAY_OWN_MD2=1` or `-DOKAY_OWN_MD2=ON` (`docs/content/model_readers.md`)
  - 3.6.27 Own MD3 reader (side by side, off by default) - `Render/Md3Reader`, `Render/ModelImport` - `ReadMd3File` / `ReadMd3Buffer`: Quake III models, one part per surface (first frame, stored normals decoded), first shader name as texture, tags as empty child nodes; selected with `SetImportBackend(".md3", Own)`, env `OKAY_OWN_MD3=1` or `-DOKAY_OWN_MD3=ON` (`docs/content/model_readers.md`)
  - 3.6.28 Own Irrlicht mesh reader (side by side, off by default) - `Render/IrrMeshReader`, `Render/ModelImport` - `ReadIrrMeshFile` / `ReadIrrMeshBuffer`: `.irrmesh` XML, UTF-8 and UTF-16, one part and material per buffer (standard, 2tcoords, tangents vertices), diffuse and emissive colours, first texture and normal map; the `.irr` scene format is not covered; selected with `SetImportBackend(".irrmesh", Own)`, env `OKAY_OWN_IRRMESH=1` or `-DOKAY_OWN_IRRMESH=ON` (`docs/content/model_readers.md`)
  - 3.6.29 Own B3D reader (side by side, off by default) - `Render/B3dReader`, `Render/ModelImport` - `ReadB3dFile` / `ReadB3dBuffer`: Blitz3D chunk tree walked with an explicit stack; nodes, meshes, brushes, bone weights, key animation; left-handed turned right-handed; selected with `SetImportBackend(".b3d", Own)`, env `OKAY_OWN_B3D=1` or `-DOKAY_OWN_B3D=ON` (`docs/content/model_readers.md`)
  - 3.6.30 Own DirectX .x reader (side by side, off by default) - `Render/XFileReader`, `Render/ModelImport` - `ReadXFile` / `ReadXBuffer`: text, binary and MSZIP-compressed `.x` (32 and 64 bit floats): frames as nodes, meshes split per material, materials, skin weights, animation sets with rotation, scale, position and matrix keys; selected with `SetImportBackend(".x", Own)`, env `OKAY_OWN_X=1` or `-DOKAY_OWN_X=ON` (`docs/content/model_readers.md`)
  - 3.6.31 Own AC3D reader (side by side, off by default) - `Render/Ac3dReader`, `Render/ModelImport` - `ReadAc3dFile` / `ReadAc3dBuffer`: `.ac` / `.acc` / `.ac3d` text, UTF-8 and UTF-16: object tree with loc and rot as nodes, polygons (triangulated), triangle strips, per-vertex normals or crease-angle smoothing, materials, texture with texrep and texoff, Catmull-Clark `subdiv`; lines, lights and extra texture layers named in the notes; selected with `SetImportBackend(".ac", Own)`, env `OKAY_OWN_AC3D=1` or `-DOKAY_OWN_AC3D=ON` (`docs/content/model_readers.md`)
  - 3.6.32 Own OGRE XML mesh reader (side by side, off by default) - `Render/OgreXmlReader`, `Render/ModelImport` - `ReadOgreXmlFile` / `ReadOgreXmlBuffer`: `.mesh.xml` shared and own vertex buffers, one part per submesh, positions, normals, first texture coordinates, materials from the `.material` scripts next to the file, on a compact XML tree (`xml::ParseFlat`: 0.42x Assimp's time, 0.54x its memory); skeletons (`.skeleton.xml`, and the binary `.skeleton` through `ReadOgreSkeletonBinary`, found next to a mesh only with `OKAY_OWN_OGRE_SKELBIN=1`, off by default) and bone assignments skin the parts; selected with `SetImportBackend(".mesh.xml", Own)`, env `OKAY_OWN_OGRE=1` or `-DOKAY_OWN_OGRE=ON` (`docs/content/model_readers.md`)
  - 3.6.33 Own XGL / ZGL reader (side by side, off by default) - `Render/XglReader`, `Render/ModelImport` - `ReadXglFile` / `ReadXglBuffer`: XGL XML scenes and ZGL (the same in one zlib stream, our own inflate): meshes with per-corner normals and texture coordinates, materials, objects as nodes with forward / up / position / scale; selected with `SetImportBackend(".xgl", Own)`, env `OKAY_OWN_XGL=1` or `-DOKAY_OWN_XGL=ON` (`docs/content/model_readers.md`)
  - 3.6.34 Own Irrlicht scene reader (side by side, off by default) - `Render/IrrSceneReader`, `Render/ModelImport` - `ReadIrrSceneFile` / `ReadIrrSceneBuffer`: `.irr` node tree with position, rotation and scale, cube and sphere nodes, and the mesh files a scene names read through the same import table (`IrrMeshLoader`); animators and camera, light and sky nodes (kept as empty nodes) not read; selected with `SetImportBackend(".irr", Own)`, env `OKAY_OWN_IRR=1` or `-DOKAY_OWN_IRR=ON` (`docs/content/model_readers.md`)
  - 3.6.35 Own NFF reader (side by side, off by default) - `Render/NffReader`, `Render/ModelImport` - `ReadNffFile` / `ReadNffBuffer`: Neutral File Format (polygons, patches, spheres and ellipsoids, cones and cylinders, tet / hex / oct / dod / ico solids, `tess`, fill colours) and the Sense8 "nff" object files with their `.mat` tables; lights, viewpoint and the colour coefficients named in the notes; selected with `SetImportBackend(".nff", Own)`, env `OKAY_OWN_NFF=1` or `-DOKAY_OWN_NFF=ON` (`docs/content/model_readers.md`, `docs/content/model_readers.md`)
  - 3.6.36 Remaining Assimp formats - `docs/content/own_readers_remaining.md` - the importers of Assimp v5.4.3 without an own reader (6 switched on in our Windows build after batches E to E5: OpenGEX, MDL, Q3BSP, SIB, Nendo, glTF 1.0, plus the binary half of OGRE): what each is, difficulty, real files in Assimp's tree and the order to write them in; documentation only, no code
  - 3.6.37 Own Terragen reader (side by side, off by default) - `Render/TerragenReader`, `Render/ModelImport` - `ReadTerragenFile` / `ReadTerragenBuffer`: the `.ter` height field (SIZE, XPTS, YPTS, SCAL, CRAD, CRVM, ALTW) as one grid mesh in metres, curve mode 1 applied, Z-up turned to Y-up, normals from the grid, u and v 0..1; grids up to 2049 x 2049; selected with `SetImportBackend(".ter", Own)`, env `OKAY_OWN_TER=1` or `-DOKAY_OWN_TER=ON` (`docs/content/model_readers.md`)
  - 3.6.38 Own MDC, HMP and CSM readers (side by side, off by default) - `Render/MdcReader`, `Render/HmpReader`, `Render/CsmReader`, `Render/ModelImport` - `ReadMdcFile` / `ReadHmpFile` / `ReadCsmFile` and the `...Buffer` forms: MDC (Return to Castle Wolfenstein, first frame as one part per surface, tags as nodes), HMP (3D GameStudio terrain, HMP4, HMP5 and HMP7 since 2026-11-23, the height grid as one mesh, embedded skins skipped), CSM (Character Studio motion capture: markers as nodes and one clip of position keys, no mesh); written from format descriptions and sample files, no other importer's source read; selected with `SetImportBackend(".mdc", Own)` (and `.hmp`, `.csm`), env `OKAY_OWN_MDC=1` / `OKAY_OWN_HMP=1` / `OKAY_OWN_CSM=1` or `-DOKAY_OWN_<FMT>=ON` (`docs/content/model_readers.md`, `own_hmp_reader.md`, `own_csm_reader.md`)
  - 3.6.39 Texture paths named in model files - `Render/TexturePath`, `Render/ModelReaders`, `Render/Mesh` - `ResolveModelTexturePath` / `SetModelTextureRoots`: network, device and drive-relative paths, and names that are not valid UTF-8, refused on the text before any disk access, each with one log warning naming the file and the reason; a texture or `mtllib` inside the model's folder is kept (links followed); absolute and `../` paths only inside an allowed root (the editor's open project, validated), links followed; used by both OBJ/MTL readers and, for the screen and folder check, by scene files (#753)
  - 3.6.41 Own PMX reader (side by side, off by default, unverified on real files) - `Render/PmxReader`, `Render/ModelImport` - `ReadPmxFile` / `ReadPmxBuffer`: MikuMikuDance models, PMX 2.0 / 2.1 (UTF-16LE or UTF-8, index sizes 1 / 2 / 4, BDEF1 / BDEF2 / BDEF4 / SDEF / QDEF weights, one skinned part per material, bones as nodes with inverse binds, texture paths kept inside the model's folder); morphs are checked, not imported; selected with `SetImportBackend(".pmx", Own)`, env `OKAY_OWN_PMX=1` or `-DOKAY_OWN_PMX=ON` (`docs/content/model_readers.md`)
  - 3.6.42 Own LWS / MOT reader (side by side, off by default, unverified on real files) - `Render/LwsReader`, `Render/ModelImport` - `ReadLwsFile` / `ReadLwsBuffer`: LightWave scenes (items as nodes, `ParentItem`, position / heading / pitch / bank / scale envelopes as one clip, the object files a scene names read through the import table and only from inside the scene's folder) and `.mot` motions; selected with `SetImportBackend(".lws", Own)`, env `OKAY_OWN_LWS=1` or `-DOKAY_OWN_LWS=ON` (`docs/content/model_readers.md`)
  - 3.6.43 Why an import failed - `Render/ModelImportProblem`, `editor/main.cpp` - `DescribeModelImportProblem(path)`: missing file, folder, empty file, unknown extension (with the list this build reads), a picture or text file renamed to .glb, glTF 1 binary, cut-off .glb, .glb / .gltf swapped, not JSON, not an FBX, very large, else the seam reader's own error; every "Model import failed" Console line in the editor (drop, double-click, Hierarchy drag, Import Model, character model) now ends with that reason; a folder named like a model (`x.obj`, `x.glb`, `x.fbx`) or a font (`x.ttf`) is refused instead of ending the editor with std::length_error / bad_alloc / ios_base::failure (`ImportModel` / `ImportModelScene` refuse anything that is not an ordinary file; `Mesh::LoadOBJ`, the own FBX `LoadFile` and `TtfFont::LoadFromFile` refuse it too)
  - 3.6.44 Own MDL reader, Quake 1, Half-Life 1 and 3D GameStudio (side by side) - `Render/MdlReader`, `Render/GameStudioSkin`, `Render/ModelImport` - `ReadMdlFile` / `ReadMdlBuffer`: Quake 1 (`IDPO`) and Half-Life 1 (`IDST`) models, and since 2026-11-23 the 3D GameStudio MDL3 / MDL4 / MDL5 models (Conitec's published layout: first frame, skin-vertex texture coordinates) and MDL7 (worked out from three real files: groups as nodes, materials with colours and texture file names, one part per skin); bones, animation frames and embedded skin pictures not read; env `OKAY_OWN_MDL=0/1` (`docs/content/model_readers.md`, section MDL)
  - 3.6.45 Own SIB reader (side by side, on by default; Assimp the fallback when built) - `Render/SibReader`, `Render/ModelImport` - `ReadSibFile` / `ReadSibBuffer`: Nevercenter Silo `.sib`, worked out from the one sample file (no public description exists): shapes with n-gon faces, per-corner texture coordinates and per-face materials, materials with a diffuse colour and a texture file, instances placed in their own frame; mirrors, lights and groups not read; selected with `SetImportBackend(".sib", Own)`, env `OKAY_OWN_SIB=0/1` or `-DOKAY_OWN_SIB=OFF/ON` (`docs/content/model_readers.md`, section SIB)
- **Code:** `Render/ModelImport`, `Render/ModelImportProblem`, `Render/ModelData`, `Render/ModelReaders`, `Render/ColladaReader`, `Render/Max3dsReader`, `Render/BvhReader`, `Render/ThreeMfReader`, `Render/Md2Reader`, `Render/Md3Reader`, `Render/IrrMeshReader`, `Render/B3dReader`, `Render/XFileReader`, `Render/Ac3dReader`, `Render/OgreXmlReader`, `Render/XglReader`, `Render/IrrSceneReader`, `Render/NffReader`, `Render/TerragenReader`, `Render/PmxReader`, `Render/LwsReader`, `Render/ModelImportAssimp`, `Render/FbxReader`, `Render/FbxScene`, `Render/Gltf`, `Render/Mesh`, `Components/BlendShapes`, `editor/MainShared.hpp`, `editor/main.cpp`, `scripts/gen_fbx_fixtures.py`, `scripts/gen_3ds_fixtures.py`, `scripts/gen_bvh_fixtures.py`
- **API:** `ImportModel`, `ImportModelScene`, `AnalyzeModel`, `ImportableExtensions`, `AssimpAvailable`, `okay::fbx::LoadFile`, `okay::fbx::OwnReaderEnabled`, `OKAY_OWN_FBX`, `ImportModelData`, `ImportedModel`, `SetImportBackend`, `ReadObjFile`, `ReadStlFile`, `ReadPlyFile`, `ReadColladaFile`, `OKAY_OWN_COLLADA`, `Read3dsFile`, `ReadBvhFile`, `OKAY_OWN_3DS`, `OKAY_OWN_BVH`, `Read3mfFile`, `OKAY_OWN_3MF`, `ReadMd2File`, `ReadMd3File`, `ReadIrrMeshFile`, `ReadB3dFile`, `ReadXFile`, `ReadAc3dFile`, `ReadOgreXmlFile`, `ReadXglFile`, `ReadIrrSceneFile`, `ReadNffFile`, `ReadMdcFile`, `ReadHmpFile`, `ReadCsmFile`, `ReadTerragenFile`, `OKAY_OWN_TER`, `ReadPmxFile`, `ReadLwsFile`, `OKAY_OWN_PMX`, `OKAY_OWN_LWS`, `IrrMeshLoader`, `OKAY_OWN_AC3D`, `OKAY_OWN_OGRE`, `OKAY_OWN_XGL`, `OKAY_OWN_IRR`, `OKAY_OWN_NFF`, `OKAY_OWN_MDC`, `OKAY_OWN_HMP`, `OKAY_OWN_CSM`, `OKAY_OWN_MD2`, `OKAY_OWN_MD3`, `OKAY_OWN_IRRMESH`, `OKAY_OWN_B3D`, `OKAY_OWN_X`, `LoadGLTF`, `AddAnimationsFromFile`, `BlendShapes`; script `blend_shape("Smile", 0.8)`, `blend_shape_get`
- **Editor:** File > Import Model..., Project panel (double-click, drag into viewport), Console import report, Blend Shapes inspector
- **Docs:** `docs/content/model_import.md`, `docs/editor/modeling.md`, `docs/gameplay/animation.md` (imported characters, blend shapes, Tips), `docs/content/model_readers.md`, `docs/content/own_readers_speed.md` (speed and memory of our readers against Assimp, `tests/bench_model_readers.cpp`), `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/own_readers_remaining.md`, `docs/content/model_corpus.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`, `docs/content/model_readers.md`
- **Tests:** `model_import_problem`, `gltf_import`, `gltf_cubicspline`, `model_import_rig`, `blend_shapes`, `obj_import`, `loaders_robust`, `model_readers`, `obj_texture_roots`, `model_readers_fuzz`, `collada_reader`, `3ds_reader`, `bvh_reader`, `3ds_bvh_hostile`, `3mf_reader`, `batchb_hostile`, `md2_reader`, `md3_reader`, `irrmesh_reader`, `b3d_reader`, `x_reader`, `batchc_hostile`, `own_dispatch_c`, `ac3d_reader`, `ogre_reader`, `xgl_reader`, `irr_reader`, `nff_reader`, `batchd_hostile`, `terragen_reader`, `terragen_hostile`, `own_dispatch_d`, `model_corpus`, `fbx_reader`, `fbx_hostile`, `mdc_reader`, `hmp_reader`, `csm_reader`, `batche_hostile`, `own_dispatch_e`, `own_reader_wiring`
- **Status:** Partial - glTF, OBJ, STL and PLY are well tested (the last two with fixtures, malformed input and a fuzz sweep); FBX is exercised by `fbx_reader` and `fbx_hostile` on small generated fixtures (our own reader, against values recorded when it last agreed with the deleted ufbx path) and, when `OKAY_MODEL_CORPUS` is set, on 43 real files, with no real-world file stored in the repository; the Assimp formats are only exercised by `model_readers` in a build with OKAY_USE_ASSIMP, where it compares Assimp with our readers (so the default suite does not cover them)
- **Open:** our Collada, 3DS and BVH readers are off by default and were checked on one outside corpus only (`docs/content/model_corpus.md`; Collada has no morphs and flattens curve interpolation to linear, see `docs/content/model_import.md`); still Assimp-only (no reader of ours yet): the formats listed in `docs/content/own_readers_remaining.md` (6 switched on in our build: OpenGEX, MDL, Q3BSP, SIB, Nendo, glTF 1.0) and OGRE's binary `.skeleton` by default (our reader reads it only with `OKAY_OWN_OGRE_SKELBIN=1`); the 39 importers that have a reader of ours (counted 2026-11-15) are all off by default except the always-on glTF 2.0, OBJ, STL and PLY (`docs/status/own_flip_proposal.md` has the order of flips and the readiness of each); OBJ keeps v as written while PLY/FBX/Assimp flip it (`docs/content/model_import.md`); packed metallic-roughness and emissive textures not imported; BVH is named in the Add-animations dialog; it reads through our own reader only when switched on (`OKAY_OWN_BVH`), otherwise only with an Assimp build; sample Mixamo menu items download from GitHub with curl on first use; our own FBX reader is non-default and checked on one outside corpus of 43 files only, see the NOT handled list in docs/content/model_readers.md
- **Related:** 3.7, 5.5, 5.2, 20.4, 2.2
- **Words:** import, glTF, GLB, FBX, OBJ, MTL, STL, PLY, DAE, Collada, Assimp, ufbx, skin, bones, weights, morph target, blend shape, shape key, Mixamo, import report, textures, own FBX reader, OKAY_OWN_FBX, okay::fbx
- **Seen as:** imported model is white or has no textures; model faces the wrong way or is a tiny or huge size; character stays in a T-pose; faces dark; Console warns about missing bones or unweighted vertices; FBX will not import in a default build; a .dae/.3ds file will not import without Assimp
- **Ask me like:** "import FBX takes as separate clips"; "support the metallic-roughness texture from glTF"

### 3.7 Rigging and playable setup
- **System:** Rigging - Adds a skeleton to a model and sets it up so it can be animated and played as a character.
  Home: `engine/include/okay/Components/HumanoidRetarget.hpp`. Entry: `engine/include/okay/Components/HumanoidRetarget.hpp`. Also called: rig, skeleton setup, auto rig, playable setup.
- **What:** Turn a model into something that plays: auto-rig a model with no skeleton to the engine humanoid, attach any imported model as a player's body (size, grounding, facing, collider, locomotion clips), or build a whole playable third-person character from a model file in one click.
- **Parts:**
  - 3.7.1 Rig Model (Humanoid) auto-rig - `Components/Character` - BindCustomMesh binds each vertex to its nearest bone segment so the built-in animations drive the model; Unrig restores the blocky body
  - 3.7.2 Set Character Model - `Render/ModelImport` - AttachCharacterModel: import as child, scale to height, ground feet, face -Z, wire locomotion, hide the blocky Character
  - 3.7.3 Make Playable Character - `Render/ModelImport` - MakePlayableCharacter: Third Person player + model + humanoid IK + Look-At; deactivates any other playable avatar
  - 3.7.4 Facing and collider fit - `Render/ModelImport`, `Physics/ColliderFit` - FaceCharacterModel (facing read from left/right bones), FitCharacterCollider, Fit Collider after rescale
  - 3.7.5 Character Model panel and Fix animations - `Render/ModelImport`, `editor/main.cpp` - DescribeCharacterModel slots (imported, built-in or empty), AutoMapLocomotion, FixCharacterAnimations, Add animation files
  - 3.7.6 Map Humanoid Bones window - `editor/main.cpp`, `Animation/HumanoidBones` - hand-map engine bones on a rig that auto-detect misses
  - 3.7.7 Rig test assets and harness - `tests/rig_harness.hpp` - eleven rigs (Mixamo, VRoid, Character Creator, Daz, UE mannequin, Fox...) walked through the same motions
- **Code:** `Render/ModelImport`, `Components/Character`, `Physics/ColliderFit`, `Animation/HumanoidBones`, `editor/main.cpp`, `tests/rig_harness.hpp`
- **API:** `AttachCharacterModel`, `MakePlayableCharacter`, `FaceCharacterModel`, `FitCharacterCollider`, `DescribeCharacterModel`, `EnableBuiltinAnimations`, `Character::BindCustomMesh`; script `set_character_model(path)`
- **Editor:** Hierarchy right-click > Rig Model (Humanoid) / Map Humanoid Bones... / Use Built-in Animations; Project panel right-click > Make Playable Character; Character Model panel (Set Character Model..., Fix Facing, Turn 180/90, Fit Collider)
- **Docs:** `docs/gameplay/animation.md`, `docs/gameplay/ik.md`
- **Tests:** `make_playable`, `character_model_setup`, `imported_glitches`, `rig_sweep`, `humanoid_bones`, `collider_fit`
- **Status:** Works - make_playable, character_model_setup and rig_sweep (11 rigs) exist; In flight: #554 adds PlayerSetup (a status read-out and a "wire missing animation and IK" step on the controller panel) and small ModelImport.hpp edits
- **Open:** old map: one-click humanoid import on any rig; auto-rig expects an upright model facing +Z in a T/A pose; a model with fewer than 8 of the 15 humanoid bones is not set up; in flight in #554 (Grok): PlayerSetup/WirePlayerAnimation
- **Related:** 3.6, 4.1, 4.9, 5.3, 5.4, 5.6
- **Words:** rig, auto-rig, Rig Model, Make Playable Character, Set Character Model, humanoid, avatar, Mixamo, VRoid, T-pose, bone map, Fix Facing, Fit Collider
- **Seen as:** imported character is in a T-pose or faces sideways; collider does not wrap the model; two players after Make Playable; model floats or sinks; wrong animation in a slot; "rigging did nothing"
- **Ask me like:** "make a Mixamo character playable in one click"; "the model walks backwards after Set Character Model"

### 3.8 Sculpt and paint brushes
- **System:** Modeling
- **What:** Viewport brushes on an edited mesh: sculpt (Grab, Inflate, Smooth, Flatten, Pinch) and per-face colour painting that multiplies with the material colour.
- **Parts:**
  - 3.8.1 Sculpt brush - `Render/Mesh` - SculptBrush (Grab, Inflate, Smooth = Laplacian over edge neighbours, Flatten, Pinch) with radius, strength, BrushFalloff (smoothstep), symmetryX in one pass; seam and pole copies move together; subdivide first for detail
  - 3.8.2 Paint brush and fill - `Render/Mesh` - EnsureFaceColors, PaintFaces (a quad or n-gon paints as one face), FillFacesColor, MottleFaceColors, Clear Paint; saved with the scene
- **Code:** `Render/Mesh`, `editor/main.cpp`
- **API:** `Mesh::SculptBrush`, `Mesh::BrushFalloff`, `Mesh::PaintFaces`, `FillFacesColor`
- **Editor:** Modeling > Edit Mesh > Sculpt Brush and Paint Brush ticks
- **Docs:** `docs/editor/modeling.md`
- **Tests:** `sculpt_paint`, `meshedit`, `mesh3d`
- **Status:** Works - `sculpt_paint` checks every brush, the falloff, X symmetry and face paint against hand-worked numbers; the editor strokes themselves are not tried in a running editor
- **Open:** no per-vertex weight or texture painting (vertex colours are separate, see docs); no Draw/Clay/Crease brush
- **Related:** 3.1, 2.2
- **Words:** sculpt, brush, inflate, grab, pinch, flatten, paint, vertex colour, face colour
- **Seen as:** brush does nothing on a coarse mesh; paint colour is muddy because it multiplies with the material
- **Ask me like:** "add a Crease brush"; "make paint strokes undoable one by one"

### 3.9 Generate and whole-mesh operations
- **System:** Modeling
- **What:** Profile-driven shapes (lathe, screw, extrude outline, pipe along a path) and the one-click whole-mesh operations in the Modeling window's Edit tab.
- **Parts:**
  - 3.9.1 Lathe and Screw - `Render/Mesh` - Mesh::Lathe, Mesh::Screw from a (radius, height) point list; Vase, Goblet, Column presets
  - 3.9.2 Extrude Outline and Pipe - `Render/Mesh` - Mesh::Extrude, Mesh::SweepPath with twist-free frames (Straight, Arc, Helix, S-Curve, Zigzag presets)
  - 3.9.3 Subdivide, smooth, relax, fill, grid snap - `Render/Mesh` - Subdivide, Smooth, SubdivideSmooth, Smooth/Relax, FillHoles, BridgeLoops, LoopCut (axis), SnapToGrid, AutoSmooth (ComputeAutoSmoothNormals)
- **Code:** `Render/Mesh`, `editor/main.cpp`
- **API:** `Mesh::Lathe`, `Mesh::Screw`, `Mesh::Extrude`, `Mesh::SweepPath`, `Mesh::FillHoles`
- **Editor:** Modeling window > Generate and Edit tabs
- **Docs:** `docs/editor/modeling.md`
- **Tests:** `mesh3d`, `modeling`
- **Status:** Works - geometry builders covered by mesh3d and modeling; I did not read the editor panels
- **Open:** none known
- **Related:** 3.3, 3.5
- **Words:** lathe, screw, extrude outline, pipe, sweep, vase, spring, auto smooth, subdivide, fill holes
- **Seen as:** pipe twists at bends; lathe shape is inside out; Fill Holes leaves a gap
- **Ask me like:** "add a Loft between two profiles"; "pipe along a custom spline"

### 3.10 Add library and procedural props
- **System:** Modeling
- **What:** The Modeling window's Add library and GameObject > 3D Object menu: 23 primitives plus pre-coloured nature, prop and structure meshes, with procedural variants (one number per object regenerates the exact shape) and Shape tab collision modes.
- **Parts:**
  - 3.10.1 Nature props - `Render/Mesh` - Tree, Pine, PalmTree, DeadTree, Bush, Rock, Mushroom, Cactus, Crystal with Variant stepper and Randomize; terrain Scatter gives every instance its own variant
  - 3.10.2 Props and structures - `Render/Mesh` - Barrel, Crate, Fence, Well, StreetLamp, Campfire, Lantern, Chest, Signpost, Cart, Tent, Bench, House, Tower, Windmill, Bridge, RockArch
  - 3.10.3 Variant randomness - `Render/Mesh` - FromNameSeeded, NameHasVariants, PropRand (test comment: it once overflowed signed ints for variant >= 6; values must stay identical)
  - 3.10.4 Shape tab and collider refit - `Physics/ColliderFit`, `editor/main.cpp` - Box (bounds) or Mesh (exact) collision, colliders refit when the mesh changes, Add Physics
- **Code:** `Render/Mesh`, `Physics/ColliderFit`, `editor/main.cpp`
- **API:** `Mesh::FromName`, `Mesh::FromNameSeeded`, `Mesh::Tree(variant)`, `ColliderFit`, `MeshRenderer`
- **Editor:** Modeling window > Add (filter box, categories) and Shape tab; GameObject > 3D Object > Nature / Props / Structures
- **Docs:** `docs/editor/modeling.md`
- **Tests:** `mesh3d`, `mesh_variant_hash`, `collider_fit`
- **Status:** Works - variants and collider fit have dedicated tests; looks of each prop are not covered by tests
- **Open:** none known
- **Related:** 3.5, 2.6, 6.2
- **Words:** nature, tree, rock, props, barrel, crate, house, variant, randomize, scatter, collision mode, mesh collider, box collider
- **Seen as:** every tree in the forest looks the same; collider does not match after Extrude; prop is a flat plastic colour
- **Ask me like:** "add a Boulder variant to Rock"; "refit the collider after an edit"

### 3.11 Guided creation (templates, parts, safe regeneration)
- **System:** Modeling
- **What:** A Guided page in the Modeling window: pick a category and a starting design, get an ordinary editable mesh generated from a small recipe, change its settings, swap designs per kind of part or per part, keep editing by hand. Rebuilding never overwrites hand edits silently (keep / discard / convert). The fence and the architecture templates (wall, stairs, door, window, roof, building) exist; other categories show an honest empty state. Click-to-draw path and a collider option are on the page.
- **Parts:**
  - 3.11.1 Recipe, registry and generation - `Modeling/Guided` - Recipe (text `okayguided 1`), Template and Registry, deterministic Generate, parts with stable ids
  - 3.11.2 Analysis and regeneration - `Modeling/Guided` - Analyze (intact / edited / removed parts, custom faces), Regenerate with Keep / Discard decisions, DetachPart, ConvertToRegular
  - 3.11.3 Fence template - `Modeling/GuidedFence` - 7 start designs, posts, caps, rails, pickets, bars, chain link, gate, path and ground slope, user parts as designs
  - 3.11.4 User parts and templates - `Modeling/Guided` - MakeUserPart / FitUserPart (`.okaypart`), UserTemplate (`.okaytemplate`), folder loaders
  - 3.11.6 Architecture templates - `Modeling/GuidedArchitecture` - Wall (path, openings), Stairs (straight, L, U, spiral), Door, Window, Roof, Building composed of swappable parts
  - 3.11.7 Vehicle templates and Detail level - `Modeling/GuidedVehicles`, `Modeling/Guided` - Car, Truck, Bus, Motorcycle, Trailer with swappable wheels, lights, bumpers, mirrors, spoilers, exhausts; the shared Detail setting (Low to Ultra) and TriangleCount
  - 3.11.8 Character templates - `Modeling/GuidedCharacters` - Human (9), Robot (6), Creature (11 on five body plans) with swappable head, hair, clothes, hands, feet, accessories and proportion sliders
  - 3.11.5 Guided page - `editor/GuidedPanel`, `editor/main.cpp` - categories, design browser with thumbnails, settings, parts list, decision window, undo and redo of guided steps
- **Code:** `Modeling/Guided`, `Modeling/GuidedFence`, `editor/GuidedPanel`, `editor/main.cpp` (DrawModeling page 7)
- **API:** `guided::Recipe`, `Template`, `Registry`, `Generate`, `Create`, `Analyze`, `Regenerate`, `DetachPart`, `ConvertToRegular`, `MakeUserPart`, `GuidedHistory`
- **Editor:** Modeling window > Guided
- **Docs:** `docs/editor/guided_modeling.md`, `docs/editor/modeling.md`, `docs/try_it.md` (7zg)
- **Tests:** `guided_modeling`, `guided_architecture`, `guided_vehicles`, `guided_characters`, `guided_detail`
- **Status:** Partial - the engine side (generation, rules, files) is tested headlessly; the editor page compiles but has not been opened in a real window or on Windows
- **Open:** Architecture, vehicles, characters and the fence exist (with a shared Detail level); environment and props do not; no landing railings; wall corners are not mitred; material slots are stored but only colours are drawn (M5b)
- **Related:** 3.1, 3.5, 3.9
- **Words:** guided, procedural, template, fence, parts, recipe, regenerate, picket, wrought iron, chain link, gate, user part, convert to regular mesh
- **Seen as:** the Guided page asks what to do with parts you edited; a model made here still edits like any mesh; a fence changed height but one post did not
- **Ask me like:** "add a wall template"; "let me draw the fence path in the 3D view"; "add a collider to a guided model"

### 3.12 Simple view of the Modeling window (beginner mode)
- **System:** Modeling
- **What:** The Modeling window opens in a simple view by default: Undo / Redo / Do again always on top, the model's size, a "What do I click next?" line that follows the selection, "Pick parts" (corners, edges, faces; one click turns edit mode on), up to six quick actions for what is selected, the last four tools used, and five tabs named by what you want to do (Shape with seven one-click starter models, Reshape, Details, Colour, Finish). Every tool has a plain name, a one-sentence tip, an example and its shortcut; tools that cannot run are greyed out with the reason. There is no Advanced switch any more (removed 2026-11-18): the Modeling window is one window with a page list on the left, and the simple view is its first page, "Easy", which is the page it opens on. Every other tool is a page in the same list, so no tool was lost; the old `modelingadvanced` line in okay_settings.txt is read and ignored. Tools run the existing operations (no second copy). The page list shows pages under job headings (Start here, Make something, Change it, Fix parts, Extras, Files) with plain titles (Easy, Add shapes, Step by step, Make shapes, Change the shape, Bend and cut, Fix the surface, Effects, Bring in a model; the old titles Add, Guided, Generate, Shape, Edit, Edit Mesh, Modifiers and Import are still found by the search box; fold it with the arrow or Ctrl+\) (Create, Shape and size, Edit parts, Effects, Files), has a tool search box (type bevel or round: matching buttons are outlined on their page and the page is named) and a hint line with a plain sentence for the button under the mouse; the Edit and Edit Mesh pages got real section headings (the mirror and deform tools used to sit under UV Unwrap). The simple view also has: Pick whole part and Pick similar, a mirror-my-changes switch (on by default), the last four tools remembered between sessions, and a live preview while the How much slider moves (applied once when let go). Click counts for eight beginner jobs, old window to new (walked through the code, not timed with a person): box with a roof 9 to 1, round a corner 6 to 3, window hole 7 to 3, symmetric face 4 to 1, stretch 3 to 1, table legs 10 to 1, smooth a rock 5 to 2, paint two colours 9 to 4.
- **Code:** `editor/ModelingEasy.hpp` (catalogue, jargon check, hints, quick actions, recent tools, job counts), `editor/ModelingEasyOps.hpp` (tool to mesh operation), `editor/ModelingStarters.hpp`, `editor/ModelingEasyUi.cpp`, `editor/ModelingToolIndex.hpp` (every tool, pages by job, search), `editor/ModelingHighlight.hpp`, `editor/main.cpp` (DrawModelingSimple, DrawModeling)
- **API:** `oked::easymodel::Tools`, `NextHint`, `QuickActions`, `WhyNot`, `RunMeshTool`, `BuildStarter`
- **Editor:** Modeling window (opens on the Easy page; the other pages are in the list on its left)
- **Docs:** none yet (internal note only)
- **Tests:** `modeling_easy`, `modeling_easy_ops`, `modeling_starters`, `modeling_tool_index`, `editor_layout`
- **Status:** Partial - the logic is tested headlessly and the widgets were drawn headless at 1280x720; the editor was not run on a real desktop or Windows
- **Open:** not built: drag a face to pull it out without a mode switch, double-click in the 3D view to pick a part (a Pick whole part button exists), click-drag paint select, snapping on by default with a visible grid size, collapsible sections in the other pages that remember open or closed; the mirror switch drives X symmetry only (and the older Start with symmetry on setting still exists beside it); starter tiles have no picture in the headless shots; the Save model button still says "Mesh Asset" on the Bring in a model page
- **Related:** 3.1, 3.5, 3.10, 3.11
- **Words:** beginner, simple view, easy modeling, quick actions, starters, tree, house, car, sword, table, rocket, what do I click next, page list, ctrl backslash
