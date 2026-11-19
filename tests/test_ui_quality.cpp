// In-game UI quality: the numbers behind "sharper, smoother UI" (docs/in_game_ui_quality.md). Everything is measured, none compared to a
// stored picture:
//   * text: glyph weight (stem darkening), exact quarter-pixel face sizes, subpixel glyph phases that move the ink by the phase, text
//     that lands on the device pixel grid (sharper than a resampled one), quarter-pixel text positions in Best;
//   * shadows: the soft shadow profile matches a Gaussian-blurred edge (0.5 * erfc) and has no steps;
//   * borders and shapes at a canvas scale of 1.5: whole device pixels, and a circle's edge pixels match analytic coverage;
//   * pictures: shrunk pictures are filtered (match an area average), the size rules, the pixel-art look untouched;
//   * gradients: Best dithers (the mean follows the ramp, bands break up);
//   * the quality setting: saved with the scene, defaulted, mapped from the environment names.
// Drawn by the real UI pass on SDL's software renderer. Prints the UI draw time per quality (the benchmark).
#include "test_framework.hpp"
#include "ui_quality_render.hpp"
#include "ui_record_painter.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include "okay/UI/UIFontPolicy.hpp"
#include "okay/UI/UIImageScale.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

using namespace okay;
using namespace ui_quality;

namespace {

// ---- tiny scene helpers -----------------------------------------------------------------------------------------------------------------
GameObject* MakeCanvas(Scene& s, int refW, int refH, bool scaled) {
    s.physicsEnabled = false;
    GameObject* cvo = s.CreateGameObject("Canvas");
    Canvas* cv = cvo->AddComponent<Canvas>();
    if (scaled) { cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize; cv->referenceResolution = {(float)refW, (float)refH}; }
    else cv->scaleMode = Canvas::ScaleMode::ConstantPixelSize;
    return cvo;
}
template <class T> T* AddTo(Scene& s, GameObject* cvo, const char* name) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->SetParent(cvo->transform, false);
    return g->AddComponent<T>();
}

double Lum(const Image& im, int x, int y) { return (Byte(im, x, y, 0) + Byte(im, x, y, 1) + Byte(im, x, y, 2)) / 3.0; }

// Ink centroid (x) of everything brighter than the background in a box.
double InkCentroidX(const Image& im, int x0, int y0, int w, int h, double bg) {
    double m = 0, mx = 0;
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) { const double v = std::max(0.0, Lum(im, x, y) - bg); m += v; mx += v * (x + 0.5); }
    return m > 0 ? mx / m : 0.0;
}

// How sharp the ink is: sum of squared ink over sum of ink. A glyph bitmap drawn one to one keeps its crisp edges (high); the same
// glyph resampled at a half-pixel offset spreads each edge over two pixels (lower).
double Sharpness(const Image& im, int x0, int y0, int w, int h, double bg, double fg) {
    double m = 0, m2 = 0;
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) {
            const double t = std::min(1.0, std::max(0.0, (Lum(im, x, y) - bg) / (fg - bg)));
            m += t; m2 += t * t;
        }
    return m > 0 ? m2 / m : 0.0;
}

double Erfc(double x) { return std::erfc(x); }

