# Particle system upgrade

Owner request: "improve particle system too". This page lists what existed, what was weak, what changed, how it was
tested, the numbers, and what is not done. Everything here is code written for this engine from general techniques
(trapezoid integration, exponential drag, lattice value noise, Rodrigues rotation, cone / ball / triangle sampling by area,
painter's-algorithm sorting, projective geometry). Nothing was copied from another engine.

## Existing / Gap

| Area | Existing (before) | Gap found (from reading the code and the test renders) | Now |
|---|---|---|---|
| Storage | `std::vector<Particle>` of structs; each new particle scanned the whole array for a free slot | Emission cost grew with the square of the pool (50 000 particles: 928 per ms); no cap policy | Pooled structure of arrays, live particles packed at the front, O(1) spawn, hard cap with Skip-new or Replace-old |
| Emission | rate, one burst, duration/loop | Spawns clumped at the start of each frame (visible at 30 fps); one burst only; no prewarm; no per-distance | Sub-frame spawning on the emitter's path, five bursts, prewarm, per-distance emission |
| Shapes | Point, Circle, Sphere, Box, Cone, Edge | No hemisphere, no mesh skin, no skin-only | Hemisphere, Mesh surface (area-weighted), skin-only for Sphere / Hemisphere / Box |
| Start values | Size, life, spin, speed with +/- ranges, one colour | One colour only | Two colours or any hue per particle |
| Over life | Colour gradient, size and speed curves, noise, orbit, vortex | No spin / force / gravity / drift curves, drag and wind were per-frame (changed with frame rate) | Spin, force, gravity, drift curves; drift, push, away-from-centre; drag and wind exact at any frame rate; trapezoid motion; 3 noise layers |
| Drawing | Two copies (editor, player), billboard or stretch, global sort | No flat / upright mode, no sheets, no ribbons, no soft edges, no lighting, no blend mode, size ignored lens | One shared draw list (`Render/ParticleDraw`) used by both; five modes, picture sheets with blending, ribbons, soft edges, lit, blend, three sort modes |
| Collision | Flat ground plane | Nothing against scene colliders | Scene colliders through the physics raycast under a per-frame ray budget; end-on-hit |
| Sub-emitters | None | None | On birth, end and hit (child objects) |
| Speed | Updated everything every frame | No LOD, no cull, no jobs | Distance LOD, off-screen cull (optional pause), job system for big pools |
| Editor | Inspector rows, 8 presets, gradient and curve editors | No preview, presets only in a combo | Live preview with Play / Pause / Restart / Step / Speed, 17 presets in GameObject > Effects, Add Component and the right-click menu, new rows with plain tooltips |
| Script | `emit`, `particles_on`, three Visual Script nodes | No typed API | `Particles` type: Play, Stop, Restart, Burst, SetRate, SetColor, UsePreset, rate, isPlaying, windFactor, aliveCount (also Visual Script Engine API nodes) |

Wind: the field `windInfluence` (shown as "Affected by wind") already existed. The one place it is applied is
`ParticleSystem::WindAt`. The wind branch (`claude/wind-1`) edits the old `Update` loop of the header; when merging, move its
`WindField::At` line into `WindAt`.

## What changed (in the order asked)

1. **Emission and shape.** Rate, per distance, five bursts, duration, loop, prewarm; Hemisphere and Mesh surface shapes; skin-only; two colours or any hue; spawns are placed on the emitter's path inside the frame. Shapes and start speed now turn with the object (a ring on a tilted object is tilted). Sub-effects on birth, end and hit.
2. **Over life.** Colour, size, speed (already there), plus spin, force, gravity and drift curves; drift, push and away-from-centre speeds; orbit axis follows the object; three noise layers (our own lattice value noise); drag is exact (`v = a/k + (v - a/k)e^(-kt)`), so the path is the same at 30, 60 and 144 fps.
3. **Drawing.** `engine/src/Render/ParticleDraw.cpp` makes one sorted list of shapes from the camera matrix; the editor draws it with ImGui and the player with the painter (the two old copies are gone). Billboard, Stretch, Flat on the ground, Upright, Cubes; picture sheets with frame blending; ribbon trails; soft edges where a depth picture exists; lit by the scene lights (one sample per effect); sort by distance, oldest, youngest or birth order; Normal, Glow and Premultiplied blend.
4. **Collision.** Ground plane (as before) plus end-on-hit; scene colliders through `Physics3D::Raycast` from where the particle was to where it is, at most `collisionRaysPerFrame` rays a frame (rotating window).
5. **Speed.** Pooled structure of arrays (25 floats a slot in one block, about 101 bytes a slot, nothing allocated while it runs), live particles packed at the front, O(1) spawn, hard cap with Skip-new / Replace-old, distance LOD, off-screen cull (optionally pausing), `JobSystem::ParallelFor` for pools of 4096 or more (the per-particle step writes only its own slot; collision and removal stay serial; the test checks the parallel answer is bit-identical to the serial one).
6. **Editor.** `editor/ParticleEditor.cpp`: Live preview (Play / Pause / Restart / Step / Burst / Speed, an orbiting mini view of the real component), Inspector rows with plain tooltips for everything new, 17 presets in GameObject > Effects > Particle Effects, Add Component > Particle effect presets and the Scene view right-click menu. `editor/main.cpp` has only small edits (include, two combo lists, three one-line calls, the draw block).
7. **Script.** `Particles` type: `Play`, `Stop`, `Restart`, `Burst(n)`, `SetRate(n)`, `SetColor(r,g,b,a)`, `UsePreset("Fire")`, `rate`, `isPlaying`, `windFactor`, `aliveCount`. They also appear as Visual Script Engine API nodes (the older Play / Stop / Burst Particles nodes stay). `docs/engine_api.md` is regenerated.
8. **Example.** `examples/particles/particles.okayscene`: all 17 presets on pedestals with 3D labels (shipped by `scripts/build-windows-dist.sh`).

Save and load: one new optional record `particlesx` (only what differs from the default is written, so a scene that does not use the new fields is byte-for-byte what older builds wrote, and older builds skip the record). Old scenes load unchanged. Every new field is also a registered property (reflection, undo, the property panels).

**Behaviour changes in old scenes (small, on purpose).** Particles are born part-way through the frame (smooth at any fps) and the motion is the exact trapezoid, so a replayed seed no longer gives the same numbers as the old build (the golden numbers in `particles_fx` were re-recorded). Drag and wind are now frame-rate independent. Start velocity and shapes follow the object's rotation (before, a rotated emitter still sprayed along world axes). The orbit axis follows the object in World space too.

## Tests

`particles_upgrade` (new, 252 checks, seeded): determinism; emission count and mean height at 30 / 60 / 144 fps (200 / 200 / 200 particles, 2.985 / 2.985 / 2.985); motion at 30 / 60 / 144 fps (identical to 4 decimals, y = -2.4268); exact gravity and drag; every shape inside its bounds (mesh surface covers all six faces of a cube, 327 / 352 / 329 / 321 / 351 / 328); duration, loop, bursts, prewarm, per distance; start colours; spin, force, gravity, drift and radial modules; noise; caps (Skip new never exceeds, Replace old stays full and keeps the newest; negative and huge caps are clamped); no NaN for all 17 presets under frame times of 0, 1/144, 1/30, 0.5 and 3 s with a moving emitter; ground bounce, end on hit, scene colliders under a 16 ray budget; sub-effects; LOD (200 / 125 / 50 particles near / mid / far); pause when culled; parallel equals serial; the draw list (counts, sort order, near is bigger, culling, billboard modes, sheet UVs and blending, ribbons, age sorting, raster); scene round trip of every new field (and that a default system writes no `particlesx`); the example scene loads, runs and is drawn; the editor preset list equals the engine's; time and memory budget.
Also run and green here: `particles_fx` (golden numbers re-recorded), `particles_serialize`, `particles_tilemap`, `fix_140_141_142_131`, `scene_serializer_audit`, `object_pool_audit`, `duplicate_components`, `script_fx`, `vs_types` (docs regenerated), `vs_registry_effect` (the test scene now has a Particle System so the new bindings are proven), `scene_add_menu`, `examples_work` (includes the new scene).

**How these were run (be careful).** The machine could not build the whole project in a useful time, so the 17 engine files that include the particle header (plus the three new ones) were compiled from this branch with the project's own flags, and linked against the engine library of the wind branch's build for all other files. Two tests (`property_registry`, `save_coverage`) failed only because that borrowed library contains the wind branch's Wind Zone types; the rest of their checks passed. A clean full build and the whole `ctest` run were not done here. Editor and player `main.cpp` and `ParticleEditor.cpp` were only syntax-checked (compiled with `-fsyntax-only`), never linked or run.

## Numbers (`bench_particles`, 60 fps frames, the same file before and after)

The machine was loaded by other builds, so these move by about 2x between runs. Before = the old header (O(n^2) spawn scan).

| scene | before (particles per ms) | after (particles per ms) |
|---|---|---|
| sparks 500 | 34 781 | 23 700 to 28 900 (a little slower: tiny pool, the pass has more modules to test) |
| fire 4 000 | 5 105 | 8 400 to 8 700 |
| smoke 20 000 | 1 620 (12.2 ms a frame) | 5 000 to 12 400 (1.6 to 4.0 ms a frame) |
| rain 50 000 | 928 (53 ms a frame) | 33 000 to 72 500 (0.7 to 1.5 ms a frame) |

Memory: about 101 bytes a slot (1.93 MB for 20 000), measured by the test.

## Not done / unverified

- True additive blend: the painters (and ImGui) have normal blend only. Glow is a brighter, slightly more transparent normal blend. A real additive mode needs a painter change (SDL and software painters).
- Cube particles are cubes, not arbitrary meshes. Soft edges and the depth hiding need the software depth picture.
- Collision with the 2D physics world is not done (3D colliders and the ground plane only). With a small ray budget a particle can sink below a floor for a few frames before its turn (measured: lowest ever -3.45 m with 16 rays for 300 particles, back above -0.25 m at the end).
- No curve editor for start values; no curl noise; no texture sheet files ship with the engine (the example uses the soft puff).
- Sub-emitter children are found by name among the direct children.
- Not run: the editor in a window (menus, Inspector rows, live preview), the player, GPU renderers, Windows. The Inspector preview advances the effect only while its Inspector is showing; other effects in the scene move in Play.
- Merge note for the wind branch: it edits the old `Update` loop in the header. Put its `WindField::At` call inside `ParticleSystem::WindAt` and keep the `windy` check in `Tick` (a calm scene must not slow particles).

## How to try it

See `docs/try_it.md` entry 82 (open `examples\particles\particles.okayscene`, press Play; GameObject > Effects > Particle Effects; the Inspector's Live preview).
