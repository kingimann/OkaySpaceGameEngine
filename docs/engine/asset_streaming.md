# Asset streaming (design note)

Internal record (owner request 2026-11-25: "a centralized, native C++ asset streaming system ... automatic by default").
Feature map: 1.11 (Asset Manager, the home), 27.4 and 45.1 (world streaming).

## One system, one home

The streamer is the **Asset Manager** (`engine/include/okay/Core/AssetManager.hpp`). It already had one cache entry per
file, reference counted handles, per-type loaders, background decodes on the job system and hot reload. Streaming is
added to it, not beside it: `AssetManager::Stream<T>(path, priority)` is `LoadAsync` with a priority and a cache that
keeps decoded assets for reuse under a RAM budget. Everything else that streams goes through it:

- **Scene cells**: `Scene::BeginAsyncLoad` with `AsyncLoadOptions::stream` (the existing multi-frame scene load, extended).
- **World cells**: `WorldStreamer` (the existing component) now drives `optimize::StreamingGrid` (the existing,
  previously unused cell planner) and loads each cell as a streamed scene load.
- **Meshes**: the new `modeling::MeshFile` loader on the Asset Manager; `LoadMeshAsset` (the one mesh file reader every
  caller uses) copies a streamed decode when the file has not changed (`AssetManager::FindFresh`).
- **Textures**: the texture library (`Render/TextureAsset`, already on the Asset Manager) gets `textures::Prefetch`,
  which decodes through `AssetManager::Stream`; its memory budget now defaults to the streamer's video budget.

No new manager, folder or "v2" was made.

## How a streamed cell loads (round 1)

1. `BeginAsyncLoad(path, {stream = true, priority})` returns a handle at once. A job (EngineJobs, inline with 0 workers)
   reads and unpacks the file (`SceneLoadJob::OpenFile`), lists the files it names (`ExtractSceneRefs`, the Asset
   Database scanner) and asks the streamer for its mesh files (`Stream<MeshFile>`, same priority).
2. When the read is done the main thread asks the texture library to prefetch the textures it names. The cell waits
   (phase `Reading`) until its meshes and textures are decoded: dependencies first.
3. Objects are made one at a time (`SceneLoadJob::Step`) while the frame budget lasts (`AssetManager::FrameBudgetMs`,
   default 4 ms, shared by every streamed load and spread unload in the frame; at least one object per frame so a load
   always moves). Higher priority loads go first. Nothing Awakes until the whole cell is in (the existing rule).
4. Unloading (`UnloadAdditiveSpread`) removes the objects children-first in batches sized to the remaining budget. A
   load that is still reading or decoding is cancelled: its read job is cancelled if it has not started and its queued
   mesh decodes are dropped (`CancelStream`). A cell with nested prefabs or a variant is made in one go (PrefabTools
   cannot make it in steps yet).
5. `CompleteLoad(handle)` finishes a load in one call (teleport, loading screen); `WorldStreamer::SyncNow` uses it.

Per-cell state: `Scene::GetLoadPhase` / `WorldStreamer::GetCellState`: Reading (queued or reading or decoding its
assets), Creating, Loaded, Unloading, Unloaded.

## Budgets (Automatic mode)

`StreamSettings::automatic = true` by default; no code, zones or per-asset settings are needed.

- RAM: a quarter of physical memory (`AssetManager::SystemMemoryBytes`: `sysconf` on Linux, `GlobalMemoryStatusEx` on
  Windows), clamped to 256 MB .. 8 GB. When the cache is over it, `Pump()` lets go of cached assets nobody holds:
  lowest priority first, then the one wanted longest ago. Assets in use are never evicted.
- VRAM: 80% of what a renderer reported (`ReportVideoMemory`), else an eighth of physical memory, 512 MB .. 2 GB. It is
  the texture library's budget (`textures::BudgetBytes`), which already lets go of textures not drawn lately.
- Frame: 4 ms of main-thread streaming work.
- Custom mode: the numbers in `StreamSettings`; env `OKAY_STREAM_RAM_MB` / `OKAY_STREAM_VRAM_MB` override Automatic.

## Spec items, what each reuses and adds

