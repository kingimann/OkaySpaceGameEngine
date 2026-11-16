# Performance log

Owner request (2026-11-15): "work on optimization for the engine, improve FPS." This page is the record: what was
measured, what was changed, and the numbers before and after.

**In short.** On the software renderer (the CPU renderer, used when no GPU renderer starts) a whole frame is now 2.3 to
6 times faster on five of the six benchmark scenes, and the one scene that is only UI did not change. Physics with many
colliders costs 10 to 20 times less. Every picture and every object position is bit for bit the same as before (the
benchmark prints a hash of both, and the hashes are equal). **Nothing was measured on Windows, on a GPU renderer or on
real hardware**: these are Linux numbers from a shared virtual machine (see "What the numbers are, and are not").

Rules this pass followed: measure first (never guess), keep behaviour identical (every change has a test or a hash that
shows the output did not change), write everything from general algorithms (CLAUDE.md rule 19a), and keep `<mutex>` and
`<thread>` out of anything the Windows programs can reach (rule 21a; the headers added here include neither).

## 1. How to measure (the benchmark)

`tests/bench_frame.cpp` builds fixed scenes and runs whole frames the way Play mode and the player do (scene update,
physics, animation, software render, UI). It is not a test: ctest only runs a tiny smoke version, `bench_frame_smoke`.

```
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=OFF \
      -DOKAY_BUILD_EDITOR=OFF -DOKAY_BUILD_PLAYER=OFF -DOKAY_BUILD_TOOLS=OFF -DOKAY_BUILD_OKAYUI_TESTS=OFF
ninja -C build bench_frame
build/tests/bench_frame --list                        # the scenes
build/tests/bench_frame                                # every scene, 100 frames each after 30 warm-up frames, 640x360
build/tests/bench_frame --scene cubes2000 --frames 300 --size 960x540
build/tests/bench_frame --scene cubes2000 --shadows 0  # without the sun's shadow maps
build/tests/bench_frame --scene-file examples/perf/many_colliders.okayscene   # any .okayscene
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
engine's own profiler splits out `Physics.3D`, `Physics.2D`, `Scripts.*`), `light gather`, `render 3d` (split into
`Render.Visibility`, `Render.ShadowMaps`, `Render.Prewarm`, `Render.Raster`, `Render.Post`, `Render.Resolve`) and `ui`.
Rows marked `(cpu)` are processor time of all threads, which is much less noisy than the wall clock on a shared machine.
After the table come `BENCH,...` lines (easy to paste into a spreadsheet) and two hashes: `frame_hash` (the last picture;
0 for the UI scene, which draws no 3D picture) and `state_hash` (where every object is at the end).
**A change that is only meant to be faster must print the same two hashes as before.**

Switches that bring the old code back one at a time (the same pictures and motion, only slower), so one program can show
what each change is worth: `--legacy-sw` (all three software renderer ones), `--no-span`, `--no-bandskip`,
`--no-lightcull`, `--no-query-cache`, `--legacy-physics`. `--perpixel 0|1` forces the lighting mode.
`--save-scenes DIR` writes the three 3D scenes as `.okayscene` files (that is how `examples/perf/` was made).
The three files are the scenes `cubes2000`, `bodies500` and `lights64` (under the names `many_colliders`, `falling_bodies`
and `many_lights`). `--scene-file` gives the same picture and state hashes as the built-in scene for the first two. Not for
`many_lights`: a scene file stores a sphere only as "Sphere", so its balls come back with the default radius 0.5 instead of
the 0.6 of the built-in scene; the lamps, the positions and the counts are the same (`state_hash` is equal), the picture is a
little different.

To compare two command lines fairly, `scripts/bench_ab.py` runs them in turns and prints the median of each phase:

```
B=build/tests/bench_frame
python3 scripts/bench_ab.py "shadow depth" 5 "$B --scene cubes2000 --frames 8 --warmup 3 --no-span" \
                                              "$B --scene cubes2000 --frames 8 --warmup 3"
