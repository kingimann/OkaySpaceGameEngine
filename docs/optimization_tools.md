# Optimization tools

Engine-side algorithms for feature map area 27: LOD generation, occlusion
culling, batching, streaming and a scene analysis report. They live in
`engine/include/okay/Optimize/` (sources in `engine/src/Optimize/`) and are pure
CPU geometry and arithmetic: no scene, renderer, component or editor types, no
threads, no I/O. Every function is deterministic (same input, same output), so
results are testable and reproducible.

None of this is wired into a renderer, the scene or the editor yet. The pieces
are the building blocks those integrations will call; the integration points
are listed at the end.

```cpp
#include "okay/Optimize/MeshSimplify.hpp"      // 27.1
#include "okay/Optimize/LodGroup.hpp"          // 27.1
#include "okay/Optimize/OcclusionBuffer.hpp"   // 27.2
#include "okay/Optimize/PortalCells.hpp"       // 27.2
#include "okay/Optimize/BatchPlanner.hpp"      // 27.3
#include "okay/Optimize/StreamingGrid.hpp"     // 27.4
#include "okay/Optimize/MipBudget.hpp"         // 27.4
#include "okay/Optimize/SceneOptimizationReport.hpp"
using namespace okay::optimize;
```

These headers are not part of `Okay.hpp`; include the ones you need.

## 27.1 LOD generation

### Simplifying a mesh

`SimplifyMesh(mesh, options)` is a quadric error metric edge collapse
simplifier working on the engine `Mesh`.

```cpp
SimplifyOptions o;
o.targetRatio = 0.25f;                 // keep a quarter of the triangles
SimplifyResult r = SimplifyMesh(mesh, o);
// r.mesh, r.trianglesBefore/After, r.error (world units), r.reachedTarget
```

What it keeps:

- **Topology.** Collapses obey the link condition, so a closed manifold mesh
  stays closed and manifold and its Euler characteristic never changes (a
  sphere stays a sphere, a torus keeps its hole). A closed mesh never goes below
  4 triangles. Winding is preserved (a flip guard refuses collapses that fold a
  neighbouring triangle).
- **Boundaries.** Vertices on an open boundary are only collapsed along the
  boundary, and a heavy quadric penalty (`boundaryWeight`) keeps the outline
  where it was. A hole stays a hole. `lockBoundary` freezes the outline.
- **UV seams and hard edges.** Vertices are welded by position first, then every
  triangle corner keeps its own UV and normal (a "wedge"). An edge whose two sides
  disagree is a seam and is treated like a boundary (`seamWeight`), so textures
  do not tear or smear and flat-shaded edges stay crisp. `lockSeams` freezes
  them. A cube with split normals therefore does not simplify at all.
- **Attributes.** UVs, normals and per-triangle colours ride along. Interior
  vertices may collapse to a better position than either endpoint
  (`optimalPosition`), with UV, normal and skin interpolated from the surface.
- **Skin weights.** `Mesh` has no skin data (it lives on `SkinnedMesh`), so pass
  it in: `o.skin = &skinData` (`joints` and `weights` parallel to the vertices,
  the same layout as `SkinnedMesh::jointIdx/jointWt`). The result's `skin` is
  parallel to the new vertices. A collapse that would blend weights further
  apart than `skinTolerance` is refused. With `skinPolicy = SkinPolicy::Refuse`,
  or when the skin does not match the mesh, the mesh comes back unchanged with
  `skinned = true`, `skinSupported = false` and a `note`. Joint order inside a
  vertex's four slots can change; look weights up by joint id.

Controls: `targetRatio` or `targetTriangles`; `maxError` (stop when a collapse
would move the surface more than this, in world units; with `targetRatio = 1`
it simplifies as far as the error allows); `minTriangles`; `minNormalDot`.

Dropped on purpose: `polyFace`, `seams` and `cornerUVs` (modeling data; the
result has exact per-vertex UVs).

The `error` figures are an RMS-style estimate from the quadrics, the same order
as the real deviation (the tests measure the real one against it), not a hard
bound. Cost is roughly O(n log n); a 49k triangle UV sphere simplifies to 10 %
in about half a second.

### LOD chains

