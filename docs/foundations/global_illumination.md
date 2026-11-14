# Global illumination foundation: probes and a CPU baker

Status: design page. Phase 1 code is in the engine (see "Phase 1 files"). Phases 2 to 4 are not written.
Nothing here is wired into a renderer yet, and nothing here changes how any existing scene looks.

## What global illumination is

Direct light is light that goes from a lamp to a surface. Global illumination (GI) is the light that
then bounces off that surface and lights other surfaces. A red wall makes a white floor next to it
slightly red. A bright window lights the whole room, not only the patch of floor the sun hits.

Today the engine has direct light (sun, point, spot, area), shadows, and one flat ambient colour (or a
sky-to-ground blend, `SceneLights::AmbientAt`). The only bounce light is that ambient term, and it
knows nothing about the room around it. `BakeFaceLighting` bakes direct light, shadows and a small
ambient-occlusion estimate into per-face colours. It has no bounces.

## What Unreal does

- **Lightmass (baked).** An offline baker (Unreal now ships GPU Lightmass, a path tracer) writes
  light into textures that are laid over static meshes (lightmaps) and into volumes for moving things
  (Volumetric Lightmap: a sparse grid of spherical-harmonic probes). Quality is high and runtime cost is
  near zero. The cost is bake time, UV layouts for every static mesh, and no response to moving lights.
- **Lumen (dynamic).** A real-time GI system. It traces rays against a signed-distance-field scene,
  or against hardware ray tracing, caches the results in screen-space probes and a surface cache, and
  filters them over a few frames. It reacts to moving lights and doors. It needs a fast GPU, a lot of
  engineering and a denoiser.
- **Reflection captures.** Cube maps captured in the scene, with a box or sphere influence shape,
  blended on surfaces for glossy reflections.

## What we will do

We start with the part that is practical on our stack (CPU code we can test without a GPU), and that
gives most of the benefit for static and slow-changing scenes.

| Phase | What | Needs a GPU to finish? | State |
|---|---|---|---|
| 1 | Light probe volumes: a grid of L2 spherical-harmonic (SH) probes baked by a CPU path tracer. CPU reference for sampling and interpolation. Serialization. | No (all CPU, unit tested) | Code written |
| 2 | Reflection probes: cube maps with a parallax box and blending between probes. | Capture needs a renderer; the CPU baker could also fill them | Design only |
| 3 | Runtime probe sampling in the OpenGL and Direct3D 11 shaders (3D texture or buffer of SH coefficients, trilinear blend, replaces the flat ambient term). Software renderer uses the CPU function now. | Yes | Not started |
| 4 | Later: screen-space GI, or a software ray-traced GI for moving lights. UV lightmaps for fine contact shadows. | Yes | Idea only |

### Phase 1 in detail

1. **Ray tracer** (`GiBvh.hpp`). A bounding volume hierarchy over the triangles of the scene's static
   meshes. Built with a binned surface-area heuristic, traversed with an explicit stack. Closest-hit and
   any-hit queries. Our own code; the tests compare it with a brute-force loop.
2. **Path tracer** (`GiBaker.hpp`). For each probe, shoot rays over the sphere. Each ray follows
   diffuse bounces (cosine-weighted), with next-event estimation for sun, point and spot lights (a shadow
   ray to the light at every hit), emissive surfaces counted on hit, and the sky gradient on a miss.
   Russian roulette ends long paths without bias.
3. **Probe volume** (`LightProbeVolume.hpp`). A box of `nx * ny * nz` probes. Each probe holds 9 SH
   coefficients per colour channel (L2). Radiance is projected onto SH, and sampled back as diffuse
   "ambient" light for a world point and normal (the Ramamoorthi and Hanrahan irradiance formula).
   Between probes we blend the 8 nearest linearly (trilinear). Because SH is linear, blending
   coefficients equals blending results, so the shader does one blend then one dot product.
4. **Baker API.** Progress and cancel callback, deterministic seeding: each probe gets its own random
   seed from (user seed, probe index), so the result does not depend on order or thread count.
5. **Serialization.** A small binary format: magic, version, grid size, bounds, coefficients, CRC32.
   Loading checks every length before allocating, rejects NaN and absurd sizes, and verifies the checksum.
6. **Ambient lookup.** `ProbeAmbientAt(volume, point, normal, fallback)` gives the ambient light for the
   software renderer now, and is the exact reference the shaders must match later.

#### Units, and what the probes replace

The engine shades a surface as `albedo * (ambient + sum of lights)`. The probes use the same units: a
probe returns the colour a white surface would get from the indirect light, so a uniform sky of colour
`c` gives ambient `c`. Probes carry the sky and the light that bounced off surfaces. They do not carry
direct light from lamps, because the renderer already adds that. When probes are in use they replace the
flat or hemisphere ambient term only.

#### Known limits of phase 1

- No visibility in the blend: a probe on the other side of a thin wall can leak light through it. Fixes
  (per-probe depth, or moving probes out of walls) are later work.
