#include "okay/Graphics/TtfFont.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Utf8.hpp"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <memory>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace okay {

namespace {
std::vector<std::string> g_fallbackPaths;
}

struct TtfFont::Impl {
    std::vector<unsigned char> bytes;          // stbtt reads the font from here
    stbtt_fontinfo info{};
    int shelfX = 1, shelfY = 1, shelfH = 0;    // shelf packer cursor (1 px gutters)
    bool full = false;                         // no room left: new glyphs show as '?'
    std::unordered_map<std::uint32_t, Glyph> more;     // non-ASCII glyphs (node-stable)
    std::unordered_map<std::uint64_t, float> kerning;  // pair -> bake-height px
};

TtfFont::TtfFont() : m_impl(std::make_unique<Impl>()) {}
TtfFont::~TtfFont() = default;

bool TtfFont::LoadFromFile(const std::string& path, float bakeHeightPx) {
    m_valid = false;
    // Read the whole font file.
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { Log::Error("[ttf] cannot open ", path); return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); Log::Error("[ttf] empty file ", path); return false; }
    std::vector<unsigned char> data((std::size_t)sz);
    std::size_t got = std::fread(data.data(), 1, (std::size_t)sz, f);
    std::fclose(f);
    if (got != (std::size_t)sz) { Log::Error("[ttf] short read ", path); return false; }
    return Init(std::move(data), bakeHeightPx, path);
}

bool TtfFont::LoadFromMemory(const unsigned char* bytes, std::size_t size, float bakeHeightPx) {
    m_valid = false;
    if (!bytes || size < 16) return false;
    return Init(std::vector<unsigned char>(bytes, bytes + size), bakeHeightPx, "(memory)");
}

bool TtfFont::Init(std::vector<unsigned char> bytes, float bakeHeightPx, const std::string& path) {
    m_valid = false;
    if (bakeHeightPx < 8.0f) bakeHeightPx = 8.0f;
    m_bakeHeight = bakeHeightPx;
    *m_impl = Impl{};
    m_impl->bytes = std::move(bytes);
    const unsigned char* data = m_impl->bytes.data();
    int off = stbtt_GetFontOffsetForIndex(data, 0);
    if (off < 0 || !stbtt_InitFont(&m_impl->info, data, off)) {
        Log::Error("[ttf] not a valid font: ", path);
        return false;
    }
    float scale = stbtt_ScaleForPixelHeight(&m_impl->info, bakeHeightPx);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&m_impl->info, &asc, &desc, &gap);
    m_ascent     = (float)asc * scale;
    m_lineHeight = (float)(asc - desc + gap) * scale;

    // White everywhere, coverage in alpha, so hosts can tint it.
    std::vector<std::uint8_t> rgba((std::size_t)kAtlasSize * kAtlasSize * 4, 255);
    for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 0;
    m_atlas.Set(kAtlasSize, kAtlasSize, std::move(rgba));

    for (int i = 0; i < kCount; ++i)
        if (!Bake((std::uint32_t)(kFirst + i), m_ascii[i])) m_ascii[i] = Glyph{};
    ++m_version;
    m_valid = true;
    return true;
}

bool TtfFont::HasGlyph(std::uint32_t cp) const {
    return m_valid && stbtt_FindGlyphIndex(&m_impl->info, (int)cp) != 0;
}

