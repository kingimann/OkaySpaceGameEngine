# Performance log

Owner request (2026-11-15): "work on optimization for the engine, improve FPS." This page is the record: what was
measured, what was changed, and the numbers before and after. A change with no real win was reverted, not kept.

Rules this pass followed: measure first (never guess), keep behaviour identical (every change below has a test or a
hash showing the output did not change), write everything from general algorithms (CLAUDE.md rule 19a), and keep
`<mutex>` / `<thread>` out of anything the Windows programs can reach (rule 21a).

## How to measure (the benchmark)

`tests/bench_frame.cpp` builds fixed scenes and runs whole frames the way Play mode and the player do. It is not a
test (ctest only runs a tiny smoke version, `bench_frame_smoke`).

```
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=OFF -DOKAY_BUILD_OKAYUI_TESTS=OFF
ninja -C build bench_frame
build/tests/bench_frame --list                       # the scenes
build/tests/bench_frame                               # every scene, 100 frames each after 30 warm-up frames, 640x360
build/tests/bench_frame --scene cubes2000 --frames 300 --size 960x540
build/tests/bench_frame --scene cubes2000 --shadows 0 # without the sun's shadow maps
```

| Scene | What it is |
|---|---|
| `cubes2000` | 2,000 static cubes with box colliders under a sun with cascaded shadows |
| `bodies500` | 500 dynamic boxes and spheres falling and piling up on a floor |
| `skinned100` | 100 imported skinned characters (`tests/assets/rigs/mixamo_t_walk_only.glb`) playing a clip |
| `lights64` | 64 point lights over 400 props |
| `ui500` | 500 UI widgets in a 10,000 object scene (the UI pass only) |
| `mixed` | 300 props, 100 bodies, 20 characters, 8 lamps and a sun: a small game level |

Each run prints a table (median, 95th percentile and max, ms per frame) split by phase: `update` (scene update; the
engine's own profiler splits out Physics.3D, Physics.2D, Scripts.*), `light gather`, `render 3d` (split into
Render.Visibility, ShadowMaps, Prewarm, Raster, Post, Resolve), and `ui`. There is a `(cpu)` row for update, render and
ui: processor time of all threads, which is much less noisy than the wall clock on a shared machine. After the table come
`BENCH,...` lines (easy to paste into a spreadsheet) and two hashes: `frame_hash` (the last picture) and `state_hash`
(where every object is at the end). **A change that is only meant to be faster must print the same two hashes as before.**

For a function level profile: `valgrind --tool=callgrind --collect-atstart=no ./bench_frame --scene cubes2000 --frames 3 --warmup 2`
then `callgrind_annotate`. Only the measured frames are counted (the bench switches collection on and off itself).
Instruction counts are exact and do not depend on how busy the machine is.

### What the numbers are, and are not

* Machine: a shared 4 core Linux VM, other jobs running (load average 10 to 15 while measuring). Wall times are noisy
  (30 percent or more); the `(cpu)` rows and the instruction counts are the trustworthy ones. Each number below is the
  median of at least 5 runs (A and B runs alternated so drift hits both).
* Build: CMake `Release` (GCC 13, `-O3 -DNDEBUG`). Not Debug.
* **Not measured:** any GPU path (OpenGL, Direct3D 11 and 12), Windows, real hardware, audio, the editor window. The
  software renderer is what the bench draws with; the player only uses it when no GPU renderer starts. The physics,
  scene update, animation and visibility numbers are shared by every renderer.

## Where the time went (baseline, before any change)

Instruction counts per frame from callgrind (Release, 640x360, shadows off unless noted), share of the whole frame:

| Scene | Biggest costs |
|---|---|
| `cubes2000` | `Physics3D::Step` pair loop 60 percent (every collider tested against every other: 2 million pairs per step); `dynamic_cast` in component lookups 14 percent |
| `cubes2000` with shadows | software sun shadow maps (`RenderCascadeDepth`) 86 percent |
| `bodies500` | `Physics3D::Raycast` called once per falling body per step (`CatchTunneledFloor`), each scanning all colliders: 38 percent |
| `lights64` | `SceneLights::ShadeNormalized` (every light for every vertex): 83 percent |
| `skinned100`, `mixed` | the renderer's per-vertex shading, FXAA, `ShadeNormalized`; physics under 5 percent |
| `ui500` | `strcmp` and `dynamic_cast` from component lookups, 15 percent; UI batching otherwise |

## Changes

| # | Change | Scene | Before ms | After ms | What it measures | Output identical? |
|---|---|---|---|---|---|---|
| 1 | Physics broad phase: sort and sweep instead of all pairs (`Physics/Broadphase.hpp`) | `cubes2000` | 21.7 | 1.06 | `Physics.3D`, wall, median of 5 runs | yes, same `state_hash` and `frame_hash` as the old code |
| 1 | same | `cubes2000` | 10.8 | 2.2 | scene `update`, CPU time | yes |
| 1 | same | `bodies500` | 13.0 | 8.6 | `Physics.3D`, wall (the rest of it is the item 2 below) | yes |
| 1 | same | `mixed` | 1.41 | 1.09 | `Physics.3D`, wall | yes |

Machine and build for every row: shared 4 core Linux VM, Release `-O3`, software renderer, 640x360, `--shadows 0`
(unless the row says otherwise). Wall numbers carry the noise of a busy machine; the CPU rows carry less.

### 1. Physics broad phase (`engine/include/okay/Physics/Broadphase.hpp`)

*What was wrong.* Each physics step compared every collider with every other one (n times n minus 1, over 2): 2,000
cubes meant about 2 million pair visits per step, each behind several checks, even when only a handful of boxes touched.
It was the biggest single cost in a scene with many colliders (60 percent of the instructions of `cubes2000`).

*What changed.* The pairs whose boxes overlap are found by sorting the boxes along their widest axis and sweeping
(sort and sweep, written from the general idea). The list is the same as the old double loop produced, in the same order,
because the solver applies its impulses in pair order. `Physics3D::Step` and `Physics2D::Step` use it.

*How it was checked.* `tests/test_broadphase.cpp` (ctest `broadphase`): the sweep against the old all-pairs loop on 280
random box sets (identical boxes, touching, zero size, inverted, a huge box, boxes to ignore), pair for pair and in order;
a 3D scene with 260 bodies, pillars, a trigger and a floor, and a 2D scene, each run with the old loop (switch
`broadphase::ForceBruteForce`) and the sweep: every body ends at the same bits and the collision callbacks fire the same
number of times (3,402 enters, 27,458 stays, 3,217 exits, 66 trigger enters). Also the whole bench: the old binary and the
new one print the same `state_hash` and `frame_hash` on `cubes2000`, `bodies500` and `mixed`. The existing physics
tests (physics3d, physics, physics_nan, physics_callback_removal, physics_dispatch_removal, physics_queries_ext,
physics_explode, physics_joints, ragdoll, rigidbody_step_parent, script_physics, physics2d_casts, vehicle, vehicle_seat,
meshcollider3d) pass.

## Found but not fixed (ranked, for the next pass)

(filled in at the end of this pass)
