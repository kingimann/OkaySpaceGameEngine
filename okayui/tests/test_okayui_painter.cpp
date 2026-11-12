// okay::platform::Painter, headless, on both implementations:
//   software  the CPU painter the own backend draws with (OkayUI::soft::CreateBitmapPainter)
//   sdl2      SDL's software renderer on an RGBA surface (okay::platform::CreateSdl2SurfacePainter)
// The same drawing runs on each and the pixels are checked against numbers worked out by hand, so the two
// cannot drift apart: exact for rects, clip, 1:1 and nearest texture copies and flat triangles; within one
// level of rounding for alpha blending and colour / alpha modulation (SDL truncates, the software painter
// rounds). Features SDL's software renderer does not have (bilinear sampling, rotation about a point,
// hostile input) are checked on the software painter only and say so.
// Then OkayUI's own frame goes through FrameDrawer (OkayUI::soft) on both painters and is compared with the
// committed golden images and with the draw-list rasteriser.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "../demo/DemoScene.hpp"
#include "okay/Platform/Painter.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>

using namespace okayuitest;
namespace plat = okay::platform;
namespace ui2d = okay::ui2d;
namespace UI = OkayUI;

namespace {

struct Img {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;
    const std::uint8_t* at(int x, int y) const { return &px[((size_t)y * (size_t)w + (size_t)x) * 4]; }
};
Img Read(plat::Painter& p) {
    Img i;
    p.ReadPixels(i.px, i.w, i.h);
    return i;
}
bool Is(const Img& i, int x, int y, int r, int g, int b, int a = 255, int tol = 0) {
    if (x < 0 || y < 0 || x >= i.w || y >= i.h) return false;
    const std::uint8_t* q = i.at(x, y);
    return std::abs(q[0] - r) <= tol && std::abs(q[1] - g) <= tol && std::abs(q[2] - b) <= tol && std::abs(q[3] - a) <= tol;
}
int CountColor(const Img& i, int x0, int y0, int x1, int y1, int r, int g, int b) {
    int n = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) if (Is(i, x, y, r, g, b)) ++n;
    return n;
}
void Fill(plat::Painter& p, int r, int g, int b, int a = 255) {
    p.SetClipRect(nullptr);
    p.SetBlendMode(plat::BlendMode::None);
    p.SetDrawColor((std::uint8_t)r, (std::uint8_t)g, (std::uint8_t)b, (std::uint8_t)a);
    p.Clear();
}
void Color(plat::Painter& p, int r, int g, int b, int a = 255) { p.SetDrawColor((std::uint8_t)r, (std::uint8_t)g, (std::uint8_t)b, (std::uint8_t)a); }

// A 4 x 4 texture: pixel (i, j) = (i * 60, j * 60, 7, 255).
ui2d::Tex Grid(plat::Painter& p, plat::ScaleMode sm = plat::ScaleMode::Nearest) {
    std::uint8_t px[4 * 4 * 4];
    for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) {
        std::uint8_t* q = &px[((size_t)j * 4 + (size_t)i) * 4];
        q[0] = (std::uint8_t)(i * 60); q[1] = (std::uint8_t)(j * 60); q[2] = 7; q[3] = 255;
    }
    plat::TextureDesc d; d.width = 4; d.height = 4; d.scale = sm;
    return p.CreateTexture(d, px);
}

