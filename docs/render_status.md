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
| Lit materials (lights, texture, normal, gloss, AO maps) | Partial | Done | Done | Done | Software: per-pixel only when Per-pixel lighting is on, `Raster::TrianglePhong`; GL: `kFrag`; D3D11: `PSMain` in `kHLSL`; D3D12: same HLSL copy (`kHLSL12`) |
| Sun shadows | Partial | Done | Done | Done | Software: `RenderShadowMap` + `ShadowFactor`, drawn on the per-pixel path only; GL/D3D11: `PlanShadows` (`ShadowSetup.hpp`); D3D12: `ComputeDirectionalShadowVP` |
| Sun shadow cascades | Done | Done | Done | Missing | Software: `ShadowCascades()`; GL/D3D11: `PlanShadows`; D3D12 draws one map and reads only `ShadowDistance` |
| Spot, point and area light shadows | Partial | Done | Done | Missing | Software: `RenderLocalShadowMaps` + `LocalShadowFactor` (new, per-pixel path only); GL: `localShadow` in `kFrag`; D3D11: `localShadow` in `kHLSL`; D3D12: `lsh = 1.0` for every non-sun light |
| Ambient occlusion (SSAO) | Partial | Done | Partial | Missing | Software: `ApplySSAO` (process switch only, a different algorithm); GL: `kSsaoFrag`, `computeAo`; D3D11: `PSAo` pass added in this change (UNVERIFIED); D3D12: no code |
| Tonemap and exposure | Partial | Done | Done | Missing | Software: `ApplyToneMap` (process switch only); GL: `kUberFrag`; D3D11: `PSUber`; D3D12: none |
| Bloom | Partial | Done | Done | Missing | Software: `ApplyBloom` (process switch only); GL: `kBrightFrag` + `kBlurFrag`; D3D11: `PSDown` + `PSBlur`; D3D12: none |
| Vignette | Partial | Done | Done | Partial | GL/D3D11: in the uber pass, with smoothness. Software and D3D12: the host (editor or player) paints the amount; smoothness is ignored (`PostVignetteHandled`) |
| Colour grading | Partial | Done | Done | Missing | Software: `ApplyColorGrade` (process switch only); GL: `kUberFrag`; D3D11: `PSUber`; D3D12: none |
| Depth of field | Missing | Done | Partial | Missing | GL: `kDofFrag`; D3D11: `PSDof` (shared maths in `DofMath.hpp`, compile-checked, UNVERIFIED on a GPU); the others: no code |
| Anti-aliasing (FXAA) | Partial | Done | Done | Missing | Software: `ApplyFXAA` (process switch); GL: `kFxaaFrag`; D3D11: `PSFxaa`; D3D12: none |
| MSAA | Missing | Done | Done | Done | GL: `EnsureTargets(samples)`; D3D11: `EnsureTargets` clamps to what the device supports; D3D12: `Impl::msaa` picks up to 4x. Software has no MSAA |
| Fog | Done | Done | Done | Done | Software: `FogFactor` in `RenderMeshes`; GL and D3D11/D3D12 HLSL: same maths in the mesh shader |
| Sky | Partial | Done | Done | Partial | GL: `kSkyFrag`; D3D11: `PSSky`; Software and D3D12: a plain gradient from `EffectiveSkyColors` (no disc, halo, clouds, stars) |
| Reflections (sky gradient) | Partial | Done | Done | Done | Software: `EnvSky()` in `TrianglePhong` (per-pixel path); GL/D3D11/D3D12: `uSky` in the mesh shader. Not real reflection probes anywhere |
| GPU skinning | Missing | Done | Done | Missing | GL/D3D11: `SupportsGpuSkinning`; D3D12 and software skin on the CPU (`player/main.cpp`: `if (d3d12Ready) gpuSkin = false`) |
| Particles, lines, trails | Partial | Partial | Partial | Partial | All four: the host draws them as a CPU overlay after the frame. Only software frames give them depth occlusion (the GPU renderers do not read depth back) |
| Presentation | Done | Partial | Partial | Partial | Software draws straight into the CPU frame. The three GPU renderers all read the frame back to the CPU (`RenderToPixels`). D3D11 can render into the editor's own texture on Windows (`RenderToTexture`, UNVERIFIED). No swap-chain presentation anywhere |

## Notes that matter

- **Software renderer, per-pixel only.** Sun shadows, spot and point shadows, reflections and rim light are all drawn only on the per-pixel lighting path. That path is **off by default** (`PerPixelLighting()` is false). With it off, the software renderer shades per vertex and draws no shadows at all. This is why the table says Partial for the software shadows.
- **Software renderer ignores the scene's post settings.** Its bloom, tonemap, grading and FXAA read the process-wide switches (Build Settings and the Rendering panel) and not the scene's Post-processing section.
- **Ambient occlusion has no normals** (the same on OpenGL and the new Direct3D 11 pass): it compares depths only, so a floor seen at a slant darkens a little by itself. The unit test measures about 0.72 on open floor against 0.69 in a box crease. Radius and Intensity control how strong it looks.
- **Direct3D 11 is now level with OpenGL on ambient occlusion, on paper.** The pass was added in this change. It compiles under MinGW. It has never run on a GPU. Treat it as UNVERIFIED until someone with Windows looks at a frame. It runs only when the post chain runs and an Ambient Occlusion switch is on (the scene's post.ssao, or the Build Settings switch).
- **Direct3D 12 is a preview.** No post-processing, one sun shadow map, no spot or point shadows, CPU skinning. Build Settings now says so.
- **Depth of field** is OpenGL, plus a new Direct3D 11 pass (`DofMath.hpp`) that compiles under MinGW and has never run on a GPU. It reuses the ambient occlusion depth image, so turning it on without ambient occlusion still runs the half-resolution depth pass.
- **Every GPU frame is read back** to the CPU. That is a cost per frame. The profiler's "GPU ms" is CPU wall time.

## Who is behind on what

- Software is behind OpenGL on: depth of field, MSAA, GPU skinning, the scene's post settings, the sky disc and clouds. It is ahead on one thing: it is the only one with automated pixel tests.
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
| Direct3D 11 ambient occlusion pass | Compile-checked with `x86_64-w64-mingw32-g++ -fsyntax-only` | Not run. No Windows, no GPU. UNVERIFIED |
| Direct3D 12 honest labels (Build Settings, `RendererFeatures.hpp`, header comment) | `tests/test_renderer_features.cpp` | n/a |

## Next three renderer items

1. Turn the software renderer's per-pixel path on by default (or make shadows work on the vertex path), then read the scene's Post-processing section so it matches the GPU renderers. This is the biggest gap for people who see the software frame.
2. Put the new D3D11 ambient occlusion in front of a real Windows GPU and fix what the first frame shows. Then do the same review for the D3D11 depth of field pass.
3. Direct3D 12 post chain: share the D3D11 post shaders and targets instead of copying them a second time, or park D3D12 for good and say so.
