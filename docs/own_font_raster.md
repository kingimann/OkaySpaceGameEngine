# Our own font rasteriser: quality, kerning, fallback and the Dear ImGui switch

The engine reads and draws TrueType / OpenType fonts with its own code (`engine/include/okay/Graphics/TtfRaster.hpp`,
`engine/src/Graphics/TtfRaster.cpp`; the atlas wrapper `Graphics/TtfFont`). The game's UI text, OkayUI and the
editor's UI preview already use it. The editor's and launcher's own windows draw text through Dear ImGui, whose font
atlas uses the rasteriser built into Dear ImGui (third-party code). This page covers the work to replace that one:
how ours compares, what was fixed, kerning, font fallback, the switch, speed and memory, and the tests.

Status: **off by default.** `OKAY_OWN_FONT_RASTER=1` turns it on for the editor and launcher. Nothing was run on
Windows or in a real window; see "Not verified" at the end.

## What each file was written from

| File | Written from |
|---|---|
| `engine/src/Graphics/TtfRaster.cpp` (kerning) | The OpenType specification, GPOS chapter (header, FeatureList, LookupList, lookup type 2 pair adjustment formats 1 and 2, lookup type 9 extension, Coverage formats 1 and 2, ClassDef formats 1 and 2, ValueRecord) and the `kern` table chapter (version 0, format 0 subtables, coverage bits) |
| `engine/src/Graphics/TtfRaster.cpp` (overlap rows) | The general non-zero winding scanline method (sorted edge crossings per sample line, winding counted left to right) and the signed-area accumulation the rasteriser already used |
| `shared/OwnImGuiFontLoader.hpp` | The declarations and comments of `ImFontLoader`, `ImFontConfig`, `ImFontBaked`, `ImFontGlyph`, `ImFontAtlasPackAddRect` and `ImFontAtlasBakedSetFontGlyphBitmap` in `imgui.h` and `imgui_internal.h`, plus the core glue that calls a loader (`ImFontBaked_BuildLoadGlyph`, `ImFontAtlasBakedAddFontGlyph`, `ImFontAtlasPackAddRect`) to see which adjustments the core makes. Dear ImGui's own stb_truetype loader and stb_truetype were **not** read. Line metrics (ascent rounded up, descent rounded down) were found by measuring the stock loader's output, not by reading it |
| `tests/test_font_raster_compare.cpp`, `tests/test_ttf_text.cpp`, the new parts of `tests/test_ttf_rasterizer.cpp` | Our own; expected Roboto kerning values from fontTools 4.68.0 (an independent reader, run once on the decoded embedded bytes; not shipped, not a dependency) |

No font assets were added. The two fonts used are the embedded Roboto Medium and JetBrains Mono (licences in
`docs/THIRD_PARTY.md`).

## 1. Quality: ours against Dear ImGui's, headless

`tests/test_font_raster_compare.cpp` draws the same strings through the same Dear ImGui atlas and layout twice, once
with Dear ImGui's loader as the editor and launcher configure it (`OversampleH = OversampleV = 2`, `PixelSnapH`), once
with ours, and composites the glyph quads with bilinear sampling (as a linear-filtered texture is drawn). A third image
is the exact coverage of the same outlines at the same pen positions (rendered at 16 x 16 the resolution and
box-filtered). Strings: "The quick brown fox jumps over the lazy dog", digits, ASCII punctuation, accented Latin
(`àáâãäå çèéêë ñòóôö ùúüÿ ÀÉÎÕÜ Çß œŒ`) and a code line (`if (a != b && c <= d) { x->y[i] = 0x1F; } // 0O 1lI| ;:`).

Run on Linux on 2026-11-16 (levels are 0..255; "mean" is over pixels either image inks):

