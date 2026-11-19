// The Direct3D 11 painter's geometry (engine/src/Platform/D3D11Batch.hpp): rectangles, scissor, textured quads with rotation and flips, geometry,
// checked against numbers worked out by hand, and its pixel coverage against the software painter on a tiny CPU rasteriser of the same triangles
// (a pixel is covered when its centre is inside a triangle, the rule a GPU uses). Runs anywhere: no Direct3D.
#include "Platform/D3D11Batch.hpp"
#ifndef D3D_BATCH_NO_SOFT   // (a quick build of the pure checks without the engine: -DD3D_BATCH_NO_SOFT)
#include "okay/Platform/Painter.hpp"
#include "OkayUI_SoftPainter.hpp"
#endif

#include <cmath>
#include <cstdio>
#include <vector>

using namespace okay::platform::d3d;
namespace ui2d = okay::ui2d;
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
#define NEAR(a, b) (std::fabs((a) - (b)) < 1e-4)

// Fill triangles into an RGBA buffer, opaque, pixel-centre rule (top-left bias ignored: the shapes below avoid pixel-centre ties).
static void Raster(const std::vector<Vtx>& v, std::vector<unsigned char>& px, int W, int H) {
    for (size_t i = 0; i + 3 <= v.size(); i += 3) {
        const Vtx &a = v[i], &b = v[i + 1], &c = v[i + 2];
        const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (area == 0) continue;
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            const double X = x + 0.5, Y = y + 0.5;
            const double w0 = ((b.x - X) * (c.y - Y) - (b.y - Y) * (c.x - X)) / area;
            const double w1 = ((c.x - X) * (a.y - Y) - (c.y - Y) * (a.x - X)) / area;
            const double w2 = 1 - w0 - w1;
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) { unsigned char* p = &px[((size_t)y * W + x) * 4]; p[0] = a.r; p[1] = a.g; p[2] = a.b; p[3] = a.a; }
        }
    }
}

