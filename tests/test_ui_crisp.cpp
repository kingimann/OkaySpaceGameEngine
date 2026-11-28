// In-game UI must stay crisp when magnified (owner report: "if you zoom in it's pixelated").
// The scene in ui_crisp_scene.hpp is drawn by the real UI pass on the software painter at 1x..4x
// of its 320x180 reference canvas and measured:
//   * text, circles, rings and pictures are rasterised at the physical pixel size, not magnified
//     from a small bitmap ("block detail": how much a render varies inside an aligned block; a
//     nearest-neighbour magnified picture has none);
//   * a ring's edge matches an ideal anti-aliased annulus (coverage error) and has soft edge pixels;
//   * pictures are filtered smoothly, or with nearest in the pixel-art look;
//   * the glyph cache is keyed by size and bounded; renders are deterministic.
#include "test_framework.hpp"
#include "ui_crisp_render.hpp"
#include "okay/UI/UIFontPolicy.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace okay;

namespace {

int Chan(const Image& im, int x, int y, int c) {
    const Color p = im.GetPixel(x, y);
    const float v = c == 0 ? p.r : (c == 1 ? p.g : p.b);
    return (int)(v * 255.0f + 0.5f);
}

// Mean absolute difference (0..255, over RGB) between each pixel and the mean of its aligned BxB block.
double BlockDetail(const Image& im, int x0, int y0, int w, int h, int B) {
    double sum = 0.0; long n = 0;
    for (int by = y0; by + B <= y0 + h; by += B)
        for (int bx = x0; bx + B <= x0 + w; bx += B)
            for (int c = 0; c < 3; ++c) {
                double mean = 0.0;
                for (int y = 0; y < B; ++y) for (int x = 0; x < B; ++x) mean += Chan(im, bx + x, by + y, c);
                mean /= (double)(B * B);
                for (int y = 0; y < B; ++y) for (int x = 0; x < B; ++x) sum += std::fabs(Chan(im, bx + x, by + y, c) - mean);
                n += B * B;
            }
    return n ? sum / (double)n : 0.0;
}

// Bounding box of pixels whose red channel differs from the flat background by more than `thr`.
bool InkBox(const Image& im, int x0, int y0, int w, int h, int bgR, int thr, int& bx0, int& by0, int& bx1, int& by1) {
    bx0 = by0 = 1 << 30; bx1 = by1 = -1;
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            if (std::abs(Chan(im, x, y, 0) - bgR) > thr) {
                bx0 = std::min(bx0, x); by0 = std::min(by0, y); bx1 = std::max(bx1, x); by1 = std::max(by1, y);
            }
    return bx1 >= 0;
}

// How much of the pixel is the red ring: the red channel minus the green one (the ring is 255,90,90, the background 14,16,22, the card's border and the
// text are grey or blue, so they read as nothing). Anything between is a blend, so this is the coverage.
double kRingT(const Image& im, int x, int y) { return (Chan(im, x, y, 0) - Chan(im, x, y, 1) + 2.0) / 167.0; }

struct RingFit { double cx = 0, cy = 0, err = 0, soft = 0, perimeter = 0; };

// Fit the red reticle ring (flat dark background, ring colour red 255) drawn near `hint` and compare its
// coverage with an ideal anti-aliased annulus [rIn, rOut] (device pixels) around the fitted centre.
RingFit FitRing(const Image& im, int x0, int y0, int w, int h, double rIn, double rOut) {
    RingFit f;
    const int bgR = 14;
    double m = 0, mx = 0, my = 0;
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) {
            const double t = kRingT(im, x, y);
            if (t > 0) { m += t; mx += t * (x + 0.5); my += t * (y + 0.5); }
        }
    if (m <= 0) return f;
    f.cx = mx / m; f.cy = my / m;
    double e = 0; long n = 0, soft = 0;
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) {
            const double t = std::min(1.0, std::max(0.0, kRingT(im, x, y)));
            double ref = 0.0;   // 8x8 supersampled ideal annulus
            for (int sy = 0; sy < 8; ++sy)
                for (int sx = 0; sx < 8; ++sx) {
                    const double px = x + (sx + 0.5) / 8.0 - f.cx, py = y + (sy + 0.5) / 8.0 - f.cy;
                    const double d = std::sqrt(px * px + py * py);
                    if (d >= rIn && d <= rOut) ref += 1.0 / 64.0;
                }
            const double dc = std::hypot(x + 0.5 - f.cx, y + 0.5 - f.cy);
            if (dc > rIn - 3 && dc < rOut + 3) { e += std::fabs(t - ref); ++n; if (t > 0.05 && t < 0.95) ++soft; }
        }
    f.err = n ? e / (double)n : 0.0;
    f.soft = (double)soft;
    f.perimeter = 2.0 * 3.14159265 * 0.5 * (rIn + rOut) * 2.0;   // both edges, in device pixels
    return f;
}

} // namespace

