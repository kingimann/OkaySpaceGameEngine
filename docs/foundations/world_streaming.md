# World streaming (foundation)

Status: design page for the large-world streaming foundation. Phase 1 code lives in
`engine/include/okay/Scene/WorldPartition*.hpp` (namespace `okay::worldpart`). Everything here is
optional and off unless a game adds the pieces. No existing scene, default or renderer changes.

## What we are copying, and what we are not

Unreal's World Partition keeps one big map as one editable thing and cuts it into grid cells
behind the scenes. Players and other "streaming sources" make cells load and unload. Data layers
switch groups of actors on and off. HLOD draws far cells as merged proxies. Level instancing places
a sub-level many times. Actors keep a stable id so saves, networking and references survive a
cell going away and coming back.

| Unreal World Partition | Us, phase 1 to 3 | Later |
|---|---|---|
| One-file-per-actor storage, grid cells computed in the editor | A scene is cut into one text file per cell ("chunk"). The splitter assigns each top-level object to the cell of its origin. | Editor window that edits the whole map as one |
| Streaming sources (player, camera, any actor) with load radius | `StreamingSource` component: load radius, unload radius, priority. Many sources at once | Per-source shape (cone, box) |
| Hysteresis | Unload radius is never below the load radius; a cell loads when any source is inside the load radius and unloads only when every source is outside the unload radius | |
| Async loading and a per-frame cost | Loader abstraction: a synchronous one and a budgeted queue (time budget per frame, nearest first). Granularity is one cell | Worker-thread decode of chunk text, finer than one cell |
| Stable actor ids | Every object keeps its `guid` across unload and load (the scene reader does not keep it on a merge, so the chunk code restores it itself) | Network ids derived from the same guid |
| Data layers | Phase 3: a layer name on a chunk root, a set of enabled layers on the streamer | Editor UI, runtime layer states |
| HLOD | Not started | Merged low-detail mesh per cell (`MeshSimplify` exists) |
| Level instancing | Not started | A chunk placed with an offset (`MergeFromText` already takes an offset) |

Honest differences: Unreal streams per actor with its own cell hash and an editor that hides the
cells. We stream per top-level object tree, one cell at a time, with the cells visible as files. We
have no editor window for it yet.

## What already exists in the engine (we reuse it, not duplicate it)

* `Components/WorldStreamer` (27.4): one target, hand-written cell prefabs, Chebyshev rings.
  It does not know about saves, ids, budgets or several sources. We leave it alone.
  The new class is called `PartitionStreamer` so the names do not clash.
* `Optimize/StreamingGrid`: one viewer, budgets, priority, async hooks. It is the closest relative.
  We reuse its `CellCoord` and `GridPlane`. It cannot take several sources, and a cell size of 0 or
  a huge position is not rejected there (the cast to `int` is undefined for a huge value). The new
  planner is a small multi-source version. A later step can fold the two together.
* `SceneSerializer`: `SerializeObject` and `InstantiateFromText` write and read one object tree;
  `MergeFromText` takes an offset. `Scene::BeginAsyncLoad` makes a few objects per frame but works on
  a whole scene file, so it is not a fit for cells.
* `Core/TimeBudget`: the frame time slice. The budgeted loader uses it with a fake clock in tests.
* `Core/SaveFramework`: see "Saves" below. It has a hazard we must tell you about.

## Phase 1: grid, chunks, sources, streamer (this change)

* `WorldGrid`: cell size (rejected if 0, negative, NaN or infinite), plane (XZ default, XY, XYZ), cell
  of a position (floor, so negative positions work), box of a cell, neighbours, distance from a point
  to a cell box. A position whose cell index would pass a limit (about one million cells either way) is
  "outside the world", not a wrapped number.
* Chunk file (`okaychunk 1`): a header, a cell address, a checksum (FNV-1a 64), then one block per
  top-level object: its guids in tree order and its `SerializeObject` text. Sizes are length-prefixed
  and checked against what is left, so a hostile file fails cleanly and never reads past the end.
* Splitter: `SplitSceneIntoChunks(scene, grid, options)`. An object goes to the cell of its origin. An
  object tagged `AlwaysLoaded`, or carrying a `StreamingSource`, or whose position is not a finite
  in-range number, goes to the always-loaded ("persistent") chunk. Scene-wide settings (gravity, fog,
  name) are not in chunks: they stay in the host scene.
* `StreamingSource`: a component with load radius, unload radius, priority. It saves with the scene
  through the component serializer registry.
* `PartitionStreamer`: each `Update` it asks "which cells want to be loaded" from the sources and the
  hysteresis rule and hands them to a loader. Unloading first writes the live state of each object back
  to the chunk store, so a round trip keeps changes.
* Loaders: `SyncCellLoader` (everything queued is loaded at once, for tools and tests) and
  `BudgetedCellLoader` (a time budget per frame, at least one cell per frame so a slow cell still
  finishes, nearest or highest priority first, a cancel when the cell is no longer wanted).

