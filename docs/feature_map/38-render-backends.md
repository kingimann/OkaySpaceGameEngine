# 38. Render backends & GPU

The things that actually put a 3D frame on screen: the OpenGL, Direct3D 11, Direct3D 12 and software renderers, the console renderer, the web path, the shader programs, GPU resources and the frame and present path in the editor and player.
Covers the backend files of `engine/include/okay/Render` and `engine/src/Render` (`GLRenderer`, `D3D11Renderer`, `D3D12Renderer`, `SoftwareRenderer`, `ConsoleRenderer`, `ShadowSetup`, `Renderer`), about 8k lines, plus the host code that calls them in `editor/main.cpp` and `player/main.cpp`.
Split out of 2.1 so each backend can be targeted ("fix shadows on Direct3D 11"). What the frame looks like (lights, sky, post effects, materials) stays in area 2. Nothing is open: #574 added GPU timing, render stats and a frame debugger as library code (not called by any renderer yet) and #575 made all four renderers skip children of an inactive parent.

## 38. Render backends & GPU

### 38.1 Renderer interface and backend selection
- **What:** How the engine talks to a renderer and how the editor and the player pick one for the machine they run on.
- **Parts:**
  - 38.1.1 Interface - `Render/Renderer` - `IRenderer` with five calls, implemented only by the console renderer (see 2.1.1)
  - 38.1.2 GPU contract - `Render/GLRenderer`, `Render/D3D11Renderer`, `Render/D3D12Renderer` - no common base class: each exposes `RenderToPixels(scene, viewProj, eye, w, h, samples, clear colour, ignore)` returning a w*h RGBA8 buffer owned by the renderer; the software equivalent is `RenderMeshesSS`
  - 38.1.3 Editor choice - `editor/main.cpp` (`Render3DTexture`) - GPU on by default: Direct3D 11 on Windows, else OpenGL, else software; three failed frames put it on software for 5 seconds before retrying
  - 38.1.4 Player choice - `player/main.cpp` - Direct3D 12 when opted in, then Direct3D 11, then OpenGL, then software; three failed frames switch to software for the rest of the run; `gpu=0` in the build config skips GPUs
  - 38.1.5 Quality presets - `Core/DisplaySettings` - Low/Medium/High (see 1.15)
- **Code:** `Render/Renderer`, `editor/main.cpp`, `player/main.cpp`
- **API:** `IRenderer`, `GLRenderer::RenderToPixels`, `D3D11Renderer::RenderToPixels`, `RenderMeshesSS`, Build Game "GPU renderer" and "Prefer Direct3D 12" options
- **Editor:** View > Rendering & Performance (a read-only "Renderer: <backend>" line and 3D Anti-aliasing 2x); Build Game > quality preset, GPU renderer and Prefer Direct3D 12
- **Docs:** `docs/rendering.md`, `docs/lighting.md` (which renderers do what)
- **Tests:** `software_renderer`
- **Status:** Works - read both selection chains; but the backends cannot be unit tested headless (no GL or D3D context), so only the software path and the planning maths have tests
- **Open:** no shared backend interface, so every feature is copied three times; no Vulkan or Metal backend; the editor retries the GPU after 5 seconds but the player gives up for the session after three failures; the editor never offers Direct3D 12
- **Related:** 2.1, 38.2, 38.3, 38.4, 38.5, 38.10, 1.15
- **Words:** renderer, backend, GPU, OpenGL, Direct3D, DirectX, D3D11, D3D12, software renderer, fallback, RenderToPixels, GPU renderer toggle
- **Seen as:** the 3D view is black or very slow; the editor says "GPU renderer failing; using the software renderer briefly"; the game looks different on another machine
- **Ask me like:** "Force the player to use OpenGL"; "Add a Vulkan backend behind the same RenderToPixels contract"

