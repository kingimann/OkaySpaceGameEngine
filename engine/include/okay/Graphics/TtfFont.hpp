#pragma once
#include "okay/Graphics/Image.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <unordered_map>

namespace okay {

/// A TrueType/OpenType font rasterized into a CPU glyph atlas, so imported
/// .ttf/.otf files can draw real text alongside the built-in 8x8 bitmap font.
///
/// On load the printable ASCII range (32..126) is baked into a single RGBA Image
/// (white with per-pixel coverage in alpha) via stb_truetype; any other character
/// (accented letters, Cyrillic, CJK...) is baked into the same atlas the first time
/// it is asked for, and the atlas grows when it fills. Hosts either blit the atlas
/// on the CPU (the software/player text path) or upload it as a GPU texture and
/// draw a quad per glyph (the editor's ImGui preview, OkayUI), uploading again
/// when Generation() changes. All glyph metrics are stored at the bake height;
/// callers scale to a target size.
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

    TtfFont();
    ~TtfFont();
    TtfFont(const TtfFont&) = delete;
    TtfFont& operator=(const TtfFont&) = delete;

    /// Bake `path` at `bakeHeightPx` pixels. Returns false (and stays !Valid) if the
    /// file can't be read or doesn't parse as a font.
    bool LoadFromFile(const std::string& path, float bakeHeightPx = 48.0f);
    /// Bake from font bytes already in memory (e.g. an embedded font; copied).
    /// `name` is what messages call it.
    bool LoadFromMemory(const unsigned char* data, std::size_t size, float bakeHeightPx = 48.0f,
                        const std::string& name = "(memory)");

    bool  Valid()       const { return m_valid; }
    float BakeHeight()  const { return m_bakeHeight; }
    const Image& Atlas() const { return m_atlas; }
    /// Changes whenever glyphs are added to the atlas: a host that uploaded Atlas()
    /// uploads it again.
    std::uint32_t Generation() const { return m_generation; }
    /// The file the font came from ("(memory)" for an embedded one), for messages.
    const std::string& Name() const { return m_name; }

    /// The baked glyph for an ASCII char, or nullptr if out of range / not baked.
    const Glyph* Get(char c) const {
        unsigned uc = (unsigned char)c;
        if ((int)uc < kFirst || (int)uc > kLast) return nullptr;
        return &m_glyphs[(int)uc - kFirst];
    }
    /// The glyph for any code point, baked on first use; nullptr when the font has
    /// no glyph for it (or its atlas is full).
    const Glyph* GlyphFor(std::uint32_t cp) const;
    /// True when the font has a glyph for `cp`.
    bool Has(std::uint32_t cp) const;
    /// True when the font has every visible character of UTF-8 `text`.
    bool HasAll(const std::string& text) const;
    /// Bake the glyphs `text` needs now, so a host uploads the atlas once before drawing it.
    void Prepare(const std::string& text) const;

    /// Scale factor from bake-height space to a target cap/pixel height.
    float ScaleFor(float pixelHeight) const {
        return (m_bakeHeight > 0.0f) ? (pixelHeight / m_bakeHeight) : 1.0f;
    }
    /// Width of the longest line of UTF-8 `text` at the given pixel height.
    float Measure(const char* text, float pixelHeight) const;
    /// Distance between successive baselines at the given pixel height.
    float LineHeight(float pixelHeight) const { return m_lineHeight * ScaleFor(pixelHeight); }
    /// Distance from the pen line (glyph top) down to the baseline at this height.
    float Ascent(float pixelHeight) const { return m_ascent * ScaleFor(pixelHeight); }

private:
    struct Info;   // stb_truetype's font info over m_bytes
    bool  Bake(const unsigned char* bytes, std::size_t size, float bakeHeightPx, const std::string& name);
    bool  Place(int w, int h, int& x, int& y) const;
    bool  m_valid = false;
    float m_bakeHeight = 48.0f;
    float m_lineHeight = 0.0f;   // at bake height
    float m_ascent = 0.0f;       // at bake height
    std::string m_name;
    std::vector<unsigned char> m_bytes;    // the font file, for glyphs baked later
    std::unique_ptr<Info> m_info;
    Glyph m_glyphs[kCount];
    // Glyphs baked after load share the atlas: a shelf packer below the ASCII rows.
    mutable Image m_atlas;
    mutable std::unordered_map<std::uint32_t, Glyph> m_extra;
    mutable std::unordered_map<std::uint32_t, bool> m_has;   // cached "does the font have it"
    mutable int m_shelfX = 0, m_shelfY = 0, m_shelfH = 0;
    mutable std::uint32_t m_generation = 0;
    mutable bool m_fullReported = false;
};

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

/// The font to draw UTF-8 `text` with: `wanted` (nullptr = the 8x8 bitmap font,
/// which only has ASCII) when it has every character, else the first font that
/// does: the current language's font (Localization::LanguageFont), the built-in
/// UI font, then a system font for that script. Each switch is logged once.
/// When no font has them all, `wanted` stays (missing characters are skipped).
TtfFont* FontForText(TtfFont* wanted, const std::string& text);
/// Installed fonts the fallback looks at, best first (the ones that exist here).
const std::vector<std::string>& SystemFallbackFonts();

} // namespace okay
