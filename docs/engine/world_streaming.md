# World streaming, level streaming, chunk streaming and World Partition

> **What this is:** how the engine keeps only the part of a big world near the player in memory (feature map 2.6.6 and
> 27.4). Four pieces share one loading path: World Partition (split a scene into cells), the World Streamer (stream the
> cells), Level Streaming (stream whole scenes or sections) and the Chunk Streamer (generate procedural chunks, used by the
> endless Voxel World). Mesh, texture and audio streaming (LOD and asset streaming) are a separate piece of work.

## The shared loading path: `Scene/SceneStreamQueue`

A streamed document (a cell prefab, a level scene, a remembered cell) comes in like this:

1. `RequestFile` / `RequestText` read the file and unpack it on the engine job system (`EngineJobs()`, `Core/Jobs`).
   With 0 workers (a WebGL build, a deterministic test) the read runs inline; nothing else changes.
2. `Pump` (main thread, once a frame) hands a finished read to `Scene::BeginAsyncLoad(std::unique_ptr<SceneLoadJob>, opts)`,
   new in this change: the scene makes the objects a few at a time. `AsyncLoadOptions::timeBudgetMs` (new) keeps making
   batches of `objectsPerFrame` until that many milliseconds are used (at least one batch a frame). At most
   `maxAttaching` documents are made at once, so the budget is the frame's total. Nothing in a document Awakes or Starts
   until all of it is in (the old async-load rule).
3. `Release` stops a load (what it made is removed) or removes what a finished load brought in (`Scene::UnloadAdditive`).
   `Scene::LoadedRoots(handle)` (new) gives the top-level objects a finished load made.

`Flush` finishes every read and makes every waiting document at once, for the first frame of a level and for teleports.
The worker only fills a slot that the main thread reads after the job is done, so the header has no locks or threads
(rule 21a).

## World Streamer (`Components/WorldStreamer`)

Cell files `<prefix>_<x>_<z>.okayprefab` in `folder` (a relative folder is also looked for next to the scene file; the
folder is remembered when the component is made by a scene load). The folder is scanned once at Start (`Rescan()` looks
again). Decisions come from `Optimize/StreamingGrid`:

- square rings (`CellDistance::Chebyshev`, new): every cell within `loadRadius` cells of a source loads; a cell unloads
  only past `unloadRadius` (hysteresis);
- several sources (`StreamingGrid::Update(std::vector<StreamingViewer>)`, new): `target` (or the main camera) and every
  enabled `StreamingSource`; a cell's distance is to the nearest source and its priority the best any source gives;
- nearest first with a bonus for cells in front of a source (`viewDirectionWeight`), at most `maxLoadsPerFrame` new
  requests a frame and `maxInFlight` at once, and an optional `memoryBudgetMB` (the cell file sizes).

Loads go through the queue above (`backgroundLoading`, `attachBudgetMs`). With `rememberChanges` (default on) a cell that
unloads is serialized first (`SceneSerializer::SerializeObject` per root) and the next load of that cell uses the copy:
moved, damaged and destroyed objects stay that way for the rest of the session (not across a saved game yet). A cell
whose voxel terrain was dug stays loaded either way (the old rule: the voxel field is written quantized, so a copy would
not give the exact dig back). `debugOverlay` keeps one never-saved object,
"Streaming Debug", whose mesh draws the grid (green loaded, yellow loading, grey on disk, white squares for the sources).
`SyncNow()` loads every wanted cell at once; `Start` uses it, so the first ring is there on the first frame.

The scene record `worldstreamer` gained ten trailing fields (background, budget, limits, look-ahead, memory budget,
remember, sources, overlay, overlay height). Older files read with the defaults; older builds ignore the extra numbers.

## World Partition (`Scene/WorldPartition`, editor window)

`PlanPartition(scene, settings)` decides, for every top-level object, a cell (the cell of the centre of its drawn
bounds, `Scene::WorldBounds`) or "always loaded":

- a `PartitionLayer` component (Auto, Always Loaded, Streamed) on the object decides first;
- cameras, directional lights, UI canvases, World Streamers, Level Streaming Volumes, Streaming Sources, Voxel Worlds
  and objects tagged `Player` stay loaded;
- an object that draws nothing and has no collider stays loaded (a game manager); one with a collider streams by position;
- an object wider than `maxSpanCells` cells stays loaded, or goes to its centre's cell with `SpanRule::BoundsCentre`.

`WritePartition(scene, settings, outScene, cellFolder)` writes one prefab per non-empty cell (objects at their world
positions under a root named like the file), removes this prefix's cell files that the plan no longer has (other files in
the folder are left alone), and writes a new scene: the source without the streamed objects plus a "World Streamer" whose
folder is relative to the new scene. The source scene is not changed. Running it again updates the cells; running it on a
partitioned scene (one whose World Streamer streams that folder) is refused.

