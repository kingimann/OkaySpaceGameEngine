# 3. 3D modeling & import

The built-in mesh editor (Modeling window, Edit Mesh mode, UV Editor), the procedural primitive and prop library, the `.okaymesh` polygon format, and everything that brings an outside 3D model in (glTF, FBX, OBJ, optional Assimp formats) up to making an imported model a playable character.
About 3,400 lines in `Modeling/*`, plus the 4,000-line header `Render/Mesh`, the 2,570-line `Render/ModelImport.cpp` and `Render/Gltf`; the editor side lives in `editor/main.cpp` (DrawModeling, DrawUVEditor, the import and character-model helpers).
No open pull request touches `Modeling/*`. The animation PR #554 (Grok's sessions, in flight) makes small edits to `Render/ModelImport` (see 3.7); open items marked "old map" come from the previous feature map and were not re-checked.

## 3. 3D modeling & import

### 3.1 Edit mode
- **What:** Blender-style mesh edit mode (Tab, G/R/S, axis lock, snapping, mirror) on the selected object's Mesh Renderer: Tab to enter, Vertex/Edge/Face selection, G/R/S modal transforms with axis locks, typed values and snapping, soft select, X symmetry and a set of face, edge and vertex tools. The mesh is kept welded while editing.
- **Parts:**
  - 3.1.1 Edit mode, selection and polygon stats - `editor/main.cpp`, `Render/Mesh` - Tab or the Edit Mesh tick, Object/Edit header, keys 1/2/3, click, Shift, Alt-click loop, Ctrl+Alt ring, Grow/Shrink/Linked/Invert, stats line
  - 3.1.2 G/R/S modal transforms - `Modeling/EditTransform` - EditTransformSession, AxisLock (X/Y/Z, Shift = plane), NumericInput, Move/Rotate/ScaleMatrix, Ctrl snap, Esc restores exactly
  - 3.1.3 Soft Select, Symmetry X and vertex snap - `Render/Mesh` - MoveVerticesSoft, SnapToGrid, hold V to snap to vertices
  - 3.1.4 Face tools - `Render/Mesh` - ExtrudeFaces, InsetFaces, SubdivideFaces, PokeFaces, Extrude/Inset Individual, DeleteFaces, FlipNormals, OrientFacesOutward (Recalc Outside), DuplicateFaces, SeparateFaces
  - 3.1.5 Edge and vertex tools - `Render/Mesh` - CollapseEdges, SplitEdges, ExtrudeEdges, BevelVertices, SmoothVertices (Relax Sel), FlattenVertices, ShrinkFatten, SphereizeVertices, WeldVertices (Merge by Distance)
  - 3.1.6 Polygon mesh core - `Modeling/PolyMesh` - quads and n-gons with per-corner UVs, face colour and material slot; FromMesh/ToMesh, Check report, Euler characteristic
  - 3.1.7 Edit-session undo - `editor/main.cpp` - Ctrl+Z steps through single mesh edits; leaving edit mode makes the whole session one scene undo step (per docs)
- **Code:** `Modeling/EditTransform`, `Modeling/PolyMesh`, `Render/Mesh`, `editor/main.cpp`, `editor/EditorState.*`
- **API:** `EditTransformSession`, `AxisLock`, `NumericInput`, `PolyMesh::FromMesh/ToMesh`, `Mesh::ExtrudeFaces`, `InsetFaces`, `OrientFacesOutward`; keys Tab, G, R, S, X/Y/Z, Ctrl+Z
- **Editor:** Modeling window (View > Editors & Tools > Modeling), the 3D view's Object/Edit header and status line, Edit Mesh section
- **Docs:** `docs/modeling.md`
- **Tests:** `edit_transform`, `meshedit`, `polymesh`, `modeling`
- **Status:** Works - four test suites cover the maths and mesh ops; I read the headers but not the editor key handling in main.cpp
- **Open:** tools that rebuild triangles outside the polygon core (the classic Extrude) fall back to one UV per vertex where they touch (per docs); possible doc gap: docs/modeling.md says seams and edge split are "not here yet" but UV seams exist (3.4)
- **Related:** 3.2, 3.4, 3.8, 15.9
- **Words:** edit mode (Tab, G/R/S, axis lock, snapping, mirror), Edit Mesh, Tab, G R S, grab, rotate, scale, extrude, inset, poke, soft select, proportional editing, symmetry X, recalc normals, merge by distance, separate, duplicate faces
- **Seen as:** pressing Tab over the 3D view does nothing; a tool is greyed out; faces look dark or inside out after a weld or mirror; Esc does not put the vertices back; a typed distance goes the wrong way
- **Ask me like:** "make G snap to the grid while Ctrl is held"; "Extrude Faces leaves the collider behind"

