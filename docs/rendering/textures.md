# Texture system (shared texture assets)

> **What this is:** the internal record of how a picture becomes a texture in every renderer: one shared asset with a
> handle, loaded through `AssetManager`, hot reloaded, with per-texture settings, our own mip chains, a memory count
> and a budget.
>
> **Status:** built on branch `claude2/texture-system` (2026-11-22). Software and OpenGL paths tested on Linux;
> Direct3D 11 and 12 compile-checked with MinGW only. See "What ran" below.
>
> **Last checked:** 2026-11-22.

## Before

Each renderer kept its own path-keyed cache. The software renderer's `TextureCacheMap()` (a global
`unordered_map<string, Image>`) was the only CPU copy; OpenGL, Direct3D 11 and Direct3D 12 read it by name and kept
their own GPU object by name. Nothing was ever freed, a changed file was never noticed, and a changed import setting
flushed every GPU texture through one global counter (`TextureCacheGeneration`). `AssetManager` (handles, reference
counts, hot reload) existed with no caller. MemoryTracker had a `Textures` category that nobody filled. Mip chains
differed per renderer: the software renderer averaged gamma-encoded bytes (a black and white checker went to grey 128,
darker than it looks), OpenGL called `glGenerateMipmap`, Direct3D 11 and 12 uploaded no mips at all for an ordinary
picture. Direct3D 12 also never uploaded a picture again after a reimport, and its first-time uploads reset the frame's
command list in the middle of recording.

## Now

Code: `engine/include/okay/Render/TextureAsset.hpp`, `engine/src/Render/TextureAsset.cpp`. Written for this engine
from its own `AssetManager`, `MemoryTracker` and `okay::tex` mip filters (`Texture/Mip.hpp`); no outside code.

- **One asset per picture.** `textures::Load(path)` decodes a file, a `proc:` name or a registered `@` name once,
  through `AssetManager` (a `TextureAsset` loader), and returns a `TextureHandle`: a counted pointer that shares the
  AssetManager entry's reference count (`AssetHandle::Shared()`). The asset holds the RGBA8 pixels (import `maxSize`
  applied), its `TextureSettings`, and a `serial` that is new for every decode or change. The last handle frees it.
- **What the renderers use.** `textures::ForRender(name)` (one hash lookup) is the lookup every renderer makes. The
  library holds one handle per name drawn. `GetCachedTexture` and `GetCachedMips` (software renderer, older callers)
  go through it. OpenGL (`GLRenderer::TextureFor`), Direct3D 11 (`D3D11Renderer::Impl::TextureFor`) and Direct3D 12
  (`D3D12Renderer::Impl::TextureFor`) keep only their GPU object per name plus the serial it was made from, and upload
  again when the serial changes. When the library lets go of a name (`Drop`, `InvalidateAll`, a budget trim) it bumps
  `textures::Generation()`, and each GPU renderer frees its copies of names no longer held (`textures::IsHeld`).
  Direct3D 12 reuses the freed staging descriptor slots, and uploads every picture of a frame before recording starts
  (`PrepareTextures`).
- **Hot reload.** `textures::Pump()` runs once a frame (player before the render, editor before `NewFrame`). With hot
  reload on (editor: on, `OKAY_TEXTURE_HOT_RELOAD=0` turns it off; player: `OKAY_TEXTURE_HOT_RELOAD=1`), AssetManager
  compares each loaded file's time and size every 0.5 s, decodes a changed one on its worker thread, and moves the new
  pixels into the same asset. Handles stay valid; the serial changes; the renderers re-upload only that picture. A
  missing picture is looked for again every 120 frames while hot reload is on.
- **A file changed on disk, seen by the Project panel.** `InvalidateTextureCacheForFile(file)`
  (`Render/TextureCacheFiles`, header and editor hook from `claude2/upgrade-import`) calls `textures::ReloadFile`: every
  name the file was drawn under (relative, "./" and ".." forms, absolute, another spelling) and every asset loaded from
  it that other code holds is decoded again in place. So hot reload, Reload and the Project panel's reload are one path.
- **Background loading.** `textures::SetAsyncLoading(true)` or `OKAY_TEXTURE_ASYNC=1`: `ForRender` starts the decode on
  AssetManager's worker and returns null (the surface draws untextured) until it is done. Off by default.
- **Changed settings.** The Inspector's Apply calls `InvalidateTextureCache(path)` (drop and re-decode that picture in
  place, so handles held elsewhere see it) and then `InvalidateAllTextureCaches()` (other names for the same file).
