#include "OkayUI_FontAtlas.hpp"
#include "okay/Graphics/TtfRaster.hpp"   // our own TrueType reader / rasterizer
#include "okay/Graphics/TtfFont.hpp"   // EmbeddedUIFontData: the launcher's font (Roboto)
#include "okay/UI/UIPainterSDL.hpp"    // SDLFontAtlas
#include "OkfDecode.hpp"               // our own font container (shared/)
#include "JetBrainsMonoFontOwn.h"      // the code font, JetBrains Mono (OFL; docs/THIRD_PARTY.md)

#include <algorithm>
#include <chrono>
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
bool  g_wantLargeCode = false, g_growFailed = false;   // code was asked for larger than the atlas has (Atlas::PickCode); Grow() acts on it

// One face to bake: its glyphs (code point and glyph id, ascending code points) and the atlas sizes it gets.
struct FaceBake {
    okay::ttf::Face face;
    int cps[256] = {}, ids[256] = {}, n = 0;
    float scales[kCodeSizes] = {};
    std::vector<okay::ttf::BakedGlyph> baked;
    bool Prepare(const std::vector<unsigned char>& bytes, const float* sizes, int nsizes) {
        if (bytes.empty() || !face.Load(bytes.data(), bytes.size(), 0)) return false;
        int all[256]; const int nall = BuildCodepoints(all);
        n = 0;
        for (int k = 0; k < nall; ++k) {
            const int gi = face.FindGlyph((std::uint32_t)all[k]);
            if (gi) { cps[n] = all[k]; ids[n] = gi; ++n; }
        }
        for (int s = 0; s < nsizes; ++s) scales[s] = face.ScaleForPixelHeight(sizes[s]);
        baked.assign((std::size_t)nsizes * n, okay::ttf::BakedGlyph());
        return n > 0;
    }
    // Pack every (size, glyph), tallest first so the shelves stay tight. False when the atlas is full.
    bool Pack(okay::ttf::ShelfPacker& packer, unsigned char* alpha, int nsizes) {
        struct Item { int s, k, h; };
        std::vector<Item> items;
        for (int s = 0; s < nsizes; ++s)
            for (int k = 0; k < n; ++k) {
                int x0, y0, x1, y1;
                face.GlyphBox(ids[k], scales[s], x0, y0, x1, y1);
                items.push_back({s, k, y1 - y0});
            }
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.h > b.h; });
        for (const Item& it : items)
            if (!okay::ttf::BakeGlyph(face, ids[it.k], scales[it.s], packer, alpha, baked[(std::size_t)it.s * n + it.k])) return false;
        return true;
    }
    void Fill(Size* out, const float* sizes, int nsizes, int W, int H) const {
        int asc = 0, desc = 0, gap = 0;
        face.VMetrics(asc, desc, gap);
        for (int s = 0; s < nsizes; ++s) {
            Size& sz = out[s];
            for (short& a : sz.ascii) a = -1;
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
                if (cps[k] < 128) sz.ascii[cps[k]] = (short)k;
            }
        }
    }
};

// The sizes baked. The UI font: the launcher look's text sizes. The code font: the same, then larger ones so the script editor's
// biggest code (16 px x UI scale 2 x zoom 3 = 96 px) is still drawn from a bake at least as large.
const float kUiPx[kUiSizes] = {13.0f, 16.0f, 20.0f, 26.0f, 34.0f, 46.0f};
const float kCodePx[kCodeSizes] = {13.0f, 16.0f, 20.0f, 26.0f, 34.0f, 46.0f, 62.0f, 80.0f, 96.0f};