### 3.2 Edge and loop tools
- **What:** Edge-loop modeling on the polygon mesh: select loops and rings, cut new loops, slide, bevel, knife, dissolve, bridge, fill. Each tool keeps the numbering of existing vertices so a selection survives it.
- **Parts:**
  - 3.2.1 Loop and ring selection - `Modeling/PolyOps` - EdgeLoop, EdgeRing, OrderChains, PolyTopology; Alt-click loop, Ctrl+Alt-click ring, Select Loop/Ring buttons
  - 3.2.2 Loop cut and edge slide - `Modeling/PolyOps` - LoopCut (Ctrl+R preview, scroll for more cuts), EdgeSlide (G then G)
  - 3.2.3 Bevel - `Modeling/PolyOps` - BevelEdges (Ctrl+B), segments and profile; where three or more bevels meet the corner is closed with one face
  - 3.2.4 Knife, split edge, connect - `Modeling/PolyOps` - KnifeCut (K), SplitEdge, SplitFace (Connect Verts)
  - 3.2.5 Dissolve, merge, bridge and fill - `Modeling/PolyOps` - DissolveEdges, DissolveVerts, MergeVerts, BridgeEdgeLoops, FillFaces, GridFill, CompactVerts
  - 3.2.6 Catmull-Clark subdivision - `Modeling/PolyOps` - CatmullClark(pm, levels) on real polygons (used by the Subdivision modifier)
- **Code:** `Modeling/PolyOps`, `Modeling/PolyMesh`, `editor/main.cpp`
- **API:** `EdgeLoop`, `EdgeRing`, `LoopCut`, `EdgeSlide`, `BevelEdges`, `KnifeCut`, `DissolveEdges`, `BridgeEdgeLoops`, `GridFill`, `CatmullClark`; keys Ctrl+R, Ctrl+B, K
- **Editor:** Modeling window > Edit Mesh > Edge loops group; Ctrl+R / Ctrl+B / K in the 3D view
- **Docs:** `docs/modeling.md`
- **Tests:** `poly_ops`
- **Status:** Works - poly_ops checks counts, closed shells and winding on real geometry; limits listed in Open
- **Open:** Bevel needs edges with a face on both sides; Bridge works on open borders only and does not remove faces first; Edge slide does not adjust UVs so a textured surface stretches; Grid Fill needs an even number of rim edges (all per docs)
- **Related:** 3.1, 3.3, 3.4
- **Words:** loop cut, edge loop, edge ring, edge slide, bevel, chamfer, knife, dissolve, bridge, fill, grid fill, merge at center, Catmull-Clark, subdivision surface
- **Seen as:** a loop cut previews but does not apply; a bevel leaves a hole at a corner; a bridge says the loops need the same edge count; a slid loop stretches the texture
- **Ask me like:** "add chamfer profile to the bevel"; "knife cut across two faces fails"

### 3.3 Modifiers
- **What:** Two kinds of modifiers: one-shot operations that change the mesh at once (array, remesh, decimate, boolean, hull, bisect...), and a non-destructive stack on a Mesh Modifiers component whose result is drawn, collided with and exported while the base mesh stays editable.
- **Parts:**
  - 3.3.1 One-shot modifiers - `Render/Mesh` - Array, VoxelRemesh/Remesh, Decimate, Boolean (Union/Subtract/Intersect), ConvexHull, Bisect, Split in two, ShrinkFatten, Wireframe, Displace, CastToCylinder, Stretch
  - 3.3.2 Non-destructive stack - `Modeling/Modifiers`, `Components/MeshModifiers` - Mirror, Array, Subdivision, Solidify, Bevel, Boolean, Decimate, Displace; enable, reorder, Apply, Apply All
  - 3.3.3 Stack evaluation and caching - `Modeling/Modifiers` - EvaluateModifiers, ApplyModifier, ModifierInputHash (base, settings, Boolean cutter mesh and placement), per-modifier error text; stack saved as `modstack` lines
  - 3.3.4 Whole-mesh deformers - `Render/Mesh` - Mirror (cuts at the plane first), Spherify, Twist, Taper, Bend, Solidify, Relax, Shear, Jitter, Symmetrize