### 38.2 OpenGL backend
- **What:** The GPU renderer for every platform: an MSAA offscreen render with per-pixel lighting, shadow atlases, a procedural sky and a full post chain.
- **Parts:**
  - 38.2.1 Loading and context - `Render/GLRenderer` - `LoadGL(getProc)` resolves modern GL functions at run time (no link-time GL dependency); the host makes a hidden SDL window with a 3.2 compatibility context; shaders are `#version 120`
  - 38.2.2 Main pass - `Render/GLRenderer` - depth-tested MSAA target resolved to one sample; up to 16 lights, textures with normal, specular and AO maps, 9 shader modes, fog, reflections, emissive and alpha
  - 38.2.3 Shadows - `Render/GLRenderer`, `Render/ShadowSetup` - sun cascade atlas and a local-light tile atlas, 12-tap filtered compare
  - 38.2.4 Sky and post chain - `Render/GLRenderer` - procedural sky pass, HDR targets, SSAO, bloom, exposure/tonemap/grade/vignette, depth of field, FXAA (the only backend with SSAO and depth of field)
  - 38.2.5 GPU skinning - `Render/GLRenderer` - bone rows in the vertex shader (`SupportsGpuSkinning`, `GpuSkinJoints`)
  - 38.2.6 Cleanup - `Render/GLRenderer` - `Destroy()` frees every GL object with the context current
- **Code:** `Render/GLRenderer`
- **API:** `GLRenderer`, `GLRenderer::LoadGL`, `GLRenderer::RenderToPixels`, `GLRenderer::SupportsGpuSkinning`
- **Editor:** the Scene, Game and Camera Preview views when OpenGL is the backend; Lighting panel effects (2.4, 2.5)
- **Docs:** `docs/rendering.md`, `docs/lighting.md`
- **Tests:** `light_shadows`, `shadows` (the shadow plan, not a GL run)
- **Status:** Works - read the header and the shader and uniform lists in `GLRenderer.cpp` (2.5k lines); no test creates a GL context
- **Open:** every frame is read back to the CPU (no direct present); needs a 3.2 compatibility context; possible doc mismatch: `docs/rendering.md` says OpenGL also runs in the web player, but `player/main.cpp` skips every GPU backend under Emscripten (see 38.7)
- **Related:** 38.1, 38.8, 38.9, 2.3, 2.5
- **Words:** GLRenderer, OpenGL, GL, MSAA, FBO, GLSL, shadow atlas, SSAO, bloom, depth of field, GPU skinning, hidden GL window
- **Seen as:** shadows or bloom missing on Linux; black view with an old driver; the editor stutters on a big scene
- **Ask me like:** "Add a screen-space reflections pass to the OpenGL chain"

### 38.3 Direct3D 11 backend
- **What:** The default GPU renderer on Windows: same contract as OpenGL, with an isolated device, optional direct rendering into the editor's own textures and pipelined read-back.
- **Parts:**
  - 38.3.1 Device - `Render/D3D11Renderer` - `Init()` makes an isolated device with an offscreen target; `InitShared(device)` builds on SDL's own device
  - 38.3.2 Main pass - `Render/D3D11Renderer` - HLSL compiled at start with d3dcompiler (vs_4_0, ps_4_0), up to 4x MSAA resolved before read-back, lighting, textures, fog and 9 shader modes
  - 38.3.3 Shadows - `Render/D3D11Renderer`, `Render/ShadowSetup` - sun cascades and local tiles from the shared plan, plus a planar ground shadow
  - 38.3.4 Post chain - `Render/D3D11Renderer` - bloom, tonemap, grading, vignette and FXAA, plus an ambient occlusion pass (`SsaoMath.hpp`, compile-checked only, UNVERIFIED on a GPU); no depth of field
  - 38.3.5 Zero-copy and pipelined read-back - `Render/D3D11Renderer` - `RenderToTexture` resolves into an SDL texture; `SetReadbackStream(id)` shows the previous frame to avoid a GPU stall
  - 38.3.6 GPU skinning and sky - `Render/D3D11Renderer` - 256-joint bone palette; procedural sky and clouds
- **Code:** `Render/D3D11Renderer`
- **API:** `D3D11Renderer::Init`, `D3D11Renderer::InitShared`, `D3D11Renderer::RenderToTexture`, `D3D11Renderer::SetReadbackStream`, `D3D11Renderer::ReleaseCom`
- **Editor:** Scene and Game views on Windows; Build Game "GPU renderer" option
- **Docs:** `docs/rendering.md`, `docs/lighting.md`
- **Tests:** -
- **Status:** Works - read the header, the effect counts in `D3D11Renderer.cpp` (2k lines) and the editor and player call sites; compiled only on Windows (guarded by `_WIN32`) and never run by a ctest
- **Open:** no depth of field; the new SSAO pass has never run on a GPU (only the shared maths is unit tested, `ssao_math`); no automated runtime test; the editor's direct path falls back to read-back after 5 failures
- **Related:** 38.1, 38.8, 38.9, 38.10, 2.3, 2.5
- **Words:** D3D11, Direct3D 11, DirectX 11, HLSL, shared device, RenderToTexture, readback stream, zero-copy, Windows renderer
- **Seen as:** Windows-only black view or driver crash; GPU stall in the editor view; ambient occlusion looking wrong or missing on Windows (the pass is unrun)
- **Ask me like:** "Check the Direct3D 11 SSAO pass on a real GPU"; "Port depth of field from OpenGL to Direct3D 11"

