// Our own TrueType rasteriser against Dear ImGui's built-in one, headless.
//
// Both paths go through the same Dear ImGui font atlas and text layout; only the
// rasteriser differs:
//   * "imgui": Dear ImGui's default loader, configured as the editor and launcher do
//     (OversampleH = OversampleV = 2, PixelSnapH);
//   * "own":   our loader (shared/OwnImGuiFontLoader.hpp, what OKAY_OWN_FONT_RASTER=1
//     selects), which rasterises with okay::ttf::Face.
// Each string is laid out with the baked glyph metrics and drawn by a small software
// compositor (bilinear texture sampling, like the apps' linear-filtered textures) into a
// grey image. A third image, "ref", is the exact coverage of the same outlines at the
// same pen positions, computed at 16 x 16 the resolution and box-filtered down.
//
// Checks: our loader works inside Dear ImGui (glyphs, metrics, merged-font fallback),
// its advances and line metrics match Dear ImGui's, and its pixels are closer to the
// exact coverage than Dear ImGui's. With OKAY_FONT_COMPARE_OUT=<dir> it also writes the
// image pairs as PNG and prints the per-size tables (docs/own_font_raster.md has them).
// With OKAY_FONT_COMPARE_BENCH=1 it times the full UI atlas bake on both paths and
// measures their peak memory (Linux: each in its own child process).
#include "OwnImGuiFontLoader.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include "okay/Graphics/TtfRaster.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#if defined(__linux__)
#include <fstream>
#endif

using namespace okay;