// ---- 1. the face: sizes, weight, subpixel phases ----------------------------------------------------------------------------------------
void FontTests() {
    TtfFont* f = BuiltinUIFont();
    CHECK(f && f->Valid());
    if (!f) return;
    // Exact sizes: quarter-pixel steps up to 32 px.
    CHECK_NEAR(f->ForDeviceSize(16.2f, false)->BakeHeight(), 16.25, 0.01);
    CHECK_NEAR(f->ForDeviceSize(12.0f, false)->BakeHeight(), 12.0, 0.01);
    CHECK_NEAR(f->ForDeviceSize(18.6f, false)->BakeHeight(), 18.5, 0.01);
    CHECK(f->ForDeviceSize(16.2f, false) == f->ForDeviceSize(16.3f, false));   // the same quarter step is one face
    // The weight curve: stronger for small text, none from 24 px.
    CHECK_NEAR(TtfCoverageGamma(6.0f), 0.78, 1e-4);
    CHECK_NEAR(TtfCoverageGamma(24.0f), 1.0, 1e-4);
    CHECK_NEAR(TtfCoverageGamma(40.0f), 1.0, 1e-4);
    CHECK(TtfCoverageGamma(12.0f) < TtfCoverageGamma(16.0f));
    CHECK(f->ForDeviceSize(30.0f, true) == f->ForDeviceSize(30.0f, false));   // big text is left alone

    auto ink = [](const TtfFont* face, const TtfFont::Glyph* g) {
        double s = 0;
        for (int y = g->y0; y < g->y1; ++y)
            for (int x = g->x0; x < g->x1; ++x) s += face->Atlas().Data()[((std::size_t)y * face->Atlas().Width() + x) * 4 + 3];
        return s;
    };
    // Stem darkening adds weight to small text: total coverage of "l" and "e" at 10 px grows by at least 8%.
    TtfFont* plain = f->ForDeviceSize(10.0f, false);
    TtfFont* heavy = f->ForDeviceSize(10.0f, true);
    CHECK(plain != heavy);
    for (char c : {'l', 'e', 'o'}) {
        const double a = ink(plain, plain->GlyphFor((unsigned char)c)), b = ink(heavy, heavy->GlyphFor((unsigned char)c));
        std::printf("glyph '%c' at 10px: coverage %.0f -> %.0f (+%.0f%%)\n", c, a, b, (b / a - 1.0) * 100.0);
        CHECK(b > a * 1.08);
    }
    // Kerning: pairs like AV and To pull together; ordinary pairs stay put; Measure with kerning is shorter by the pair adjustments.
    TtfFont* big = f->ForDeviceSize(100.0f, false);
    const float av = big->Kerning('A', 'V', 100.0f), to = big->Kerning('T', 'o', 100.0f), ab = big->Kerning('a', 'b', 100.0f);
    std::printf("kerning at 100px: AV %.2f, To %.2f, ab %.2f px\n", av, to, ab);
    CHECK(av < -2.0f);
    CHECK(to < -2.0f);
    CHECK(std::fabs(ab) < 3.0f);
    CHECK(big->Measure("AVA", 100.0f, true) < big->Measure("AVA", 100.0f, false) - 3.0f);
    CHECK_NEAR(big->Measure("ab", 100.0f, true), big->Measure("ab", 100.0f, false), 3.0);
    // Subpixel phases: the ink of the phase-k glyph is the phase-0 ink moved by k/4 pixel, and the weight is the same.
    TtfFont* face = f->ForDeviceSize(18.0f, false);
    for (char c : {'o', 'l', 'e', 'W'}) {
        const TtfFont::Glyph* g0 = face->GlyphFor((unsigned char)c);
        auto centroid = [&](const TtfFont::Glyph* g) {
            double m = 0, mx = 0;
            for (int y = g->y0; y < g->y1; ++y)
                for (int x = g->x0; x < g->x1; ++x) {
                    const double v = face->Atlas().Data()[((std::size_t)y * face->Atlas().Width() + x) * 4 + 3];
                    m += v; mx += v * (x - g->x0 + 0.5);
                }
            return mx / m;
        };
        const double c0 = centroid(g0), w0 = ink(face, g0);
        for (int ph = 1; ph < TtfFont::kPhases; ++ph) {
            const TtfFont::Glyph* gp = face->GlyphForPhase((unsigned char)c, ph);
            CHECK(gp != nullptr);
            if (!gp) continue;
            CHECK(gp->x1 - gp->x0 == g0->x1 - g0->x0 + 1);   // one pixel wider
            CHECK_NEAR(gp->xoff, g0->xoff, 1e-4);
            CHECK_NEAR(centroid(gp) - c0, ph / 4.0, 0.05);
            CHECK_NEAR(ink(face, gp) / w0, 1.0, 0.02);
        }
    }
}