```cpp
LodChain chain = GenerateLodChain(mesh, {1.0f, 0.5f, 0.25f, 0.1f});
for (const LodMeshLevel& l : chain.levels)
    // l.mesh, l.triangles, l.error, l.switchDistance, l.switchScreenHeight
```

Every level is simplified from the original (no error build-up), triangle
counts never increase down the chain, and switch distances strictly grow. The
switch distance of a level is where its error projects to `pixelError` pixels
(`LodChainOptions`: `fovYDeg`, `viewportHeightPx`, `pixelError`, `objectSize`);
`switchScreenHeight` is the same point as a fraction of the viewport height.
LOD0 at ratio 1 is the original mesh.

### LodGroup and SelectLod

`LodGroup` is a plain data class: the levels (best first, each with the
screen-relative height it is left at), the object `size`, `cullScreenHeight`,
`minPixelHeight`, `fadeMode` / `fadeWidth` (cross-fade) and `hysteresis`.
`MakeLodGroup(chain, cullHeight)` fills it from a chain.

```cpp
LodGroup g = MakeLodGroup(chain, 0.01f);
LodView view{1080.0f, 60.0f};                  // viewport height px, vertical fov
LodSelection s = SelectLod(g, distance, view, /*bias*/1.0f, /*previous*/ lastState);
lastState = s.state;                           // feed back next frame
// s.level (-1 = culled), s.fadeTo / s.fade (cross-fade), s.screenHeight
```

`SelectLod` is a pure function. Pass the previous frame's `state` for
hysteresis: going to a worse level needs the object to shrink below
`threshold * (1 - h)`, coming back needs it to grow past `threshold * (1 + h)`,
so an object on a boundary does not flicker. With `CrossFade` the selection
reports the next level and a 0..1 blend weight that reaches 1 exactly at the
switch, so the fade is continuous. `bias` above 1 keeps detail longer.
`SelectLodByHeight`, `LodSwitchDistance`, `LodGroupScreenHeight` and
`LodDistanceFor` are the same maths in smaller pieces.

Note on the signature: the request listed `SelectLod(group, distance,
fovHeightPx, bias)`. The third argument is a `LodView` (viewport height and
vertical fov) instead, because a screen-relative height needs both the fov and,
for `minPixelHeight`, the viewport size; `LodView::FovHeightPx()` returns the
"pixels covered by a one-unit object at distance 1" figure if that is what a
caller has.

A `LODGroup` component is not included: adding a component means registering it
with the scene serializer and the component factory, which are outside this
folder. `LodGroup` is exactly what such a component would hold.

## 27.2 Occlusion culling

### OcclusionBuffer

A CPU depth buffer (256 x 128 by default) that large occluders are rasterized
into; bounding boxes are then tested against it.

```cpp
OcclusionBuffer buf(256, 128);
buf.SetCamera(eye, target, up, 60.0f, aspect, 0.1f, 500.0f);   // or SetViewProjection(vp)
buf.RasterizeBox(wallBounds);                                   // boxes, quads, triangles, meshes
buf.RasterizeMesh(mesh, modelMatrix, /*cullBackfaces*/ true);
if (buf.IsOccluded(objectBounds)) skip();                       // or Test() for the reason
```

It is conservative so it does not hide what is visible:

- it stores `1/w` (exactly linear in screen space for a planar triangle) and,
  per pixel, the farthest depth the occluder has anywhere in that pixel;
- triangles use a watertight top-left fill rule on 1/256 pixel fixed point (two
  triangles of a quad leave no gap along the diagonal);
- a box is tested over its screen rectangle grown by one pixel, and has to be
  behind the occluder by `depthBias` (a fraction of its distance, default
  0.2 %);
