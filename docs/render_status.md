# Render status: our own renderers, honestly

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

## The table

| Feature | Software | OpenGL | Direct3D 11 | Direct3D 12 | Proof (file: function) |
|---|---|---|---|---|---|
| Lit materials (lights, texture, normal, gloss, AO maps) | Partial | Done | Done | Done | Software: per-pixel when Per-pixel lighting is on, or while a shadow map exists (`ShadowsUsePerPixel`), else per vertex; `Raster::TrianglePhong`; GL: `kFrag`; D3D11: `PSMain` in `kHLSL`; D3D12: same HLSL copy (`kHLSL12`) |
| Sun shadows | Done | Done | Done | Done | Software: `RenderShadowMap` + `ShadowFactor`, drawn by default (the per-pixel path is chosen automatically while a shadow map exists); GL/D3D11: `PlanShadows` (`ShadowSetup.hpp`); D3D12: `ComputeDirectionalShadowVP` |
| Sun shadow cascades | Done | Done | Done | Missing | Software: `ShadowCascades()`; GL/D3D11: `PlanShadows`; D3D12 draws one map and reads only `ShadowDistance` |
| Spot, point and area light shadows | Done | Done | Done | Missing | Software: `RenderLocalShadowMaps` + `LocalShadowFactor` (same automatic per-pixel path); GL: `localShadow` in `kFrag`; D3D11: `localShadow` in `kHLSL`; D3D12: `lsh = 1.0` for every non-sun light |
| Ambient occlusion (SSAO) | Partial | Done | Partial | Missing | Software: `ApplySSAO` (process switch only, the scene's SSAO switch is not read; a different algorithm); GL: `kSsaoFrag`, `computeAo`; D3D11: `PSAo` pass added in this change (UNVERIFIED); D3D12: no code |
| Tonemap and exposure | Done | Done | Done | Missing | Software: `ApplyScenePost` (scene exposure, curve and tonemap switch, plus the process switch); GL: `kUberFrag`; D3D11: `PSUber`; D3D12: none |
| Bloom | Done | Done | Done | Missing | Software: `ApplyBloom`, run when the scene's Bloom or the process switch is on (the scene's threshold is clamped to 0.95: no HDR); GL: `kBrightFrag` + `kBlurFrag`; D3D11: `PSDown` + `PSBlur`; D3D12: none |
| Vignette | Done | Done | Done | Partial | GL/D3D11: in the uber pass, with smoothness. Software: `ApplyScenePost`, same maths and smoothness, sets `PostVignetteHandled` so the host skips its overlay. D3D12: the host paints the amount; smoothness is ignored |
| Colour grading | Done | Done | Done | Missing | Software: `ApplyScenePost` (temperature, tint, saturation, contrast, lift/gamma/gain) and `ApplyColorGrade` (process switch); GL: `kUberFrag`; D3D11: `PSUber`; D3D12: none |
| Depth of field | Missing | Done | Partial | Missing | GL: `kDofFrag`; D3D11: `PSDof` (shared maths in `DofMath.hpp`, compile-checked, UNVERIFIED on a GPU); the others: no code |
| Anti-aliasing (FXAA) | Done | Done | Done | Missing | Software: `ApplyFXAA` (scene's FXAA switch or the process switch; a different, cheaper algorithm); GL: `kFxaaFrag`; D3D11: `PSFxaa`; D3D12: none |
| MSAA | Missing | Done | Done | Done | GL: `EnsureTargets(samples)`; D3D11: `EnsureTargets` clamps to what the device supports; D3D12: `Impl::msaa` picks up to 4x. Software has no MSAA |
| Fog | Done | Done | Done | Done | Software: `FogFactor` in `RenderMeshes`; GL and D3D11/D3D12 HLSL: same maths in the mesh shader |
| Sky | Partial | Done | Done | Partial | GL: `kSkyFrag`; D3D11: `PSSky`; Software and D3D12: a plain gradient from `EffectiveSkyColors` (no disc, halo, clouds, stars) |
| Reflections (sky gradient) | Partial | Done | Done | Done | Software: `EnvSky()` in `TrianglePhong` (per-pixel path); GL/D3D11/D3D12: `uSky` in the mesh shader. Not real reflection probes anywhere |
| GPU skinning | Missing | Done | Done | Missing | GL/D3D11: `SupportsGpuSkinning`; D3D12 and software skin on the CPU (`player/main.cpp`: `if (d3d12Ready) gpuSkin = false`) |
| Particles, lines, trails | Partial | Partial | Partial | Partial | All four: the host draws them as a CPU overlay after the frame. Only software frames give them depth occlusion (the GPU renderers do not read depth back) |
| Presentation | Done | Partial | Partial | Partial | Software draws straight into the CPU frame. The three GPU renderers all read the frame back to the CPU (`RenderToPixels`). D3D11 can render into the editor's own texture on Windows (`RenderToTexture`, UNVERIFIED). No swap-chain presentation anywhere |

## Notes that matter

- **Software shadows now work by default.** Shadows are only sampled on the per-pixel lighting path, and `PerPixelLighting()` is still off by default. So a new switch, `ShadowsUsePerPixel()` (default **on**), makes a mesh that receives shadows take the per-pixel path while the frame has a shadow map (a sun or a spot/point light that casts, with Shadows on). With no shadow-casting light, nothing changes and the cheap path stays. To go back: set `ShadowsUsePerPixel()` to false (the per-vertex frame, no shadows) or turn Shadows off. I picked this over drawing shadows on the vertex path because vertex shading cannot show a shadow edge smaller than a triangle (a big floor would be wrong), and the per-pixel code is already tested. Cost, measured here (4 core CPU shared with other jobs, 640x360, 41 meshes, one sun shadow, `tests/test_software_default_shadows.cpp`): about 38 to 42 ms per frame against 31 ms per vertex without shadows (1.2 to 1.3 times). Not measured: a big imported model, or a weak CPU.
- **Software now reads the scene's Post-processing section** for the parts that are cheap on a CPU: exposure, the tonemap curve (None, ACES, Filmic) and the scene's Tonemap switch, colour grading (temperature, tint, saturation, contrast, lift, gamma, gain), the vignette with its smoothness, Bloom and FXAA (`PlanScenePost`, `ApplyScenePost`). Fog was already read. The frame is 8-bit and shaded in display space, so these curves act on 0..1 values and will not match the GPU pixel for pixel (no HDR, no linear lighting). **Still not read by software:** the scene's Ambient Occlusion switch (only the process switch works) and Depth of field. Depth of field was **not attempted**: it needs a per-pixel depth blur and the software depth buffer is held at the supersampled size; it is left for a later change.
- **Ambient occlusion has no normals** (the same on OpenGL and the new Direct3D 11 pass): it compares depths only, so a floor seen at a slant darkens a little by itself. The unit test measures about 0.72 on open floor against 0.69 in a box crease. Radius and Intensity control how strong it looks.
- **Direct3D 11 is now level with OpenGL on ambient occlusion, on paper.** The pass was added in this change. It compiles under MinGW. It has never run on a GPU. Treat it as UNVERIFIED until someone with Windows looks at a frame. It runs only when the post chain runs and an Ambient Occlusion switch is on (the scene's post.ssao, or the Build Settings switch).
- **Direct3D 12 is a preview.** No post-processing, one sun shadow map, no spot or point shadows, CPU skinning. Build Settings now says so.
- **Depth of field** is OpenGL, plus a new Direct3D 11 pass (`DofMath.hpp`) that compiles under MinGW and has never run on a GPU. It reuses the ambient occlusion depth image, so turning it on without ambient occlusion still runs the half-resolution depth pass. Software depth of field was skipped on purpose (see above).
- **Every GPU frame is read back** to the CPU. That is a cost per frame. The profiler's "GPU ms" is CPU wall time.

## Who is behind on what

- Software is behind OpenGL on: depth of field, the scene's SSAO switch, MSAA, GPU skinning, the sky disc and clouds. It is ahead on one thing: it is the only one with automated pixel tests.
- OpenGL is the reference for the GPU side. It misses nothing in the table except real swap-chain presentation.
- Direct3D 11 is level with OpenGL on paper (its new ambient occlusion and depth of field passes are unrun).
- Direct3D 12 is behind on almost everything after the mesh pass. Do not ship a game on it expecting the D3D11 look.

## What was done in this change, and how it was checked

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

## Why these two quality settings

I picked Bloom Softness and Ambient Occlusion Power from the candidate list because both are pure maths on a number the shader already has (a pixel's brightness, the occlusion value). That means the whole formula can be unit tested on the CPU, and with the default (softness 0, power 1) the output is mathematically the same as before, so no saved scene changes. Auto-exposure needs a GPU brightness read-back, shadow cascade blending changes the shadow sampling loop on both backends, and contact shadows need a new depth march and a light-direction input: all bigger, and harder to test without a GPU. They are still open (see below).

## What was done in the second render change

| Change | Checked how | Not checked |
|---|---|---|
| Software shadows on by default (`ShadowsUsePerPixel`) | `tests/test_software_default_shadows.cpp`: a default frame has the shadow, the switch gives back the old frame bit for bit, a mesh that does not receive shadows keeps the cheap path, a timing line | Big scenes, slow CPUs |
| Software reads the scene post (`ApplyScenePost`) | `tests/test_software_scene_post.cpp`: neutral scene equals the old tonemap pixel for pixel; exposure, curves, grading, vignette coverage, bloom and FXAA switches through `RenderMeshes` | Visual match to the GPU frame |
| Post-chain HLSL moved to `D3DPostHLSL.hpp` (text identical to what D3D11 had; D3D11 now includes it) | The literals were compared by script before and after; `tests/test_d3d_post_hlsl.cpp`; MinGW `-fsyntax-only` on D3D11 and D3D12 | Not compiled by `D3DCompile`, not run: UNVERIFIED |

## Direct3D 12 post chain: the plan (not started)

D3D12 draws the mesh pass only. To give it the D3D11 post look, in order:

1. **Shared shader text (done in this change).** `engine/include/okay/Render/D3DPostHLSL.hpp` holds `PSDown`, `PSBlur`, `PSUber` and `PSFxaa` as the macro `OKAY_POST_HLSL`. D3D11 uses it today. `SsaoMath.hpp` (`OKAY_SSAO_HLSL`) is already shared the same way. The vertex shader `VSQuad`, the `PSQuad` struct, `aces()`, the sampler and the constants are still in `kHLSL` in `D3D11Renderer.cpp`; `PSSky` (sky pass) is there too.
2. **Move the rest of the full-screen shader text** (`PSQuad`, `VSQuad`, the post constants and `aces`) into the same header, so D3D12 does not need a copy. Verify with a text test like `test_d3d_post_hlsl.cpp`.
3. **Root signature.** D3D12 needs a root signature for the quad passes: one descriptor table of three SRVs (t0 frame, t1 bloom A, t2 bloom C), one static clamp sampler, and a root constant buffer holding the same `uP0..uP2`, `uLiftAspect`, `uGainRadius`, `uGammaThresh`, `uTexelDir` layout as D3D11's constant buffer. Compile with shader model 5.1 (`ps_5_1`, as `kHLSL12` does). A register space is not needed.
4. **Targets.** A resolved single-sample HDR colour target (D3D12 already resolves MSAA), two half-resolution bloom targets and a quarter pair, a ping-pong pair, and an output RGBA8 target. These mirror `EnsurePostTargets` in D3D11. Transition barriers between every pass are the new work compared with D3D11.
5. **Frame wiring.** Call the chain only when `PostActive(rs)` is true, copy `Impl` pass order from `D3D11Renderer.cpp` (bright pass, blur x2, quarter, blur x2, uber, FXAA), set `PostVignetteHandled()`, and read back the output target.
6. **Then** `RendererFeatures.hpp` rows for D3D12 can change to Yes, with the test and this page updated. That must wait for a real Windows GPU run: until a frame is looked at, the rows stay No.

What cannot be done here: compile the HLSL (`D3DCompile` is Windows only), run D3D12, or check barriers. Only `x86_64-w64-mingw32-g++ -fsyntax-only` is possible.

## Next three renderer items

1. Put the new D3D11 ambient occlusion and depth of field in front of a real Windows GPU and fix what the first frame shows.
2. Direct3D 12 post chain: steps 2 to 6 of the plan above, or park D3D12 for good and say so.
3. Software: the scene's Ambient Occlusion switch, and a cheap depth of field (a depth-driven blur on the downsampled frame).
4. Still open on GL and D3D11, in order of value: shadow cascade blending (visible seam between cascades), auto-exposure, screen-space contact shadows. The software renderer and Direct3D 12 ignore Bloom Softness and AO Power.