namespace {

void SetEnv(const char* k, const char* v) {
#if defined(_WIN32)
    _putenv_s(k, v ? v : "");
#else
    if (v) setenv(k, v, 1); else unsetenv(k);
#endif
}

const char* const kStrings[][2] = {
    {"fox", "The quick brown fox jumps over the lazy dog"},
    {"digits", "0123456789 3.14159 +42 -7"},
    {"punct", "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"},
    {"accents", "\xC3\xA0\xC3\xA1\xC3\xA2\xC3\xA3\xC3\xA4\xC3\xA5 \xC3\xA7\xC3\xA8\xC3\xA9\xC3\xAA\xC3\xAB \xC3\xB1\xC3\xB2\xC3\xB3\xC3\xB4\xC3\xB6 \xC3\xB9\xC3\xBA\xC3\xBC\xC3\xBF \xC3\x80\xC3\x89\xC3\x8E\xC3\x95\xC3\x9C \xC3\x87\xC3\x9F \xC5\x93\xC5\x92"},
    {"code", "if (a != b && c <= d) { x->y[i] = 0x1F; } // 0O 1lI| ;:"},
};
const int kSizes[] = {12, 14, 16, 18, 24, 32, 48};

struct Fonts { std::vector<unsigned char> roboto, mono; };

const Fonts& EmbeddedFonts() {
    static Fonts f;
    static bool done = false;
    if (!done) { done = true; EmbeddedUIFontData(f.roboto); EmbeddedCodeFontData(f.mono); }
    return f;
}

// One atlas with one font (and optionally a second, merged one behind it).
struct AtlasFont {
    ImFontAtlas* atlas = nullptr;
    ImFont* font = nullptr;
    AtlasFont() = default;
    AtlasFont(const AtlasFont&) = delete;
    AtlasFont& operator=(const AtlasFont&) = delete;
    ~AtlasFont() { if (atlas) IM_DELETE(atlas); }
};

void MakeAtlas(AtlasFont& out, bool own, const std::vector<unsigned char>& bytes, float size,
               const std::vector<unsigned char>* merged = nullptr) {
    out.atlas = IM_NEW(ImFontAtlas)();
    out.atlas->RendererHasTextures = true;   // glyphs load on demand, as with the apps' backends
    if (own) out.atlas->SetFontLoader(imguifont::Loader());
    ImFontConfig fc;
    fc.OversampleH = 2; fc.OversampleV = 2; fc.PixelSnapH = true;   // the editor's and launcher's settings
    fc.FontDataOwnedByAtlas = false;
    out.font = out.atlas->AddFontFromMemoryTTF((void*)bytes.data(), (int)bytes.size(), size, &fc);
    if (merged) {
        ImFontConfig mc = fc;
        mc.MergeMode = true;
        out.atlas->AddFontFromMemoryTTF((void*)merged->data(), (int)merged->size(), size, &mc);
    }
}

// Grey image, 0..1.
struct Img {
    int w = 0, h = 0;
    std::vector<float> px;
    void Init(int W, int H) { w = W; h = H; px.assign((std::size_t)W * H, 0.0f); }
    float& At(int x, int y) { return px[(std::size_t)y * w + x]; }
};

float TexAlpha(const ImTextureData* t, int x, int y) {
    if (x < 0 || y < 0 || x >= t->Width || y >= t->Height) return 0.0f;
    const unsigned char* p = (const unsigned char*)const_cast<ImTextureData*>(t)->GetPixelsAt(x, y);
    return (t->Format == ImTextureFormat_Alpha8 ? p[0] : p[3]) / 255.0f;
}
float Bilinear(const ImTextureData* t, float u, float v) {   // u, v in texels, centres at +0.5
    const float fx = u - 0.5f, fy = v - 0.5f;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const float ax = fx - x0, ay = fy - y0;
    const float a = TexAlpha(t, x0, y0), b = TexAlpha(t, x0 + 1, y0);
    const float c = TexAlpha(t, x0, y0 + 1), d = TexAlpha(t, x0 + 1, y0 + 1);
    return (a * (1 - ax) + b * ax) * (1 - ay) + (c * (1 - ax) + d * ax) * ay;
}

// (A copy of the glyph: Dear ImGui's glyph array grows, and moves, as glyphs load.)
struct Laid { std::uint32_t cp; float pen; ImFontGlyph glyph; bool has; const ImFontGlyph* g; };

std::vector<std::uint32_t> Codepoints(const char* s) {
    std::vector<std::uint32_t> out;
    const char* end = s + std::strlen(s);
    while (s < end) {
        unsigned int c = 0;
        const int n = ImTextCharFromUtf8(&c, s, end);
        if (n <= 0) break;
        s += n;
        out.push_back(c);
    }
    return out;
}

// Lay `text` out with Dear ImGui's metrics: pen starts at x = 2, steps by AdvanceX.
std::vector<Laid> Layout(ImFontBaked* baked, const std::vector<std::uint32_t>& cps, float& width) {
    // Load every glyph first: loading may grow the atlas texture, which changes the UVs of
    // glyphs loaded before.
    for (std::uint32_t cp : cps) baked->FindGlyph((ImWchar)cp);
    std::vector<Laid> out;
    float pen = 2.0f;
    for (std::uint32_t cp : cps) {
        const ImFontGlyph* g = baked->FindGlyph((ImWchar)cp);
        out.push_back({cp, pen, g ? *g : ImFontGlyph(), g != nullptr, nullptr});
        pen += g ? g->AdvanceX : 0.0f;
    }
    for (Laid& l : out) l.g = l.has ? &l.glyph : nullptr;
    width = pen - 2.0f;
    return out;
}

// Draw laid-out glyphs: every pixel whose centre is inside a glyph quad samples the
// atlas bilinearly; glyphs combine with "over".
void Draw(Img& img, const ImFontAtlas* atlas, const std::vector<Laid>& laid, float top) {
    const ImTextureData* t = atlas->TexData;
    for (const Laid& l : laid) {
        const ImFontGlyph* g = l.g;
        if (!g || !g->Visible) continue;
        const float qx0 = l.pen + g->X0, qx1 = l.pen + g->X1, qy0 = top + g->Y0, qy1 = top + g->Y1;
        const float u0 = g->U0 * t->Width, u1 = g->U1 * t->Width, v0 = g->V0 * t->Height, v1 = g->V1 * t->Height;
        for (int y = std::max(0, (int)std::floor(qy0)); y < std::min(img.h, (int)std::ceil(qy1) + 1); ++y)
            for (int x = std::max(0, (int)std::floor(qx0)); x < std::min(img.w, (int)std::ceil(qx1) + 1); ++x) {
                const float cx = x + 0.5f, cy = y + 0.5f;
                if (cx < qx0 || cx >= qx1 || cy < qy0 || cy >= qy1) continue;
                const float u = u0 + (cx - qx0) / (qx1 - qx0) * (u1 - u0);
                const float v = v0 + (cy - qy0) / (qy1 - qy0) * (v1 - v0);
                const float a = Bilinear(t, u, v);
                float& o = img.At(x, y);
                o = a + o * (1.0f - a);
            }
    }
}

// The exact coverage of the same glyphs at the same pens: each glyph rendered by our
// analytic rasteriser at 16x the scale, placed on the 16x grid, box-filtered to 1x.
void DrawReference(Img& img, const ttf::Face& face, float pxSize, const std::vector<Laid>& laid, float baseline) {
    const int S = 16;
    const float scale = face.ScaleForPixelHeight(pxSize) * S;
    Img big; big.Init(img.w * S, img.h * S);
    std::vector<std::uint8_t> buf;
    for (const Laid& l : laid) {
        const int gi = face.FindGlyph(l.cp);
        int x0, y0, x1, y1;
        if (!gi || !face.GlyphBox(gi, scale, x0, y0, x1, y1)) continue;
        const int w = x1 - x0, h = y1 - y0;
        buf.assign((std::size_t)w * h, 0);
        face.Render(gi, scale, buf.data(), w, h, w);
        const int ox = (int)std::lround(l.pen * S) + x0, oy = (int)std::lround(baseline * S) + y0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int X = ox + x, Y = oy + y;
                if (X < 0 || Y < 0 || X >= big.w || Y >= big.h) continue;
                const float a = buf[(std::size_t)y * w + x] / 255.0f;
                float& o = big.At(X, Y);
                o = a + o * (1.0f - a);
            }
    }
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            float s = 0;
            for (int j = 0; j < S; ++j)
                for (int i = 0; i < S; ++i) s += big.At(x * S + i, y * S + j);
            img.At(x, y) = s / (S * S);
        }
}

