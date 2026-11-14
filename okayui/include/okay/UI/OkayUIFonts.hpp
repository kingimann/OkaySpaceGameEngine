#pragma once
// OkayUI fonts: arbitrary TrueType / OpenType fonts and icon fonts, the OkayUI shape of Dear ImGui's ImFont, ImFontConfig,
// AddFontFrom*, PushFont / PopFont and CalcTextSizeA. Own code (the glyphs come from our own outline rasteriser,
// engine/Graphics/TtfRaster), off by default: nothing changes until a program adds a font and pushes it.
//
//     OkayUI::FontConfig mono;                                    // a code font at 15 px ...
//     OkayUI::FontId code = OkayUI::AddFontFromFile("mono.ttf", 15.0f, &mono);
//     OkayUI::FontConfig icons; icons.merge = true;               // ... with an icon font merged in by code point range
//     icons.ranges = {{0xE000, 0xE0FF}}; icons.glyphMinAdvanceX = 16.0f;
//     OkayUI::AddFontFromFile("icons.ttf", 15.0f, &icons);        // (a merge goes into the font added just before)
//     OkayUI::PushFont(code);  OkayUI::Text("\xEE\x80\x80 main.cpp");  OkayUI::PopFont();
//     OkayUI::PushFont(code, 24.0f);                              // the same font at another size (any size, baked on demand)
//
// What a font is: an ordered list of sources (a face, the code point ranges it answers for, an offset). A character is drawn
// with the first source that has it (so a merged icon font, or a broad font merged after a Latin one, is a fallback); a character
// no source has is drawn as `fallbackChar` ('?'), and as nothing when no source has that either. Sizes are pixels of em
// (ascent minus descent, as Dear ImGui's SizePixels). A font works at any size: each pixel size is rasterised once on first use.
//
// Not covered, said plainly: hinting (outlines are anti-aliased but not grid-fitted), kerning and GPOS/GSUB (so no ligatures, no
// contextual forms), complex scripts (Arabic joining, Indic reordering, right-to-left and bidirectional text), variable fonts,
// colour and bitmap glyphs (emoji draw in one colour or not at all), font collections beyond face 0, and Dear ImGui's
// compressed-base85 font blobs (decode to plain bytes first). Text is laid out left to right, one code point at a time.
#include "okay/UI/OkayUI.hpp"
#include "okay/Graphics/TtfFont.hpp"

#include <string>
#include <vector>

namespace OkayUI {

/// A font handle. 0 is "the UI's own font" (the 8x8 bitmap font or the launcher look's Roboto, whichever is active); fonts added
/// with the calls below are 1, 2, 3 ... A bad id is treated as 0.
typedef int FontId;

struct GlyphRange { unsigned first = 0, last = 0; };   // inclusive code point range

struct FontConfig {
    bool merge = false;                 // add this face to the font added last (ImFontConfig::MergeMode) instead of a new font
    std::vector<GlyphRange> ranges;     // the code points this face answers for; empty: every one it has
    Vec2 glyphOffset{0.0f, 0.0f};       // pixels, added to every glyph of this face (nudge an icon font onto the baseline)
    float glyphMinAdvanceX = 0.0f;      // every glyph of this face steps at least this far (equal-width icons); 0: off
    float glyphMaxAdvanceX = 0.0f;      // ... at most this far; 0: off
    unsigned fallbackChar = '?';        // drawn for a character no source has (a merge may change it for the whole font)
    std::string name;                   // for messages and tests
};

// ---- adding fonts (the bytes are copied; the calls return 0 on a font that does not parse) ----------------------------------
/// `sizePx` is the font's default size (used when PushFont is given no size); it is clamped to 4..512.
FontId AddFontFromMemory(const void* data, std::size_t size, float sizePx, const FontConfig* cfg = nullptr);
FontId AddFontFromFile(const char* path, float sizePx, const FontConfig* cfg = nullptr);
/// The launcher look's own font (Roboto Medium) as a font of its own, so it can be merged with an icon font or used next to a code font.
FontId AddFontBuiltin(float sizePx, const FontConfig* cfg = nullptr);
/// Forget every added font (and the push stack). OkayUI::ResetState calls it.
void ClearFonts();
int  FontCount();
bool FontValid(FontId id);
float FontDefaultSize(FontId id);   // the size it was added with (16 for 0)
int  FontSourceCount(FontId id);    // faces in it (1 + merges)

// ---- using them ---------------------------------------------------------------------------------------------------------------
/// Use `font` for text and widgets until the matching PopFont. `sizePx` is the em height in pixels at the default text scale
/// (Style().textScale 2); 0 uses the font's default size. Window font scale and textScale scale it like any other text. Pushes nest
/// (up to 16 deep, more are ignored); PushFont(0) switches back to the UI's own font for a while.
void PushFont(FontId font, float sizePx = 0.0f);
void PopFont();                      // an unmatched pop does nothing
FontId GetCurrentFont();             // 0 when none is pushed
float  GetCurrentFontSizePx();       // 0 when none is pushed

// ---- measuring and drawing without the stack ----------------------------------------------------------------------------------
/// Size of `text` set in `font` at `sizePx` pixels: width of the longest line, line count times sizePx. '\n' starts a line;
/// `wrapWidth` > 0 wraps at spaces (or inside a word that is too long). nullptr and "" are {0, 0}. Hostile numbers are cleaned.
Vec2 CalcTextSize(FontId font, float sizePx, const char* text, float wrapWidth = 0.0f);
/// True when some source of the font has a glyph for the code point.
bool FontHasGlyph(FontId font, unsigned codepoint);

struct FontGlyphInfo {
    bool  found = false;       // false: no source had it and neither has the fallback character
    bool  usedFallback = false;
    int   source = -1;         // index of the face that supplied it
    float advance = 0;         // pen step in pixels (min / max advance applied)
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // bitmap box relative to the pen on the baseline, y down
    int   w = 0, h = 0;        // bitmap size in pixels (0: a blank glyph such as a space)
};
/// Where a character lands at `sizePx` and, when `coverage` is given, its anti-aliased bitmap (w*h bytes, 0..255).
FontGlyphInfo FontGlyph(FontId font, unsigned codepoint, float sizePx, std::vector<unsigned char>* coverage = nullptr);

/// The texture coordinate of an opaque white texel in the glyph atlas the launcher look draws shapes and text from (ImGui::GetFontTexUvWhitePixel, for custom
/// drawing with textured geometry); {0, 0} while the launcher look is off.
Vec2 GetFontTexUvWhitePixel();

namespace fontsys {   // used by OkayUI's drawing code
struct Placed { okay::TtfFont* face; const okay::TtfFont::Glyph* g; float k; float x, y; };   // pen position, y of the line top
/// Lay `text` out: the glyphs with their pen positions (line top = y), and the size. `out` may be null.
void Layout(FontId font, float sizePx, const char* text, float wrapWidth, std::vector<Placed>* out, Vec2* size);
/// Distance from a line's top to its baseline at `sizePx`.
float Ascent(FontId font, float sizePx);
}

} // namespace OkayUI