Effort: about 3 to 4 working weeks of focused work for phase 1, including these tests.

## Phase 2: async with a frame budget, priority by distance

* Bring `PartitionStreamer` into `Scene::Update` as a component (phase 1 gives the class and a
  `Tick(scene, dt)` that a component calls).
* Cell loads cut finer than one cell (per root) so one dense cell cannot blow the budget.
* A worker thread reads and checks the file and splits it into records; only object creation stays on
  the main thread (the scene is not thread safe). `Core/Jobs` exists for this.
* Priority also by view direction (the idea `StreamingGrid` already has).
* A debug overlay of cell states.

Effort: 2 to 3 weeks.

## Phase 3: hooks for other systems, data layers

What each system must be told when a cell appears or goes away:

* **Physics (2D and 3D).** Colliders and rigid bodies are components, so they register with the world
  when the objects are created and leave on destroy, which is what happens today. What is open: a
  body that is asleep in a cell and wakes when its cell loads; bodies that touch across a cell edge
  (a pile that crosses the boundary is cut in two by object origin); a loaded cell dropping onto a
  moving body. Needs a test with a rigid body resting on a cell edge.
* **NavMesh.** A baked mesh covers the whole scene. When a cell goes away the mesh still has its
  triangles (agents walk on air) and a cell that loads has no mesh. The plan is a per-cell bake stored
  next to the chunk and stitched on load (feature map 45.3). Not started.
* **Audio.** Sources inside an unloaded cell stop; a looping source restarts at the start when its
  cell returns. A stored play position per object is the fix. Not started.
* **Navigation agents and AI** walking out of a loaded area: they freeze when their cell unloads. A
  "simulate while unloaded" flag is an open design question.
* **Data layers.** A string on the chunk root ("Night", "Quest3"), a set of enabled layers on the
  streamer, a chunk is loaded only when its layer is on.

Effort: 4 to 6 weeks (navmesh stitching is the largest part), spread over several releases.

## Multiplayer

Streaming and multiplayer meet in three places.

1. **Network ids.** `NetworkManager` names spawned objects by `netId` strings. An object that unloads
   and loads must come back with the same id or a remote peer sees a different object. Phase 1 keeps
   the scene `guid` stable. Deriving the `netId` of a streamed object from its guid is phase 3.
2. **Who streams.** On a server, cells must be loaded for the union of all players' sources, because
   the server is the authority. On a client, a client streams only for itself and only draws what the
   server says exists. The server must not unload a cell that a remote player still needs; this is
   what the multi-source rule gives (a cell stays while any source is inside the unload radius).
3. **State sync.** Replicated objects in a cell that unloads on a client must be despawned locally
   without telling the server to destroy them (`NetworkManager::Despawn` does tell the others, so a
   streamer must never call it). Not done and not tested in phase 1.

Do not enable streaming together with a networked scene until phase 3 has landed.

## Saves

* A cell that is streamed out still holds object state in the chunk store. The streamer can write
  all dirty state (`CaptureState`) into one list of single-line strings that fit a `SaveBlock`, and
  put it back (`RestoreState`). So a save made while a cell is unloaded keeps its objects.
* **Hazard in `SaveSystem::CaptureScene`:** it records a Saveable object that is missing from the
  scene as "destroyed", and applying that save destroys it. An unloaded cell's Saveable objects would
  look destroyed. Until `SaveSystem` is told about streamed-out keys (a small hook, not done here),
  do not put `Saveable` on objects in streamed cells; the chunk state above already keeps their
  components. This is listed in `docs/UNFINISHED.md`.
* The chunk stores the object's saved text. Runtime-only objects (transient, generated rigs) are not
  written, as with every other scene save.

## Things this change does not do

No editor window or Scene-view overlay, no threads beyond the budgeted queue, no physics, navmesh,
audio or network hooks, no data layers, no HLOD, no level instancing, no floating origin (positions
are still `float`, so a world far from the origin jitters whatever we stream), no change to the
existing `WorldStreamer`.

## Risks

* Objects that reference each other across cells (a door and its switch). A reference by guid to an
  object in another cell fails when that cell is not loaded. A "cross-cell reference" list is needed.
* Parent links cross cells never: a tree is one unit and lives in the cell of its root.
* A tree that is very large in one cell makes one big hitch (budget granularity is a cell).
* Chunk files are text; a 1 km square of dense scenery will have many files. Phase 2 should add a
  pack file.

## Effort summary

| Step | Estimate |
|---|---|
| Phase 1: grid, chunk, splitter, sources, streamer, two loaders, tests | 3 to 4 weeks |
| Phase 2: component in the scene loop, finer loads, worker decode, overlay | 2 to 3 weeks |
| Phase 3: physics, navmesh, audio, network id and save hooks, data layers | 4 to 6 weeks |
| Editor window (split command, cell overlay, data layer UI) | 2 to 3 weeks |
| HLOD and level instancing | 4 to 6 weeks each |
