#include <cstdlib>
#include "okay/Graphics/TtfFont.hpp"
#include "okay/Graphics/TtfRaster.hpp"
#include "okay/UI/UIFontPolicy.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <unordered_map>
#include <memory>

namespace okay {

struct TtfFont::Info { ttf::Face face; float scale = 1.0f; };

namespace {
constexpr int kPad = 1;              // empty pixels around each glyph baked later
constexpr int kWideAtlas = 2048;    // the atlas widens up to this when other characters arrive
constexpr int kMaxAtlasH = 8192;     // ... and grows down to this, then a further page is not required for dialogue text
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
    // A folder opens on some systems and reports a huge size (LONG_MAX on Linux); no font is anywhere near 256 MB.
    if (sz > 256L * 1024L * 1024L) { std::fclose(f); Log::Error("[ttf] not a font file (a folder, or over 256 MB) ", path); return false; }
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
    m_valid = false;
    m_info.reset();   // the old Info points into bytes that may be gone
    m_name = path;
    m_extra.clear(); m_has.clear();
    for (auto& m : m_phased) m.clear();
    if (bakeHeightPx < 8.0f) bakeHeightPx = 8.0f;
    m_bakeHeight = bakeHeightPx;

    auto info = std::make_unique<Info>();
    if (!info->face.Load(bytes, size, 0)) {   // face 0 of a .ttc collection
        Log::Error("[ttf] not a valid font: ", path);
        return false;
    }
    float scale = info->face.ScaleForPixelHeight(bakeHeightPx);
    info->scale = scale;
    int asc = 0, desc = 0, gap = 0;
    info->face.VMetrics(asc, desc, gap);
    m_ascent     = (float)asc * scale;
    m_lineHeight = (float)(asc - desc + gap) * scale;

    // Bake the printable ASCII range into a single-channel coverage atlas. Grow the
    // atlas (up to 4096) if the glyphs don't fit at this bake height.
    ttf::BakedGlyph cdata[kCount];
    int aw = bakeHeightPx <= 24.0f ? 256 : 512, ah = aw;   // small faces need little room (95 glyphs of 12 px fit in 256 x 256); it doubles if they do not fit
    std::vector<unsigned char> gray;
    int rows = -1;
    for (int attempt = 0; attempt < 6 && rows < 0; ++attempt) {   // up to 8192: dialogue text past Latin-1 needs the room
        gray.assign((std::size_t)aw * ah, 0);
        ttf::ShelfPacker packer(aw, ah, kPad);
        bool fit = true;
        for (int i = 0; i < kCount && fit; ++i)   // a missing character bakes the font's "missing" glyph
            fit = ttf::BakeGlyph(info->face, info->face.FindGlyph((std::uint32_t)(kFirst + i)), scale, packer,
                                 gray.data(), cdata[i]);
        if (fit) rows = packer.Bottom();
        else { aw *= 2; ah *= 2; }   // didn't fit: try a bigger atlas
    }
    if (rows <= 0) { Log::Error("[ttf] atlas bake failed: ", path); return false; }

    // Expand coverage -> RGBA (white, alpha = coverage) so hosts can tint it.
    std::vector<std::uint8_t> rgba((std::size_t)aw * ah * 4);
    for (std::size_t i = 0; i < (std::size_t)aw * ah; ++i) {
        rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 255; rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = gray[i];
    }
    if (m_coverageGamma != 1.0f) {   // stem darkening: raise the coverage to a power below 1 (the rgb stays white)
        std::uint8_t lut[256];
        for (int i = 0; i < 256; ++i) lut[i] = (std::uint8_t)std::lround(std::pow(i / 255.0f, m_coverageGamma) * 255.0f);
        for (std::size_t i = 0; i < (std::size_t)aw * ah; ++i) rgba[i * 4 + 3] = lut[rgba[i * 4 + 3]];
    }
    m_atlas.Set(aw, ah, std::move(rgba));

