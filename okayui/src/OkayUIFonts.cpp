// OkayUI fonts (see okay/UI/OkayUIFonts.hpp): font objects made of merged faces, glyph lookup with fallback, layout.
#include "okay/UI/OkayUIFonts.hpp"
#include "OkayUI_FontAtlas.hpp"
#include "okay/Graphics/TtfFont.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace OkayUI {

namespace {

constexpr int kMaxFonts = 64;
constexpr int kMaxSources = 16;
constexpr std::size_t kMaxFontBytes = 64u << 20;
constexpr std::size_t kMaxTextBytes = 1u << 20;
constexpr std::size_t kMaxGlyphs = 200000;

struct Source {
    std::unique_ptr<okay::TtfFont> base;
    std::vector<GlyphRange> ranges;
    Vec2 off{0, 0};
    float minAdv = 0, maxAdv = 0;
    bool Answers(unsigned cp) const {
        if (ranges.empty()) return true;
        for (const GlyphRange& r : ranges) if (cp >= r.first && cp <= r.last) return true;
        return false;
    }
};
struct FontObj {
    std::vector<Source> src;
    float defPx = 16.0f;
    unsigned fallback = '?';
    std::string name;
};
std::vector<std::unique_ptr<FontObj>> g_fonts;

float Clean(float v, float lo, float hi, float dflt) { return std::isfinite(v) ? std::min(hi, std::max(lo, v)) : dflt; }
FontObj* Get(FontId id) { return id >= 1 && id <= (int)g_fonts.size() ? g_fonts[(size_t)id - 1].get() : nullptr; }

struct Resolved { okay::TtfFont* face = nullptr; const okay::TtfFont::Glyph* g = nullptr; float k = 1.0f; int src = -1; bool fb = false; const Source* s = nullptr; };

bool ResolveCp(const FontObj& f, unsigned cp, float px, Resolved& out) {
    for (size_t i = 0; i < f.src.size(); ++i) {
        const Source& s = f.src[i];
        if (!s.Answers(cp) || !s.base->Has(cp)) continue;
        okay::TtfFont* sized = s.base->ForPixelHeight(px);
        const okay::TtfFont::Glyph* g = sized ? sized->GlyphFor(cp) : nullptr;
        if (!g) continue;
        out.face = sized; out.g = g; out.k = px / sized->BakeHeight(); out.src = (int)i; out.s = &s;
        return true;
    }
    return false;
}
bool Resolve(const FontObj& f, unsigned cp, float px, Resolved& out) {
    if (ResolveCp(f, cp, px, out)) return true;
    if (f.fallback != cp && ResolveCp(f, f.fallback, px, out)) { out.fb = true; return true; }
    return false;
}

float AdvanceOf(const Resolved& r) {
    float a = r.g->xadvance * r.k;
    if (r.s->minAdv > 0 && a < r.s->minAdv) a = r.s->minAdv;
    if (r.s->maxAdv > 0 && a > r.s->maxAdv) a = r.s->maxAdv;
    return a;
}
float CenterShift(const Resolved& r) {   // a glyph stretched by a minimum advance is centred in its cell
    const float own = r.g->xadvance * r.k, adv = AdvanceOf(r);
    return adv != own ? (adv - own) * 0.5f : 0.0f;
}

FontId Register(std::unique_ptr<okay::TtfFont> face, float sizePx, const FontConfig* cfg) {
    FontConfig c; if (cfg) c = *cfg;
    Source s;
    s.base = std::move(face);
    for (GlyphRange r : c.ranges) {
        if (r.first > r.last) std::swap(r.first, r.last);
        if (r.first > 0x10FFFF) continue;
        r.last = std::min(r.last, 0x10FFFFu);
        s.ranges.push_back(r);
        if (s.ranges.size() >= 256) break;
    }
    s.off = Vec2{Clean(c.glyphOffset.x, -512, 512, 0), Clean(c.glyphOffset.y, -512, 512, 0)};
    s.minAdv = Clean(c.glyphMinAdvanceX, 0, 1024, 0);
    s.maxAdv = Clean(c.glyphMaxAdvanceX, 0, 1024, 0);
    if (c.merge && !g_fonts.empty()) {
        FontObj& f = *g_fonts.back();
        if ((int)f.src.size() >= kMaxSources) return 0;
        f.src.push_back(std::move(s));
        if (c.fallbackChar != '?') f.fallback = c.fallbackChar;
        return (FontId)g_fonts.size();
    }
    if ((int)g_fonts.size() >= kMaxFonts) return 0;
    auto f = std::make_unique<FontObj>();
    f->defPx = Clean(sizePx, 4, 512, 16);
    f->fallback = c.fallbackChar;
    f->name = c.name;
    f->src.push_back(std::move(s));
    g_fonts.push_back(std::move(f));
    return (FontId)g_fonts.size();
}

} // namespace