void Suite(plat::Painter& p, const char* label, bool software) {
    std::cout << "[painter] " << label << "\n";
    const int tolBlend = software ? 0 : 1;   // SDL truncates where the software painter rounds
    int w = 0, h = 0;
    p.OutputSize(&w, &h);
    CHECK(w == 64 && h == 48);
    const bool wasNative = p.NativeRenderer() != nullptr;
    CHECK(wasNative == !software);                        // an SDL_Renderer behind sdl2 only
    CHECK(p.Kind() == (software ? plat::PainterKind::Software : plat::PainterKind::Sdl2));

    {   // clear, flat rects, null rect = the whole target
        Fill(p, 10, 20, 30);
        Img i = Read(p);
        CHECK(i.w == 64 && i.h == 48 && CountColor(i, 0, 0, 64, 48, 10, 20, 30) == 64 * 48);
        Color(p, 200, 0, 0);
        const ui2d::Rect r{4, 5, 10, 6};
        p.FillRect(&r);
        i = Read(p);
        CHECK(CountColor(i, 4, 5, 14, 11, 200, 0, 0) == 60);
        CHECK(Is(i, 3, 5, 10, 20, 30) && Is(i, 14, 5, 10, 20, 30) && Is(i, 4, 4, 10, 20, 30) && Is(i, 4, 11, 10, 20, 30));
        Color(p, 0, 0, 200);
        p.FillRect(nullptr);
        i = Read(p);
        CHECK(CountColor(i, 0, 0, 64, 48, 0, 0, 200) == 64 * 48);
        const ui2d::FRect fr{10.0f, 10.0f, 4.0f, 3.0f};     // float rect: covers the pixels whose centre is inside
        Color(p, 9, 9, 9);
        p.FillRectF(&fr);
        i = Read(p);
        CHECK(CountColor(i, 10, 10, 14, 13, 9, 9, 9) == 12 && Is(i, 14, 10, 0, 0, 200) && Is(i, 10, 13, 0, 0, 200));
    }
    {   // alpha blending (source-over), and blend none replacing alpha too
        Fill(p, 100, 100, 100);
        p.SetBlendMode(plat::BlendMode::Blend);
        CHECK(p.GetBlendMode() == plat::BlendMode::Blend);
        Color(p, 200, 0, 0, 128);
        const ui2d::Rect r{8, 8, 8, 8};
        p.FillRect(&r);
        Img i = Read(p);
        CHECK(Is(i, 8, 8, 150, 50, 50, 255, tolBlend) && Is(i, 15, 15, 150, 50, 50, 255, tolBlend));
        CHECK(Is(i, 7, 8, 100, 100, 100));
        Color(p, 0, 0, 0, 0);   // alpha 0 changes nothing
        p.FillRect(&r);
        i = Read(p);
        CHECK(Is(i, 8, 8, 150, 50, 50, 255, tolBlend));
        p.SetBlendMode(plat::BlendMode::None);
        CHECK(p.GetBlendMode() == plat::BlendMode::None);
        Color(p, 5, 6, 7, 64);
        p.FillRect(&r);
        i = Read(p);
        CHECK(Is(i, 9, 9, 5, 6, 7, 64));   // replaced, alpha included
    }
    {   // clip rectangle
        Fill(p, 0, 0, 0);
        const ui2d::Rect c{10, 10, 5, 5};
        p.SetClipRect(&c);
        CHECK(p.IsClipEnabled());
        ui2d::Rect got{0, 0, 0, 0};
        p.GetClipRect(&got);
        CHECK(got.x == 10 && got.y == 10 && got.w == 5 && got.h == 5);
        Color(p, 255, 255, 255);
        p.FillRect(nullptr);
        Img i = Read(p);
        CHECK(CountColor(i, 0, 0, 64, 48, 255, 255, 255) == 25 && Is(i, 10, 10, 255, 255, 255) && Is(i, 14, 14, 255, 255, 255) && Is(i, 15, 15, 0, 0, 0) && Is(i, 9, 10, 0, 0, 0));
        const ui2d::Rect big{0, 0, 64, 48};
        Color(p, 1, 2, 3);
        p.FillRect(&big);
        i = Read(p);
        CHECK(CountColor(i, 0, 0, 64, 48, 1, 2, 3) == 25);
        p.SetClipRect(nullptr);
        CHECK(!p.IsClipEnabled());
        Color(p, 7, 7, 7);
        p.FillRect(nullptr);
        i = Read(p);
        CHECK(CountColor(i, 0, 0, 64, 48, 7, 7, 7) == 64 * 48);
    }
    {   // an empty clip rectangle clips everything away
        Fill(p, 0, 0, 0);
        const ui2d::Rect c{10, 10, 0, 0};
        p.SetClipRect(&c);
        Color(p, 255, 0, 0);
        p.FillRect(nullptr);
        Img i = Read(p);
        CHECK(software ? CountColor(i, 0, 0, 64, 48, 255, 0, 0) == 0 : true);   // SDL 2.0.x treats a 0x0 clip rect as "draw nothing" too, but older ones drew; only the software painter is pinned
        p.SetClipRect(nullptr);
    }
    {   // outlines, lines, points
        Fill(p, 0, 0, 0);
        Color(p, 255, 0, 0);
        const ui2d::Rect r{20, 20, 8, 6};
        p.DrawRect(&r);
        Img i = Read(p);
        CHECK(CountColor(i, 0, 0, 64, 48, 255, 0, 0) == 2 * 8 + 2 * 4);
        CHECK(Is(i, 20, 20, 255, 0, 0) && Is(i, 27, 20, 255, 0, 0) && Is(i, 20, 25, 255, 0, 0) && Is(i, 27, 25, 255, 0, 0) && Is(i, 23, 22, 0, 0, 0));
        Fill(p, 0, 0, 0);
        Color(p, 0, 255, 0);
        p.DrawLine(0, 40, 9, 40);
        p.DrawLine(50, 5, 50, 14);
        p.DrawLine(30, 30, 35, 35);
        p.DrawPoint(60, 2);
        i = Read(p);
        CHECK(CountColor(i, 0, 40, 10, 41, 0, 255, 0) == 10);      // both end points included
        CHECK(CountColor(i, 50, 5, 51, 15, 0, 255, 0) == 10);
        int diag = 0; for (int k = 0; k <= 5; ++k) if (Is(i, 30 + k, 30 + k, 0, 255, 0)) ++diag;
        CHECK(diag == 6 && CountColor(i, 0, 0, 64, 48, 0, 255, 0) == 10 + 10 + 6 + 1);
        CHECK(Is(i, 60, 2, 0, 255, 0));
    }
    {   // textures: 1:1 copy, scaled nearest, sub-rectangle, flip
        Fill(p, 0, 0, 0);
        ui2d::Tex t = Grid(p);
        CHECK(t != nullptr);
        int tw = 0, th = 0;
        p.QueryTexture(t, &tw, &th);
        CHECK(tw == 4 && th == 4);
        const ui2d::Rect d1{10, 10, 4, 4};
        p.Copy(t, nullptr, &d1);
        const ui2d::Rect src{1, 1, 2, 2}, d2{30, 10, 4, 4};
        p.Copy(t, &src, &d2);
        const ui2d::Rect d3{10, 30, 4, 4};
        p.CopyEx(t, nullptr, &d3, 0.0, nullptr, ui2d::FlipH);
        Img i = Read(p);
        bool exact = true;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) exact = exact && Is(i, 10 + x, 10 + y, x * 60, y * 60, 7);
        CHECK(exact);
        bool scaled = true;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) scaled = scaled && Is(i, 30 + x, 10 + y, (1 + x / 2) * 60, (1 + y / 2) * 60, 7);
        CHECK(scaled);
        bool flipped = true;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) flipped = flipped && Is(i, 10 + x, 30 + y, (3 - x) * 60, y * 60, 7);
        CHECK(flipped);
        CHECK(Is(i, 9, 10, 0, 0, 0) && Is(i, 14, 10, 0, 0, 0) && Is(i, 10, 14, 0, 0, 0));
        // the float destination takes the same pixels as the integer one
        Fill(p, 0, 0, 0);
        const ui2d::FRect f1{10.0f, 10.0f, 4.0f, 4.0f};
        p.CopyF(t, nullptr, &f1);
        i = Read(p);
        exact = true;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) exact = exact && Is(i, 10 + x, 10 + y, x * 60, y * 60, 7);
        CHECK(exact);
        p.DestroyTexture(t);
    }
    {   // colour and alpha modulation, texture blend mode
        Fill(p, 100, 100, 100);
        ui2d::Tex t = Grid(p);
        p.SetTextureColorMod(t, 128, 255, 255);
        const ui2d::Rect d{10, 10, 4, 4};
        p.Copy(t, nullptr, &d);
        Img i = Read(p);
        CHECK(Is(i, 11, 10, 30, 0, 7, 255, tolBlend + 1));          // (60 * 128) / 255
        CHECK(Is(i, 13, 12, 90, 120, 7, 255, tolBlend + 1));
        p.SetTextureColorMod(t, 255, 255, 255);
        p.SetTextureAlphaMod(t, 128);
        const ui2d::Rect d2{20, 10, 4, 4};
        p.Copy(t, nullptr, &d2);
        i = Read(p);
        CHECK(Is(i, 23, 12, 140, 110, 54, 255, tolBlend + 1));     // 180 * 128/255 + 100 * 127/255 ...
        p.SetTextureBlendMode(t, plat::BlendMode::None);
        const ui2d::Rect d3{30, 10, 4, 4};
        p.Copy(t, nullptr, &d3);
        i = Read(p);
        CHECK(Is(i, 33, 12, 180, 120, 7, 128, 1));                  // replaced, alpha modulated into the destination alpha
        p.DestroyTexture(t);
    }
    {   // transparent texels leave what is underneath, with blending
        Fill(p, 50, 60, 70);
        std::uint8_t px[2 * 1 * 4] = {255, 0, 0, 255, 0, 255, 0, 0};
        plat::TextureDesc d; d.width = 2; d.height = 1;
        ui2d::Tex t = p.CreateTexture(d, px);
        const ui2d::Rect dst{5, 5, 2, 1};
        p.Copy(t, nullptr, &dst);
        Img i = Read(p);
        CHECK(Is(i, 5, 5, 255, 0, 0) && Is(i, 6, 5, 50, 60, 70));
        p.DestroyTexture(t);
    }
    {   // UpdateTexture of a part, then drawn
        Fill(p, 0, 0, 0);
        plat::TextureDesc d; d.width = 4; d.height = 4;
        ui2d::Tex t = p.CreateTexture(d, nullptr);
        const std::uint8_t upd[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255};
        const ui2d::Rect part{1, 1, 2, 2};
        CHECK(p.UpdateTexture(t, &part, upd, 8));
        const ui2d::Rect dst{10, 10, 4, 4};
        p.Copy(t, nullptr, &dst);
        Img i = Read(p);
        CHECK(Is(i, 11, 11, 255, 0, 0) && Is(i, 12, 11, 0, 255, 0) && Is(i, 11, 12, 0, 0, 255) && Is(i, 12, 12, 255, 255, 0));
        CHECK(Is(i, 10, 10, 0, 0, 0) && Is(i, 13, 13, 0, 0, 0));
        p.DestroyTexture(t);
    }
    {   // triangles: a flat one with the fill rule, a textured quad equal to a copy, vertex colour, clip
        Fill(p, 0, 0, 0);
        p.SetBlendMode(plat::BlendMode::Blend);
        const ui2d::RGBA red{255, 0, 0, 255};
        ui2d::Vertex tri[3] = {{{10, 10}, red, {0, 0}}, {{20, 10}, red, {0, 0}}, {{10, 20}, red, {0, 0}}};
        p.Geometry(nullptr, tri, 3, nullptr, 0);
        Img i = Read(p);
        const int n = CountColor(i, 0, 0, 64, 48, 255, 0, 0);
        CHECK(n == 45 || n == 55);        // the hypotenuse's pixel centres sit exactly on the edge
        CHECK(Is(i, 10, 10, 255, 0, 0) && Is(i, 19, 10, 255, 0, 0) && Is(i, 10, 19, 255, 0, 0) && Is(i, 19, 19, 0, 0, 0));
        // textured quad, 1:1, equals the copy
        Fill(p, 0, 0, 0);
        ui2d::Tex t = Grid(p);
        const ui2d::RGBA white{255, 255, 255, 255};
        ui2d::Vertex q[4] = {{{40, 10}, white, {0, 0}}, {{44, 10}, white, {1, 0}}, {{44, 14}, white, {1, 1}}, {{40, 14}, white, {0, 1}}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        p.Geometry(t, q, 4, idx, 6);
        i = Read(p);
        bool exact = true;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) exact = exact && Is(i, 40 + x, 10 + y, x * 60, y * 60, 7);
        CHECK(exact);
        // the vertex colour multiplies the texel
        Fill(p, 0, 0, 0);
        const ui2d::RGBA half{128, 255, 255, 255};
        ui2d::Vertex q2[4] = {{{40, 10}, half, {0, 0}}, {{44, 10}, half, {1, 0}}, {{44, 14}, half, {1, 1}}, {{40, 14}, half, {0, 1}}};
        p.Geometry(t, q2, 4, idx, 6);
        i = Read(p);
        CHECK(Is(i, 43, 10, 90, 0, 7, 255, 2));
        // a clip rectangle crops geometry
        Fill(p, 0, 0, 0);
        const ui2d::Rect c{41, 11, 2, 2};
        p.SetClipRect(&c);
        p.Geometry(t, q, 4, idx, 6);
        p.SetClipRect(nullptr);
        i = Read(p);
        int lit = 0; for (int y = 0; y < 48; ++y) for (int x = 0; x < 64; ++x) if (!Is(i, x, y, 0, 0, 0)) ++lit;
        CHECK(lit >= 3 && lit <= 4 && Is(i, 41, 11, 60, 60, 7) && Is(i, 42, 12, 120, 120, 7));   // (the texel at 0,0 is black)
        p.DestroyTexture(t);
        // a gradient: the middle of a red -> blue quad is the mix
        Fill(p, 0, 0, 0);
        const ui2d::RGBA R{255, 0, 0, 255}, B{0, 0, 255, 255};
        ui2d::Vertex g[4] = {{{0, 0}, R, {0, 0}}, {{64, 0}, B, {0, 0}}, {{64, 48}, B, {0, 0}}, {{0, 48}, R, {0, 0}}};
        p.Geometry(nullptr, g, 4, idx, 6);
        i = Read(p);
        CHECK(Is(i, 32, 24, 127, 0, 128, 255, 3));
        CHECK(Is(i, 1, 24, 250, 0, 5, 255, 8));
    }
    {   // logical scale
        Fill(p, 0, 0, 0);
        p.SetScale(2.0f, 2.0f);
        Color(p, 255, 0, 0);
        const ui2d::Rect r{1, 1, 3, 3};
        p.FillRect(&r);
        p.SetScale(1.0f, 1.0f);
        Img i = Read(p);
        CHECK(CountColor(i, 0, 0, 64, 48, 255, 0, 0) == 36 && Is(i, 2, 2, 255, 0, 0) && Is(i, 7, 7, 255, 0, 0) && Is(i, 8, 8, 0, 0, 0) && Is(i, 1, 1, 0, 0, 0));
    }
    // Only SDL's software renderer lacks these, so they are pinned on the software painter.
    if (software) {
        {   // bilinear sampling
            Fill(p, 0, 0, 0);
            const std::uint8_t px[2 * 1 * 4] = {0, 0, 0, 255, 255, 255, 255, 255};
            plat::TextureDesc d; d.width = 2; d.height = 1; d.scale = plat::ScaleMode::Linear;
            ui2d::Tex t = p.CreateTexture(d, px);
            const ui2d::Rect dst{0, 0, 8, 1};
            p.Copy(t, nullptr, &dst);
            Img i = Read(p);
            CHECK(Is(i, 0, 0, 0, 0, 0) && Is(i, 7, 0, 255, 255, 255));
            CHECK(i.at(3, 0)[0] > 40 && i.at(3, 0)[0] < 215 && i.at(3, 0)[0] < i.at(4, 0)[0]);
            p.SetTextureScaleMode(t, plat::ScaleMode::Nearest);
            Fill(p, 0, 0, 0);
            p.Copy(t, nullptr, &dst);
            i = Read(p);
            CHECK(Is(i, 3, 0, 0, 0, 0) && Is(i, 4, 0, 255, 255, 255));
            p.DestroyTexture(t);
        }
        {   // rotation about the middle: a quarter turn and a half turn of the grid
            Fill(p, 0, 0, 0);
            ui2d::Tex t = Grid(p);
            const ui2d::Rect d{10, 10, 4, 4}, d2{30, 10, 4, 4};
            p.CopyEx(t, nullptr, &d, 180.0, nullptr, 0);
            p.CopyEx(t, nullptr, &d2, 90.0, nullptr, 0);
            Img i = Read(p);
            bool half = true;
            for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) half = half && Is(i, 10 + x, 10 + y, (3 - x) * 60, (3 - y) * 60, 7);
            CHECK(half);
            // 90 degrees clockwise: the top-left texel goes to the top-right corner
            CHECK(Is(i, 33, 10, 0, 0, 7) && Is(i, 33, 13, 180, 0, 7) && Is(i, 30, 10, 0, 180, 7));
            p.DestroyTexture(t);
        }
        {   // bad input is ignored, never trusted
            CHECK(p.CreateTexture(plat::TextureDesc(), nullptr) == nullptr);
            plat::TextureDesc huge; huge.width = 100000; huge.height = 4;
            CHECK(p.CreateTexture(huge, nullptr) == nullptr);
            p.DestroyTexture(nullptr);
            int bogus = 0;
            p.DestroyTexture(&bogus);                      // a pointer that is not a texture of this painter
            const ui2d::Rect r{0, 0, 4, 4};
            p.Copy(&bogus, nullptr, &r);
            p.SetTextureAlphaMod(&bogus, 3);
            CHECK(!p.UpdateTexture(&bogus, nullptr, &bogus, 4));
            Fill(p, 0, 0, 0);
            const ui2d::Rect giant{INT_MIN / 2, INT_MIN / 2, INT_MAX, INT_MAX};
            Color(p, 255, 0, 0);
            p.FillRect(&giant);
            Img i = Read(p);
            CHECK(CountColor(i, 0, 0, 64, 48, 255, 0, 0) == 64 * 48);
            p.DrawLine(INT_MIN, INT_MIN, INT_MAX, INT_MAX);          // would walk forever
            const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
            const ui2d::RGBA c{1, 2, 3, 255};
            ui2d::Vertex bad[3] = {{{nan, 0}, c, {nan, 0}}, {{inf, 5}, c, {0, inf}}, {{0, -inf}, c, {0, 0}}};
            p.Geometry(nullptr, bad, 3, nullptr, 0);
            const int wild[6] = {0, 1, 99, -4, 2, 1};
            p.Geometry(nullptr, bad, 3, wild, 6);
            const ui2d::Rect badSrc{-5, 0, 3, 3};
            ui2d::Tex t = Grid(p);
            p.Copy(t, &badSrc, &r);
            const ui2d::FRect nanDst{nan, 0, 4, 4};
            p.CopyF(t, nullptr, &nanDst);
            p.DestroyTexture(t);
            CHECK(true);                                           // got here: nothing hung, nothing crashed
        }
    }
    // frame: a bitmap painter has no window; Present is harmless and the read-back follows the drawing
    p.Present();
    Fill(p, 1, 2, 3);
    p.Present();
    Img i = Read(p);
    CHECK(Is(i, 0, 0, 1, 2, 3) && Is(i, 63, 47, 1, 2, 3));
}

