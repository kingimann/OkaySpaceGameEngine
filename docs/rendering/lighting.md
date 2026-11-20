# Lighting

> **What this is:** Lights, shadows, the lighting upgrade record and the day / night cycle.
> **Status:** combined on 2026-11-20 from 3 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Lighting and shadows](#lighting-and-shadows) (was `docs/lighting.md`)
- [Lighting upgrade (owner request: "improve lighting")](#lighting-upgrade-owner-request-improve-lighting) (was `docs/lighting_upgrade.md`)
- [Day / Night Cycle](#day--night-cycle) (was `docs/day_night_cycle.md`)

## Lighting and shadows

*Was `docs/lighting.md` until 2026-11-20.*

Every scene is lit by its **Light** objects (add one with *Add Component →
Lighting → Light*), plus an ambient term. A scene with no Light gets a default
sun so it never renders black.

### Light types

| Type | What it is | Shadows |
|------|------------|---------|
| **Directional** | The sun/moon: parallel light along the object's +Z. | Cascaded sun shadows (only the **first** enabled Directional light casts them). |
| **Point** | A bulb: radiates in all directions out to **Range**. | Six shadow-map faces (a cube) — walls block it. |
| **Spot** | A cone of **Spot Angle** degrees with a **Softness** edge. | One perspective shadow map. |
| **Area** | A broad soft panel (windows, screens, studio fill). | Like a point light (six faces). |

Common settings: **Color** or **Use Temperature** (Kelvin, with Candle → Shade
presets), **Intensity**, **Range** and **Falloff** (Linear or physical Inverse
Square) for local lights, and **Ambient** + **Ambient Tint** (the first light's
ambient sets the scene's unlit floor). Unticking a Light component's enabled box
turns the light off (saved with the scene).

#### Shadows (per light)

- **Cast Shadows** — on by default for every light.
- **Strength** — 0 = invisible, 1 = fully dark (down to the ambient light).
  Lower it for a bright overcast feel or a weak fill light.
- **Bias** — raise it if lit surfaces show dark speckles or stripes ("shadow
  acne"); lower it if shadows look detached from the objects casting them.

Spot, point and area lights share a budget of **16 shadow tiles** per frame: a
spot light takes 1, a point/area light takes 6. Lights closest to the camera get
tiles first; lights whose reach is entirely off screen are skipped. A light that
doesn't get tiles still lights the scene — just without shadows. Turn **Cast
Shadows** off on minor fill lights to leave the budget for the important ones.

#### Flicker

**Amount** (0–1) randomly dims the light by up to that fraction while the game
runs; **Speed** sets how many changes per second. Presets: *Candle*, *Torch*,
*Faulty bulb*. Each light flickers independently. In edit mode the light stays at
full intensity so you can still light the scene; flicker plays in Play mode and in
the built game.

### Mesh shadow settings

Each **Mesh Renderer** has:

- **Cast Shadows** — this mesh blocks light. Turn it off for skyboxes, decals or
  floors that never occlude anything (saves shadow-map time too).
- **Receive Shadows** — shadows from other meshes darken this one.
- **Ground Shadow** — a cheap flat "blob" projected onto a ground plane. It is a
  **fallback** only drawn while real shadows are off (Rendering → Shadows
  unticked, or the sun's Cast Shadows off); with shadow maps on it would
  double-darken shadows and show through anything between the object and the
  plane.

Transparent materials (opacity < 1) never cast shadows.

### Scene ambient

In **Environment / Render Settings**, **Ambient Mode** picks how the ambient
(indirect) light varies with surface direction:

- **Flat** — the same everywhere.
- **Hemisphere** (default) — brighter on top, darker underneath.
- **Sky colors** — tops take the sky gradient's colours and undersides the Sky
  Bottom colour; **Sky Tint** sets how strongly (0–1.5).

**Ambient** there is the level used when the scene has no Light object.

### Shadow quality

**Rendering (Lighting & Post FX)** in the editor (and **Build Settings →
Graphics** for the exported game):

- **Shadow softness** — the soft-edge radius in shadow-map texels (0 = hard).
- **Shadow distance** — how far sun shadows reach in front of the camera. 0 =
  the legacy single map fit to the whole scene.
- **Shadow cascades** (1–4) — the sun's shadow is split into rings of
  increasing size: crisp up close, still present far away. Neighbouring
  cascades blend so there is no visible seam, and the last one fades out at the
  shadow distance.
- **Shadow resolution** — texels per cascade (512–4096). Spot/point light tiles
  are a quarter of this (256–1024).

### How it works

The GPU renderers (OpenGL in the editor and Linux player, Direct3D 11 in the
Windows player) share one plan (`engine/include/okay/Render/ShadowSetup.hpp`):

- The sun's cascades are orthographic depth maps laid side by side in one
  atlas, each fit to a sphere in front of the camera (radii grow geometrically
  out to the shadow distance, capped at the scene's size) and snapped to the
  texel grid in light space, so shadows don't shimmer as the camera moves.
- Spot lights render one perspective depth map; point and area lights render
  six cube faces (slightly wider than 90° so the soft filter never reads past a
  face edge). All local maps live in one 4×4 tile atlas.
- The shader picks the tightest cascade (or the cube face by the dominant axis
  from the light), pushes the receiver a few texels along its normal and toward
  the light (world-space bias, the same for every map type), then filters with
  12 Poisson taps — rotated per pixel — each a hardware bilinear depth compare.

Highlights (specular) take each light's colour and intensity.

#### Limitations

- Only the first Directional light casts shadows; other directional lights
  light the scene unshadowed.
- The software renderer (used when no GPU is available) draws sun shadows and,
  since the render parity work, spot, point and area light shadows (up to 16
  tiles a frame, nearest lights first: a spot is 1 tile, a point light 6). It
  draws them by default: while a shadow map exists, meshes that receive shadows
  are lit per pixel even with Per-pixel lighting off (`ShadowsUsePerPixel()`,
  default on; set it false for the faster per-vertex frame, which draws no shadows). The
  opt-in Direct3D 12 backend is a preview with a single sun shadow map (no
  cascades, no spot or point shadows). The Light Inspector says so under Cast
  Shadows: "Spot and point light shadows are not drawn on Direct3D 12."
- At most 16 lights are shaded per frame on the GPU (the first 16 Light objects).

### Lighting upgrade (2026-11)

The full list of what changed, with the "Existing / Gap" table, is in [lighting_upgrade.md](lighting.md#lighting-upgrade-owner-request-improve-lighting). In short:

- **Lighting presets** (Lighting window, top): *Sunny day*, *Sunset*, *Night with lamps*, *Indoor*, *Spooky fog*. One click sets sky,
  ambient, fog, post look and the lights. One scene per preset is in `examples/lighting/` (File > Examples > Lighting presets).
- **Sun shadows** no longer shimmer when the camera moves (the cascade grid is tied to the world), use a bias that follows the slope
  of the surface, and (software renderer) get softer the farther the shadow falls from the object (**Soft Edges**) and fade out at the
  shadow distance instead of stopping at a line.
- **Spot lights** have a smooth cone edge (all renderers). **Area lights** can be a real rectangle (**Panel Width / Height**) and
  **Directional / Spot lights** can shine through a **Mask** (window, blinds, dots, leaves, star, soft circle, or your own picture):
  software renderer only for now.
- **Units**: a Light can use **Real world** units (lux for the sun, lumens for lamps); **Auto brightness** (Lighting window) keeps the
  scene readable like an eye adapting (all renderers).
- **Glow Light Range** on a Mesh Renderer: a glowing (Emissive) object also lights its surroundings (all renderers).
- **Classic lighting look** (Lighting window, or `OKAY_CLASSIC_LIGHTING=1`): the software renderer's old shadows and light roll-off, to compare.

## Lighting upgrade (owner request: "improve lighting")

*Was `docs/lighting_upgrade.md` until 2026-11-20.*

Branch `claude/lighting-2`. This page lists what lighting the engine already had, the ten gaps that showed most in headless renders
of four test scenes (a sun with cubes, an indoor room, a spot light on a pillar, twelve coloured point lamps), what was changed
for each, and what was not. Plain steps to see every change are in `docs/try_it.md` (entry `7lit`).

### What each file was written from (CLAUDE.md rule 19a)

All new code was written for this engine from general rendering knowledge: shadow-map texel snapping, receiver-plane (slope) bias,
a blocker search that widens the filter with the caster-to-receiver gap, a golden-angle disc of filter taps, smoothstep, a soft
shoulder for over-bright light, lux and lumens (candela = lumens over solid angle), colour temperature of daylight, moonlight and
lamps. No code, shader text, comments or tests were taken from another project. The only built-in masks (window, blinds, dots,
leaves, star, soft circle) are drawn by our own code, so there are no image assets and nothing to license.

| New or changed file | Written from |
|---|---|
| `engine/include/okay/Render/ShadowMath.hpp` | the ideas above; every constant is ours and pinned by `tests/test_lighting_upgrade.cpp` |
| `engine/include/okay/Render/Lighting.hpp` (spot cone, soft clip, units, masks, panels, shared per-light terms) | smoothstep, the one-sided rectangular emitter (3 x 3 samples of cosine terms), lux/lumens |
| `engine/include/okay/Render/LightingPresets.hpp` | our own look presets (`Atmosphere.hpp`) and colour temperatures |
| `engine/include/okay/Render/SoftwareRenderer.hpp` (`ShadowFactor`, `BuildCascade`) | the shadow maths above |
| `engine/include/okay/Render/ShadowSetup.hpp`, `D3D11Hlsl.hpp`, `GLRenderer.cpp` | the same snapping and slope bias, and the smoothstep cone |

### Existing / Gap

"Existing" was read in the code and checked with the headless renders (`tests/lighting_shot.cpp`). "Now" says what this change did.

| # | Existing | Gap (what you could see) | Now |
|---|---|---|---|
| 1 | Sun shadows in cascades with "texel snapping" (software, OpenGL, Direct3D 11, Direct3D 12) | The snap rounded the middle of the shadow box **in the light's own view, where it is always (0, 0)**, so it never snapped anything (GPU and Direct3D 12). The software renderer rounded on the world axes instead of the light's axes. Shadow edges crawled and shimmered when the camera moved. | The texel grid is tied to the world (`shadowmath::CascadeSnapShift`), used by all four renderers. Unit test: a point's shadow-map position only moves by whole texels when the camera creeps by odd fractions of a texel. |
| 2 | Software: depth bias 0.004 (about 15 cm in a typical cascade) plus a fixed 3.5-texel push along the normal | Shadows floated away from the objects that cast them, while sloped ground still got stripes | Bias follows the slope of the surface and the filter width (`NormalOffset`, `DepthBias`). GPU shaders: the normal push now grows with the slope (`sinT`). |
| 3 | Software: one fixed 6-tap blur | Every shadow edge equally soft; no contact hardening | A blocker search measures the gap between object and ground; the blur grows with it, up to 14 texels (`PenumbraTexels`). New **Soft Edges** slider on the light. Software renderer only. |
| 4 | Sun shadows stop at the shadow distance | A visible line where shadows end | Smooth fade over the last fifth of the distance (`DistanceFade`). Software renderer only (the GPU shaders already blend into the last cascade). |
| 5 | Spot cone: straight ramp, squared | A ring where the soft edge starts | Smoothstep (`SpotCone`), in all four renderers' shading |
| 6 | The summed light was hard-clamped at 1.0 | Any lamp brighter than 1 looked the same flat white, and a colour channel that hit 1 first shifted the hue | Light up to 1.0 is unchanged; above 1.0 a smooth shoulder (towards 1.3) keeps the order of brightness and the colour (`SoftClipLight`). Software renderer only (the GPU renderers tonemap). |
| 7 | Area light = a point light with soft wrap | No size, no shape | **Panel Width / Height**: a real one-sided rectangle (3 x 3 samples). 0 x 0 keeps the old soft glow. Software renderer only. |
| 8 | No light masks | No window shafts, blinds, tree dapple, gobos | **Mask** on Directional and Spot lights: six built-in masks or your own picture. Software renderer only. |
| 9 | Intensity is a bare number (the slider stops at 8) | Hard to match a sun and a lamp; a dark scene stays dark | **Units**: Simple or Real world (lux for the sun, lumens for lamps) and the scene's **Auto brightness**. All renderers (done when the lights are gathered). |
| 10 | Emissive meshes glow only themselves | A glowing lamp or screen does not light its surroundings | **Glow Light Range** on a Mesh Renderer adds a small point light of the emissive colour. All renderers. |
| + | The editor had look presets for the post chain only | Setting a mood took many steps | **Lighting presets** (Sunny day, Sunset, Night with lamps, Indoor, Spooky fog) in one click, and one example scene each. |

#### Looked at and left as is (already there)

- Hemisphere and sky-colour ambient (Ambient Mode: Flat, Hemisphere, Sky colors) already follows the sky (`SceneAmbientColors`), also for
  the procedural sky.
- SSAO exists on OpenGL and Direct3D 11 (not in the software renderer's scene chain); baked lighting with ambient occlusion exists (`Bake Lighting`).
- Spot, point and area shadows exist (16 tiles). Flicker, Kelvin temperature and inverse-square falloff exist.

#### Not done

- **Reflection probes and box-projected environment capture**: not built. Reflective materials still reflect the sky gradient only.
- **Light probes** and a software SSAO: not built.
- **GPU versions** of Soft Edges, masks, rectangle panels and the soft light shoulder: not written. OpenGL and Direct3D 11 draw an Area
  light as a soft point light and ignore masks. The Light Inspector says so (`RenderFeature::LightShapes`).
- The cascade fix, slope bias and smoothstep cone **were** changed in the OpenGL, Direct3D 11 and Direct3D 12 code but **never run on a GPU** here.

### How it was checked (and what was not)

Run on a Linux machine, software renderer, `-O0` build, 2026-11-19. Pictures were looked at but are **not** in the repository.

| Check | Result |
|---|---|
| `lighting_upgrade` (`tests/test_lighting_upgrade.cpp`: shadow maths, stable cascades, bias, fade, spot cone, roll-off, units and auto brightness, masks, rectangle panel, save and load, glow lights, presets, a closed room keeps the sun out, repeatable renders) | 194 checks, 0 failures |
| Stable cascades, measured: how far a fixed point's shadow-map position drifts when the camera creeps by fractions of a texel | new code 0.0001 texel, classic code 0.49 texel (the shimmer; the test fails on the old code) |
| `light_shadows`, `lighting`, `shadows`, `lightmap`, `software_renderer`, `software_local_shadows`, `software_default_shadows`, `software_scene_post`, `software_perf_paths`, `serialize`, `scene_serializer_audit`, `property_registry`, `post_sky`, `inspector_model`, `renderer_features` | all pass (these run the old and the new code paths side by side) |
| `examples_work` (loads and plays every shipped example scene, including the five new ones) | 89 scenes, 0 failed |
| `editor/main.cpp` syntax-only compile (`g++ -fsyntax-only`, with the editor's include folders) | no errors. **The editor was not linked or run**; the new panels were never drawn |
| `D3D11Renderer.cpp`, `D3D12Renderer.cpp` syntax-only with MinGW (win32 thread model) | no errors. **Never run on Windows or a GPU** |
| `GLRenderer.cpp` | **not compiled as GLSL, not run** (round 2 adds more GLSL; see the Round 2 section) |

Seen in the pictures: softer, graded shadow edges and no ring in the spot cone (software renderer); the five presets look clearly
different; the window mask draws panes of sun on the floor. One thing that is not perfect: with 16 filter taps and no per-pixel
rotation a wide soft shadow shows faint bands at its far edge. A per-pixel rotation would hide that but makes renders depend on
position noise, so it was left out.

Round 2 results (same machine): `lighting_upgrade` 194 checks, 0 failures (adds probes, scene SSAO, save/load of probes); `d3d11_layout` 80 checks,
0 failures (compiled standalone, header-only); `light_shadows`, `renderer_features`, `lighting`, `serialize`, `scene_serializer_audit`,
`software_scene_post`, `software_perf_paths` pass. Measured: a probe inside the closed room gives a darker ambient (summed over the three colour channels) than one over open ground; the test only checks that order, not a size (the red channel alone reads 0.216 inside and 0.165 outside because the sky is blue); scene SSAO lowers the room's mean brightness from 0.3258 to 0.3228 (a small, real effect on corners and feet).

#### Try it

`docs/try_it/7lit-*.md` has the steps. In short: File > Examples > Lighting presets, then View > Lighting > Lighting Presets; a Light's
Inspector for Units, Panel Width / Height, Mask and Soft Edges; and the headless tool
`lighting_shot <out dir> [tag]` (set `OKAY_CLASSIC_LIGHTING=1` for the old look) that writes the test pictures.

#### Bias, corrected after CI (round 3)

The first slope bias grew with the shadow-map texel size, so in a coarse cascade (one 256-texel map over 80 m: 0.6 m a texel) it was
metres wide and small casters lost their shadow (the `fp_hand` test found it: a head's shadow vanished). The software renderer now compares every filter tap
with the receiver's own depth at that tap (`shadowmath::ReceiverPlaneShift`) plus a constant of about one texel, and the blocker search
includes the centre and an inner ring so a one-texel caster is found. Not fixed, and not fixable by bias: a caster narrower than a
texel can still fall between texel centres (the `fp_hand` test now uses a 12 m shadow distance for that reason). The GPU shaders keep their
own world-space bias and were not changed in this round.

## Day / Night Cycle

*Was `docs/day_night_cycle.md` until 2026-11-20.*

`DayNightCycle` advances a 24-hour clock and drives the scene's sun and sky so it
slowly turns day → dusk → night → dawn — no scripting.

### Setup

1. Make a **Sun**: an object with a **Light** (Directional). 
2. Add a **Camera** (it becomes the scene's main camera).
3. Drop **Add Component ▸ Gameplay ▸ Day / Night Cycle** on any object. Press Play.

It drives the sibling Light if the cycle is on the Sun, otherwise the first Light in
the scene, plus the main camera for the sky.

### What it does each frame

- Advances `time` (hours, 0–24); a full day takes `dayLengthSeconds` of real time.
- **Sun light**: rotates across the sky (noon overhead), brightens toward
  `dayIntensity` at noon and dims to `nightIntensity` at night, warm at sunrise/
  sunset and cool (moonlight) deep at night. Ambient floor eases between
  `dayAmbient` and `nightAmbient`.
- **Sky**: blends the main camera's background through `skyDay` → `skyHorizon`
  (sunrise/sunset orange) → `skyNight`.
- Publishes the current hour to a saved value `hour` — bind a UI text with
  `{hour}` for a clock readout.

### Fields & API

| Field | Meaning |
|-------|---------|
| `dayLengthSeconds` | real seconds for a full 24h day (e.g. 120) |
| `time` | current hour, 0–24 |
| `paused` | stop the clock (set `time` by hand) |
| `controlSun` / `rotateSun` | drive the sun light / rotate it across the sky |
| `controlSky` | drive the camera background |
| `dayIntensity` / `nightIntensity`, `dayAmbient` / `nightAmbient` | light range |
| `dayLight` / `nightLight` | sun tint at noon / midnight |
| `skyDay` / `skyHorizon` / `skyNight` | sky gradient stops |

Methods: `SetTime(hour)`, `AddHours(h)`, `Pause(on)`, `Hour()`, `IsNight()`,
`Elevation()` (−1 midnight … +1 noon).