| Font, size | ours vs exact, mean / worst pixel | Dear ImGui vs exact, mean / worst pixel | ours vs Dear ImGui, mean | line height ours / imgui | ascent ours / imgui | width of all strings ours / imgui |
|---|---|---|---|---|---|---|
| Roboto 12 | 0.49 / 18.3 | 5.75 / 38.7 | 5.47 | 12 / 12 | 10 / 10 | 904 / 904 |
| Roboto 14 | 0.45 / 16.7 | 5.51 / 42.9 | 5.26 | 14 / 14 | 12 / 12 | 1025 / 1025 |
| Roboto 16 | 0.44 / 2.4 | 5.15 / 48.2 | 4.89 | 16 / 16 | 13 / 13 | 1162 / 1162 |
| Roboto 18 | 0.40 / 2.2 | 4.91 / 40.8 | 4.67 | 18 / 18 | 15 / 15 | 1325 / 1325 |
| Roboto 24 | 0.35 / 1.9 | 4.35 / 46.2 | 4.11 | 24 / 24 | 19 / 19 | 1754 / 1754 |
| Roboto 32 | 0.30 / 12.3 | 3.89 / 48.6 | 3.67 | 32 / 32 | 26 / 26 | 2351 / 2351 |
| Roboto 48 | 0.23 / 7.5 | 2.69 / 50.4 | 2.54 | 48 / 48 | 38 / 38 | 3475 / 3475 |
| JetBrains Mono 12 | 0.46 / 39.0 | 4.88 / 46.0 | 4.66 | 12 / 12 | 10 / 10 | 950 / 950 |
| JetBrains Mono 14 | 0.47 / 19.7 | 5.07 / 41.5 | 4.89 | 14 / 14 | 11 / 11 | 1140 / 1140 |
| JetBrains Mono 16 | 0.47 / 6.6 | 5.63 / 55.4 | 5.41 | 16 / 16 | 13 / 13 | 1330 / 1330 |
| JetBrains Mono 18 | 0.46 / 5.0 | 5.81 / 40.6 | 5.64 | 18 / 18 | 14 / 14 | 1520 / 1520 |
| JetBrains Mono 24 | 0.38 / 6.1 | 4.49 / 38.7 | 4.30 | 24 / 24 | 19 / 19 | 2090 / 2090 |
| JetBrains Mono 32 | 0.35 / 7.8 | 3.44 / 49.2 | 3.27 | 32 / 32 | 25 / 25 | 2850 / 2850 |
| JetBrains Mono 48 | 0.27 / 4.8 | 2.98 / 49.8 | 2.81 | 48 / 48 | 38 / 38 | 4180 / 4180 |

Per glyph (every character of those strings alone, all sizes, 1,099 renders per font): ours is 0.36 (Roboto) and
0.39 (JetBrains Mono) levels from exact on average, Dear ImGui's 4.11 and 4.21; the worst single pixel of one glyph
is 17.3 levels for ours (Roboto Ç at 12 px) and 50 to 55 for Dear ImGui's. Every advance width and both line metrics
are equal on the two paths, so switching moves no text. The worst pixels of whole strings (up to 39 levels for ours)
are where two neighbouring glyphs overlap (for example JetBrains Mono's `//`): drawing two glyphs "over" each other is
not the same as covering their union, on both paths.

Dear ImGui's extra blur comes from its oversampling: its glyphs are rasterised at 2 x 2 and drawn a quarter pixel off
the grid (`X0 = 0.25`), so every edge is softened by the texture filter. Ours draws each glyph once at the exact size,
on whole pixels.

The image triples are in the session's `shots/ttf/` folder (`<font>_<string>_<size>.png`, scaled 3x; top row ours,
middle Dear ImGui's, bottom exact). Set `OKAY_FONT_COMPARE_OUT=<folder>` when running `test_font_raster_compare` to
write them again and print the table.

### Problems found and fixed in ours

1. **Overlapping contours were too dark at their edges.** The rasteriser adds up signed areas per pixel, which is exact
   as long as contours do not overlap. Where two contours of the same direction overlap (an accent component touching
   its letter, as in Roboto's ç, or strokes drawn on top of each other, as in JetBrains Mono's 8, m and ß), a pixel
   crossed by edges of both got the sum of the two areas instead of the area of their union. Fix: rows where a sample
   line meets winding 2, or where the running sum goes past 1, are drawn again (with their neighbours) by a non-zero
   winding scanline with 16 sample lines per row and exact horizontal coverage. Before the fix the worst single glyph
   pixel was 58 levels off (JetBrains Mono 8 at 14 px), the worst mean of one glyph 4.4 levels; after, 17.3 and 1.2.
2. **Line metrics.** Our loader puts the baseline at the ascent rounded up and the descent rounded down, as Dear
   ImGui's loader does (measured); with plain rounding the text sat one pixel higher at 14, 17, 18 and 32 px.
3. **OkayUI drew 1:1 glyphs at fractional positions.** At the baked size OkayUI kept the pen's fraction for every
   glyph, so linear filtering blurred most letters by up to half a pixel. Each glyph quad now lands on the whole pixel
   nearest its pen (the pen keeps its fraction, so spacing is unchanged). Together with kerning this changed the text pixels of 18
   OkayUI golden images (`okayui/tests/golden/`, launcher look only); they were regenerated with
   `OKAYUI_UPDATE_GOLDEN=1` after looking at them side by side (only letter spacing and edge sharpness differ).
4. **Speed** (section 5): the outline is now read once per glyph, not two or three times.

Gamma: both rasterisers write linear coverage and both paths blend it the same way, so nothing changes there. No
gamma correction was added; whether text should be blended in linear light is a separate question for the renderers.

## 2. Kerning

`ttf::Face::Kerning(left, right)` returns the pair adjustment in font units: from the GPOS `kern` feature's pair
adjustment lookups (formats 1 and 2, also inside extension lookups; only lookups the `kern` feature lists, the first
subtable that covers a pair decides its lookup, lookups add up; value = the first glyph's XAdvance plus the second
glyph's XPlacement), else from the `kern` table (format 0 horizontal subtables add up, cross-stream and minimum
subtables are skipped, the override bit replaces). `TtfFont::Kerning` scales it; `TtfFont::Measure`, the game's
`UIRenderer` text, `TextRenderer::PixelWidth`, the editor's UI preview and OkayUI (both its atlas text and its fallback
font text) apply it, measuring and drawing alike.