- **Registered pictures** (`RegisterTexture`, `textures::Register`): held until `Unregister`. The terrain splat map is
  one: painted in place through `textures::RegisteredImage` and published with `textures::TouchRegistered` (a new
  serial), so only the splat is uploaded again instead of every texture.

## Settings (per texture, in the `.okaymeta`)

`TextureSettings` / `ReadTextureSettings(path)` reads the `importer texture` block in one pass:

| Key | Values | Default | Read by |
|---|---|---|---|
| `srgb` | 0 / 1 | 1 | mip filtering in linear light; compressed copy |
| `normalMap` | 0 / 1 | 0 | mips renormalised, auto compression BC5, never sRGB |
| `filter` | point, bilinear, trilinear | trilinear | software, OpenGL, Direct3D 11 (Direct3D 12: not yet) |
| `anisotropy` | 1..16 | 8 | with trilinear: OpenGL `GL_TEXTURE_MAX_ANISOTROPY`, Direct3D 11 sampler (Direct3D 12 keeps its one 8x sampler) |
| `wrap` | repeat, clamp, mirror | repeat | software, OpenGL, Direct3D 11 (Direct3D 12: not yet) |
| `mipmaps` | 0 / 1 | 1 | all four renderers |
| `mipFilter` | box, kaiser | box | all four renderers and the compressed copy |
| `maxSize` | 0, 32..16384 | 0 | the decode (all renderers) |
| `compress` | 0 / 1 | 0 | OpenGL and Direct3D 11 keep BC blocks; software and Direct3D 12 decode |
| `compression` | auto, bc1, bc3, bc4, bc5 | auto | the compressed copy in `.okaycache` |

The first six keys are `ImportSettings`' own texture keys. `anisotropy`, `normalMap`, `mipFilter` and `compression`
are texture-only keys in the same block; `ImportSettings` keeps them as extra keys (it round-trips unknown keys), so
`ImportSettings.cpp` and its layout version did not change. The editor writes them with `TextureExtraKeys`.

Editor: select a picture in the Project panel; the Inspector shows Import Settings: Kind (Colour / Normal map / Data),
Largest side, Filter (Point / Bilinear / Trilinear / Anisotropic with 2x to 16x), Wrap, Mipmaps with Mip filter
(Box / Kaiser), Compress on the GPU with Format (Auto, BC1, BC3, BC4, BC5), and a "Loaded:" line (size, mip levels,
MB, handles) when the picture is in use. These controls were hidden before (`kShowTextureSamplerSettings`) because
the Direct3D renderers did not read them.

**BC7 is not written** (and BC6H, ASTC, ETC): our encoder (`Texture/Bc`) covers BC1 to BC5. A BC7 encoder is a large
piece of work (eight modes, partition tables); it is not started.

## Mip chains

`BuildMipChain(image, settings)` / `TextureAsset::Mips()`: `okay::tex::GenerateMipsRGBA8` with the box (exact area) or
Kaiser-windowed filter, colour decoded to linear light before filtering (sRGB pictures), alpha-weighted, normal maps
decoded as vectors and renormalised, wrap-aware at the edges. The software renderer shares the asset's chain (no
copy: `MipChain` is now a view holding a `shared_ptr` to the levels). OpenGL, Direct3D 11 and Direct3D 12 upload the
same levels (built for the upload and not kept, unless the software renderer already built them). Cost: our filter
runs on the CPU at load; `glGenerateMipmap` was faster for big pictures.

## Memory and budget

- Every live asset is counted in `MemoryTracker::Global()` category `Textures` (count 1 per asset, base level plus a
  built mip chain). The GPU renderers count what they uploaded in `Textures (GPU)`.
- `textures::GetStats()`, `textures::Report()` (one row per texture, largest first) and `textures::OverlayLine()`:
  "textures 12  38.4 MB / 1024 MB  (GPU 12, 51.0 MB)".
- Shown in: the player's F3 panel and the editor's Game view stats (`FrameOverlay::Lines` adds the line while any
  texture is loaded), and the editor's Profiler window, section "Textures" (every texture: size, mips, MB, kind).
- Budget: `OKAY_TEXTURE_BUDGET_MB` (default 1024; 0 = none) or `textures::SetBudgetBytes`. Over it, `Pump` logs one
  warning per crossing, the overlay line says `OVER BUDGET`, and the library lets go of the textures no renderer has
  drawn for 300 frames (`SetIdleFrames`), oldest first, until it is under. Textures in use are never dropped.

