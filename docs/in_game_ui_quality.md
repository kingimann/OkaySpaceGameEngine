# In-game UI quality

The owner's report: "In game ui still need perfecting, still kinda rough and pixelated."
This page says what looked rough (found by drawing a test screen and looking at it, then measuring), what was
already there, what is new, how to see the difference and what is not done. "In-game UI" means the UI that games draw
(buttons, panels, text, images, sliders, the UI library), as the player and the editor Game view show it. It does not
mean the editor's own panels.

Everything below was checked with **headless pictures and numbers from SDL's software renderer on Linux**. Nothing was
run in a real editor or player window, on Windows, on the OpenGL or Direct3D 11 drivers of SDL, or on a high-DPI screen.
Read "Untested" at the end.

## How the pictures were made

`ui_quality_shot out.png <width> <height> [old|fast|good|best] [x y w h zoom]` (target in `tests/`) draws the screen in
`tests/ui_quality_scene.hpp` (a 640x360 reference canvas, "Scale With Screen Size") through the real UI pass,
`RenderSceneUI`, on SDL's software renderer. It was drawn at 640x360 (1x), 960x540 (1.5x), 1280x720 (2x) and 1920x1080
(3x), in the Old look and in the new levels, and the corners, circles, text, shadows and pictures were magnified with
nearest-neighbour (so single pixels can be seen) and looked at. The pictures are not committed (they are written to a
temporary folder); the numbers are in `test_ui_quality`.

## Existing / Gap

| Area | Existing before this change | Gap seen in the pictures (Old look) | What changed (Good, the default) |
|---|---|---|---|
| Rounded corners, pills, circles, borders | Scan-line fills at the device resolution with one soft edge pixel (`FillUIShapeSmooth`, `FillUIRingSmooth`) | A pixel that was more than half covered was drawn fully solid and one under a fifth was dropped, so the "soft" edge was a staircase with a few stray pixels (button corners clearly stepped). The top and bottom of a circle were flat caps: only one sample per row, no vertical coverage. Mean coverage error on a 1.5x circle 0.027, worst 0.43; on a radius-12 corner 0.015 / 0.34 | Exact coverage per device pixel from 4 sub-scan-lines (Good) or 8 (Best), no rounding of edge pixels: circle 0.0012 / 0.066 (Best 0.0004 / 0.008), corner 0.0009 / 0.031 (Best 0.0003 / 0.008) |
| Shapes and borders at a fractional canvas scale | Drawn at `rect x scale`, so at 1.5x a 15 px card started half way into a device pixel and a 1 px border was 1.5 px wide | Uneven, blurred borders and edges at 1.25x, 1.5x, 1.75x | Rect edges and border widths snap to whole device pixels (`SnapToDevice`): a 1 px border at 1.5x is exactly 2 device pixels, nothing in between |
| Text: placement | Glyph quads at fractional positions, with the face baked at a whole pixel size | On a GPU driver a quad at a fractional position is resampled by the linear filter (soft stems); the whole-pixel bake size meant a 3 to 4 percent resample at fractional sizes; letters drifted ("Pl ay") | Faces baked at quarter-pixel sizes (one atlas pixel = one screen pixel, no resample), every glyph quad on a whole device pixel (Fast, Good) or on a quarter-pixel phase of a pre-rendered glyph (Best) |
| Text: weight | Plain exact-area coverage | Small text (9 to 14 px) looked thin | Stem darkening: coverage raised to a power that is 0.78 at 9 px and 1 at 24 px (`TtfCoverageGamma`): +13 to +16 percent ink on `l e o` at 10 px |
| Text: spacing | Advance widths only | No kerning: "AV", "To", "Wa" had holes | Pair kerning read from the font's `kern` table and OpenType `GPOS` kern feature (formats 1 and 2, extension lookups): AV -3.2 px, To -8.7 px at 100 px, unrelated pairs 0 |
| Soft shadows | Five stacked, hard-edged copies with whole-pixel growth | Visible rings (the card shadow looked like concentric steps); biggest step between neighbouring pixels 0.118 | One mesh of 10 outline rings with the alpha of a Gaussian-blurred edge (`0.5 erfc(d / (sigma sqrt 2))`), blended by the renderer: worst error against the Gaussian 0.026, biggest step 0.063, monotone |
| Gradients | One flat colour per row (8-bit steps) | Visible bands on slow gradients (40 to 46 over 60 rows = 7 flat strips) | Best: ordered (Bayer 4x4) dither per pixel on areas up to 30,000 device pixels; the row means follow the ramp within 0.14 level and 33 of 52 rows mix two levels. Good keeps flat rows |
| Pictures shrunk | Linear sampling at any size | A 128 px picture with fine detail shown at 36 px skipped texels (a 1 px checker flickers, mean error against the area average 36 levels) | A copy filtered to the drawn size (area average in linear light, `okay::tex` Mip.hpp, `UIDownscaleImage`) is drawn instead when the picture is shrunk by 25 percent or more: mean error 2.7 levels (Best at 128 to 61 px: 7.8, was 33) |
| Pixel-art pictures | One scene-wide switch (`uiFont "pixel"`) | n/a | Unchanged: the pixel look still samples nearest and draws hard edges (test: identical to the Old look) |
| Canvas scaling | Already drawn at physical resolution (`g_uiDeviceScale`, faces baked at logical size x scale) | n/a (this was already done by `claude/ui-crisp-1`) | Unchanged, plus the snapping above |
| Blending space | SDL / GPU blend in sRGB | Light text on dark looks a little heavier than dark text on light | Not changed (see "Not done") |
| Turned or scaled widgets (UI Frame) | Rectangle runs sent as geometry | Hard, stair-stepped outer edge (seen in the pictures) | Not changed (see "Not done") |