- **Code:** `Modeling/Modifiers`, `Components/MeshModifiers`, `Render/Mesh`, `editor/main.cpp`
- **API:** `MeshModifier::Type`, `MeshModifiers::modifiers`, `Refresh()`, `EvaluateModifiers`, `SameModifiers`; Add Component > Rendering > Modifiers (non-destructive)
- **Editor:** Modeling window > Modifiers tab; Inspector Modifiers section with on/off tick, "..." menu (Move Up/Down, Apply, Remove), Show in edit mode
- **Docs:** `docs/modeling.md`
- **Tests:** `modifiers`, `modeling`
- **Status:** Works - modifiers test covers each type, apply, reorder, Boolean targets and save/reload
- **Open:** Subdivision refuses a result above 1.5 million faces; Boolean is voxel based so cost and detail depend on voxel size (per docs); the modifier list is fixed at 8 types (enum is append-only because scenes store the number)
- **Related:** 3.2, 3.5, 6.2
- **Words:** modifier, Mirror, Array, Subdivision Surface, Solidify, Bevel, Boolean, Decimate, Displace, remesh, cutter, Mesh Modifiers
- **Seen as:** a boolean cut does not follow a moved cutter; a modifier shows red text and is skipped; the collider ignores the modified mesh; Apply bakes the wrong modifier
- **Ask me like:** "add a Weld modifier to the stack"; "the Boolean cutter stays visible"

### 3.4 UV tools
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
- **Docs:** `docs/modeling.md`
- **Tests:** `uv` (test_uv.cpp)
- **Status:** Works - uv test covers per-corner UVs, seams, closed-part cutting, angle seams and packing; docs say a 24k-vertex sphere unwraps in about 3 s
- **Open:** UV convention is v = 0 at the top of the image (documented); tools that rebuild triangles outside the polygon core fall back to one UV per vertex
- **Related:** 3.1, 3.5, 2.2
- **Words:** UV, unwrap, seam, LSCM, Smart Unwrap, pack islands, texel density, UV editor, planar, box projection, tile
- **Seen as:** texture stretched or mirrored on a modeled part; seams not where expected; islands overlap after packing; texture lost after loop cut
- **Ask me like:** "pack UV islands without shrinking them"; "the UV Editor should show a checker when there is no texture"

### 3.5 Mesh assets and primitives
- **What:** The triangle Mesh type the renderer and physics use, its built-in primitives, the `.okaymesh` text format that keeps real polygons, and how edited meshes are saved (by name, by file path, or verbatim in the scene).
- **Parts:**
  - 3.5.1 Triangle Mesh and primitives - `Render/Mesh` - Cube, Sphere, Cylinder, Cone, Pyramid, Wedge, Quad, Plane, Tube, Torus, TorusKnot, Capsule, Icosphere, Grid, Hemisphere, Stairs, Gear, Prism, Octahedron, Disc, Tetrahedron, Bipyramid, RoundedBox
  - 3.5.2 `.okaymesh` file format - `Modeling/PolyMesh` - Serialize/Parse/Save/Load: verts, faces with any corner count, optional per-corner UVs, colour, material slot
  - 3.5.3 Mesh asset load, save, hash - `Modeling/MeshAsset` - LoadMeshAsset (fallback to the scene folder in exported games), SaveMeshAsset, GeometryHash
  - 3.5.4 Link and write-back - `editor/EditorState.*`, `editor/main.cpp` - Save as Mesh Asset, shared files, SaveEditedMeshAssets on Ctrl+S, Build Game copies the file to Data
  - 3.5.5 Saved forms of a mesh - `Render/Mesh` - primitive by name, file by path, hand-edited verbatim (`meshgeo` in the scene), `.obj`-backed edits kept in the scene
  - 3.5.6 OBJ export and import helpers - `Render/Mesh` - SaveOBJ, LoadOBJ (UVs, vn normals), RecenterToOrigin, ScaleToFit, NormalizeImportScale
