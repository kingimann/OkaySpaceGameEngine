# Water that is not a flat plane

Owner request (2026-11-19): "improve the water so it's not just a flat plane". This page says what we already had, what looked flat or
fake, what was built and from what, the numbers, how to try it, and what is not done. Tests: `tests/test_water_visuals.cpp` (CTest
name `water_visuals`). Pictures, example scenes and the speed table come from `tests/water_shot.cpp` (`water_shot --shots|--scenes|--bench`).

The wave maths is shared with the boat work: `Environment/WaterWaves.hpp` (the `waves::Field`, written for `docs/water_boat.md`) is the
one wave function. The drawn mesh, `Water::SurfaceAt` and the boat all call it, so the picture and the physics agree (a test checks that
every drawn vertex sits on the height `SurfaceAt` returns, to 2 cm). Nothing in the physics (buoyancy, forces) was changed here.

## Existing / Gap

Looked at first: `Components/Water.hpp`, the three renderers (`Render/SoftwareRenderer.hpp`, `GLRenderer`, `D3D11Renderer`), `docs/terrain_water.md`,
`docs/volumes.md`, the sky, fog and wind code (`Render/Atmosphere.hpp`, `Scene::RenderSettings`, `Environment/Wind.hpp`), `ParticleSystem`, `Terrain`, and
the examples (`examples/boat`). Pictures of calm lake, sea, shore and river were rendered headless and looked at (not committed).

| Area | Existing | What looked flat or fake | What this change does |
|---|---|---|---|
| Wave shape | 3 crossing sines plus a wind wave; boat branch added Gerstner waves (`Ocean`, `Calm lake`) | the sines have round crests; a calm lake had nothing moving but one soft swell | **Sea State** (Calm, Light, Rough, Storm) sets the Gerstner numbers; ripples on top (below) |
| Small detail | `uvScroll` of an optional texture (none by default) | nothing fine moved on a calm surface: it looked like a tinted plane | `proc:water_normal` (our own tileable ripple normal map) as the normal map, plus per-vertex ripples from two noise layers drifting in two directions |
| Mesh | one square grid, same cell size everywhere, fixed on the object | waves near the camera were blocky, far ones wasted triangles; the edge of the square showed | **Rings of detail**: square rings, each twice as coarse as the one inside, centred on the camera and snapped to a lattice (no swimming), cracks closed; **Sea with no edge** |
| Colour | one tint for everything (`color`) | the water was the same blue over a beach and over deep water | depth colour (turquoise to deep blue) from the Terrain under it, or `Depth`, or `Assumed depth` |
| Reflection and glint | software renderer: Fresnel (Schlick) sky mirror, sun glint, `reflectivity`/`specular` (already there) | fine | kept as it is; the ripple normals now break the mirror up |
| Foam | none | no white on crests, none at the shore | white caps from crest height and slope, a surging foam line where the water meets the ground (depth near 0), foam on ring ripples |
| Light in the crests | none | thin waves looked like opaque plastic | green-blue glow in crests when the sun is behind them |
| See-through | one `opacity` for the whole surface | the bottom is equally hidden near the shore and far out | **not done** (the renderers have no per-vertex transparency); only the colour follows depth |
| Things in the water | none visible (buoyancy only) | a crate falling in made no splash | ring ripples that fade, spray from the existing `ParticleSystem`, a wake hook (`AddWake`) a boat can call |
| Underwater | none | camera under the surface saw the same as above | blue-green exponential fog from the scene's own fog settings (restored after), bubbles option |
| Rivers | `flow` scrolls a texture | the current did nothing you could see | `current` (boat branch) drags both ripple layers, and `FlowVector()` gives the world flow for the physics |
| Settings | many numbers, no presets | hard to get a good look | 8 presets, a Sea State list, a **Look** section with plain tooltips, **GameObject > 3D Object > Water** submenu |

## What was written, and from what (rule 19a)

All new code is ours, written from general published ideas, not from any other project.

