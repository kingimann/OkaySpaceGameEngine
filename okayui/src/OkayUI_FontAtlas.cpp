#include "OkayUI_FontAtlas.hpp"
#include "stb_truetype.h"   // the implementation is compiled into the engine (TtfFont.cpp)
#include "okay/Graphics/TtfFont.hpp"   // EmbeddedUIFontData: the launcher's font (Roboto)
#include "okay/UI/UIPainterSDL.hpp"    // SDLFontAtlas

#include <cstdlib>
#include <cstring>

namespace OkayUI {
namespace fontatlas {
namespace {

// The code points baked: ASCII, Latin-1, and the punctuation / arrows UI text uses.
int BuildCodepoints(int* out) {
    int n = 0;
    for (int c = 32; c <= 126; ++c) out[n++] = c;
    for (int c = 160; c <= 255; ++c) out[n++] = c;
    const int extra[] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026,
                         0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x25B2, 0x25B6, 0x25BC, 0x25C0};
    for (int c : extra) out[n++] = c;
    return n;
}

Atlas g_atlas;
bool  g_built = false;

void Build() {
    g_built = true;
    static std::vector<unsigned char> bytes;   // stbtt reads the font in place: keep it alive
    if (!okay::EmbeddedUIFontData(bytes)) return;
    const unsigned char* ttf = bytes.data();
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) return;

    int all[256]; const int nall = BuildCodepoints(all);
    int cps[256]; int n = 0;
    for (int k = 0; k < nall; ++k) if (stbtt_FindGlyphIndex(&info, all[k])) cps[n++] = all[k];

    const float sizes[] = {13.0f, 16.0f, 20.0f, 26.0f, 34.0f, 46.0f};
    const int nsizes = (int)(sizeof(sizes) / sizeof(sizes[0]));
    const int W = 1024, H = 1024, packH = H - 4;   // bottom rows: solid white patch
    unsigned char* alpha = (unsigned char*)std::calloc((size_t)W * H, 1);
    stbtt_packedchar* packed = (stbtt_packedchar*)std::calloc((size_t)n * nsizes, sizeof(stbtt_packedchar));
    if (!alpha || !packed) { std::free(alpha); std::free(packed); return; }
    stbtt_pack_context pc;
    bool ok = stbtt_PackBegin(&pc, alpha, W, packH, W, 1, nullptr) != 0;
    if (ok) {
        stbtt_PackSetOversampling(&pc, 1, 1);   // drawn at bake size: pixel-exact and crisp
        stbtt_pack_range ranges[6];
        for (int s = 0; s < nsizes; ++s) {
            ranges[s].font_size = sizes[s];
            ranges[s].first_unicode_codepoint_in_range = 0;
            ranges[s].array_of_unicode_codepoints = cps;
            ranges[s].num_chars = n;
            ranges[s].chardata_for_range = packed + (size_t)s * n;
        }
        ok = stbtt_PackFontRanges(&pc, ttf, 0, ranges, nsizes) != 0;
        stbtt_PackEnd(&pc);
    }
    if (ok) {
        for (int y = H - 4; y < H; ++y) std::memset(alpha + (size_t)y * W, 255, (size_t)W);
        int asc = 0, desc = 0, gap = 0;
        stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
        Atlas& a = g_atlas;
        a.w = W; a.h = H; a.nsizes = nsizes;
        a.whiteU = 2.5f / W; a.whiteV = (H - 2.0f) / H;
        for (int s = 0; s < nsizes; ++s) {
            Size& sz = a.sizes[s];
            sz.px = sizes[s];
            sz.ascent = asc * stbtt_ScaleForPixelHeight(&info, sizes[s]);
            sz.count = n;
            for (int k = 0; k < n; ++k) {
                float px = 0.0f, py = 0.0f;
                stbtt_aligned_quad q;
                stbtt_GetPackedQuad(packed + (size_t)s * n, W, H, k, &px, &py, &q, 0);
                Glyph& g = sz.glyphs[k];
                g.cp = (unsigned)cps[k];
                g.x0 = q.x0; g.y0 = q.y0; g.x1 = q.x1; g.y1 = q.y1;
                g.u0 = q.s0; g.v0 = q.t0; g.u1 = q.s1; g.v1 = q.t1;
                g.advance = packed[(size_t)s * n + k].xadvance;
            }
        }
        a.rgba = (unsigned char*)std::malloc((size_t)W * H * 4);
        if (a.rgba) {
            for (int p = 0; p < W * H; ++p) {
                a.rgba[p * 4 + 0] = 255; a.rgba[p * 4 + 1] = 255; a.rgba[p * 4 + 2] = 255; a.rgba[p * 4 + 3] = alpha[p];
            }
            a.ok = true;
        }
    }
    std::free(alpha); std::free(packed);
}

} // namespace

const Glyph* Size::Find(unsigned cp) const {   // code points are baked in ascending order
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (glyphs[mid].cp == cp) return &glyphs[mid];
        if (glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

const Size& Atlas::Pick(float pixelHeight) const {
    for (int s = 0; s < nsizes; ++s) if (sizes[s].px >= pixelHeight - 0.5f) return sizes[s];
    return sizes[nsizes > 0 ? nsizes - 1 : 0];
}

const Atlas& Get() {
    if (!g_built) Build();
    return g_atlas;
}

okay::TtfFont* FallbackFont(const char* s) {
    const Atlas& a = Get();
    if (!a.ok || !s) return nullptr;
    const Size& z = a.Pick(16.0f);
    bool lacks = false;
    for (const char* p = s; *p && !lacks;) {
        const unsigned cp = NextCodepoint(&p);
        lacks = cp >= 0x80 && !z.Find(cp);
    }
    if (!lacks) return nullptr;
    okay::TtfFont* f = okay::FontForText(okay::BuiltinUIFont(), s);
    return f && f->Valid() ? f : nullptr;
}

SDL_Texture* FallbackTexture(SDL_Renderer* r, okay::TtfFont* f) { return okay::ui2d::SDLFontAtlas(r, f); }

unsigned NextCodepoint(const char** sp) {
    const unsigned char* s = (const unsigned char*)*sp;
    if (!*s) return 0;
    unsigned c = *s++;
    int extra = 0;
    if (c >= 0xF0) { c &= 0x07; extra = 3; }
    else if (c >= 0xE0) { c &= 0x0F; extra = 2; }
    else if (c >= 0xC0) { c &= 0x1F; extra = 1; }
    else if (c >= 0x80) { *sp = (const char*)s; return '?'; }   // stray continuation byte
    while (extra-- > 0) {
        if ((*s & 0xC0) != 0x80) { *sp = (const char*)s; return '?'; }
        c = (c << 6) | (*s++ & 0x3F);
    }
    *sp = (const char*)s;
    return c;
}

} // namespace fontatlas
} // namespace OkayUI