// ---- OkayUI's frame through a painter ---------------------------------------------------------
Bitmap ViaPainter(plat::Painter& p, int W, int H) {
    p.SetClipRect(nullptr);
    p.SetBlendMode(plat::BlendMode::None);
    p.SetDrawColor(12, 14, 20, 255);
    p.Clear();
    UI::soft::FrameDrawer fd(p);
    fd.Draw(UI::GetDrawData());
    Bitmap b(W, H);
    std::vector<std::uint8_t> rgba; int w = 0, h = 0;
    p.ReadPixels(rgba, w, h);
    if (w == W && h == H) b.rgba = rgba;
    return b;
}
void GoldenCheck(const char* name, const Bitmap& b) {
    std::string why;
    const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << " through the painter: " << why << "\n";
    CHECK(ok);
}
// Share of pixels that differ from the draw-list rasteriser's picture by more than `tol` in any channel.
double DiffShare(const Bitmap& a, const Bitmap& b, int tol) {
    if (a.w != b.w || a.h != b.h) return 1.0;
    size_t bad = 0;
    for (size_t i = 0; i < a.rgba.size(); i += 4) {
        bool d = false;
        for (int k = 0; k < 4; ++k) if (std::abs((int)a.rgba[i + (size_t)k] - (int)b.rgba[i + (size_t)k]) > tol) d = true;
        if (d) ++bad;
    }
    return (double)bad / (double)(a.w * a.h);
}

} // namespace