* `engine/include/okay/Environment/WaterShading.hpp`: value noise and fractal sums (lattice hash and smooth interpolation); Schlick's
  Fresnel approximation; Beer-Lambert absorption (`T = exp(-sigma d)`, red faster than blue); a backlit-crest scattering term; a ring wave
  (`cos(k(d - ct))` under a moving Gaussian that fades with age and distance); caustics as ridges where two moving noise fields cross; sea-state
  numbers; the clipmap-style ring grid (nested square rings, doubling cell size, shared snapped centre, odd edge vertices pulled onto the
  neighbour's edge). Constants were chosen by us and checked by tests.
* `engine/include/okay/Render/ProcTexture.hpp` (`water_normal`): fractal noise heights, normal from wrapped neighbour differences.
* `Components/Water.hpp`: the look code (vertex colours, normals, rings, splashes, underwater) and the presets.
* `tests/water_scenes.hpp`: the example scenes, built from cubes, spheres, a Terrain and the Water.
* Wave formula: Gerstner (trochoidal) waves, in `WaterWaves.hpp` (see `docs/water_boat.md`).

## How it works

`Water` has new settings (all off or neutral in an old scene; a scene saved before this change loads with the look off and looks as before):

| Setting | Meaning |
|---|---|
| Preset | Calm Lake, Pond, Ocean, Stormy Sea, River, Swamp, Tropical, Ice-cold: sets waves, colours, foam, ripples, current |
| Sea State | Custom, Calm, Light, Rough, Storm: wave height, length, sharpness, count, speed and foam |
| Realistic look | turns on everything below that paints the surface (off = the old plain plane) |
| Shallow / Deep colour, Clarity, Assumed depth | depth colour; Clarity = metres before it looks deep; the Terrain under the water gives the real depth |
| Foam, Shore foam | white caps (0..1); width in metres of the foam line at the shore (0 = none) |
| Crest glow | the glow when the sun is behind a wave |
| Ripples, Ripple size | the small never-stopping ripples (0 off .. 1 strong) and their size |
| Rings of detail, Rings, Sea with no edge | fine near the camera; `Detail` (resolution) is the cells across each ring |
| Splashes and ripples | things that fall in or move through make rings and spray while playing |
| Underwater look, Bubbles | fog under the surface; rising bubbles |

How the picture is made: every frame the mesh is rebuilt (as before). Each vertex gets its height (and sideways shift) from the wave field,
a normal from the field's slope bent by the ripple slope, a colour from `WaterShading` (depth colour, foam, glow), and a world-anchored UV
for the ripple normal map. The colour is stored as the mesh's vertex colour and the normal as the mesh normal, which the software, GL and D3D11
renderers all already draw, so no renderer needed a new feature (the GL and D3D11 look is therefore unverified, not missing).

Code hooks: `water->AddRipple(worldPoint, strength)`, `water->AddWake(worldPoint, velocity, dt, id)` (call every frame from a boat; it lays one ring
per half metre travelled, at any frame rate), `water->FlowVector()` (world flow, metres per second; the water does not push anything itself),
`water->CameraIsUnder()`, `water->SetSeaState(n)`, `water->ApplyPreset(n)`.

## Numbers
Test: `water_visuals`, 204 checks, 0 failures (Linux, software renderer, headless). It covers: noise, ripples (never flat, off = flat, drift with the
current), Fresnel (2 percent straight down, 100 percent grazing, rising), absorption and depth colour, crest and shore foam, crest glow, ring waves
(front speed 1.5 m/s, fade, a splash lifts the surface by centimetres, never metres), caustics, underwater fog, sea states (height, steepness, foam all rise
Calm to Storm), the ring grid (exact area of the square, no overlap, every vertex on its level's lattice, the whole grid moves by exactly one coarse cell when
the camera does, a small camera move changes nothing, stitched vertices sit half way between their neighbours, clamping keeps a lake's edge), the component
(old water unchanged, vertex data finite and in range, a beach gives turquoise shallows and a lighter foam line, rings dense near the camera, 215 of 217 sampled
vertices within 2 cm of the surface the boat reads (the others are the seam vertices pulled onto the coarse edge), 30 and 144 fps give the same surface to 2 mm,
splash rings the same at 30 and 144 fps, the wake hook lays the same number of rings at 20 and 60 fps, rings are capped at 64, a crate dropped in makes
rings and spray, fog turns on under the surface and the old fog comes back, 8 presets, sea states), saving and loading (round trip, old file without the record,
damaged numbers repaired), properties, and pictures with tolerances: a calm lake is not one flat colour, a storm varies more and has 14 percent of its vertices
painted foam against 0 for the lake, the same picture twice is identical, 30 and 120 fps give the same picture to within 2.5 grey levels, the old look is
clearly different from the new one (variance 10 against 47), and under the surface the picture is bluer.

Speed of the water mesh (`water_shot --bench`), a 100 x 100 m surface, milliseconds per frame, Release, one core, **measured while the machine was
heavily loaded by other jobs (load average 13 on 4 cores), so real numbers are lower, perhaps by a factor of 2 to 4**. Mesh build only (no drawing):

| Resolution | Old look (Classic sines) | Realistic look | Realistic look + rings of detail (4 rings) |
|---|---|---|---|
| 48 | 0.4 | 3.5 | 9.7 |
| 96 | 1.0 | 12.3 | 36 |
| 192 | 4.0 | 49 | 151 |

Rings multiply the vertex count (resolution 48 with 4 rings is about 7,700 vertices, the finest cell is 26 cm, against 2.1 m cells in the flat grid), so they are
the costly setting; the presets use Detail 48 (the editor's menu sets it). The mesh is rebuilt every frame as it always was; a later step could rebuild only
the far rings less often.

Pictures (software renderer, made by `water_shot --shots <dir>`, not committed): `lake`, `ocean_calm`, `ocean_rough`, `ocean_storm`, `ocean_old_look`, `river`,
`shore`, `underwater`. Seen by the author: the old look is a flat pale sheet; the new ocean has rolling crests with white streaks, the river has flowing ripples
between banks, the shore goes from turquoise to deep blue with a surf line, and under the surface the view is green-blue fog.

## Not done

* Refraction (the bottom shifted by the surface) and see-through that grows with depth: the renderers have one opacity per material and no
  per-vertex alpha, and no refraction hook. The bottom is seen through the same opacity everywhere; only the colour follows depth.
* Caustic light on the bottom: `watershade::Caustics` exists and is tested, but no renderer hook puts a light pattern on other objects.
* GL and D3D11: no shader work was needed (vertex colours and the normal map are existing features) and none was run. Windows was not run.
* Foam is painted per vertex, so it is as fine as the grid (rings make it fine near the camera); there is no foam texture that follows the wave.
* Foam from a wave folding over (a Jacobian) is not used; the foam uses crest height and slope.
* The wake hook is called by a script or the boat (the boat agent can adopt it); nothing in the boat code was changed here.
* Rings have no effect on floating things (visual only).