// ---- 2. pictures: size rules and the filter ---------------------------------------------------------------------------------------------
void ImageTests() {
    int w = 0, h = 0;
    CHECK(ui2d::UIImageMipSize(128, 128, 36, 36, w, h) && w == 36 && h == 36);   // small sizes are exact
    CHECK(ui2d::UIImageMipSize(128, 128, 96, 96, w, h) && w == 96);              // 75% is the limit: filtered
    CHECK(!ui2d::UIImageMipSize(128, 128, 100, 100, w, h));                      // 78%: drawn as it is
    CHECK(!ui2d::UIImageMipSize(128, 128, 256, 256, w, h));                      // grown: never
    CHECK(!ui2d::UIImageMipSize(128, 128, 0, 10, w, h));
    CHECK(ui2d::UIImageMipSize(512, 512, 200, 200, w, h) && w >= 200 && w <= 224 && w == h);   // big sizes round up in 10% steps
    CHECK(ui2d::UIImageMipSize(512, 64, 100, 64, w, h) && w >= 100 && h == 64);                // only the shrunk side changes
    // A one-pixel checker averages to mid grey (in linear light: sRGB 188) with no leftover pattern.
    Image chk(64, 64);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) chk.SetPixel(x, y, ((x + y) & 1) ? Color::White : Color::Black);
    const Image small = ui2d::UIDownscaleImage(chk, 16, 16, false);
    CHECK(small.Width() == 16 && small.Height() == 16);
    int lo = 255, hi = 0;
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) { const int v = Byte(small, x, y, 0); lo = std::min(lo, v); hi = std::max(hi, v); }
    std::printf("checker 64 -> 16: grey levels %d..%d (linear-light average 188)\n", lo, hi);
    CHECK(lo >= 184 && hi <= 192);
    // Transparent texels add no colour: half-transparent red/blue halves keep their own colour at the border.
    Image cut(8, 8);
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) cut.SetPixel(x, y, x < 4 ? Color::FromBytes(255, 0, 0, 255) : Color::FromBytes(0, 0, 255, 0));
    const Image cs = ui2d::UIDownscaleImage(cut, 2, 2, false);
    CHECK(Byte(cs, 0, 0, 0) >= 250 && Byte(cs, 0, 0, 2) <= 4);   // the red half stays red, no blue bleeds in from the see-through half
}

// A picture shown at `size` logical pixels at scale 1, rendered at `quality`, compared with the ideal area average of the source.
double PictureError(const std::string& tex, int quality, int size, bool pixel = false) {
    Scene s;
    s.uiQuality = quality;
    if (pixel) s.uiFont = "pixel";
    GameObject* cv = MakeCanvas(s, size + 8, size + 8, false);
    auto* im = AddTo<UIImage>(s, cv, "pic");
    im->position = {4, 4}; im->size = {(float)size, (float)size}; im->texture = tex;
    Image out;
    if (!RenderScene(s, size + 8, size + 8, out)) return 1e9;
    Image src; src.Load(tex);
    // Ideal: area average over each destination pixel (box, sRGB values: close enough as a yardstick).
    double err = 0; long n = 0;
    const double k = (double)src.Width() / (double)size;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            for (int c = 0; c < 3; ++c) {
                double sum = 0; int cnt = 0;
                for (int sy = (int)(y * k); sy < (int)std::ceil((y + 1) * k); ++sy)
                    for (int sx = (int)(x * k); sx < (int)std::ceil((x + 1) * k); ++sx) { sum += Byte(src, std::min(sx, src.Width() - 1), std::min(sy, src.Height() - 1), c); ++cnt; }
                err += std::fabs(Byte(out, 4 + x, 4 + y, c) - sum / cnt);
                ++n;
            }
        }
    return err / (double)n;
}

// ---- 3. shadows ---------------------------------------------------------------------------------------------------------------------------
// A black shadow of a 80 x 50 rectangle (softness 16) on white. Returns the shadow alpha down the middle column below the bottom edge.
std::vector<double> ShadowProfile(int quality) {
    Scene s;
    s.uiQuality = quality;
    GameObject* cv = MakeCanvas(s, 200, 180, false);
    auto* p = AddTo<UIPanel>(s, cv, "p");
    p->position = {60, 30}; p->size = {80, 50}; p->shape = UIShape::Rectangle; p->color = Color::FromBytes(255, 255, 255, 255);
    p->shadow = true; p->shadowOffset = {0, 0}; p->shadowSoftness = 16.0f; p->shadowColor = Color::FromBytes(0, 0, 0, 255);
    Image out;
    std::vector<double> a;
    if (!RenderScene(s, 200, 180, out, false, 255, 255, 255)) return a;
    for (int y = 80; y < 80 + 24; ++y) a.push_back(1.0 - Byte(out, 100, y, 0) / 255.0);   // distance below the edge: y + 0.5 - 80
    return a;
}

