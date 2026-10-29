#include "okay/Graphics/TtfFont.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Utf8.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <unordered_map>
#include <memory>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace okay {

struct TtfFont::Info { stbtt_fontinfo info{}; float scale = 1.0f; };

namespace {
constexpr int kPad = 1;              // empty pixels around each glyph baked later
constexpr int kWideAtlas = 1024;     // the atlas widens to this when other characters arrive
constexpr int kMaxAtlasH = 4096;     // ... and grows down to this
}

TtfFont::TtfFont() = default;
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
    m_bytes = std::move(data);
    return Bake(m_bytes.data(), m_bytes.size(), bakeHeightPx, path);
}

bool TtfFont::LoadFromMemory(const unsigned char* bytes, std::size_t size, float bakeHeightPx, const std::string& name) {
    m_valid = false;
    if (!bytes || size < 16) return false;
    m_bytes.assign(bytes, bytes + size);
    return Bake(m_bytes.data(), m_bytes.size(), bakeHeightPx, name);
}

bool TtfFont::Bake(const unsigned char* bytes, std::size_t size, float bakeHeightPx, const std::string& path) {
    (void)size;
    m_valid = false;
    m_name = path;
    m_extra.clear(); m_has.clear();
    if (bakeHeightPx < 8.0f) bakeHeightPx = 8.0f;
    m_bakeHeight = bakeHeightPx;

    auto info = std::make_unique<Info>();
    int off = stbtt_GetFontOffsetForIndex(bytes, 0);
    if (off < 0 || !stbtt_InitFont(&info->info, bytes, off)) {
        Log::Error("[ttf] not a valid font: ", path);
        return false;
    }
    float scale = stbtt_ScaleForPixelHeight(&info->info, bakeHeightPx);
    info->scale = scale;
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&info->info, &asc, &desc, &gap);
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
        rows = stbtt_BakeFontBitmap(bytes, off, bakeHeightPx,
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
    // Later glyphs go on shelves below the ASCII rows.
    m_shelfX = kPad; m_shelfY = rows + kPad; m_shelfH = 0;
    m_fullReported = false;
    m_info = std::move(info);
    ++m_generation;
    m_valid = true;
    return true;
}

// A spot for a w x h glyph: on the current shelf, else a new one; the atlas
// widens once and then grows downwards (keeping every glyph where it is).
bool TtfFont::Place(int w, int h, int& x, int& y) const {
    for (;;) {
        const int W = m_atlas.Width(), H = m_atlas.Height();
        if (m_shelfX + w + kPad > W) { m_shelfY += m_shelfH + kPad; m_shelfX = kPad; m_shelfH = 0; }
        if (m_shelfX + w + kPad <= W && m_shelfY + h + kPad <= H) {
            x = m_shelfX; y = m_shelfY;
            m_shelfX += w + kPad;
            m_shelfH = std::max(m_shelfH, h);
            return true;
        }
        int nw = W, nh = H;
        if (W < kWideAtlas) nw = kWideAtlas;
        else if (H < kMaxAtlasH) nh = std::min(H * 2, kMaxAtlasH);
        else return false;
        if (w + kPad * 2 > nw) return false;
        std::vector<std::uint8_t> px((std::size_t)nw * nh * 4, 0);
        for (std::size_t i = 0; i < (std::size_t)nw * nh; ++i) px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 255;
        for (int row = 0; row < H; ++row)
            std::memcpy(px.data() + (std::size_t)row * nw * 4, m_atlas.Data() + (std::size_t)row * W * 4, (std::size_t)W * 4);
        m_atlas.Set(nw, nh, std::move(px));
    }
}

const TtfFont::Glyph* TtfFont::GlyphFor(std::uint32_t cp) const {
    if (cp >= (std::uint32_t)kFirst && cp <= (std::uint32_t)kLast) return &m_glyphs[cp - kFirst];
    if (cp < (std::uint32_t)kFirst || !m_valid || !m_info) return nullptr;
    auto it = m_extra.find(cp);
    if (it != m_extra.end()) return &it->second;
    if (!Has(cp)) return nullptr;
    const stbtt_fontinfo& fi = m_info->info;
    const int gi = stbtt_FindGlyphIndex(&fi, (int)cp);
    const float scale = m_info->scale;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0, adv = 0, lsb = 0;
    stbtt_GetGlyphBitmapBox(&fi, gi, scale, scale, &x0, &y0, &x1, &y1);
    stbtt_GetGlyphHMetrics(&fi, gi, &adv, &lsb);
    Glyph g;
    g.xoff = (float)x0; g.yoff = (float)y0; g.xadvance = (float)adv * scale;
    const int w = x1 - x0, h = y1 - y0;
    if (w > 0 && h > 0) {
        int px = 0, py = 0;
        if (!Place(w, h, px, py)) {
            if (!m_fullReported) {
                m_fullReported = true;
                Log::Warning("[ttf] the glyph atlas of ", m_name, " is full; some characters are not drawn");
            }
            m_has[cp] = false;
            return nullptr;
        }
        std::vector<unsigned char> cov((std::size_t)w * h, 0);
        stbtt_MakeGlyphBitmap(&fi, cov.data(), w, h, w, scale, scale, gi);
        const int W = m_atlas.Width();
        std::uint8_t* dst = m_atlas.Data();
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col)
                dst[(((std::size_t)(py + row) * W) + px + col) * 4 + 3] = cov[(std::size_t)row * w + col];
        g.x0 = px; g.y0 = py; g.x1 = px + w; g.y1 = py + h;
        ++m_generation;
    }
    return &(m_extra[cp] = g);
}

