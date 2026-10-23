#pragma once
// Internal to OkayUI: the TrueType glyph atlas behind the "launcher look". The
// launcher's own font (Roboto Medium, shared/RobotoFont.h) is baked once at a few
// pixel sizes into one RGBA atlas (white, coverage in alpha) with a solid white
// patch, so flat shapes and text share a single texture and draw in one batch.

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
    int   count = 0;
    Glyph glyphs[256];
    const Glyph* Find(unsigned cp) const;   // binary search; nullptr if not baked
};

struct Atlas {
    bool ok = false;
    int  w = 0, h = 0;
    unsigned char* rgba = nullptr;   // w*h*4
    float whiteU = 0, whiteV = 0;    // a fully opaque texel
    int   nsizes = 0;
    Size  sizes[6];
    /// The baked size to draw `pixelHeight` text with (the smallest that's >= it,
    /// else the largest).
    const Size& Pick(float pixelHeight) const;
};

/// Build (once) and return the atlas; ok == false if the font couldn't be baked.
const Atlas& Get();

/// Next code point from UTF-8 (advances *s; returns 0 at the end).
unsigned NextCodepoint(const char** s);

} // namespace fontatlas
} // namespace OkayUI
