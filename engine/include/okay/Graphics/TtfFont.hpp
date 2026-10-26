#pragma once
#include "okay/Graphics/Image.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <memory>

namespace okay {

/// A TrueType/OpenType font rasterized into a CPU glyph atlas, so imported
/// .ttf/.otf files can draw real text alongside the built-in 8x8 bitmap font.
///
/// The atlas is one fixed-size RGBA Image (white, coverage in alpha). Printable
/// ASCII is baked on load; any other character is baked the first time it is asked
/// for (UTF-8 text, accents, Cyrillic, CJK...), into free space, so glyphs already
/// handed out never move. A character this font doesn't have is drawn from the
/// fallback fonts (SetFontFallbacks, then the built-in UI font) into the same
/// atlas, sized to match. AtlasVersion() changes whenever pixels are added: hosts
/// that upload the atlas as a texture re-upload it when the version moves. Call
/// Prepare(text) before drawing a string so all its glyphs are in the atlas first.
/// All glyph metrics are at the bake height; callers scale to a target size.
class TtfFont {
public:
    static constexpr int kFirst = 32;    // ' '
    static constexpr int kLast  = 126;   // '~'
    static constexpr int kCount = kLast - kFirst + 1;
    // Atlas width and height (pixels): two fit side by side, with gutters, in a
    // 2048 UI batch page.
    static constexpr int kAtlasSize = 1022;

    /// One baked glyph: its rect in the atlas (pixels) plus placement metrics, all
    /// in atlas/bake-height space. xoff/yoff position the bitmap relative to the pen
    /// (yoff is from the baseline, negative = above it); xadvance is the pen step.
    struct Glyph {
        int   x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // atlas pixel rect [x0,x1) x [y0,y1)
        float xoff = 0, yoff = 0, xadvance = 0;
    };

    TtfFont();
    ~TtfFont();
    TtfFont(const TtfFont&) = delete;
    TtfFont& operator=(const TtfFont&) = delete;

    /// Bake `path` at `bakeHeightPx` pixels. Returns false (and stays !Valid) if the
    /// file can't be read or doesn't parse as a font.
    bool LoadFromFile(const std::string& path, float bakeHeightPx = 48.0f);
    /// Bake from font bytes already in memory (e.g. an embedded font). The bytes are copied.
    bool LoadFromMemory(const unsigned char* data, std::size_t size, float bakeHeightPx = 48.0f);

    bool  Valid()       const { return m_valid; }
    float BakeHeight()  const { return m_bakeHeight; }
    const Image& Atlas() const { return m_atlas; }
    /// Changes every time glyphs are added to the atlas.
    unsigned AtlasVersion() const { return m_version; }

    /// The glyph for a code point, baked on first use. A character no font has
    /// shows as '?'. nullptr only for control characters or an invalid font.
    const Glyph* GetCodepoint(std::uint32_t cp) const;
    /// The glyph for a single byte (ASCII; bytes >= 128 are read as Latin-1).
    const Glyph* Get(char c) const { return GetCodepoint((unsigned char)c); }
    /// True when this font itself (not a fallback) has a glyph for `cp`.
    bool HasGlyph(std::uint32_t cp) const;
    /// Extra pen advance between two consecutive characters (kerning), bake-height px.
    float Kerning(std::uint32_t left, std::uint32_t right) const;
    /// Bake every glyph UTF-8 `text` needs, before drawing it.
    void Prepare(const char* text) const;

    /// Scale factor from bake-height space to a target cap/pixel height.
    float ScaleFor(float pixelHeight) const {
        return (m_bakeHeight > 0.0f) ? (pixelHeight / m_bakeHeight) : 1.0f;
    }
    /// Width of the longest line of UTF-8 `text` at the given pixel height (kerned).
    float Measure(const char* text, float pixelHeight) const;
    /// Distance between successive baselines at the given pixel height.
    float LineHeight(float pixelHeight) const { return m_lineHeight * ScaleFor(pixelHeight); }
    /// Distance from the pen line (glyph top) down to the baseline at this height.
    float Ascent(float pixelHeight) const { return m_ascent * ScaleFor(pixelHeight); }

private:
    struct Impl;
    bool  Init(std::vector<unsigned char> bytes, float bakeHeightPx, const std::string& name);
    bool  Bake(std::uint32_t cp, Glyph& out) const;   // rasterize into free atlas space
    bool  m_valid = false;
    float m_bakeHeight = 48.0f;
    float m_lineHeight = 0.0f;   // at bake height
    float m_ascent = 0.0f;       // at bake height
    mutable Image m_atlas;       // glyphs are added lazily, so these change on const reads
    mutable unsigned m_version = 0;
    mutable Glyph m_ascii[kCount];
    std::unique_ptr<Impl> m_impl;
};

/// Fonts consulted, in order, for characters a font lacks (then the built-in UI
/// font). For example a CJK or emoji font. Paths that fail to load are skipped.
void SetFontFallbacks(const std::vector<std::string>& paths);

/// Process-wide font cache: returns a loaded font for `path` (baked once and reused),
/// or nullptr if it can't be loaded. Empty path returns nullptr. Failures are cached
/// so a missing/bad file isn't retried every frame.
TtfFont* GetFont(const std::string& path);

/// The launcher's font (Roboto Medium, built into the engine), baked once.
TtfFont* BuiltinUIFont();
/// The raw bytes of that font (decompressed once). False if unavailable.
bool EmbeddedUIFontData(std::vector<unsigned char>& out);
/// A scene's default UI font (Scene::uiFont): "" = the built-in launcher font,
/// "pixel" = the classic 8x8 bitmap font (nullptr), else a .ttf/.otf path.
TtfFont* SceneUIFont(const std::string& uiFont);
/// The font screen-space text without its own font uses (hosts set it from the
/// scene each frame; nullptr = the 8x8 bitmap font).
void SetActiveUIFont(TtfFont* f);
TtfFont* ActiveUIFont();

} // namespace okay