### 38.4 Direct3D 12 backend
- **What:** An opt-in explicit-API sibling of the Direct3D 11 backend, used only by the player when Build Game asks for it.
- **Parts:**
  - 38.4.1 Opt-in selection - `player/main.cpp` - Build Game "Prefer Direct3D 12" (config `d3d12=1`); the editor does not use it
  - 38.4.2 Device and command plumbing - `Render/D3D12Renderer` - isolated device, queue, list, descriptors and an offscreen target; GPU wait and barriers
  - 38.4.3 Shader - `Render/D3D12Renderer` - a copy of the Direct3D 11 HLSL (`kHLSL12`, ps_5_1) so shading matches
  - 38.4.4 Features - `Render/D3D12Renderer` - sun shadow map with PCF, fog, textures; per the file comment up to 4x MSAA and a planar ground shadow; normal, gloss, AO, metallic-roughness and emissive maps (the header comment was corrected to match the code)
  - 38.4.5 Missing - `Render/D3D12Renderer` - no cascades, no spot or point shadows, no post chain, no GPU skinning, no sky pass (no matches in the source)
- **Code:** `Render/D3D12Renderer`
- **API:** `D3D12Renderer::Init`, `D3D12Renderer::RenderToPixels`, Build Game "Prefer Direct3D 12"
- **Editor:** Build Game > GPU renderer > Prefer Direct3D 12 (Windows only)
- **Docs:** `docs/lighting.md` (limitations)
- **Tests:** -
- **Status:** Partial - opt-in and incomplete; the source says it is compile and link verified through MinGW and "intended to be runtime-tested on Windows"
- **Open:** (the header and code now agree: up to 4x MSAA is applied); labelled "preview" in Build Settings; no post effects, cascades or skinning; no runtime test
- **Related:** 38.1, 38.3, 2.3
- **Words:** D3D12, Direct3D 12, DirectX 12, explicit API, command queue, opt-in backend
- **Seen as:** characters do not animate or post effects are off when D3D12 is chosen; shadows only from one sun
- **Ask me like:** "What is missing before Direct3D 12 can be the default?"

### 38.5 Software rasterizer
- **What:** The CPU renderer that runs everywhere and is the fallback and the test renderer: a z-buffered triangle rasterizer with lighting, shadows and its own post passes.
- **Parts:**
  - 38.5.1 Rasterizer - `Render/SoftwareRenderer` - `Raster`: W-buffer depth, perspective-correct textured triangles with mip maps, flat or per-pixel lighting
  - 38.5.2 Scene draw - `Render/SoftwareRenderer` - `RenderMeshes` and `RenderMeshesSS` (supersampled), frustum culling, camera culling mask, row-parallel passes (`ParallelRows`)
  - 38.5.3 Shadows - `Render/SoftwareRenderer` - cascaded sun shadow maps with soft filtering (`ShadowMap`, `ShadowFactor`), plus spot, point and area light shadow maps (`RenderLocalShadowMaps`, `LocalShadowFactor`, `LocalShadowMath.hpp`); drawn on the per-pixel path only
  - 38.5.4 Post passes - `Render/SoftwareRenderer` - SSAO, bloom, tonemap, colour grade, vignette, FXAA with process-wide switches (see 2.5.3)
  - 38.5.5 Texture caches - `Render/SoftwareRenderer` - `RegisterTexture`, `GetCachedTexture`, mip chains; the GPU backends reuse this cache, including `proc:` textures
  - 38.5.6 Occlusion depth - `Render/SoftwareRenderer` - `SceneOcclusionDepth` lets particles and lines hide behind geometry