FontId AddFontFromMemory(const void* data, std::size_t size, float sizePx, const FontConfig* cfg) {
    if (!data || size < 12 || size > kMaxFontBytes) return 0;
    sizePx = Clean(sizePx, 4, 512, 16);
    auto face = std::make_unique<okay::TtfFont>();
    const float bake = std::min(64.0f, std::max(8.0f, std::floor(sizePx + 0.5f)));
    if (!face->LoadFromMemory((const unsigned char*)data, size, bake, cfg && !cfg->name.empty() ? cfg->name : std::string("(font)"))) return 0;
    return Register(std::move(face), sizePx, cfg);
}

FontId AddFontFromFile(const char* path, float sizePx, const FontConfig* cfg) {
    if (!path || !*path) return 0;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    std::vector<unsigned char> bytes;
    unsigned char buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        bytes.insert(bytes.end(), buf, buf + n);
        if (bytes.size() > kMaxFontBytes) { std::fclose(f); return 0; }
    }
    std::fclose(f);
    FontConfig c; if (cfg) c = *cfg;
    if (c.name.empty()) c.name = path;
    return AddFontFromMemory(bytes.data(), bytes.size(), sizePx, &c);
}

FontId AddFontBuiltin(float sizePx, const FontConfig* cfg) {
    std::vector<unsigned char> bytes;
    if (!okay::EmbeddedUIFontData(bytes)) return 0;
    FontConfig c; if (cfg) c = *cfg;
    if (c.name.empty()) c.name = "Roboto Medium";
    return AddFontFromMemory(bytes.data(), bytes.size(), sizePx, &c);
}

void ClearFonts() { g_fonts.clear(); }
int  FontCount() { return (int)g_fonts.size(); }
bool FontValid(FontId id) { return Get(id) != nullptr; }
float FontDefaultSize(FontId id) { const FontObj* f = Get(id); return f ? f->defPx : 16.0f; }
int  FontSourceCount(FontId id) { const FontObj* f = Get(id); return f ? (int)f->src.size() : 0; }

bool FontHasGlyph(FontId id, unsigned cp) {
    const FontObj* f = Get(id);
    if (!f) return false;
    for (const Source& s : f->src) if (s.Answers(cp) && s.base->Has(cp)) return true;
    return false;
}

