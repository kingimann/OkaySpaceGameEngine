# 43-58. Proposed areas

These are new areas the owner did not ask for yet: things a complete engine and editor usually has that areas 1-42 do not cover. Each was checked against the code first, so each says what already exists (often more than the old list assumed) and what is missing. Nothing here is built unless it says so.
The owner decides which to adopt, reorder or drop; the suggested priority is only a starting point. Verified against origin/main as of 2026-11-09 (up to #587 and #585 merged; a number after a part is the PR that added it). The loop and time passes (#581, #585) already build part of 46 (replay), 50.3 (video capture) and 58 (frame statistics); nothing in this file is In flight any more.
Things that turned out to exist already (world streaming, crash reports, Steam and PlayFab cloud calls, a launcher template gallery, a Git menu, a Terrain scatter button) are Partial or Basic here, not Planned. Numbers 43 to 58 are proposals; sub-area IDs follow the same rules as the rest of the map.

## 43. 2D tools

### 43.1 Tilemaps and tilesets
- **What:** Paint 2D levels with tiles taken from a picture (a tileset), with auto-tiling edges, layers and a brush, instead of coloured squares.
- **Parts:**
  - 43.1.1 Tilemap data - `Components/Tilemap` - grid of integer tile ids with cell and world conversion; saved in scene files as a `tilemap` record
  - 43.1.2 Tilemap collider - `Components/TilemapCollider2D` - builds static boxes from solid tiles, merging runs in a row
  - 43.1.3 Script editing - `Scripting/OkayScriptVM` - `set_tile`, `get_tile`, `tile_resize`, `tile_w`, `tile_h` on a sibling Tilemap
  - 43.1.4 Drawing - `player/main.cpp`, `editor/main.cpp` - each non-empty cell is drawn as a coloured square chosen by id (`TileColor`); there is no tileset image
- **Code:** `Components/Tilemap`, `Components/TilemapCollider2D`, `Scripting/OkayScriptVM`, `player/main.cpp`, `editor/main.cpp`
- **API:** `Tilemap::Resize`, `SetTile`, `GetTile`, `CellToWorld`, `WorldToCell`; script `set_tile(x, y, id)`
- **Editor:** GameObject > Create Tilemap (a 12-tile ground row), Add Component > Tilemap Collider 2D; no tile painting window or Inspector section (searched `editor/main.cpp`)
- **Docs:** `docs/scripting.md` (tile built-ins)
- **Tests:** `tilemap_collision`, `script_tilemap`, `particles_tilemap`
- **Status:** Basic - ids, collision and script editing work; tiles cannot be textured or painted in the editor.
- **Exists today:** All of Parts. Levels are built by script or by code.
- **Suggested priority:** High - the 2D templates (Platformer, Top-Down, Snake) are popular and a tile level cannot be drawn or textured today.
- **Open:** first step: a tileset asset (image, tile size, collision flags) and draw tiles from it in the player and editor renderers; then a paint window with brush, fill, rectangle and eraser; then auto-tile rules; one tilemap is one layer.
- **Related:** 4.3, 6.1, 2.1, 15.6, 43.2
- **Words:** tilemap, tileset, tile palette, tile painting, autotile, rule tile, tile layers, grid level, TilemapCollider2D
- **Seen as:** a 2D level is made of flat coloured squares; no way to draw a level with my art; cannot paint tiles in the editor
- **Ask me like:** "let me paint tilemaps with a tileset image"; "add auto-tiling for ground edges"

### 43.2 Sprite atlas and slicing
- **What:** Cut a sprite sheet into frames you can name and reuse, pack many sprites into one atlas, and edit pivots.
- **Parts:**
  - 43.2.1 Sprite sub-region - `Components/SpriteRenderer` - `uvMin` and `uvMax` pick part of a texture; sorting layer and order, flip, pixel filter
  - 43.2.2 Atlas animation - `Components/SpriteAnimator` - `atlasColumns`, `atlasRows`, `atlasCount`, `rowOverride` walk a regular grid; named clips
  - 43.2.3 Sprite-sheet character dialog - `editor/main.cpp` - "2D Character from a sprite sheet": pick an image and a columns by rows grid, get an animated character in one step
  - 43.2.4 Pixel-art Sprite Editor - `editor/main.cpp` - pencil, eraser, fill, eyedropper on a small canvas; saves PNGs into `Assets/Sprites`
- **Code:** `Components/SpriteRenderer`, `Components/SpriteAnimator`, `editor/main.cpp`
- **API:** `SpriteRenderer::uvMin`, `uvMax`, `SpriteAnimator::atlasColumns`
- **Editor:** View > Editors & Tools > Sprite Editor; GameObject > 2D Character (Sprite Sheet)...
- **Docs:** `docs/scripting.md` (`play_anim`)
- **Tests:** `spriteanim`, `2d_expand`
- **Status:** Basic - regular grids only; no irregular slicing, no packing, no pivot or trim data.
- **Exists today:** Everything in Parts. A sprite is one texture file or one cell of an equal grid.
- **Suggested priority:** Medium - fine for simple sheets; real 2D art packs use irregular frames and pivots.
- **Open:** first step: a Sprite asset (texture, rectangle, pivot, name) that the Inspector picks, plus a slicing window (grid, by cell size, automatic by transparency); atlas packing for builds comes later.
- **Related:** 43.1, 43.3, 20.4, 2.9
- **Words:** sprite atlas, sprite sheet, slice, pivot, texture packer, SpriteRenderer, atlas columns, sprite editor, nine-slice sprite
- **Seen as:** a sprite sheet with different sized frames cannot be used; sprite pivot is always the centre
- **Ask me like:** "add a sprite slicing window that finds frames automatically"

### 43.3 Sprite animation tools
- **What:** Make and edit flip-book animations for sprites with a timeline, events and previews, and state changes between clips.
- **Parts:**
  - 43.3.1 Flip-book animator - `Components/SpriteAnimator` - frame list or atlas cells at a frame rate, loop flag, named clips switched with `Play(name)`
  - 43.3.2 Script control - `Scripting/OkayScriptVM` - `play_anim()`, `stop_anim()`
  - 43.3.3 Skeletal and frame events - `-` - not built (the Animator and timeline windows work on 3D clips, area 5)
- **Code:** `Components/SpriteAnimator`, `Scripting/OkayScriptVM`
- **API:** `SpriteAnimator::Play`, `frames`, `fps`, `clips`
- **Editor:** SpriteAnimator Inspector section
- **Docs:** `docs/scripting.md`
- **Tests:** `spriteanim`
- **Status:** Partial - playing clips works; there is no sprite animation editor, no events and no blending to a state machine for sprites.
- **Exists today:** The component and its clips list. The Animator state machine (5.1) drives 3D characters and was not checked for sprites.
- **Suggested priority:** Medium - needed for any serious 2D character, after tilesets.
- **Open:** first step: a Sprite Animation window with a frame strip, per-frame duration and animation events (call a script function on frame N); 2D skeletal animation is a much larger later step.
- **Related:** 43.2, 5.1, 5.2, 4.3
- **Words:** sprite animation, flipbook, animation clip, frame events, 2D skeletal animation, SpriteAnimator, animation window 2D
- **Seen as:** cannot time a footstep sound to a sprite frame; animations are set by typing frame paths
- **Ask me like:** "add a sprite animation editor with frame events"

### 43.4 2D lighting
- **What:** Lights, shadows and normal maps that affect sprites, for moody or dynamic 2D scenes.
- **Parts:**
  - 43.4.1 Light components - `Components/Light` - point, spot and directional lights exist for 3D meshes
  - 43.4.2 Sprites are unlit - `Components/SpriteRenderer` - no light, normal map or shadow fields on sprites
- **Code:** `Components/Light`, `Components/SpriteRenderer`
- **API:** none for 2D
- **Editor:** none
- **Docs:** `docs/lighting.md` (3D)
- **Tests:** `lighting`
- **Status:** Planned - nothing lights sprites.
- **Exists today:** 3D lighting and shadows only.
- **Suggested priority:** Low - a visual upgrade, not needed to make 2D games.
- **Open:** first step: an unlit-versus-lit flag and a simple 2D point light with falloff that tints sprites in the software renderer first (the reference), then GL and D3D11.
- **Related:** 2.3, 43.2
- **Words:** 2D lights, sprite lighting, normal maps for sprites, light 2D, shadow casters 2D, torch light, ambient 2D
- **Seen as:** a dark cave level looks the same as a bright one; torches do nothing to sprites
- **Ask me like:** "add 2D point lights that light sprites"

## 44. Level design and blockout tools

### 44.1 Blockout and grid placement
- **What:** Build rough level layouts fast from simple shapes on a grid with consistent units, then replace them with real art.
- **Parts:**
  - 44.1.1 Primitives - `Render/Mesh`, `editor/main.cpp` - GameObject menu cubes, planes, spheres, cylinders and more with `Mesh::FromName`
  - 44.1.2 Grid snap - `editor/main.cpp`, `editor/SurfaceSnap.*` - the toolbar Snap toggle and a move step (Ctrl while dragging, Alt to switch it off for a drag), Edit > Snap Position to Grid, script `grid_snap`
  - 44.1.5 Rotation and scale snap - `editor/SurfaceSnap.*`, `editor/main.cpp` (`DrawViewport`, `DragVec3Axis`) - one settings object holds the move, rotate (1, 5, 15, 45, 90 or custom degrees) and scale steps, shown in the Snap popup and an on-canvas chip, saved in `okay_settings.txt`; the rotate rings, scale handles, mesh-edit scale and the Inspector Position, Rotation and Scale fields use the same steps; Edit > Snap Rotation and Scale to Steps
  - 44.1.6 Surface snap - `editor/SurfaceSnap.*`, `editor/main.cpp` (`PlaceOnSurfaceUnderCursor`), `EditorState::spawnPlaceHook` - Move-handle drags (the Surf toggle, or Ctrl+Shift for one drag) put the object on the surface under the cursor, with optional align-to-normal, rest-on-surface and an offset along the normal; the same options place new primitives and players, duplicates and dropped assets (Place new objects on the surface); Edit > Drop to Surface. Hits colliders and terrain exactly, mesh triangles for meshes without a solid collider (a MeshCollider is only a box), and render bounds for very large meshes; skips the dragged object and its children, hidden and inactive objects and triggers
  - 44.1.7 Align, distribute and mirror - `editor/SelectionTools.*`, `editor/main.cpp` (`DrawAlignMirrorMenu`, `DrawViewport` gizmo block) - Edit > Align / Distribute / Mirror, the Hierarchy right-click menu and the Scene toolbar Align popup: align X, Y, Z to the min, centre or max of the selection or to the active object (world-space renderer or collider boxes), distribute 3 or more objects by equal gaps or equal centre spacing, mirror across a world plane through the selection centre, the active pivot or the origin as a copy or in place (flips the mesh and its winding instead of a negative scale; children, splines, collider offsets and sprites follow); the Move, Rotate and Scale gizmos apply to every selected object about the active object or the selection centre (toolbar Pivot / Center button); greyed-out items say why; one undo step each
  - 44.1.3 Edit Mesh - `editor/main.cpp`, `Modeling/PolyOps` - select, move, extrude, inset, subdivide and other mesh operations on a selected mesh (area 3)
  - 44.1.4 Player-facing builders - `Components/BlockBuilder`, `Components/StructureBuilder`, `Components/BuilderMode` - block and structure placement for in-game building (a runtime feature, not an editor tool)
- **Code:** `Render/Mesh`, `editor/main.cpp`, `editor/SurfaceSnap.*`, `editor/SelectionTools.*`, `Modeling/PolyOps`, `Components/BlockBuilder`, `Components/StructureBuilder`, `Components/BuilderMode`
- **API:** `Mesh::FromName`, `okay::editor::snap::Cfg()`, `RaycastSurface`, `ComputeSurfacePose`, `BlockBuilder`
- **Editor:** GameObject menu, toolbar Snap and Surf, Snap settings popup (arrow beside the magnet), Edit menu (Snap Position to Grid, Snap Rotation and Scale to Steps, Drop to Surface, Align / Distribute / Mirror), toolbar Pivot / Center and Align buttons, 3D Modeling window
- **Docs:** `docs/modeling.md`, `docs/build_mode.md`
- **Tests:** `mesh3d`, `poly_ops`, `builder_mode`, `block_builder`, `surface_snap`, `selection_tools`
- **Status:** Basic - shapes, move, rotation and scale snapping and surface snapping work; no blockout kit.
- **Exists today:** All of Parts. Rotation snap (15 degree detents) and scale snap existed already in the gizmos; this area now also has user-set rotate and scale steps, saved snap settings and surface snap. There is no dedicated greybox kit with a fixed unit size and no snapping to neighbouring pieces (edge or vertex of another object's piece; V vertex snap moves mesh vertices only). Surface snap is not applied to the 2D view or to UI widgets, and its align-to-normal turns only the primary object of a multi selection (the others follow by the same move; see 44.1.7).
- **Suggested priority:** Medium - speeds up every 3D level, mostly editor UI work on existing pieces.
- **Open:** first step: a Blockout palette (floor, wall, ramp, stairs, door frame at 1 m units) that snaps to grid and to neighbouring pieces, with rotation snap in 15 degree steps.
- **Related:** 3.1, 3.5, 15.3, 15.6, 44.2
- **Words:** blockout, greybox, whitebox, level design, align objects, distribute objects, mirror objects, flip objects, gizmo pivot, grid snap, snap to ground, snap to surface, drop to surface, align to normal, rotation snap, scale snap, snap steps, vertex snap, primitives
- **Seen as:** walls do not line up; placing things at exact distances is fiddly; objects float above the ground
- **Ask me like:** "add a blockout palette with snapping walls and floors"

### 44.2 Prefab brush and placement
- **What:** Paint or stamp many prefab instances into a level (props, trees, lamps) with random rotation, scale and spacing.
- **Parts:**
  - 44.2.1 Drop from Project - `editor/main.cpp` - drag a prefab into the scene to place one copy
  - 44.2.2 Terrain scatter and foliage brush - `editor/LevelTools.*`, `Components/Foliage` - scatter built-in props over a terrain, or paint, erase and rescale a mesh, prefab or built-in prop on a terrain or any surface (see 29.2)
  - 44.2.3 Spline instancer - `Components/SplineMesh` - copies of an object or prefab along a curve
  - 44.2.4 Plugin example - `editor/main.cpp` - the New Plugin templates include a "scatter copies of the selected object" script
- **Code:** `editor/main.cpp`, `editor/LevelTools.*`, `Components/Foliage`, `Components/SplineMesh`
- **API:** `SplineInstancer`, `Foliage`
- **Editor:** Project panel drag and drop, Terrain scatter, Foliage Inspector brush, Plugins > New Plugin
- **Docs:** `docs/splines.md`, `docs/editor-plugins.md`, `docs/level_tools.md`
- **Tests:** `splines`, `foliage`
- **Status:** Works for one plant kind per layer - the foliage brush paints a prefab, mesh file or built-in prop over a terrain or any surface with density, slope and height limits.
- **Exists today:** The four pieces above. A layer holds one kind; a mix needs several layers.
- **Suggested priority:** Medium - shared with the foliage brush in 29.2; build one tool for both.
- **Open:** a weighted list of several prefabs in one brush, and colliders for painted copies.
- **Related:** 29.2, 21.1, 15.3, 22.1
- **Words:** prefab brush, paint objects, scatter, stamp, place many, props, random rotation, align to surface
- **Seen as:** placing 100 rocks one by one; copies all face the same way
- **Ask me like:** "add a prefab brush that paints props on surfaces"

### 44.3 Measuring and level metrics
- **What:** Rulers, size labels and reference figures so a designer can judge distances, jump heights and room sizes.
- **Parts:**
  - 44.3.1 Stats panel - `editor/main.cpp` - object and triangle counts only
  - 44.3.3 Measure tool - `editor/LevelTools.*` - View options > Measure tool (or M): click two points on the surfaces in the 3D view and read the distance, the distance across the ground and the height difference
  - 44.3.2 Controller numbers - `Components/PlayerController` - `jumpForce` and speeds exist as fields but no jump arc or reach is drawn as a guide
- **Code:** `editor/main.cpp`, `editor/LevelTools.*`, `Components/PlayerController`
- **API:** `RaycastSurfaces`
- **Editor:** Scene view > View options > Measure tool
- **Docs:** `docs/level_tools.md`
- **Tests:** `foliage` (the surface ray cast it uses)
- **Status:** Basic - the measure tool works in the real editor; no scale reference figure and no metrics overlay.
- **Exists today:** The measure tool, counts and the world axes toggle (`g_showWorldAxes`).
- **Suggested priority:** Low - useful but small.
- **Open:** first step: a reference-figure toggle showing a 1.8 m person and the player's jump arc.
- **Related:** 4.1, 4.2, 15.3, 44.1
- **Words:** measure tool, ruler, scale reference, jump arc, level metrics, distance label, player height reference
- **Seen as:** cannot tell if a gap is jumpable; rooms feel too big or small
- **Ask me like:** "add a measure tool to the Scene view"

### 44.4 Volumes and zones
- **What:** The first step of this proposal is built (11.12): one region shape with checkpoint, kill, force, atmosphere and ambience kinds. What remains is the zone kinds that have nothing under them yet.
- **Parts:**
  - 44.4.1 Trigger colliders - `Physics/Collider3D`, `Physics/Collider2D` - `isTrigger` colliders send enter and exit events
  - 44.4.2 Survival zone - `Components/SurvivalZone` - a trigger volume that applies cold, radiation, water and other effects while inside
  - 44.4.3 Spawners and logic links - `Components/Spawner`, `Components/LogicLinks` - spawn areas and wired triggers (the plate and trigger zone now show as volumes, see 11.12.5)
  - 44.4.4 Checkpoint trigger (#572) - `Core/SaveFramework` - saves a checkpoint when the player comes near
- **Code:** `Physics/Collider3D`, `Physics/Collider2D`, `Components/SurvivalZone`, `Components/Spawner`, `Components/LogicLinks`, `Components/Volume`
- **API:** `Collider3D::isTrigger`, `OnTriggerEnter3D`, `SurvivalZone`, `Volume`
- **Editor:** Collider Inspector "Trigger" box; GameObject > Volume (11.12.5)
- **Docs:** `docs/survival_kit.md`, `docs/volumes.md`
- **Tests:** `physics_callback_removal`, `survival`, `volumes`
- **Status:** Partial - 11.12 covers checkpoint, kill, force, fog and sound zones; the rest below is not built.
- **Exists today:** Trigger colliders, the Survival Zone, spawners and the Volume component (11.12).
- **Suggested priority:** Low - the common zones exist.
- **Open:** a reverb zone (needs a reverb effect in the audio mixer); zones for post effects other than fog and vignette (needs per-scene settings for them); a navigation-area volume (needs area costs in the navmesh); convex region shapes; a spawn-area volume (the Spawner has a radius only); a music zone that switches tracks
- **Related:** 6.2, 11.4, 11.6, 11.12, 35.2
- **Words:** trigger volume, zone, spawn area, area trigger, volume, music zone, reverb zone, isTrigger, SurvivalZone
- **Seen as:** need a reverb zone for a cave; need a navigation area that costs more
- **Ask me like:** "add a reverb zone for the cave"

## 45. Open-world streaming and world partition

### 45.1 Cell authoring and baking
- **What:** Split one big scene into streaming cells and manage them, so open worlds can be edited as one map.
- **Parts:**
  - 45.1.1 Streamer component - `Components/WorldStreamer` - loads cells named `<prefix>_<x>_<z>.okayprefab` within a radius of the camera; hysteresis between load and unload radius
  - 45.1.2 Additive scene merge - `Scene/SceneSerializer` - `MergeFromText` with an offset places scene "chunks" side by side
  - 45.1.3 Cell editor - `-` - not built (cells are written by hand today)
- **Code:** `Components/WorldStreamer`, `Scene/SceneSerializer`
- **API:** `WorldStreamer` (`cellSize`, `loadRadius`, `unloadRadius`, `folder`, `prefix`), `SyncNow`, `LoadedCells`
- **Editor:** WorldStreamer Inspector section
- **Docs:** `docs/scene_management.md`
- **Tests:** `worldstreamer`
- **Status:** Partial - runtime streaming works; authoring is manual.
- **Exists today:** The streamer, as above. It assumes each cell prefab already holds objects at final world positions.
- **Suggested priority:** Medium - open worlds are a stated goal of the terrain tools; this is the missing workflow.
- **Open:** first step: "Split scene into cells" (grid size, object assignment by position, writes cell prefabs and a manifest) and a Scene view overlay of cell borders; overlaps 27.4 on the runtime side.
- **Related:** 27.4, 2.6, 29.1, 15.6
- **Words:** world partition, cells, chunks, split scene, open world, streaming cells, world streamer, bake cells
- **Seen as:** a huge map in one scene is slow to edit and load; cells have to be made by hand
- **Ask me like:** "add a Split Scene Into Cells command"

### 45.2 Large world coordinates
- **What:** Keep things precise and smooth very far from the origin (physics and rendering jitter at large distances).
- **Parts:**
  - 45.2.1 Single precision positions - `Math/Vec3`, `Scene/Transform` - float vectors throughout
  - 45.2.2 Double-precision time only - `Core/Time` - elapsed time is double precision (#557); no Math header uses double
- **Code:** `Math/Vec3`, `Scene/Transform`
- **API:** none
- **Editor:** none
- **Docs:** `docs/core_audit_notes.md` (lists double-precision variants for large worlds as a recommendation; header search only)
- **Tests:** none
- **Status:** Planned - no floating origin or large-world mode found.
- **Exists today:** Float positions everywhere in Math. Searched the engine, editor and docs for "floating origin" and found nothing.
- **Suggested priority:** Low - only matters for worlds many kilometres across.
- **Open:** first step: measure where jitter starts in a test scene; then a floating-origin option that shifts the whole scene back when the camera passes a distance.
- **Related:** 45.1, 6.4, 1.3
- **Words:** large world, floating origin, origin shift, precision, jitter far from origin, world coordinates
- **Seen as:** objects shake far from the centre; physics gets unstable at big distances
- **Ask me like:** "add a floating origin option for huge worlds"

### 45.3 Streaming-aware systems
- **What:** Make navigation, physics, AI and saves behave correctly while parts of the world come and go.
- **Parts:**
  - 45.3.1 Navmesh - `AI/NavMeshSurface` - bake and queries on loaded geometry; no per-cell navmesh
  - 45.3.2 Saves - `Core/SaveGame` - save the whole scene; state of unloaded cells is not kept (see 35.3)
  - 45.3.3 Agents far away - `Components/NavMeshAgent` - agents in unloaded cells do not exist
- **Code:** `AI/NavMeshSurface`, `Core/SaveGame`, `Components/NavMeshAgent`
- **API:** none
- **Editor:** none
- **Docs:** `docs/navigation.md`
- **Tests:** `navmesh`
- **Status:** Planned - the systems assume everything is loaded.
- **Exists today:** Nothing cell-aware (checked the headers for streaming mentions).
- **Suggested priority:** Medium - without this an open world cannot have persistent NPCs or saves.
- **Open:** first step: per-cell navmesh bake stored with the cell and stitched at load; cell-level state records for saves.
- **Related:** 9.1, 35.3, 27.4, 45.1
- **Words:** streaming navmesh, persistent world state, unloaded cells, far agents, world state save
- **Seen as:** NPCs vanish when I walk away; a door I opened is closed again after returning
- **Ask me like:** "make saves remember the state of cells that are unloaded"

### 45.4 Distant scenery
- **What:** Cheap stand-ins for faraway objects (impostors, merged far meshes, terrain horizon) so the view distance can be large.
- **Parts:**
  - 45.4.1 Fog and sky - `Render/Atmosphere` - fog hides the far plane (area 2.4)
  - 45.4.2 LOD and merge (#574) - `Optimize/LodGroup`, `Optimize/BatchPlanner` - building blocks for far meshes
- **Code:** `Render/Atmosphere`, `Optimize/LodGroup`
- **API:** `LodGroup`, `MergeStaticMeshes`
- **Editor:** none
- **Docs:** `docs/optimization_tools.md`
- **Tests:** `optimize_simplify`
- **Status:** Planned - no impostor or horizon system.
- **Exists today:** Fog only; the LOD data layer (#574) exists but nothing in the renderers uses it.
- **Suggested priority:** Low - after LOD components exist (27.1).
- **Open:** first step: merged low-detail meshes per cell ("hierarchical LOD") using the simplifier; billboard impostors for trees.
- **Related:** 27.1, 27.3, 2.4, 29.2
- **Words:** impostor, HLOD, billboards, horizon, far view, distant terrain, view distance
- **Seen as:** pop-in at the edge of fog; far mountains missing
- **Ask me like:** "generate merged far-distance meshes per cell"

## 46. Replay system

### 46.1 Input recording and playback
- **What:** Record a play session as the inputs players gave, and play it back to watch it again, test it or share it.
- **Parts:**
  - 46.1.1 Per-tick input structure - `Net/PlayerInput` - `PlayerInput` and `IPlayerSim`: a controller that reads input in one place and moves only through `Simulate(input, dt)`
  - 46.1.2 Injectable input source - `Net/NetworkManager` - `SetInputSource(fn)` lets code supply inputs (the header names "tests, bots and replays")
  - 46.1.3 Fixed step and seeded random - `Scene/Scene`, `Core/Rng` - fixed-step physics option and seedable generators
  - 46.1.4 Headless play-test harness (#572) - `Testing/PlayTest` - fake input per frame with a repeatable clock
  - 46.1.5 Frame-time recorder and player (#581) - `Core/FrameRecord` - `FrameRecorder` and `FramePlayer` save and replay each frame's duration, pause and hitch flags and one integer marker per frame (`SetFrameMarker`, meant to index your own input log) in the checksummed text file `okayframes 1`; a replay through a `GameLoop` reproduces `Time` bit for bit; it does not record inputs (per docs)
- **Code:** `Net/PlayerInput`, `Net/NetworkManager`, `Core/Rng`, `Testing/PlayTest`, `Core/FrameRecord`
- **API:** `PlayerInput`, `NetworkManager::SetInputSource`, `PlayTest`, `FrameRecorder`, `FramePlayer`, `GameLoop::AttachRecorder`
- **Editor:** none
- **Docs:** `docs/dedicated-server.md`; `docs/automated_testing.md`
- **Tests:** `net_auth_move`, `net_predict`; `playtest`, `frame_record`
- **Status:** Planned - the building blocks exist (play-test harness and, since #581, the frame-time half of a recorder and player on main); no input recorder.
- **Exists today:** Authoritative-movement code already separates input from simulation for networked controllers; whether a whole scene replays identically across machines was not verified.
- **Suggested priority:** Low - nice for testing and spectating; first useful as a test tool.
- **Open:** first step: record `PlayerInput` per fixed tick plus the random seed and scene file, tie each tick to a `FrameRecorder` marker (`GameLoop::SetFrameMarker`), and replay it through `PlayTest` to check determinism before building a viewer.
- **Related:** 33.1, 16.2, 6.4, 1.1
- **Words:** replay, demo recording, input recording, playback, killcam, deterministic, record and replay, ghost
- **Seen as:** a bug cannot be reproduced; want to watch my last run
- **Ask me like:** "record inputs and replay a level deterministically"

### 46.2 State replay and killcam
- **What:** Replay from recorded object states (snapshots) instead of inputs, to show the last seconds of a match from any camera.
- **Parts:**
  - 46.2.1 Network snapshots and deltas - `Net/NetDelta`, `ECS/NetWorldSync` - the engine already produces compact state snapshots for networking
  - 46.2.2 Rewind raycast - `Net/NetworkManager` - the host keeps about a second of object poses and `RewindRaycast` shoots against where things were at an earlier net tick (lag compensation)
- **Code:** `Net/NetDelta`, `ECS/NetWorldSync`, `Net/NetworkManager`
- **API:** `NetDelta`, `NetworkManager::RewindRaycast`
- **Editor:** none
- **Docs:** `docs/dedicated-server.md`
- **Tests:** `net_sync`, `net_lagcomp`
- **Status:** Planned - snapshots exist for netcode only.
- **Exists today:** Snapshots and a short server-side pose history, used for networking.
- **Suggested priority:** Low - depends on 46.1 and multiplayer needs.
- **Open:** first step: write snapshots to a ring buffer and a free camera that reads them back.
- **Related:** 16.3, 46.1, 4.8
- **Words:** killcam, state replay, snapshot replay, spectate, instant replay, ring buffer
- **Seen as:** cannot show a kill from another angle
- **Ask me like:** "keep the last 10 seconds of snapshots for a killcam"

### 46.3 Replay files and viewer
- **What:** A saved replay file and a player with pause, speed and camera controls.
- **Parts:**
  - 46.3.1 File format and viewer - `-` - not built
- **Code:** -
- **API:** none
- **Editor:** none
- **Docs:** -
- **Tests:** none
- **Status:** Planned - nothing exists.
- **Exists today:** No replay file. The `okayframes 1` frame-time file (46.1.5) and the save framework's versioned slot headers (35.4) show the header, checksum and strict-loader pattern to copy.
- **Suggested priority:** Low.
- **Open:** first step: define the file (header with version, scene, seed; per-tick records) with the same versioning rules as saves (35.4).
- **Related:** 46.1, 35.4, 50.3
- **Words:** replay file, replay viewer, playback speed, scrub, demo file
- **Seen as:** no way to share a replay
- **Ask me like:** "define a replay file format"

## 47. Mod support

### 47.1 Mod loader and content overrides
- **What:** Let players add or replace game content (items, scenes, art, scripts) from a Mods folder without editing the game.
- **Parts:**
  - 47.1.1 Data-driven content - `Inventory/ItemDatabase`, `Core/DataAsset` - items, recipes and loot tables are text files the game loads; `.okaydata` assets
  - 47.1.2 Asset base folder - `Core/Paths` - one asset base directory with change listeners (#571); no list of search folders
  - 47.1.3 Packed data - `Core/DataPack` - optional obfuscation of shipped data (not security)
  - 47.1.4 Build integrity manifest - `shared/AntiCheatIntegrity.hpp` - Data/manifest.json lists every shipped file's SHA-256 when anti-cheat is on, which a mod would change
- **Code:** `Inventory/ItemDatabase`, `Core/DataAsset`, `Core/Paths`, `Core/DataPack`, `shared/AntiCheatIntegrity.hpp`
- **API:** `ItemDatabase::Load`, `DataAsset::Load`, `Paths::SetAssetBaseDir`
- **Editor:** none
- **Docs:** `docs/packaging.md`, `docs/anticheat.md`
- **Tests:** `dataasset`, `datapack`, `item_database`
- **Status:** Planned - content is text and could be overlaid; no mod discovery or load order exists.
- **Exists today:** The data formats above. The player loads `items.okayitems` and scenes from one base folder.
- **Suggested priority:** Low - strongest for sandbox and survival games; depends on how open the owner wants games to be.
- **Open:** first step: asset lookup through an ordered list of folders (mods first), a `mod.txt` manifest (name, version, load order), and a rule for what a mod may replace.
- **Related:** 17.6, 20.3, 47.2, 16.6
- **Words:** mods, modding, mod folder, content override, load order, workshop mods, user content, add-ons
- **Seen as:** players ask for custom items and maps; cannot add content without rebuilding the game
- **Ask me like:** "load extra content from a Mods folder with a manifest"

### 47.2 Mod scripting sandbox
- **What:** Run scripts from mods safely: limit file access, network and engine calls.
- **Parts:**
  - 47.2.1 Script languages - `Scripting/OkayScriptVM` - scripts have builtins for files and networking
  - 47.2.2 No permission model - `-` - the script hosts have no capability list
- **Code:** `Scripting/OkayScriptVM`
- **API:** script built-ins (`docs/scripting_reference.md`)
- **Editor:** none
- **Docs:** `docs/scripting_reference.md`
- **Tests:** `scripting`, `script_semantics`
- **Status:** Planned - scripts run with the full builtin set (`docs/editor-plugins.md` lists files among the builtins).
- **Exists today:** Full-access scripts.
- **Suggested priority:** Low - a prerequisite for 47.1 on public games.
- **Open:** first step: a capability flag per script host (files, network, process) defaulting to off for mod scripts.
- **Related:** 13.1, 47.1, 24.2
- **Words:** sandbox, script permissions, safe scripting, capabilities, untrusted scripts, mod security
- **Seen as:** a downloaded mod could read my files
- **Ask me like:** "add a permission list to the script hosts"

### 47.3 Mod packaging and sharing
- **What:** Package, publish and install mods through the launcher's Marketplace or Workshop.
- **Parts:**
  - 47.3.1 Workshop and Marketplace - `Platform/Workshop`, `launcher/main.cpp` - published items with a download URL; kinds include games and templates
  - 47.3.2 Mod item kind - `-` - not built
- **Code:** `Platform/Workshop`, `launcher/main.cpp`
- **API:** `WorkshopItem`
- **Editor:** Asset Manager window (imports downloads)
- **Docs:** `docs/supabase-backend.md`
- **Tests:** none
- **Status:** Planned - sharing exists, a "mod" kind and installer do not.
- **Exists today:** Publish and download of whole games, scenes and templates.
- **Suggested priority:** Low.
- **Open:** first step: add a `mod` kind, an installer that unpacks into the game's Mods folder, and an enable and disable list.
- **Related:** 17.6, 47.1, 25.4
- **Words:** workshop mods, mod install, publish mod, marketplace kind, mod browser
- **Seen as:** no place to find or install mods
- **Ask me like:** "add a mod item kind to the Workshop"

## 48. Crash reporting and telemetry

### 48.1 Crash reports
- **What:** When the editor or a shipped game crashes, keep a useful report and make it easy to send, so bugs can be found without guessing.
- **Parts:**
  - 48.1.1 Editor crash handlers - `editor/main.cpp` - a Windows exception filter and a Linux/macOS signal handler write `okay_crash.txt` (version, last step, exception, module, backtrace) and append `okay_crashlog.txt`; `OKAY_TRACE` records the last step
  - 48.1.2 Crash Log tab - `editor/main.cpp` - View > Crash Log shows past reports with a copy button for support
  - 48.1.3 Launcher crash page - `shared/CrashReports.hpp`, `launcher/main.cpp` - Settings > Crash reports parses the log, names the exception (for example "Access violation") and notifies once per new crash
  - 48.1.4 Player crash handler - `player/main.cpp` - no crash handler found in the player
- **Code:** `editor/main.cpp`, `shared/CrashReports.hpp`, `launcher/main.cpp`, `player/main.cpp`
- **API:** `OKAY_TRACE("step")`, `okaycrash::ParseLog`, `okaycrash::Load`, `okaycrash::ExceptionName`
- **Editor:** View > Crash Log
- **Docs:** `docs/editor.md`, `docs/launcher-multiplayer.md`
- **Tests:** `crash_reports`
- **Status:** Partial - the editor records and shows crashes; the shipped player records nothing, and nothing is sent anywhere.
- **Exists today:** All Parts except the player handler; reports stay on the machine.
- **Suggested priority:** High - a player-side report is the cheapest way to learn what breaks on real machines.
- **Open:** first step: reuse the editor's handler in the player (report next to the game, plus last Console lines); then an optional upload (see 48.3 consent first); symbolizing backtraces needs the build's symbols.
- **Related:** 22.3, 17.1, 18.4, 23.4
- **Words:** crash report, crash handler, okay_crash.txt, backtrace, minidump, bug report, crash log, last step, OKAY_TRACE
- **Seen as:** a player says "it crashed" and I have nothing; the launcher shows editor crashes only
- **Ask me like:** "add a crash handler and report file to the player"; "add a Send report button with the last log lines"

### 48.2 Opt-in analytics events
- **What:** Count how people use a game or the editor (levels reached, features used) to find problems, only with the player's consent.
- **Parts:**
  - 48.2.1 Local play time - `launcher/main.cpp` - the launcher credits play time per game on the machine (not sent)
  - 48.2.2 Online services - `Platform/Account/AccountService`, `Platform/NativeHttp` - the engine can already post to a Supabase backend (accounts, match results, forums), so a sink exists
  - 48.2.3 Event API and sink - `-` - not built; no analytics or telemetry code was found (searched the engine, editor, player and launcher)
- **Code:** `launcher/main.cpp`, `Platform/Account/AccountService`, `Platform/NativeHttp`
- **API:** none yet
- **Editor:** none
- **Docs:** `docs/supabase-backend.md`
- **Tests:** none
- **Status:** Planned - no analytics exist, which also means nothing is collected.
- **Exists today:** Only the transport pieces.
- **Suggested priority:** Low - useful once there are shipped games; the owner may prefer to ship without it.
- **Open:** first step: an `Analytics::Event(name, fields)` API that does nothing unless consent is on, writes to a local file first, and a Supabase table for upload; no personal data by default.
- **Related:** 48.3, 17.2, 17.10, 49.3
- **Words:** analytics, telemetry, usage statistics, events, funnel, retention, metrics, tracking
- **Seen as:** no idea where players quit a level
- **Ask me like:** "add an opt-in analytics event API with a local log first"

### 48.3 Consent and privacy
- **What:** Ask before anything is collected or sent, explain what, and let the player change their mind.
- **Parts:**
  - 48.3.1 Prefs and settings screens - `Core/Prefs`, `Components/SettingsScreen` - a place to store a consent flag and show a toggle
  - 48.3.2 Consent dialog and policy text - `-` - not built
- **Code:** `Core/Prefs`, `Components/SettingsScreen`
- **API:** `Prefs::SetBool`
- **Editor:** none
- **Docs:** `docs/game_ui.md`
- **Tests:** `prefs`, `pausemenu`
- **Status:** Planned - no consent flag or dialog exists.
- **Exists today:** A settings screen and typed Prefs.
- **Suggested priority:** Low - required before 48.2 or an upload in 48.1 ships.
- **Open:** first step: a first-run consent dialog component and a settings toggle bound to one Prefs key that all senders check.
- **Related:** 48.1, 48.2, 12.8
- **Words:** consent, privacy, opt in, GDPR, data collection, telemetry setting, privacy policy
- **Seen as:** worry that a build sends data without asking
- **Ask me like:** "add a consent dialog and a Prefs flag that gates uploads"

### 48.4 Collection backend and dashboards
- **What:** Where reports and events go and how the team reads them.
- **Parts:**
  - 48.4.1 SQL for the existing backend - `docs/sql/*` - accounts, social and match tables for Supabase; no crash or event tables
  - 48.4.2 Dashboards - `-` - not built
- **Code:** `docs/sql/social.sql`, `docs/sql/matches.sql`
- **API:** none
- **Editor:** none
- **Docs:** `docs/supabase-backend.md`
- **Tests:** `publish_supabase_checks`
- **Status:** Planned - no crash or event tables exist in the Supabase SQL.
- **Exists today:** A Supabase backend for other features.
- **Suggested priority:** Low.
- **Open:** first step: `crash_reports` and `events` tables with row-level security and an upload function that only inserts; a simple web view of the counts.
- **Related:** 17.10, 48.1, 48.2
- **Words:** backend, Supabase tables, crash dashboard, ingest, events table
- **Seen as:** reports have nowhere to go
- **Ask me like:** "design tables for crash reports and events in Supabase"

## 49. Cloud services

### 49.1 Cloud saves
- **What:** Keep a player's saves in the cloud so progress follows them between machines.
- **Parts:**
  - 49.1.1 Steam Cloud calls - `Platform/Steam/SteamService` - `CloudWrite`, `CloudRead`, `CloudHasFile`, `CloudDelete`, `CloudFiles` (default implementations do nothing); script `steam_cloud_write`, `steam_cloud_read`
  - 49.1.2 PlayFab user data - `Platform/PlayFab/PlayFab` - `SetUserData`, `GetUserData`, `ListUserData`; script `pf_save`
  - 49.1.3 Save sync hooks (#572) - `Core/SaveFrameworkSync` - `ISaveSync` with in-memory and folder backends and conflict reporting
  - 49.1.4 Glue between saves and a service - `-` - not built
- **Code:** `Platform/Steam/SteamService`, `Platform/PlayFab/PlayFab`, `Core/SaveFrameworkSync`, `Scripting/OkayScriptVM`
- **API:** `Steam::Get().CloudWrite`, `PlayFab::Get().SetUserData`, `ISaveSync`
- **Editor:** none
- **Docs:** `docs/scripting_reference.md` (Steam), `docs/steam_release.md`; `docs/save_framework.md`
- **Tests:** `steam`, `steam_script`; `save_framework`
- **Status:** Partial - the service calls exist and are scriptable; nothing syncs a save slot to them.
- **Exists today:** Raw per-service calls. A game must write its own copy logic.
- **Suggested priority:** Medium - players expect cloud saves on Steam; the hooks exist as library code in #572 (`ISaveSync`, in-memory and folder backends) but no Steam or PlayFab backend is behind them.
- **Open:** first step: `ISaveSync` backends for Steam Cloud and PlayFab user data, with "newest wins" and a conflict prompt; size limits per service.
- **Related:** 35.1, 17.10, 16.1
- **Words:** cloud saves, Steam Cloud, PlayFab user data, sync saves, remote storage, conflict
- **Seen as:** progress lost when changing computers
- **Ask me like:** "sync save slots to Steam Cloud"

### 49.2 Leaderboards
- **What:** Per-game score tables, with friends and global views.
- **Parts:**
  - 49.2.1 Steam leaderboards - `Platform/Steam/SteamService` - `LeaderboardEntry`, script `steam_leaderboard`, `steam_leaderboard_top`
  - 49.2.2 PlayFab leaderboards - `Platform/PlayFab/PlayFab` - `SetStat`, `GetLeaderboard`; script `pf_set_stat`, `pf_leaderboard_top`
  - 49.2.3 Match-result leaderboards - `Net/MatchResults` - a host reports scores and the launcher shows per-game leaderboards (Supabase `matches.sql`)
- **Code:** `Platform/Steam/SteamService`, `Platform/PlayFab/PlayFab`, `Net/MatchResults`
- **API:** `PlayFab::GetLeaderboard`, `MatchResults`
- **Editor:** none (visual-script playfab and steam instructions)
- **Docs:** `docs/scripting_reference.md`, `docs/launcher-multiplayer.md`
- **Tests:** `steam`, `match_results`
- **Status:** Partial - three separate implementations; no single API a game uses regardless of platform.
- **Exists today:** All Parts, each with its own calls.
- **Suggested priority:** Low - depends on what the owner wants to host.
- **Open:** first step: a small `Leaderboards` interface with the three as backends and a UI list component.
- **Related:** 17.7, 16.4, 12.4
- **Words:** leaderboard, high scores, rankings, stats, PlayFab stat, Steam leaderboard
- **Seen as:** need a high score table that works on and off Steam
- **Ask me like:** "add one Leaderboards API over Steam and PlayFab"

### 49.3 Achievements sync
- **What:** Unlock achievements in a game and have them show in the launcher, on Steam and across machines.
- **Parts:**
  - 49.3.1 Local achievements - `Platform/Achievements` - cross-game list stored per user on the machine, read by games and the launcher, works offline
  - 49.3.2 Steam achievements - `Platform/Steam/SteamService` - unlock, clear, progress; script `steam_unlock`
  - 49.3.3 Cloud sync - `-` - not built (the header says an optional Supabase sync "can be layered on top later")
- **Code:** `Platform/Achievements`, `Platform/Steam/SteamService`
- **API:** `Achievements`, `Steam::Get().UnlockAchievement`
- **Editor:** none
- **Docs:** `docs/scripting_reference.md`
- **Tests:** `steam`
- **Status:** Partial - local and Steam achievements work; they are not linked and not synced.
- **Exists today:** Both stores, separately.
- **Suggested priority:** Low.
- **Open:** first step: one `Unlock(id)` that writes local, calls Steam when present and queues an upload when signed in.
- **Related:** 17.7, 17.10
- **Words:** achievements, unlock, trophies, achievement points, sync achievements, profile
- **Seen as:** unlocked on Steam but not in the launcher
- **Ask me like:** "make one unlock call that updates local and Steam achievements"

### 49.4 Remote configuration
- **What:** Change game settings (prices, event dates, feature switches) from a server without shipping a new build.
- **Parts:**
  - 49.4.1 Cvars (merged #571) - `Core/Cvar` - named settings that can be loaded from a file or the command line; no remote source
  - 49.4.2 Remote fetch - `-` - not built
- **Code:** `Core/Cvar`
- **API:** `Cvars::Int`, `Cvars::Load`
- **Editor:** none
- **Docs:** `docs/core_tools.md`
- **Tests:** `core_console`
- **Status:** Planned - cvars exist locally; nothing fetches settings from a server.
- **Exists today:** Local cvars only.
- **Suggested priority:** Low.
- **Open:** first step: a signed JSON file fetched at start that sets cvars; falls back to the shipped defaults offline.
- **Related:** 54.3, 49.1
- **Words:** remote config, feature flags, live ops, server settings, hotfix values
- **Seen as:** need to tweak a value without an update
- **Ask me like:** "load cvars from a remote JSON file at startup"

## 50. Capture and media

### 50.1 Screenshots
- **What:** Save a picture of the game or editor view with one key, at a chosen size.
- **Parts:**
  - 50.1.1 Image saving - `Graphics/Image` - `Image::SavePNG`
  - 50.1.2 Scene view capture - `editor/main.cpp` - the Scene view options have a debug button "Save Scene view -> PNG" that writes `scene_capture.png` from the software renderer's raw pixels
  - 50.1.3 Software scene render (#572) - `Testing/Regression` - `RenderSceneToImage` renders meshes to an `Image`
  - 50.1.4 In-game screenshot key - `player/main.cpp` - no screenshot key found in the player
- **Code:** `Graphics/Image`, `editor/main.cpp`, `Testing/Regression`
- **API:** `Image::SavePNG`, `RenderSceneToImage`
- **Editor:** Scene view options > Save Scene view -> PNG (debug)
- **Docs:** -
- **Tests:** `image`
- **Status:** Basic - the editor can capture its Scene view; a shipped game cannot take a screenshot.
- **Exists today:** Scene view capture to a fixed file name in the current folder.
- **Suggested priority:** Medium - cheap and wanted for store pages, bug reports and thumbnails (35.1).
- **Open:** first step: a Screenshot key in the player writing a timestamped PNG to the user pictures or data folder, from the actual displayed frame.
- **Related:** 35.1, 20.5, 50.2
- **Words:** screenshot, capture, scene_capture.png, PNG, F12, thumbnail, store screenshot
- **Seen as:** no way to take a picture of my game; capture goes to the wrong folder
- **Ask me like:** "add an F12 screenshot key to the player"

### 50.2 Photo mode
- **What:** Pause the game and move a free camera with filters to take nice pictures.
- **Parts:**
  - 50.2.1 Free camera - `Components/FreeRoamController` - a fly camera driven by mouse and keys
  - 50.2.2 Post effects - `Render/Atmosphere` - bloom, grading, vignette, depth of field, fog presets
  - 50.2.3 Pause and cursor control - `Core/Game`, `Input/Cursor` - pause state and cursor lock
  - 50.2.4 Photo mode component - `-` - not built
- **Code:** `Components/FreeRoamController`, `Render/Atmosphere`, `Core/Game`, `Input/Cursor`
- **API:** `FreeRoamController`, script `post_set`, `fog_set`
- **Editor:** none
- **Docs:** `docs/rendering.md`
- **Tests:** `post_sky`
- **Status:** Planned - all ingredients exist; nothing combines them.
- **Exists today:** The three pieces above.
- **Suggested priority:** Low - a polish feature after 50.1.
- **Open:** first step: a `PhotoMode` component: key toggles pause, hides the UI, switches to a free camera with sliders for field of view, depth of field and grading, and a capture button.
- **Related:** 50.1, 4.8, 2.5, 12.8
- **Words:** photo mode, free camera, pause camera, depth of field, filters, hide UI, screenshot mode
- **Seen as:** players ask for a camera to take pictures
- **Ask me like:** "add a Photo Mode component"

### 50.3 Video recording
- **What:** Record gameplay or a cutscene to a video file or image sequence.
- **Parts:**
  - 50.3.1 Frame capture - `-` - see 50.1; no recorder, encoder or image-sequence writer was found
  - 50.3.2 Fixed-rate capture clock (#581) - `Core/GameLoop` - `SetCapture(fps, frames)` makes every frame exactly 1/fps seconds whatever the machine does and stops after a frame budget, so two runs give the same `Time` values frame for frame
- **Code:** `Core/GameLoop`
- **API:** `GameLoop::SetCapture`, `ClearCapture`, `SetFrameBudget`
- **Editor:** none
- **Docs:** `docs/loop_and_time.md`
- **Tests:** `capture_mode`
- **Status:** Planned - no recorder or encoder exists; only the exact-frame-time half is on main (#581).
- **Exists today:** Single-frame PNG saving; the capture clock exists in `GameLoop` (no PNG writer is attached to it).
- **Suggested priority:** Low - image sequences (PNG per frame) would cover trailers; real encoding needs a library.
- **Open:** first step: an image-sequence recorder at a fixed time step (so slow frames do not drop video frames), using `GameLoop::SetCapture` (#581) or the fixed-step time from #572's play-test clock.
- **Related:** 50.1, 10.1, 46.1
- **Words:** video recording, record gameplay, trailer, image sequence, capture video, cutscene render
- **Seen as:** need to record a trailer
- **Ask me like:** "add an image-sequence recorder for trailers"

### 50.4 Video playback
- **What:** Play video files in the game (intro logos, cutscenes, menu backgrounds).
- **Parts:**
  - 50.4.1 Audio decoding - `Audio/AudioClip`, `Audio/AudioMixer` - WAV, OGG and MP3 audio; no video decoder
- **Code:** `Audio/AudioClip`, `Audio/AudioMixer`
- **API:** none for video
- **Editor:** none
- **Docs:** `docs/audio.md`
- **Tests:** `audio_formats`
- **Status:** Planned - no video playback code or decoder library exists (searched the engine, editor and player).
- **Exists today:** Audio only; cutscenes are in-engine sequences (area 10).
- **Suggested priority:** Low - most indie games use in-engine cutscenes; web builds could use the browser's video element.
- **Open:** first step: choose a codec and a small decoder library (for example a simple open format) and a `VideoPlayer` component drawing to a texture; keep it optional.
- **Related:** 10.1, 7.2, 2.9
- **Words:** video playback, intro video, movie, cutscene video, VideoPlayer, mp4, webm, ogv
- **Seen as:** cannot play a pre-rendered cutscene or intro movie
- **Ask me like:** "add a video player component for intro movies"

## 51. Onboarding, tutorials and samples

### 51.1 Sample gallery
- **What:** Ready-made example projects people can open, play, take apart and learn from.
- **Parts:**
  - 51.1.1 Create page templates - `launcher/main.cpp` - 21 templates in groups including "Game kits" and "Examples and systems" (Coin Collector, Snake, Brick Breaker, Inventory, Humanoid and more)
  - 51.1.2 Code demos - `examples/netdemo/main.cpp`, `examples/physicsdemo/main.cpp` - small C++ programs
  - 51.1.3 Editor plugin examples - `examples/editor-plugins/AlignTools.okay` - sample plugins
  - 51.1.4 Marketplace - `Platform/Workshop` - community templates and games
- **Code:** `launcher/main.cpp`, `examples/netdemo/main.cpp`, `examples/physicsdemo/main.cpp`, `examples/editor-plugins/AlignTools.okay`
- **API:** launcher Create page
- **Editor:** File > New with the template list
- **Docs:** `docs/making_a_game.md`, `docs/editor.md`
- **Tests:** `templates`
- **Status:** Partial - a template gallery exists; templates are not annotated and there is no "how this was made" guide.
- **Exists today:** The Parts above. The launcher card list is also the starting point for 25.1.
- **Suggested priority:** High - the cheapest way to make the engine learnable; most of it is content, not code.
- **Open:** first step: each template gets a short "What to look at" note shown when it opens (which objects, scripts and settings matter); a "Samples" tab in the launcher with screenshots.
- **Related:** 25.1, 17.1, 51.2
- **Words:** sample projects, examples, template gallery, starter kits, demo games, learn by example
- **Seen as:** a new user does not know where to start; templates work but do not explain themselves
- **Ask me like:** "add a What to look at note to each template"

### 51.2 Guided tutorials in the editor
- **What:** Step-by-step lessons inside the editor that highlight the thing to click and check that it was done.
- **Parts:**
  - 51.2.1 Written guide - `docs/making_a_game.md` - first game from an empty editor to a shipped one
  - 51.2.2 Language tutorial - `editor/main.cpp` - the Scripting Reference window has a "Learn the basics" section teaching OkayScript from scratch
  - 51.2.3 Interactive lessons - `-` - not built (no step highlighting or task checking found)
- **Code:** `editor/main.cpp`
- **API:** Help > Scripting Reference
- **Editor:** Help > Scripting Reference, Keyboard Shortcuts
- **Docs:** `docs/making_a_game.md`
- **Tests:** none
- **Status:** Basic - reading material exists; nothing guides clicks.
- **Exists today:** One document and one reference window with a tutorial section.
- **Suggested priority:** Medium - after the sample gallery.
- **Open:** first step: a lesson format (a text file of steps, each with a highlighted window or button and a check such as "an object named Player exists") and one lesson, "Make something move".
- **Related:** 51.1, 51.3, 13.5, 15.1
- **Words:** tutorial, onboarding, first run, guided tour, lessons, learn, getting started, interactive tutorial
- **Seen as:** I do not know what to click first
- **Ask me like:** "add a guided first lesson that checks my steps"

### 51.3 Welcome screen and tips
- **What:** A first-run screen with what to do next, recent projects and tips, and hints shown at the right moment.
- **Parts:**
  - 51.3.1 Splash and Console welcome - `editor/main.cpp` - a splash screen and a "Welcome to OkaySpace" log line; per-project splash is customizable (25.2)
  - 51.3.2 Launcher home - `launcher/main.cpp` - recent projects and the Create page
  - 51.3.3 Tooltips - `editor/main.cpp` - many controls have hover tooltips
- **Code:** `editor/main.cpp`, `launcher/main.cpp`
- **API:** none
- **Editor:** startup splash
- **Docs:** `docs/editor.md`
- **Tests:** none
- **Status:** Basic - the launcher serves as the start page; the editor shows no first-run help.
- **Exists today:** As above.
- **Suggested priority:** Low.
- **Open:** first step: a dismissible "Getting started" panel in an empty project with three links (open a sample, run the first lesson, read the docs).
- **Related:** 51.1, 51.2, 17.1
- **Words:** welcome screen, getting started, tips, first run, start page, hints
- **Seen as:** the editor opens to an empty scene with no direction
- **Ask me like:** "add a Getting Started panel to empty projects"

### 51.4 Contextual help
- **What:** Open the right documentation page from any component or window.
- **Parts:**
  - 51.4.1 Help menu - `editor/main.cpp` - Scripting Reference, Keyboard Shortcuts, About; node help in the visual script editor
  - 51.4.2 Docs in the repo - `docs/README.md` - an index of Markdown pages
  - 51.4.3 Per-component help link - `-` - not built
- **Code:** `editor/main.cpp`, `editor/VisualScriptNodeHelp.hpp`
- **API:** none
- **Editor:** Help menu, node help panel
- **Docs:** `docs/README.md`
- **Tests:** none
- **Status:** Basic - a Help menu and node help exist; no per-component links.
- **Exists today:** The Help menu and node help text.
- **Suggested priority:** Medium - pairs with the documentation site (57).
- **Open:** first step: a "?" button on each component header that opens its docs page (a table from component name to page).
- **Related:** 57.1, 15.2, 14.4
- **Words:** help, contextual help, docs link, component help, F1, question mark button
- **Seen as:** I cannot find what a component does
- **Ask me like:** "add a help button to every component header"

## 52. Simulation extras (cloth, soft bodies, fluids, destruction, crowds)

### 52.1 Cloth and hair
- **What:** Fabric, flags, capes and hair that move with physics.
- **Parts:**
  - 52.1.1 Verlet particle chain - `Components/Ragdoll` - the ragdoll uses Verlet particles with distance constraints; the solver could be a base
  - 52.1.2 Distance and spring joints - `Components/Joint3D` - `Mode { Distance, Spring, Pin, Hinge }`
  - 52.1.3 Cloth component - `-` - not built (no cloth code found)
- **Code:** `Components/Ragdoll`, `Components/Joint3D`
- **API:** none for cloth
- **Editor:** none
- **Docs:** `docs/ragdolls.md`
- **Tests:** `ragdoll`, `physics_joints`
- **Status:** Planned - no cloth, rope or hair code exists; only the ragdoll's particle chain.
- **Exists today:** A particle chain solver inside the ragdoll; no mesh cloth.
- **Suggested priority:** Low - visual polish.
- **Open:** first step: a rope and flag component on a particle grid with pin points and wind from `Environment/Wind`.
- **Related:** 5.8, 6.3, 30.3
- **Words:** cloth, flag, cape, hair physics, rope, verlet, wind
- **Seen as:** capes and flags are stiff
- **Ask me like:** "add a simple flag and rope component on the ragdoll's solver"

### 52.2 Soft bodies
- **What:** Jelly-like or squishy objects that deform and recover.
- **Parts:**
  - 52.2.1 Rigid bodies only - `Physics/Physics3D`, `Physics/Rigidbody3D` - no deformable body found
- **Code:** `Physics/Physics3D`
- **API:** none
- **Editor:** none
- **Docs:** -
- **Tests:** none
- **Status:** Planned - only rigid bodies exist.
- **Exists today:** Nothing.
- **Suggested priority:** Low.
- **Open:** first step: a spring-mass lattice on a mesh (same solver as 52.1).
- **Related:** 52.1, 6.2
- **Words:** soft body, jelly, deformable, squish, spring mesh
- **Seen as:** need a squishy character or slime
- **Ask me like:** "add a spring-mass soft body component"

### 52.3 Fluids
- **What:** Water that flows and splashes, beyond a flat animated surface.
- **Parts:**
  - 52.3.1 Water surface - `Components/Water` - animated wave mesh with reflection, buoyancy and drag on rigid bodies (`fluidDensity`)
  - 52.3.2 Particles - `Components/ParticleSystem` - splashes by particle effects
  - 52.3.3 Fluid simulation - `-` - not built
- **Code:** `Components/Water`, `Components/ParticleSystem`
- **API:** `Water`, `fluidDensity`
- **Editor:** Water Inspector section
- **Docs:** `docs/rendering.md`
- **Tests:** `terrain` (also covers Water), `survival`
- **Status:** Basic - a convincing surface and buoyancy; no flowing fluid.
- **Exists today:** Parts 52.3.1 and 52.3.2.
- **Suggested priority:** Low.
- **Open:** first step: shallow-water ripples reacting to objects entering the water.
- **Related:** 2.6, 2.7, 6.2
- **Words:** fluid, water simulation, ripples, splash, buoyancy, flow
- **Seen as:** objects entering water make no ripples
- **Ask me like:** "make objects create ripples in the Water surface"

### 52.4 Destruction
- **What:** Break objects into pieces and collapse structures.
- **Parts:**
  - 52.4.1 Block destruction - `Components/Destructible` - fracture of cube blocks into debris, structural collapse of unsupported blocks, destruction gun
  - 52.4.2 Projectile and explosion physics - `Physics/Physics3D` - radial impulses (`test_physics_explode`)
- **Code:** `Components/Destructible`, `Physics/Physics3D`
- **API:** `Destructible::BreakBlock`, `BreakAt`, `CollapseUnsupported`
- **Editor:** Destructible Inspector section
- **Docs:** `docs/build_mode.md`
- **Tests:** `destructible`, `physics_explode`
- **Status:** Partial - works for the block grid used by builders; arbitrary meshes do not fracture.
- **Exists today:** Cube fracture with debris lifetime and collapse.
- **Suggested priority:** Low - already enough for block games.
- **Open:** first step: pre-fractured meshes (a mesh authored as pieces) with a break threshold and impulse.
- **Related:** 6.6, 11.5
- **Words:** destruction, fracture, debris, collapse, break apart, Destructible, shatter
- **Seen as:** a statue or car cannot break
- **Ask me like:** "let any mesh with pre-split pieces break on impact"

### 52.5 Crowds
- **What:** Hundreds of moving characters that avoid each other cheaply.
- **Parts:**
  - 52.5.1 Agent avoidance - `Components/NavMeshAgent`, `Components/NavMeshObstacle` - reciprocal velocity sampling among nearby agents and obstacles; per-agent priority
  - 52.5.2 Crowd system - `-` - not built (no flow fields, agent LOD or formations)
- **Code:** `Components/NavMeshAgent`, `Components/NavMeshObstacle`, `AI/NavMesh`
- **API:** `NavMeshAgent::radius`, `avoidancePriority`, `avoidanceRange`
- **Editor:** NavMeshAgent Inspector section
- **Docs:** `docs/navigation.md`
- **Tests:** `navmesh`
- **Status:** Basic - local avoidance per agent; the cost with hundreds of agents is not measured.
- **Exists today:** Agents steer around each other; each agent scans neighbours itself.
- **Suggested priority:** Low.
- **Open:** first step: measure 200 agents in a play-test (#572) and add a spatial hash for neighbour search; then flow fields for large groups.
- **Related:** 9.2, 58.2
- **Words:** crowd, crowd simulation, many agents, flocking, flow field, avoidance, horde
- **Seen as:** a hundred NPCs make the game slow
- **Ask me like:** "make 200 nav agents run at a steady frame rate"

## 53. Day-night and weather hooks

### 53.1 Time-of-day events and gameplay
- **What:** Let games react to the time of day (open and close shops, NPC schedules, spawn night enemies, light street lamps).
- **Parts:**
  - 53.1.1 Day-night clock - `Components/DayNightCycle` - 24-hour clock, sun rotation, light and sky colours; `Hour()`, `IsNight()`, `Elevation()`, `SetTime`, `AddHours`; publishes `hour` as a saved value
  - 53.1.2 Script access - `Scripting/OkayScriptVM` - `time_of_day([hours])`
  - 53.1.3 Events and schedules - `-` - not built (no sunrise, sunset or hour-changed event; nothing uses `IsNight` in NPCs or spawners)
- **Code:** `Components/DayNightCycle`, `Scripting/OkayScriptVM`
- **API:** `DayNightCycle::Hour`, `IsNight`, `SetTime`; script `time_of_day`
- **Editor:** DayNightCycle Inspector section
- **Docs:** `docs/day_night_cycle.md`
- **Tests:** `daynight`
- **Status:** Partial - the clock and visuals work; gameplay cannot subscribe to it.
- **Exists today:** Parts 53.1.1 and 53.1.2.
- **Suggested priority:** Medium - cheap to add and makes survival and RPG games feel alive.
- **Open:** first step: publish events on the `EventBus` (HourChanged, Sunrise, Sunset) and a "run actions at hour X" component; possible gap: the clock is not synchronised over the network (not found in `Net/*`).
- **Related:** 2.4, 11.4, 9.5, 16.3
- **Words:** day night, time of day, schedule, sunrise, sunset, hour event, night enemies, DayNightCycle
- **Seen as:** NPCs stand in the street all night; shops are always open
- **Ask me like:** "add hour-changed and sunset events to the day-night cycle"

### 53.2 Weather gameplay effects
- **What:** Rain, snow and storms that change the game, not only the picture (wet surfaces, cold, slippery ground, noise, visibility).
- **Parts:**
  - 53.2.1 Weather presets - `Environment/Weather` - rain, snow and storm rigs from particles that follow the camera, with fade between presets, lightning flashes and optional thunder; `weather_set` in scripts
  - 53.2.2 Wind - `Environment/Wind` - a global wind vector used by weather and water
  - 53.2.3 Survival zones - `Components/SurvivalZone` - cold, water and radiation effects for a trigger area; not linked to weather
- **Code:** `Environment/Weather`, `Environment/Wind`, `Components/SurvivalZone`
- **API:** `RenderSettings::weather`, script `weather_set("rain", intensity)`
- **Editor:** Scene settings weather section
- **Docs:** `docs/rendering.md`, `docs/survival_kit.md`
- **Tests:** `post_sky` (weather), `survival`
- **Status:** Partial - visuals and sound; no effect on gameplay.
- **Exists today:** Parts above; checked that survival code does not read the weather settings.
- **Suggested priority:** Medium - connects two features that already exist.
- **Open:** first step: a `WeatherEffects` component mapping a preset to survival modifiers (wetness, temperature drop, friction scale, stealth) with indoor volumes that block it.
- **Related:** 2.4, 11.4, 6.2, 53.1
- **Words:** weather, rain, snow, storm, wetness, temperature, slippery, weather hooks, lightning
- **Seen as:** rain does not make the player wet or cold; ground never gets slippery
- **Ask me like:** "make rain lower temperature and add wetness in survival"

### 53.3 Time and weather authoring
- **What:** Scrub time of day and weather in the editor and plan transitions.
- **Parts:**
  - 53.3.1 Live preview in the editor - `editor/main.cpp` - weather ticks in edit mode for a live preview; `OKAY_UI_WEATHER` dev hook
  - 53.3.2 Time scrubber and weather timeline - `-` - not built
- **Code:** `editor/main.cpp`, `Environment/Weather`
- **API:** none
- **Editor:** Scene settings
- **Docs:** `docs/day_night_cycle.md`
- **Tests:** none
- **Status:** Basic - weather previews in edit mode; no time scrubber.
- **Exists today:** Weather preview in edit mode; no time slider found.
- **Suggested priority:** Low.
- **Open:** first step: a time-of-day slider in the Scene view toolbar for scenes with a DayNightCycle.
- **Related:** 53.1, 15.3
- **Words:** time slider, weather timeline, preview time of day, scrub sun
- **Seen as:** have to press Play to see dusk
- **Ask me like:** "add a time-of-day slider to the Scene view"

## 54. Debug draw, gizmos, dev console and cheats

### 54.1 Debug draw API
- **What:** Draw lines, boxes, spheres and text in the world from code or scripts for a moment, to see what the game is thinking.
- **Parts:**
  - 54.1.1 Physics debug records - `Physics/PhysicsDebug` - the physics step records contacts and query rays for the editor overlay when `Collect()` is on
  - 54.1.2 Debug text lines - `Core/DebugOverlay` - builds overlay lines such as FPS and object count (merged #571); no drawing
  - 54.1.3 General draw calls - `-` - not found: no script or C++ call draws a debug line or shape
- **Code:** `Physics/PhysicsDebug`, `Core/DebugOverlay`
- **API:** `PhysicsDebug::Collect`, `DebugOverlay::BuildLines`
- **Editor:** physics debug toggles in the Scene view
- **Docs:** `docs/core_tools.md`
- **Tests:** `core_console`
- **Status:** Basic - only physics queries are visualised.
- **Exists today:** Parts 54.1.1 and 54.1.2. Script `debug_log` writes text only.
- **Suggested priority:** High - it is the quickest way for authors to debug movement, AI and aiming, and it is small.
- **Open:** first step: `Debug::DrawLine/Box/Sphere/Text(position, color, seconds)` in C++ plus `draw_line` and `draw_sphere` script builtins, collected per frame and drawn by the editor Scene view and an in-game debug layer.
- **Related:** 6.5, 13.6, 23.3, 54.2
- **Words:** debug draw, draw line, draw ray, debug shapes, gizmo drawing, visual debugging, draw_line
- **Seen as:** cannot see where my raycast goes or what my AI targets; need to add temporary cubes to debug
- **Ask me like:** "add a debug draw API and draw_line script builtin"

### 54.2 Gizmos for components
- **What:** Components draw their own helper shapes in the Scene view (ranges, directions, areas), selected or always.
- **Parts:**
  - 54.2.1 Hard-coded gizmos - `editor/main.cpp` - the editor draws helpers for specific components in its own code (selection outline, camera and light shapes under the View option "Gizmos", IK target helpers, spline handles)
  - 54.2.2 Transform gizmo - `editor/main.cpp` - move, rotate and scale handles with local or world axes
  - 54.2.3 Component callback - `-` - not built: components cannot ask to draw a gizmo
- **Code:** `editor/main.cpp`
- **API:** none
- **Editor:** Scene view
- **Docs:** `docs/editor.md`
- **Tests:** none
- **Status:** Basic - gizmos exist but only for components the editor knows by name.
- **Exists today:** Parts 54.2.1 and 54.2.2. A new component gets no gizmo without editing `editor/main.cpp`.
- **Suggested priority:** Medium - also unlocks editor plugins (24.2) and custom components (24.1).
- **Open:** first step: a `OnDrawGizmos(DebugDraw&)` virtual on `Component`, drawn only in the editor through the 54.1 API.
- **Related:** 54.1, 24.2, 15.3
- **Words:** gizmo, OnDrawGizmos, scene helper, range sphere, direction arrow, component gizmo
- **Seen as:** my component's range is invisible in the Scene view
- **Ask me like:** "let components draw their own gizmos in the Scene view"

### 54.3 In-game dev console
- **What:** A console inside the running game to type commands, change settings and print values.
- **Parts:**
  - 54.3.1 Console and cvar model (merged #571) - `Core/Console`, `Core/Cvar` - command registry (`help`, `list`, `set`, `get`, `toggle`, `reset`, `history`, `echo`), cvars with ranges and change callbacks, saved persistent values, command-line `--cvar`
  - 54.3.2 Command line - `Core/CommandLine` - `--flag`, `--key=value` parsing used by the player
  - 54.3.3 In-game console window - `-` - not built: neither the editor nor the player calls `Console` or `Cvars`; the player's F1 overlay is a widget demo (Test UI)
- **Code:** `Core/Console`, `Core/Cvar`, `Core/CommandLine`, `player/main.cpp`
- **API:** `Console::Register`, `Console::Execute`, `Cvars::Int`, `Cvars::Bool`
- **Editor:** none
- **Docs:** `docs/core_tools.md`
- **Tests:** `core_console`, `core_cli`
- **Status:** Partial - the model is merged and tested; no window and no cvars are registered by the engine.
- **Exists today:** The registry and parser only.
- **Suggested priority:** Medium - the model is done; a console window and a handful of engine cvars would make it usable.
- **Open:** first step: a backtick-toggled console component using `Console::History/Lines/Complete`, and engine cvars (`r.shadows`, `debug.overlay`, `time.scale`).
- **Related:** 23.4, 49.4, 54.4
- **Words:** dev console, in-game console, cvar, console command, tilde console, debug menu, command line, DebugOverlay
- **Seen as:** cannot change a setting while the game runs without a rebuild
- **Ask me like:** "add an in-game console toggled with the tilde key"

### 54.4 Cheats and debug commands
- **What:** Commands such as god mode, give item, teleport, set time, available in development builds.
- **Parts:**
  - 54.4.1 Cvar convention - `Core/Cvar` - the docs use `cheat.god` as an example name; no cheat is implemented
  - 54.4.2 Anti-cheat (different thing) - `Net/AntiCheat` - the "cheat" code in the engine is about detecting cheating players in multiplayer
  - 54.4.3 Command set - `-` - not built
- **Code:** `Core/Cvar`, `Net/AntiCheat`
- **API:** `Console::Register`
- **Editor:** none
- **Docs:** `docs/core_tools.md`, `docs/anticheat.md`
- **Tests:** `core_console`, `net_anticheat`
- **Status:** Planned - no player-side cheat commands exist.
- **Exists today:** Nothing implements player-side cheat commands (`grep` for "cheat" found only anti-cheat code).
- **Suggested priority:** Low - after 54.3; must be off in release and multiplayer builds.
- **Open:** first step: register `god`, `give <item> [n]`, `tp <x> <y> <z>`, `time <hour>` against the survival, inventory and day-night components, gated by a `dev` build flag.
- **Related:** 54.3, 16.6, 11.1
- **Words:** cheats, god mode, give item, teleport, debug commands, dev build, noclip
- **Seen as:** testing a late level means playing from the start
- **Ask me like:** "add god, give and teleport console commands for dev builds"

## 55. Script debugging and profiling

### 55.1 Breakpoints and stepping for text scripts
- **What:** Pause an OkayScript or C#-style script on a line, step, and see variables, like the visual script debugger already does for graphs.
- **Parts:**
  - 55.1.1 Visual-script debugger - `Components/ActionList`, `VisualScript/VsDebug`, `editor/GameFiber.hpp` - breakpoints on nodes freeze the game mid-frame; continue and step; red badges
  - 55.1.2 Script editor markers - `editor/main.cpp` - F11 toggles a bookmark shown as a marker (the editor widget's breakpoint markers are repurposed for bookmarks)
  - 55.1.3 VM debug hooks - `-` - not found: the script VM headers have no breakpoint, step or trace interface
- **Code:** `Components/ActionList`, `VisualScript/VsDebug`, `editor/GameFiber.hpp`, `Scripting/OkayScriptVM`, `Scripting/NextScriptVM`
- **API:** `ActionList::Breakpoints()`
- **Editor:** Visual Script editor debug bar; Script editor bookmarks
- **Docs:** `docs/visual_scripting.md`
- **Tests:** `vs_debug`, `game_fiber`
- **Status:** Partial - graphs can be debugged; text scripts cannot.
- **Exists today:** Parts 55.1.1 and 55.1.2. The game fiber is reusable for text scripts.
- **Suggested priority:** High - most gameplay is written in text scripts and today the only tool is `print`.
- **Open:** first step: a line hook in the OkayScript VM and the typed-script interpreter (`engine/src/Script/Interpreter.cpp`) that calls a debug callback; reuse the game fiber to freeze the frame.
- **Related:** 13.5, 14.5, 23.4
- **Words:** script debugger, breakpoint, step over, step into, call stack, pause on error, GameFiber, debug script
- **Seen as:** my script does something odd and I can only add prints
- **Ask me like:** "add breakpoints and stepping to OkayScript in the script editor"

### 55.2 Watch and call stack
- **What:** See variable values and the chain of calls that led here.
- **Parts:**
  - 55.2.1 Live variables - `editor/main.cpp` - the Variables window and the Inspector show visual-script variables and script fields live in Play
  - 55.2.2 Call stack - `-` - not built for text scripts
- **Code:** `editor/main.cpp`
- **API:** none
- **Editor:** Variables window
- **Docs:** `docs/editor.md`
- **Tests:** none
- **Status:** Partial - variables show live; no call stack or expression watch.
- **Exists today:** Variable display; no stack or expression watch.
- **Suggested priority:** Medium - follows 55.1.
- **Open:** first step: expose the interpreter's frame and locals read-only to the editor while paused.
- **Related:** 55.1, 34.2
- **Words:** watch window, call stack, locals, inspect variable, expression watch
- **Seen as:** cannot see a local variable
- **Ask me like:** "show local variables and the call stack when a script is paused"

### 55.3 Script profiler
- **What:** Show which scripts and functions take the time.
- **Parts:**
  - 55.3.1 Aggregate script time - `Scene/Scene` - the profiler times `Scripts.Update` and `Scripts.LateUpdate` as one block each
  - 55.3.2 Per-script timing - `-` - not built
- **Code:** `Scene/Scene`, `Core/Profiler`
- **API:** `OKAY_PROFILE`
- **Editor:** Profiler window shows the Scripts category
- **Docs:** `docs/core_logging_profiling.md`
- **Tests:** `profiler`
- **Status:** Basic - the profiler shows one number for all scripts.
- **Exists today:** One number for all scripts.
- **Suggested priority:** Medium - quick to add with `OKAY_PROFILE` per script object, and shows up in the existing Profiler window.
- **Open:** first step: a profile scope named after the script around each `ScriptComponent::Update` (off unless the profiler window is open).
- **Related:** 23.1, 13.4
- **Words:** script profiler, slow script, per-script time, Scripts.Update
- **Seen as:** the Scripts bar is big and I do not know which script
- **Ask me like:** "show per-script time in the Profiler"

## 56. Platform targets and store packaging

### 56.1 Desktop packaging
- **What:** Produce installable builds for Windows, Linux and macOS with the right layout, icons and installers.
- **Parts:**
  - 56.1.1 Windows - `scripts/build-windows-dist.sh`, `scripts/okayspace-installer.nsi`, `scripts/build-windows-exe.sh` - cross-compiled with MinGW from Linux, NSIS installer, release workflow `windows-exe`
  - 56.1.2 Linux - `scripts/build.sh`, `.github/workflows/release.yml` - `linux-server` job builds the dedicated server; desktop builds from source
  - 56.1.3 Build Game - `editor/main.cpp` - exports data, config and the player beside it for Windows or Linux
  - 56.1.4 Steam - `steam/upload.sh`, `docs/steam_release.md` - depot and app build files
- **Code:** `scripts/build-windows-dist.sh`, `scripts/okayspace-installer.nsi`, `scripts/build-windows-exe.sh`, `scripts/build.sh`, `steam/upload.sh`, `editor/main.cpp`
- **API:** Build Settings dialog
- **Editor:** File > Build Settings
- **Docs:** `docs/packaging.md`, `docs/releasing.md`, `docs/steam_release.md`
- **Tests:** `publish_supabase_checks`; CI jobs `windows-cross`, `windows-export`
- **Status:** Partial - Windows and Steam are covered; no macOS bundle and no Linux installer or AppImage.
- **Exists today:** Parts above. `README.md` mentions `launch.sh` for Linux and macOS from source.
- **Suggested priority:** Medium - macOS and Linux packages widen the audience of exported games.
- **Open:** first step: Linux AppImage or tarball for exported games from the same Build dialog; macOS app bundle later (needs a macOS build machine).
- **Related:** 18.1, 18.4, 15.7
- **Words:** packaging, installer, NSIS, AppImage, macOS bundle, Linux build, Steam depot, distribution
- **Seen as:** players on Linux or Mac cannot get a build
- **Ask me like:** "add a Linux AppImage export to Build Game"

### 56.2 Mobile packaging
- **What:** Build, sign and ship Android and iOS apps with touch controls and store assets.
- **Parts:**
  - 56.2.1 Android export - `editor/main.cpp`, `docs/web_mobile.md` - Build Settings writes game data into an assets folder plus a guide; the APK is built by the author in Android Studio
  - 56.2.2 iOS - `docs/web_mobile.md` - a guide only
  - 56.2.3 Mobile UI - `Components/UISafeArea`, `Components/UITouchScroll` - safe areas and touch scrolling (area 12.9)
- **Code:** `editor/main.cpp`, `Components/UISafeArea`, `Components/UITouchScroll`
- **API:** Build Settings target
- **Editor:** Build Settings (Android option), phone and tablet emulation in the Game view
- **Docs:** `docs/web_mobile.md`
- **Tests:** `ui_nav_touch`
- **Status:** Basic - data export and a guide; no build, signing or store packaging.
- **Exists today:** Export plus guide; the editor can emulate phone screens in the Game view.
- **Suggested priority:** Low - depends on whether mobile is a target.
- **Open:** first step: a CI job that builds a sample APK from the guide so the guide is known to work.
- **Related:** 18.2, 12.9, 8.4
- **Words:** Android, iOS, APK, AAB, mobile build, store packaging, touch, signing
- **Seen as:** cannot publish to the Play Store from the editor
- **Ask me like:** "build an Android APK in CI from the sample project"

### 56.3 Web export
- **What:** Run games in a browser.
- **Parts:**
  - 56.3.1 Emscripten build - `scripts/build-web.sh`, `player/CMakeLists.txt` - the player runs under `emscripten_set_main_loop`; CI job `web-build`
  - 56.3.2 Web export from Build Game - `editor/main.cpp` - writes a game folder and build instructions
- **Code:** `scripts/build-web.sh`, `editor/main.cpp`
- **API:** Build Settings target Web
- **Editor:** Build Settings
- **Docs:** `docs/web_mobile.md`
- **Tests:** CI `web-build`
- **Status:** Partial - builds in CI; networking (UDP) does not work in the browser (per `docs/web_mobile.md`).
- **Exists today:** Parts above.
- **Suggested priority:** Low.
- **Open:** first step: package the web build as a zip ready for itch.io from the editor when emscripten is installed.
- **Related:** 18.2
- **Words:** web export, WebAssembly, Emscripten, WebGL, itch.io, browser game
- **Seen as:** web build needs manual steps
- **Ask me like:** "make Build Game produce an itch.io-ready zip for web"

### 56.4 Console platforms
- **What:** Ship on game consoles.
- **Parts:**
  - 56.4.1 No console support - `-` - no console SDK code, renderer backends or input mappings were found
- **Code:** -
- **API:** none
- **Editor:** none
- **Docs:** -
- **Tests:** none
- **Status:** Planned - nothing for consoles exists.
- **Exists today:** Renderers are software, OpenGL, Direct3D 11 and Direct3D 12 (Windows only); input is SDL.
- **Suggested priority:** Low - console SDKs are under platform agreements and need dedicated work; listed only for completeness.
- **Open:** first step: none until the owner decides; keeping platform code behind the `Platform/*` interfaces helps.
- **Related:** 56.1, 8.2
- **Words:** console, Switch, PlayStation, Xbox, certification, platform SDK
- **Seen as:** requests for console releases
- **Ask me like:** "what would porting to a console need"

## 57. Documentation website

### 57.1 Static documentation site
- **What:** A searchable website built from the Markdown docs, with navigation by topic, versions and links.
- **Parts:**
  - 57.1.1 Markdown docs - `docs/README.md` - 59 Markdown pages in `docs/` grouped by task, with an index
  - 57.1.2 Generated API page - `docs/engine_api.md` - generated from the binding table and checked by a test that fails when it is stale
  - 57.1.3 Site generator and hosting - `-` - not found (no site config or Pages workflow in the repository)
- **Code:** `docs/README.md`, `docs/engine_api.md`
- **API:** none
- **Editor:** Help menu opens in-editor windows, not the site
- **Docs:** `docs/README.md`
- **Tests:** `vs_types` (checks that `docs/engine_api.md` is up to date)
- **Status:** Basic - good Markdown, no website.
- **Exists today:** Parts 57.1.1 and 57.1.2.
- **Suggested priority:** Medium - docs are already plentiful; a site makes them findable.
- **Open:** first step: a static site generator over `docs/` with a CI job publishing to GitHub Pages; docs/archive is out of scope.
- **Related:** 19.1, 51.4, 57.2
- **Words:** documentation website, docs site, GitHub Pages, search docs, manual, handbook
- **Seen as:** have to browse Markdown files on GitHub; no search
- **Ask me like:** "publish the docs folder as a searchable site"

### 57.2 Generated API reference
- **What:** Reference pages for C++ and script APIs that are always in step with the code.
- **Parts:**
  - 57.2.1 Engine API from the binding table - `Script/EngineApi`, `docs/engine_api.md` - one entry per script property and method
  - 57.2.2 Script builtins - `docs/scripting_reference.md` - written by hand, area by area; the editor also lists builtins
  - 57.2.3 C++ headers - `-` - not generated (no Doxygen or similar)
- **Code:** `Script/EngineApi`
- **API:** `OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types`
- **Editor:** Scripting Reference window
- **Docs:** `docs/engine_api.md`, `docs/scripting_reference.md`
- **Tests:** `vs_types`
- **Status:** Partial - one generated page and one hand-written list.
- **Exists today:** Generated Engine API page; hand-written builtin list; editor docs window.
- **Suggested priority:** Low.
- **Open:** first step: generate `scripting_reference.md` from the same builtin table the editor window uses so they cannot drift.
- **Related:** 13.6, 24.4
- **Words:** API reference, script reference, Doxygen, generated docs, builtins list
- **Seen as:** a builtin exists but is missing from the docs
- **Ask me like:** "generate the builtins reference from the code"

### 57.3 Docs inside the editor
- **What:** Search and read documentation without leaving the editor.
- **Parts:**
  - 57.3.1 Scripting Reference window - `editor/main.cpp` - reference and tutorial for scripts
  - 57.3.2 Docs window for the rest - `-` - not built
- **Code:** `editor/main.cpp`
- **API:** Help > Scripting Reference
- **Editor:** Help menu
- **Docs:** `docs/scripting_reference.md`
- **Tests:** none
- **Status:** Basic - only scripts have an in-editor reference.
- **Exists today:** Scripts only.
- **Suggested priority:** Low - overlaps 51.4.
- **Open:** first step: open `docs/*.md` pages in an editor window (the Markdown subset used is small).
- **Related:** 51.4, 57.1
- **Words:** docs in editor, help window, search docs, F1 help
- **Seen as:** need a browser to read the manual
- **Ask me like:** "show docs pages inside the editor"

## 58. Performance budgets and CI perf tests

### 58.1 Performance budgets
- **What:** Written limits (frame time, draw calls, memory, triangles) per scene or system, checked automatically.
- **Parts:**
  - 58.1.1 Optimization report thresholds (#574) - `Optimize/SceneOptimizationReport` - `OptSettings` with a draw call budget, texture memory budget and similar limits
  - 58.1.2 Profiler data - `Core/Profiler` - per-section times that a budget could check
  - 58.1.3 Budget file and checker - `-` - not built
- **Code:** `Optimize/SceneOptimizationReport`, `Core/Profiler`
- **API:** `AnalyzeScene`, `OptSettings`
- **Editor:** none
- **Docs:** `docs/optimization_tools.md`
- **Tests:** `optimize_report`
- **Status:** Planned - building blocks only.
- **Exists today:** Data layers for measuring; no budget definitions.
- **Suggested priority:** Medium - gives the owner an early warning before a scene gets slow.
- **Open:** first step: a `budgets.okaybudget` text file (frame ms, draw calls, triangles, texture MB per scene) and a command that reports pass or fail.
- **Related:** 27.3, 23.1, 23.2
- **Words:** performance budget, frame budget, draw call limit, memory budget, scene limits, thresholds
- **Seen as:** a level gets slower with each change and nobody notices
- **Ask me like:** "define per-scene performance budgets and check them"

### 58.2 Benchmark scenes
- **What:** A fixed set of scenes that stress the engine (many objects, many lights, UI, agents) and print numbers.
- **Parts:**
  - 58.2.1 UI render benchmark - `tests/test_ui_render_bench.cpp` - 500 widgets in a 10k-object scene must use under 1 ms CPU, under 20 draw calls and no allocations after warm-up (timing only in optimized builds)
  - 58.2.2 Perf dump hook - `editor/main.cpp` - `OKAY_PERF_DUMP` prints an average frame breakdown every two seconds
  - 58.2.3 Play-test harness (#572) - `Testing/PlayTest` - headless fixed-step runs, a base for scene benchmarks
  - 58.2.4 Frame statistics (#581, #585) - `Core/FrameStats` - average fps, 1% low, 0.1% low, worst frame and hitch count over a 240-frame window, a text or JSON summary of a recorded session, a CSV export (`ToCsv`, `SaveCsv`) and the console commands `time.stats` and `time.stats_csv` (`Core/LoopConsole`); can sync into `Debug/FrameHistory`
- **Code:** `tests/test_ui_render_bench.cpp`, `editor/main.cpp`, `Testing/PlayTest`, `Core/FrameStats`, `Core/LoopConsole`
- **API:** `OKAY_PERF_DUMP` environment variable
- **Editor:** none
- **Docs:** `docs/qa_workflow.md`
- **Tests:** `ui_render_bench`
- **Status:** Basic - one acceptance benchmark exists.
- **Exists today:** Parts above.
- **Suggested priority:** Medium.
- **Open:** first step: benchmark scenes for 10k meshes, 100 lights, 200 agents and 1000 particles, run headless with the software renderer for repeatability.
- **Related:** 33.1, 58.1, 52.5
- **Words:** benchmark, perf test, stress test, frame time test, ui_render_bench, OKAY_PERF_DUMP
- **Seen as:** cannot tell if a change made things slower
- **Ask me like:** "add benchmark scenes for objects, lights and agents"

### 58.3 Performance regression guard in CI
- **What:** CI compares benchmark results with a baseline and fails or warns when something gets slower.
- **Parts:**
  - 58.3.1 CI jobs - `.github/workflows/ci.yml` - five jobs run tests, sanitizers, Windows cross-build, export check and web build; none records timings
  - 58.3.2 Baseline compare - `-` - not built
- **Code:** `.github/workflows/ci.yml`
- **API:** none
- **Editor:** none
- **Docs:** `docs/qa_workflow.md`
- **Tests:** `ui_render_bench`
- **Status:** Planned - CI records no timings.
- **Exists today:** Test jobs only; timing checks in tests run only in optimized, non-sanitized builds.
- **Suggested priority:** Low - after 58.2; shared runners are noisy, so compare ratios with tolerance.
- **Open:** first step: store the benchmark output as a CI artifact and print a table in the job summary before gating anything.
- **Related:** 33.3, 18.3, 58.2
- **Words:** performance regression, CI perf, baseline, benchmark compare, slow commit
- **Seen as:** a slow change merged unnoticed
- **Ask me like:** "record benchmark numbers in CI and show them in the summary"

### 58.4 Build size and load-time report
- **What:** Tell the author how big a build is, what is largest, and how long scenes take to load.
- **Parts:**
  - 58.4.1 Build size report - `editor/main.cpp` - Build Game counts total bytes, splits by category (exe, libraries, data, assets) and lists files by size; reports missing referenced files
  - 58.4.2 Load times - `-` - not built
- **Code:** `editor/main.cpp`
- **API:** none
- **Editor:** Build result popup
- **Docs:** `docs/editor.md`
- **Tests:** none
- **Status:** Partial - size is reported; time is not.
- **Exists today:** Per-file breakdown after a build.
- **Suggested priority:** Low.
- **Open:** first step: log scene load time and asset decode time (the async loader in #570 reports progress) in the Profiler.
- **Related:** 18.1, 27.4, 20.2
- **Words:** build size, load time, asset size report, startup time, largest files
- **Seen as:** the build is large and I do not know why
- **Ask me like:** "show scene load time in the Profiler"
