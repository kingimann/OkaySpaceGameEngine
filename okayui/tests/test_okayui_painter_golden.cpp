// okay::platform::Painter: render targets, and a golden-image scene for the headless reference painter.
//
//  * Render targets (Painter::SetTarget): the same checks, worked out by hand, on the software painter
//    (OkayUI::soft::CreateBitmapPainter) and on SDL's software renderer through the sdl2 painter.
//  * One scene (flat and blended rects, outlines, lines, points, clipping, a gradient triangle, nearest and
//    modulated texture copies, a flipped copy, a render target drawn into and copied twice) is drawn by the
//    software painter and compared with the committed golden PNG painter_scene.png (OKAYUI_UPDATE_GOLDEN=1
//    rewrites it). The sdl2 painter draws the same scene and is compared with the software result by count of
//    differing pixels, since SDL places triangle and edge pixels slightly differently.
//
// Written from the Painter interface (engine/include/okay/Platform/Painter.hpp), not from any other project.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/Platform/Painter.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <cstdlib>
#include <cstring>

using namespace okayuitest;
namespace plat = okay::platform;
namespace ui2d = okay::ui2d;

namespace {

struct Img {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;
    const std::uint8_t* at(int x, int y) const { return &px[((size_t)y * (size_t)w + (size_t)x) * 4]; }
};
Img Read(plat::Painter& p) { Img i; p.ReadPixels(i.px, i.w, i.h); return i; }
bool Is(const Img& i, int x, int y, int r, int g, int b, int a = 255) {
    if (x < 0 || y < 0 || x >= i.w || y >= i.h) return false;
    const std::uint8_t* q = i.at(x, y);
    return q[0] == r && q[1] == g && q[2] == b && q[3] == a;
}
void Col(plat::Painter& p, int r, int g, int b, int a = 255) { p.SetDrawColor((std::uint8_t)r, (std::uint8_t)g, (std::uint8_t)b, (std::uint8_t)a); }

void Targets(plat::Painter& p, const char* label, bool software) {
    std::cout << "[targets] " << label << "\n";
    int w = 0, h = 0;
    p.SetClipRect(nullptr); p.SetScale(1, 1);
    p.SetBlendMode(plat::BlendMode::None);
    Col(p, 10, 20, 30); p.Clear();

    plat::TextureDesc td; td.width = 24; td.height = 16; td.target = true;
    ui2d::Tex t = p.CreateTexture(td, nullptr);
    CHECK(t != nullptr);
    CHECK(p.GetTarget() == nullptr);

    CHECK(p.SetTarget(t));
    CHECK(p.GetTarget() == t);
    p.OutputSize(&w, &h);
    CHECK(w == 24 && h == 16);                                   // the target's size while it is current
    Img a = Read(p);
    CHECK(a.w == 24 && a.h == 16 && Is(a, 0, 0, 0, 0, 0, 0) && Is(a, 23, 15, 0, 0, 0, 0));   // a new target is transparent black
    Col(p, 200, 100, 50);
    ui2d::Rect r{2, 3, 5, 4};
    p.FillRect(&r);
    a = Read(p);
    CHECK(Is(a, 2, 3, 200, 100, 50) && Is(a, 6, 6, 200, 100, 50) && Is(a, 7, 6, 0, 0, 0, 0) && Is(a, 1, 3, 0, 0, 0, 0));
    CHECK(p.SetTarget(t));                                       // setting the current target again is fine and keeps the pixels
    CHECK(Is(Read(p), 2, 3, 200, 100, 50));

    // The window's clip and scale are saved while a target is current; a target starts unclipped at scale 1 each time it
    // becomes current (that is what SDL does, and the software painter follows it).
    p.SetTarget(nullptr);
    ui2d::Rect mclip{0, 0, 50, 40};
    p.SetScale(2, 2);                                            // scale first: SDL keeps a clip in device pixels, so the order matters
    p.SetClipRect(&mclip);
    CHECK(p.SetTarget(t));
    CHECK(!p.IsClipEnabled());
    Col(p, 0, 255, 0);
    ui2d::Rect clip{0, 0, 6, 6};
    p.SetClipRect(&clip);
    ui2d::Rect big{0, 0, 20, 20};
    p.FillRect(&big);                                            // scale 1 in the target: the clip cuts it to 6 x 6 pixels
    a = Read(p);
    CHECK(Is(a, 0, 0, 0, 255, 0) && Is(a, 5, 5, 0, 255, 0));    // inside the clip: green
    CHECK(Is(a, 6, 6, 200, 100, 50) && Is(a, 6, 2, 0, 0, 0, 0)); // outside the clip: unchanged (orange from before, transparent)
    CHECK(p.SetTarget(nullptr));
    CHECK(p.GetTarget() == nullptr);
    p.OutputSize(&w, &h);
    CHECK(w == 64 && h == 48);                                   // back on the main surface
    CHECK(p.IsClipEnabled());                                    // the window's clip came back, not the target's
    ui2d::Rect got{0, 0, 0, 0};
    p.GetClipRect(&got);
    CHECK(got.x == 0 && got.y == 0 && got.w == 50 && got.h == 40);
    Col(p, 255, 0, 0);
    ui2d::Rect q{1, 1, 3, 3};
    p.FillRect(&q);                                              // window scale 2: a 6 x 6 square from device 2,2
    Img m = Read(p);
    CHECK(Is(m, 2, 2, 255, 0, 0) && Is(m, 7, 7, 255, 0, 0) && Is(m, 8, 8, 10, 20, 30) && Is(m, 1, 1, 10, 20, 30));
    CHECK(Is(m, 10, 10, 10, 20, 30));                            // the target's fill did not leak onto the main surface
    p.SetClipRect(nullptr);
    p.SetScale(1, 1);

    // A target copied onto the main surface, with the target's own blending.
    p.SetBlendMode(plat::BlendMode::Blend);
    p.SetTextureBlendMode(t, plat::BlendMode::Blend);
    Col(p, 0, 0, 0); p.SetBlendMode(plat::BlendMode::None); p.Clear();
    ui2d::Rect dst{10, 10, 24, 16};
    p.Copy(t, nullptr, &dst);
    m = Read(p);
    CHECK(Is(m, 10, 10, 0, 255, 0));                             // the green fill is at the target's top-left
    CHECK(Is(m, 40, 40, 0, 0, 0));                               // nothing outside the copy
    // The target's transparent part leaves the main surface alone when blended.
    p.SetBlendMode(plat::BlendMode::None); Col(p, 9, 9, 9); p.Clear();
    p.Copy(t, nullptr, &dst);
    m = Read(p);
    CHECK(Is(m, 33, 25, 9, 9, 9));                               // bottom-right of the copy: transparent, so the grey shows

    // Several targets, each keeping its own pixels.
    ui2d::Tex t2 = p.CreateTexture(td, nullptr);
    p.SetTarget(t2);
    Col(p, 1, 2, 3); p.SetBlendMode(plat::BlendMode::None); p.Clear();
    p.SetTarget(t);
    CHECK(Is(Read(p), 0, 0, 0, 255, 0));                         // t still green at 0,0
    p.SetTarget(t2);
    CHECK(Is(Read(p), 0, 0, 1, 2, 3));
    p.QueryTexture(t2, &w, &h);
    CHECK(w == 24 && h == 16);
    // Destroying the current target returns drawing to the main surface.
    p.DestroyTexture(t2);
    CHECK(p.GetTarget() == nullptr);
    p.OutputSize(&w, &h);
    CHECK(w == 64 && h == 48);
    p.DestroyTexture(t);

    if (software) {   // behaviours only the reference painter promises
        ui2d::Tex bogus = (ui2d::Tex)(std::uintptr_t)0x1234;
        CHECK(!p.SetTarget(bogus) && p.GetTarget() == nullptr);   // an unknown texture is refused
        ui2d::Tex t3 = p.CreateTexture(td, nullptr);
        p.SetTarget(t3);
        Col(p, 77, 77, 77); p.SetBlendMode(plat::BlendMode::None); p.Clear();
        ui2d::Rect self{0, 0, 24, 16};
        p.Copy(t3, nullptr, &self);                               // copying a target into itself draws nothing and does not crash
        CHECK(Is(Read(p), 5, 5, 77, 77, 77));
        const std::uint8_t px[4] = {1, 2, 3, 4};
        ui2d::Rect one{0, 0, 1, 1};
        CHECK(p.UpdateTexture(t3, &one, px, 4));                  // updating the current target writes into its pixels
        CHECK(Is(Read(p), 0, 0, 1, 2, 3, 4));
        p.SetTarget(nullptr);
        Col(p, 0, 0, 0); p.Clear();
        p.DestroyTexture(t3);
    }
}

// ---- the golden scene ------------------------------------------------------------------------------------------
constexpr int kW = 96, kH = 72;

ui2d::Tex Checker(plat::Painter& p) {
    std::uint8_t px[8 * 8 * 4];
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        const bool on = ((x / 2) + (y / 2)) % 2 == 0;
        std::uint8_t* q = &px[((size_t)y * 8 + (size_t)x) * 4];
        q[0] = on ? 240 : 40; q[1] = on ? 200 : 60; q[2] = on ? 60 : 160; q[3] = 255;
    }
    plat::TextureDesc d; d.width = 8; d.height = 8;
    return p.CreateTexture(d, px);
}