    for (int i = 0; i < kCount; ++i) {
        const ttf::BakedGlyph& b = cdata[i];
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
    const ttf::Face& face = m_info->face;
    const int gi = face.FindGlyph(cp);
    const float scale = m_info->scale;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0, adv = 0, lsb = 0;
    face.GlyphBox(gi, scale, x0, y0, x1, y1);
    face.HMetrics(gi, adv, lsb);
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
        face.Render(gi, scale, cov.data(), w, h, w);
        if (m_coverageGamma != 1.0f)
            for (auto& v : cov) v = (std::uint8_t)std::lround(std::pow(v / 255.0f, m_coverageGamma) * 255.0f);
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

const TtfFont::Glyph* TtfFont::GlyphForPhase(std::uint32_t cp, int phase) const {
    if (phase <= 0 || phase >= kPhases) return GlyphFor(cp);
    if (!m_valid || !m_info) return GlyphFor(cp);
    auto& cache = m_phased[phase - 1];
    auto it = cache.find(cp);
    if (it != cache.end()) return &it->second;
    const Glyph* base = GlyphFor(cp);
    if (!base) return nullptr;
    const ttf::Face& face = m_info->face;
    const int gi = face.FindGlyph(cp);
    const float scale = m_info->scale;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    Glyph g = *base;   // same pen advance and offsets; only the bitmap moves
    if (face.GlyphBox(gi, scale, x0, y0, x1, y1)) {
        const int w = x1 - x0 + 1, h = y1 - y0;
        int px = 0, py = 0;
        if (w > 0 && h > 0 && Place(w, h, px, py)) {
            std::vector<unsigned char> cov((std::size_t)w * h, 0);
            face.Render(gi, scale, cov.data(), w, h, w, (float)phase / (float)kPhases);
            if (m_coverageGamma != 1.0f)
                for (auto& v : cov) v = (std::uint8_t)std::lround(std::pow(v / 255.0f, m_coverageGamma) * 255.0f);
            const int W = m_atlas.Width();
            std::uint8_t* dst = m_atlas.Data();
            for (int row = 0; row < h; ++row)
                for (int col = 0; col < w; ++col)
                    dst[(((std::size_t)(py + row) * W) + px + col) * 4 + 3] = cov[(std::size_t)row * w + col];
            g.x0 = px; g.y0 = py; g.x1 = px + w; g.y1 = py + h;
            ++m_generation;
        } else if (w > 0 && h > 0) {
            return base;   // the atlas is full: the whole-pixel glyph still draws
        }
    }
    return &(cache[cp] = g);
}

bool TtfFont::Has(std::uint32_t cp) const {
    if (cp <= (std::uint32_t)kFirst) return true;   // space and controls draw nothing
    if (!m_valid || !m_info) return false;          // (some CJK fonts have no Latin letters)
    auto it = m_has.find(cp);
    if (it != m_has.end()) return it->second;
    const bool has = m_info->face.FindGlyph(cp) != 0;
    m_has[cp] = has;
    return has;
}

bool TtfFont::HasAll(const std::string& text) const {
    for (std::size_t i = 0; i < text.size();)
        if (!Has(utf8::Next(text, i))) return false;
    return true;
}

void TtfFont::Prepare(const std::string& text) const {
    if (utf8::IsAscii(text)) return;
    for (std::size_t i = 0; i < text.size();) GlyphFor(utf8::Next(text, i));
}

float TtfFont::Kerning(std::uint32_t left, std::uint32_t right, float pixelHeight) const {
    if (!m_valid || !m_info || left < (std::uint32_t)kFirst || right < (std::uint32_t)kFirst) return 0.0f;
    const std::uint64_t key = ((std::uint64_t)left << 32) | right;
    auto it = m_kern.find(key);
    int units;
    if (it != m_kern.end()) units = it->second;
    else {
        units = m_info->face.Kerning(m_info->face.FindGlyph(left), m_info->face.FindGlyph(right));
        m_kern[key] = units;
    }
    if (units == 0) return 0.0f;
    // font units -> pixels at the baked scale, then to the requested height
    return (float)units * m_info->scale * ScaleFor(pixelHeight);
}

float TtfFont::Measure(const char* text, float pixelHeight, bool kerning) const {
    if (!text || !m_valid) return 0.0f;
    float scale = ScaleFor(pixelHeight);
    float maxW = 0.0f, lineW = 0.0f;
    std::uint32_t prev = 0;
    const char* end = text + std::strlen(text);
    for (const char* p = text; p < end;) {
        const std::uint32_t cp = utf8::Next(p, end);
        if (cp == '\n') { if (lineW > maxW) maxW = lineW; lineW = 0.0f; prev = 0; continue; }
        if (kerning && prev) lineW += Kerning(prev, cp, pixelHeight);
        prev = cp;
        if (const Glyph* g = GlyphFor(cp)) lineW += g->xadvance * scale;
        else if (cp >= (std::uint32_t)kFirst) lineW += pixelHeight * 0.5f;   // the gap a missing glyph leaves
    }
    if (lineW > maxW) maxW = lineW;
    return maxW;
}

TtfFont* TtfFont::ForPixelHeight(float pixelHeight) const {
    if (!m_valid || m_bytes.empty()) return const_cast<TtfFont*>(this);
    const int h = UIGlyphBakePixels(pixelHeight);
    if (std::fabs(m_bakeHeight - (float)h) < 0.51f) return const_cast<TtfFont*>(this);
    auto& slot = m_sized[h];
    if (!slot) {
        auto baked = std::make_unique<TtfFont>();
        if (!baked->LoadFromMemory(m_bytes.data(), m_bytes.size(), (float)h, m_name))
            return const_cast<TtfFont*>(this);
        slot = std::move(baked);
    }
    return slot.get();
}

float TtfCoverageGamma(float deviceHeight) {
    float t = (deviceHeight - 9.0f) / 15.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return 0.78f + (1.0f - 0.78f) * t;
}

TtfFont* TtfFont::ForDeviceSize(float deviceHeight, bool darken) const {
    if (!m_valid || m_bytes.empty()) return const_cast<TtfFont*>(this);
    float h;
    // Quarter-pixel steps up to 16 px, half-pixel steps up to 32 (the size error stays under 1.6%, so a glyph is drawn one to one),
    // then the 6% steps of UIGlyphBakePixels. That keeps the number of baked faces near 130 however the zoom is dragged.
    if (deviceHeight <= 16.0f) h = std::max(4.0f, std::round(deviceHeight * 4.0f) / 4.0f);
    else if (deviceHeight <= 32.0f) h = std::round(deviceHeight * 2.0f) / 2.0f;
    else h = (float)UIGlyphBakePixels(deviceHeight);
    const float gamma = darken ? TtfCoverageGamma(h) : 1.0f;
    if (gamma == 1.0f && std::fabs(m_bakeHeight - h) < 0.01f && m_coverageGamma == 1.0f) return const_cast<TtfFont*>(this);
    const int key = (int)std::lround(h * 4.0f) * 2 + (gamma != 1.0f ? 1 : 0) + 1000000;   // apart from ForPixelHeight's whole-pixel keys
    auto& slot = m_sized[key];
    if (!slot) {
        auto baked = std::make_unique<TtfFont>();
        baked->m_coverageGamma = gamma;
        if (!baked->LoadFromMemory(m_bytes.data(), m_bytes.size(), h, m_name)) return const_cast<TtfFont*>(this);
        slot = std::move(baked);
    }
    return slot.get();
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
        // OKAY_NO_SYSTEM_FONTS=1: use no installed font at all (tests that compare pictures must not depend on the machine's fonts)
        if (const char* off = std::getenv("OKAY_NO_SYSTEM_FONTS"); off && off[0] && off[0] != '0') return std::vector<std::string>();
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
    // Installed fonts with Latin letters first, so a language's texts keep one look
    // whether or not they mix in digits and Latin words.
    for (int pass = 0; pass < 2; ++pass)
        for (const std::string& path : SystemFallbackFonts())
            if (TtfFont* f = GetFont(path); f && f != wanted && (pass == 1 || f->Has('A')) && f->HasAll(text)) return use(f);
    // Nothing has every character: say which one, once.
    std::uint32_t missing = 0;
    for (std::size_t i = 0; i < text.size() && !missing;) {
        const std::uint32_t cp = utf8::Next(text, i);
        if (!(wanted ? wanted->Has(cp) : cp <= (std::uint32_t)TtfFont::kLast)) missing = cp;
    }
    std::string ch;
    utf8::Append(ch, missing);
    if (said.insert(from + "\x1fnone\x1f" + ch).second)
        Log::Warning("Text: no font has \"", ch, "\"", which, "; give the language a font in the Localization window");
    return wanted;
}

} // namespace okay