void ShadowTests() {
    const std::vector<double> good = ShadowProfile(1), old = ShadowProfile(3);
    CHECK(good.size() == 24 && old.size() == 24);
    if (good.size() != 24) return;
    const double sigma = 16.0 * 0.4;
    double worst = 0, maxStep = 0, oldStep = 0;
    for (int i = 0; i < 24; ++i) {
        const double d = i + 0.5;
        const double want = 0.5 * Erfc(d / (sigma * 1.41421356));
        worst = std::max(worst, std::fabs(good[i] - want));
        if (i > 0) { maxStep = std::max(maxStep, std::fabs(good[i] - good[i - 1])); oldStep = std::max(oldStep, std::fabs(old[i] - old[i - 1])); }
        if (i > 0) CHECK(good[i] <= good[i - 1] + 0.01);   // fades monotonically
    }
    std::printf("soft shadow: worst error vs a Gaussian-blurred edge %.3f; biggest step between neighbouring pixels %.3f (Old look %.3f)\n", worst, maxStep, oldStep);
    CHECK(worst < 0.06);
    CHECK(maxStep < 0.075);
    CHECK(good[0] > 0.35 && good[0] < 0.55);        // half strength at the edge
    CHECK(good[23] < 0.06);                          // gone at the far end
}

// ---- 4. shapes and borders at a canvas scale of 1.5 -----------------------------------------------------------------------------------------
void ScaledShapeTests() {
    // A 60 x 30 rounded panel with a 1 px red border and a blue fill at (10, 10) of a 100 x 60 canvas, drawn at 150 x 90 (scale 1.5).
    auto render = [](int quality) {
        Scene s; s.uiQuality = quality;
        GameObject* cv = MakeCanvas(s, 100, 60, true);
        auto* p = AddTo<UIPanel>(s, cv, "p");
        p->position = {10, 10}; p->size = {60, 30}; p->shape = UIShape::Rounded; p->cornerRadius = 6.0f;
        p->color = Color::FromBytes(0, 0, 255, 255); p->borderWidth = 1.0f; p->borderColor = Color::FromBytes(255, 0, 0, 255);
        Image out; RenderScene(s, 150, 90, out, false, 0, 0, 0);
        return out;
    };
    const Image good = render(1), old = render(3);
    // Across the middle row, left edge: black, then the border, then the fill: nothing in between.
    auto classify = [](const Image& im, int y, int x0, int x1, int& mixed, int& border) {
        mixed = 0; border = 0;
        for (int x = x0; x < x1; ++x) {
            const int r = Byte(im, x, y, 0), g = Byte(im, x, y, 1), b = Byte(im, x, y, 2);
            const bool bg = r < 4 && g < 4 && b < 4, red = r > 250 && g < 4 && b < 4, blue = b > 250 && r < 4 && g < 4;
            if (red) ++border;
            if (!bg && !red && !blue) ++mixed;
        }
    };
    int mg, bg_, mo, bo;
    classify(good, 37, 10, 40, mg, bg_);
    classify(old, 37, 10, 40, mo, bo);
    std::printf("left border at 1.5x: Good %d solid border pixels, %d blended; Old look %d solid, %d blended\n", bg_, mg, bo, mo);
    CHECK(mg == 0);
    CHECK(bg_ == 2);   // 1 px x 1.5 rounds to 2 device pixels
    CHECK(good.GetPixel(14, 37).r < 0.02f && good.GetPixel(15, 37).r > 0.98f);   // the card starts on device pixel 15 (10 x 1.5)

    // A circle of 24 logical px (36 device px) at 1.5x: every edge pixel's coverage against a 16 x 16 supersampled analytic circle.
    auto circle = [](int quality) {
        Scene s; s.uiQuality = quality;
        GameObject* cv = MakeCanvas(s, 100, 60, true);
        auto* p = AddTo<UIPanel>(s, cv, "c");
        p->position = {30.3f, 10}; p->size = {24, 24}; p->shape = UIShape::Circle; p->color = Color::White;   // a fractional logical position on purpose
        Image out; RenderScene(s, 150, 90, out, false, 0, 0, 0);
        return out;
    };
    const Image cg = circle(1);
    // Fit the circle: centroid of the white mass, radius from the area.
    double m = 0, mx = 0, my = 0;
    for (int y = 0; y < 90; ++y) for (int x = 0; x < 150; ++x) { const double v = Byte(cg, x, y, 0) / 255.0; m += v; mx += v * (x + 0.5); my += v * (y + 0.5); }
    const double cx = mx / m, cy = my / m, R = std::sqrt(m / 3.14159265);
    double err = 0, worst = 0; long n = 0;
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 150; ++x) {
            const double dc = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            if (dc < R - 3 || dc > R + 3) continue;
            double ref = 0;
            for (int sy = 0; sy < 16; ++sy) for (int sx = 0; sx < 16; ++sx)
                if (std::hypot(x + (sx + 0.5) / 16.0 - cx, y + (sy + 0.5) / 16.0 - cy) <= R) ref += 1.0 / 256.0;
            const double e = std::fabs(Byte(cg, x, y, 0) / 255.0 - ref);
            err += e; worst = std::max(worst, e); ++n;
        }
    std::printf("circle at 1.5x: radius %.2f device px (ideal 18), edge coverage error mean %.4f, worst %.3f\n", R, err / n, worst);
    CHECK_NEAR(R, 18.0, 0.35);
    CHECK(err / n < 0.03);
    CHECK(worst < 0.3);
}