## The quality setting

Scene setting `uiQuality` (saved as a `uiquality N` line; a scene without the line is Good):

- **Fast (0)**: text snapped to whole device pixels; everything else as in the Old look.
- **Good (1, the default)**: Fast plus stem darkening, kerning, exact edge coverage (4 sub-scan-lines), pixel-snapped
  shapes and borders, soft shadow mesh, filtered picture downscale.
- **Best (2)**: Good plus quarter-pixel text phases, 8 sub-scan-lines, gradient dithering.
- **Old look (3)**: the drawing from before this change, for comparison. The golden-image test scenes
  (`tests/golden/ui`) are pinned to it and still match pixel for pixel.

Where to set it: the editor's **Lighting** window, **UI Font** header, **UI Quality** (saved with the scene, so the built
game matches). Without opening anything: the environment variable `OKAY_CLASSIC_UI=1` forces the Old look, and
`OKAY_UI_QUALITY=fast|good|best|old` forces a level, in the editor, the player and an exported game. The variable wins over
the scene. A project-wide default in Project Settings or Build Settings is not built (see "Not done").

## Where the code is

- `engine/src/UI/UIRenderer.cpp`: `EmitMulti`, `SnapToDevice`, `FillUIShadowSoft`, `PackCovDither`, the glyph placement in `DrawTtfText`, the picture downscale choice, `g_uiQuality`.
- `engine/include/okay/UI/UIFontPolicy.hpp`: `UIQuality`, `UIQualityFor` (environment names).
- `engine/include/okay/UI/UIImageScale.hpp`, `engine/src/UI/UIImageScale.cpp`: the size rules and the filtered copy; `Painter::LoadTextureSized` (SDL painter and `BatchPainter` implement it; other painters fall back to the full-size picture).
- `engine/src/Graphics/TtfFont.cpp`, `TtfRaster.cpp`: `ForDeviceSize`, `GlyphForPhase`, `TtfCoverageGamma`, `Face::Kerning`, `Face::Render` with a sub-pixel shift.
- `engine/include/okay/Scene/Scene.hpp` and `SceneSerializer.cpp`: the `uiQuality` setting.

Written from: the OpenType specification (`kern`, `GPOS` header, feature and lookup lists, coverage, class definitions, pair
adjustment formats 1 and 2), the definition of the complementary error function for the shadow profile, the Bayer ordered
dither matrix, and the box and Lanczos filter definitions already in `okay::tex`. No code from another project.

## Tests

`test_ui_quality` (180 checks, no window needed): glyph weight, exact face sizes, sub-pixel phases (ink moves by k/4),
kerning pairs, picture size rules and filter against an area average in linear light, shadow profile against the
Gaussian, border snapping at 1.5x, circle and rounded-corner coverage against a 16x16 supersampled analytic shape,
glyph quads on the pixel grid, quarter-pixel text positions, gradient dither, the setting (saved, defaulted, environment
names), the batched path against the direct one, and a draw time per level. `test_ui_renderer_golden` (Old look pinned),
`test_ui_crisp`, `test_ui_render_bench` (draw calls, no allocation in steady state, time) still pass.

## Not done / untested

- **Untested**: Windows, a real editor or player window, SDL's OpenGL and Direct3D 11 drivers, the OpenGL and Direct3D 11
  3D renderers, high-DPI screens, the Xbox soft painter. The UI pass is one code path for all of them (it draws through
  `ui2d::Painter`), so they get the same geometry, but only SDL's software renderer was run. On that renderer the Old look's
  glyph positions were already cut to whole pixels by the renderer itself, so the sharper text mainly shows on the GPU
  drivers (unverified there); the quad-on-grid test proves where the quads are sent.
- Turned or scaled widgets (UI Frame) still have a hard stair-stepped outer edge: fixing it needs offscreen rendering or
  multi-sampling in the painter backends.
- Blending in linear light (gamma-correct) is not done: the renderers blend in sRGB. Stem darkening is a fixed
  curve, not dependent on the text colour.
- No glyph hinting (outlines are scaled exactly), no sub-pixel (LCD) text, no SDF text.
- Nine-slice panels still copy integer rects; at fractional scales a seam between pieces is possible (not seen in the pictures).
- Gradients bigger than 30,000 device pixels are not dithered; rectangle shapes with a gradient are dithered only in Best.
- "Blur behind" is not built.
- The quality setting is per scene. There is no project-wide default in Project Settings or Build Settings (use the
  environment variable, or set the scene).
- Kerning applies to the TrueType faces (the built-in Roboto and any .ttf/.otf the scene names); the 8x8 pixel font is unchanged.
- Performance: see the numbers below; they come from an unoptimised (`-O0`) build and are only good for comparing the levels with each other.