- **Code:** `Render/SoftwareRenderer`
- **API:** `Raster`, `RenderMeshes`, `RenderMeshesSS`, `Shadows()`, `ShadowsEnabled()`, `PerPixelLighting()`, `SceneOcclusionDepth`
- **Editor:** the fallback for the Scene and Game views; Lighting panel > Rendering section switches
- **Docs:** `docs/rendering.md`, `docs/lighting.md`
- **Tests:** `software_renderer`, `shadows`, `light_shadows`, `software_local_shadows`, `ssao_math`, `shaders`, `lighting`, `frustum_culling`
- **Status:** Works - read the header (1.9k lines, header-only) and tests such as the depth-test one
- **Open:** slow on big imported models (the editor drops the render scale by triangle load); per-pixel lighting, SSAO and bloom are off by default; no procedural sky, clouds or DoF; shadows only on the per-pixel path
- **Related:** 38.1, 38.10, 2.3, 2.5, 2.8
- **Words:** software renderer, Raster, RenderMeshes, RenderMeshesSS, CPU rasterizer, z-buffer, W-buffer, supersampling, fallback renderer, mip map
- **Seen as:** 1 FPS with a large model; jagged edges; no shadows at all until Per-pixel lighting is on
- **Ask me like:** "Why is the software renderer used when I have a GPU?"; "Turn per-pixel lighting on by default in the software path"

### 38.6 Console renderer
- **What:** A dependency-free renderer that draws 2D quads as characters in the terminal, so the engine runs headless.
- **Parts:**
  - 38.6.1 Frame buffer - `Render/ConsoleRenderer` - glyph and depth per cell, nearer quad wins, framed output with an optional screen clear
  - 38.6.2 Camera and quads - `Render/ConsoleRenderer` - orthographic `SetCamera`, `DrawQuad` from `SpriteRenderer::OnRender`
  - 38.6.3 Test access - `Render/ConsoleRenderer` - `FrameBuffer()` exposes the characters
- **Code:** `Render/ConsoleRenderer`
- **API:** `ConsoleRenderer`, `ConsoleRenderer::FrameBuffer`, `Application::Config::clearScreen`
- **Editor:** none
- **Docs:** -
- **Tests:** `scene`
- **Status:** Works - read the header and `ConsoleRenderer.cpp` (99 lines); colour is only used for alpha, 3D is not drawn
- **Open:** no colour or 3D output; used by `sandbox/main.cpp` and the default `Application` only
- **Related:** 2.1, 1.1
- **Words:** ConsoleRenderer, ASCII, terminal renderer, headless, glyph
- **Seen as:** a text-mode demo shows only boxes
- **Ask me like:** "Add ANSI colours to the console renderer"

### 38.7 Web (WebGL) path
- **What:** How the player runs in a browser: the player is compiled to WebAssembly and uses the software path for 3D; there is no engine WebGL renderer class.
- **Parts:**
  - 38.7.1 Web build - `scripts/build-web.sh` - Emscripten, SDL2 from Emscripten's port, scene preloaded as `game.okayscene`; Assimp, libsodium and the relay are off
  - 38.7.2 Renderer in the browser - `player/main.cpp` - GPU backend creation is compiled out under `__EMSCRIPTEN__`, so frames use `RenderMeshesSS`; the 2D and UI drawing go through SDL
  - 38.7.3 CI job - `.github/workflows/ci.yml` - the "WebGL build (Emscripten)" job builds the player with that script
- **Code:** `scripts/build-web.sh`, `player/main.cpp`
- **API:** `scripts/build-web.sh <scene>`, Build Game Platform "Web (WebGL / WASM)"
- **Editor:** Build Game > Platform > Web (WebGL / WASM)
- **Docs:** `docs/web_mobile.md`
- **Tests:** -
- **Status:** Basic - the build and the software path exist (read `build-web.sh` and the `__EMSCRIPTEN__` guards); 3D in the browser is the CPU rasterizer
- **Open:** no WebGL or WebGPU backend, so GPU-only effects (SSAO, depth of field, procedural sky and clouds) are missing in the browser; possible doc mismatch in `docs/rendering.md` ("OpenGL in the editor and the Linux / web player"); software speed limits web scenes
- **Related:** 18.2, 38.1, 38.5, 2.5
- **Words:** WebGL, WASM, Emscripten, web player, browser, HTML5 build, okay-player.html
- **Seen as:** web export looks flatter or runs slowly than desktop; no clouds or bloom in the browser
- **Ask me like:** "Add a WebGL2 renderer using the OpenGL shaders"

