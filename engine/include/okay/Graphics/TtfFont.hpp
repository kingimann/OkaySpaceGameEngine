#pragma once
#include "okay/Graphics/Image.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace okay {

/// A TrueType/OpenType font rasterized (once) into a CPU glyph atlas, so imported
/// .ttf/.otf files can draw real text alongside the built-in 8x8 bitmap font.
///
/// On load the printable ASCII range (32..126) is baked into a single RGBA Image
/// (white with per-pixel coverage in alpha) via stb_truetype. Hosts either blit
/// the atlas on the CPU (the software/player text path) or upload it once as a GPU
/// texture and draw a quad per glyph (the editor's ImGui preview, OkayUI). All
/// glyph metrics are stored at the bake height; callers scale to a target size.
class TtfFont {
public:
    static constexpr int kFirst = 32;    // ' '
    static constexpr int kLast  = 126;   // '~'
    static constexpr int kCount = kLast - kFirst + 1;

    /// One baked glyph: its rect in the atlas (pixels) plus placement metrics, all
    /// in atlas/bake-height space. xoff/yoff position the bitmap relative to the pen
    /// (yoff is from the baseline-top); xadvance is the pen step to the next glyph.
    struct Glyph {
        int   x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // atlas pixel rect [x0,x1) x [y0,y1)
        float xoff = 0, yoff = 0, xadvance = 0;
    };

    /// Bake `path` at `bakeHeightPx` pixels. Returns false (and stays !Valid) if the
    /// file can't be read or doesn't parse as a font.
    bool LoadFromFile(const std::string& path, float bakeHeightPx = 48.0f);

    bool  Valid()       const { return m_valid; }
    float BakeHeight()  const { return m_bakeHeight; }
    const Image& Atlas() const { return m_atlas; }

    /// The baked glyph for an ASCII char, or nullptr if out of range / not baked.
    const Glyph* Get(char c) const {
        unsigned uc = (unsigned char)c;
        if ((int)uc < kFirst || (int)uc > kLast) return nullptr;
        return &m_glyphs[(int)uc - kFirst];
    }

    /// Scale factor from bake-height space to a target cap/pixel height.
    float ScaleFor(float pixelHeight) const {
        return (m_bakeHeight > 0.0f) ? (pixelHeight / m_bakeHeight) : 1.0f;
    }
    /// Width of the longest line of `text` at the given pixel height.
    float Measure(const char* text, float pixelHeight) const;
    /// Distance between successive baselines at the given pixel height.
    float LineHeight(float pixelHeight) const { return m_lineHeight * ScaleFor(pixelHeight); }
    /// Distance from the pen line (glyph top) down to the baseline at this height.
    float Ascent(float pixelHeight) const { return m_ascent * ScaleFor(pixelHeight); }

private:
    bool  m_valid = false;
    float m_bakeHeight = 48.0f;
    float m_lineHeight = 0.0f;   // at bake height
    float m_ascent = 0.0f;       // at bake height
    Image m_atlas;
    Glyph m_glyphs[kCount];
};

/// Process-wide font cache: returns a loaded font for `path` (baked once and reused),
/// or nullptr if it can't be loaded. Empty path returns nullptr. Failures are cached
/// so a missing/bad file isn't retried every frame.
TtfFont* GetFont(const std::string& path);

} // namespace okay