bool TtfFont::Has(std::uint32_t cp) const {
    if (cp <= (std::uint32_t)kLast) return true;   // ASCII is baked at load (controls draw nothing)
    if (!m_valid || !m_info) return false;
    auto it = m_has.find(cp);
    if (it != m_has.end()) return it->second;
    const bool has = stbtt_FindGlyphIndex(&m_info->info, (int)cp) != 0;
    m_has[cp] = has;
    return has;
}

bool TtfFont::HasAll(const std::string& text) const {
    for (std::size_t i = 0; i < text.size();) {
        const std::uint32_t cp = utf8::Next(text, i);
        if (cp > (std::uint32_t)kLast && !Has(cp)) return false;
    }
    return true;
}

void TtfFont::Prepare(const std::string& text) const {
    if (utf8::IsAscii(text)) return;
    for (std::size_t i = 0; i < text.size();) GlyphFor(utf8::Next(text, i));
}

float TtfFont::Measure(const char* text, float pixelHeight) const {
    if (!text || !m_valid) return 0.0f;
    float scale = ScaleFor(pixelHeight);
    float maxW = 0.0f, lineW = 0.0f;
    const char* end = text + std::strlen(text);
    for (const char* p = text; p < end;) {
        const std::uint32_t cp = utf8::Next(p, end);
        if (cp == '\n') { if (lineW > maxW) maxW = lineW; lineW = 0.0f; continue; }
        if (const Glyph* g = GlyphFor(cp)) lineW += g->xadvance * scale;
        else if (cp >= (std::uint32_t)kFirst) lineW += pixelHeight * 0.5f;   // the gap a missing glyph leaves
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

TtfFont* BuiltinUIFont() {
    static std::unique_ptr<TtfFont> font;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::vector<unsigned char> bytes;
        if (EmbeddedUIFontData(bytes)) {
            auto f = std::make_unique<TtfFont>();
            if (f->LoadFromMemory(bytes.data(), bytes.size(), 64.0f, "the built-in UI font")) font = std::move(f);
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

// ---- Glyph fallback ------------------------------------------------------------
const std::vector<std::string>& SystemFallbackFonts() {
    static const std::vector<std::string> found = [] {
        // Fonts the major desktops install, covering Latin, Greek, Cyrillic, CJK and more.
        static const char* const kCandidates[] = {
#if defined(_WIN32)
            "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
            "C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/msgothic.ttc",
            "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simsun.ttc", "C:/Windows/Fonts/malgun.ttf",
            "C:/Windows/Fonts/Nirmala.ttf", "C:/Windows/Fonts/seguisym.ttf",
#elif defined(__APPLE__)
            "/System/Library/Fonts/Supplemental/Arial Unicode.ttf", "/System/Library/Fonts/Helvetica.ttc",
            "/System/Library/Fonts/Hiragino Sans GB.ttc", "/System/Library/Fonts/PingFang.ttc",
            "/System/Library/Fonts/AppleSDGothicNeo.ttc",
#else
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
            "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",
#endif
        };
        std::vector<std::string> out;
        for (const char* c : kCandidates) {
            std::error_code ec;
            if (std::filesystem::exists(std::filesystem::u8path(c), ec)) out.push_back(c);
        }
        return out;
    }();
    return found;
}

TtfFont* FontForText(TtfFont* wanted, const std::string& text) {
    if (utf8::IsAscii(text)) return wanted;   // every font, the 8x8 one too, has ASCII
    if (wanted && wanted->Valid() && wanted->HasAll(text)) return wanted;
    static std::set<std::string> said;
    const std::string from = wanted ? wanted->Name() : std::string("the 8x8 pixel font");
    const std::string lang = Localization::Language();
    const std::string which = lang.empty() ? std::string() : " (" + lang + ")";
    auto use = [&](TtfFont* f) {
        if (said.insert(from + "\x1f" + f->Name()).second)
            Log::Info("Text: ", from, " lacks characters this text", which, " needs; drawing it with ", f->Name());
        return f;
    };
    const std::string langFont = Localization::LanguageFont();
    if (TtfFont* f = GetFont(langFont); f && f != wanted && f->HasAll(text)) return use(f);
    if (TtfFont* f = BuiltinUIFont(); f && f != wanted && f->HasAll(text)) return use(f);
    for (const std::string& path : SystemFallbackFonts())
        if (TtfFont* f = GetFont(path); f && f != wanted && f->HasAll(text)) return use(f);
    // Nothing has every character: say which one, once.
    std::uint32_t missing = 0;
    for (std::size_t i = 0; i < text.size() && !missing;) {
        const std::uint32_t cp = utf8::Next(text, i);
        if (cp > (std::uint32_t)TtfFont::kLast && !(wanted && wanted->Has(cp))) missing = cp;
    }
    std::string ch;
    utf8::Append(ch, missing);
    if (said.insert(from + "\x1fnone\x1f" + ch).second)
        Log::Warning("Text: no font has \"", ch, "\"", which, "; give the language a font in the Localization window");
    return wanted;
}

} // namespace okay