- a box touching the near plane (the camera is inside it) is `NearClipped` and
  treated as visible; a box fully off screen is `OffScreen`, not "occluded"
  (frustum culling's job);
- occluders are double sided, so a room's walls hide what is outside it from a
  camera inside the room.

Limit: coverage is sampled at pixel centres, so a gap narrower than about one
pixel between two occluders is not resolved and a box visible only through it
can be culled. The test suite checks the property with 3000 random boxes
against exact ray casts (with the reference walls grown by 1.5 pixels).
`ToAscii()` prints the buffer as a small text picture for debugging.

### PortalCells

Cells (convex boxes) joined by portals (convex polygons), for indoor levels.

```cpp
PortalCells world;
int a = world.AddCell(boundsA, "Hall");
int b = world.AddCell(boundsB, "Kitchen");
int door = world.AddPortalRect(a, b, doorCentre, halfWidthVec, halfHeightVec);
world.SetPortalOpen(door, false);                               // a closed door blocks sight
PortalVisibility v = world.ComputeVisibility(eye, viewProj);
v.CellVisible(b);  v.BoundsVisible(b, objectBounds);
```

A breadth-first walk starts in the camera's cell and crosses every open portal
the view reaches, narrowing the frustum at each one (planes through the eye and
the portal's clipped outline, plus the portal plane). A cell is visible only
through a chain of portals, and `BoundsVisible` tests an object against the
narrowed frusta of its own cell, so something in the camera's frustum but
behind a wall is rejected. Loops of rooms terminate, `maxDepth` and
`maxVisits` bound the work (`truncated` says the budget ran out), standing in a
doorway does not narrow anything, and an eye outside every cell reports
`cameraOutside` (treat everything as visible).

## 27.3 Batching

```cpp
std::vector<DrawItem> items = ...;     // mesh, material, shader state key, transform, static, bounds
BatchPlan plan = PlanBatches(items);
// plan.batches (draw order), plan.stats (draw calls and state changes before/after)
std::vector<MergeResult> merged = BuildMergedMeshes(plan, items, lookupMesh);
```

Batch kinds:

- **Instanced**: identical mesh + material + state, chunked by
  `maxInstancesPerBatch`. Dynamic repeats always; static repeats once there are
  `staticInstancingThreshold` (8) of them (instancing saves memory over merging
  copies).
- **StaticMerged**: static items with the same material + state, packed in Morton
  (spatial) order under `staticVertexBudget` (65535 vertices) so each merged mesh
  stays compact and culls well. Groups smaller than `minMergeItems` stay single.
- **Single**: everything else. Skinned items are never merged or instanced.
  Transparent items stay single and are drawn last, back to front, because
  blending needs that order.

Opaque batches are sorted by shader state, then material, then mesh, then front
to back. `BatchStats` reports draw calls before and after and the state changes
(shader / material / mesh binds counted from an empty state) in input order
versus plan order, plus a weighted cost.

`MergeStaticMeshes(inputs)` combines meshes with the transforms baked in:
positions, normals through the inverse transpose (so non-uniform scale is
right), mirrored transforms get their winding flipped (volume stays positive),
UVs / normals / per-triangle colours are kept and inputs missing them get
neutral values (normals are generated for those that lack them when any other
input has them). `MergeResult` gives each input's first vertex and triangle.

## 27.4 Streaming

### StreamingGrid

Decides which cells of a large world should be in memory for a moving viewer
and asks the caller to load and unload them through hooks. It never loads
anything or blocks.

```cpp
StreamingConfig cfg;
cfg.cellSize = 64; cfg.loadRadius = 192; cfg.unloadRadius = 256;   // hysteresis
cfg.maxLoadsPerFrame = 2; cfg.maxInFlight = 4; cfg.memoryBudget = 512;
StreamingGrid grid(cfg);
grid.SetHooks({ /*requestLoad*/ ..., /*requestUnload*/ ..., /*cancelLoad*/ ... });
grid.RegisterCell({x, 0, z}, "terrain/chunk_3_7", /*cost*/ 12.0f);
// every frame:
StreamingFrame f = grid.Update(cameraPos, cameraForward);
// when the asset manager finishes (any thread you like, then call on the update thread):
grid.NotifyLoaded(cell);  // or NotifyLoadFailed(cell)
```

- **Grid**: `GridPlane::XZ` (ground, height ignored), `XY` (side view) or `XYZ`.
- **Radii**: a cell loads when the distance to its box is within `loadRadius`
  and unloads only beyond `unloadRadius` (>= load radius). The gap stops a
  viewer on a boundary from thrashing; a cell is never requested twice while it
  stays resident.
- **Priority**: nearest first, with a bonus for cells ahead
  (`viewDirectionWeight`; cells directly behind count 1 + 2w times farther). The
  cell the viewer stands in is always first. Ties break by coordinate.
- **Budgets**: new requests per frame, request cost per frame, requests in
  flight, unloads per frame, and a `memoryBudget` over loaded + loading cost.
  When the budget is full, loaded cells outside the load radius are evicted
  first (farthest first), then loaded cells that rank worse than the wanted one,
  so the nearest cells always win a tight budget.
- **Async-friendly**: `requestLoad` may return false to refuse for now (queue
  full; the cell stays unloaded and is retried). A cell that leaves the radius
  while loading gets `cancelLoad`; a late `NotifyLoaded` then returns false so the
  caller can drop what it loaded. A failed load backs off `retryDelayFrames`
  updates. `UnregisterCell` tells the world about loaded / loading cells.

A cell's `asset` string is passed to every hook, which is the hook point for
the asset manager (`Update` itself touches no files).

### MipBudget

```cpp
std::vector<MipTexture> tex = ...;   // size, bytes per pixel, on-screen pixels, importance
MipOptions o; o.budgetBytes = 256ull * 1024 * 1024;
MipPlan plan = PlanMips(tex, o);     // plan.choices[i].mip, .bytes; plan.overBudget
```

Each texture's ideal mip comes from its largest on-screen size (and `uvScale`
for tiling), with `mipBias` and hysteresis (feed the last choice back in
`previousMip`). Invisible textures keep the smallest resident mip
(`minDimension`). If the total is over budget, textures are demoted one mip at a
time, always the one with the lowest `importance * screenPixels / bytesSaved`,
until it fits. Pinned textures stay at mip 0; if nothing more can be demoted the
plan says `overBudget`. Resident bytes of a mip count the whole chain below it.

## Scene optimization report

`AnalyzeScene(OptScene, OptSettings)` takes plain structs (one `OptObject` per
renderer: triangles, material id, bounds, static / transparent / skinned / has
LOD, optional on-screen size and textures; optionally a material table) and
returns findings, worst first, each with a severity, an explanation and a fix:

| Finding | When |
|---|---|
| `TinyDrawCalls` | many non-transparent meshes under `tinyTriangleLimit` triangles |
| `HugeTextureSmallObject` | a texture far larger than the object can show (known on-screen size, else texels per world unit), with the wasted megabytes |
| `HighPolyNoLod` | meshes over `highPolyTriangles` without a LOD chain |
| `DuplicateMaterials` | materials with the same shader and signature but different ids (breaks batching) |
| `OverdrawRisk` | stacks of overlapping transparent bounds (`overdrawLayers`) |
| `TooManyDrawCalls` | more objects than `drawCallBudget` |
| `TextureMemory` | unique texture memory over `textureBudgetMB` |

`report.ToText()` is a readable summary, `report.ToJson()` is one line of valid
JSON (names escaped). Thresholds are all in `OptSettings`.

## Tests

`test_optimize_simplify`, `test_optimize_occlusion`, `test_optimize_batching`,
`test_optimize_streaming`, `test_optimize_report` (ctest names `optimize_*`).
They cover: simplification keeping manifold topology and area / distance within
error on a sphere, a subdivided cube, a plane grid, a disc, a holed grid and a
torus; boundary, seam, hard edge, colour and skin preservation; LOD chain
monotonicity; LOD hysteresis and cross-fade; a wall hiding a box, a partly
visible box, the camera inside a room, a 3000 box conservativeness sweep against
ray casts; portal visibility with narrowing; batching counts and state changes;
merge baking; streaming hysteresis, budgets, cancellation and memory pressure;
mip budgets; and every report finding plus the JSON.

## Not done yet

- No renderer uses these yet: culling and LOD selection still need a call in each
  renderer's visibility pass, and `MergeStaticMeshes` results need a draw path.
- No `LODGroup`, occluder or portal components, editor windows (LOD preview,
  batching stats, streaming debug overlay) or scene serialization for them.
- No asset manager adapter for `StreamingGrid` hooks and no real texture
  streaming I/O behind `PlanMips`.
- Occlusion uses a CPU rasterizer only (no GPU queries, no hierarchical buffer).
- The simplifier does not weigh colour or material changes in its error: per-triangle
  colours ride along on the triangles that survive. A mesh with `cornerUVs` is split
  into per-vertex UVs first.
- Timings quoted here (a 49k triangle UV sphere to 10 % in about half a second) are
  from a Release build on a development machine, not a benchmark.