struct Diff { double mean = 0, max = 0; long over8 = 0; };   // in 0..255 levels
Diff Compare(const Img& a, const Img& b) {
    Diff d;
    double sum = 0; long n = 0, inked = 0;
    for (std::size_t i = 0; i < a.px.size(); ++i) {
        const double e = std::fabs((double)a.px[i] - (double)b.px[i]) * 255.0;
        if (a.px[i] > 0 || b.px[i] > 0) { sum += e; ++inked; }
        d.max = std::max(d.max, e);
        if (e > 8.0) ++d.over8;
        ++n;
    }
    d.mean = inked ? sum / (double)inked : 0.0;   // mean over pixels either image inks
    return d;
}

void SavePair(const std::string& dir, const std::string& name, const Img& own, const Img& im, const Img& ref) {
    // Three rows (ours, Dear ImGui's, exact), black text on white, scaled 3x so it can be seen.
    const int Z = 3, W = own.w * Z, H = (own.h * 3 + 2) * Z;
    std::vector<std::uint8_t> out((std::size_t)W * H, 255);
    const Img* rows[3] = {&own, &im, &ref};
    for (int r = 0; r < 3; ++r)
        for (int y = 0; y < own.h; ++y)
            for (int x = 0; x < own.w; ++x) {
                const std::uint8_t v = (std::uint8_t)std::lround(255.0f * (1.0f - std::min(1.0f, rows[r]->px[(std::size_t)y * own.w + x])));
                for (int j = 0; j < Z; ++j)
                    for (int i = 0; i < Z; ++i)
                        out[(std::size_t)((r * (own.h + 1) + y) * Z + j) * W + x * Z + i] = v;
            }
    ImageWrite::WritePNG(dir + "/" + name + ".png", W, H, 1, out.data());
}