```

For a function level profile: `valgrind --tool=callgrind --collect-atstart=no build/tests/bench_frame --scene cubes2000
--frames 3 --warmup 2`, then `callgrind_annotate`. Only the measured frames are counted (the bench switches counting
on and off itself). Instruction counts are exact and do not depend on how busy the machine is.

**One limit, found late in this pass:** callgrind's counting switch works per thread, so **only the main thread is
counted**. The main thread runs the physics, the scene update, the sun shadow maps and the first band of rows of every
pass that is split over threads (`ParallelRows`); the other bands run on worker threads that are not counted (checked with
`--separate-threads=yes`: thread 1 shows 93.0 million instructions on `lights64`, threads 2 to 4 show 0). So a count is
the main thread's work. That is close to the length of a frame when the bands are balanced, but it is not the total work
of all threads (the `frame (cpu, all threads)` row is), and after band skipping the bands are no longer balanced.

The engine's own profiler (`OKAY_PROFILE`, `docs/core_logging_profiling.md`) now also records the software renderer's
phases as `Render.*`, so the editor's Profiler panel shows them.

**On Windows.** `bench_frame` is a developer tool built from source; it is not in the Windows release. What the release
has: the example scenes `examples\perf\*.okayscene` and the editor's Profiler (`docs/try_it.md`, section 14).

## 2. What the numbers are, and are not

* **Machine:** a shared Linux virtual machine, 4 cores of an Intel Xeon at 2.30 GHz, 16 GB, other jobs running the whole
  time. Wall times are noisy (see 4.4). The `(cpu)` rows are the trustworthy totals; instruction counts are exact but count the main thread only (section 1).
* **Build:** CMake `Release` with GCC 13.3 (`-O3 -DNDEBUG`). **Not Debug**; no Debug numbers exist.
* **Method:** every table row is the median of at least 5 runs of the whole program; the two sides of a comparison are run
  in turns (A, B, A, B, ...) so a slow minute hits both. Each run is itself the median of 8 to 300 frames.
* **The timing runs were not taken under the machine-wide build lock** (the lock queue was a dozen jobs long). The load
  average was about 2 while they ran, with one niced compile of another agent running now and then.
* **Not measured:** any GPU path (OpenGL, Direct3D 11 and 12), Windows, real hardware, audio, networking, the editor
  window. The software renderer is what the bench draws with; the player uses it only when no GPU renderer starts, and the
  editor viewport only when its GPU renderer is switched off or fails (`editor/main.cpp`, the `RenderMeshesSS` fallback).
  The editor's asset thumbnails (`RenderFramedImage`, 128 x 128 pictures of prefabs and models) always use it, so they
  should get faster too (not measured). So changes 2 and 3 below help in those places only. Changes 1, 4 and 5 (physics and
  scene update) are shared by every renderer, but were measured only here.

## 3. Where the time went (baseline, before any change)

Shares of the main thread's instructions per frame from callgrind (Release, 640x360, shadows off unless noted):

| Scene | Biggest costs |
|---|---|
| `cubes2000` | `Physics3D::Step` pair loop 60 percent (every collider tested against every other: 2 million pairs per step); `dynamic_cast` in component lookups 14 percent |
| `cubes2000` with shadows | software sun shadow maps (`RenderCascadeDepth`) 86 percent |
| `bodies500` | `Physics3D::Raycast` called once per falling body per step (`CatchTunneledFloor`), each scanning all colliders: 38 percent |
| `lights64` | `SceneLights::ShadeNormalized` (every light for every vertex): 83 percent |
| `skinned100`, `mixed` | the renderer's per-vertex shading, FXAA, `ShadeNormalized`; physics under 5 percent |
| `ui500` | `strcmp` and `dynamic_cast` from component lookups, 15 percent; UI batching otherwise |

## 4. Results

All tables: Release, 640x360, software renderer, the machine of section 2. "Before" is the original code (the commit this
branch started from, `24efceb0`, with only the benchmark and its profiler scopes added so it can be measured); "after" is
the last commit of this branch.

### 4.1 The whole package, original code against this branch

Default settings (sun shadows on), median of 5 alternating runs, ms per frame. **Both hashes were equal on every scene.**

| Scene | Frame, wall: before to after | Frame, CPU of all threads | Scene update (what every renderer pays) | Biggest parts, before to after |
|---|---|---|---|---|
| `cubes2000` | 151.2 to 55.4 (2.7x) | 163.4 to 66.0 (2.5x) | 10.49 to 1.08 (9.7x) | shadow maps 114.6 to 35.5; physics 9.3 to 0.46; raster 18.6 to 12.9 |
| `bodies500` | 216.0 to 93.1 (2.3x) | 212.0 to 93.4 (2.3x) | 5.40 to 0.73 (7.4x) | shadow maps 160.6 to 59.0; physics 4.9 to 0.45; raster 38.0 to 23.5 |
| `skinned100` | 175.5 to 73.0 (2.4x) | 182.0 to 78.2 (2.3x) | 1.99 to 1.69 (1.2x) | shadow maps 138.8 to 46.9; raster 24.2 to 16.2 |
| `lights64` (sun, no shadows) | 98.1 to 16.4 (6.0x) | 145.2 to 17.7 (8.2x) | 0.26 to 0.09 | raster 91.1 to 9.1 |
| `mixed` | 196.5 to 70.2 (2.8x) | 219.1 to 89.5 (2.4x) | 2.00 to 0.73 (2.7x) | shadow maps 156.5 to 47.0; physics 1.40 to 0.30; raster 30.9 to 18.4 |
| `ui500` | 1.96 to 1.95 (1.0x) | same | 1.00 to 0.97 | no change (see below) |

* Same picture, same positions: besides the six runs above, the original program and this branch were run on the five 3D scenes
  with the sun on and off at five picture sizes (96x54, 321x177, 640x360, 1000x563, 1280x720; the odd and tiny sizes move the
  band boundaries): 50 of 50 pairs printed the same `frame_hash` and `state_hash`.
  Long runs too (160x90, sun off): `bodies500` and `mixed` after 610 frames, `cubes2000` after 210, `skinned100` after 310:
  the same hashes.
* `Render.Prewarm` got more expensive (`cubes2000` 0.15 to 0.42 ms): the per-object work that every thread used to repeat
  inside `Render.Raster` is now done once, there. The totals above include it.
* The four scenes whose sun casts shadows are still dominated by the shadow maps (63 to 67 percent of their frame); see section 6.
* `ui500`: nothing in the UI path was changed, and nothing changed. The first table (5 runs of 80 frames) said 1.75 to
  1.97 ms (0.89x); that was noise. Repeated with 9 runs of 300 frames it said 1.956 to 1.953 ms. The exact count says the
  same: 59.39 million instructions before, 60.69 million after (3 frames), and the whole difference (+1.3 million) is
  inside libc's `strcmp`, called the same number of times (291,427 and 240,090 calls in both) but costing more
  instructions per call because the type-name strings sit at other addresses in the new program.

### 4.2 One change at a time

Same program, one change switched off (A, with the matching `--no-...` switch) and on (B). Sun shadows off unless the row
says otherwise. Median of 5 alternating runs.

| # | Change | Scene | Measure | A (off) ms | B (on) ms | A / B |
|---|---|---|---|---|---|---|
| 1 | Physics broad phase: sort and sweep | `cubes2000` | `Physics.3D`, wall | 2.317 | 0.455 | 5.1x |
| 1 | same | `cubes2000` | scene update, CPU | 2.906 | 1.061 | 2.7x |
| 1 | same | `cubes2000` | whole frame, wall | 11.506 | 9.600 | 1.20x |
| 1+4 | broad phase and vertical ray grid (one switch) | `bodies500` | `Physics.3D`, wall | 4.339 | 0.447 | 9.7x |
| 1+4 | same | `bodies500` | scene update, CPU | 4.575 | 0.694 | 6.6x |
| 1+4 | same | `bodies500` | whole frame, wall | 15.379 | 11.080 | 1.39x |
| 2 | Shadow depth: row spans, 4 texels at a time | `cubes2000`, shadows on | `Render.ShadowMaps`, wall | 125.099 | 38.283 | 3.3x |
| 2 | same | `cubes2000`, shadows on | whole frame, CPU | 159.701 | 71.549 | 2.2x |
| 2 | same | `mixed`, shadows on | `Render.ShadowMaps`, wall | 164.505 | 47.113 | 3.5x |
| 2 | same | `mixed`, shadows on | whole frame, CPU | 207.346 | 90.270 | 2.3x |
| 3a | Light culling: only the lights that reach a mesh | `lights64` | `Render.Raster`, wall | 15.614 | 4.885 | 3.2x |
| 3a | same | `lights64` | whole frame, CPU | 38.308 | 18.185 | 2.1x |
| 3a | same | `mixed` | `Render.Raster`, wall | 5.596 | 4.548 | 1.23x |
| 3a | same | `mixed` | whole frame, CPU | 18.312 | 16.679 | 1.10x |
| 3b | Band skipping: a thread skips what its rows cannot show | `cubes2000` | `Render.Raster`, wall | 6.636 | 3.128 | 2.1x |
| 3b | same | `cubes2000` | whole frame, CPU | 19.287 | 15.046 | 1.28x |
| 3b | same (light culling off in both) | `lights64` | `Render.Raster`, wall | 60.313 | 15.855 | 3.8x |
| 3b | same | `lights64` | whole frame, CPU | 114.547 | 37.367 | 3.1x |
| 5 | Component lists kept between steps | `cubes2000` | scene update, CPU | 2.063 | 1.003 | 2.1x |
| 5 | same | `cubes2000` | `Physics.2D` + `Physics.3D`, wall | 1.201 | 0.441 | 2.7x |
| 5 | same | `mixed` | scene update, CPU | 0.875 | 0.579 | 1.5x |

Reading the table:

* The same hashes were printed by A and B in every row.
* The one-change ratios are smaller than the whole-change numbers of 4.1, because the old path behind a switch is already
  better than the original code. Example: the original pair loop ran its object checks (active in the hierarchy, layers,
  removed) for all 2 million pairs *before* it compared boxes. The all-pairs path behind `--legacy-physics` compares boxes
  first, so it is already 4x cheaper than the original: `Physics.3D` on `cubes2000` is 9.3 ms in the original program (the
  same with the sun on or off), 2.3 ms with the switch, 0.45 ms with the sweep.
* There is no switch for the dead shading computations and the values worked out once per frame instead of once per
  thread (the rest of change 3). Measured in one batch on `lights64` with light culling and band skipping both off: the
  original program 151.7 ms of CPU time per frame (`Render.Raster` 62.4 ms wall), the new code 116.6 ms (48.9 ms), 1.30x.
  On `cubes2000` without shadows and with band skipping off the new code's `Render.Raster` was 6.6 ms against 5.8 ms in
  the original (0.88x, about the size of the noise): the per-object bookkeeping costs a little where nothing is skipped.
* Wall times of the renderer rows depend on how many cores were free at that moment (the renderer runs one band of rows
  per core). The CPU rows do not. Compare the CPU rows when they disagree with the wall rows.

### 4.3 Instructions the main thread ran (exact, but not the total work)

callgrind, only the 3 measured frames after 2 warm-up frames, instructions per frame, millions, **main thread only** (see
the limit in section 1). These do not depend on the machine or on its load.

| Scene | Settings | Original | This branch | Original / this branch |
|---|---|---|---|---|
| `cubes2000` | shadows on | 1,791.5 | 418.2 | 4.28x |
| `cubes2000` | shadows off | 250.4 | 57.3 | 4.37x |
| `bodies500` | shadows on | 2,200.0 | 508.5 | 4.33x |
| `bodies500` | shadows off | 192.8 | 37.8 | 5.10x |
| `skinned100` | shadows on | 1,892.1 | 391.6 | 4.83x |
| `lights64` | sun without shadows | 487.4 | 31.0 | 15.71x |
| `mixed` | shadows on | 2,200.9 | 465.6 | 4.73x |
| `mixed` | shadows off | 142.4 | 42.6 | 3.34x |
| `ui500` | | 19.8 | 20.2 | 0.98x (see 4.1) |

Read these as "how much shorter the main thread's work got", not as "how much less work the machine does": they are
larger than the time ratios of 4.1 (2.3x to 8.2x) because the sun shadow maps, the physics and the scene update run
entirely on the main thread and shrank a lot, while the banded passes are only counted for their first band. For the
total work use the `frame (cpu, all threads)` column of 4.1. One example of the difference: on `lights64`, with light
culling on, the main thread runs 93.0 million instructions and with it off 86.6 million (more with it on, because the
main thread pays for working out which lights reach which mesh), while the CPU time of all threads is 17.3 ms with it
on and 36.8 ms with it off: the saving is on the worker threads, which callgrind did not count.

### 4.4 How noisy this is

* Running the same command twice in turns gives ratios of 0.99 to 1.01 on every row: the alternation itself adds no bias
  (checked on `ui500` with 9 runs, in both orders).
* But the machine's speed drifts. The identical command `bench_frame --scene ui500 --frames 300 --warmup 20` printed a
  frame time of 1.5 ms in one batch and 2.1 ms in another a few minutes later; the identical `lights64` command printed
  16.4 ms of wall time in one batch and 9.6 ms in another, while its CPU time was 17.7 and 18.2 ms. So an absolute wall time
  here is good to about 30 percent, and a ratio from one batch to about 10 percent. The wins in this log are 1.2x to 20x:
  the small ones (1.1x to 1.3x) are at the edge of what wall time can show, which is why the CPU rows are there.
* Medians of 5 runs (9 for `ui500`). The p95 and max columns are in the bench's own output; they are not used here.

## 5. The changes, one by one

The commits on `claude/perf-pass-1`, in order: the benchmark and the profiler scopes, then one commit for each change
below (1, 2, 3, 5, 4), then the benchmark's switches, then this log with the docs and the example scenes.
The commit messages quote shares "of the instructions" (60, 86, 83, 38 percent, a fifth): those are shares of the main
thread's instructions (the limit in section 1; a commit message cannot be changed without rewriting the branch). The commit
message of change 1 also quotes a first `Physics.3D` measurement (21.7 to 1.06 ms) taken while the machine ran at load
10 to 15; tables 4.1 and 4.2 replace it.

### 0. The benchmark (`tests/bench_frame.cpp`) and the `Render.*` profiler scopes

Not a speed-up; it is what everything else was measured with. The software renderer gets `OKAY_PROFILE` scopes
(`Render.Visibility`, `.ShadowMaps`, `.Prewarm`, `.Raster`, `.Post`, `.Resolve`). They cost a few nanoseconds when the
profiler is off (it is off unless the Profiler panel or `OKAY_PERF_DUMP` is on). Written from scratch.

### 1. Physics broad phase: sort and sweep (`engine/include/okay/Physics/Broadphase.hpp`)

*What was wrong.* Each physics step compared every collider with every other one: 2,000 cubes meant about 2 million pair
visits per step, each behind several checks, even when only a handful of boxes touched. 60 percent of the main thread's
instructions of `cubes2000` (the physics step runs on the main thread, so that is also close to its share of the frame).

*What changed.* The pairs whose boxes overlap are found by sorting the boxes along their widest axis and sweeping (the
general sort-and-sweep idea, written from scratch). The list is the same as the old double loop produced, in the same
order, because the solver applies its impulses in pair order. `Physics3D::Step` and `Physics2D::Step` use it. The box
comparison now comes first, so the object checks (active, layers, removed) run only for the few pairs whose boxes overlap,
not for all of them; they are filters without side effects, so which pairs get through is unchanged.

*How it was checked.* `tests/test_broadphase.cpp` (ctest `broadphase`, 293 checks): the sweep against the old all-pairs loop
on 280 random box sets (identical boxes, touching, zero size, inverted, a huge box, boxes to ignore), pair for pair and in
order; a 3D scene with 260 bodies, pillars, a trigger and a floor, and a 2D scene, each run with the old loop (switch
`broadphase::ForceBruteForce()`) and the sweep: every body ends at the same bits and the collision callbacks fire the same
number of times. The whole bench: the original program and the new one print the same `state_hash` and `frame_hash` on
all six scenes.

*Back to the old code:* `broadphase::ForceBruteForce() = true` (bench `--legacy-physics`).


### 2. Software shadow depth: only the texels a triangle can cover, four at a time (`engine/include/okay/Render/TriangleSpan.hpp`)

*What was wrong.* The cascaded sun shadow pass visited every texel of every triangle's bounding box and ran three edge
tests on it. A big ground quad cut by a diagonal has a box twice the size of the triangle, so half of the visits were
wasted. 86 percent of the main thread's instructions of `cubes2000` with shadows on (the shadow maps are drawn by the
main thread alone; in time they were 76 percent of the frame: 114.6 of 151.2 ms).

*What changed.* `TriangleRows` works out, for every row, the columns outside which no texel can pass the edge tests (the
bound is widened by an error bound on the float rounding of those tests, and by one column, so it can only be too wide,
never too narrow). `RasterDepthTriangle` then runs the same tests with the same arithmetic on the columns that remain,
four at a time with SSE2 (every lane does the scalar operations in the same order; no fused multiply-add, because the
build is plain C++17 for baseline x86-64), and a scalar loop handles the row ends and CPUs without SSE2. Written from the
edge-function rasterization idea and a rounding error bound worked out and tested here.

*How it was checked.* `tests/test_triangle_span.cpp` (ctest `triangle_span`, 19,088,086 checks, 0 failures): the span
against a copy of the old loop on 6,000 random, huge, thin, sliver and grid-aligned triangles (19 million passing texels,
none outside its span) and the depth buffers of 4,000 triangles, compared bit for bit (also built at -O0, -O2, -O3 and with
`OKAY_NO_SIMD`). The bench: same `frame_hash` as the original program with shadows on and off.

*Back to the old code:* `detail::TriangleSpanDisabled() = true` (bench `--no-span`).

### 3. Software renderer: per-object work once, band skipping, light culling, no dead shading (`SoftwareRenderer.hpp`, `LightSubset.hpp`)

*What was wrong.* `RenderMeshes` ran every object through every thread's band of rows (so each object's setup was repeated
once per thread, and objects outside the band were rasterized into nothing), ran every vertex through every light, and worked
out colours that no code path read. `lights64`: 83 percent of the main thread's instructions were the light loop.

*What changed.*
* **3b, band skipping.** Each object's drawn vertices give a world box once per frame (from the vertices actually drawn, so
  skinned and deformed meshes are as safe as still ones); its eight corners say which screen rows it can reach, and a thread
  whose rows miss them skips the object. A triangle wholly in front of the near plane whose rows miss the band is skipped
  before it is shaded, using the rasterizers' own row arithmetic. Visibility and culled lookups are one vector index
  instead of two hash lookups per object per thread.
* **3a, light culling.** A point, spot or area light whose range does not reach a mesh's box adds exactly zero, so the mesh
  is shaded with only the lights that reach it (`Render/LightSubset.hpp`: the loops of `SceneLights::ShadeNormalized` and
  `SpecularN` over a list of indices, operation for operation). Lights with non-finite values are never culled. It lives
  in its own header so that the change does not rebuild everything that includes `Lighting.hpp`.
* **3c, dead shading removed.** The per-vertex colour and specular are never read on the per-pixel path, and the
  per-triangle colour, lighting, specular and fog are read only by the flat (unlit, untextured) path; they are no longer
  computed for the others. Values that do not change inside a frame are worked out once.

*How it was checked.* `tests/test_software_perf_paths.cpp` (ctest `software_perf_paths`): 360 renders (three lamp counts,
six cameras including one inside the level and one against a tilted plane that the near plane cuts, five shadow, per-pixel
and fog settings; textured, unlit, double-sided, outlined and shiny meshes; point, spot and area lights of both falloffs)
with every shortcut off, all on and each alone, comparing colour and depth bit for bit: 360 of 360 identical. Plus a mesh
edited between frames. Plus the same `frame_hash` as the original program on every scene, with shadows on and off.

*Risk to know about.* Light culling relies on a light's contribution being exactly 0 beyond its range. If someone changes
the falloff or adds a light type that reaches farther than its range, or changes `ShadeNormalized`/`SpecularN` without the
copies in `LightSubset.hpp`, `software_perf_paths` fails (it compares against the legacy path).

*Back to the old code:* `BandSkipEnabled() = false`, `LightCullingEnabled() = false` (bench `--no-bandskip`,
`--no-lightcull`).

### 4. Vertical rays of the tunnelled floor catch from a grid (`engine/src/Physics/Physics3D.cpp`, `DownRays`)

*What was wrong.* `Physics3D::Step` casts one downward ray per falling body (`CatchTunneledFloor`), and `Raycast` measured
every collider's world box for every ray: 500 falling bodies meant 250,000 box measurements in one step. 38 percent of the
main thread's instructions of `bodies500`.

*What changed.* The boxes do not move while that loop runs (a lift of a body drops the index), so they are measured once
and put in a grid over the ground plane; a vertical ray looks only at the colliders whose box covers its column.
`DownRays::Cast` uses the same filters, the same box arithmetic, the same shape tests and the same collider order as
`Raycast` (a tie goes to the later collider), then the terrains. Boxes the grid cannot place (not finite, far out, covering
many cells) are tried for every ray, and the lists are fetched at the first ray, so a step with no falling body pays nothing.

*How it was checked.* `tests/test_broadphase.cpp`: 150 bodies thrown down at 30 to 120 m/s onto a thin floor in 30 Hz steps,
among clutter, a huge trigger slab and many static boxes, run once with the grid and once with the plain `Raycast`: same bits
for every object at the end, and the same number of bodies near the floor and under it (134 and 16). The bench prints the
same `state_hash` as the original program on `bodies500`.

*Back to the old code:* `broadphase::ForceBruteForce() = true` (the same switch as change 1; bench `--legacy-physics`).

### 5. Component lists kept between physics steps (`engine/include/okay/Scene/ComponentCache.hpp`)

*What was wrong.* Each physics step (and the scene's own step around it) asked for the rigidbodies, colliders, terrains and
joints about fifteen times, and every ask visited every object and ran a `dynamic_cast` on every component. About a fifth of
the main thread's instructions of a 2,000 object frame once the broad phase was fixed.

*What changed.* `CachedComponents<T>(scene)` returns a copy of the same list `FindObjectsOfType<T>()` gives, kept until
the scene's component count, structure count or identity changes (those change exactly when a component is added or removed
or an object is created, destroyed or moved). Reordering the components of one object (`MoveComponent`, `ReorderComponents`)
now also bumps the structure count, so a kept list is never in a stale order. It returns a copy because a step holds its list
while contact callbacks add and remove components.

*How it was checked.* `tests/test_component_cache.cpp` (ctest `component_cache`): 1,500 random edits (objects and components
made, removed and destroyed, components reordered, scenes cleared, two scenes asked in turn), each followed by a comparison
of seven lists with a fresh walk; the copy stays as it was when the scene changes. Same hashes in the bench.

*Risk to know about.* Code that changes a scene's components without going through the normal add and remove calls would
leave a kept list stale. None was found in the engine. `ComponentCacheDisabled() = true` (bench `--no-query-cache`) brings the
plain walk back, and the test fails if the two ever disagree.

### Checks that cover every change

* Original program against this branch: the same `frame_hash` and `state_hash` on all six scenes (4.1), on 50 pairs of scene,
  size and sun setting, and after long runs (610 frames of `bodies500` and `mixed`).
* The four new tests (`broadphase` 293 checks, `triangle_span` 19,088,086, `component_cache` 6 with 1,500 random edits,
  `software_perf_paths` 360 renders) and the benchmark's tiny smoke run: all pass. They were also run under valgrind
  memcheck (all four, plus all six scenes of the benchmark at a small scale): no memory errors. CI's own sanitizer job (address, undefined, float-cast-overflow, no recovery) has not been run on this
  branch; every float-to-int conversion this branch added is guarded (one that was not, in the vertical ray grid, was fixed
  in the last code commit).
* No `<mutex>`, `<thread>`, `condition_variable`, `call_once` or `thread_local` was added anywhere under `engine/`.

### Not kept

Nothing was reverted for lack of a win: every change above shows its win in the phase it targets (tables 4.1 and 4.2).
One idea was rejected before it was written: a type-id memo for `GameObject::GetComponent<T>()` (see section 6, item 2),
because it changes `GameObject.hpp`, which rebuilds about 130 compile units, on a machine that was out of memory.

## 6. Found but not fixed (ranked, for the next pass)

Ranked by how much of a frame each one is now. Shares of time come from the tables above (wall time of a frame). Shares of
instructions are of the main thread only (the limit in section 1), from the same callgrind runs as 4.3; they are good for
the single threaded parts (physics, scene update, visibility, shadow maps) and only rough for the banded passes.

1. **Software sun shadow maps are still the largest cost whenever shadows are on: 63 to 67 percent of the wall time of a
   frame** (35 to 59 ms of 55 to 93 ms; `cubes2000` 64, `bodies500` 63, `skinned100` 64, `mixed` 67 percent), and 76 to 84
   percent of the main thread's instructions (`RasterDepthTriangle`, plus clearing the maps). The default is 3 cascades of
   2048 x 2048 floats, 48 MB written per frame. Ideas, from safest: skip a cascade (clear and draw) when the light, the
   camera and every caster in it are unchanged; clear only what was drawn; draw the shadow maps on the worker threads too
   (one cascade each: they are independent); draw only front faces of closed meshes (almost always the same map, but not
   bit for bit at silhouettes, so it needs the owner's tolerance); a smaller default map or fewer cascades (a visible
   change: the owner's decision).
2. **FXAA and the scene post pass: `Render.Post` is 3.4 to 5.1 ms, 31 to 46 percent of the wall time of a shadows-off
   frame** (9.6 to 11.2 ms). In the callgrind profile of a shadows-off `skinned100` frame `ApplyFXAA` is 39 and
   `ApplyScenePost` 18 percent of the main thread's instructions (23 and 11 on `cubes2000`, 32 and 15 on `mixed`). About 9
   to 13 percent of those instructions are calls to libm's `fmaxf` and `fminf`, which the compiler cannot inline (they
   promise exact NaN handling); SSE min and max, or a branch-free version, would remove most of that. The picture must
   stay bit for bit: prove it with `frame_hash` as in changes 2 and 3.
3. **RTTI in component lookups: 20 percent of the main thread's instructions of a shadows-off `cubes2000` frame**
   (`dynamic_cast`, `__do_dyncast` and `strcmp` by themselves; 24.6 percent with their callers, mostly `visibility::Build`
   and `Scene::Update`) **and 21 percent of the `ui500` frame** (`GetComponent<SpriteRenderer>` and friends). Every
   `GetComponent<T>()` is a `dynamic_cast` on each component of the object. A type id stored in each component would make
   it a compare. This changes `GameObject.hpp` and `Component.hpp` (about 130 compile units rebuild): do it when the machine
   is quiet. This is the best next step for the editor and for scenes with many objects on any renderer.
4. **The pointer ray: `Scene::DispatchPointer` calls `Physics3D::Raycast` every frame, through every collider** (5.7 percent
   of the main thread's instructions of a shadows-off `cubes2000` frame). The grid of change 4 only answers vertical rays; a
   general bounding volume tree would answer this one too. `FitColliders` is another 5.7 percent of the same frame.
5. **Skinning: `SkinnedMesh::Skin` and `Transform::WorldCache`/`RefreshWorld` are 16 and 10 percent of the main thread's
   instructions of a shadows-off `skinned100` frame** (100 characters). Not looked at in detail.
6. **Thread start-up (unmeasured guess).** `ParallelRows` creates and joins threads for each pass of a frame. Instruction
   counts cannot show that cost (it is waiting, not instructions); wall time on Windows may. A standing worker pool needs
   waiting that obeys rule 21a (no mutex or condition variable in code the Windows programs reach).
7. Nothing was found in the narrow phase or the solver: on `bodies500` physics is now about 0.45 ms of a 94 ms frame.

## 7. What could not be measured or checked

* **Windows, every GPU renderer, real hardware, a Debug build:** not run.
* **`bench_frame` on Windows:** it uses `clock_gettime` for the CPU rows (POSIX), and the Windows CI job builds only six
  named tests, so it is not built there. Shipping a `bench_frame.exe` in the release (so the exact benchmark runs on the
  owner's PC) needs a `GetProcessTimes` branch there and a line in `scripts/build-windows-dist.sh`; not done.
* **The MinGW (Windows) cross compile of the new headers:** see the status line at the end of this file.
* **The editor's Profiler panel and the example scenes opened in the editor:** not run. The menu path in
  `docs/try_it.md` was read from `editor/main.cpp`; the three example scenes were written by `bench_frame --save-scenes`
  and loaded and run headless by `bench_frame --scene-file`, not clicked open in the editor.
* **Numbers on the owner's PC:** none. On a normal Windows PC the Direct3D renderer draws the viewport and the game, so
  changes 2 and 3 do not apply there (except to the thumbnails above); changes 1, 4 and 5 do, and the way to see them is
  `docs/try_it.md` section 14.
* **Noise:** about 10 percent between batches (section 4.4); the timing batch was not run under the build lock.