Checked: synthetic fonts with a GPOS table (format 1, format 2 behind an extension lookup, a `liga` lookup that must be
ignored) and with a `kern` table; Roboto's real pairs against fontTools reading the same bytes: AV -77, VA -75,
To -208, Ta -168, Te -89, Yo -65, AT -120, LT -205, AW -39, Wa -33, `P,` -404, `F.` -274, Av -50, LV -206, Ty -82,
AY -150, and 0 for rn, ff, AA, oT (font units of 2048 per em; at 16 px, To is -1.39 px). JetBrains Mono has no
kerning (it is monospaced; its GPOS only positions marks).

Not on the Dear ImGui path: Dear ImGui's text layout has no kerning step, so `OKAY_OWN_FONT_RASTER=1` changes the
pixels of each glyph, not the spacing.

## 3. Fallback

`okay::FontChain` is an ordered list of fonts; each character is taken from the first font that has it
(`FontFor`, `Measure`, `Split` into runs). `BuiltinCodeFont()` and `EmbeddedCodeFontData()` give the embedded
JetBrains Mono next to the existing `BuiltinUIFont()`. Tested: with JetBrains Mono then Roboto, Latin letters come from
JetBrains Mono, a character only Roboto has (the first found is U+0132, Ĳ) from Roboto, a CJK character from neither;
widths add up per font. The older `FontForText` (one font for a whole string) is unchanged.

On the Dear ImGui path, fallback is Dear ImGui's merged fonts (`MergeMode`), which ask each source's loader in turn;
ours answers "not here" for a missing glyph, so the next source is tried. Tested with JetBrains Mono merged before
Roboto on both loaders.

## 4. The switch: `OKAY_OWN_FONT_RASTER=1`

`shared/OwnImGuiFontLoader.hpp` is our implementation of Dear ImGui 1.92's `ImFontLoader` interface (the route Dear
ImGui's own header recommends for custom glyphs; the old custom-rect glyph API is obsolete in 1.92). Dear ImGui's
source is not modified. `okay::imguifont::InstallIfRequested(io.Fonts)` is called right after `ImGui::CreateContext()`
in the editor (`editor/main.cpp`) and in the launcher (`launcher/main.cpp`, main window and the separate chat window),
before any font is added. With the variable set, every glyph of those atlases, including the merged CJK, emoji and
localisation fonts loaded from the system, comes from `ttf::Face`. Without it nothing changes.

How to tell it is on: the editor's Console says "Fonts: OKAY_OWN_FONT_RASTER=1, the editor's text is drawn by
OkaySpace's own TrueType rasteriser"; the launcher writes a line starting `[launcher] OKAY_OWN_FONT_RASTER=1` to
stderr. Dear ImGui's metrics window, where a build shows it, names the loader "OkaySpace TrueType (own)". It is not
part of `OKAY_OWN_STACK=1` and not in the `[okay] stack:` line.

OkayUI already used our rasteriser before this work (`okayui/src/OkayUI_FontAtlas.cpp`), and now kerns.

Sizes of merged fonts: a font merged into another at a different size (the editor merges a CJK font at 16 px into its
18 px UI font) keeps its size relative to the first one, as with Dear ImGui's loader (found by measuring: an earlier
version drew it at the main font's size and a JetBrains Mono glyph merged at 16 into Roboto 18 came out 8 px wide
instead of 7). The test checks advance and baseline of such a glyph against Dear ImGui's loader.