- **Code:** `Render/Mesh`, `Modeling/MeshAsset`, `Modeling/PolyMesh`, `editor/EditorState.*`
- **API:** `Mesh::Cube/Sphere/...`, `Mesh::FromName`, `LoadMeshAsset`, `SaveMeshAsset`, `GeometryHash`, `MeshRenderer::meshPath`; Inspector "Mesh File" field
- **Editor:** GameObject > 3D Object menu (every primitive); Modeling > Import tab and Edit Mesh > Save as Mesh Asset; Mesh Renderer inspector
- **Docs:** `docs/modeling.md`
- **Tests:** `mesh3d`, `mesh_asset`, `mesh_variant_hash`, `meshedit`, `polymesh`, `obj_import`
- **Status:** Partial - primitives and file round-trips are tested; per-face materials are not built and the inside-out claim below is unconfirmed
- **Open:** M5b per-face materials: PolyFace has a material slot but Mesh has no per-triangle slot; old map says Sphere, Cylinder, Cone, Tube, Torus, TorusKnot and Capsule can be inside out (possible bug, unverified: mesh3d checks normals "regardless of winding")
- **Related:** 3.1, 3.6, 2.1, 2.2, 1.5
- **Words:** mesh, primitive, okaymesh, Mesh File, meshPath, OBJ, wedge, gear, icosphere, quad, polygon mesh, mesh asset
- **Seen as:** a primitive is dark or inside out; an edited mesh reverts after reopening the scene; the exported game cannot find the mesh file; every object using the shared file changes at once
- **Ask me like:** "support per-face materials in .okaymesh"; "why is the Cone inside out"

### 3.6 Model import
- **What:** One entry point turns a model file into a merged mesh or a scene hierarchy with materials, skeleton, skins, clips and morph targets, then prints an import report. Built in: OBJ, glTF/GLB and FBX (vendored ufbx); other formats only when the build has Assimp.
- **Parts:**
  - 3.6.1 glTF 2.0 and GLB - `Render/Gltf`, `Render/ModelImport` - own JSON and base64 reader, accessors including sparse and quantized, up to 8 influences (4 kept), matrix nodes decomposed, STEP/LINEAR/CUBICSPLINE keys, multi-material skins
  - 3.6.2 FBX - `Render/ModelImport` - built in through the vendored ufbx (`engine/third_party/ufbx`): Y-up metres, skins, blend-shape channels, takes as clips, PBR and FBX materials
  - 3.6.3 OBJ - `Render/Mesh`, `Render/ModelImport` - LoadOBJ with UVs and authored normals, material texture path
  - 3.6.4 Optional Assimp formats - `Render/ModelImport` - DAE, STL, PLY, 3DS, BLEND and more, only with OKAY_USE_ASSIMP (ON for the Windows dist build, OFF by default and for the web build)
  - 3.6.5 Scene import and report - `Render/ModelImport` - ImportModelScene (one object per node, skinned parts per material), AnalyzeModel/ModelReport (meshes, bones, weights, clips, warnings)
  - 3.6.6 Materials and textures - `Render/Gltf`, `Render/ModelImport` - albedo, normal, specular, AO, emissive; embedded images extracted to a sidecar file; packed metallic-roughness and emissive textures are not imported
  - 3.6.7 Blend shapes (morph targets) - `Components/BlendShapes` - sparse per-shape offsets, weight sliders, filter box, glTF targets and FBX channels, `weights.<n>` clip tracks, applied before skinning
  - 3.6.8 Import UI and extra animation files - `editor/main.cpp`, `editor/MainShared.hpp` - File > Import Model..., Project panel drop, Add animation files (Mixamo FBX, glTF; DAE/BVH are named in the dialog)
