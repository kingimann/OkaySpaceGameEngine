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