// ---- 5. text on the pixel grid -----------------------------------------------------------------------------------------------------------
Image TextShot(int quality, float x, float y, float px, int W, int H, bool scaled15) {
    Scene s; s.uiQuality = quality;
    GameObject* cv = MakeCanvas(s, scaled15 ? (int)(W / 1.5f) : W, scaled15 ? (int)(H / 1.5f) : H, scaled15);
    auto* t = AddTo<TextRenderer>(s, cv, "t");
    t->screenSpace = true; t->text = "HIlTiEhm"; t->screenPos = {x, y}; t->size = {200, 40}; t->pixelSize = px;
    t->color = Color::White; t->shadow = false; t->outline = false;
    Image out; RenderScene(s, W, H, out, false, 0, 0, 0);
    return out;
}

// Where glyph quads land: the share of quads whose left and top edge sit on a whole device pixel (within 0.03).
double GlyphsOnGrid(int quality, float x, float y, float px, bool scaled15, int* count = nullptr) {
    Scene s; s.uiQuality = quality;
    const int W = scaled15 ? 300 : 200, H = scaled15 ? 90 : 60;
    GameObject* cv = MakeCanvas(s, 200, 60, scaled15);
    auto* t = AddTo<TextRenderer>(s, cv, "t");
    t->screenSpace = true; t->text = "Hello Wagon 123, fi"; t->screenPos = {x, y}; t->size = {190, 40}; t->pixelSize = px;
    t->color = Color::White; t->shadow = false; t->outline = false;
    uitest::RecordPainter rec;
    ui2d::RenderSceneUI(s, rec, W, H);
    const float S = scaled15 ? 1.5f : 1.0f;
    int n = 0, on = 0;
    for (const auto& k : rec.inks) {
        if (!k.glyph) continue;
        ++n;
        const float fx = k.x * S - std::floor(k.x * S), fy = k.y * S - std::floor(k.y * S);
        const bool gx = fx < 0.03f || fx > 0.97f, gy = fy < 0.03f || fy > 0.97f;
        if (gx && gy) ++on;
    }
    if (count) *count = n;
    return n ? (double)on / (double)n : 0.0;
}

