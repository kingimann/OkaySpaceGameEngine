# RHI: the rendering hardware interface

> **What this is:** the design record of the RHI, the one set of calls every drawing backend implements, so a
> rendering feature is written once instead of once per renderer. Internal record, not a guide.
>
> **Status:** round 2 (2026-11-26, owner: "yes i want a custom one"). The core, the OpenGL, Software and Direct3D 11
> backends, the OkayShader bridge for unlit surfaces and Bloom without the post chain (OpenGL and Direct3D 11
> renderers) are in. The Direct3D 11 backend is compile-checked with MinGW only: unverified until it runs on Windows.
> Direct3D 12, Vulkan and WebGPU are later rounds. Code: `engine/include/okay/Render/RHI/`, `engine/src/Render/RHI/`.
> Tests: `rhi` (Software, headless; the HLSL rules as text), `rhi_gl` (OpenGL against Software, Xvfb).
>
> **Last checked:** 2026-11-26.

## Why

Before the RHI the engine had four 3D renderers (`GLRenderer`, `D3D11Renderer`, `D3D12Renderer`, the software
rasterizer) that share no interface: every effect (sky, shadows, bloom, SSAO, depth of field...) is written four times
and drifts (`docs/status/foundations_gap.md` row 8). The RHI is the layer under the renderers. Features move onto it one
at a time, each proved with a picture test, and each backend's own copy of that feature is deleted in the same change
(rules 19c and 19d: never two paths side by side).

## The API (`RHI/RHI.hpp`, namespace `okay::rhi`)