Editor: View > Editors & Tools > World Partition. Cell size, cell file name, what wide objects do, load and unload radius,
"Show the grid in the Scene view" (cells tinted by how many objects they get, with counts; yellow dots for objects that stay
loaded), a list of what stays loaded and why, "Partition into cells" / "Update the cells" (needs a saved scene; writes
`<scene>_partitioned.okayscene` and `<scene>_cells/` next to it) and "Open the partitioned scene". In Play the Scene view
draws every World Streamer's cells by state; a selected World Streamer shows its cell files in Edit mode; Level Streaming
Volumes draw their box (and, selected, the preload distance).

## Level streaming (`Components/LevelStreaming`)

`LevelStreaming::LoadLevelAsync(scene, nameOrPath, offset, onDone, notify, sceneDir)`, `UnloadLevel`, `IsLevelLoaded`,
`IsLevelLoading`, `LevelProgress`, `Levels`, `LevelRoots`, `SetAttachBudgetMs`. A name is a build-list name, a path, a path
with `.okayscene` added, or either under the scene's folder. Asking for a level that is loading or loaded does not load it
twice. The streamed levels live on a hidden, never-saved "Level Streaming" object, so a replacing scene load takes them away.
The object that asked gets the custom event `LevelLoaded` (value: the level name), `LevelFailed` or `LevelUnloaded`: a
visual script's On Custom Event node, a script's `OnEvent_LevelLoaded(name)`.

Scripts: `load_level_async(name [, x, y, z])`, `unload_level(name)`, `is_level_loaded(name)`, `level_progress(name)`.
Visual scripts listen with On Custom Event (`LevelLoaded`, `LevelUnloaded`, `LevelFailed`); a load is started by a Level
Streaming Volume or a script (dedicated Load Level / Unload Level nodes are not done).

`LevelStreamingVolume`: a box (`size`, centred on the object) that loads `level` when a source (`target` or the main camera,
plus Streaming Sources) comes within `preloadDistance` of it, and unloads it once every source is farther than
`preloadDistance + unloadMargin`; `keepLoaded` never unloads; `offset` moves the level's objects.

## Chunk streaming (`Optimize/ChunkStreamer`) and the Voxel World

`ChunkStreamer` streams chunks named by an integer coordinate around one or more viewers, for endless worlds: chunks are
registered with the `StreamingGrid` as viewers come near and forgotten well behind them. Hooks: `generate` (on a worker;
plain data only), `attach` (main thread; at most `maxAttachPerFrame` chunks and `attachBudgetMs` a frame), `detach`
(return data to keep an edited chunk; the next load uses it instead of generating). A generator that throws is retried later.

`VoxelWorld` uses it: every chunk is an ordinary `VoxelTerrain` (diggable, caves, colours, the Voxel Digger) on a
never-saved child "Voxel Chunk x,z". The ground is one function of the world position (our own value noise: a hash of the
lattice point, the 6t^5 - 15t^4 + 10t^3 fade, octaves halving in size), so neighbours share their border samples exactly
and the surface joins without cracks. Generation runs on workers; meshing and the upload run on the main thread,
`maxChunksPerFrame` and `meshBudgetMs` a frame. A dug chunk keeps its shape when it streams out and back in
(`rememberEdits`, for the session).

## Examples (shipped in `examples/streaming`)

- `open_world_source.okayscene` - a 12 x 12 block town (about 1,700 objects) to try World Partition on.
- `open_world.okayscene` + `open_world_cells/` - the same town partitioned into 60 m cells (load radius 2), overlay on.
- `level_corridor.okayscene` + `corridor_b`, `corridor_c` - two Level Streaming Volumes load the next corridor sections.
- `infinite_voxels.okayscene` - an endless Voxel World with caves.

All use a fly camera (WASD, hold the right mouse button to look, Shift faster). `tests/test_streaming_examples.cpp`
loads each, flies through it, and prints the frame times (OKAY_REGEN_STREAMING=1 rebuilds them).

## Not done

- Saves do not know about remembered cells or chunks (they are kept for the session only; area 35.3).
- Navmesh and physics broadphase per cell (27.4 package 4), and mip / mesh / audio streaming (the other agent's work).
- Dedicated Load Level / Unload Level visual-script nodes.
- Voxel World: an edit that crosses a chunk border changes only the chunk under the brush; lighting normals at chunk
  borders are one-sided, so a faint seam can show in the shading; materials are not generated per layer yet.
- Cells are on the XZ plane only for the World Streamer (the grid itself supports XY and 3D).
