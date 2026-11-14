# Virtual geometry: what Nanite does and what we will really build

This page is a plan. It says what Unreal Engine's Nanite does, what part of that we can honestly build, in what order, and what needs a GPU we do not have here. Where something is only a guess it says so.

Status today: phase 1 and the start of phase 2 are written and tested on the CPU (see "What exists"). Nothing here draws differently yet. No default changed, no existing scene looks different, and no GPU code was run.

## What Nanite does

Nanite lets an artist drop in a film-quality mesh with millions of triangles and still run at frame rate. It does five things:

1. **Cuts every mesh into clusters** of about 128 triangles each.
2. **Builds a tree of clusters.** Groups of clusters are merged and simplified into coarser clusters, again and again, up to one cluster for the whole mesh. The simplification keeps the group border fixed, so neighbouring groups at different detail never leave cracks.
3. **Picks the detail per cluster, per frame, on the GPU.** For each cluster it projects the simplification error to pixels and uses the cluster when the error is under about one pixel. So detail changes smoothly across one object, not one switch per object.
4. **Culls on the GPU.** Frustum, back-face cone and occlusion (against last frame's depth pyramid) tests run in compute shaders over the whole cluster tree. The CPU never sees individual triangles.
5. **Draws in two ways.** Big triangles go through the normal hardware rasteriser. Tiny triangles (smaller than a pixel or so) go through a software rasteriser written as a compute shader that writes a visibility buffer. Materials are shaded afterwards from that buffer. It also streams cluster pages from disk so the whole mesh need not be in memory.

The result: no hand-made LODs, no pop, a cost that follows pixels on screen and not triangles in the scene.

## What we will realistically do

Our four renderers are a CPU rasteriser, OpenGL, Direct3D 11 and a Direct3D 12 preview (`docs/render_status.md`). None has compute shaders, indirect draws or a depth pyramid wired in. Steps 3 to 5 above are GPU work that depends on those. So the plan builds the data and the maths on the CPU first, where we can test it, and treats the GPU as a later, separate, unverifiable step.

### Phase 1: automatic LOD chains (done on the CPU)

Cut an object into whole-mesh levels with quadric error simplification (Garland and Heckbert), and switch between them by screen size.

- The simplifier already existed: `engine/src/Optimize/MeshSimplify.cpp`. It keeps closed meshes closed, keeps open borders, UV seams, hard normal edges, per-triangle colours and skin weights.
- New: `engine/src/Optimize/AutoLod.cpp` (`GenerateAutoLod`). It is a safe front door: it cleans hostile input, builds 100 / 50 / 25 / 12.5 percent levels, drops a level whose error is over a budget (5 percent of the object size by default), checks each level, and gives per-level screen-size thresholds in the meaning `LODGroup::Level::screenSize` already has. `FillLodGroup` is the hook the editor can call later. The editor is not wired.
- What it is not: it is not per-cluster detail. One object is one level at a time, so a huge mesh still pops between levels.
- Needs a GPU: no. Existing renderers already draw the chosen level (`Render/Visibility.cpp`).
- Effort left: S (a few days) to wire a button and a project-wide "generate LODs on import" option, and to measure error against more imported meshes.

### Phase 2: meshlets and cluster culling (started on the CPU)

Cut each mesh into clusters of at most 64 vertices and 126 triangles. Give each cluster a bounding sphere and a normal cone. Throw away whole clusters that are outside the view, face away, or are too far.

- New: `engine/src/Optimize/Meshlet.cpp` (`BuildMeshlets`, `CullMeshlets`, `BruteForceVisibleTriangles`). The data layout (a vertex index list and 8-bit local triangle indices per cluster) is the layout mesh shaders and compute culling passes read.
- The CPU culling is the reference. The test suite proves it never drops a triangle the slow per-triangle test would keep, and measures how many extra triangles survive.
- Needs a GPU: the point of clusters is GPU-driven culling. On the CPU, clusters mostly cost time. The real win needs a compute pass, indirect draws and a way to draw a list of clusters from one buffer. None of that exists in our renderers.
- Effort left: L (weeks). A compute culling pass and cluster draw path in OpenGL 4.3 first, then Direct3D 11 (compute and `DrawIndexedInstancedIndirect`). Direct3D 12 later. All of it unverifiable here; each step needs someone with a GPU.
- Occlusion culling against a depth pyramid is a further M step after that. The CPU `OcclusionBuffer` is the reference we can reuse for tests.

### Phase 3: a cluster hierarchy with screen-space error (not started)

This is the part that makes it "virtualized": build the cluster tree (merge groups of about 4 clusters, simplify the group keeping its border locked, split into new clusters, repeat), store a bounding sphere and an error value per cluster and per group, and pick a cut through the tree per frame.

- Hard parts: choosing groups so borders are shared between levels, locking borders in the simplifier (a `lockBoundary`-style option already exists on `SimplifyOptions`), making errors monotonic up the tree so the cut is consistent, and never cracking. This is the real research cost.
- CPU first: build the tree offline, select a cut on the CPU, draw that list with the existing renderers as one merged mesh per frame. This works without a GPU path, is testable, and already gives smooth per-object detail. It is also slow for large scenes.
- Effort: XL (one to two months for the builder and CPU selection, with crack-free tests). The GPU selection pass is another L after phase 2's GPU work.

### Later: streaming, software rasteriser for tiny triangles, visibility buffer

- Streaming cluster pages: needs a disk format, a page table and a request loop. L to XL. Our `StreamingGrid` is a scene-cell streamer and is not the same thing.
- Software rasteriser for sub-pixel triangles and a visibility buffer: XL. Our CPU rasteriser can show correctness on small scenes, but the speed case only exists on a GPU. We will not claim it until it runs on one.
- We do not plan to copy Nanite's material, shadow or tessellation integration. The first goal is smooth detail and a cheap triangle count, not feature parity.

## What needs a GPU, honestly

| Piece | Can test here | Needs a GPU to be real |
|---|---|---|
| Simplifier, auto LOD chain, thresholds | Yes (CPU tests, sanitizers) | No |
| Meshlet building, bounds, cones | Yes | No |
| Cluster culling reference | Yes | The speed-up needs compute passes |
| Cluster hierarchy build and cut selection | Yes (CPU) | The per-frame cut at scale |
| Compute culling, indirect draw, depth pyramid | No (compile only at best) | Yes |
| Visibility buffer, software rasteriser | CPU reference only | Yes |
| Streaming | Format and logic only | Real paging behaviour |

Direct3D code in CI is compile-checked with MinGW and never run. Treat every GPU step as UNVERIFIED until someone with a Windows machine has looked at a frame.

## Effort summary (guesses)

| Step | Size | Notes |
|---|---|---|
| Phase 1 wiring (editor button, import option) | S | Code and tests done; no UI yet |
| Phase 2 CPU builder and culling reference | Done | Tests in `test_meshlet` |
| Phase 2 GPU culling and cluster draw (GL) | L | Weeks; unverifiable here |
| Phase 2 GPU path in Direct3D 11 | L | Weeks; unverifiable here |
| Phase 3 hierarchy builder and CPU cut | XL | The crack-free guarantee is the hard bit |
| Phase 3 GPU cut selection | L | After phase 2 GPU |
| Streaming | L to XL | |
| Software rasteriser and visibility buffer | XL | GPU only |

S is days, M is one to two weeks, L is several weeks, XL is months. These are guesses by one engineer who has not done the GPU parts.

## What exists in the repo

- `engine/include/okay/Optimize/MeshSimplify.hpp`, `engine/src/Optimize/MeshSimplify.cpp`: the quadric simplifier and `GenerateLodChain` (tests `optimize_simplify`).
- `engine/include/okay/Optimize/AutoLod.hpp`, `engine/src/Optimize/AutoLod.cpp`: `GenerateAutoLod`, `MeshIsSound`, `FillLodGroup` (test `auto_lod`).
- `engine/include/okay/Optimize/Meshlet.hpp`, `engine/src/Optimize/Meshlet.cpp`: `BuildMeshlets`, `CullMeshlets`, `BruteForceVisibleTriangles`, `ValidateMeshlets` (test `meshlet`).
- `engine/include/okay/Components/LODGroup.hpp`, `engine/src/Render/Visibility.cpp`: the existing per-object LOD choice (test `visibility_lod`).

## Limits to know

- Culling by cone assumes the mesh is single-sided and the world matrix has rotation, translation and one uniform scale. Turn the cone off for double-sided meshes. A mirrored (negative scale) matrix flips winding and is not handled.
- The simplifier's error is an estimate in world units, not a measured Hausdorff distance. The tests compare it with a measured distance and require the same order of size.
- AutoLod refuses meshes whose coordinates are beyond 1e15 units and leaves them unsimplified with a note.
- Skinned meshes simplify only when the caller passes the skin weights in.
