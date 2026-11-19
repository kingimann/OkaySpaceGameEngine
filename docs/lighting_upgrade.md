# Lighting upgrade (owner request: "improve lighting")

Branch `claude/lighting-2`. This page lists what lighting the engine already had, the ten gaps that showed most in headless renders
of four test scenes (a sun with cubes, an indoor room, a spot light on a pillar, twelve coloured point lamps), what was changed
for each, and what was not. Plain steps to see every change are in `docs/try_it.md` (entry `7lit`).

## What each file was written from (CLAUDE.md rule 19a)

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

## Existing / Gap

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

### Looked at and left as is (already there)

- Hemisphere and sky-colour ambient (Ambient Mode: Flat, Hemisphere, Sky colors) already follows the sky (`SceneAmbientColors`), also for
  the procedural sky.
- SSAO exists on OpenGL and Direct3D 11 (not in the software renderer's scene chain); baked lighting with ambient occlusion exists (`Bake Lighting`).
- Spot, point and area shadows exist (16 tiles). Flicker, Kelvin temperature and inverse-square falloff exist.

### Not done

- **Reflection probes and box-projected environment capture**: not built. Reflective materials still reflect the sky gradient only.
- **Light probes** and a software SSAO: not built.
- **GPU versions** of Soft Edges, masks, rectangle panels and the soft light shoulder: not written. OpenGL and Direct3D 11 draw an Area
  light as a soft point light and ignore masks. The Light Inspector says so (`RenderFeature::LightShapes`).
- The cascade fix, slope bias and smoothstep cone **were** changed in the OpenGL, Direct3D 11 and Direct3D 12 code but **never run on a GPU** here.