- **Code:** `Render/ModelImport`, `Render/Gltf`, `Render/Mesh`, `Components/BlendShapes`, `editor/MainShared.hpp`, `editor/main.cpp`
- **API:** `ImportModel`, `ImportModelScene`, `AnalyzeModel`, `ImportableExtensions`, `AssimpAvailable`, `LoadGLTF`, `AddAnimationsFromFile`, `BlendShapes`; script `blend_shape("Smile", 0.8)`, `blend_shape_get`
- **Editor:** File > Import Model..., Project panel (double-click, drag into viewport), Console import report, Blend Shapes inspector
- **Docs:** `docs/modeling.md`, `docs/animation.md` (imported characters, blend shapes, Tips)
- **Tests:** `gltf_import`, `gltf_cubicspline`, `model_import_rig`, `blend_shapes`, `obj_import`, `loaders_robust`
- **Status:** Partial - glTF and OBJ are well tested; no ctest loads an FBX or an Assimp format (I grepped tests/), so those paths are unexercised by the suite
- **Open:** possible doc bug: docs/animation.md Tips says FBX animations import via Assimp, but FBX is read by the built-in ufbx path; packed metallic-roughness and emissive textures not imported; BVH is named in the Add-animations dialog but no BVH reader found in engine/src (works only if the Assimp build reads it; unverified); sample Mixamo menu items download from GitHub with curl on first use
- **Related:** 3.7, 5.5, 5.2, 20.4, 2.2
- **Words:** import, glTF, GLB, FBX, OBJ, DAE, Collada, Assimp, ufbx, skin, bones, weights, morph target, blend shape, shape key, Mixamo, import report, textures
- **Seen as:** imported model is white or has no textures; model faces the wrong way or is a tiny or huge size; character stays in a T-pose; faces dark; Console warns about missing bones or unweighted vertices; FBX will not import in a default build
- **Ask me like:** "import FBX takes as separate clips"; "support the metallic-roughness texture from glTF"

### 3.7 Rigging and playable setup
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
- **Editor:** Hierarchy right-click > Rig Model (Humanoid) / Map Humanoid Bones... / Use Built-in Animations; Project panel right-click > Make Playable Character; Character Model panel (Set Character Model..., Fix Facing, Turn 180/90, Fit Collider); GameObject > Player > Mixamo Soldier / Xbot
- **Docs:** `docs/animation.md`, `docs/ik.md`
- **Tests:** `make_playable`, `character_model_setup`, `imported_glitches`, `rig_sweep`, `humanoid_bones`, `collider_fit`
- **Status:** Works - make_playable, character_model_setup and rig_sweep (11 rigs) exist; In flight: #554 adds PlayerSetup (a status read-out and a "wire missing animation and IK" step on the controller panel) and small ModelImport.hpp edits
- **Open:** old map: one-click humanoid import on any rig; auto-rig expects an upright model facing +Z in a T/A pose; a model with fewer than 8 of the 15 humanoid bones is not set up; in flight in #554 (Grok): PlayerSetup/WirePlayerAnimation
- **Related:** 3.6, 4.1, 4.9, 5.3, 5.4, 5.6
- **Words:** rig, auto-rig, Rig Model, Make Playable Character, Set Character Model, humanoid, avatar, Mixamo, VRoid, T-pose, bone map, Fix Facing, Fit Collider
- **Seen as:** imported character is in a T-pose or faces sideways; collider does not wrap the model; two players after Make Playable; model floats or sinks; wrong animation in a slot; "rigging did nothing"
- **Ask me like:** "make a Mixamo character playable in one click"; "the model walks backwards after Set Character Model"

### 3.8 Sculpt and paint brushes
- **What:** Viewport brushes on an edited mesh: sculpt (Grab, Inflate, Smooth, Flatten, Pinch) and per-face colour painting that multiplies with the material colour.
- **Parts:**
  - 3.8.1 Sculpt brush - `Render/Mesh` - SculptBrush with radius, strength, Symmetry X; subdivide first for detail
  - 3.8.2 Paint brush and fill - `Render/Mesh` - EnsureFaceColors, PaintFaces, FillFacesColor, MottleFaceColors, Clear Paint; saved with the scene
- **Code:** `Render/Mesh`, `editor/main.cpp`
- **API:** `Mesh::SculptBrush`, `Mesh::PaintFaces`, `FillFacesColor`
- **Editor:** Modeling > Edit Mesh > Sculpt Brush and Paint Brush ticks
- **Docs:** `docs/modeling.md`
- **Tests:** `meshedit`, `mesh3d`
- **Status:** Basic - brush maths are in the Mesh header and docs describe them; I found no test that names sculpt or paint specifically
- **Open:** no per-vertex weight or texture painting; paint colours are per face only
- **Related:** 3.1, 2.2
- **Words:** sculpt, brush, inflate, grab, pinch, flatten, paint, vertex colour, face colour
- **Seen as:** brush does nothing on a coarse mesh; paint colour is muddy because it multiplies with the material
- **Ask me like:** "add a Crease brush"; "make paint strokes undoable one by one"