| Object | What it is |
|---|---|
| `Device` | Makes and destroys every resource, answers `GetCaps()`, runs command lists (`Submit`), reads textures back. `CreateSoftwareDevice()`, `CreateOpenGLDevice()`, `CreateD3D11Device(ID3D11Device*)`. |
| `SwapChain` | The picture shown in a window: `BackBuffer()` is a texture a render pass can draw into, `Present()` shows it. OpenGL: framebuffer 0 and a swap callback. Software: a texture the host reads. |
| `Buffer` | Vertex or index bytes (`BufferUsage`), optional initial data, `UpdateBuffer`. |
| `Texture` | 2D, `RGBA8`, `RGBA16F`, `RGBA32F` or `Depth`; sampled and/or a render target. Upload and read-back take rows bottom first. |
| `Sampler` | Filter (nearest, linear) and wrap (clamp, repeat). No mip levels yet. |
| `Shader` | One program for every backend: GLSL 1.20 text (OpenGL), HLSL shader model 4.0 text (Direct3D 11, rules below), the same program as two C++ functions (Software). It declares its vertex attribute names (location i), ONE uniform block (named fields at byte offsets) and named texture slots. |
| `Pipeline` | Shader + vertex layout (stride, attributes: location, format, offset) + topology + cull mode + blend state (separate colour and alpha factors, add, write mask) + depth state (test, write, compare). |
| `RenderPassDesc` | Colour and/or depth attachment, load op (load, clear, don't care), clear values. |
| `CommandList` | Recorded, then submitted: `BeginRenderPass`, `SetViewport`, `SetPipeline`, `SetVertexBuffer`, `SetIndexBuffer`, `SetTexture(slot, texture, sampler)`, `SetUniforms(bytes)`, `Draw`, `DrawIndexed`, `EndRenderPass`. |

**Conventions** (OpenGL's, so the OpenGL backend converts nothing): clip space -w..w on all three axes, depth = z/w * 0.5
+ 0.5; window y goes up and texture row 0 is the bottom (NDC y = -1, v = 0, the first row uploaded); front faces are
counter-clockwise. A later Direct3D or Vulkan backend flips y in its viewport and remaps depth, not the features.

**Uniforms.** A shader has one uniform block described by `UniformField`s (name, type, offset). Fields follow the
HLSL constant-buffer rule (no field crosses a 16-byte boundary, a `mat4` starts on one); `ValidateShaderDesc` checks it,
so the same bytes become a D3D11 cbuffer or a Vulkan uniform buffer without change. OpenGL 2.1 has no uniform buffers
that we can rely on, so its backend calls `glUniform*` per field by name. `SetUniforms` copies the bytes into the command
list (the frame's uniform memory); explicit backends will copy that arena into a per-frame ring buffer.

**HLSL rules** (Direct3D 11; `ValidateHlslDesc` checks them as text on every platform, and the backend runs it before
`D3DCompile`): entry points `VSMain` and `PSMain`; vertex attribute location i is the input semantic `TEXCOORDi`; the
uniform block is a `cbuffer` at `register(b0)` with the same byte layout; texture slot i is `register(ti)` with its
sampler at `register(si)`; the vertex stage returns `rhiClip(clipPos)` as `SV_Position`. `rhiClip` comes from
`kHlslPrelude`, which the backend puts before each stage: it flips y and maps z from -w..w to 0..w. Texture rows are
stored as given (RHI row 0 = Direct3D row 0), so with that one flip texture coordinates, viewports (TopLeftY = the RHI's
y), `SV_Position` (= the RHI's fragCoord) and read-back need no change, and the RHI's counter-clockwise front faces are
Direct3D's clockwise ones (`FrontCounterClockwise = FALSE`).

**Validation.** The command list refuses mistakes while recording and `Submit` then refuses the list: a draw outside a
pass or without a pipeline, nested passes, sampling a texture that is the current pass's own attachment (a feedback
loop), a texture bound without a sampler. `Submit` also refuses a destroyed handle, an index or vertex read past the end
of a buffer, and a bad attachment. Handle ids are never reused, so a stale handle is caught, not aliased.

**State tracking and barriers.** The command list records a `Transition` (texture, new state: render target, depth
write, shader read...) whenever a texture's use changes. A transition asked for inside a pass is moved to just before
that pass begins (Direct3D 12 and Vulkan cannot change a state inside one). OpenGL and Software count and ignore them
(`Caps::needsBarriers` false); the Direct3D 12 and Vulkan backends will turn them into resource barriers, checking the
device's last known state so a transition that is already true costs nothing.

**Lifetime.** `Device::Destroy(handle)` frees at once on backends with `Caps::framesInFlight` 1 (OpenGL, Software).
A backend that lets the CPU run ahead (Direct3D 12, Vulkan: 2 or 3 frames) keeps the resource until that many
`EndFrame()` calls have passed (tested with a stand-in device). `LiveResources()` counts what is alive, for leak checks;
the tests check it returns to 0. Destroying a swap chain's back buffer alone does nothing; it goes with its swap chain.

**Capabilities** (`Caps`): backend, device name, max texture size, texture slots, float render targets, depth textures,
32-bit indices, base vertex, whether barriers matter, frames in flight. Features ask before using something optional.

## Backends

| Backend | State | Mapping |
|---|---|---|
| Software | Round 1, done | Runs the lists on the CPU: homogeneous clipping on all six planes, 8 sub-pixel bits, edge functions with a top-left tie rule (two triangles sharing an edge never both draw a pixel: a blended full-screen quad must not add its diagonal twice), perspective-correct varyings, depth, GL-style bilinear sampling (texel centres at (i + 0.5) / size), blending in float with the destination read as 8-bit or rounded to half floats. It is the reference picture for the other backends. The software renderer's own rasterizer (`TriangleSpan.hpp`) was not reused: its float edge test draws shared edges twice, right for its opaque passes, wrong for blending. |
| OpenGL | Round 1, done | Shares `GLRenderer::LoadGL`'s entry points (no second loader; six optional ones were added). Buffer objects, 2D textures (sampler state applied when bound: GL 2.1 has no sampler objects), one program per shader with attribute locations bound by name, a cached framebuffer object per (colour, depth) pair, back buffer = framebuffer 0. Its own vertex array object when the context has them. `Submit` leaves GL in the state `GLRenderer` expects (no program, framebuffer 0, blend/depth/cull off, masks on, unit 0, the old vertex array object). `WrapGLTexture` lets an RHI pass draw into a texture the renderer made. Not yet: `glDrawElementsBaseVertex`, read-back of depth. |
| Direct3D 11 | Round 2, written; unverified | `RHI/D3D11Device.cpp`. Runs on the D3D11 renderer's own `ID3D11Device` and immediate context (it keeps a reference; no second device, no loader: `D3DCompile` from d3dcompiler, already linked). Buffers: default usage, `UpdateSubresource`. Textures: the views their use needs (SRV, RTV, DSV; a sampled depth texture is typeless R24G8); RGBA16F converted to and from halves (`tex::FloatToHalf`). Samplers: state objects. Shaders: VS + PS compiled at `CreateShader` (vs_4_0 / ps_4_0) and one dynamic constant buffer, written with `Map(WRITE_DISCARD)` before a draw when the uniforms changed. Pipeline: input layout + blend, depth-stencil and rasterizer state objects (Direct3D 11 returns the same object for an identical description, so there is no cache of our own). Render pass: `OMSetRenderTargets` + clears; shader inputs of the last pass unbound first. `Submit` saves the context state it touches (input assembler, shaders, b0, t0-t7, s0-s7, targets, blend, depth, rasterizer, viewport) and puts it back. `WrapD3D11Texture` uses a renderer texture. Swap chains are off-screen textures for now (as on Software). Xbox (UWP) has no run-time compiler: `CreateShader` fails there until RHI shaders go into the shader bundle. Compiles with MinGW win32 (rule 21a); not run on Windows yet. |
| Direct3D 12 | Round 4 | Command allocators per frame in flight (3), descriptor heaps, a per-frame upload ring for the uniform arena, root signature with one root CBV + a descriptor table of textures, PSOs cached by pipeline description, the recorded transitions as resource barriers, deferred destruction. |
| Vulkan | Round 5 | As Direct3D 12, plus render passes / dynamic rendering, SPIR-V (from our own GLSL-to-SPIR-V or OkayShader emitter, never glslang: rule 19a), memory from our own sub-allocator. Not on the Windows-only plan yet (rule 14). |
| WebGPU | Later | Close to Vulkan in shape; WGSL from OkayShader. Only if the web player comes back. |

## Shaders and OkayShader

OkayShader (`Render/OkayShader.hpp`) compiles one surface source to GLSL, HLSL and a CPU register program. Round 1
uses it directly: `ShaderFromSurface` (`RHI/SurfaceShader.hpp`) wraps an **unlit** surface in a mesh vertex stage and a
fragment `main`, so one `.okayshader` file draws on OpenGL (its GLSL) and Software (its CPU program); the uniform block
puts the time and each property in their own 16 bytes, the same slots as the HLSL cbuffer OkayShader emits. Lit
surfaces wait for lighting on the RHI.

OkayShader only describes surfaces, not whole vertex/fragment stages, so the bloom pass still carries its GLSL and its
C++ twin by hand (in one file, side by side). Round 2 adds **pass shaders** to OkayShader (`stage vertex { }` and
`stage fragment { }` with the same expression language), so one source gives GLSL, HLSL and the CPU program for every
pass and the hand-written twins are deleted.

The 2D `platform::Painter` (UI) stays separate. It could later sit on the RHI (its OpenGL painter is quads, a texture
atlas and scissors: a pipeline, a dynamic vertex buffer and `SetViewport`), once the RHI has scissor rects and dynamic
buffer rings; that would also give the UI a Direct3D path for free.

## Migration order

Each step moves one feature, adds its picture test (the old output against the RHI output, on every backend that had
it), and deletes the replaced per-backend code in the same change.

| # | Feature | Picture test | State |
|---|---|---|---|
| 1 | OpenGL Bloom without the post chain (bright pass, 2 blur rounds, additive composite) | `rhi_gl`: the renderer's frame equals the RHI OpenGL bloom pixel for pixel, and the Software device's bloom within 3/255 | **Done (round 1).** `kBrightFrag`, `kAddFrag`, their programs, uniforms and `EnsureBloomTargets` deleted from `GLRenderer`. Before deleting, the same test run against the old code gave 0 differing pixels. |
| 2 | Direct3D 11 backend; Bloom without the post chain on it | compare D3D11 against Software on Windows (not run yet) | **Written (round 2), unverified.** The D3D11 renderer had no Bloom without the post chain (View > Lighting "Bloom" did nothing there unless the scene's post chain ran), so there was no copy to delete: it now runs `rhi::BloomPass`, the same pass as OpenGL. Its bloom INSIDE the post chain (`PSDown` / `PSBlur`, half and quarter size, added in the uber pass) is shared HLSL with Direct3D 12 and the twin of the OpenGL chain's; it moves with step 3, for all three at once. |
| 3 | The rest of the full-screen passes: SSAO blur/multiply, the post chain (downsample, uber, DoF, FXAA) on OpenGL and Direct3D 11 | old vs RHI per pass, exact on the same backend | round 2-3 |
| 4 | Sky pass (procedural) | old vs RHI per backend; Software against OpenGL within a tolerance | round 3 |
| 5 | Shadow depth passes (depth-only pipelines, viewport per cascade) | depth atlas compare | round 3 |
| 6 | Unlit then lit mesh pass (lighting uniforms as a block, then the main shader) | the existing `okayshader_gl` scenes, renderer vs RHI | round 3-4 |
| 7 | Software renderer onto the RHI Software backend (its own rasterizer then deleted, after its look is matched) | `software_renderer` goldens | round 4 |
| 8 | Direct3D 12 backend; `D3D12Renderer` deleted | Windows | round 4 |

The Software renderer's own bloom (`ApplyBloom`: 4x box downsample and box blurs) is a different look from the OpenGL
one; it stays until step 7, when the owner chooses one look (the RHI pass makes both backends draw the same).

## Round 1 status

- Done: the API above; Software and OpenGL backends; `ShaderFromSurface`; `BloomPass` used by `GLRenderer`;
  tests `rhi` (device, upload/read-back, layout rules, validation, transitions placed before passes, every pixel of a
  tiled surface drawn exactly once, exact triangle coverage, blending maths, write mask, depth order, culling, indexed
  U16/U32 equal to expanded draws, out-of-range index refused, the test scene against `tests/golden/rhi/software_scene.png`,
  OkayShader surface colours, bloom properties, swap chain, lifetime) and `rhi_gl` (the same scene on OpenGL against
  Software, an OkayShader surface on both, and the Bloom equality above).
- Not yet: lines and strips, scissor, mip levels, multisampled targets and resolve, more than one colour attachment,
  base vertex on OpenGL, depth read-back on OpenGL, texture copies, compute.
- Unverified: any real GPU (the GL tests run on Mesa's llvmpipe under Xvfb), Windows (the files compile with MinGW
  `-fsyntax-only`), Direct3D.

## Round 2 status

- Done: `ShaderDesc::hlslVertex` / `hlslFragment`, the HLSL rules and `ValidateHlslDesc`; the Direct3D 11 backend
  (`CreateD3D11Device`, `WrapD3D11Texture`); the bloom pass's HLSL (the GLSL line for line); the D3D11 renderer runs
  `rhi::BloomPass` for Bloom without the post chain, on its own device (its resolve texture became a render target too,
  so the glow is drawn onto it). Test `rhi`: the HLSL rules (each one refused when broken), the prelude, a stand-in
  device that refuses HLSL breaking the rules runs the whole bloom pass, and no D3D11 device or wrap off Windows.
- Unverified: everything Direct3D 11. It compiles with MinGW (`x86_64-w64-mingw32-g++-win32 -fsyntax-only`, also with
  `OKAY_XBOX_UWP`); no HLSL was compiled and nothing was drawn. The first Windows run should compare the D3D11 bloom
  against the Software device's, as `rhi_gl` does for OpenGL.
- Not done: scissor, mip levels and texture copies (nothing moved so far needs them); OkayShader pass shaders (one
  source for GLSL, HLSL and the CPU program, so the hand-written twins in `BloomPass.cpp` go); HLSL for OkayShader
  surfaces (`ShaderFromSurface`: its property cbuffer is at b4 with its own layout, the RHI has one block at b0);
  a real Direct3D 11 swap chain (DXGI, a window).