## Mip streaming (2026-11-22)

Off by default. `textures::SetStreaming(true)`, `OKAY_TEXTURE_STREAMING=1`, the editor's Stats panel >
Streaming > "Stream texture mips", or an **Asset Streaming** component in the scene (while it plays) turn it on.

- A picture file (mipmaps on, not `proc:` or `@`) is decoded and cut down to its first mip whose longest side is
  at most `StreamStartSize()` (64 by default, the component's "Start size"). `TextureAsset::image` holds that
  mip; `residentMip`, `fullWidth`, `fullHeight` say which. Renderers need no change: they upload `image` and its
  chain as before, and a new serial makes them upload again.
- `streaming::Update(scene)` (Render/AssetStreaming, once a frame; the component calls it) measures each
  texture on screen (largest bounding-sphere diameter of a mesh renderer that uses it, by `tex::ProjectedPixels`,
  times the material tiling), asks `optimize::PlanMips` for the mip each should keep (one texel per pixel,
  hysteresis from the resident mip, the budget), evicts blurrier mips at once and queues sharper ones with
  `textures::RequestMip`: a background decode on `JobSystem::Global()` (low priority), at most
  `uploadMBPerFrame` of new pixels per frame (the most visible first; the first is always let through).
  `textures::Pump` applies finished loads (`ApplyStreamLoads`).
- One load per texture at a time; an eviction while a load runs marks it stale (its result is dropped).
- The component's "Budget" (MB, 0 = the texture budget) is what `PlanMips` keeps to; the Stats panel shows
  memory used against it, full-size memory, pending loads, loads done and evictions; F3 prints a
  `streaming:` line.
- When the component goes (Stop in the editor) streaming turns off and the streamed pictures are read again
  whole (`textures::InvalidateAll`).
- Limits: a sharper mip decodes the whole file again (PNG/JPEG have no mips on disk; a `.dds` with mips is
  decoded whole too); the CPU keeps the resident mip (and the chain once a renderer asks for it); textures
  that no mesh renderer uses go down to the smallest allowed mip; registered (`@`) and procedural pictures
  never stream.

## What ran

All on Linux, Release build, 2026-11-22, after merging `origin/main`:

- `texture_asset` 161 checks, 0 failures: handles, sharing, freeing to zero, hot reload of a rewritten PNG (pixels,
  serial, size change and memory), settings parsing and range, Reload after a setting change, gamma-correct and linear
  mips, normal-map length, mipmaps off, registered pictures, procedural names, the budget trim, the compressed copy's
  format, background loading, the read-only scope, `InvalidateTextureCacheForFile`.
- `gl_texture_asset` 31 checks, 0 failures, OpenGL on Mesa llvmpipe 25.2 through EGL: our chain uploaded (level 1 of a
  colour checker reads 188, of a data checker 128), hot reload re-uploads under the new serial, anisotropy 4 read back
  from GL, point filtering turns it off, freed copies leave `Textures (GPU)`.
- Before (the old cache, measured with a probe against the old headers): level 1 of the checker read 128; a rewritten
  file was never seen (`GetCachedTexture` kept the old pixels).
- `import_settings_real`: three expectations changed from the old byte average to the gamma-correct value (0.5 to
  0.735, 0.25 to 0.368, 0.5 to 0.735).
- Direct3D 11 and Direct3D 12: compile-checked with MinGW (`x86_64-w64-mingw32-g++-win32 -fsyntax-only`); never run.
- Editor: `ProjectPanelUI.cpp` (the Inspector section) syntax-checked; `editor/main.cpp` (Profiler section, per-frame
  Pump) was not compiled: the compiler was killed for lack of memory twice on the shared build machine. Not looked at.
- Player: the per-frame `Pump` line was not compiled (player not built).

## Not done

- No streaming loop (low mip first); `Texture/Stream` plans, nothing loads partial chains yet.
- Direct3D 12 samples every picture with one static sampler (anisotropic 8x, repeat): Filter, Wrap and anisotropy are
  not applied there.
- The compressed path still decodes the picture twice the first time (once for the asset, once to build the `.okaycache`
  copy), and OpenGL / Direct3D 11 keep the RGBA8 asset in CPU memory next to the compressed GPU copy.
- No sRGB GPU formats: the GPU renderers upload `RGBA8` and shade in gamma space as before; only the mips are filtered
  in linear light.
- Mesh and shader handles (the rest of foundations row 7) are not part of this.