### 38.8 Shader pipeline
- **What:** The shader programs the backends run, how they stay in step with the CPU maths, and the places a custom shader would plug in.
- **Parts:**
  - 38.8.1 GLSL programs - `Render/GLRenderer` - `#version 120` source strings: main mesh, depth-only shadow, procedural sky, bloom, SSAO, uber post, FXAA, depth of field
  - 38.8.2 HLSL programs - `Render/D3D11Renderer`, `Render/D3D12Renderer` - compiled at start with d3dcompiler; Direct3D 12 keeps its own copy
  - 38.8.3 Shader modes - `Components/MeshRenderer` - `Standard`, `Unlit`, `Toon`, `Gradient`, `Fresnel`, `Iridescent`, `Hologram`, `Posterize`, `Velvet` selected by a uniform (see 2.2.2)
  - 38.8.4 CPU mirrors - `Render/Atmosphere`, `Render/Lighting` - sky palette, fog factor, tonemap curves and light attenuation are written once on the CPU and mirrored in the shaders so values agree
  - 38.8.5 Shadow plan - `Render/ShadowSetup` - the cascade and tile layout both GPU backends consume
  - 38.8.6 Custom shaders - planned (area 26): there are no shader files, no shader graph and no hook for user shaders
- **Code:** `Render/ShadowSetup`, `Render/Atmosphere`, `Render/Lighting`, `Render/GLRenderer`, `Render/D3D11Renderer`
- **API:** `MeshRenderer::Shader`, `PlanShadows`, `FogFactor`, `LightAttenuation`
- **Editor:** Mesh Renderer shader dropdown; Lighting panel; no shader editor
- **Docs:** `docs/lighting.md` ("How it works")
- **Tests:** `shaders`, `light_shadows`, `post_sky`, `lighting`
- **Status:** Works - read the shader sources and uniform lists; the `shaders` test covers only the Material side
- **Open:** every lighting or fog change must be repeated in GLSL, HLSL (twice) and the software renderer; custom shaders, a shader graph and a PBR model are area 26; no shader compile-error surface beyond the log
- **Related:** 26, 2.2, 2.3, 2.4, 2.5, 38.9
- **Words:** shader, GLSL, HLSL, uniform, shader mode, toon, hologram, fresnel, material shader, shader graph, custom shader, d3dcompiler
- **Seen as:** a shader mode looks different between OpenGL and Direct3D; a new light feature works on one backend only
- **Ask me like:** "Add a dissolve shader mode to all backends"