struct SizeRow { double ownMean = 0, imMean = 0, ownImMean = 0; double ownMax = 0, imMax = 0; double widthOwn = 0, widthIm = 0; int n = 0; };

// ---- Quality: strings and single glyphs at each size, both fonts ---------------------
void QualityRun(const char* fontName, const std::vector<unsigned char>& bytes, const char* outDir) {
    ttf::Face face;
    CHECK(face.Load(bytes.data(), bytes.size()));
    if (outDir) std::printf("\n%s: size | ours vs exact (mean / max) | Dear ImGui vs exact (mean / max) | ours vs Dear ImGui (mean) | line height ours / imgui | ascent ours / imgui | width ours / imgui (all strings)\n", fontName);
    double glyphWorstOwn = 0, glyphWorstIm = 0; std::string worstOwnName, worstImName;
    double glyphMaxOwn = 0, glyphMaxIm = 0; std::string maxOwnName, maxImName;
    double glyphSumOwn = 0, glyphSumIm = 0, glyphSumPair = 0; long glyphN = 0;
    for (int size : kSizes) {
        AtlasFont own, im;
        MakeAtlas(own, true, bytes, (float)size);
        MakeAtlas(im, false, bytes, (float)size);
        CHECK(own.font && im.font);
        if (!own.font || !im.font) return;
        ImFontBaked* bo = own.font->GetFontBaked((float)size);
        ImFontBaked* bi = im.font->GetFontBaked((float)size);
        CHECK(bo && bi);
        if (!bo || !bi) return;
        // Line metrics: same line height (the font size) and ascent / descent within a pixel.
        CHECK_NEAR(bo->Size, bi->Size, 0.01);
        CHECK_NEAR(bo->Ascent, bi->Ascent, 0.01);
        CHECK_NEAR(bo->Descent, bi->Descent, 0.01);
        SizeRow row;
        for (const auto& s : kStrings) {
            const std::vector<std::uint32_t> cps = Codepoints(s[1]);
            float wo = 0, wi = 0;
            const std::vector<Laid> lo = Layout(bo, cps, wo);
            const std::vector<Laid> li = Layout(bi, cps, wi);
            // Same advances (both snapped to whole pixels): the same string width.
            CHECK_NEAR(wo, wi, 0.01);
            for (std::size_t k = 0; k < cps.size() && k < lo.size(); ++k)
                if (lo[k].g && li[k].g && std::fabs(lo[k].g->AdvanceX - li[k].g->AdvanceX) > 0.01f) {
                    std::fprintf(stderr, "  advance differs: %s %dpx U+%04X own %.2f imgui %.2f\n", fontName, size, cps[k],
                                 lo[k].g->AdvanceX, li[k].g->AdvanceX);
                    CHECK(false);
                }
            Img a, b, r, ri;
            const int W = (int)std::ceil(std::max(wo, wi)) + 6, H = size * 3 / 2 + 4;
            a.Init(W, H); b.Init(W, H); r.Init(W, H); ri.Init(W, H);
            const float top = 2.0f;
            Draw(a, own.atlas, lo, top);
            Draw(b, im.atlas, li, top);
            DrawReference(r, face, (float)size, lo, top + bo->Ascent);    // each path against the exact
            DrawReference(ri, face, (float)size, li, top + bi->Ascent);   // coverage at its own baseline
            const Diff dOwn = Compare(a, r), dIm = Compare(b, ri), dPair = Compare(a, b);
            row.ownMean += dOwn.mean; row.imMean += dIm.mean; row.ownImMean += dPair.mean;
            row.ownMax = std::max(row.ownMax, dOwn.max); row.imMax = std::max(row.imMax, dIm.max);
            row.widthOwn += wo; row.widthIm += wi; ++row.n;
            if (outDir) SavePair(outDir, std::string(fontName) + "_" + s[0] + "_" + std::to_string(size), a, b, r);

            // Per glyph: each character alone at pen 2.
            for (std::size_t k = 0; k < cps.size(); ++k) {
                if (cps[k] == ' ') continue;
                const std::vector<std::uint32_t> one{cps[k]};
                float w1 = 0, w2 = 0;
                const std::vector<Laid> go = Layout(bo, one, w1), gim = Layout(bi, one, w2);
                Img ga, gb, gr, gri;
                const int GW = size * 2 + 6;
                ga.Init(GW, H); gb.Init(GW, H); gr.Init(GW, H); gri.Init(GW, H);
                Draw(ga, own.atlas, go, top); Draw(gb, im.atlas, gim, top);
                DrawReference(gr, face, (float)size, go, top + bo->Ascent);
                DrawReference(gri, face, (float)size, gim, top + bi->Ascent);
                const Diff e1 = Compare(ga, gr), e2 = Compare(gb, gri), e3 = Compare(ga, gb);
                glyphSumOwn += e1.mean; glyphSumIm += e2.mean; glyphSumPair += e3.mean; ++glyphN;
                char nm[64]; std::snprintf(nm, sizeof nm, "U+%04X@%d", cps[k], size);
                if (e1.mean > glyphWorstOwn) { glyphWorstOwn = e1.mean; worstOwnName = nm; }
                if (e2.mean > glyphWorstIm) { glyphWorstIm = e2.mean; worstImName = nm; }
                if (e1.max > glyphMaxOwn) { glyphMaxOwn = e1.max; maxOwnName = nm; }
                if (e2.max > glyphMaxIm) { glyphMaxIm = e2.max; maxImName = nm; }
            }
        }
        const double n = row.n ? row.n : 1;
        if (outDir)
            std::printf("  %2d | %5.2f / %5.1f | %5.2f / %5.1f | %5.2f | %d / %d | %.0f / %.0f | %.0f / %.0f\n", size,
                        row.ownMean / n, row.ownMax, row.imMean / n, row.imMax, row.ownImMean / n, (int)bo->Size,
                        (int)bi->Size, bo->Ascent, bi->Ascent, row.widthOwn, row.widthIm);
        // Ours must be at least as close to the exact coverage as Dear ImGui's.
        CHECK(row.ownMean <= row.imMean + 0.5);
        CHECK(row.ownMean / n < 6.0);
    }
    if (outDir)
        std::printf("  per glyph (%ld glyph renders): mean |ours - exact| %.2f, |imgui - exact| %.2f, |ours - imgui| %.2f levels; "
                    "worst ours %s (%.2f), worst imgui %s (%.2f)\n",
                    glyphN, glyphSumOwn / (double)glyphN, glyphSumIm / (double)glyphN, glyphSumPair / (double)glyphN,
                    worstOwnName.c_str(), glyphWorstOwn, worstImName.c_str(), glyphWorstIm);
    if (outDir)
        std::printf("  per glyph, worst single pixel: ours %s (%.1f levels), imgui %s (%.1f levels)\n", maxOwnName.c_str(),
                    glyphMaxOwn, maxImName.c_str(), glyphMaxIm);
    CHECK(glyphSumOwn / (double)(glyphN ? glyphN : 1) < 1.0);   // ours: within a level of exact coverage on average
    CHECK(glyphMaxOwn < 24.0);
}