int main() {
    // scale and rounding
    DevRect d = ScaleRect(ui2d::Rect{3, 4, 10, 5}, 2.0f, 2.0f);
    CHECK(d.x0 == 6 && d.y0 == 8 && d.x1 == 26 && d.y1 == 18);
    d = ScaleRect(ui2d::Rect{1, 1, 3, 3}, 1.5f, 1.5f);   // 1.5 -> 2, 6.0 -> 6 (half up)
    CHECK(d.x0 == 2 && d.y0 == 2 && d.x1 == 6 && d.y1 == 6);
    d = ScaleRectF(ui2d::FRect{0.4f, 0.6f, 2.0f, 2.0f}, 1.0f, 1.0f);   // pixel centres 0.5, 1.5 inside [0.4, 2.4): pixels 0..2, y: centres 1.5, 2.5 inside [0.6, 2.6)
    CHECK(d.x0 == 0 && d.x1 == 2 && d.y0 == 1 && d.y1 == 3);
    // scissor
    d = Scissor(false, ui2d::Rect{}, 1, 1, 100, 50); CHECK(d.x0 == 0 && d.y1 == 50 && d.x1 == 100);
    d = Scissor(true, ui2d::Rect{10, 10, 30, 100}, 2, 2, 100, 50); CHECK(d.x0 == 20 && d.y0 == 20 && d.x1 == 80 && d.y1 == 50);   // cut to the target
    d = Scissor(true, ui2d::Rect{10, 10, 0, 5}, 1, 1, 100, 50); CHECK(d.Empty());                                                    // empty clip: nothing shows
    d = Scissor(true, ui2d::Rect{200, 10, 5, 5}, 1, 1, 100, 50); CHECK(d.Empty());                                                   // entirely outside
    // quad and clip space
    std::vector<Vtx> v;
    Quad(v, 2, 3, 6, 9, 10, 20, 30, 40);
    CHECK(v.size() == 6 && v[0].x == 2 && v[0].y == 3 && v[2].x == 6 && v[2].y == 9 && v[5].x == 2 && v[5].y == 9 && v[1].u == 1 && v[1].v == 0 && v[0].r == 10 && v[0].a == 40);
    float cx, cy; ToClip(0, 0, 200, 100, cx, cy); CHECK(NEAR(cx, -1) && NEAR(cy, 1)); ToClip(200, 100, 200, 100, cx, cy); CHECK(NEAR(cx, 1) && NEAR(cy, -1)); ToClip(100, 50, 200, 100, cx, cy); CHECK(NEAR(cx, 0) && NEAR(cy, 0));
    // texture quads
    v.clear();
    CHECK(TexQuad(v, 8, 4, nullptr, 10, 20, 16, 8, 2, 2, 0, nullptr, 0, 255, 255, 255, 255));
    CHECK(NEAR(v[0].x, 20) && NEAR(v[0].y, 40) && NEAR(v[2].x, 52) && NEAR(v[2].y, 56) && v[0].u == 0 && v[2].u == 1 && v[2].v == 1);
    v.clear(); ui2d::Rect src{2, 1, 4, 2};
    CHECK(TexQuad(v, 8, 4, &src, 0, 0, 4, 2, 1, 1, 0, nullptr, ui2d::FlipH, 255, 255, 255, 255));
    CHECK(NEAR(v[0].u, 6.0 / 8) && NEAR(v[1].u, 2.0 / 8) && NEAR(v[0].v, 1.0 / 4) && NEAR(v[2].v, 3.0 / 4));        // horizontal flip swaps u
    v.clear(); CHECK(TexQuad(v, 8, 4, &src, 0, 0, 4, 2, 1, 1, 0, nullptr, ui2d::FlipV, 255, 255, 255, 255)); CHECK(NEAR(v[0].v, 3.0 / 4) && NEAR(v[2].v, 1.0 / 4));
    v.clear(); CHECK(TexQuad(v, 8, 4, nullptr, 0, 0, 10, 10, 1, 1, 90, nullptr, 0, 255, 255, 255, 255));            // 90 degrees about the middle (5, 5): (0,0) -> (10,0)
    CHECK(NEAR(v[0].x, 10) && NEAR(v[0].y, 0) && NEAR(v[1].x, 10) && NEAR(v[1].y, 10) && NEAR(v[2].x, 0) && NEAR(v[2].y, 10));
    v.clear(); ui2d::FPoint ctr{0, 0}; CHECK(TexQuad(v, 8, 4, nullptr, 5, 5, 10, 10, 1, 1, 90, &ctr, 0, 255, 255, 255, 255));   // about the top-left corner (5, 5)
    CHECK(NEAR(v[0].x, 5) && NEAR(v[0].y, 5) && NEAR(v[1].x, 5) && NEAR(v[1].y, 15));
    v.clear();
    CHECK(!TexQuad(v, 8, 4, nullptr, 0, 0, 0, 10, 1, 1, 0, nullptr, 0, 255, 255, 255, 255));                        // empty destination
    ui2d::Rect bad{6, 0, 4, 4};
    CHECK(!TexQuad(v, 8, 4, &bad, 0, 0, 4, 4, 1, 1, 0, nullptr, 0, 255, 255, 255, 255) && v.empty());               // source outside the texture
    CHECK(!TexQuad(v, 0, 4, nullptr, 0, 0, 4, 4, 1, 1, 0, nullptr, 0, 255, 255, 255, 255));
    // geometry
    ui2d::Vertex gv[4] = {{{0, 0}, {255, 0, 0, 255}, {0, 0}}, {{10, 0}, {0, 255, 0, 128}, {1, 0}}, {{10, 10}, {0, 0, 255, 255}, {1, 1}}, {{0, 10}, {255, 255, 255, 255}, {0, 1}}};
    int idx[9] = {0, 1, 2, 0, 2, 3, 0, 1, 9};   // the last triangle has a bad index
    v.clear(); GeometryVerts(v, gv, 4, idx, 9, 2, 3, 255, 255, 255, 255);
    CHECK(v.size() == 6 && NEAR(v[1].x, 20) && NEAR(v[1].y, 0) && NEAR(v[2].y, 30) && v[1].a == 128);
    v.clear(); GeometryVerts(v, gv, 4, nullptr, 0, 1, 1, 128, 255, 255, 64);   // no indices: vertices in order, only the first triangle; colour / alpha modulation
    CHECK(v.size() == 3 && v[0].r == 128 && v[0].a == 64 && v[1].g == 255 && v[1].a == 32);
    v.clear(); GeometryVerts(v, gv, 2, nullptr, 0, 1, 1, 255, 255, 255, 255); CHECK(v.empty());   // fewer than three

#ifndef D3D_BATCH_NO_SOFT
    // pixel coverage against the software painter for the same drawing: rects at scale 2, an outline, and a clipped textured quad
    {
        const int W = 64, H = 48;
        auto sw = OkayUI::soft::CreateBitmapPainter(W, H);
        CHECK(sw != nullptr);
        if (sw) {
            sw->SetBlendMode(okay::platform::BlendMode::None); sw->SetDrawColor(0, 0, 0, 255); sw->Clear();
            sw->SetScale(2.0f, 2.0f);
            sw->SetDrawColor(200, 10, 20, 255); ui2d::Rect fr{3, 4, 10, 5}; sw->FillRect(&fr);
            sw->SetDrawColor(10, 200, 20, 255); ui2d::Rect orr{16, 2, 8, 6}; sw->DrawRect(&orr);
            std::vector<unsigned char> mine((size_t)W * H * 4, 0);
            for (size_t i = 3; i < mine.size(); i += 4) mine[i] = 255;
            std::vector<Vtx> vv;
            FillDev(vv, ScaleRect(fr, 2, 2), 200, 10, 20, 255);
            OutlineDev(vv, ScaleRect(orr, 2, 2), 10, 200, 20, 255);
            Raster(vv, mine, W, H);
            std::vector<unsigned char> ref; int rw = 0, rh = 0; sw->ReadPixels(ref, rw, rh);
            int diff = 0; for (size_t i = 0; i < mine.size(); ++i) if (mine[i] != ref[i]) ++diff;
            std::printf("[coverage] differing bytes between the triangle rasteriser and the software painter: %d\n", diff);
            CHECK(diff == 0);
        }
    }
#endif
    // a line covers both end pixels and stays within a pixel of the segment
    {
        const int W = 32, H = 32;
        std::vector<Vtx> vv; LineQuad(vv, 4.5, 4.5, 20.5, 4.5, 1.0, 255, 255, 255, 255);   // a horizontal line along the middle of row 4
        std::vector<unsigned char> px((size_t)W * H * 4, 0); Raster(vv, px, W, H);
        auto on = [&](int x, int y) { return px[((size_t)y * W + x) * 4] == 255; };
        CHECK(on(4, 4) && on(20, 4) && on(12, 4) && !on(3, 4) && !on(21, 4) && !on(12, 3) && !on(12, 5));
        vv.clear(); LineQuad(vv, 6.5, 6.5, 6.5, 6.5, 1.0, 255, 255, 255, 255);              // a point: one pixel
        std::fill(px.begin(), px.end(), 0); Raster(vv, px, W, H);
        int n = 0; for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) if (on(x, y)) ++n;
        CHECK(n >= 1 && n <= 2 && on(6, 6));
    }
    std::printf("%d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
