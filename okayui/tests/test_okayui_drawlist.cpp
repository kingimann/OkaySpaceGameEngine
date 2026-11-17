// OkayUI draw list, cursor and measurement, item state and disabled blocks, headless: what each call draws is
// checked pixel by pixel through the software backend, and the layout and state queries as plain values.
// The golden image of the "Draw list lab" page (okayui/demo/DemoScene.hpp, okayui_headless_demo out.png lab) is here too.
#include "test_framework.hpp"
#include "../demo/DemoScene.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <cstring>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
using UI::Vec2;

namespace {

const UI::Color32 kRed = UI::RGBA(255, 0, 0), kGreen = UI::RGBA(0, 255, 0), kBlue = UI::RGBA(0, 0, 255);
const unsigned kBlack = Rgba(0, 0, 0), kPixRed = Rgba(255, 0, 0), kPixGreen = Rgba(0, 255, 0), kPixBlue = Rgba(0, 0, 255);

// One frame that only calls the draw list, then rasterised on black.
Bitmap Render(Sim& sim, const std::function<void()>& f) {
    Reset();
    sim.Frame(f);
    return sim.Draw(0, 0, 0);
}
int Count(const Bitmap& b, unsigned colour) {
    int n = 0;
    for (int y = 0; y < b.h; ++y) for (int x = 0; x < b.w; ++x) if (PixelAt(b, x, y) == colour) ++n;
    return n;
}
int Lit(const Bitmap& b) { return b.w * b.h - Count(b, kBlack); }

void ColourTests() {
    CHECK(UI::RGBA(1, 2, 3, 4) == 0x04030201u);
    CHECK(UI::RGBA(255, 0, 0) == 0xff0000ffu);
    CHECK(UI::RGBA(0x1ff, 0, 0, 0) == 0x000000ffu);                       // channels are masked
    CHECK(UI::ColorMulAlpha(UI::RGBA(10, 20, 30, 200), 0.5f) == UI::RGBA(10, 20, 30, 100));
    CHECK(UI::ColorMulAlpha(UI::RGBA(10, 20, 30, 200), 2.0f) == UI::RGBA(10, 20, 30, 200));   // clamped
    CHECK(UI::ColorMulAlpha(UI::RGBA(10, 20, 30, 200), -1.0f) == UI::RGBA(10, 20, 30, 0));
    CHECK(UI::ColorMulAlpha(UI::RGBA(10, 20, 30, 200), NAN) == UI::RGBA(10, 20, 30, 0));
    Reset();
    const unsigned char* t = UI::Style().text;
    CHECK(UI::GetColorU32(UI::Col_Text) == UI::RGBA(t[0], t[1], t[2], t[3]));
    CHECK(((UI::GetColorU32(UI::Col_Text, 0.5f) >> 24) & 255u) == (unsigned)(t[3] * 0.5f + 0.5f));
}

void ShapeTests() {
    Sim sim(160, 120);
    const UI::DrawList& dl = UI::GetWindowDrawList();
    {   // a filled rectangle covers exactly its pixels
        const Bitmap b = Render(sim, [&] { dl.AddRectFilled({10, 10}, {30, 20}, kRed); });
        CHECK(Count(b, kPixRed) == 200 && Lit(b) == 200);
        CHECK(PixelAt(b, 10, 10) == kPixRed && PixelAt(b, 29, 19) == kPixRed);
        CHECK(PixelAt(b, 9, 10) == kBlack && PixelAt(b, 30, 10) == kBlack && PixelAt(b, 10, 20) == kBlack);
    }
    {   // an outline is drawn inside the bounds: a 2 px border of a 20x10 box
        const Bitmap b = Render(sim, [&] { dl.AddRect({10, 10}, {30, 20}, kGreen, 0.0f, 2.0f); });
        CHECK(Count(b, kPixGreen) == 20 * 10 - 16 * 6 && Lit(b) == 104);
        CHECK(PixelAt(b, 10, 10) == kPixGreen && PixelAt(b, 29, 19) == kPixGreen && PixelAt(b, 20, 15) == kBlack);
        CHECK(PixelAt(b, 9, 15) == kBlack && PixelAt(b, 30, 15) == kBlack);
        // a stroke thicker than half the box fills it
        const Bitmap f = Render(sim, [&] { dl.AddRect({10, 10}, {20, 20}, kGreen, 0.0f, 6.0f); });
        CHECK(Count(f, kPixGreen) == 100);
    }
    {   // a rounded rectangle leaves its corners empty and its edge middles lit
        const Bitmap b = Render(sim, [&] { dl.AddRectFilled({10, 10}, {50, 40}, kRed, 8.0f); });
        CHECK(PixelAt(b, 10, 10) == kBlack && PixelAt(b, 49, 10) == kBlack && PixelAt(b, 10, 39) == kBlack && PixelAt(b, 49, 39) == kBlack);
        CHECK(PixelAt(b, 30, 10) == kPixRed && PixelAt(b, 10, 25) == kPixRed && PixelAt(b, 30, 25) == kPixRed);
        const int n = Count(b, kPixRed);
        CHECK(n < 40 * 30 && n > 40 * 30 - 4 * 8 * 8 / 2 - 40);     // fewer than the square by about four quarter-discs' corners
        const Bitmap o = Render(sim, [&] { dl.AddRect({10, 10}, {50, 40}, kRed, 8.0f, 2.0f); });
        CHECK(PixelAt(o, 30, 25) == kBlack && PixelAt(o, 30, 10) == kPixRed && PixelAt(o, 30, 11) == kPixRed && PixelAt(o, 10, 10) == kBlack);
    }
    {   // lines: a horizontal one of thickness 2 is exactly 2 rows tall
        const Bitmap b = Render(sim, [&] { dl.AddLine({10, 50}, {50, 50}, kBlue, 2.0f); });
        CHECK(Count(b, kPixBlue) == 80 && PixelAt(b, 30, 49) == kPixBlue && PixelAt(b, 30, 50) == kPixBlue && PixelAt(b, 30, 48) == kBlack && PixelAt(b, 30, 51) == kBlack);
        // a diagonal reaches both ends
        const Bitmap d = Render(sim, [&] { dl.AddLine({10, 10}, {60, 60}, kBlue, 3.0f); });
        CHECK(PixelAt(d, 11, 11) == kPixBlue && PixelAt(d, 58, 58) == kPixBlue && PixelAt(d, 35, 35) == kPixBlue && PixelAt(d, 50, 20) == kBlack);
    }
    {   // circles: area, centre, ring
        const Bitmap f = Render(sim, [&] { dl.AddCircleFilled({60, 60}, 20.0f, kRed); });
        const int area = Count(f, kPixRed);
        CHECK(area > 1150 && area < 1300);                                  // pi * 400 = 1257
        CHECK(PixelAt(f, 60, 60) == kPixRed && PixelAt(f, 41, 60) == kPixRed && PixelAt(f, 39, 60) == kBlack && PixelAt(f, 42, 42) == kBlack);
        const Bitmap r = Render(sim, [&] { dl.AddCircle({60, 60}, 20.0f, kRed, 0, 2.0f); });
        CHECK(PixelAt(r, 60, 60) == kBlack && PixelAt(r, 79, 60) == kPixRed && PixelAt(r, 81, 60) == kBlack && PixelAt(r, 60, 41) == kPixRed);
        const int ring = Count(r, kPixRed);
        CHECK(ring > 200 && ring < 280);                                    // 2 * pi * 19 * 2 = 239
        const Bitmap coarse = Render(sim, [&] { dl.AddCircleFilled({60, 60}, 20.0f, kRed, 4); });   // a square-ish diamond with 4 segments
        CHECK(Count(coarse, kPixRed) < area - 100);
    }
    {   // triangles and quads
        const Bitmap t = Render(sim, [&] { dl.AddTriangleFilled({10, 10}, {70, 10}, {10, 70}, kGreen); });
        CHECK(PixelAt(t, 15, 15) == kPixGreen && PixelAt(t, 60, 60) == kBlack && PixelAt(t, 12, 60) == kPixGreen);
        CHECK(std::abs(Count(t, kPixGreen) - 1800) < 80);                    // half of 60 x 60
        const Bitmap q = Render(sim, [&] { dl.AddQuadFilled({10, 10}, {50, 20}, {40, 60}, {5, 50}, kGreen); });
        CHECK(PixelAt(q, 30, 35) == kPixGreen && PixelAt(q, 60, 60) == kBlack && PixelAt(q, 2, 2) == kBlack);
        const Bitmap o = Render(sim, [&] { dl.AddTriangle({10, 10}, {70, 10}, {10, 70}, kGreen, 2.0f); });
        CHECK(PixelAt(o, 30, 10) == kPixGreen && PixelAt(o, 10, 40) == kPixGreen && PixelAt(o, 20, 20) == kBlack && PixelAt(o, 60, 60) == kBlack);   // centred on the path
        const Bitmap oq = Render(sim, [&] { dl.AddQuad({10, 10}, {50, 10}, {50, 50}, {10, 50}, kGreen, 2.0f); });
        CHECK(PixelAt(oq, 30, 10) == kPixGreen && PixelAt(oq, 30, 30) == kBlack && PixelAt(oq, 49, 30) == kPixGreen && PixelAt(oq, 50, 30) == kPixGreen && PixelAt(oq, 51, 30) == kBlack);
    }
    {   // a closed translucent stroke: mitred joins mean no pixel is blended twice, even at the corners
        const UI::Color32 half = UI::RGBA(255, 0, 0, 128);
        const Vec2 sq[4] = {{20, 20}, {80, 20}, {80, 80}, {20, 80}};
        const Bitmap b = Render(sim, [&] { dl.AddPolyline(sq, 4, half, true, 6.0f); });
        const unsigned want = PixelAt(b, 50, 18);
        CHECK(want != kBlack && (want >> 24) > 100 && (want >> 24) < 140);
        int other = 0, same = 0;
        for (int y = 0; y < b.h; ++y) for (int x = 0; x < b.w; ++x) { const unsigned p = PixelAt(b, x, y); if (p == kBlack) continue; if (p == want) ++same; else ++other; }
        CHECK(other == 0 && same > 4 * 60 * 6 - 60);                       // all stroke pixels equal; about 4 sides * 60 * 6
        CHECK(PixelAt(b, 17, 17) == want);                                  // the mitred outer corner is filled
        CHECK(PixelAt(b, 50, 50) == kBlack);
        // an open polyline does not close
        const Bitmap o = Render(sim, [&] { dl.AddPolyline(sq, 4, kRed, false, 2.0f); });
        CHECK(PixelAt(o, 50, 20) == kPixRed && PixelAt(o, 80, 50) == kPixRed && PixelAt(o, 50, 80) == kPixRed && PixelAt(o, 20, 50) == kBlack);
        // duplicates and a single point are survivable
        const Vec2 dup[4] = {{10, 10}, {10, 10}, {30, 10}, {30, 10}};
        const Bitmap dd = Render(sim, [&] { dl.AddPolyline(dup, 4, kRed, false, 2.0f); });
        CHECK(PixelAt(dd, 20, 10) == kPixRed || PixelAt(dd, 20, 9) == kPixRed);
        const Vec2 one[2] = {{40, 40}, {40, 40}};
        const Bitmap oo = Render(sim, [&] { dl.AddPolyline(one, 2, kRed, false, 4.0f); });
        CHECK(PixelAt(oo, 40, 40) == kPixRed);
    }
    {   // a convex polygon (a hexagon) and the refusals
        Vec2 hex[6];
        for (int i = 0; i < 6; ++i) hex[i] = {60 + 30 * std::cos(i * 1.0471976f), 60 + 30 * std::sin(i * 1.0471976f)};
        const Bitmap b = Render(sim, [&] { dl.AddConvexPolyFilled(hex, 6, kGreen); });
        CHECK(PixelAt(b, 60, 60) == kPixGreen && PixelAt(b, 85, 60) == kPixGreen && PixelAt(b, 5, 5) == kBlack);
        const int n = Count(b, kPixGreen);
        CHECK(n > 2300 && n < 2400);                                        // 3 * sqrt(3) / 2 * 900 = 2338
        const Bitmap none = Render(sim, [&] { dl.AddConvexPolyFilled(hex, 2, kGreen); dl.AddConvexPolyFilled(nullptr, 6, kGreen); dl.AddConvexPolyFilled(hex, 5000, kGreen); });
        CHECK(Lit(none) == 0);
    }
    {   // a gradient changes monotonically from left to right
        const Bitmap b = Render(sim, [&] { dl.AddRectFilledMultiColor({10, 10}, {110, 30}, UI::RGBA(0, 0, 0), UI::RGBA(255, 255, 255), UI::RGBA(255, 255, 255), UI::RGBA(0, 0, 0)); });
        const unsigned a = PixelAt(b, 12, 20) >> 24, m = PixelAt(b, 60, 20) >> 24, c = PixelAt(b, 107, 20) >> 24;
        CHECK(a < 20 && c > 235 && m > 100 && m < 160 && a < m && m < c);
        CHECK(PixelAt(b, 60, 12) == PixelAt(b, 60, 28));                    // no vertical change
    }
    {   // a cubic Bezier passes through its ends and its curve point
        const Vec2 p1{10, 80}, p2{10, 10}, p3{110, 10}, p4{110, 80};
        const Bitmap b = Render(sim, [&] { dl.AddBezierCubic(p1, p2, p3, p4, kRed, 2.0f); });
        const float mx = 0.125f * p1.x + 0.375f * p2.x + 0.375f * p3.x + 0.125f * p4.x, my = 0.125f * p1.y + 0.375f * p2.y + 0.375f * p3.y + 0.125f * p4.y;
        CHECK(CountNot(b, (int)mx - 2, (int)my - 2, (int)mx + 3, (int)my + 3, kBlack) > 0);
        CHECK(CountNot(b, 8, 76, 14, 82, kBlack) > 0 && CountNot(b, 106, 76, 112, 82, kBlack) > 0);
        CHECK(PixelAt(b, 60, 70) == kBlack);                                // well below the curve
    }
}

void PathTests() {
    Sim sim(160, 120);
    const UI::DrawList& dl = UI::GetWindowDrawList();
    const Bitmap b = Render(sim, [&] {
        dl.PathClear();
        dl.PathLineTo({10, 10}); dl.PathLineTo({60, 10}); dl.PathLineTo({60, 40});
        dl.PathStroke(kRed, false, 2.0f);                                   // consumes the path
        dl.PathFillConvex(kGreen);                                          // nothing left to fill
        dl.PathArcTo({100, 60}, 20.0f, 0.0f, 3.14159265f, 12);              // a half circle, the lower half on screen
        dl.PathFillConvex(kBlue);
    });
    CHECK(PixelAt(b, 35, 10) == kPixRed && PixelAt(b, 60, 25) == kPixRed && PixelAt(b, 35, 25) == kBlack);
    CHECK(Count(b, kPixGreen) == 0);
    CHECK(PixelAt(b, 100, 70) == kPixBlue && PixelAt(b, 100, 50) == kBlack);   // the half disc is below its diameter
    const int half = Count(b, kPixBlue);
    CHECK(half > 580 && half < 650);                                        // pi * 400 / 2 = 628
    // paths with fewer than two points draw nothing and are cleared
    const Bitmap n = Render(sim, [&] { dl.PathLineTo({10, 10}); dl.PathStroke(kRed); dl.PathStroke(kRed); dl.PathLineTo({20, 20}); dl.PathLineTo({40, 20}); dl.PathStroke(kRed, false, 2.0f); });
    CHECK(PixelAt(n, 30, 20) == kPixRed && PixelAt(n, 10, 10) == kBlack);
    // an overlong path stops at its capacity without breaking
    const Bitmap big = Render(sim, [&] { for (int i = 0; i < 1000; ++i) dl.PathLineTo({(float)(i % 150), (float)(i % 100)}); dl.PathStroke(kRed); });
    CHECK(Lit(big) > 0);
}

void TextAndClipTests() {
    Sim sim(200, 80);
    const UI::DrawList& dl = UI::GetWindowDrawList();
    // AddText draws the same pixels as Label at the same place
    const Bitmap a = Render(sim, [&] { dl.AddText({10, 10}, UI::GetColorU32(UI::Col_Text), "Okay"); });
    Reset();
    sim.Frame([&] { UI::Label(10, 10, "Okay"); });
    const Bitmap l = sim.Draw(0, 0, 0);
    CHECK(Lit(a) > 100 && a.rgba == l.rgba);
    const Bitmap big = Render(sim, [&] { dl.AddText({10, 10}, kRed, "Okay", 4.0f); });
    CHECK(Lit(big) > Lit(a) * 3);
    CHECK(Lit(Render(sim, [&] { dl.AddText({10, 10}, kRed, nullptr); dl.AddText({10, 10}, kRed, ""); dl.AddText({NAN, 10}, kRed, "x"); })) == 0);
    // clip rectangles
    const Bitmap c = Render(sim, [&] {
        dl.PushClipRect({20, 20}, {40, 40});
        dl.AddRectFilled({0, 0}, {100, 100}, kRed);
        dl.PopClipRect();
        dl.AddRectFilled({100, 0}, {110, 10}, kGreen);                      // after the pop: unclipped
    });
    CHECK(Count(c, kPixRed) == 400 && PixelAt(c, 20, 20) == kPixRed && PixelAt(c, 40, 20) == kBlack && PixelAt(c, 19, 30) == kBlack);
    CHECK(Count(c, kPixGreen) == 100);
}

void ForegroundTests() {
    Sim sim(200, 120);
    Reset();
    UI::Rect r;
    auto ui = [&] {
        if (UI::Begin("W", 10, 10, 180, 100)) { UI::Button("Cover"); r = UI::GetItemRect(); }
        UI::End();
        const Vec2 c{r.x + 4, r.y + 4};
        UI::GetForegroundDrawList().AddRectFilled(c, {c.x + 10, c.y + 10}, kRed);        // submitted after the window, on top
        UI::GetWindowDrawList().AddRectFilled({c.x + 30, c.y}, {c.x + 40, c.y + 10}, kGreen);
    };
    sim.Frames(2, ui);
    const Bitmap b = sim.Draw(0, 0, 0);
    CHECK(PixelAt(b, (int)r.x + 6, (int)r.y + 6) == kPixRed);
    CHECK(PixelAt(b, (int)r.x + 36, (int)r.y + 6) == kPixGreen);
    // the foreground list is not clipped by a clip rectangle the window list obeys
    Reset();
    const Bitmap f = Render(sim, [&] {
        UI::PushClipRect(0, 0, 20, 20);
        UI::GetWindowDrawList().AddRectFilled({0, 0}, {60, 10}, kGreen);
        UI::GetForegroundDrawList().AddRectFilled({0, 20}, {60, 30}, kRed);
        UI::PopClipRect();
    });
    CHECK(Count(f, kPixGreen) == 200 && Count(f, kPixRed) == 600);
}

void HostileInputTests() {
    Sim sim(100, 100);
    const UI::DrawList& dl = UI::GetWindowDrawList();
    const float nan = NAN, inf = INFINITY;
    Reset();
    sim.Frame([] {});
    const int before = UI::GetDrawData().vertexCount;
    Reset();
    sim.Frame([&] {
        dl.AddLine({nan, 0}, {1, 1}, kRed); dl.AddLine({0, 0}, {inf, 1}, kRed); dl.AddLine({0, 0}, {10, 10}, kRed, nan); dl.AddLine({0, 0}, {10, 10}, kRed, -3.0f);
        dl.AddRectFilled({0, 0}, {nan, 5}, kRed); dl.AddRectFilled({5, 5}, {0, 0}, kRed); dl.AddRect({0, 0}, {1e30f, 5}, kRed);
        dl.AddCircle({0, 0}, inf, kRed); dl.AddCircleFilled({0, 0}, -5.0f, kRed); dl.AddCircleFilled({nan, 0}, 5.0f, kRed);
        dl.AddTriangleFilled({0, 0}, {nan, 1}, {2, 2}, kRed); dl.AddQuad({0, 0}, {1, 1}, {inf, 2}, {3, 3}, kRed);
        dl.AddBezierCubic({0, 0}, {1, 1}, {nan, 2}, {3, 3}, kRed); dl.AddText({0, 0}, kRed, "x", nan);
        dl.PathArcTo({0, 0}, nan, 0, 1); dl.PathArcTo({0, 0}, 5.0f, 0, inf); dl.PathStroke(kRed);
        dl.AddRectFilledMultiColor({0, 0}, {nan, 3}, kRed, kRed, kRed, kRed); dl.AddImage(nullptr, {0, 0}, {5, 5});
        const Vec2 bad[3] = {{0, 0}, {5, 5}, {nan, 1}};
        dl.AddPolyline(bad, 3, kRed, true); dl.AddConvexPolyFilled(bad, 3, kRed); dl.AddPolyline(nullptr, 3, kRed, false); dl.AddPolyline(bad, -3, kRed, false);
    });
    CHECK(UI::GetDrawData().vertexCount == before);                         // none of it drew anything
    // radius and segment counts are bounded
    Reset();
    sim.Frame([&] { dl.AddCircleFilled({50, 50}, 1e6f, kRed, 1000000); dl.AddCircle({50, 50}, 1e6f, kRed, -5, 3.0f); dl.PathArcTo({50, 50}, 30.0f, 0, 6.0f, 100000); dl.PathStroke(kRed); });
    CHECK(UI::GetDrawData().vertexCount < 262144);
}

void LargeDrawListTests() {
    const UI::DrawList& dl = UI::GetWindowDrawList();
    // 12,500 separate 2x2 squares = 50,000 vertices: far past the old 20,000 limit, all of it drawn
    Sim sim(600, 190);
    Reset();
    sim.Frame([&] {
        for (int i = 0; i < 12500; ++i) { const float x = (float)(i % 200) * 3.0f, y = (float)(i / 200) * 3.0f; dl.AddRectFilled({x, y}, {x + 2, y + 2}, kRed); }
    });
    UI::DrawData d = UI::GetDrawData();
    CHECK(d.vertexCount == 50000 && d.indexCount == 75000 && d.dropped == 0 && UI::DroppedGeometry() == 0);
    const Bitmap b = sim.Draw(0, 0, 0);
    CHECK(Count(b, kPixRed) == 12500 * 4 && Lit(b) == 12500 * 4);
    CHECK(PixelAt(b, 0, 0) == kPixRed && PixelAt(b, 2, 0) == kBlack && PixelAt(b, 298, 187) == kPixRed);   // the first and the last square
    // the same on the foreground list, together with a full main batch: neither layer loses anything
    Reset();
    sim.Frame([&] {
        for (int i = 0; i < 12500; ++i) { const float x = (float)(i % 200) * 3.0f, y = (float)(i / 200) * 3.0f; dl.AddRectFilled({x, y}, {x + 2, y + 2}, kRed); }
        for (int i = 0; i < 12500; ++i) { const float x = (float)(i % 200) * 3.0f + 1.0f, y = (float)(i / 200) * 3.0f + 1.0f; UI::GetForegroundDrawList().AddRectFilled({x, y}, {x + 1, y + 1}, kGreen); }
    });
    d = UI::GetDrawData();
    CHECK(d.vertexCount == 100000 && d.dropped == 0);
    const Bitmap b2 = sim.Draw(0, 0, 0);
    CHECK(Count(b2, kPixGreen) == 12500 && Count(b2, kPixRed) == 12500 * 3);
    // past the real limit nothing crashes, the data stays valid and the loss is reported, not hidden
    Reset();
    sim.Frame([&] { for (int i = 0; i < 70000; ++i) { const float x = (float)(i % 200) * 3.0f, y = (float)(i / 200 % 60) * 3.0f; dl.AddRectFilled({x, y}, {x + 2, y + 2}, kRed); } });
    d = UI::GetDrawData();
    CHECK(d.vertexCount <= 262144 && d.vertexCount >= 262140 && d.dropped == 70000 - d.vertexCount / 4 && UI::DroppedGeometry() == d.dropped);
    sim.Draw(0, 0, 0);
    Reset();
    sim.Frame([] {});
    CHECK(UI::GetDrawData().dropped == 0);                                   // the count is per frame
}

// One window "W" with `body`; a Rig like the behaviour test's.
struct Rig {
    Sim sim;
    std::function<void()> body;
    Rig() { Reset(); }
    void Frame() { sim.Frame([&] { if (UI::Begin("W", 10, 10, 400, 400) && body) body(); UI::End(); }); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void MoveTo(float x, float y) { sim.Move(x, y); Frame(); }
    void ClickC(const UI::Rect& r) { MoveTo(CenterX(r), CenterY(r)); sim.Down(); Frame(); sim.Up(); Frame(); }
};

void MeasureTests() {
    Rig r;
    Vec2 sz{}, pos0{}, pos1{}, pos2{}, pos3{}, avail0{}, avail1{}; UI::Rect rb, rt; float lh = 0, fh = 0, fs = 0, lhs = 0, fhs = 0;
    r.body = [&] {
        sz = UI::CalcTextSize("Go"); lh = UI::GetTextLineHeight(); fh = UI::GetFrameHeight(); fs = UI::GetFontSize();
        lhs = UI::GetTextLineHeightWithSpacing(); fhs = UI::GetFrameHeightWithSpacing();
        pos0 = UI::GetCursorScreenPos(); avail0 = UI::GetContentRegionAvail();
        UI::Button("Go"); rb = UI::GetItemRect();
        pos1 = UI::GetCursorScreenPos(); avail1 = UI::GetContentRegionAvail();
        UI::SameLine();
        pos2 = UI::GetCursorScreenPos();
        UI::Text("after"); rt = UI::GetItemRect();
        pos3 = UI::GetCursorScreenPos();
    };
    r.Frames(2);
    CHECK_NEAR(sz.x, 32, 1e-3); CHECK_NEAR(sz.y, 16, 1e-3);                 // two 16 px glyphs, one 16 px line
    CHECK_NEAR(lh, 16, 1e-3); CHECK_NEAR(fs, 16, 1e-3); CHECK_NEAR(fh, 28, 1e-3);
    CHECK_NEAR(lhs, 16 + 6, 1e-3); CHECK_NEAR(fhs, 28 + 6, 1e-3);
    CHECK_NEAR(fh, rb.h, 1e-3);                                             // the frame height is a button's height
    CHECK_NEAR(pos0.x, rb.x, 1e-3); CHECK_NEAR(pos0.y, rb.y, 1e-3);         // the cursor is where the next item lands
    CHECK_NEAR(pos1.x, rb.x, 1e-3); CHECK_NEAR(pos1.y, rb.y + rb.h + 6, 1e-3);
    CHECK_NEAR(pos2.x, rb.x + rb.w + 8, 1e-3); CHECK_NEAR(pos2.y, rb.y, 1e-3);   // after SameLine: right of the button
    CHECK_NEAR(rt.x, pos2.x, 1e-3); CHECK_NEAR(rt.y, pos2.y, 1e-3);
    CHECK_NEAR(pos3.x, rb.x, 1e-3);
    CHECK_NEAR(avail0.x, 400 - 20, 1e-3);                                   // window width minus the padding on both sides
    CHECK_NEAR(avail0.y, 410 - 10 - pos0.y, 1e-3);                          // down to the window bottom minus padding
    CHECK_NEAR(avail1.y, avail0.y - (rb.h + 6), 1e-3);
    CHECK_NEAR(UI::CalcTextSize("").x, 0, 1e-3); CHECK_NEAR(UI::CalcTextSize(nullptr).x, 0, 1e-3);
    CHECK_NEAR(UI::CalcTextSize("Go").x, UI::CalcTextWidth("Go"), 1e-3);
    // outside a window everything is zero
    Reset();
    CHECK(UI::GetCursorScreenPos().x == 0 && UI::GetContentRegionAvail().x == 0 && UI::GetCursorPos().y == 0);
}

void CursorTests() {
    Rig r;
    UI::Rect a, b, c; Vec2 rel0{}, rel1{};
    r.body = [&] {
        rel0 = UI::GetCursorPos();
        UI::Text("one"); a = UI::GetItemRect();
        UI::SetCursorScreenPos({200, 150});
        UI::Text("placed"); b = UI::GetItemRect();
        UI::Text("below"); c = UI::GetItemRect();
        UI::SetCursorPos({30, 100});
        rel1 = UI::GetCursorPos();
        UI::Text("rel");
    };
    r.Frames(2);
    CHECK_NEAR(rel0.x, 0, 1e-3); CHECK_NEAR(rel0.y, 0, 1e-3);               // relative to the content origin
    CHECK_NEAR(b.x, 200, 1e-3); CHECK_NEAR(b.y, 150, 1e-3);
    CHECK_NEAR(c.x, 10 + 10, 1e-3); CHECK_NEAR(c.y, b.y + b.h + 6, 1e-3);   // the layout resumes under the placed item, at the left edge
    CHECK_NEAR(rel1.x, 30, 1e-3); CHECK_NEAR(rel1.y, 100, 1e-3);
    // SetCursorPosX / Y keep the other axis; a pending override is visible to Get
    Vec2 g{}; UI::Rect d, e;
    r.body = [&] {
        UI::Text("x"); UI::SetCursorPosX(50); g = UI::GetCursorScreenPos(); UI::Text("moved"); d = UI::GetItemRect();
        UI::SetCursorPosY(120); UI::Text("down"); e = UI::GetItemRect();
    };
    r.Frames(2);
    CHECK_NEAR(g.x, 10 + 10 + 50, 1e-3); CHECK_NEAR(d.x, g.x, 1e-3); CHECK_NEAR(d.y, g.y, 1e-3);
    CHECK_NEAR(e.y, 10 + 28 + 10 + 120, 1e-3);
    // a SetCursorScreenPos left over at the end of a window does not move the next window's first item
    Reset();
    Sim sim(300, 200); UI::Rect f1, f2;
    sim.Frames(2, [&] {
        if (UI::Begin("A", 0, 0, 100, 100)) { UI::SetCursorScreenPos({90, 90}); }
        UI::End();
        if (UI::Begin("B", 150, 0, 100, 100)) { UI::Text("first"); f1 = UI::GetItemRect(); }
        UI::End();
    });
    CHECK_NEAR(f1.x, 160, 1e-3); CHECK_NEAR(f1.y, 0 + 28 + 10, 1e-3);
    (void)f2;
}

void ChildAvailTests() {
    Rig r; Vec2 inner{}, inner2{}, outer{};
    r.body = [&] {
        if (UI::BeginChild("c", 200, 100)) { inner = UI::GetContentRegionAvail(); UI::Text("t"); inner2 = UI::GetContentRegionAvail(); UI::EndChild(); }
        outer = UI::GetContentRegionAvail();
    };
    r.Frames(2);
    CHECK_NEAR(inner.x, 200 - 12 - 10, 1e-3);                               // child width minus 6 px padding each side and the scroll bar
    CHECK(inner.y > 80 && inner.y <= 88 + 1e-3 && inner2.y < inner.y);      // 100 minus the padding, less after a row
    CHECK_NEAR(outer.x, 380, 1e-3);                                         // back to the window
}

void InvisibleButtonTests() {
    Rig r; int clicks = 0; UI::Rect rc, rt; bool act = false, actd = false, deact = false, clicked = false, hov = false;
    r.body = [&] {
        if (UI::InvisibleButton("hit", 120, 40)) ++clicks;
        rc = UI::GetItemRect();
        act = UI::IsItemActive(); actd = UI::IsItemActivated(); deact = UI::IsItemDeactivated(); clicked = UI::IsItemClicked(); hov = UI::IsItemHovered();
        UI::Text("after"); rt = UI::GetItemRect();
    };
    r.Frames(2);
    CHECK_NEAR(rc.w, 120, 1e-3); CHECK_NEAR(rc.h, 40, 1e-3);
    CHECK_NEAR(rt.y, rc.y + 40 + 6, 1e-3);                                  // it takes layout space
    CHECK(!act && !hov && clicks == 0);
    r.MoveTo(CenterX(rc), CenterY(rc));
    CHECK(hov && !act);
    r.sim.Down(); r.Frame();
    CHECK(act && actd && clicked && clicks == 0);                           // the press frame: active, activated, clicked
    r.Frame();
    CHECK(act && !actd && !clicked && !deact);                              // held: active only
    r.sim.Up(); r.Frame();
    CHECK(clicks == 1 && !act && !actd && deact);                           // released on it: one click, deactivated
    r.Frame();
    CHECK(!deact && clicks == 1);
    // slid off before releasing: no click, but still deactivated
    r.sim.Down(); r.Frame(); r.MoveTo(300, 300); r.sim.Up(); r.Frame();
    CHECK(clicks == 1 && deact);
    // it draws nothing: the pixels in its rectangle are the window's background
    r.MoveTo(300, 300);
    const Bitmap b = r.sim.Draw(0, 0, 0);
    const unsigned bg = PixelAt(b, (int)rc.x + 60, (int)rc.y + 20);
    CHECK(CountNot(b, (int)rc.x, (int)rc.y, (int)(rc.x + rc.w), (int)(rc.y + rc.h), bg) == 0);
    // right click: IsItemClicked(1) only
    bool rclick = false;
    r.body = [&] { UI::InvisibleButton("hit", 120, 40); rclick = UI::IsItemClicked(1); rc = UI::GetItemRect(); };
    r.Frames(2); r.MoveTo(CenterX(rc), CenterY(rc)); r.sim.rdown = true; r.Frame();
    CHECK(rclick); r.sim.rdown = false; r.Frame(); CHECK(!rclick);
    // size 0 means full width and one row; hostile sizes do not crash and are ignored
    r.body = [&] { UI::InvisibleButton("full", 0, 0); rc = UI::GetItemRect(); CHECK(!UI::InvisibleButton("bad", NAN, 5)); CHECK(!UI::InvisibleButton("bad2", 5, INFINITY)); };
    r.Frames(2);
    CHECK_NEAR(rc.w, 380, 1e-3); CHECK_NEAR(rc.h, 28, 1e-3);
}

void ItemStateTests() {
    Rig r; bool cb = false; float sl = 0.5f; UI::Rect rcb, rsl; bool act = false, actd = false, deact = false; bool txt = true;
    r.body = [&] {
        if (txt) { UI::Text("plain"); act = UI::IsItemActive(); }
        else { UI::Checkbox("check", &cb); rcb = UI::GetItemRect(); act = UI::IsItemActive(); actd = UI::IsItemActivated(); deact = UI::IsItemDeactivated(); }
        if (!txt) { UI::SliderFloat("slide", &sl, 0, 1); rsl = UI::GetItemRect(); }
    };
    r.Frames(2);
    CHECK(!act && !UI::IsItemActive());                                     // text registers no id
    txt = false; r.Frames(2);
    r.MoveTo(CenterX(rcb), CenterY(rcb)); r.sim.Down(); r.Frame();
    CHECK(act && actd);
    r.sim.Up(); r.Frame();
    CHECK(!act && deact && cb);
    // a slider drag: active while held, whatever the slider itself reports
    bool sact = false;
    r.body = [&] { UI::SliderFloat("slide", &sl, 0, 1); rsl = UI::GetItemRect(); sact = UI::IsItemActive(); };
    r.Frames(2);
    r.MoveTo(rsl.x + 40, CenterY(rsl)); r.sim.Down(); r.Frame(); r.MoveTo(rsl.x + 90, CenterY(rsl));
    CHECK(sact);
    r.sim.Up(); r.Frame();
    CHECK(!sact);
    // after leaving the window the queries answer false
    Reset();
    CHECK(!UI::IsItemActive() && !UI::IsItemActivated() && !UI::IsItemDeactivated() && !UI::IsItemClicked());
}

void MouseQueryTests() {
    Sim sim(100, 100);
    bool down = false, clicked = false, released = false, rdown = false, rclicked = false, rrel = false; Vec2 pos{}, delta{};
    auto ui = [&] {
        down = UI::IsMouseDown(); clicked = UI::IsMouseClicked(); released = UI::IsMouseReleased();
        rdown = UI::IsMouseDown(1); rclicked = UI::IsMouseClicked(1); rrel = UI::IsMouseReleased(1);
        pos = UI::GetMousePos(); delta = UI::GetMouseDelta();
    };
    Reset();
    sim.Move(10, 20); sim.Frame(ui); sim.Move(15, 30); sim.Frame(ui);
    CHECK(pos.x == 15 && pos.y == 30 && delta.x == 5 && delta.y == 10 && !down && !clicked);
    sim.Down(); sim.Frame(ui); CHECK(down && clicked && !released);
    sim.Frame(ui);              CHECK(down && !clicked && !released);
    sim.Up(); sim.Frame(ui);    CHECK(!down && !clicked && released);
    sim.Frame(ui);              CHECK(!released);
    sim.rdown = true; sim.Frame(ui); CHECK(rdown && rclicked && !rrel && !down);
    sim.rdown = false; sim.Frame(ui); CHECK(!rdown && !rclicked && rrel);
    sim.blocked = true; sim.Down(); sim.Frame(ui); CHECK(!down && !clicked);    // another layer owns the mouse
    sim.blocked = false; sim.Up(); sim.Frame(ui);
    sim.Move(50, 50); sim.Frame([&] { CHECK(UI::IsMouseHoveringRect({40, 40}, {60, 60})); CHECK(!UI::IsMouseHoveringRect({51, 40}, {60, 60})); });
}

void DisabledTests() {
    {   // a disabled button cannot be clicked, an enabled one after it can
        Rig r; int a = 0, b = 0; UI::Rect ra, rb; bool dis = true, inside = false, after = true;
        r.body = [&] {
            UI::BeginDisabled(dis); inside = UI::IsDisabled();
            if (UI::Button("Off")) ++a;
            ra = UI::GetItemRect();
            UI::EndDisabled(); after = UI::IsDisabled();
            if (UI::Button("On")) ++b;
            rb = UI::GetItemRect();
        };
        r.Frames(2);
        CHECK(inside && !after);
        r.ClickC(ra); CHECK(a == 0);
        r.ClickC(rb); CHECK(b == 1);
        dis = false; r.Frames(2);
        r.ClickC(ra); CHECK(a == 1);                                        // BeginDisabled(false) does nothing
        r.body = [&] { UI::BeginDisabled(true); UI::BeginDisabled(false); UI::EndDisabled(); inside = UI::IsDisabled(); if (UI::Button("Off")) ++a; ra = UI::GetItemRect(); UI::EndDisabled(); after = UI::IsDisabled(); };
        r.Frames(2);
        CHECK(inside && !after);                                            // still disabled after the inner pair
        r.ClickC(ra); CHECK(a == 1);
    }
    {   // checkbox, slider, drag field, radio, combo, text field, tree node
        Rig r; bool cb = false; float sl = 0.5f, dr = 1.0f; int radio = 0, combo = 0; char text[16] = "abc"; bool tree = false;
        UI::Rect rcb, rsl, rdr, rra, rco, rtx, rtr;
        static const char* const items[] = {"A", "B"};
        bool dis = true;
        r.body = [&] {
            UI::BeginDisabled(dis);
            UI::Checkbox("cb", &cb); rcb = UI::GetItemRect();
            UI::SliderFloat("sl", &sl, 0, 1); rsl = UI::GetItemRect();
            UI::DragFloat("dr", &dr, 0.1f); rdr = UI::GetItemRect();
            UI::RadioButton("ra", &radio, 1); rra = UI::GetItemRect();
            UI::Combo("co", items, 2, &combo); rco = UI::GetItemRect();
            UI::InputText("tx", text, sizeof text); rtx = UI::GetItemRect();
            if (UI::TreeNode("tr")) { tree = true; UI::TreePop(); } else tree = false; rtr = UI::GetItemRect();
            UI::EndDisabled();
        };
        r.Frames(2);
        r.ClickC(rcb); CHECK(!cb);
        r.MoveTo(rsl.x + 20, CenterY(rsl)); r.sim.Down(); r.Frame(); r.MoveTo(rsl.x + 60, CenterY(rsl)); r.sim.Up(); r.Frame(); CHECK(sl == 0.5f);
        r.MoveTo(rdr.x + 20, CenterY(rdr)); r.sim.Down(); r.Frame(); r.MoveTo(rdr.x + 80, CenterY(rdr)); r.sim.Up(); r.Frame(); CHECK(dr == 1.0f);
        r.ClickC(rra); CHECK(radio == 0);
        r.ClickC(rco); r.Frames(2); r.MoveTo(rco.x + 20, rco.y + rco.h * 1.5f); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); CHECK(combo == 0);
        r.ClickC(rtx); r.sim.Type("zz"); r.Frame(); CHECK(!std::strcmp(text, "abc") && !UI::WantsTextInput());
        r.ClickC(rtr); CHECK(!tree);
        dis = false; r.Frames(2);
        r.ClickC(rcb); CHECK(cb);                                           // the same widgets work when enabled
        r.ClickC(rra); CHECK(radio == 1);
        r.ClickC(rtx); r.sim.Type("zz"); r.Frame(); CHECK(!std::strcmp(text, "abczz"));
        r.ClickC(rtr); CHECK(tree);
        // a field that has the keyboard stops taking text the frame it becomes disabled
        dis = true; r.sim.Type("QQ"); r.Frame(); CHECK(!std::strcmp(text, "abczz") && !UI::WantsTextInput());
    }
    {   // dimmed: every channel of the face is halfway between the enabled face and the panel behind it
        Rig r; UI::Rect rb; bool dis = false;
        r.body = [&] { UI::BeginDisabled(dis); UI::Button("Face"); rb = UI::GetItemRect(); UI::EndDisabled(); };
        r.Frames(2);
        const Bitmap on = r.sim.Draw(0, 0, 0);
        dis = true; r.Frames(2);
        const Bitmap off = r.sim.Draw(0, 0, 0);
        const int px = (int)rb.x + 3, py = (int)rb.y + 8;                  // inside the face, left of the label
        const unsigned pOn = PixelAt(on, px, py), pOff = PixelAt(off, px, py);
        CHECK(pOn != pOff);
        const unsigned pan = ThemeRgba(UI::Style().panel);
        for (int ch = 24; ch >= 8; ch -= 8) {
            const int fOn = (pOn >> ch) & 255, fPan = (pan >> ch) & 255, fOff = (pOff >> ch) & 255;
            CHECK(std::abs(fOff - (fOn + fPan) / 2) <= 1);
        }
        CHECK((pOff & 255) == 255);                                         // still opaque: no layer blends through another
        // the draw list is not dimmed
        Sim sim(40, 40);
        Reset();
        sim.Frame([&] { UI::BeginDisabled(); UI::GetWindowDrawList().AddRectFilled({0, 0}, {10, 10}, kRed); UI::GetForegroundDrawList().AddRectFilled({20, 0}, {30, 10}, kRed); UI::EndDisabled(); });
        const Bitmap d = sim.Draw(0, 0, 0);
        CHECK(PixelAt(d, 5, 5) == kPixRed && PixelAt(d, 25, 5) == kPixRed);
    }
    {   // unbalanced calls are harmless: an extra End, and a Begin that never ends does not leak into the next frame
        Rig r; int n = 0; UI::Rect rb;
        r.body = [&] { UI::EndDisabled(); UI::EndDisabled(); if (UI::Button("x")) ++n; rb = UI::GetItemRect(); };
        r.Frames(2); r.ClickC(rb); CHECK(n == 1);
        r.body = [&] { UI::BeginDisabled(); UI::BeginDisabled(); };
        r.Frame();
        r.body = [&] { CHECK(!UI::IsDisabled()); if (UI::Button("x")) ++n; rb = UI::GetItemRect(); };
        r.Frames(2); r.ClickC(rb); CHECK(n == 2);
        for (int i = 0; i < 40; ++i) UI::BeginDisabled();                    // more than the stack holds: capped, no crash
        for (int i = 0; i < 40; ++i) UI::EndDisabled();
    }
}

void LabTests() {
    {   // the lab page, classic look: golden image after a scripted interaction
        Reset();
        Sim sim(760, 600);
        okayuidemo::LabState st;
        okayuidemo::RunLabScript(sim, st);
        CHECK(st.canvasClicks == 3 && st.nMarkers == 2 && st.nStrokes == 1 && st.dragEnded == 1);   // three presses: two clicks and a drag
        CHECK(st.locked && st.lockedClicks == 0 && st.openClicks == 1);
        CHECK(st.hoverOnCanvas);
        std::string why;
        const bool ok = MatchesGolden("drawlist_lab", sim.Draw(12, 14, 20), &why);
        if (!ok) std::cerr << "  golden drawlist_lab: " << why << "\n";
        CHECK(ok);
    }
    {   // the same page in the launcher look
        Reset();
        UI::UseLauncherLook(true); UI::Style().caretBlink = false;
        Sim sim(760, 600);
        okayuidemo::LabState st;
        okayuidemo::RunLabScript(sim, st);
        CHECK(st.canvasClicks == 3 && st.nMarkers == 2 && st.nStrokes == 1 && st.locked && st.lockedClicks == 0);
        std::string why;
        const bool ok = MatchesGolden("drawlist_lab_launcher", sim.Draw(12, 14, 20), &why);
        if (!ok) std::cerr << "  golden drawlist_lab_launcher: " << why << "\n";
        CHECK(ok);
        UI::UseLauncherLook(false);
    }
    {   // deterministic
        Bitmap a, b;
        for (int pass = 0; pass < 2; ++pass) {
            Reset();
            Sim sim(760, 600);
            okayuidemo::LabState st;
            okayuidemo::RunLabScript(sim, st);
            (pass == 0 ? a : b) = sim.Draw(12, 14, 20);
        }
        CHECK(a.rgba == b.rgba);
    }
}

// Theme::antiAliased: shapes get a one-pixel edge ramp (the fringe, or a baked disc or ring from the launcher atlas). Drawn in
// red on black, a pixel's red value is how much of it the shape covers, so the sum of red / 255 is the shape's area.
Bitmap RenderAA(Sim& sim, bool aa, const std::function<void()>& f, int* indices = nullptr) {
    Reset();
    UI::UseLauncherLook(true);
    UI::Style().antiAliased = aa;
    sim.Frame(f);
    if (indices) *indices = UI::GetDrawData().indexCount;
    const Bitmap b = sim.Draw(0, 0, 0);
    UI::UseLauncherLook(false);
    return b;
}
double Coverage(const Bitmap& b) {
    double s = 0.0;
    for (int y = 0; y < b.h; ++y) for (int x = 0; x < b.w; ++x) s += (double)(PixelAt(b, x, y) >> 24) / 255.0;
    return s;
}
int Partial(const Bitmap& b) {   // pixels neither black nor full red
    int n = 0;
    for (int y = 0; y < b.h; ++y) for (int x = 0; x < b.w; ++x) { const unsigned r = PixelAt(b, x, y) >> 24; if (r > 0 && r < 255) ++n; }
    return n;
}

void AntiAliasTests() {
    Sim sim(160, 120);
    const UI::DrawList& dl = UI::GetWindowDrawList();
    const double kPi = 3.14159265358979;
    {   // a plain rectangle on whole pixels is the same with or without it
        const auto f = [&] { dl.AddRectFilled({10, 10}, {50, 40}, kRed); };
        CHECK(RenderAA(sim, true, f).rgba == RenderAA(sim, false, f).rgba);
        const auto g = [&] { dl.AddLine({10, 50}, {50, 50}, kRed, 2.0f); dl.AddLine({80.5f, 10}, {80.5f, 60}, kRed, 1.0f); };
        CHECK(RenderAA(sim, true, g).rgba == RenderAA(sim, false, g).rgba);   // lines along pixel edges stay crisp bars
        // a one-pixel line between two pixel columns covers both by half
        const Bitmap h = RenderAA(sim, true, [&] { dl.AddLine({70, 10}, {70, 60}, kRed, 1.0f); });
        CHECK(std::fabs(Coverage(h) - 50.0) < 1.0 && (PixelAt(h, 69, 30) >> 24) > 115 && (PixelAt(h, 69, 30) >> 24) < 140 && (PixelAt(h, 70, 30) >> 24) > 115);
    }
    {   // a rounded rectangle: smooth corners, the right area, far fewer indices than a fan and fringe
        int idx = 0;
        const Bitmap a = RenderAA(sim, true, [&] { dl.AddRectFilled({10, 10}, {50, 40}, kRed, 8.0f); }, &idx);
        const Bitmap n = RenderAA(sim, false, [&] { dl.AddRectFilled({10, 10}, {50, 40}, kRed, 8.0f); });
        const double area = 40.0 * 30.0 - (4.0 - kPi) * 64.0;
        CHECK(std::fabs(Coverage(a) - area) < 4.0);
        CHECK(Partial(a) > 16 && Partial(n) == 0);
        CHECK(PixelAt(a, 10, 10) == kBlack && PixelAt(a, 30, 10) == kPixRed && PixelAt(a, 10, 25) == kPixRed && PixelAt(a, 30, 25) == kPixRed);
        CHECK(PixelAt(a, 30, 9) == kBlack && PixelAt(a, 9, 25) == kBlack);   // nothing spills past a side on whole pixels
        CHECK(idx > 0 && idx <= 42);
        // off the pixel grid: the sides fade too, and the area is still right
        const Bitmap f = RenderAA(sim, true, [&] { dl.AddRectFilled({10.5f, 10.25f}, {50.5f, 40.25f}, kRed, 8.0f); }, &idx);
        CHECK(std::fabs(Coverage(f) - area) < 4.0);
        CHECK((PixelAt(f, 30, 10) >> 24) > 150 && (PixelAt(f, 30, 10) >> 24) < 230);   // three quarters of that row
        CHECK((PixelAt(f, 10, 25) >> 24) > 100 && (PixelAt(f, 10, 25) >> 24) < 155);   // half of that column
        CHECK(idx <= 66);
        // a translucent one does not double up where its pieces meet
        const Bitmap t = RenderAA(sim, true, [&] { dl.AddRectFilled({10, 10}, {50, 40}, UI::RGBA(255, 0, 0, 128), 8.0f); });
        int over = 0;
        for (int y = 0; y < t.h; ++y) for (int x = 0; x < t.w; ++x) if ((PixelAt(t, x, y) >> 24) > 130) ++over;
        CHECK(over == 0);
    }
    {   // circles: the disc's area; a hexagon asked for with 6 segments stays a hexagon
        int idx = 0;
        const Bitmap c = RenderAA(sim, true, [&] { dl.AddCircleFilled({60, 60}, 20.0f, kRed, 64); }, &idx);
        CHECK(std::fabs(Coverage(c) - kPi * 400.0) < 6.0 && Partial(c) > 40 && idx == 6);
        CHECK(PixelAt(c, 60, 60) == kPixRed && PixelAt(c, 60, 39) == kBlack && PixelAt(c, 60, 41) == kPixRed);
        const Bitmap h = RenderAA(sim, true, [&] { dl.AddCircleFilled({60, 60}, 20.0f, kRed, 6); });
        CHECK(std::fabs(Coverage(h) - 1.5 * std::sqrt(3.0) * 400.0) < 6.0 && Partial(h) > 20);
        const Bitmap big = RenderAA(sim, true, [&] { dl.AddCircleFilled({80, 60}, 55.0f, kRed, 128); });   // larger than any baked disc
        CHECK(std::fabs(Coverage(big) - kPi * 55.0 * 55.0) < 20.0);
    }
    {   // lines and outlines: the stroke's area, soft edges
        const Bitmap d = RenderAA(sim, true, [&] { dl.AddLine({10, 10}, {60, 60}, kRed, 2.0f); });
        CHECK(std::fabs(Coverage(d) - 2.0 * std::sqrt(2.0) * 50.0) < 4.0 && Partial(d) > 50);
        const Bitmap thin = RenderAA(sim, true, [&] { dl.AddLine({10, 10}, {60, 40}, kRed, 0.5f); });
        CHECK(std::fabs(Coverage(thin) - 0.5 * std::sqrt(50.0 * 50.0 + 30.0 * 30.0)) < 3.0);
        int idx = 0;
        const Bitmap o = RenderAA(sim, true, [&] { dl.AddRect({10, 10}, {50, 40}, kRed, 8.0f, 1.0f); }, &idx);   // a baked ring at each corner
        const double perim = 2.0 * (39.0 + 29.0) - 8.0 * 7.5 + 2.0 * kPi * 7.5;
        CHECK(std::fabs(Coverage(o) - perim) < 4.0 && idx <= 48);
        CHECK(PixelAt(o, 30, 10) == kPixRed && PixelAt(o, 30, 11) == kBlack && PixelAt(o, 30, 9) == kBlack);
        const Bitmap o2 = RenderAA(sim, true, [&] { dl.AddRect({10, 10}, {50, 40}, kRed, 8.0f, 2.0f); });
        const double perim2 = 2.0 * (38.0 + 28.0) - 8.0 * 7.0 + 2.0 * kPi * 7.0;
        CHECK(std::fabs(Coverage(o2) - 2.0 * perim2) < 6.0);
        const Bitmap r = RenderAA(sim, true, [&] { dl.AddCircle({60, 60}, 20.0f, kRed, 64, 2.0f); });
        CHECK(std::fabs(Coverage(r) - kPi * (400.0 - 324.0)) < 6.0);
    }
    {   // convex polygons, one colour or one per point: the area, and an edge that fades
        const Vec2 tri[3] = {{10, 10}, {70, 20}, {30, 70}};
        const double area = 0.5 * std::fabs((70.0 - 10) * (70.0 - 10) - (30.0 - 10) * (20.0 - 10));
        const Bitmap a = RenderAA(sim, true, [&] { dl.AddConvexPolyFilled(tri, 3, kRed); });
        CHECK(std::fabs(Coverage(a) - area) < 4.0 && Partial(a) > 40);
        const UI::Color32 cols[3] = {kRed, kRed, kRed};
        const Bitmap m = RenderAA(sim, true, [&] { dl.AddConvexPolyFilledMultiColor(tri, cols, 3); });
        CHECK(m.rgba == a.rgba);
        const Bitmap t = RenderAA(sim, true, [&] { dl.AddTriangleFilled(tri[0], tri[1], tri[2], kRed); });
        CHECK(t.rgba == a.rgba);
    }
    CHECK(!UI::Style().antiAliased);   // off by default
}

// DrawList::Cached: kept geometry copied to a new place draws the same pixels as drawing it there; the drawing runs once.
struct CacheArgs { const UI::DrawList* dl; Vec2 c; int* runs; };
void CachedShape(void* u) {
    const CacheArgs& a = *(const CacheArgs*)u;
    ++*a.runs;
    a.dl->AddCircleFilled(a.c, 9.0f, kRed, 32);
    a.dl->AddLine({a.c.x - 8, a.c.y + 3}, {a.c.x + 8, a.c.y - 5}, kGreen, 1.6f);
    const Vec2 tri[3] = {{a.c.x - 4, a.c.y}, {a.c.x + 4, a.c.y}, {a.c.x, a.c.y + 7.5f}};
    a.dl->AddConvexPolyFilled(tri, 3, kBlue);
}
void CachedTests() {
    Sim sim(160, 120);
    const UI::DrawList& dl = UI::GetWindowDrawList();
    for (int aa = 0; aa < 2; ++aa) {
        int runs = 0;
        const Vec2 at[3] = {{30.25f, 30.5f}, {70.25f, 45.5f}, {110.25f, 90.5f}};   // the same fraction of a pixel
        const Bitmap kept = RenderAA(sim, aa != 0, [&] {
            for (const Vec2& p : at) { CacheArgs a{&dl, p, &runs}; dl.Cached(42, p, CachedShape, &a); }
        });
        int fresh = 0;
        const Bitmap direct = RenderAA(sim, aa != 0, [&] { for (const Vec2& p : at) { CacheArgs a{&dl, p, &fresh}; CachedShape(&a); } });
        int differ = 0, maxd = 0;
        for (std::size_t i = 0; i < kept.rgba.size(); ++i) { const int d = std::abs((int)kept.rgba[i] - (int)direct.rgba[i]); if (d) ++differ; if (d > maxd) maxd = d; }
        CHECK(runs == 1 && maxd <= 2 && differ < 40);   // the same pixels up to float rounding (a level here and there)
        // another fraction of a pixel is drawn afresh (and kept as its own entry)
        int runs2 = 0;
        RenderAA(sim, aa != 0, [&] {
            const Vec2 p{30.0f, 30.0f}, q{30.75f, 30.0f};
            CacheArgs a{&dl, p, &runs2}; dl.Cached(42, p, CachedShape, &a);
            CacheArgs b{&dl, q, &runs2}; dl.Cached(42, q, CachedShape, &b);
            CacheArgs c{&dl, Vec2{50.0f, 60.0f}, &runs2}; dl.Cached(42, c.c, CachedShape, &c);
        });
        CHECK(runs2 == 2);
    }
    {   // a drawing that changes the clip is not kept; junk is ignored
        int runs = 0;
        struct ClipArgs { const UI::DrawList* dl; int* runs; } a{&dl, &runs};
        auto clipped = [](void* u) {
            const ClipArgs& c = *(const ClipArgs*)u; ++*c.runs;
            c.dl->PushClipRect({0, 0}, {20, 20}); c.dl->AddRectFilled({0, 0}, {40, 40}, kRed); c.dl->PopClipRect();
        };
        RenderAA(sim, false, [&] { dl.Cached(7, {0, 0}, clipped, &a); dl.Cached(7, {0, 0}, clipped, &a); dl.Cached(8, {NAN, 0}, clipped, &a); dl.Cached(9, {0, 0}, nullptr, nullptr); });
        CHECK(runs == 2);
    }
}

// Shapes wholly above or below the clip rectangle are not emitted; partly visible ones are; a Cached drawing made out of view
// still holds everything (its copy in view is whole), and a copy out of view is skipped.
void ClipSkipTests() {
    const UI::DrawList& dl = UI::GetWindowDrawList();
    Sim sim(100, 300);
    auto count = [&](const std::function<void()>& f) {
        Reset();
        sim.Frame([&] { dl.PushClipRect({0, 0}, {100, 50}); f(); dl.PopClipRect(); });
        return UI::GetDrawData().vertexCount;
    };
    const int none = count([] {});
    CHECK(count([&] { dl.AddRectFilled({0, 60}, {10, 70}, kRed); dl.AddRectFilled({0, -30}, {10, -20}, kRed, 3.0f); }) == none);
    CHECK(count([&] { dl.AddCircleFilled({20, 90}, 5.0f, kRed); dl.AddCircle({20, 90}, 5.0f, kRed); dl.AddLine({0, 80}, {50, 90}, kRed, 2.0f); }) == none);
    CHECK(count([&] { dl.AddText({0, 120}, kRed, "below"); dl.AddRect({0, 70}, {20, 90}, kRed, 4.0f, 1.0f); }) == none);
    CHECK(count([&] { dl.AddRectFilled({0, 45}, {10, 55}, kRed); }) > none);   // partly visible
    CHECK(count([&] { dl.AddText({0, 40}, kRed, "edge"); }) > none);
    // without a clip rectangle nothing is skipped
    Reset();
    sim.Frame([&] { dl.AddRectFilled({0, 260}, {10, 270}, kRed); });
    CHECK(UI::GetDrawData().vertexCount > none);
    // Cached: first drawn out of view (kept whole), then copied into view; a copy out of view adds nothing
    struct A { const UI::DrawList* dl; UI::Vec2 o; } a{&dl, {0, 0}};
    auto draw = [](void* u) { const A& m = *(const A*)u; m.dl->AddRectFilled(m.o, {m.o.x + 10, m.o.y + 10}, kRed); m.dl->AddText({m.o.x, m.o.y}, kRed, "ab"); };
    int inView = 0, outView = 0, fresh = 0;
    Reset();
    sim.Frame([&] {
        dl.PushClipRect({0, 0}, {100, 50});
        const int v0 = UI::GetDrawData().vertexCount; (void)v0;
        a.o = {0, 200}; dl.Cached(77, a.o, draw, &a);
        dl.PopClipRect();
    });
    outView = UI::GetDrawData().vertexCount;
    Reset();   // ResetState empties the store: draw fresh in view for the reference count
    sim.Frame([&] { dl.PushClipRect({0, 0}, {100, 50}); a.o = {0, 10}; dl.Cached(78, a.o, draw, &a); dl.PopClipRect(); });
    fresh = UI::GetDrawData().vertexCount;
    CHECK(outView == fresh);   // the recording out of view was not skipped
    sim.Frame([&] {
        dl.PushClipRect({0, 0}, {100, 50});
        a.o = {0, 10}; dl.Cached(78, a.o, draw, &a);    // copy in view
        a.o = {0, 210}; dl.Cached(78, a.o, draw, &a);   // copy out of view: skipped
        dl.PopClipRect();
    });
    inView = UI::GetDrawData().vertexCount;
    CHECK(inView == fresh);
}

// AddImageRounded: a fan whose texture coordinates follow the position; a texture is required; junk is ignored.
void ImageRoundedTests() {
    const UI::DrawList& dl = UI::GetWindowDrawList();
    Sim sim(100, 100);
    SDL_Texture* fake = (SDL_Texture*)(std::uintptr_t)0x1000;   // never sampled: the headless backend skips textured commands
    Reset();
    sim.Frame([] {});
    const int before = UI::GetDrawData().vertexCount, beforeIdx = UI::GetDrawData().indexCount;
    Reset();
    sim.Frame([&] { dl.AddImageRounded(fake, {10, 20}, {50, 60}, 20.0f); });
    UI::DrawData d = UI::GetDrawData();
    CHECK(d.vertexCount - before == 29 && d.indexCount - beforeIdx == 84);
    bool uvOk = true, onCircle = true, found = false;
    const UI::DrawVertex* v = (const UI::DrawVertex*)d.vertices;
    for (int i = before; i < d.vertexCount; ++i) {
        const float eu = (v[i].x - 10.0f) / 40.0f, ev = (v[i].y - 20.0f) / 40.0f;
        if (std::fabs(v[i].u - eu) > 1e-4f || std::fabs(v[i].v - ev) > 1e-4f) uvOk = false;
        if (i > before && std::fabs(std::hypot(v[i].x - 30.0f, v[i].y - 40.0f) - 20.0f) > 0.01f) onCircle = false;   // a circle
    }
    for (int k = 0; k < d.cmdCount; ++k) if (d.cmds[k].texture == fake) found = true;
    CHECK(uvOk && onCircle && found);
    Reset();
    sim.Frame([&] {
        dl.AddImageRounded(nullptr, {0, 0}, {5, 5}, 2.0f); dl.AddImageRounded(fake, {5, 5}, {0, 0}, 2.0f);
        dl.AddImageRounded(fake, {0, 0}, {NAN, 5}, 2.0f); dl.AddImageRounded(fake, {0, 0}, {5, 5}, 2.0f, {NAN, 0}, {1, 1});
    });
    CHECK(UI::GetDrawData().vertexCount == before);
    Reset();
    sim.Frame([&] { dl.AddImageRounded(fake, {0, 0}, {10, 10}, 0.0f); });   // no rounding: the plain quad
    CHECK(UI::GetDrawData().vertexCount - before == 4);
}

// Theme::cacheDrawing: text runs and rounded rectangles copied from the store draw the same pixels as drawn afresh, at whole
// and fractional positions, after a move and in other colours; a changed text shows the change; widths are the same.
void CacheTextTests() {
    Sim sim(220, 120);
    const UI::DrawList& dl = UI::GetWindowDrawList();
    float ox = 0.0f;
    std::string msg = "Cached text run";
    auto scene = [&] {
        dl.AddText({10 + ox, 10}, kRed, msg.c_str());
        dl.AddText({12.4f + ox, 40.6f}, UI::RGBA(80, 200, 255), "Fractional position");
        dl.AddText({10 + ox, 70}, kRed, "Scaled", 1.5f);
        dl.AddText({150, 10}, kRed, "ab");   // short texts are drawn directly
        dl.AddRectFilled({100 + ox, 60}, {180 + ox, 100}, kRed, 8.0f);   // rounded rectangles are kept too
        dl.AddRectFilled({100.5f, 20.3f}, {170, 50}, UI::RGBA(80, 200, 255), 6.0f);
    };
    auto render = [&](bool cache, int frames, int* verts = nullptr) {
        Reset();
        UI::UseLauncherLook(true);
        UI::Style().cacheDrawing = cache;
        for (int i = 0; i < frames; ++i) sim.Frame(scene);
        if (verts) *verts = UI::GetDrawData().vertexCount;
        const Bitmap b = sim.Draw(0, 0, 0);
        UI::Style().cacheDrawing = false;
        UI::UseLauncherLook(false);
        return b;
    };
    int vPlain = 0, vCached = 0;
    const Bitmap plain = render(false, 1, &vPlain), cached = render(true, 3, &vCached);   // frames 2 and 3 are copies
    CHECK(plain.rgba == cached.rgba && vPlain == vCached);
    ox = 7.0f;   // moved by whole pixels: copied with an offset
    const Bitmap plainMoved = render(false, 1);
    Reset(); UI::UseLauncherLook(true); UI::Style().cacheDrawing = true;
    ox = 0.0f; sim.Frame(scene); ox = 7.0f; sim.Frame(scene);
    const Bitmap cachedMoved = sim.Draw(0, 0, 0);
    msg = "Another text run";   // a different text is not mistaken for the one kept
    sim.Frame(scene);
    const Bitmap changed = sim.Draw(0, 0, 0);
    UI::Style().cacheDrawing = false; UI::UseLauncherLook(false);
    CHECK(plainMoved.rgba == cachedMoved.rgba);
    const Bitmap plainChanged = render(false, 1);
    CHECK(plainChanged.rgba == changed.rgba && changed.rgba != cachedMoved.rgba);
    ox = 0.0f; msg = "Cached text run";
    Reset(); UI::UseLauncherLook(true);
    float wPlain[3], wCached[3][2];
    const char* texts[3] = {"Width of this text", "Another width", "Width of this text!"};
    sim.Frame([&] { for (int i = 0; i < 3; ++i) wPlain[i] = UI::CalcTextSize(texts[i]).x; });
    UI::Style().cacheDrawing = true;
    sim.Frame([&] { for (int k = 0; k < 2; ++k) for (int i = 0; i < 3; ++i) wCached[i][k] = UI::CalcTextSize(texts[i]).x; });
    float scaledCached = 0.0f; sim.Frame([&] { UI::PushTextScale(1.5f); scaledCached = UI::CalcTextSize(texts[0]).x; UI::PopTextScale(); });
    UI::Style().cacheDrawing = false;
    float scaledPlain = 0.0f; sim.Frame([&] { UI::PushTextScale(1.5f); scaledPlain = UI::CalcTextSize(texts[0]).x; UI::PopTextScale(); });
    UI::UseLauncherLook(false);
    bool same = true;
    for (int i = 0; i < 3; ++i) for (int k = 0; k < 2; ++k) same = same && wCached[i][k] == wPlain[i];
    CHECK(same && scaledCached == scaledPlain && scaledPlain > wPlain[0]);
}

} // namespace

int main() {
    RUN_SUITE("okayui drawlist");
    ColourTests();
    ShapeTests();
    PathTests();
    TextAndClipTests();
    ForegroundTests();
    HostileInputTests();
    LargeDrawListTests();
    MeasureTests();
    CursorTests();
    ChildAvailTests();
    InvisibleButtonTests();
    ItemStateTests();
    MouseQueryTests();
    DisabledTests();
    LabTests();
    AntiAliasTests();
    CachedTests();
    ClipSkipTests();
    ImageRoundedTests();
    CacheTextTests();
    TEST_MAIN_RESULT();
}
