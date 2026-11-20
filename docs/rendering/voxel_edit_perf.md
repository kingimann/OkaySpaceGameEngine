# Voxel terrain: why digging dropped the frame rate, and what was done

Owner report: "when i try to dig or add my fps drops". This page says what was measured, what the cause was, what changed and what
is still unproven. Branch `claude/voxel-edit-fps`. Written from the CPU numbers of a headless run; there was **no GPU** here.

## How it was measured

`tests/bench_voxel_edit.cpp` (target `bench_voxel_edit`, never run by ctest) drives the real **Voxel Digger**: a camera 6 m above
the ground sweeps at about 2 m/s, the left mouse button is held (`Input::FeedMouse`), 60 frames a second, brush radius 2.5,
strength 8 per second, 150 frames. Per frame it records the whole `Scene::Update`, the Profiler rows `Voxel.Edit`, `Voxel.Mesh`
and `Voxel.Upload`, and a CPU **replica** of what the OpenGL renderer does with every mesh that changed
(`GLRenderer::EnsureMeshVB`: the change check, the expansion into its interleaved layout, the exact content hash). The GPU
upload and the driver are not measured. Median of the frames, three repeats, `-O2`, a shared 4-core machine at load about 3
(`scratchpad` output kept in the PR). Four terrains: the class default 64x36x64 @0.8, the owner's **Sandbox** 72x44x72 @1.5,
a large 160x64x160 and a huge 256x64x256.

## What the numbers said (before)

CPU milliseconds per edit frame, median:

| terrain | triangles | Voxel.Mesh | combine | renderer: expand + hash | bytes re-expanded for upload | total |
|---|---|---|---|---|---|---|
| default 64x36x64 | 11 k | 1.9 | 0.2 | 0.18 + 1.44 | 1.7 MB | **3.8** |
| Sandbox 72x44x72 | 13 k | 0.7 | 0.2 | 0.22 + 1.70 | 2.1 MB | **2.9** |
| large 160x64x160 | 60 k | 1.2 | 1.0 | 1.1 + 7.8 | 9.6 MB | **11.3** |
| huge 256x64x256 | 144 k | 1.3 | 3.4 | 3.5 + 19.2 | 23 MB | **27.4** |

The brush itself costs nothing (0.002 ms). The cost grows with the size of the WHOLE terrain, not of the brush: every edit frame
the terrain copied every chunk into a new combined mesh, handed the renderers a "new" mesh, and each renderer expanded all of it
again, hashed all of it byte by byte (about 1.3 ns a byte, 1.7 ms for the Sandbox's 1.5 MB) and uploaded it. Other costs found:

* the hash is also run once a second on every static mesh (1.7 ms Sandbox, 19 ms huge), and its cheap per-frame check costs about
  25 microseconds per mesh in view;
* releasing the button swept the whole field for floating crumbs: 2-3 ms (Sandbox), 20 ms (large), 86 ms (huge), and when it
  found anything it re-meshed EVERY chunk (12 ms Sandbox, 190 ms huge);
* Smooth copied the whole field on every call (0.5 ms large, 3.2 ms huge per frame);
* the dirty box ignored that a vertex normal reads two cells past its cube, so cubes next to a dig kept stale normals (4 of 60
  random edits gave a mesh different from a full rebuild; `tests/test_voxel_edit.cpp` failed on the old code);
* a brush with radius 0 or a NaN strength or position wrote NaN into the field (5 of 7 checks failed on the old code).

## What changed

* **Chunk objects** (`VoxelTerrain::renderMode`, default Auto). Scenes, tests and the editor's Edit mode keep one combined mesh,
  exactly as before. Once the RUNNING game edits the terrain it is drawn by one small transient renderer object per 16-cube chunk
  (`Voxel Chunk x,y,z`, never saved, material copied from the terrain's Mesh Renderer, which is left empty). A dig re-uploads only
  the chunks it touched (0.17-0.34 MB a frame instead of 1.3-17 MB) and renderers can cull chunks out of view. Same picture:
  `test_voxel_chunks` renders both ways with the software renderer and compares every pixel.
* **Exact dirty set**, including the neighbours a normal reads; edits near chunk borders and grid edges are tested one cell at a time.
* **Faster marching cubes, same bits**: cubes with no surface are dropped before any work, density is read without clamping when
  the sample cube is inside the grid, and each shared edge vertex gets its normal once. The original loop is kept as
  `BuildMeshReference()` and `test_voxel_chunks` demands identical bytes from both on seven grids after random edits. About 2.7x
  faster per chunk.
* **Renderer hash** (`engine/include/okay/Render/MeshHash.hpp`, used by `GLRenderer.cpp` and `D3D11Renderer.cpp`): four 8-byte lanes
  instead of one byte at a time, about 15x faster (`tests/test_mesh_hash.cpp`: every single bit of buffers of 26 sizes changes
  the hash). It only compares values made by itself, so nothing depends on the old numbers. This also speeds up every other mesh.
* **Floaters**: after one full sweep, a release only looks around the cells the stroke touched (0.004-0.02 ms) and only re-meshes
  those chunks. The first sweep (and an explicit `RemoveFloaters`) is a little slower than before (4/34/104 ms for Sandbox/large/huge).
* Smooth copies only its brush box. Bad brush numbers are ignored. One long frame digs at most a tenth of a second's worth.
  The digger aims from the scene's main camera and remembers where the terrain is instead of searching the scene every frame.
* Profiler rows `Voxel.Edit`, `Voxel.Mesh`, `Voxel.Upload`; `VoxelTerrain::lastStats`; a line on the Voxel Terrain card.
* Saved scenes are smaller: the terrain's per-face colours (0.4 MB of the Sandbox scene) were written and rebuilt on load; they are
  no longer written. The density text is unchanged, old scenes load the same.

## After

Same machine, same runs (total CPU per edit frame, ms; "chunks" = chunk objects with the OLD hash, "after" = chunk objects and the new hash):

| terrain | before | chunks only | after | meshes the renderer checks per frame |
|---|---|---|---|---|
| default 64x36x64 | 3.83 | 1.69 | **0.87** | 1 -> 26 |
| Sandbox 72x44x72 | 2.96 | 1.11 | **0.44** | 1 -> 26 |
| large 160x64x160 | 11.3 | 4.1 | **1.05** | 1 -> 147 |
| huge 256x64x256 | 27.4 | 8.8 | **1.71** | 1 -> 310 |

Chunk objects alone cost a per-frame check for every chunk in view (3 ms for 147 meshes with the old hash), which is why the faster
hash belongs with them. Idle exact hash of the whole terrain once a second: 1.7 -> 0.23 ms (Sandbox), 19 -> 3.7 ms (huge).

## What is not known

* GPU upload and driver time, real frame rates, and Windows: none of it was run. The CPU numbers are a replica of the OpenGL path;
  Direct3D 11 has the same code; Direct3D 12 expands every visible mesh every frame whether or not you dig (not changed).
* The editor click path (Inspector line, Profiler rows) was compile-checked only (`editor/main.cpp` syntax-checked).
* Software renderer: per-frame cost is the triangles drawn, so digging is not special there.

## Try it

`docs/try_it.md` section 7s. Switch back to the old behaviour with `OKAY_VOXEL_OLD_REMESH=1`.