int main() {
    RUN_SUITE("ui_crisp");
    const std::string dir = OKAY_TEST_OUTPUT_DIR;
    const std::string tex = dir + "/ui_crisp_tex.png";
    CHECK(ui_crisp::WriteTexture(tex));

    Image r1, r4, r4pix, r4b, r4bpix, r3, r4again, r4nearest;
    CHECK(ui_crisp::Render(320, 180, tex, false, false, r1));
    CHECK(ui_crisp::Render(960, 540, tex, false, false, r3));
    CHECK(ui_crisp::Render(1280, 720, tex, false, false, r4));
    CHECK(ui_crisp::Render(1280, 720, tex, true, false, r4pix));
    CHECK(ui_crisp::Render(1280, 720, tex, false, true, r4b));      // the player's batched path
    CHECK(ui_crisp::Render(1280, 720, tex, true, true, r4bpix));
    CHECK(ui_crisp::Render(1280, 720, tex, false, false, r4again));

    // A bitmap-magnified UI (what the owner saw): the 1x render blown up 4x with nearest.
    r4nearest = Image(1280, 720);
    for (int y = 0; y < 720; ++y) for (int x = 0; x < 1280; ++x) r4nearest.SetPixel(x, y, r1.GetPixel(x / 4, y / 4));

    // ---- determinism ----
    {
        // Two renders of the same scene match, except for a few pixels on the soft edge of the round picture: SDL's software renderer does not give
        // bit-identical results for textured triangles whose outer vertices are fully see-through (it varies a little with what was drawn before in the
        // process). Those pixels differ by one level of 255. So: at most 16 pixels may differ, none by more than 2.
        int nd = 0, worst = 0;
        for (int y = 0; y < 720; ++y)
            for (int x = 0; x < 1280; ++x) {
                const Color a = r4.GetPixel(x, y), b = r4again.GetPixel(x, y);
                const int d = std::max({std::abs((int)(a.r * 255.0f + 0.5f) - (int)(b.r * 255.0f + 0.5f)), std::abs((int)(a.g * 255.0f + 0.5f) - (int)(b.g * 255.0f + 0.5f)),
                                        std::abs((int)(a.b * 255.0f + 0.5f) - (int)(b.b * 255.0f + 0.5f))});
                if (d > 0) { ++nd; worst = std::max(worst, d); }
            }
        std::printf("two renders of the scene: %d pixels differ, by at most %d\n", nd, worst);
        CHECK(nd <= 16);
        CHECK(worst <= 2);
    }

    // ---- flat fills have no hairlines: at 4x a panel or button body is one colour all the way (a float rect truncated by the software renderer
    // left a one-pixel gap between neighbouring strips) ----
    {
        int offCard = 0, offBtn = 0;
        for (int y = 380; y < 460; ++y) for (int x = 100; x < 560; ++x) {
            if (Chan(r4, x, y, 0) != 28 || Chan(r4, x, y, 1) != 32 || Chan(r4, x, y, 2) != 44) ++offCard;
        }
        for (int y = 170; y < 246; ++y) for (int x = 80; x < 130; ++x) {   // the button's left part, left of its label
            if (Chan(r4, x, y, 0) != 66 || Chan(r4, x, y, 1) != 140 || Chan(r4, x, y, 2) != 250) ++offBtn;
        }
        std::printf("flat fill pixels that are not the fill colour: card %d, button %d\n", offCard, offBtn);
        CHECK(offCard == 0);
        CHECK(offBtn == 0);
    }

    // ---- text: rasterised at the physical size ----
    {
        const int tx = 64, ty = 48, tw = 520, th = 100;   // the title and the button label area (device px, 4x)
        const double nearest = BlockDetail(r4nearest, tx, ty, tw, th, 4);
        const double crisp = BlockDetail(r4, tx, ty, tw, th, 4);
        std::printf("text block detail: magnified 1x %.3f, native 4x %.3f\n", nearest, crisp);
        CHECK(nearest < 0.01);        // the metric sees a bitmap magnification
        CHECK(crisp > 1.5);           // native 4x text has detail inside every 4x4 block
        // Glyph height scales with the canvas scale (rasterised at size x zoom, not magnified).
        int a0, b0, a1, b1, c0, d0, c1, d1, e0, f0, e1, f1;
        CHECK(InkBox(r1, 16, 12, 130, 14, 14, 40, a0, b0, a1, b1));
        CHECK(InkBox(r3, 48, 36, 390, 42, 14, 40, c0, d0, c1, d1));
        CHECK(InkBox(r4, 64, 48, 520, 56, 14, 40, e0, f0, e1, f1));
        const double h1 = b1 - b0 + 1, h3 = d1 - d0 + 1, h4 = f1 - f0 + 1;
        std::printf("title ink height: 1x %.0f, 3x %.0f, 4x %.0f\n", h1, h3, h4);
        CHECK(std::fabs(h3 / h1 - 3.0) < 0.5);
        CHECK(std::fabs(h4 / h1 - 4.0) < 0.6);
    }

    // ---- shapes: ring coverage against an ideal annulus ----
    {
        // The reticle ring: bottom-left anchored (40,-30) from the bottom edge, radius 14 + thickness 2.
        // The reticle is a 24 x 24 box anchored bottom-left at (40,-30), so its centre is at (52,138) logical, (208,552) device.
        const int x0 = 80, y0 = 440, w = 260, h = 260;
        const RingFit smooth = FitRing(r4, x0, y0, w, h, 4 * 14.5, 4 * 16.5);
        const RingFit hard = FitRing(r4pix, x0, y0, w, h, 4 * 14.5, 4 * 16.5);
        const RingFit smoothB = FitRing(r4b, x0, y0, w, h, 4 * 14.5, 4 * 16.5);
        std::printf("ring coverage error: smooth %.4f (soft px %.0f of %.0f), batched %.4f, pixel-art %.4f (soft %.0f)\n",
                    smooth.err, smooth.soft, smooth.perimeter, smoothB.err, hard.err, hard.soft);
        CHECK(smooth.err < 0.045);
        CHECK(smoothB.err < 0.045);
        CHECK(smooth.soft > 0.6 * smooth.perimeter);     // the edge is anti-aliased
        CHECK(hard.err > smooth.err * 1.5);              // the old hard rasteriser is clearly worse
        const double ringDetail = BlockDetail(r4, x0, y0, w - w % 4, h, 4);
        const double ringDetailOld = BlockDetail(r4nearest, x0, y0, w - w % 4, h, 4);
        std::printf("ring block detail: native %.3f, magnified 1x %.3f\n", ringDetail, ringDetailOld);
        CHECK(ringDetail > 0.5);
        CHECK(ringDetailOld < 0.01);
    }

    // ---- pictures: smooth by default, nearest in the pixel-art look ----
    {
        // The 48x48 picture at (172,8): a 16x16 texture magnified 12x.
        const int px = 4 * 172, py = 4 * 8, pw = 192, ph = 192;
        const double smooth = BlockDetail(r4, px, py, pw, ph, 12);
        const double smoothB = BlockDetail(r4b, px, py, pw, ph, 12);
        const double hard = BlockDetail(r4pix, px, py, pw, ph, 12);
        const double hardB = BlockDetail(r4bpix, px, py, pw, ph, 12);
        std::printf("picture block detail (12x12): smooth %.3f, smooth batched %.3f, pixel-art %.3f, pixel-art batched %.3f\n",
                    smooth, smoothB, hard, hardB);
        CHECK(smooth > 2.0);
        CHECK(smoothB > 2.0);
        CHECK(hard < 0.5);       // pixel-art: every texel stays one flat block
        CHECK(hardB < 0.5);
        // The round picture draws, its corner stays background, its middle does not.
        const int rx = 4 * 232, ry = 4 * 8;
        CHECK(Chan(r4, rx + 2, ry + 2, 0) == 14);
        CHECK(Chan(r4, rx + 96, ry + 96, 2) != 22);
    }

    // ---- glyph cache: keyed by size, bounded ----
    {
        TtfFont* ui = SceneUIFont("");
        CHECK(ui && ui->Valid());
        std::set<TtfFont*> faces;
        bool nearEnough = true, stable = true;
        for (float px = 8.0f; px <= 256.0f; px += 0.37f) {   // (the font never bakes below 8 px; UI text that small is not drawn)
            TtfFont* f = ui->ForPixelHeight(px);
            faces.insert(f);
            if (ui->ForPixelHeight(px) != f) stable = false;
            const float bh = f->BakeHeight();
            if (px <= 32.0f ? std::fabs(bh - std::round(px)) > 0.51f : std::fabs(bh - px) / px > 0.045f)   /* steps are ~6% apart (3% off at worst) and the request is rounded to a whole pixel before it is stepped, which adds up to 1.5% more */ { if (nearEnough) std::printf("  first bake mismatch: wanted %.2f px, baked %.2f\n", px, bh); nearEnough = false; }
        }
        std::printf("distinct baked faces for 8..256 px in 0.37 steps: %zu\n", faces.size());
        CHECK(stable);
        CHECK(nearEnough);
        CHECK(faces.size() <= 80);
        CHECK(ui->ForPixelHeight(16.0f) != ui->ForPixelHeight(32.0f));
        CHECK(std::fabs(ui->ForPixelHeight(16.0f)->BakeHeight() - 16.0f) < 0.01f);
        CHECK(std::fabs(ui->ForPixelHeight(64.0f)->BakeHeight() - 64.0f) < 0.01f ||
              std::fabs(ui->ForPixelHeight(64.0f)->BakeHeight() - 64.0f) / 64.0f < 0.035f);
    }

    std::remove(tex.c_str());
    TEST_MAIN_RESULT();
}