What the loader does not do: colour glyphs (it never did; emoji are one colour on both paths), Dear ImGui's
`RasterizerMultiply` is still applied by Dear ImGui's core after our bitmap is copied, `OversampleH/V` are ignored
(not needed: glyphs are placed on whole pixels and rasterised exactly).

## 5. Speed and memory

Full UI atlas: the editor's faces (Roboto 18 and 22, JetBrains Mono 48) and the launcher's (Roboto 17, JetBrains Mono
16), every ASCII and Latin-1 glyph, 950 glyphs, through the real Dear ImGui atlas. Run with
`OKAY_FONT_COMPARE_BENCH=1 test_font_raster_compare` on a shared, busy 4-core Linux machine (processor time, median of
21, both compiled -O3):

| | Ours | Dear ImGui's | |
|---|---|---|---|
| Bake time | 7.5 ms (fastest 7.1) | 8.1 ms (fastest 7.8) | **met**: about 0.9x (three runs: 0.88, 0.92, 0.96) |
| Peak memory the bake adds (fresh process) | 2.1 MB | 8.2 MB | **met**: a quarter |
| Atlas texture | 512 x 512 | 1024 x 1024 | a quarter (no 2 x 2 oversampling) |
| Instructions (callgrind, one bake) | 55.9 M | 76.5 M | |

Before the speed work ours took about 2.1 times Dear ImGui's time (the overlap fix had added a scanline pass to every
glyph). What was done, from callgrind profiles: the outline is parsed once per glyph (`Face::RenderGlyph`, used by
`BakeGlyph`, `TtfFont` and the loader) instead of two or three times; the winding check uses a coarse edge list
(0.2 px) and per-line crossing lists (4 crossings sorted by a 5-swap network), and only overlapping rows get the
16-line scanline with finely flattened edges; scratch buffers are reused per thread; floor and ceiling of bitmap
coordinates no longer call the maths library; segments inside the glyph box skip the column clipping.
The margin is small and the machine was loaded; treat "faster" as "about the same, slightly faster".

## 6. Tests

| Test | What it checks |
|---|---|
| `ttf_rasterizer` (extended) | GPOS and `kern` parsing on synthetic fonts; Roboto pairs against fontTools; the last 400 truncation lengths of the GPOS font; truncations (every 7 bytes in the first 2 KB, then every 4 KB) and 440 random mutations of the embedded Roboto and JetBrains Mono (kerning exercised too). Also builds standalone under ASan and UBSan (see below) |
| `ttf_text` (new) | `TtfFont::Kerning` and `Measure` with kerning; `FontChain` order, runs, widths, missing characters; golden images of our rasteriser (two strings, 12 to 48 px, `tests/golden/ttf/`, `OKAY_UPDATE_GOLDEN=1` rewrites them, 2 levels of tolerance) |
| OkayUI tests (all 25) | Pass with the regenerated launcher-look goldens |
| `font_raster_compare` (new) | Our loader inside Dear ImGui: equal advances, ascent, descent and line height to Dear ImGui's loader at every size; ours at least as close to exact coverage as Dear ImGui's, under 1 level per glyph on average and under 24 levels at any pixel of a single glyph; merged-font fallback on both loaders; the switch reads the variable. `OKAY_FONT_COMPARE_OUT` and `OKAY_FONT_COMPARE_BENCH` add the pictures, tables and the benchmark |

Sanitizers: `test_ttf_rasterizer` built with `-DOKAY_TTF_STANDALONE` (only `TtfRaster.cpp`, the test and the
header-only font decoder) and `-fsanitize=address,undefined -fno-sanitize-recover=undefined` ran clean (272 checks), and
`test_font_raster_compare` with our loader and Dear ImGui's core all built with ASan and UBSan ran clean (193 checks).

## Not verified

* Windows: nothing here ran on Windows.
* A real window: the editor and launcher with `OKAY_OWN_FONT_RASTER=1` were compiled (syntax-checked) but not run;
  the comparison draws glyph quads with our own compositor, not with SDL's renderer. High-DPI (`RasterizerDensity`
  other than 1) is handled in the code but not tested.
* Kerning in a real OkayUI or game UI window (headless tests only).
* Speed on another machine; the numbers above are from one busy Linux machine.

## Before switching the default

Owner's rule: switch only when it is faster and up to par. Up to par: closer to exact coverage than Dear ImGui's at
every size tested, identical metrics. Faster: by a small margin here, a quarter of the memory. Missing before a flip:
someone looking at the editor and launcher with the variable on, on Windows, at 100% and 150% scaling.