bool TtfFont::Bake(std::uint32_t cp, Glyph& out) const {
    // The font that has the character: this one, else the first fallback that does.
    const Impl* src = nullptr;
    int gi = stbtt_FindGlyphIndex(&m_impl->info, (int)cp);
    if (gi) src = m_impl.get();
    else {
        auto tryFont = [&](const TtfFont* f) {
            if (src || !f || f == this || !f->m_valid) return;
            if (int g = stbtt_FindGlyphIndex(&f->m_impl->info, (int)cp)) { src = f->m_impl.get(); gi = g; }
        };
        for (const std::string& path : g_fallbackPaths) tryFont(GetFont(path));
        tryFont(BuiltinUIFont());
    }
    if (!src) return false;

    // Sized to this font's bake height, whichever font it comes from.
    const float scale = stbtt_ScaleForPixelHeight(&src->info, m_bakeHeight);
    int adv = 0, lsb = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphHMetrics(&src->info, gi, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(&src->info, gi, scale, scale, &x0, &y0, &x1, &y1);
    const int w = x1 - x0, h = y1 - y0;
    out = Glyph{};
    out.xoff = (float)x0; out.yoff = (float)y0; out.xadvance = (float)adv * scale;
    if (w <= 0 || h <= 0) return true;   // blank (a space): metrics only

    Impl& im = *m_impl;
    if (im.full) return false;
    if (im.shelfX + w + 1 > kAtlasSize) { im.shelfY += im.shelfH + 1; im.shelfX = 1; im.shelfH = 0; }
    if (w + 2 > kAtlasSize || im.shelfY + h + 1 > kAtlasSize) {
        im.full = true;
        Log::Warning("[ttf] glyph atlas full; further new characters show as '?'");
        return false;
    }
    const int ax = im.shelfX, ay = im.shelfY;
    im.shelfX += w + 1;
    if (h > im.shelfH) im.shelfH = h;

    std::vector<unsigned char> gray((std::size_t)w * h);
    stbtt_MakeGlyphBitmap(&src->info, gray.data(), w, h, w, scale, scale, gi);
    std::uint8_t* px = m_atlas.Data();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            px[((std::size_t)(ay + y) * kAtlasSize + (ax + x)) * 4 + 3] = gray[(std::size_t)y * w + x];
    out.x0 = ax; out.y0 = ay; out.x1 = ax + w; out.y1 = ay + h;
    ++m_version;
    return true;
}

const TtfFont::Glyph* TtfFont::GetCodepoint(std::uint32_t cp) const {
    if (!m_valid || cp < 32 || cp == 127) return nullptr;   // controls draw nothing
    if (cp <= (std::uint32_t)kLast) return &m_ascii[cp - kFirst];
    auto it = m_impl->more.find(cp);
    if (it != m_impl->more.end()) return &it->second;
    Glyph g;
    if (!Bake(cp, g)) g = m_ascii['?' - kFirst];   // no font has it, or the atlas is full
    return &m_impl->more.emplace(cp, g).first->second;
}

float TtfFont::Kerning(std::uint32_t left, std::uint32_t right) const {
    if (!m_valid || !left || !right) return 0.0f;
    const std::uint64_t key = ((std::uint64_t)left << 32) | right;
    auto it = m_impl->kerning.find(key);
    if (it != m_impl->kerning.end()) return it->second;
    float k = 0.0f;
    const int a = stbtt_FindGlyphIndex(&m_impl->info, (int)left);
    const int b = stbtt_FindGlyphIndex(&m_impl->info, (int)right);
    if (a && b)
        k = (float)stbtt_GetGlyphKernAdvance(&m_impl->info, a, b) *
            stbtt_ScaleForPixelHeight(&m_impl->info, m_bakeHeight);
    m_impl->kerning.emplace(key, k);
    return k;
}

void TtfFont::Prepare(const char* text) const {
    if (!text || !m_valid) return;
    for (const char* p = text; *p;) GetCodepoint(NextCodepoint(p));
}

float TtfFont::Measure(const char* text, float pixelHeight) const {
    if (!text || !m_valid) return 0.0f;
    float scale = ScaleFor(pixelHeight);
    float maxW = 0.0f, lineW = 0.0f;
    std::uint32_t prev = 0;
    for (const char* p = text; *p;) {
        const std::uint32_t cp = NextCodepoint(p);
        if (cp == '\n') { if (lineW > maxW) maxW = lineW; lineW = 0.0f; prev = 0; continue; }
        if (const Glyph* g = GetCodepoint(cp)) lineW += (g->xadvance + Kerning(prev, cp)) * scale;
        prev = cp;
    }
    if (lineW > maxW) maxW = lineW;
    return maxW;
}

void SetFontFallbacks(const std::vector<std::string>& paths) { g_fallbackPaths = paths; }

// ---- Process-wide cache ------------------------------------------------------
TtfFont* GetFont(const std::string& path) {
    if (path.empty()) return nullptr;
    static std::unordered_map<std::string, std::unique_ptr<TtfFont>> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second.get();    // may be a cached failure (nullptr-valued)
    auto font = std::make_unique<TtfFont>();
    if (!font->LoadFromFile(path)) {
        cache[path] = nullptr;   // remember the failure so we don't re-read every frame
        return nullptr;
    }
    TtfFont* raw = font.get();
    cache[path] = std::move(font);
    return raw;
}

TtfFont* BuiltinUIFont() {
    static std::unique_ptr<TtfFont> font;
    static bool tried = false;
    if (!tried) {
        tried = true;
        static std::vector<unsigned char> bytes;
        if (EmbeddedUIFontData(bytes)) {
            auto f = std::make_unique<TtfFont>();
            if (f->LoadFromMemory(bytes.data(), bytes.size(), 64.0f)) font = std::move(f);
        }
    }
    return font.get();
}

TtfFont* SceneUIFont(const std::string& uiFont) {
    if (uiFont.empty()) return BuiltinUIFont();
    if (uiFont == "pixel") return nullptr;
    TtfFont* f = GetFont(uiFont);
    return f ? f : BuiltinUIFont();   // a missing font file falls back to the built-in one
}

namespace { TtfFont* g_activeUIFont = nullptr; }
void SetActiveUIFont(TtfFont* f) { g_activeUIFont = f; }
TtfFont* ActiveUIFont() { return g_activeUIFont; }

} // namespace okay
