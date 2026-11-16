#include "OkayUI_FontAtlas.hpp"
#include "okay/Graphics/TtfRaster.hpp"   // our own TrueType reader / rasterizer
#include "okay/Graphics/TtfFont.hpp"   // EmbeddedUIFontData: the launcher's font (Roboto)
#include "okay/UI/UIPainterSDL.hpp"    // SDLFontAtlas

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

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
    static std::vector<unsigned char> bytes;   // the Face reads the font in place: keep it alive
    if (!okay::EmbeddedUIFontData(bytes)) return;
    okay::ttf::Face face;
    if (!face.Load(bytes.data(), bytes.size(), 0)) return;

    int all[256]; const int nall = BuildCodepoints(all);
    int cps[256]; int glyphIds[256]; int n = 0;
    for (int k = 0; k < nall; ++k) {
        const int gi = face.FindGlyph((std::uint32_t)all[k]);
        if (gi) { cps[n] = all[k]; glyphIds[n] = gi; ++n; }
    }

    // 17 px is the launcher's Dear ImGui font size, and 14, 18, 21, 23 and 25 px its scaled text (rounded to whole pixels):
    // its OkayUI pages draw their text at those sizes, crisp (docs/own_ui_toolkit.md section 8).
    const float sizes[] = {13.0f, 14.0f, 16.0f, 17.0f, 18.0f, 20.0f, 21.0f, 23.0f, 25.0f, 26.0f, 34.0f, 46.0f};
    const int nsizes = (int)(sizeof(sizes) / sizeof(sizes[0]));
    const int W = 1024, H = 1024, packH = H - 4;   // bottom rows: solid white patch
    std::vector<unsigned char> alpha((std::size_t)W * H, 0);

    // Every (size, glyph) bitmap goes into one atlas, tallest first so the shelves stay tight.
    struct Item { int s, k, h; };
    std::vector<Item> items;
    float scales[sizeof(sizes) / sizeof(sizes[0])];
    for (int s = 0; s < nsizes; ++s) {
        scales[s] = face.ScaleForPixelHeight(sizes[s]);
        for (int k = 0; k < n; ++k) {
            int x0, y0, x1, y1;
            face.GlyphBox(glyphIds[k], scales[s], x0, y0, x1, y1);
            items.push_back({s, k, y1 - y0});
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.h > b.h; });

    std::vector<okay::ttf::BakedGlyph> baked((std::size_t)nsizes * n);
    okay::ttf::ShelfPacker packer(W, packH, 1);
    bool ok = true;
    for (const Item& it : items)
        if (!okay::ttf::BakeGlyph(face, glyphIds[it.k], scales[it.s], packer, alpha.data(), baked[(std::size_t)it.s * n + it.k])) {
            ok = false; break;
        }
    // The discs go on shelves below the glyphs (so every glyph stays where it was), as many radii as there is room for.
    // Then the one-pixel rings the same way.
    int discMax = 0, ringMax = 0;
    int discX[Atlas::kDiscMaxR + 1] = {}, discY[Atlas::kDiscMaxR + 1] = {}, ringX[Atlas::kDiscMaxR + 1] = {}, ringY[Atlas::kDiscMaxR + 1] = {};
    if (ok) {
        int used = 0;
        for (const okay::ttf::BakedGlyph& b : baked) used = std::max(used, b.y1);
        int sx = 0, sy = used + 2, shelfH = 0;
        for (int ring = 0; ring < 2; ++ring) {
            for (int T = 2; T <= Atlas::kDiscMaxR; ++T) {
                const int side = 2 * (T + Atlas::kDiscPad);
                if (sx + side > W) { sx = 0; sy += shelfH; shelfH = 0; }
                if (sy + side > packH) break;
                (ring ? ringX : discX)[T] = sx; (ring ? ringY : discY)[T] = sy;
                for (int y = 0; y < side; ++y)
                    for (int x = 0; x < side; ++x) {
                        const float dx = (float)x + 0.5f - (float)(T + Atlas::kDiscPad), dy = (float)y + 0.5f - (float)(T + Atlas::kDiscPad);
                        const float d = std::sqrt(dx * dx + dy * dy);
                        const float cov = std::max(0.0f, std::min(1.0f, ring ? 1.0f - std::fabs(d - ((float)T - 0.5f)) : (float)T + 0.5f - d));
                        alpha[(std::size_t)(sy + y) * W + (std::size_t)(sx + x)] = (unsigned char)(cov * 255.0f + 0.5f);
                    }
                sx += side; shelfH = std::max(shelfH, side); (ring ? ringMax : discMax) = T;
            }
        }
    }
    if (ok) {
        for (int y = H - 4; y < H; ++y) std::memset(alpha.data() + (std::size_t)y * W, 255, (std::size_t)W);
        int asc = 0, desc = 0, gap = 0;
        face.VMetrics(asc, desc, gap);
        Atlas& a = g_atlas;
        a.w = W; a.h = H; a.nsizes = nsizes;
        a.whiteU = 2.5f / W; a.whiteV = (H - 2.0f) / H;
        a.discMax = discMax;
        for (int T = 2; T <= discMax; ++T) { a.discCx[T] = (float)(discX[T] + T + Atlas::kDiscPad); a.discCy[T] = (float)(discY[T] + T + Atlas::kDiscPad); }
        a.ringMax = ringMax;
        for (int T = 2; T <= ringMax; ++T) { a.ringCx[T] = (float)(ringX[T] + T + Atlas::kDiscPad); a.ringCy[T] = (float)(ringY[T] + T + Atlas::kDiscPad); }
        for (int s = 0; s < nsizes; ++s) {
            Size& sz = a.sizes[s];
            sz.px = sizes[s];
            sz.ascent = asc * scales[s];
            sz.count = n;
            for (int k = 0; k < n; ++k) {
                const okay::ttf::BakedGlyph& b = baked[(std::size_t)s * n + k];
                Glyph& g = sz.glyphs[k];
                g.cp = (unsigned)cps[k];
                g.x0 = b.xoff; g.y0 = b.yoff; g.x1 = b.xoff + (float)(b.x1 - b.x0); g.y1 = b.yoff + (float)(b.y1 - b.y0);
                g.u0 = (float)b.x0 / W; g.v0 = (float)b.y0 / H; g.u1 = (float)b.x1 / W; g.v1 = (float)b.y1 / H;
                g.advance = b.xadvance;
                if (g.cp < 128) sz.ascii[g.cp] = (short)(k + 1);
            }
        }
        a.rgba = (unsigned char*)std::malloc((size_t)W * H * 4);
        if (a.rgba) {
            for (int p = 0; p < W * H; ++p) {
                a.rgba[p * 4 + 0] = 255; a.rgba[p * 4 + 1] = 255; a.rgba[p * 4 + 2] = 255; a.rgba[p * 4 + 3] = alpha[(std::size_t)p];
            }
            a.ok = true;
        }
    }
}

} // namespace

const Glyph* Size::Find(unsigned cp) const {   // code points are baked in ascending order
    if (cp < 128) return ascii[cp] > 0 ? &glyphs[ascii[cp] - 1] : nullptr;
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
    const char* q = s;
    while (*q && (unsigned char)*q < 0x80) ++q;   // plain ASCII: the atlas has every character
    if (!*q) return nullptr;
    const Size& z = a.Pick(16.0f);
    bool lacks = false;
    for (const char* p = q; *p && !lacks;) {
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