namespace fontsys {

float Ascent(FontId id, float px) {
    const FontObj* f = Get(id);
    if (!f || f->src.empty()) return 0.0f;
    px = Clean(px, 1, 1024, 16);
    okay::TtfFont* sized = f->src[0].base->ForPixelHeight(px);
    return sized ? sized->Ascent(px) : 0.0f;
}

void Layout(FontId id, float px, const char* text, float wrap, std::vector<Placed>* out, Vec2* size) {
    if (out) out->clear();
    if (size) *size = Vec2{0, 0};
    const FontObj* f = Get(id);
    if (!f || !text || !*text || !std::isfinite(px) || px <= 0.0f) return;
    px = Clean(px, 1, 1024, 16);
    wrap = (std::isfinite(wrap) && wrap > 0.0f) ? std::min(wrap, 1.0e6f) : 0.0f;
    float x = 0, maxW = 0, spaceAdv = 0;
    int line = 0;
    // the last place a line may break: the glyph index after a space, the pen x after it, the line width before it
    long breakIdx = -1; float breakX = 0, breakW = 0;
    {
        Resolved sp;
        if (Resolve(*f, ' ', px, sp)) spaceAdv = AdvanceOf(sp);
    }
    std::size_t count = 0;
    std::size_t bytes = 0;
    for (const char* p = text; *p && bytes < kMaxTextBytes && count < kMaxGlyphs; ++count) {
        const char* before = p;
        const unsigned cp = OkayUI::fontatlas::NextCodepoint(&p);
        bytes += (std::size_t)(p - before);
        if (cp == '\n') { maxW = std::max(maxW, x); x = 0; ++line; breakIdx = -1; continue; }
        if (cp == '\r' || (cp < 32 && cp != '\t')) continue;
        float adv; Resolved r; bool have = false;
        if (cp == '\t') adv = spaceAdv * 4.0f;
        else if (Resolve(*f, cp, px, r)) { adv = AdvanceOf(r); have = true; }
        else continue;
        if (wrap > 0 && x + adv > wrap && x > 0 && cp != ' ' && cp != '\t') {
            if (breakIdx >= 0) {
                maxW = std::max(maxW, breakW);
                if (out) for (size_t i = (size_t)breakIdx; i < out->size(); ++i) { (*out)[i].x -= breakX; (*out)[i].y += px; }
                x -= breakX; ++line; breakIdx = -1;
            } else { maxW = std::max(maxW, x); x = 0; ++line; }
        }
        if (have && out && r.g->x1 > r.g->x0 && r.g->y1 > r.g->y0)
            out->push_back(Placed{r.face, r.g, r.k, x + CenterShift(r) + r.s->off.x, (float)line * px + r.s->off.y});
        if (cp == ' ' || cp == '\t') { breakW = x; x += adv; breakIdx = out ? (long)out->size() : 0; breakX = x; }
        else x += adv;
    }
    maxW = std::max(maxW, x);
    if (size) *size = Vec2{maxW, (float)(line + 1) * px};
}

} // namespace fontsys

Vec2 CalcTextSize(FontId id, float px, const char* text, float wrap) {
    if (!text || !*text || !std::isfinite(px) || px <= 0.0f) return Vec2{0, 0};
    if (!Get(id)) {   // the UI's own font, scaled to the size asked for
        const Vec2 b = OkayUI::CalcTextSize(text);
        const float h = b.y > 0 ? b.y : 1.0f;
        const float k = Clean(px, 1, 1024, 16) / h;
        return Vec2{b.x * k, Clean(px, 1, 1024, 16)};
    }
    Vec2 s;
    fontsys::Layout(id, px, text, wrap, nullptr, &s);
    return s;
}

FontGlyphInfo FontGlyph(FontId id, unsigned cp, float px, std::vector<unsigned char>* coverage) {
    FontGlyphInfo info;
    if (coverage) coverage->clear();
    const FontObj* f = Get(id);
    if (!f || !std::isfinite(px) || px <= 0) return info;
    px = Clean(px, 1, 1024, 16);
    Resolved r;
    if (!Resolve(*f, cp, px, r)) return info;
    info.found = true; info.usedFallback = r.fb; info.source = r.src;
    info.advance = AdvanceOf(r);
    const float dx = CenterShift(r) + r.s->off.x, dy = r.s->off.y;
    info.w = std::max(0, r.g->x1 - r.g->x0); info.h = std::max(0, r.g->y1 - r.g->y0);
    info.x0 = dx + r.g->xoff * r.k; info.y0 = dy + r.g->yoff * r.k;   // relative to the baseline
    info.x1 = info.x0 + info.w * r.k; info.y1 = info.y0 + info.h * r.k;
    if (coverage && info.w > 0 && info.h > 0) {
        const okay::Image& a = r.face->Atlas();
        coverage->assign((size_t)info.w * info.h, 0);
        for (int y = 0; y < info.h; ++y)
            for (int x = 0; x < info.w; ++x) {
                const int ax = r.g->x0 + x, ay = r.g->y0 + y;
                if (ax < 0 || ay < 0 || ax >= a.Width() || ay >= a.Height()) continue;
                (*coverage)[(size_t)y * info.w + x] = a.Data()[((size_t)ay * a.Width() + ax) * 4 + 3];
            }
    }
    return info;
}

} // namespace OkayUI