void TextTests() {
    // Snapped to the device pixel grid, small text at a canvas scale of 1.5 is sharper than the resampled Old look.
    const double fastSharp = Sharpness(TextShot(0, 10.3f, 8.2f, 1.5f, 210, 60, true), 0, 0, 210, 60, 0, 255);
    const double oldSharp = Sharpness(TextShot(3, 10.3f, 8.2f, 1.5f, 210, 60, true), 0, 0, 210, 60, 0, 255);
    const double goodSharp = Sharpness(TextShot(1, 10.3f, 8.2f, 1.5f, 210, 60, true), 0, 0, 210, 60, 0, 255);
    std::printf("text at 1.5x: sharpness (ink squared / ink) Fast %.3f, Good %.3f, Old look %.3f\n", fastSharp, goodSharp, oldSharp);
    CHECK(fastSharp > oldSharp + 0.03);
    CHECK(goodSharp > oldSharp + 0.03);

    // Where the glyph quads land: every one on a whole device pixel in Fast, Good and Best; the Old look puts them anywhere.
    for (bool scaled : {false, true}) {
        int n = 0;
        const double fast = GlyphsOnGrid(0, 10.3f, 8.2f, 1.5f, scaled, &n), good = GlyphsOnGrid(1, 10.3f, 8.2f, 1.5f, scaled, nullptr);
        const double best = GlyphsOnGrid(2, 10.3f, 8.2f, 1.5f, scaled, nullptr), old = GlyphsOnGrid(3, 10.3f, 8.2f, 1.5f, scaled, nullptr);
        std::printf("glyph quads on the pixel grid (%d glyphs, scale %s): Fast %.2f, Good %.2f, Best %.2f, Old look %.2f\n", n, scaled ? "1.5" : "1", fast, good, best, old);
        CHECK(n >= 14);
        CHECK(fast > 0.99 && good > 0.99 && best > 0.99);
        CHECK(old < 0.5);
    }

    // Quarter-pixel text positions (Best): the ink moves with the position. Good rounds each letter to a whole pixel, so on average it
    // follows the move too, but only to within half a pixel.
    auto shift = [&](int quality, float off) {
        const Image a = TextShot(quality, 10.0f, 6.0f, 1.5f, 120, 40, false), b = TextShot(quality, 10.0f + off, 6.0f, 1.5f, 120, 40, false);
        return InkCentroidX(b, 0, 0, 120, 40, 0) - InkCentroidX(a, 0, 0, 120, 40, 0);
    };
    for (float off : {0.25f, 0.5f, 0.75f}) {
        const double best = shift(2, off), good = shift(1, off);
        std::printf("text moved by %.2f px: ink moved %.3f in Best, %.3f in Good\n", off, best, good);
        CHECK_NEAR(best, off, 0.1);
        CHECK_NEAR(good, off, 0.5);
    }
}

// ---- 6. gradients ---------------------------------------------------------------------------------------------------------------------------
void GradientTests() {
    auto render = [](int quality) {
        Scene s; s.uiQuality = quality;
        GameObject* cv = MakeCanvas(s, 120, 80, false);
        auto* p = AddTo<UIPanel>(s, cv, "g");
        p->position = {10, 10}; p->size = {100, 60}; p->shape = UIShape::Rounded; p->cornerRadius = 4.0f;
        p->color = Color::FromBytes(40, 40, 40, 255); p->useGradient = true; p->colorBottom = Color::FromBytes(46, 46, 46, 255);
        Image out; RenderScene(s, 120, 80, out, false, 0, 0, 0);
        return out;
    };
    const Image good = render(1), best = render(2);
    // Good: every row is one level (bands). Best: rows mix two neighbouring levels while the row mean follows the ramp.
    int distinctGoodRow = 0, mixedRows = 0;
    double meanErr = 0; int rows = 0;
    for (int y = 14; y < 66; ++y) {
        std::set<int> gset, bset; double sum = 0;
        for (int x = 20; x < 100; ++x) { gset.insert(Byte(good, x, y, 0)); bset.insert(Byte(best, x, y, 0)); sum += Byte(best, x, y, 0); }
        distinctGoodRow = std::max(distinctGoodRow, (int)gset.size());
        if (bset.size() >= 2) ++mixedRows;
        const double ideal = 40.0 + 6.0 * ((y - 10 + 0.5) / 59.0);
        meanErr += std::fabs(sum / 80.0 - ideal); ++rows;
    }
    std::printf("gradient 40..46 over 60 rows: Good has %d level(s) per row; Best mixes levels in %d of %d rows, mean row error %.3f levels\n", distinctGoodRow, mixedRows, rows, meanErr / rows);
    CHECK(distinctGoodRow == 1);
    CHECK(mixedRows >= rows * 0.6);
    CHECK(meanErr / rows < 0.35);
}