- L2 SH is soft. It cannot do sharp indirect shadows or glossy light. That is what reflection probes and
  the lightmaps are for.
- Static meshes only. The baker does not see moving objects. Area lights are treated as point lights.
- Materials are a flat diffuse albedo and an emissive colour. Textures do not tint bounces.
- Probes inside solid geometry are not detected or repaired.
- Single-threaded bake.

## Phase 2: reflection probes (design)

A reflection probe stores a cube map (for example 128 or 256 pixels per face, with mip levels for
roughness) captured from its position, plus an influence box. At runtime a surface picks the probes
whose boxes contain it, blends the two or three nearest by distance to the box edge, and corrects the
reflection direction with a box-projection ("parallax") so the reflection of a wall stays on the wall.
HDR format (RGBA16F or RGB9E5), prefiltered with GGX. The capture needs the renderer (render six views),
so it is mostly GPU work. A CPU capture through the baker is possible for a slow, exact fallback. Data
will live in a `ReflectionProbe` component and a baked asset next to the scene.

## Phase 3: shader sampling (design)

Upload the grid as a 3D texture per SH band (RGBA16F, 7 textures of coefficients) or one structured
buffer. The shader finds the cell from the world position, fetches 8 probes, blends, evaluates the
irradiance polynomial with the surface normal, and multiplies by albedo. Both GLSL and HLSL mirror the
CPU function in `LightProbeVolume.hpp`. This cannot be verified without a GPU, so it will be marked
"compile-checked only" like the other GPU work in `docs/UNFINISHED.md`.

## What needs a GPU

- Phase 1: nothing. All of it runs and is tested on the CPU.
- Phase 2: capturing needs the renderer. Prefiltering can be CPU or compute shader.
- Phase 3: all of it. Writing the shaders can be done blind, running them cannot.
- Phase 4: yes.

## Honest effort estimates

These are for one developer, including tests and docs, and assume a GPU is available for phases 2 and 3.

| Piece | Estimate |
|---|---|
| Phase 1 (done in this change, CPU only, unit tested) | done |
| Phase 1 editor: bake button, probe volume component, save beside the scene | 2 to 3 days |
| Phase 1 quality: probe-in-wall repair, visibility weights, multi-threaded bake | 3 to 5 days |
| Phase 3 GL shader sampling | 2 to 3 days |
| Phase 3 D3D11 shader sampling | 2 days, plus a Windows machine to run it |
| Phase 2 reflection probes (capture, prefilter, blend, parallax, component, editor) | 2 to 3 weeks |
| Phase 4 screen-space GI | 3 to 4 weeks |
| Phase 4 software ray-traced GI (a Lumen-like path) | months, and it needs a GPU path |
| Phase 4 UV lightmaps | 2 to 3 weeks (UV unwrap, atlas, bake, sampling) |

## Phase 1 files

- `engine/include/okay/Render/GiBvh.hpp`: triangles, rays, BVH build and traversal.
- `engine/include/okay/Render/GiSH.hpp`: L2 SH projection, evaluation, irradiance.
- `engine/include/okay/Render/LightProbeVolume.hpp`: volume, trilinear sampling, ambient lookup, serialization.
- `engine/include/okay/Render/GiBaker.hpp`: scene description, path-tracing integrator, bake loop.
- `tests/test_gi_probes.cpp`: the tests.

## How we will know it is right

- BVH answers equal a brute-force loop on random scenes.
- A closed room of albedo `a` and emission `E` converges to `E / (1 - a)` (the furnace test).
- Light never increases: with albedo below 1 and no emission, no radiance exceeds the sky's.
- SH projection of known functions reconstructs within stated error bounds.
- Interpolation is continuous across cell borders.
- The same seed gives byte-identical output.
- Visual quality is not tested here. It needs a renderer, and is unverified.

## Provenance

All new files were written from scratch in this repository, from published algorithms and maths, with no code, comments or tables copied or ported from another project.

- `GiBvh.hpp`: Moller-Trumbore ray/triangle test (Moller and Trumbore 1997), slab ray/box test, binned surface-area-heuristic BVH build (the general technique from the ray-tracing literature).
- `GiSH.hpp`: real spherical harmonic basis and the irradiance (cosine convolution) formula of Ramamoorthi and Hanrahan, "An Efficient Representation for Irradiance Environment Maps" (2001). The constants are checked numerically in `TestShConstants` (orthonormality and agreement with a brute-force cosine integral).
- `LightProbeVolume.hpp`: trilinear interpolation; CRC-32 (the standard published polynomial); our own binary layout.
- `GiBaker.hpp`: Monte Carlo path tracing with cosine-weighted bounces, next-event estimation and Russian roulette (Kajiya 1986, standard texts). The random generator is our own: a counter plus an xor-shift/multiply mixer whose two multipliers are the fractional bits of sqrt(2)-1 and sqrt(3)-1 and whose step is the fractional bits of the golden ratio, all computed by us; its quality is tested in `TestRng`.
- `tests/test_gi_probes.cpp`: written from scratch.