// ---- Fallback through merged sources: JetBrains Mono first, Roboto behind it ---------
void MergedFallback() {
    const Fonts& f = EmbeddedFonts();
    ttf::Face mono, rob;
    CHECK(mono.Load(f.mono.data(), f.mono.size()));
    CHECK(rob.Load(f.roboto.data(), f.roboto.size()));
    // A character Roboto has and JetBrains Mono lacks (and the other way round).
    std::uint32_t onlyRoboto = 0, onlyMono = 0;
    for (std::uint32_t cp = 0x20; cp < 0x3000 && (!onlyRoboto || !onlyMono); ++cp) {
        const bool m = mono.FindGlyph(cp) != 0, r = rob.FindGlyph(cp) != 0;
        if (!onlyRoboto && r && !m) onlyRoboto = cp;
        if (!onlyMono && m && !r) onlyMono = cp;
    }
    CHECK(onlyRoboto != 0);
    for (int own = 0; own < 2; ++own) {
        AtlasFont a;
        MakeAtlas(a, own == 1, f.mono, 16.0f, &f.roboto);
        ImFontBaked* b = a.font->GetFontBaked(16.0f);
        const ImFontGlyph* gm = b->FindGlyphNoFallback((ImWchar)'m');
        const ImFontGlyph* gr = b->FindGlyphNoFallback((ImWchar)onlyRoboto);
        CHECK(gm && gm->SourceIdx == 0);   // from JetBrains Mono
        CHECK(gr && gr->SourceIdx == 1);   // fell back to Roboto
        CHECK(gr && gr->Visible);
        if (onlyMono) { const ImFontGlyph* g = b->FindGlyphNoFallback((ImWchar)onlyMono); CHECK(g && g->SourceIdx == 0); }
        CHECK(b->FindGlyphNoFallback((ImWchar)0x10FFF0) == nullptr || sizeof(ImWchar) == 2);
    }
    std::printf("[fallback] merged JetBrains Mono + Roboto: U+%04X comes from Roboto on both loaders\n", onlyRoboto);
}

