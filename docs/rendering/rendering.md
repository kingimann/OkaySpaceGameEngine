# Rendering

> **What this is:** How a frame is drawn: post-processing, sky, fog, weather, and the honest status of each renderer (software, OpenGL, Direct3D 11, Direct3D 12).
>
> **Status:** combined on 2026-11-20 from 2 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
>
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Rendering: post-processing, sky, fog and weather](#rendering-post-processing-sky-fog-and-weather) (was `docs/rendering.md`)
- [Render status: our own renderers, honestly](#render-status-our-own-renderers-honestly) (was `docs/render_status.md`)

## Rendering: post-processing, sky, fog and weather

*Was `docs/rendering.md` until 2026-11-20.*

The look of a scene beyond its lights lives in the **Lighting** panel (*View →
Panels → Lighting*) in four sections: **Post-processing**, **Sky**, **Fog** and
**Weather**. Everything there is saved with the scene and previews live in the
Scene and Game views. With every setting at its default the frame is exactly
what the engine drew before these existed: no post pass runs, the sky is the
authored gradient, there is no fog and no weather.

Scripts change the same values at runtime (see [Scripting](#scripting)).

### Look presets

The **Look Preset** combo at the top of *Post-processing* fills all four
sections at once: **Default**, **Cinematic**, **Vivid**, **Noir**, **Sunset**,
**Night**, **Foggy morning** and **Storm**. Sunset, Night, Foggy morning and
Storm also aim the scene's first directional light at the sun height the look
needs (low, below the horizon, ...). **Reset** puts the four sections back to
their defaults and keeps the sky gradient colours. Both go through Undo.

### Post-processing

Not every switch is drawn on every renderer. The Lighting window, Build
Settings and the Light Inspector put a grey note under a switch that the
renderer in front of you ignores, so a dead control is labelled instead of
silent.

What the scene's Post-processing section actually draws:

| | OpenGL | Direct3D 11 | Direct3D 12 | Software |
|---|---|---|---|---|
| Linear lighting, exposure, tonemap, bloom, colour grading, FXAA | yes | yes | no | exposure, tonemap, bloom, grading and FXAA yes (8-bit, no linear lighting) |
| Vignette amount | yes | yes | yes | yes |
| Ambient occlusion in this section | yes | yes (new, unrun on a GPU) | no | yes (its own CPU algorithm) |
| Depth of field | yes | yes (new, unrun on a GPU) | no | yes (perspective cameras only) |

Fog is drawn on all four. The vignette amount is an overlay when the post chain
itself does not run, so it still shows on Direct3D 12 and the software renderer.
Sun shadows are drawn on all four. Spot and point light shadows are drawn on
OpenGL, Direct3D 11 and the software renderer, and not on Direct3D 12; the Light
Inspector says so under Cast Shadows. The software renderer draws shadows (sun,
spot and point) by default (a mesh that receives shadows is lit per pixel while a shadow map exists; `ShadowsUsePerPixel()` = false gives the faster per-vertex frame with no shadows). The separate Ambient Occlusion
switch in Build Settings is drawn by OpenGL and the software renderer, and by
Direct3D 11 while the post chain runs. The Direct3D 11 pass is new and has not
been run on a GPU. Direct3D 12 is a preview with no post-processing. For the
full list, see [render_status.md](rendering.md#render-status-our-own-renderers-honestly).

The *Rendering (Lighting & Post FX)* section below these is the editor's own
process-wide switches (shadow quality, the software renderer's effects). It is
not the same list as the scene's Post-processing section, and it is not saved
with the scene.

When an effect that this renderer draws is on, the scene renders into a float (HDR) target, so
highlights above white survive for bloom and the tonemap, and the chain runs:
SSAO → bloom → exposure / tonemap / grading / vignette → depth of field → FXAA.

| Setting | What it does |
|---------|--------------|
| **Filmic Rendering** | The linear-lighting pipeline (on by default). Off = the legacy flat response, and no tonemap curve. |
| **Exposure** | Scene brightness multiplier, 1 = as lit. |
| **Tonemap** | How highlights roll off: **ACES** (the default, punchy), **Filmic** (a softer shoulder), **None** (clip). |
| **Bloom** | Glow around bright pixels: **Threshold** (brightness that starts to glow; with Filmic Rendering 1 = white), **Intensity**, **Radius**, **Softness** (0 = the hard cut, as before; higher fades the glow in around the threshold instead of popping on). Blurred at half and quarter resolution, so it stays cheap at 1080p. |
| **Colour Grading** | **Temperature** (cool ↔ warm) and **Tint** white balance, **Saturation**, **Contrast**, and per-channel **Lift** (blacks), **Gamma** (mid-tones), **Gain** (whites). |
| **Vignette** | Darkens the edges and corners, with a **Smoothness** for how far in it reaches. 0 = off. The post chain draws it when it runs; otherwise it is an overlay that looks the same on every renderer. |
| **Anti-aliasing (FXAA)** | A luma-driven edge filter on the finished frame. |
| **Ambient Occlusion (SSAO)** | Darkens creases and contact points: **Radius** (world units), **Intensity** and **Power** (1 = as before; higher keeps only deep creases dark, lower spreads a lighter shadow). OpenGL, and Direct3D 11 (new, unrun on a GPU). |
| **Depth of Field** | Blurs outside the **Focus Range** either side of the **Focus Distance**. OpenGL, the software renderer (perspective cameras), and Direct3D 11 (new, unrun on a GPU). Off unless the switch is on. A pixel nothing covers stays transparent, so the sky behind the frame shows through. |

A lightning strike (see Weather) spikes the exposure for a few frames, so a
storm blinks even with every other effect off.

### Sky

**Sky Mode** chooses between:

- **Colour** — the gradient in *Environment (Sky & Light)*: Sky Top / Horizon /
  Sky Bottom, the horizon position, the screen-space sun disc and star field.
  This is what every scene had before.
- **Procedural** — a sky the GPU renderers draw behind everything, following the
  camera's rotation and field of view. Its colours come from the **sun's
  height**: the scene's directional light is the sun (a **Day/Night Cycle**
  component rotates it, so the sky turns from day blue through dusk orange to a
  dark night with stars as the clock runs). It has a **Sun Disc** (angular
  radius in degrees) with a **Sun Halo**, **Horizon Haze**, stars once the sun
  has set (**Star Density** / **Star Brightness**) and a cloud layer: **Cloud
  Cover** (clear → overcast), **Cloud Density**, **Cloud Speed** and **Cloud
  Scale**. Clouds are lit by the sun and drift with the scene's wind.

Reflective materials and the *Sky colors* ambient mode take the procedural
sky's colours too. OpenGL and Direct3D 11 draw the sun disc, halo, clouds and
stars. Direct3D 12 and the software renderer show the sun's colours as a plain
gradient (no disc, stars or clouds). The Lighting window says so under Sky
Mode. In the Scene view the *Debug View → Sky gradient* toggle hides the
procedural sky as well.

### Fog

**Enable Fog**, then pick a **Fog Mode**:

- **Linear** — fades from **Fog Start** to **Fog End** (world units from the camera).
- **Exponential** — thickens with distance by **Density**.
- **Height** — a layer that thins above **Height** by **Falloff**: a valley
  mist, a low morning fog. Looking down into it fills with fog; looking up out
  of it clears.

**Match Sky Colour** takes the fog colour from the sky's horizon (the gradient's
in Colour mode, the sun's in Procedural mode), so distant geometry dissolves
into the sky instead of a flat band; otherwise set **Fog Color**. Fog is applied
in the scene shaders on every renderer, including the software one.

### Weather

**Weather Preset**: **None**, **Rain**, **Snow** or **Storm**, at an
**Intensity** (0–1) that scales how much falls. The precipitation is built from
the particle system (the *Rain* and *Snow* looks) on a transient
"Weather (runtime)" object that follows the active camera, so the player is
always under it; it is never saved. A preset change cross-fades over **Fade**
seconds, and "None" takes the rig down once the last drops have landed. In edit
mode the preview follows the Scene camera.

**Storm** is heavy rain plus gusts on top of the wind and lightning: each strike
is a brief exposure flash, and **Thunder Sound** (a clip from the Project
panel) plays a moment later.

**Wind** is a global vector (world units per second). Every particle system
drifts with it (a system's **Wind Influence** scales that; 1 = the wind's
speed, 0 = ignore it) and a **Water** surface's swell quickens and runs
downwind. A scene with no wind leaves both exactly as authored.

### Scripting

OkayScript builtins (also in [scripting.md](../scripting/scripting.md#okayscript-1)):

```cs
post_set("bloom", 1);              // any setting PostProcess.Set knows; 0 / 1 for switches
post_set("tonemap", "filmic");     // or 0 none, 1 ACES, 2 filmic
fog_set(true, "#c8d8e6", 20, 90);  // on, optional "#rrggbb" colour, start, end
fog_set(true, 10, 60, "height", 0.05);   // ... then a mode and its density
weather_set("storm", 0.8);         // none / rain / snow / storm, intensity
float hour = time_of_day();        // the scene's Day/Night Cycle; time_of_day(18.5) sets it
wind_set(8, 90);                   // the scene's wind: 8 m/s toward +X (docs/gameplay/wind.md); wind_gust(0.5, 0.2)
```

C#-style statics: `PostProcess.Set(name, value)` and `PostProcess.exposure`,
`Fog.Set(enabled, start, end)`, `Fog.SetColor(r, g, b)`, `Fog.SetMode(mode,
density)`, `Fog.enabled`, `Weather.Set(preset, intensity)`,
`Weather.SetWind(x, y, z)`, `Weather.preset` (see
[engine_api.md](../engine_api.md)).

Visual-script nodes: **Set Weather** (preset in the node, Intensity pin),
**Set Fog** (On, Start, End pins) and **Set Post Effect** (effect name in the
node, Value pin), plus the engine-API nodes made from the statics above.

### Scene file

The settings are extra header records of the scene file, written only when
they differ from the defaults: `fogmode`, `skyproc`, `post` and `weather`
lines after `rendersettings`, and a `particleswind` line on a particle system
that doesn't take the full wind. A scene that never touched them is written
exactly as before and loads with everything off.

## Render status: our own renderers, honestly

*Was `docs/render_status.md` until 2026-11-20.*

This page says what each of our four renderers really draws today. It was read off the code, not guessed. Each row names the file and the function that proves it.

The four renderers:

- **Software** is `engine/include/okay/Render/SoftwareRenderer.hpp`. It runs on the CPU. It is the reference, and the only one we can test headless on Linux.
- **OpenGL** is `engine/src/Render/GLRenderer.cpp`. It is the most complete GPU renderer.
- **Direct3D 11** is `engine/src/Render/D3D11Renderer.cpp`. It is the default on Windows.
- **Direct3D 12** is `engine/src/Render/D3D12Renderer.cpp`. It is a preview: opt in only.

How to read the table:

- **Done**: it is in the code and the backend draws it.
- **Partial**: some of it is drawn, or only under a condition (the note says which).
- **Missing**: the code is not there.

"Done" does not mean "run on a GPU". We have no GPU and no Windows machine in this environment. Only the software renderer is run by tests here. The GPU files are compile-checked (Direct3D with the MinGW cross compiler in CI) and read, nothing more. Rows that rest on unrun GPU code say UNVERIFIED in the notes below.

The machine-readable version of the "ignored switch" part is `engine/include/okay/Render/RendererFeatures.hpp`. The editor reads it to show grey notes under switches a backend ignores.

### The table

| Feature | Software | OpenGL | Direct3D 11 | Direct3D 12 | Proof (file: function) |
|---|---|---|---|---|---|
| Lit materials (lights, texture, normal, gloss, AO maps) | Partial | Done | Done | Done | Software: per-pixel when Per-pixel lighting is on, or while a shadow map exists (`ShadowsUsePerPixel`), else per vertex; `Raster::TrianglePhong`; GL: `kFrag`; D3D11: `PSMain` in `kHLSL`; D3D12: same HLSL copy (`kHLSL12`) |
| Sun shadows | Done | Done | Done | Done | Software: `RenderShadowMap` + `ShadowFactor`, drawn by default (the per-pixel path is chosen automatically while a shadow map exists); GL/D3D11: `PlanShadows` (`ShadowSetup.hpp`); D3D12: `ComputeDirectionalShadowVP` |
| Sun shadow cascades | Done | Done | Done | Missing | Software: `ShadowCascades()`; GL/D3D11: `PlanShadows`; D3D12 draws one map and reads only `ShadowDistance` |
| Spot, point and area light shadows | Done | Done | Done | Missing | Software: `RenderLocalShadowMaps` + `LocalShadowFactor` (same automatic per-pixel path); GL: `localShadow` in `kFrag`; D3D11: `localShadow` in `kHLSL`; D3D12: `lsh = 1.0` for every non-sun light |
| Ambient occlusion (SSAO) | Partial | Done | Partial | Missing | Software: `ApplySSAO` (the process switch, or the scene's SSAO switch with its radius, intensity and AO Power; a different algorithm); GL: `kSsaoFrag`, `computeAo`; D3D11: `PSAo` pass added in this change (UNVERIFIED); D3D12: no code |
| Tonemap and exposure | Done | Done | Done | Missing | Software: `ApplyScenePost` (scene exposure, curve and tonemap switch, plus the process switch); GL: `kUberFrag`; D3D11: `PSUber`; D3D12: none |
| Bloom | Done | Done | Done | Missing | Software: `ApplyBloom`, run when the scene's Bloom or the process switch is on (the scene's threshold is clamped to 0.95: no HDR); GL: `kBrightFrag` + `kBlurFrag`; D3D11: `PSDown` + `PSBlur`; D3D12: none |
| Vignette | Done | Done | Done | Partial | GL/D3D11: in the uber pass, with smoothness. Software: `ApplyScenePost`, same maths and smoothness, sets `PostVignetteHandled` so the host skips its overlay. D3D12: the host paints the amount; smoothness is ignored |
| Colour grading | Done | Done | Done | Missing | Software: `ApplyScenePost` (temperature, tint, saturation, contrast, lift/gamma/gain) and `ApplyColorGrade` (process switch); GL: `kUberFrag`; D3D11: `PSUber`; D3D12: none |
| Depth of field | Done | Done | Partial | Missing | Software: `ApplySceneDof` (perspective cameras; own 9-tap binomial blur, same focus maths, `tests/test_software_scene_post.cpp`); GL: `kDofFrag` with the premultiplied `kDofCopyFrag` / `kDofBlurFrag` (`tests/test_gl_post_alpha.cpp` on Mesa under Xvfb); D3D11: `PSDof`, `PSDofCopy`, `PSDofBlur` (shared maths in `DofMath.hpp`, compile-checked, UNVERIFIED on a GPU); D3D12: no code |
| Anti-aliasing (FXAA) | Done | Done | Done | Missing | Software: `ApplyFXAA` (scene's FXAA switch or the process switch; a different, cheaper algorithm); GL: `kFxaaFrag`; D3D11: `PSFxaa`; D3D12: none |
| MSAA | Missing | Done | Done | Done | GL: `EnsureTargets(samples)`; D3D11: `EnsureTargets` clamps to what the device supports; D3D12: `Impl::msaa` picks up to 4x. Software has no MSAA |
| Fog | Done | Done | Done | Done | Software: `FogFactor` in `RenderMeshes`; GL and D3D11/D3D12 HLSL: same maths in the mesh shader |
| Sky | Partial | Done | Done | Partial | GL: `kSkyFrag`; D3D11: `PSSky`; Software and D3D12: a plain gradient from `EffectiveSkyColors` (no disc, halo, clouds, stars) |
| Reflections (sky gradient) | Partial | Done | Done | Done | Software: `EnvSky()` in `TrianglePhong` (per-pixel path); GL/D3D11/D3D12: `uSky` in the mesh shader. Not real reflection probes anywhere |
| Custom shaders (`.okayshader`) | Done | Done | Partial | Missing | Software: `oshader::RunCpu` in `TrianglePhong`; GL: a main-shader variant per file (`EnsureCustomProgram`); D3D11: a `PSMain` variant (written, UNVERIFIED on Windows); D3D12: drawn as Standard. See `docs/rendering/shaders.md` |
| GPU skinning | Missing | Done | Done | Missing | GL/D3D11: `SupportsGpuSkinning`; D3D12 and software skin on the CPU (`player/main.cpp`: `if (d3d12Ready) gpuSkin = false`) |
| Particles, lines, trails | Partial | Partial | Partial | Partial | All four: the host draws them as a CPU overlay after the frame. Only software frames give them depth occlusion (the GPU renderers do not read depth back) |
| Presentation | Done | Partial | Partial | Partial | Software draws straight into the CPU frame. The three GPU renderers all read the frame back to the CPU (`RenderToPixels`). D3D11 can render into the editor's own texture on Windows (`RenderToTexture`, UNVERIFIED). No swap-chain presentation anywhere |

### Notes that matter

- **Software shadows now work by default.** Shadows are only sampled on the per-pixel lighting path, and `PerPixelLighting()` is still off by default. So a new switch, `ShadowsUsePerPixel()` (default **on**), makes a mesh that receives shadows take the per-pixel path while the frame has a shadow map (a sun or a spot/point light that casts, with Shadows on). With no shadow-casting light, nothing changes and the cheap path stays. To go back: set `ShadowsUsePerPixel()` to false (the per-vertex frame, no shadows) or turn Shadows off. I picked this over drawing shadows on the vertex path because vertex shading cannot show a shadow edge smaller than a triangle (a big floor would be wrong), and the per-pixel code is already tested. Cost, measured here (4 core CPU shared with other jobs, 640x360, 41 meshes, one sun shadow, `tests/test_software_default_shadows.cpp`): about 38 to 42 ms per frame against 31 ms per vertex without shadows (1.2 to 1.3 times). Not measured: a big imported model, or a weak CPU.
- **Software now reads the scene's Post-processing section** for the parts that are cheap on a CPU: exposure, the tonemap curve (None, ACES, Filmic) and the scene's Tonemap switch, colour grading (temperature, tint, saturation, contrast, lift, gamma, gain), the vignette with its smoothness, Bloom and FXAA (`PlanScenePost`, `ApplyScenePost`). Fog was already read. The frame is 8-bit and shaded in display space, so these curves act on 0..1 values and will not match the GPU pixel for pixel (no HDR, no linear lighting). The scene's Ambient Occlusion switch is read too (lighting change #1022). **Depth of field** was added later (2026-11, `ApplySceneDof`): a half-resolution premultiplied copy, two rounds of a 9-tap binomial blur, mixed back by the same circle of confusion as the GPU (`DofMath.hpp`). The distance is the straight-line eye distance, like the GPU depth image: the W-buffer gives view depth and `DofRayScale` turns it into distance along the pixel's ray. It needs a perspective camera: an orthographic one has the same W everywhere, so the pass is skipped. It runs at the supersampled size when supersampling is on.
- **Ambient occlusion has no normals** (the same on OpenGL and the new Direct3D 11 pass): it compares depths only, so a floor seen at a slant darkens a little by itself. The unit test measures about 0.72 on open floor against 0.69 in a box crease. Radius and Intensity control how strong it looks.
- **Direct3D 11 is now level with OpenGL on ambient occlusion, on paper.** The pass was added in this change. It compiles under MinGW. It has never run on a GPU. Treat it as UNVERIFIED until someone with Windows looks at a frame. It runs only when the post chain runs and an Ambient Occlusion switch is on (the scene's post.ssao, or the Build Settings switch).
- **Direct3D 12 is a preview.** No post-processing, one sun shadow map, no spot or point shadows, CPU skinning. Build Settings now says so.
- **Depth of field** is OpenGL, the software renderer, plus a Direct3D 11 pass (`DofMath.hpp`) that compiles under MinGW and has never run on a GPU. On the GPU it reuses the ambient occlusion depth image, so turning it on without ambient occlusion still runs the half-resolution depth pass.
- **Fixed (2026-11): depth of field turned the background black on OpenGL and Direct3D 11 when the GPU sky pass is off.** The editor and the player render with a transparent clear. When the GPU does not draw the sky (a camera that clears to a solid colour, Sky off, an orthographic camera in the player, the Scene view's sky toggle off) they paint the background behind the frame. The blurred copy was written with alpha 1, so every out-of-focus empty pixel became an opaque black blur over that background. With the GPU sky pass on (the usual Game view) the sky is part of the frame and was not affected. The copy and the blur now carry the coverage premultiplied, and the mix is premultiplied: an empty pixel stays empty and a far object fades softly into the sky. OpenGL is checked by `tests/test_gl_post_alpha.cpp` (Mesa software GL under Xvfb; it failed before the fix with the empty corner at alpha 255, black). The Direct3D 11 change is the same maths, compile-checked only, UNVERIFIED.
- **Every GPU frame is read back** to the CPU. That is a cost per frame. The profiler's "GPU ms" is CPU wall time.

### Who is behind on what

- Software is behind OpenGL on: MSAA, GPU skinning, the sky disc and clouds, depth of field with an orthographic camera, Bloom Softness. It is ahead on one thing: it is the only one with automated pixel tests.
- OpenGL is the reference for the GPU side. It misses nothing in the table except real swap-chain presentation.
- Direct3D 11 is level with OpenGL on paper (its new ambient occlusion and depth of field passes are unrun).
- Direct3D 12 is behind on almost everything after the mesh pass. Do not ship a game on it expecting the D3D11 look.

### What was done in this change, and how it was checked

| Change | Checked how | Not checked |
|---|---|---|
| Software spot, point and area light shadows (`RenderLocalShadowMaps`, `LocalShadowFactor`) | `tests/test_software_local_shadows.cpp`: pixel probes and a rendered frame, run on Linux | Visual quality on a big scene, speed on a big scene |
| Shared view maths for local shadows (`LocalShadowMath.hpp`) used by the software renderer and the GPU planner | `tests/test_light_shadows.cpp` (the GPU planner tests) still pass | The GPU backends were not run |
| Shared SSAO maths (`SsaoMath.hpp`): constants, CPU reference, and the HLSL text | `tests/test_ssao_math.cpp`: CPU maths, a frame from the software renderer, and a text check that GL, D3D11 and the header agree | The HLSL has never been compiled by the real HLSL compiler (`D3DCompile`) |
| Shared depth of field maths (`DofMath.hpp`): constants, CPU reference, HLSL text | `tests/test_dof_math.cpp`: CPU maths, and a text check that GL, D3D11 and the header agree | The HLSL has never been compiled by `D3DCompile` |
| Direct3D 11 depth of field pass (`PSDof`) | Compile-checked with `x86_64-w64-mingw32-g++ -fsyntax-only`; not run. UNVERIFIED | Blur quality and focus on a real frame |
| Bloom Softness and Ambient Occlusion Power (`PostQualityMath.hpp`, `post.bloomSoftness`, `post.ssaoPower`, saved as a `postq` line, `post_set` names, inspector sliders) | `tests/test_post_quality_math.cpp` (CPU maths: softness 0 and power 1 equal today's output exactly; knee is continuous; text check that GL and D3D11 paste the same code), `tests/test_post_sky.cpp` (save round-trip, clamps, defaults write nothing); D3D11 compile-checked | GLSL never compiled here; HLSL never run through `D3DCompile`; unrun on a GPU. UNVERIFIED |
| Direct3D 11 ambient occlusion pass | Compile-checked with `x86_64-w64-mingw32-g++ -fsyntax-only` | Not run. No Windows, no GPU. UNVERIFIED |
| Direct3D 12 honest labels (Build Settings, `RendererFeatures.hpp`, header comment) | `tests/test_renderer_features.cpp` | n/a |

### Why these two quality settings

I picked Bloom Softness and Ambient Occlusion Power from the candidate list because both are pure maths on a number the shader already has (a pixel's brightness, the occlusion value). That means the whole formula can be unit tested on the CPU, and with the default (softness 0, power 1) the output is mathematically the same as before, so no saved scene changes. Auto-exposure needs a GPU brightness read-back, shadow cascade blending changes the shadow sampling loop on both backends, and contact shadows need a new depth march and a light-direction input: all bigger, and harder to test without a GPU. They are still open (see below).

### What was done in the second render change

| Change | Checked how | Not checked |
|---|---|---|
| Software shadows on by default (`ShadowsUsePerPixel`) | `tests/test_software_default_shadows.cpp`: a default frame has the shadow, the switch gives back the old frame bit for bit, a mesh that does not receive shadows keeps the cheap path, a timing line | Big scenes, slow CPUs |
| Software reads the scene post (`ApplyScenePost`) | `tests/test_software_scene_post.cpp`: neutral scene equals the old tonemap pixel for pixel; exposure, curves, grading, vignette coverage, bloom and FXAA switches through `RenderMeshes` | Visual match to the GPU frame |
| Post-chain HLSL moved to `D3DPostHLSL.hpp` (text identical to what D3D11 had; D3D11 now includes it) | The literals were compared by script before and after; `tests/test_d3d_post_hlsl.cpp`; MinGW `-fsyntax-only` on D3D11 and D3D12 | Not compiled by `D3DCompile`, not run: UNVERIFIED |

### Direct3D 12 post chain: the plan (not started)

D3D12 draws the mesh pass only. To give it the D3D11 post look, in order:

1. **Shared shader text (done in this change).** `engine/include/okay/Render/D3DPostHLSL.hpp` holds `PSDown`, `PSBlur`, `PSUber` and `PSFxaa` as the macro `OKAY_POST_HLSL`. D3D11 uses it today. `SsaoMath.hpp` (`OKAY_SSAO_HLSL`) is already shared the same way. The vertex shader `VSQuad`, the `PSQuad` struct, `aces()`, the sampler and the constants are still in `kHLSL` in `D3D11Renderer.cpp`; `PSSky` (sky pass) is there too.
2. **Move the rest of the full-screen shader text** (`PSQuad`, `VSQuad`, the post constants and `aces`) into the same header, so D3D12 does not need a copy. Verify with a text test like `test_d3d_post_hlsl.cpp`.
3. **Root signature.** D3D12 needs a root signature for the quad passes: one descriptor table of three SRVs (t0 frame, t1 bloom A, t2 bloom C), one static clamp sampler, and a root constant buffer holding the same `uP0..uP2`, `uLiftAspect`, `uGainRadius`, `uGammaThresh`, `uTexelDir` layout as D3D11's constant buffer. Compile with shader model 5.1 (`ps_5_1`, as `kHLSL12` does). A register space is not needed.
4. **Targets.** A resolved single-sample HDR colour target (D3D12 already resolves MSAA), two half-resolution bloom targets and a quarter pair, a ping-pong pair, and an output RGBA8 target. These mirror `EnsurePostTargets` in D3D11. Transition barriers between every pass are the new work compared with D3D11.
5. **Frame wiring.** Call the chain only when `PostActive(rs)` is true, copy `Impl` pass order from `D3D11Renderer.cpp` (bright pass, blur x2, quarter, blur x2, uber, FXAA), set `PostVignetteHandled()`, and read back the output target.
6. **Then** `RendererFeatures.hpp` rows for D3D12 can change to Yes, with the test and this page updated. That must wait for a real Windows GPU run: until a frame is looked at, the rows stay No.

What cannot be done here: compile the HLSL (`D3DCompile` is Windows only), run D3D12, or check barriers. Only `x86_64-w64-mingw32-g++ -fsyntax-only` is possible.

### Next three renderer items

1. Put the new D3D11 ambient occlusion and depth of field in front of a real Windows GPU and fix what the first frame shows.
2. Direct3D 12 post chain: steps 2 to 6 of the plan above, or park D3D12 for good and say so.
3. Software: the scene's Ambient Occlusion switch, and a cheap depth of field (a depth-driven blur on the downsampled frame).
4. Still open on GL and D3D11, in order of value: shadow cascade blending (visible seam between cascades), auto-exposure, screen-space contact shadows. Direct3D 12 ignores Bloom Softness and AO Power; the software renderer ignores Bloom Softness.
