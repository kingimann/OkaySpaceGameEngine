#include "OkayUI_FontAtlas.hpp"
#include "stb_truetype.h"   // the implementation is compiled into the engine (TtfFont.cpp)
#include "RobotoFont.h"     // the launcher's font: RobotoMedium_compressed_data_base85

#include <cstdlib>
#include <cstring>

namespace OkayUI {
namespace fontatlas {
namespace {

// ---- Dear ImGui's compressed-font format: base85 text -> stb_compress stream -> TTF.
// Decompression from stb.h (public domain) by Sean Barrett, as used by Dear ImGui.
unsigned Decode85Byte(char c) { return c >= '\\' ? c - 36 : c - 35; }
void Decode85(const unsigned char* src, unsigned char* dst) {
    while (*src) {
        unsigned tmp = Decode85Byte(src[0]) + 85 * (Decode85Byte(src[1]) + 85 * (Decode85Byte(src[2]) +
                       85 * (Decode85Byte(src[3]) + 85 * Decode85Byte(src[4]))));
        dst[0] = (tmp >> 0) & 0xFF; dst[1] = (tmp >> 8) & 0xFF; dst[2] = (tmp >> 16) & 0xFF; dst[3] = (tmp >> 24) & 0xFF;
        src += 5; dst += 4;
    }
}
unsigned char *g_outEnd, *g_outBegin, *g_out;
const unsigned char* g_inBegin;
void Match(const unsigned char* data, unsigned len) {
    if (g_out + len > g_outEnd) { g_out += len; return; }
    if (data < g_outBegin) { g_out = g_outEnd + 1; return; }
    while (len--) *g_out++ = *data++;
}
void Lit(const unsigned char* data, unsigned len) {
    if (g_out + len > g_outEnd) { g_out += len; return; }
    if (data < g_inBegin) { g_out = g_outEnd + 1; return; }
    std::memcpy(g_out, data, len);
    g_out += len;
}
#define IN2(x) ((i[x] << 8) + i[(x) + 1])
#define IN3(x) ((i[x] << 16) + IN2((x) + 1))
#define IN4(x) ((i[x] << 24) + IN3((x) + 1))
const unsigned char* Token(const unsigned char* i) {
    if (*i >= 0x20) {
        if (*i >= 0x80)      Match(g_out - i[1] - 1, i[0] - 0x80 + 1), i += 2;
        else if (*i >= 0x40) Match(g_out - (IN2(0) - 0x4000 + 1), i[2] + 1), i += 3;
        else                 Lit(i + 1, i[0] - 0x20 + 1), i += 1 + (i[0] - 0x20 + 1);
    } else {
        if (*i >= 0x18)      Match(g_out - (IN3(0) - 0x180000 + 1), i[3] + 1), i += 4;
        else if (*i >= 0x10) Match(g_out - (IN3(0) - 0x100000 + 1), IN2(3) + 1), i += 5;
        else if (*i >= 0x08) Lit(i + 2, IN2(0) - 0x0800 + 1), i += 2 + (IN2(0) - 0x0800 + 1);
        else if (*i == 0x07) Lit(i + 3, IN2(1) + 1), i += 3 + (IN2(1) + 1);
        else if (*i == 0x06) Match(g_out - (IN3(1) + 1), i[4] + 1), i += 5;
        else if (*i == 0x04) Match(g_out - (IN3(1) + 1), IN2(4) + 1), i += 6;
    }
    return i;
}
unsigned Decompress(unsigned char* output, const unsigned char* i, unsigned outCap) {
    if (IN4(0) != 0x57bC0000 || IN4(4) != 0) return 0;
    const unsigned olen = (i[8] << 24) + (i[9] << 16) + (i[10] << 8) + i[11];
    if (olen > outCap) return 0;
    g_inBegin = i; g_outEnd = output + olen; g_outBegin = output;
    i += 16;
    g_out = output;
    for (;;) {
        const unsigned char* old = i;
        i = Token(i);
        if (i == old) return (*i == 0x05 && i[1] == 0xfa && g_out == output + olen) ? olen : 0;
        if (g_out > output + olen) return 0;
    }
}
#undef IN2
#undef IN3
#undef IN4

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
    const char* b85 = RobotoMedium_compressed_data_base85;
    const int b85len = (int)std::strlen(b85);
    const int compSize = ((b85len + 4) / 5) * 4;
    unsigned char* comp = (unsigned char*)std::malloc((size_t)compSize + 8);
    if (!comp) return;
    Decode85((const unsigned char*)b85, comp);
    const unsigned ttfCap = (comp[8] << 24) + (comp[9] << 16) + (comp[10] << 8) + comp[11];
    unsigned char* ttf = (unsigned char*)std::malloc(ttfCap + 16);
    if (!ttf) { std::free(comp); return; }
    const unsigned ttfLen = Decompress(ttf, comp, ttfCap);
    std::free(comp);
    stbtt_fontinfo info;
    if (!ttfLen || !stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) { std::free(ttf); return; }

    int all[256]; const int nall = BuildCodepoints(all);
    int cps[256]; int n = 0;
    for (int k = 0; k < nall; ++k) if (stbtt_FindGlyphIndex(&info, all[k])) cps[n++] = all[k];

    const float sizes[] = {13.0f, 16.0f, 20.0f, 26.0f, 34.0f, 46.0f};
    const int nsizes = (int)(sizeof(sizes) / sizeof(sizes[0]));
    const int W = 1024, H = 1024, packH = H - 4;   // bottom rows: solid white patch
    unsigned char* alpha = (unsigned char*)std::calloc((size_t)W * H, 1);
    stbtt_packedchar* packed = (stbtt_packedchar*)std::calloc((size_t)n * nsizes, sizeof(stbtt_packedchar));
    if (!alpha || !packed) { std::free(alpha); std::free(packed); std::free(ttf); return; }
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
    std::free(alpha); std::free(packed); std::free(ttf);
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