### 3.9 Generate and whole-mesh operations
- **What:** Profile-driven shapes (lathe, screw, extrude outline, pipe along a path) and the one-click whole-mesh operations in the Modeling window's Edit tab.
- **Parts:**
  - 3.9.1 Lathe and Screw - `Render/Mesh` - Mesh::Lathe, Mesh::Screw from a (radius, height) point list; Vase, Goblet, Column presets
  - 3.9.2 Extrude Outline and Pipe - `Render/Mesh` - Mesh::Extrude, Mesh::SweepPath with twist-free frames (Straight, Arc, Helix, S-Curve, Zigzag presets)
  - 3.9.3 Subdivide, smooth, relax, fill, grid snap - `Render/Mesh` - Subdivide, Smooth, SubdivideSmooth, Smooth/Relax, FillHoles, BridgeLoops, LoopCut (axis), SnapToGrid, AutoSmooth (ComputeAutoSmoothNormals)
- **Code:** `Render/Mesh`, `editor/main.cpp`
- **API:** `Mesh::Lathe`, `Mesh::Screw`, `Mesh::Extrude`, `Mesh::SweepPath`, `Mesh::FillHoles`
- **Editor:** Modeling window > Generate and Edit tabs
- **Docs:** `docs/modeling.md`
- **Tests:** `mesh3d`, `modeling`
- **Status:** Works - geometry builders covered by mesh3d and modeling; I did not read the editor panels
- **Open:** none known
- **Related:** 3.3, 3.5
- **Words:** lathe, screw, extrude outline, pipe, sweep, vase, spring, auto smooth, subdivide, fill holes
- **Seen as:** pipe twists at bends; lathe shape is inside out; Fill Holes leaves a gap
- **Ask me like:** "add a Loft between two profiles"; "pipe along a custom spline"

### 3.10 Add library and procedural props
- **What:** The Modeling window's Add library and GameObject > 3D Object menu: 23 primitives plus pre-coloured nature, prop and structure meshes, with procedural variants (one number per object regenerates the exact shape) and Shape tab collision modes.
- **Parts:**
  - 3.10.1 Nature props - `Render/Mesh` - Tree, Pine, PalmTree, DeadTree, Bush, Rock, Mushroom, Cactus, Crystal with Variant stepper and Randomize; terrain Scatter gives every instance its own variant
  - 3.10.2 Props and structures - `Render/Mesh` - Barrel, Crate, Fence, Well, StreetLamp, Campfire, Lantern, Chest, Signpost, Cart, Tent, Bench, House, Tower, Windmill, Bridge, RockArch
  - 3.10.3 Variant randomness - `Render/Mesh` - FromNameSeeded, NameHasVariants, PropRand (test comment: it once overflowed signed ints for variant >= 6; values must stay identical)
  - 3.10.4 Shape tab and collider refit - `Physics/ColliderFit`, `editor/main.cpp` - Box (bounds) or Mesh (exact) collision, colliders refit when the mesh changes, Add Physics
- **Code:** `Render/Mesh`, `Physics/ColliderFit`, `editor/main.cpp`
- **API:** `Mesh::FromName`, `Mesh::FromNameSeeded`, `Mesh::Tree(variant)`, `ColliderFit`, `MeshRenderer`
- **Editor:** Modeling window > Add (filter box, categories) and Shape tab; GameObject > 3D Object > Nature / Props / Structures
- **Docs:** `docs/modeling.md`
- **Tests:** `mesh3d`, `mesh_variant_hash`, `collider_fit`
- **Status:** Works - variants and collider fit have dedicated tests; looks of each prop are not covered by tests
- **Open:** none known
- **Related:** 3.5, 2.6, 6.2
- **Words:** nature, tree, rock, props, barrel, crate, house, variant, randomize, scatter, collision mode, mesh collider, box collider
- **Seen as:** every tree in the forest looks the same; collider does not match after Extrude; prop is a flat plastic colour
- **Ask me like:** "add a Boulder variant to Rock"; "refit the collider after an edit"
