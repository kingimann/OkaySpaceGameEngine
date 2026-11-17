#pragma once
// Internal to OkayUI: the TrueType glyph atlas behind the "launcher look". The
// launcher's own font (Roboto Medium, shared/RobotoFont.h) is baked once at a few
// pixel sizes into one RGBA atlas (white, coverage in alpha) with a solid white
// patch, so flat shapes and text share a single texture and draw in one batch.
// The code font (JetBrains Mono, shared/JetBrainsMonoFontOwn.h: the face the
// script editor's ImGui widget uses) is baked into the same atlas, at the same
// sizes, for OkayUI::PushCodeFont (the code editor widget).

struct SDL_Renderer;
struct SDL_Texture;
namespace okay { class TtfFont; }

namespace OkayUI {
namespace fontatlas {

struct Glyph {
    unsigned cp = 0;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // quad offsets from the pen at the baseline (bake px)
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;   // atlas UVs
    float advance = 0;                      // pen step (bake px)
};

struct Size {
    float px = 0;          // bake height (ascent - descent)
    float ascent = 0;      // pen-top to baseline (bake px)
    float scale = 0;       // bake px per font unit (for Kerning)
    int   count = 0;
    Glyph glyphs[256];
    short ascii[128] = {};                   // glyphs[] index of each ASCII code point, -1 when not baked (filled with the glyphs)
    const Glyph* Find(unsigned cp) const;   // ASCII: a table; others: binary search; nullptr if not baked
};

/// The code font can be baked at more sizes than the UI font: the script editor shows code at 16 px x UI scale (0.7 to 2) x
/// zoom (0.7 to 3), so 7.8 to 96 px, and text should only be drawn from a bake at least as large (a downscale, never a blurry
/// upscale). The first atlas has the UI font's six sizes for both fonts (what every program needs); the first time code is
/// drawn larger than the largest of them, the atlas grows (between two frames) to the code font's nine sizes.
constexpr int kUiSizes = 12, kCodeSizes = 9, kBaseCodeSizes = 6;   // UI: the launcher look plus the Dear ImGui-matched page sizes

struct Atlas {
    bool ok = false;
    int  w = 0, h = 0;
    unsigned char* rgba = nullptr;   // w*h*4
    float whiteU = 0, whiteV = 0;    // a fully opaque texel
    int   nsizes = 0;
    Size  sizes[kUiSizes];
    int   ncode = 0;     // the code font's sizes (0: it could not be baked; the UI font stands in)
    Size  code[kCodeSizes];
    // how it was built
    int   usedRows = 0;  // rows the glyphs took (the packer's bottom), out of h - 4 (the white patch)
    int   grows = 0;     // how many times the height was doubled to fit the code font
    double buildMs = 0;  // time to rasterise and pack
    /// The baked size to draw `pixelHeight` text with (the smallest that's >= it,
    /// else the largest).
    const Size& Pick(float pixelHeight) const;
    /// The same for the code font (the UI font's size when the code font is missing). Asking for code larger than the largest
    /// bake while the atlas does not have every code size yet asks for the atlas to grow (see Grow).
    const Size& PickCode(float pixelHeight) const;
};

/// Build (once) and return the atlas; ok == false if the font couldn't be baked.
const Atlas& Get();
/// Build an atlas into `out` (for tests): `startH` rows high, doubled while the code font (`codeSizes` of its sizes) does not
/// fit, up to `maxH`; when it still does not fit, the atlas has the UI font only (ncode 0). Free it with Free.
void BuildInto(Atlas& out, int startH, int maxH, int codeSizes = kCodeSizes);
/// Called between frames (OkayUI::BeginFrame): when larger code was asked for, rebuild the atlas with every code size. The UI
/// font's glyphs keep their texel places; the atlas may be taller (UVs change). True when it changed (the caller re-uploads it).
bool Grow();
void Free(Atlas& a);

/// Pair kerning between two code points of the atlas font, in font units (multiply by
/// Size::scale for bake px); from the font's GPOS / kern data, 0 when it has none.
int KerningUnits(unsigned left, unsigned right);

/// Next code point from UTF-8 (advances *s; returns 0 at the end).
unsigned NextCodepoint(const char** s);

/// A string with characters the atlas lacks (Cyrillic, Greek, CJK...) is drawn with
/// the engine font okay::FontForText picks for it (the language's font, the built-in
/// UI font, an installed one; each switch logged once). nullptr when the atlas has
/// every character.
okay::TtfFont* FallbackFont(const char* s, bool code = false);   // code: checked against the code font
/// That font's glyph atlas as a texture on `r` (uploaded again when it grows).
SDL_Texture* FallbackTexture(SDL_Renderer* r, okay::TtfFont* f);

} // namespace fontatlas
} // namespace OkayUI
