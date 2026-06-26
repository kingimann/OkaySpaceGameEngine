#include "okay/Graphics/TtfFont.hpp"
#include "okay/Core/Log.hpp"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <memory>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace okay {

bool TtfFont::LoadFromFile(const std::string& path, float bakeHeightPx) {
    m_valid = false;
    if (bakeHeightPx < 8.0f) bakeHeightPx = 8.0f;
    m_bakeHeight = bakeHeightPx;

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

    stbtt_fontinfo info;
    int off = stbtt_GetFontOffsetForIndex(data.data(), 0);
    if (off < 0 || !stbtt_InitFont(&info, data.data(), off)) {
        Log::Error("[ttf] not a valid font: ", path);
        return false;
    }
    float scale = stbtt_ScaleForPixelHeight(&info, bakeHeightPx);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    m_ascent     = (float)asc * scale;
    m_lineHeight = (float)(asc - desc + gap) * scale;

    // Bake the printable ASCII range into a single-channel coverage atlas. Grow the
    // atlas once if the glyphs don't fit at this bake height.
    stbtt_bakedchar cdata[kCount];
    int aw = 512, ah = 512;
    std::vector<unsigned char> gray;
    int rows = -1;
    for (int attempt = 0; attempt < 2; ++attempt) {
        gray.assign((std::size_t)aw * ah, 0);
        rows = stbtt_BakeFontBitmap(data.data(), off, bakeHeightPx,
                                    gray.data(), aw, ah, kFirst, kCount, cdata);
        if (rows > 0) break;
        aw = 1024; ah = 1024;   // didn't fit — try a bigger atlas
    }
    if (rows <= 0) { Log::Error("[ttf] atlas bake failed: ", path); return false; }

    // Expand coverage -> RGBA (white, alpha = coverage) so hosts can tint it.
    std::vector<std::uint8_t> rgba((std::size_t)aw * ah * 4);
    for (std::size_t i = 0; i < (std::size_t)aw * ah; ++i) {
        rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 255; rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = gray[i];
    }
    m_atlas.Set(aw, ah, std::move(rgba));

    for (int i = 0; i < kCount; ++i) {
        const stbtt_bakedchar& b = cdata[i];
        Glyph& g = m_glyphs[i];
        g.x0 = b.x0; g.y0 = b.y0; g.x1 = b.x1; g.y1 = b.y1;
        g.xoff = b.xoff; g.yoff = b.yoff; g.xadvance = b.xadvance;
    }
    m_valid = true;
    return true;
}

float TtfFont::Measure(const char* text, float pixelHeight) const {
    if (!text || !m_valid) return 0.0f;
    float scale = ScaleFor(pixelHeight);
    float maxW = 0.0f, lineW = 0.0f;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { if (lineW > maxW) maxW = lineW; lineW = 0.0f; continue; }
        if (const Glyph* g = Get(*p)) lineW += g->xadvance * scale;
    }
    if (lineW > maxW) maxW = lineW;
    return maxW;
}

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

} // namespace okay