int main() {
    RUN_SUITE("okayui painter");
    {
        auto sw = UI::soft::CreateBitmapPainter(64, 48);
        CHECK(sw != nullptr);
        if (sw) Suite(*sw, "software (own)", true);
        auto sd = plat::CreateSdl2SurfacePainter(64, 48);
        CHECK(sd != nullptr);
        if (sd) Suite(*sd, "sdl2 (SDL software renderer on a surface)", false);
    }
    CHECK(UI::soft::CreateBitmapPainter(0, 10) == nullptr && UI::soft::CreateBitmapPainter(10, 100000) == nullptr);
    {   // the shared cache and font atlas upload, on both painters
        for (int which = 0; which < 2; ++which) {
            auto p = which == 0 ? UI::soft::CreateBitmapPainter(16, 16) : plat::CreateSdl2SurfacePainter(16, 16);
            if (!p) { CHECK(false); continue; }
            CHECK(p->LoadTexture("") == nullptr);
            CHECK(p->LoadTexture("/definitely/not/here.png") == nullptr);     // a miss, remembered
            CHECK(p->CachedTexture("/definitely/not/here.png") == nullptr);
        }
    }
    {   // OkayUI classic look: the frame through the painter equals the golden image the draw-list rasteriser matches
        Reset();
        Sim sim(800, 780);
        okayuidemo::State st;
        okayuidemo::RunScript(sim, st);
        auto sw = UI::soft::CreateBitmapPainter(800, 780);
        auto sd = plat::CreateSdl2SurfacePainter(800, 780);
        CHECK(sw && sd);
        if (sw && sd) {
            const Bitmap viaSoft = ViaPainter(*sw, 800, 780);
            const Bitmap viaSdl = ViaPainter(*sd, 800, 780);
            GoldenCheck("demo_window", viaSoft);                         // golden image
            const Bitmap ref = sim.Draw(12, 14, 20);
            const double d = DiffShare(viaSoft, ref, 0);
            std::cout << "  classic: software painter vs draw-list rasteriser, differing pixels " << d * 100 << " %\n";
            CHECK(d == 0.0);                                             // the same rasteriser: identical
            const double ds = DiffShare(viaSdl, viaSoft, 2);
            std::cout << "  classic: sdl2 painter vs software painter, differing pixels (> 2 levels) " << ds * 100 << " %\n";
            CHECK(ds < 0.002);
        }
    }
    {   // launcher look: glyph atlas texture, sampled linearly by the painter like a GPU does
        Reset();
        UI::UseLauncherLook(true); UI::Style().caretBlink = false;
        Sim sim(800, 780);
        okayuidemo::State st;
        okayuidemo::RunScript(sim, st);
        auto sw = UI::soft::CreateBitmapPainter(800, 780);
        auto sd = plat::CreateSdl2SurfacePainter(800, 780);
        CHECK(sw && sd);
        if (sw && sd) {
            const Bitmap viaSoft = ViaPainter(*sw, 800, 780);
            const Bitmap viaSdl = ViaPainter(*sd, 800, 780);
            const Bitmap ref = sim.Draw(12, 14, 20);        // nearest-sampled atlas
            const double d = DiffShare(viaSoft, ref, 8);
            std::cout << "  launcher look: software painter (linear) vs golden-style rasteriser (nearest), differing > 8 levels " << d * 100 << " %\n";
            CHECK(d < 0.03);                                // glyph edges only
            const double ds = DiffShare(viaSdl, viaSoft, 8);
            std::cout << "  launcher look: sdl2 painter vs software painter, differing > 8 levels " << ds * 100 << " %\n";
            CHECK(ds < 0.03);
        }
        UI::UseLauncherLook(false);
    }
    TEST_MAIN_RESULT();
}