// ---- Speed and memory: the editor's and launcher's whole UI atlas --------------------
// Editor: Roboto 18 and 22 (heading), JetBrains Mono at 48 (code); launcher: Roboto 17,
// JetBrains Mono 16. Every glyph of ASCII and Latin-1 (what the UI shows from the start).
double BakeFullUiAtlas(bool own, int* texW = nullptr, int* texH = nullptr) {
    const Fonts& f = EmbeddedFonts();
    // Processor time, not wall time: a busy shared machine pauses the process, and that is not the bake's cost.
    const std::clock_t t0 = std::clock();
    ImFontAtlas* atlas = IM_NEW(ImFontAtlas)();
    atlas->RendererHasTextures = true;
    if (own) atlas->SetFontLoader(imguifont::Loader());
    struct Face { const std::vector<unsigned char>* bytes; float size; };
    const Face faces[] = {{&f.roboto, 18}, {&f.roboto, 22}, {&f.mono, 48}, {&f.roboto, 17}, {&f.mono, 16}};
    for (const Face& fc : faces) {
        ImFontConfig c; c.OversampleH = 2; c.OversampleV = 2; c.PixelSnapH = true; c.FontDataOwnedByAtlas = false;
        ImFont* font = atlas->AddFontFromMemoryTTF((void*)fc.bytes->data(), (int)fc.bytes->size(), fc.size, &c);
        ImFontBaked* b = font->GetFontBaked(fc.size);
        for (int cp = 32; cp <= 255; ++cp) if (cp < 127 || cp >= 160) b->FindGlyph((ImWchar)cp);
    }
    const double ms = 1000.0 * (double)(std::clock() - t0) / (double)CLOCKS_PER_SEC;
    if (texW) *texW = atlas->TexData->Width;
    if (texH) *texH = atlas->TexData->Height;
    IM_DELETE(atlas);
    return ms;
}

#if defined(__linux__)
long StatusKb(const char* key) {
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line))
        if (line.rfind(key, 0) == 0) return std::atol(line.c_str() + std::strlen(key));
    return -1;
}
// Peak resident memory one bake adds, in KiB: this program started again with --peak, so
// the bake runs in a fresh process whose heap holds nothing freed by earlier bakes.
long PeakChild() {   // (in that process) prints the number
    const Fonts& f = EmbeddedFonts();   // decoded first: not part of the bake
    (void)f;
    { std::ofstream c("/proc/self/clear_refs"); c << "5"; }   // reset the high-water mark
    const long before = StatusKb("VmHWM:");
    return before;
}
long PeakKb(const char* self, bool own) {
    const std::string cmd = std::string("\"") + self + "\" --peak " + (own ? "own" : "imgui");
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return -1;
    long kb = -1;
    if (std::fscanf(p, "%ld", &kb) != 1) kb = -1;
    pclose(p);
    return kb;
}
#endif