### 38.9 GPU resources
- **What:** What the backends keep on the GPU between frames: render targets, vertex buffers, textures, shadow maps and bone palettes.
- **Parts:**
  - 38.9.1 Render targets - `Render/GLRenderer` - MSAA target and resolve, shadow atlases, bloom, SSAO and post targets, re-created when the view size changes
  - 38.9.2 Parked size sets - `Render/GLRenderer` - targets for the other view sizes (Scene, Game, Camera Preview, Animation preview) are parked and swapped back instead of rebuilt
  - 38.9.3 Mesh buffer cache - `Render/GLRenderer`, `Render/D3D11Renderer` - static meshes expanded and uploaded once, checked by a pointer fingerprint, a sampled hash and a periodic full hash; skinned meshes keyed by `SkinnedMesh::version`
  - 38.9.4 Texture cache - `Render/GLRenderer`, `Render/D3D11Renderer` - name to GPU texture, uploaded on first use from the shared CPU cache
  - 38.9.5 Bone palette - `Render/GLRenderer`, `Render/D3D11Renderer` - joint rows for GPU skinning (256 joints in Direct3D 11)
  - 38.9.6 Memory tracking - (#574) `Debug/MemoryTracker`, nothing reports GPU memory into it; the asset manager does not track GPU textures (1.11)
- **Code:** `Render/GLRenderer`, `Render/D3D11Renderer`, `Render/D3D12Renderer`
- **API:** `GLRenderer::Destroy`, `D3D11Renderer::Destroy`, `D3D11Renderer::ReleaseCom`
- **Editor:** editor view slots 0-3 (`ResetViewTextures` after device loss, editor/main.cpp)
- **Docs:** -
- **Tests:** `frustum_culling`
- **Status:** Works - read the cache structs in the headers; behaviour verified only by use, not by tests
- **Open:** no GPU memory budget, no texture streaming or compression (import setting `compress` is stored but unused, 1.12); a changed mesh relies on hash checks, so in-place edits can lag up to about a second until the full hash refreshes (per the header comment)
- **Related:** 38.2, 38.3, 38.10, 1.11, 1.12, 27
- **Words:** GPU memory, vertex buffer, texture cache, render target, framebuffer, atlas, bone palette, VRAM, upload
- **Seen as:** a hitch when a new texture appears; an edited mesh updates a moment late; memory grows when many views are open
- **Ask me like:** "Where do we upload textures and can we compress them?"

### 38.10 Frame and present path
- **What:** The journey from a rendered 3D image to the screen in the editor and the player, including the fallbacks and what is drawn on top.
- **Parts:**
  - 38.10.1 Offscreen render and upload - `editor/main.cpp`, `player/main.cpp` - the backend renders to an offscreen target and returns RGBA8; the host uploads it into an SDL streaming texture and draws it under the UI
  - 38.10.2 Editor view slots - `editor/main.cpp` - `Render3DTexture` serves the Scene view, Game view, Camera Preview and Animation preview with separate textures; supersampling and auto render scale for software
  - 38.10.3 Windows direct path - `Render/D3D11Renderer` - `RenderToTexture` into the SDL texture with no CPU read-back (not for the Animation preview)
  - 38.10.4 Pipelined read-back - `Render/D3D11Renderer` - one frame of latency in exchange for no GPU stall
  - 38.10.5 Fallback and self-heal - `editor/main.cpp`, `player/main.cpp` - repeated failures drop to software (editor retries after 5 s)
  - 38.10.6 Host-drawn layers - `player/main.cpp` - particles, lines, trails, text and UI are drawn after the 3D image; a half-resolution software depth pass fills the occlusion buffer when particles exist
- **Code:** `Render/SoftwareRenderer`, `Render/D3D11Renderer`, `editor/main.cpp`, `player/main.cpp`
- **API:** `Render3DTexture`, `SDL_UpdateTexture`, `SceneOcclusionDepth`, `D3D11Renderer::RenderToTexture`
- **Editor:** Scene view, Game view, Camera Preview, Animation preview; Stats and Profiler panels show render time
- **Docs:** -
- **Tests:** -
- **Status:** Works - read `Render3DTexture` in `editor/main.cpp` and the player's frame code; no test covers the host paths
- **Open:** every GPU frame goes through the CPU in the player and in the editor outside the Windows direct path; "GPU" time in the profiler is a synchronous render proxy, not a GPU timer (1.10); no swap chain or vsync control inside the backends
- **Related:** 38.1, 38.3, 38.5, 2.7, 15.3, 1.10
- **Words:** present, swap chain, readback, frame path, SDL texture, view slot, Scene view render, Game view render, supersampling, auto performance
- **Seen as:** a frame of delay in the Scene view; the editor view freezes while the GPU recovers; particles show through walls only with a GPU backend
- **Ask me like:** "Can the editor view present without reading pixels back on OpenGL?"

### 38.11 GPU timing and frame debugging
- **What:** Knowing what the GPU spent: real GPU timers, draw-call lists and render stats.
- **Parts:**
  - 38.11.1 Today - `Core/Profiler`, `Render/Culling` - a synchronous render-time proxy (`Prof().Gpu`) and the drawn/culled object counts
  - 38.11.2 Data layers - (#574) `Debug/GpuTimer`, `Debug/RenderStats`, `Debug/DrawCallRecorder`, `Debug/FrameHistory`, `Debug/MemoryTracker`, `Debug/DebugStatsOverlay`: GPU scopes with a CPU-fence fallback, per-frame render counters, a one-frame draw list, memory categories and an overlay text builder
- **Code:** `Core/Profiler`, `Render/Culling`, `Debug/GpuTimer`, `Debug/RenderStats`, `Debug/DrawCallRecorder`
- **API:** `Prof().Gpu`, `LastCullCounters`, `GpuTimer`, `GpuScope`, `DrawCallRecorder`
- **Editor:** Stats and Profiler windows (editor/main.cpp)
- **Docs:** `docs/debug_tools.md`
- **Tests:** `profiler`, `debug_gpu_stats`, `debug_draw_calls`
- **Status:** Partial - the data layers are on main (#574, tests above) but no backend calls them ("No renderer calls the hooks yet", per docs; grep finds no `GpuScope` outside `Debug/`); the live "GPU" number is still the synchronous render-time proxy
- **Open:** wiring real GPU timers into each backend; a frame debugger window (area 23)
- **Related:** 1.10, 23, 38.9
- **Words:** GPU timer, draw calls, frame debugger, render stats, GPU profiler, overdraw
- **Seen as:** cannot tell whether the CPU or GPU is the bottleneck
- **Ask me like:** "Show draw calls and triangles per frame on a debug overlay"