// ---- 7. the setting ------------------------------------------------------------------------------------------------------------------------
void SettingTests() {
    Scene a;
    CHECK(a.uiQuality == 1);   // Good by default
    CHECK(SceneSerializer::Serialize(a).find("uiquality") == std::string::npos);   // the default is not written: old files stay as they were
    a.uiQuality = 2;
    const std::string text = SceneSerializer::Serialize(a);
    CHECK(text.find("uiquality 2") != std::string::npos);
    Scene b;
    std::string err;
    CHECK(SceneSerializer::Deserialize(b, text, &err));
    CHECK(b.uiQuality == 2);
    Scene c; c.uiQuality = 3;
    CHECK(SceneSerializer::Deserialize(c, SceneSerializer::Serialize(Scene()), &err));
    CHECK(c.uiQuality == 1);   // a scene without the record is Good again
    CHECK(UIQualityFor(0) == UIQuality::Fast && UIQualityFor(1) == UIQuality::Good && UIQualityFor(2) == UIQuality::Best && UIQualityFor(3) == UIQuality::Old);
    CHECK(UIQualityFor(-4) == UIQuality::Fast && UIQualityFor(9) == UIQuality::Old);
}

// ---- 8. the whole screen: it draws, the batched path agrees, and the numbers --------------------------------------------------------------------
void ScreenTests(const std::string& tex) {
    for (int q = 0; q < 4; ++q) {
        Scene s; Build(s, tex, q);
        Image direct, batched;
        CHECK(RenderScene(s, 1280, 720, direct));
        CHECK(RenderScene(s, 1280, 720, batched, true));
        long far = 0, ink = 0;
        for (int y = 0; y < 720; ++y)
            for (int x = 0; x < 1280; ++x) {
                const int d = std::abs(Byte(direct, x, y, 0) - Byte(batched, x, y, 0)) + std::abs(Byte(direct, x, y, 1) - Byte(batched, x, y, 1)) + std::abs(Byte(direct, x, y, 2) - Byte(batched, x, y, 2));
                if (d > 90) ++far;
                if (Byte(direct, x, y, 0) > 40) ++ink;
            }
        std::printf("quality %d: batched path differs strongly on %ld of %ld lit pixels\n", q, far, ink);
        CHECK(ink > 20000);
        CHECK(far < ink / 30);   // the batched (player) path draws the same picture: under 3% of lit pixels differ by more than a third of full
    }
    // The draw time of the whole screen at 1280x720 on the software renderer, per quality (informational; the UI CPU benchmark is test_ui_render_bench).
    for (int q : {3, 0, 1, 2}) {
        Scene s; Build(s, tex, q);
        Image out;
        RenderScene(s, 1280, 720, out);   // warm up: font faces, atlases
        const auto t0 = std::chrono::steady_clock::now();
        const int frames = 5;
        for (int i = 0; i < frames; ++i) RenderScene(s, 1280, 720, out);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
        static const char* names[] = {"Fast", "Good", "Best", "Old look"};
        std::printf("reference screen 1280x720, software renderer, %-8s: %.1f ms per frame (includes the renderer setup and read-back)\n", names[q], ms);
        CHECK(ms < 3000.0);
    }
}

} // namespace

int main() {
    RUN_SUITE("ui_quality");
    const std::string dir = OKAY_TEST_OUTPUT_DIR;
    const std::string tex = dir + "/ui_quality_tex.png";
    CHECK(WriteTexture(tex));
    FontTests();
    ImageTests();
    {
        const double good = PictureError(tex, 1, 36), old = PictureError(tex, 3, 36), best = PictureError(tex, 2, 61), oldB = PictureError(tex, 3, 61);
        std::printf("picture 128 -> 36: mean error vs the area average, Good %.2f, Old look %.2f; 128 -> 61: Best %.2f, Old look %.2f (levels of 255)\n", good, old, best, oldB);
        CHECK(good < old * 0.7);
        CHECK(best < oldB * 0.8);
        CHECK(good < 22.0);
        // Pixel art: the picture is not filtered (the pixel look keeps nearest sampling), so Good and Old draw it identically.
        const double pg = PictureError(tex, 1, 36, true), po = PictureError(tex, 3, 36, true);
        CHECK_NEAR(pg, po, 1e-6);
    }
    ShadowTests();
    ScaledShapeTests();
    TextTests();
    GradientTests();
    SettingTests();
    ScreenTests(tex);
    TEST_MAIN_RESULT();
}
