# Our own texture library (`okay::tex`)

Mipmaps, atlases, format conversion, block compression and a streaming planner, written by us. It adds
capability that was missing, so it is on wherever it only adds (nothing is replaced, nothing is switched off by
default). Branch `claude/texture-lib-1`.

## Existing / Gap (searched before building)

| Area | Existing (what we already had) | Gap (what was missing, and what this adds) |
|---|---|---|
| Mip chains | `Optimize/MipBudget` (feature map 27.4) only plans *which mip stays resident*; `GLRenderer` calls `glGenerateMipmap` on upload when the import setting `mipmaps` is on (the GPU's own box-ish filter, not sRGB-correct, no alpha coverage, no normal maps). `Image::Sample` is bilinear only. | No CPU mip generator at all. Adds `Texture/Mip`: box, Lanczos3, Kaiser; linear-light sRGB filtering; alpha-weighted (premultiplied) filtering; alpha-test coverage kept; normal-map renormalising. |
| Atlases | `Graphics/TtfRaster` `ShelfPacker` (glyph atlases only), `TtfFont` glyph atlas, `UI/UIBatcher` atlas pages, `Render/ShadowSetup` shadow atlas layout, `SpriteRenderer`/`SpriteAnimator` use a hand-made grid (columns/rows). Feature map 2.12 says: "no sprite atlas importer or slicing tool". | No general rectangle packer, no padding/bleed, no manifest. Adds `Texture/Atlas` (MaxRects, rotation, bleed, JSON manifest) and the `okay-texture atlas` command. The glyph `ShelfPacker` is left as it is (different job: grows on demand). |
| Format conversion | `Image` is RGBA8 only; `Color` is float RGBA; `ImageWrite` writes PNG. | No BGRA, 565, half float or single channel; no premultiply or channel packing. Adds `Texture/Convert`. |
| Compression | Import setting `compress` exists but is "only a flag" (feature map 20.4: "compressed GPU formats are only a flag"). No DDS, no BC code anywhere. | Adds `Texture/Bc` (BC1/BC3/BC4/BC5 encoder and decoder) and `Texture/Dds` (writer and reader). The files can be inspected in any DDS viewer. |
| Streaming | `Optimize/MipBudget` (`PlanMips`: on-screen size to mip, memory budget, demotion order, hysteresis). `Optimize/StreamingGrid` streams world cells, not textures. | No camera-distance input, no load/evict delta, no per-frame upload cap. Adds `Texture/Stream`, which *calls* `PlanMips` (no second copy of the budget logic). |
| Import settings | `Core/ImportSidecar` stores texture `maxSize`, `filter`, `wrap`, `mipmaps`, `compress`; Project panel Import Settings box (`DrawImportSettings`) edits some of them. | The box had no way to produce a mip or compressed file. Adds a "Generate mipmaps / compress" section that writes a `.dds` beside the picture. The stored `compress` flag and the renderers are **not** changed (see "Not done"). |

## What each file was written from (rule 19a: no code from other projects)

| File | Written from |
|---|---|
| `Texture/Texture.hpp/.cpp` | The sRGB transfer curve (IEC 61966-2-1: linear toe below 0.04045, power 2.4 above) and the definition of PSNR. |
| `Texture/Mip.hpp/.cpp` | The definitions of the box filter (exact area overlap), the Lanczos window (sinc(x)sinc(x/3)) and the Kaiser-windowed sinc (Bessel I0 by its power series); separable resampling with the kernel stretched by the shrink ratio; alpha weighting (premultiplied filtering); the idea of keeping the share of texels at or above the alpha cutoff constant (our own bisection on an alpha scale). |
| `Texture/Atlas.hpp/.cpp` | The MaxRects idea (free-rectangle list, split on overlap, prune contained rectangles, best short side fit) as described in the public paper "A Thousand Ways to Pack the Bin" (J. Jylanki, 2010). Padding, bleed, rotation, sheet growth and the manifest are our own design. |
| `Texture/Convert.hpp/.cpp` | The IEEE 754 binary16 layout (round to nearest even, subnormals, infinity, NaN) and the usual 5-6-5 packing. Rounding rules are ours: `floor(v * max + 0.5)`. |
| `Texture/Bc.hpp/.cpp` | The public block layouts (BC1: two RGB565 endpoints and 2 bit indices, 3 colour mode when `c0 <= c1`; BC4: two 8 bit endpoints and 3 bit indices, 8 value mode when `a0 > a1`, else 6 values plus 0 and 255; BC3 = BC4 alpha + BC1; BC5 = two BC4). The encoder is our own: principal axis by power iteration on the block covariance, endpoint quantisation, least-squares refinement for the chosen indices, and a +-1 search around endpoints at quality 2. Palette interpolation uses truncating integer division; a GPU may differ by one level. |
| `Texture/Dds.hpp/.cpp` | The public DDS file layout (magic, 124 byte header, pixel format, caps; the 20 byte DX10 extension for BC4/BC5 with DXGI formats 80 and 83; FourCC `DXT1`/`DXT5` for BC1/BC3). |
| `Texture/Stream.hpp/.cpp` | The pinhole projection (pixels = size / (distance * 2 tan(fov/2)) * screen height) and the existing `Optimize/MipBudget`. |
| `Texture/Export.hpp/.cpp` | Our own glue over `okay::Image` (load, PNG save). |
| `tools/okaytexture/main.cpp` | Our own command line. |
| `examples/texture/make_samples.py` | Our own procedural sample pictures (checker, fence, bump normal map, icons). No outside assets, so nothing to record in `docs/THIRD_PARTY.md`. |

## Behaviour worth knowing

- **Colour space.** Colour bytes are only treated as sRGB when asked (`--srgb`, "Colour picture"); alpha is always
  linear. Normal maps are never sRGB and never premultiplied.
- **Alpha.** Filtering is weighted by alpha, so a transparent texel's hidden colour never bleeds into its
  neighbours. `--cutout` rescales alpha per level so the share of texels at or above 0.5 equals level 0's.
- **Sizes.** Any size works (1x1, 1xN, odd, non power of two). Level sizes halve rounding down, never below 1.
  Level 0 is returned byte for byte.
- **Compression choice.** `auto` picks BC1 for opaque pictures, BC3 when any alpha is below 255, BC5 for normal
  maps. BC1 can also do 1 bit alpha (`bc1AlphaCutoff`).
- **Streaming.** The planner never holds back a texture that has nothing resident (it would pop in); the upload cap
  limits sharpening only.

## Test results (Linux, hand-built; see `tests/test_tex_*.cpp`)

Numbers in the tests were worked out by hand (box averages, sRGB 0.5 linear = 187.5 -> 188, half-float bit
patterns, BC palettes, DDS header offsets, projected pixels), not copied from the code's output.

| Test | What it covers |
|---|---|
| `tex_mip` | chain sizes, box averages, 3x1 and 5x3 non-power-of-two, sRGB-correct average, premultiplied alpha, filter weights sum to 1 for every size pair, coverage kept, normal-map unit length, zero-vector fallback, hostile input |
| `tex_atlas` | exact fits, padding and bleed gaps, rotation, size limits, 40 random sets (no overlap, inside the sheet, deterministic), composed pixels incl. bleed and rotation, exact manifest text |
| `tex_convert` | all 65536 half floats round-trip, rounding ties, subnormals, 565, 16F, BGRA, R8 luma, premultiply, swizzle, packing, hostile |
| `tex_bc` | decoder by hand (palettes, 3 colour mode, 6 and 8 value BC4), exact round trip for 2-colour blocks, solid blocks, PSNR thresholds (gradient BC1 38.7 dB, soft ball 40.7 dB, noise 13.7 dB), quality never worse with a higher setting, odd sizes, truncated and random data |
| `tex_dds_stream` | DDS header offsets and round trips of every format, truncations and 3000 header bit flips, hand-worked streaming mips, hysteresis, tiling, budget, upload cap |

## Try it

See `docs/try_it/` entry `tex1` (generated into `docs/try_it.md`): the `examples/texture` folder in the Windows
release has `try_texture.bat`, and the editor's Project panel has a "Generate mipmaps / compress" section.
A headless render of a generated mip chain was looked at while writing this (not committed: no screenshots in the repo).

## Not done / unverified

- **Not run on Windows.** The Windows release was not built or run here; the tool, the editor section and the
  example are unverified there.
- **No renderer reads the `.dds` files yet.** Loading DDS into the GL/D3D renderers, honouring the `compress`
  import flag and uploading BC data is the next step. Until then the files are for inspection and for tools.
- BC6H/BC7, ASTC and ETC encoders are not written. BC1 quality is a fast encoder, not an exhaustive one.
- No automatic texture streaming loop in the renderer: `StreamPlanner` is the planning half; a host has to feed it
  instances and do the uploads.
- No `Export` unit test of file I/O in CI beyond the CLI run by hand (`examples/texture/try_texture.bat` commands were
  run on Linux with the same sources).