void Scene(plat::Painter& p) {
    p.SetClipRect(nullptr); p.SetScale(1, 1);
    p.SetBlendMode(plat::BlendMode::None);
    Col(p, 24, 28, 44); p.Clear();
    p.SetBlendMode(plat::BlendMode::Blend);

    // flat and blended rectangles, an outline, lines, points
    Col(p, 200, 60, 60); ui2d::Rect a{4, 4, 24, 16}; p.FillRect(&a);
    Col(p, 60, 120, 220, 128); ui2d::Rect b{16, 10, 24, 16}; p.FillRect(&b);       // half-transparent over the first
    Col(p, 240, 240, 240); ui2d::Rect o{3, 3, 38, 26}; p.DrawRect(&o);
    Col(p, 90, 230, 120); p.DrawLine(46, 4, 90, 28); p.DrawLine(46, 28, 90, 4); p.DrawLine(46, 16, 90, 16);
    Col(p, 255, 255, 0);
    for (int i = 0; i < 8; ++i) p.DrawPoint(48 + i * 4, 33);

    // a clipped gradient triangle and a flat one
    ui2d::Rect clip{4, 36, 30, 28}; p.SetClipRect(&clip);
    ui2d::Vertex v[3] = {
        {{4.0f, 36.0f}, {255, 0, 0, 255}, {0, 0}},
        {{44.0f, 40.0f}, {0, 255, 0, 255}, {0, 0}},
        {{14.0f, 68.0f}, {0, 0, 255, 255}, {0, 0}}};
    p.Geometry(nullptr, v, 3, nullptr, 0);
    p.SetClipRect(nullptr);
    ui2d::Vertex f[3] = {
        {{36.0f, 66.0f}, {250, 250, 250, 160}, {0, 0}},
        {{46.0f, 66.0f}, {250, 250, 250, 160}, {0, 0}},
        {{41.0f, 56.0f}, {250, 250, 250, 160}, {0, 0}}};
    p.Geometry(nullptr, f, 3, nullptr, 0);

    // textures: nearest scaled copy, colour modulated copy, flipped copy
    ui2d::Tex chk = Checker(p);
    ui2d::Rect d1{52, 38, 16, 16}; p.Copy(chk, nullptr, &d1);
    p.SetTextureColorMod(chk, 255, 120, 120); p.SetTextureAlphaMod(chk, 200);
    ui2d::Rect d2{72, 38, 16, 16}; p.Copy(chk, nullptr, &d2);
    p.SetTextureColorMod(chk, 255, 255, 255); p.SetTextureAlphaMod(chk, 255);
    ui2d::Rect src{0, 0, 4, 8}; ui2d::Rect d3{52, 56, 8, 16};
    p.CopyEx(chk, &src, &d3, 0.0, nullptr, 1);                                       // horizontal flip, left half only
    p.DestroyTexture(chk);

    // a render target: drawn into, then copied twice (the second time scaled and faded)
    plat::TextureDesc td; td.width = 12; td.height = 12; td.target = true;
    ui2d::Tex t = p.CreateTexture(td, nullptr);
    p.SetTarget(t);
    p.SetBlendMode(plat::BlendMode::Blend);
    Col(p, 255, 160, 0); ui2d::Rect tr{1, 1, 10, 10}; p.FillRect(&tr);
    Col(p, 20, 20, 20); ui2d::Rect ti{4, 4, 4, 4}; p.FillRect(&ti);
    Col(p, 255, 255, 255); p.DrawLine(0, 0, 11, 11);
    p.SetTarget(nullptr);
    ui2d::Rect e1{62, 56, 12, 12}; p.Copy(t, nullptr, &e1);
    p.SetTextureAlphaMod(t, 120);
    ui2d::Rect e2{76, 56, 16, 12}; p.Copy(t, nullptr, &e2);
    p.DestroyTexture(t);
}

