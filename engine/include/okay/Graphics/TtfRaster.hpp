#pragma once
// Our own TrueType / OpenType font reader and glyph rasterizer (no third-party code).
//
// Reads the sfnt container (TrueType 0x00010000 / 'true', OpenType 'OTTO', and
// .ttc collections), the tables a text renderer needs (head, hhea, maxp, hmtx, cmap
// formats 0/4/6/12/13) and glyph outlines from either `glyf` (simple and composite
// glyphs, quadratic curves) or `CFF ` (Type 2 charstrings, cubic curves). Outlines
// are filled with exact-area anti-aliased coverage (a signed-area accumulation
// buffer, non-zero winding) and are NOT hinted, so sizes scale smoothly.
//
// The reader never trusts the file: every read is bounds-checked, recursion and
// work are capped, and a corrupt font or glyph yields "no font" / an empty glyph,
// never a crash. The Face does not copy the bytes: they must outlive it.
//
// Not supported (a font using them still loads; the feature is skipped): hinting
// instructions, kerning / GPOS / GSUB, variable fonts (gvar / CFF2), bitmap and
// colour glyph tables (CBDT, sbix, COLR), cmap format 2, the deprecated seac form
// of CFF `endchar`.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace okay {
namespace ttf {

namespace detail { class PathSink; struct PointList; struct CsState; }

class Face {
public:
    /// Fonts in the file: 1 for a plain font, N for a .ttc collection, 0 if the
    /// bytes are not a font container at all.
    static int FaceCount(const std::uint8_t* data, std::size_t size);

    /// Parse the font at `index` (0 for a plain font). `data` must outlive the Face.
    /// False (and !Valid) when it is not a usable font.
    bool Load(const std::uint8_t* data, std::size_t size, int index = 0);
    bool Valid() const { return m_valid; }

    int GlyphCount() const { return m_numGlyphs; }
    int UnitsPerEm() const { return m_upem; }
    bool IsCff() const { return m_cff; }

    /// Glyph index for a Unicode code point; 0 (the "missing" glyph) when the font
    /// has none.
    int FindGlyph(std::uint32_t codepoint) const;

    /// hhea ascent (positive), descent (negative) and line gap, in font units.
    void VMetrics(int& ascent, int& descent, int& lineGap) const {
        ascent = m_asc; descent = m_desc; lineGap = m_gap;
    }
    /// Scale (pixels per font unit) that makes ascent - descent equal `pixelHeight`.
    float ScaleForPixelHeight(float pixelHeight) const;

    /// Advance and left side bearing of a glyph, in font units.
    bool HMetrics(int glyph, int& advance, int& leftBearing) const;

    /// Pixel box of the glyph at `scale` with the pen at the origin: x grows right,
    /// y grows DOWN from the baseline ([x0,x1) x [y0,y1), so y0 is negative above
    /// the baseline). False when the glyph has no outline (space), is out of range,
    /// or the box would exceed 4096 pixels a side; the box is then empty.
    bool GlyphBox(int glyph, float scale, int& x0, int& y0, int& x1, int& y1) const;

    /// Rasterize the glyph into out[row * stride + col], (w x h) being the size of
    /// its GlyphBox. Every pixel of the w x h area is written (0..255 coverage).
    /// False (the area is zeroed) when the glyph cannot be drawn.
    bool Render(int glyph, float scale, std::uint8_t* out, int w, int h, int stride) const;

private:
    struct Table { std::uint32_t off = 0, len = 0; };
    struct CffIndex {
        std::uint32_t count = 0, offSize = 0;
        std::size_t offsets = 0;   // absolute offset of the offset array
        std::size_t data = 0;      // absolute offset of (data start - 1)
        bool Get(const Face& f, std::uint32_t i, std::size_t& start, std::size_t& len) const;
    };
    struct CffFd { CffIndex subrs; bool hasSubrs = false; };

    std::uint32_t U8(std::size_t o) const { return o < m_n ? m_d[o] : 0u; }
    std::uint32_t U16(std::size_t o) const;
    std::uint32_t U32(std::size_t o) const;
    int I16(std::size_t o) const { return (int)(std::int16_t)U16(o); }
    bool Has(std::size_t off, std::size_t len) const { return off <= m_n && len <= m_n - off; }

    bool FindTable(std::uint32_t dir, const char* tag, Table& t) const;
    bool PickCmap();
    int CmapLookup(std::uint32_t cp) const;
    bool LoadCff();
    bool ReadIndex(std::size_t at, CffIndex& idx, std::size_t& end) const;
    bool GlyphData(int glyph, std::size_t& off, std::size_t& len) const;
    bool GlyfPoints(int glyph, int depth, detail::PointList& out, int& budget) const;
    bool Outline(int glyph, detail::PathSink& sink) const;
    bool CffOutline(int glyph, detail::PathSink& sink) const;
    bool CffRun(const CffIndex& local, std::size_t start, std::size_t len, detail::CsState& s, int depth) const;
    bool GlyphBounds(int glyph, float& xmin, float& ymin, float& xmax, float& ymax) const;

    const std::uint8_t* m_d = nullptr;
    std::size_t m_n = 0;
    bool m_valid = false, m_cff = false;
    int m_upem = 0, m_numGlyphs = 0, m_numHM = 0, m_locaLong = 0;
    int m_asc = 0, m_desc = 0, m_gap = 0;
    Table m_head, m_hhea, m_maxp, m_hmtx, m_cmap, m_loca, m_glyf, m_cffT;
    std::size_t m_cmapSub = 0;   // absolute offset of the chosen cmap subtable
    int m_cmapFmt = -1;   // -1: no usable cmap subtable
    bool m_cmapSymbol = false;
    // CFF
    CffIndex m_charStrings, m_gsubrs;
    std::vector<CffFd> m_fds;
    std::size_t m_fdSelect = 0;
    int m_fdSelectFmt = -1;
};

/// Shelf packer for glyph atlases: rows of glyphs left to right, `pad` empty pixels
/// around each, the next row starting below the tallest glyph of the one before.
struct ShelfPacker {
    int w = 0, h = 0, pad = 1;
    int x = 1, y = 1, rowH = 0;
    ShelfPacker() = default;
    ShelfPacker(int width, int height, int padding = 1) : w(width), h(height), pad(padding), x(padding), y(padding) {}
    /// A spot for a gw x gh glyph; false when the atlas is full.
    bool Place(int gw, int gh, int& px, int& py);
    /// First row below everything placed so far.
    int Bottom() const { return rowH > 0 ? y + rowH + pad : y; }
};

/// One glyph placed in an atlas, in pixels of the bake scale: the atlas rect
/// [x0,x1) x [y0,y1), the bitmap's offset from the pen/baseline (yoff is negative
/// above the baseline) and the pen advance.
struct BakedGlyph {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float xoff = 0, yoff = 0, xadvance = 0;
};

/// Rasterize `glyph` at `scale` into a single-channel atlas (size packer.w x
/// packer.h, one byte per pixel; cells are overwritten) at the next free spot.
/// Glyphs with no outline (space) take no room. False when the atlas is full.
bool BakeGlyph(const Face& face, int glyph, float scale, ShelfPacker& packer,
               std::uint8_t* atlas, BakedGlyph& out);

} // namespace ttf
} // namespace okay