| Spec item | Reuses | Adds | Round |
|---|---|---|---|
| Async load and unload | AssetManager decode jobs, Scene multi-frame loads, Core/Jobs | background read, spread unload, cancel | 1 (done) |
| RAM and VRAM budgets from hardware | texture library budget | system memory detection, RAM-budget cache eviction, VRAM default | 1 (done; renderers do not report VRAM yet) |
| Distance / visibility / dependency priorities | StreamingGrid (distance + view direction) | priority queue in AssetManager, priority per scene load, assets before objects | 1 (done: distance, view direction, dependencies; true visibility (culling) not used) |
| Predictive loading from movement | StreamingGrid view-direction weight | look-ahead along the target's measured velocity (`WorldStreamer::predictSeconds`, 1 s, saved) | 2 (done early, in round 1) |
| Texture streaming | texture library, `tex::StreamPlanner` / `PlanMips` | prefetch through the streamer | 1: whole textures; mip-level streaming in the renderers: 2 (not started) |
| Mesh streaming | `LoadMeshAsset`, AssetManager Mesh loader | `MeshFile` loader, `FindFresh` | 1 (done) |
| Audio streaming | `AudioStream` (already plays long sounds from disk) | nothing yet | already streams; clip prefetch: 2 |
| Animation, terrain streaming | animation clips, Terrain / VoxelTerrain | nothing yet | 3 (not started) |
| Shared caching and dependencies | AssetManager cache (one entry per file), AssetDatabase reference scanner | cache kept after last user, per-cell dependency wait | 1 (done) |
| Progressive LOD | `Optimize/LodGroup`, `MeshSimplify`, mip chain | coarse-first loads | 2 (not started) |
| Frame-time budgets | Scene multi-frame loads (`objectsPerFrame`) | time budget shared by loads and unloads | 1 (done) |
| World streaming integration | WorldStreamer, StreamingGrid | WorldStreamer on StreamingGrid + streamed loads | 1 (done) |
| Editor (Automatic / Custom, live stats) | Project Settings pages (OkayUI) | Streaming page | 3 (not started; `GetStreamStats` is there for it) |

## Measurements (2026-11-26, round 1)

`test_asset_streaming_stress`: 12 x 12 cells, 20,304 objects (70 mesh renderers with a child each per cell), 6 mesh
files, 6 textures, a 900-frame flight paced at 60 fps. Linux, -O1 build, a shared machine with load about 18 on 4
cores, so frame work is measured as the main thread's CPU time (wall time also counts the OS giving the CPU to other
programs in 4 ms slices). "Heavy" cells keep per-triangle colours, written as about 9 KB of text per object.

| run | load before play | max frame | p99 frame | p50 frame | streaming work p99 | most objects resident | pop-in frames (of 839) |
|---|---|---|---|---|---|---|---|
| streamed | none | 11.1 ms | 9.1 ms | 1.8 ms | 4.3 ms (budget 4) | 8,137 | 7 |
| up front | 2,951 ms | 11.0 ms | 7.8 ms | 6.2 ms | - | 20,305 | 0 |
| heavy streamed | none | 11.9 ms | 10.0 ms | 6.7 ms | 4.6 ms | 9,653 | 114 |
| heavy up front | 7,178 ms | 9.9 ms | 9.1 ms | 6.6 ms | - | 20,305 | 0 |

Read: the streamed world never has the multi-second load, keeps 40-48% of the objects, and its steady frames are 3x
cheaper; frames where cells are being made cost the budget on top. Heavy cells parse too slowly on the main thread to
keep up with the camera at 4 ms a frame (114 pop-in frames): parsing records on the job threads is the next step.
Process RSS was also recorded but is not a fair comparison (the second run reuses the first run's heap).

## Not done / known gaps (round 1)

- Text records are parsed on the main thread inside the budget; one object can only be stopped after, so a big record
  (heavy cells above) costs its parse in one go, and throughput is bound by it.
- Awake/Start of a finished cell's components runs in the frame the cell finishes (not spread).
- Renderers do not upload or drop mip levels by the planner; no GPU renderer reports its video memory.
- Audio clips, animation clips and terrain chunks are not prefetched; no progressive LOD.
- No editor page. Saves still do not cover unloaded cells (35.3). Cells are on the XZ plane only.
