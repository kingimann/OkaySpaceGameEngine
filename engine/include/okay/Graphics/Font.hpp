#pragma once

namespace okay {

/// A built-in 8x8 monochrome bitmap font (vendored public-domain font8x8) for
/// drawing text without any external font files. Each glyph is an 8x8 grid;
/// Pixel(c, x, y) is true where the character is "on".
struct Font8x8 {
    static constexpr int Width = 8;
    static constexpr int Height = 8;

    /// The 8 row bytes for an ASCII character (row 0 = top). Bit (1 << x) is the
    /// pixel at column x (0 = leftmost). Non-ASCII returns the blank glyph.
    static const unsigned char* Glyph(char c);

    /// Is the pixel at (x, y) within this glyph's 8x8 cell set?
    static bool Pixel(char c, int x, int y) {
        if (x < 0 || y < 0 || x >= Width || y >= Height) return false;
        return (Glyph(c)[y] & (1u << x)) != 0;
    }

    /// Width in pixels of a string at this font's native size (8 px per char).
    static int MeasureWidth(const char* text);
};

} // namespace okay