bool Equalish(const Img& a, const Img& b, int tol, double* fracDiff) {
    if (a.w != b.w || a.h != b.h) return false;
    size_t bad = 0;
    for (size_t i = 0; i < a.px.size(); i += 4) {
        bool d = false;
        for (int k = 0; k < 4; ++k) if (std::abs((int)a.px[i + (size_t)k] - (int)b.px[i + (size_t)k]) > tol) d = true;
        if (d) ++bad;
    }
    if (fracDiff) *fracDiff = (double)bad / (double)(a.px.size() / 4);
    return true;
}

} // namespace

int main() {
    RUN_SUITE("painter render targets and golden scene");
    {
        auto sw = OkayUI::soft::CreateBitmapPainter(64, 48);
        CHECK(sw != nullptr);
        if (sw) Targets(*sw, "software", true);
    }
    {
        auto sd = plat::CreateSdl2SurfacePainter(64, 48);
        if (sd) Targets(*sd, "sdl2 (SDL software renderer)", false);
        else std::cout << "[targets] sdl2 painter not available here: skipped\n";
    }

    auto ref = OkayUI::soft::CreateBitmapPainter(kW, kH);
    CHECK(ref != nullptr);
    if (!ref) TEST_MAIN_RESULT();
    Scene(*ref);
    const Img soft = Read(*ref);
    CHECK(soft.w == kW && soft.h == kH);

    // Pixels worked out from the drawing calls, so the golden is not the only thing keeping the scene honest.
    CHECK(Is(soft, 10, 8, 200, 60, 60));                          // the red rectangle, outside the blue one
    CHECK(Is(soft, 2, 2, 24, 28, 44));                            // background outside everything
    CHECK(Is(soft, 3, 10, 240, 240, 240));                        // the outline's left edge
    CHECK(Is(soft, 52, 16, 90, 230, 120) || Is(soft, 52, 17, 90, 230, 120));   // the horizontal line
    CHECK(Is(soft, 48, 33, 255, 255, 0) && Is(soft, 52, 33, 255, 255, 0) && !Is(soft, 50, 33, 255, 255, 0));   // every fourth pixel is a point
    {   // blue (60,120,220) at alpha 128 over red (200,60,60): (60*128 + 200*127 + 127) / 255 = 131
        const std::uint8_t* q = soft.at(20, 14);
        CHECK(q[0] >= 129 && q[0] <= 132 && q[3] == 255);
    }
    CHECK(Is(soft, 54, 40, 240, 200, 60));                        // checker texel (0,0) is the light square, scaled x2
    CHECK(!Is(soft, 14, 45, 24, 28, 44));                         // inside the triangle and its clip: not background
    CHECK(Is(soft, 12, 66, 24, 28, 44));                          // below the clip rectangle: untouched

    {   // golden file
        Bitmap bm(kW, kH);
        bm.rgba = soft.px;
        std::string why;
        const bool ok = MatchesGolden("painter_scene", bm, &why);
        if (!ok) std::cerr << "  golden painter_scene: " << why << "\n";
        CHECK(ok);
    }

    if (auto sd = plat::CreateSdl2SurfacePainter(kW, kH)) {
        Scene(*sd);
        const Img other = Read(*sd);
        double frac = 1.0;
        CHECK(Equalish(soft, other, 3, &frac));
        std::cout << "[scene] sdl2 painter vs software painter: " << frac * 100.0 << "% of pixels differ by more than 3 levels\n";
        CHECK(frac < 0.08);   // triangle and rotated-edge placement differ; flat rects, lines, textures and targets agree
        // The parts both painters promise exactly: the flat rectangle, the outline and the horizontal line.
        CHECK(Is(other, 10, 8, 200, 60, 60) && Is(other, 3, 10, 240, 240, 240) && Is(other, 54, 40, 240, 200, 60));
    } else std::cout << "[scene] sdl2 painter not available here: skipped\n";

    TEST_MAIN_RESULT();
}