void Bench(const char* self) {
    BakeFullUiAtlas(true); BakeFullUiAtlas(false);   // warm up (decoded fonts, caches)
    const int N = 21;
    std::vector<double> to, ti;
    for (int i = 0; i < N; ++i) { to.push_back(BakeFullUiAtlas(true)); ti.push_back(BakeFullUiAtlas(false)); }
    std::sort(to.begin(), to.end()); std::sort(ti.begin(), ti.end());
    int ow = 0, oh = 0, iw = 0, ih = 0;
    BakeFullUiAtlas(true, &ow, &oh); BakeFullUiAtlas(false, &iw, &ih);
    std::printf("[bench] full UI atlas (5 faces x 190 glyphs), processor time, median of %d: own %.2f ms, Dear ImGui %.2f ms "
                "(own/imgui %.2f; fastest own %.2f, imgui %.2f); atlas texture own %dx%d, imgui %dx%d\n",
                N, to[N / 2], ti[N / 2], to[N / 2] / ti[N / 2], to[0], ti[0], ow, oh, iw, ih);
#if defined(__linux__)
    const long po = PeakKb(self, true), pi = PeakKb(self, false);
    std::printf("[bench] peak resident memory added by the bake (fresh process each): own %ld KiB, Dear ImGui %ld KiB\n", po, pi);
#endif
}

} // namespace

int main(int argc, char** argv) {
    // A context only for Dear ImGui's allocator and asserts; the atlases are standalone.
    ImGui::CreateContext();
#if defined(__linux__)
    if (argc == 3 && std::strcmp(argv[1], "--peak") == 0) {   // Bench's child: one bake, print the peak it added
        const long before = PeakChild();
        BakeFullUiAtlas(std::strcmp(argv[2], "own") == 0);
        std::printf("%ld\n", StatusKb("VmHWM:") - before);
        return 0;
    }
#endif
    const Fonts& f = EmbeddedFonts();
    CHECK(!f.roboto.empty());
    CHECK(!f.mono.empty());
    if (f.roboto.empty() || f.mono.empty()) TEST_MAIN_RESULT();
    const char* outDir = std::getenv("OKAY_FONT_COMPARE_OUT");
    if (outDir && !*outDir) outDir = nullptr;

    RUN_SUITE("own rasteriser vs Dear ImGui's: Roboto");
    QualityRun("roboto", f.roboto, outDir);
    RUN_SUITE("own rasteriser vs Dear ImGui's: JetBrains Mono");
    QualityRun("mono", f.mono, outDir);
    RUN_SUITE("fallback through merged sources");
    MergedFallback();
    RUN_SUITE("the switch: OKAY_OWN_FONT_RASTER");
    {
        ImFontAtlas* a = IM_NEW(ImFontAtlas)();
        SetEnv("OKAY_OWN_FONT_RASTER", "0");
        CHECK(!imguifont::InstallIfRequested(a));
        CHECK(a->FontLoader != imguifont::Loader());
        SetEnv("OKAY_OWN_FONT_RASTER", "1");
        CHECK(imguifont::InstallIfRequested(a));
        CHECK(a->FontLoader == imguifont::Loader());
        SetEnv("OKAY_OWN_FONT_RASTER", nullptr);
        IM_DELETE(a);
    }
    if (const char* b = std::getenv("OKAY_FONT_COMPARE_BENCH"); b && *b == '1') {
        RUN_SUITE("speed and memory");
        std::string self = argv[0];
#if defined(__linux__)
        std::error_code ec;
        const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec) self = exe.string();
#endif
        Bench(self.c_str());
    }
    ImGui::DestroyContext();
    TEST_MAIN_RESULT();
}