void BuildAtlas(Atlas& a, int startH, int maxH, int codeSizes) {
    codeSizes = std::min(std::max(codeSizes, 1), kCodeSizes);
    const auto t0 = std::chrono::steady_clock::now();
    static std::vector<unsigned char> bytes;   // the Face reads the font in place: keep it alive
    if (bytes.empty() && !okay::EmbeddedUIFontData(bytes)) return;
    static std::vector<unsigned char> codeBytes;
    if (codeBytes.empty()) {
        std::vector<std::uint8_t> b;
        if (okf::DecodeLines(okf_data::kJetBrainsMonoOkf, okf_data::kJetBrainsMonoOkfLines, b)) codeBytes.assign(b.begin(), b.end());
    }
    static FaceBake ui, code;   // static: a Face is large and keeps pointers into its bytes
    static bool prepared = false, uiOk = false, haveCode = false;
    if (!prepared) {
        prepared = true;
        uiOk = ui.Prepare(bytes, kUiPx, kUiSizes);
        haveCode = code.Prepare(codeBytes, kCodePx, kCodeSizes);
    }
    if (!uiOk) return;

    // The UI font is packed first, exactly as before the code font was added, so its glyphs keep their places; the code font
    // goes into the space left. When it does not fit, the atlas is made twice as tall (the UI font still packs the same way).
    // If it still does not fit at maxH, the atlas keeps the UI font only and the code font falls back to it.
    const int W = 1024;
    int grows = 0;
    for (int H = std::max(64, startH);; H *= 2, ++grows) {
        const bool last = H * 2 > maxH;
        const int packH = H - 4;   // bottom rows: solid white patch
        std::vector<unsigned char> alpha((std::size_t)W * H, 0);
        okay::ttf::ShelfPacker packer(W, packH, 1);
        if (!ui.Pack(packer, alpha.data(), kUiSizes)) { if (last) return; continue; }
        const bool codeOk = haveCode && code.Pack(packer, alpha.data(), codeSizes);
        if (haveCode && !codeOk && !last) continue;
        if (!codeOk && haveCode) {   // the last try: repack the UI font alone (the code font's glyphs half-written above it)
            std::fill(alpha.begin(), alpha.end(), (unsigned char)0);
            packer = okay::ttf::ShelfPacker(W, packH, 1);
            if (!ui.Pack(packer, alpha.data(), kUiSizes)) return;
        }
        for (int y = H - 4; y < H; ++y) std::memset(alpha.data() + (std::size_t)y * W, 255, (std::size_t)W);
        a.w = W; a.h = H; a.nsizes = kUiSizes;
        a.whiteU = 2.5f / W; a.whiteV = (H - 2.0f) / H;
        ui.Fill(a.sizes, kUiPx, kUiSizes, W, H);
        a.ncode = 0;
        if (codeOk) { code.Fill(a.code, kCodePx, codeSizes, W, H); a.ncode = codeSizes; }
        a.usedRows = packer.Bottom();
        a.grows = grows;
        a.rgba = (unsigned char*)std::malloc((size_t)W * H * 4);
        if (a.rgba) {
            for (int p = 0; p < W * H; ++p) {
                a.rgba[p * 4 + 0] = 255; a.rgba[p * 4 + 1] = 255; a.rgba[p * 4 + 2] = 255; a.rgba[p * 4 + 3] = alpha[(std::size_t)p];
            }
            a.ok = true;
        }
        a.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return;
    }
}

void Build() {
    g_built = true;
    BuildAtlas(g_atlas, 1024, 4096, kBaseCodeSizes);
}

} // namespace

const Glyph* Size::Find(unsigned cp) const {   // code points are baked in ascending order
    if (cp < 128) { const int i = ascii[cp]; return (i >= 0 && i < count) ? &glyphs[i] : nullptr; }
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

const Size& Atlas::PickCode(float pixelHeight) const {
    if (ncode <= 0) return Pick(pixelHeight);
    for (int s = 0; s < ncode; ++s) if (code[s].px >= pixelHeight - 0.5f) return code[s];
    if (ncode < kCodeSizes && this == &g_atlas) g_wantLargeCode = true;   // larger than baked: grow before the next frame
    return code[ncode - 1];
}

bool Grow() {
    if (!g_wantLargeCode || g_growFailed) return false;
    g_wantLargeCode = false;
    if (!g_atlas.ok || g_atlas.ncode <= 0 || g_atlas.ncode >= kCodeSizes) return false;
    static Atlas bigger;   // large: not on the stack
    BuildAtlas(bigger, g_atlas.h, 4096, kCodeSizes);
    if (!bigger.ok || bigger.ncode < kCodeSizes) { Free(bigger); g_growFailed = true; return false; }   // (not tried again)
    std::free(g_atlas.rgba);
    bigger.grows += g_atlas.grows;
    g_atlas = bigger;          // (the copy takes over the pixels)
    bigger.rgba = nullptr; bigger.ok = false;
    return true;
}

const Atlas& Get() {
    if (!g_built) Build();
    return g_atlas;
}

void BuildInto(Atlas& out, int startH, int maxH, int codeSizes) {
    Free(out);
    out.grows = 0; out.ncode = 0;
    BuildAtlas(out, startH, maxH, codeSizes);
}

void Free(Atlas& a) {
    std::free(a.rgba);
    a.rgba = nullptr;
    a.ok = false;
}

okay::TtfFont* FallbackFont(const char* s, bool code) {
    const Atlas& a = Get();
    if (!a.ok || !s) return nullptr;
    const Size& z = code ? a.PickCode(16.0f) : a.Pick(16.0f);
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
