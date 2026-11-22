// The OpenGL okay::platform::Painter (engine/src/Platform/GLPainter.cpp) on a real own-backend window under Xvfb
// (tests/run_under_xvfb.sh, Mesa's software GL): the same drawing sequence on the GL painter and on the software
// painter (the reference, OkayUI::soft::CreateBitmapPainter) must give the same picture within GPU rounding.
// Also: kind, driver name, output size, textures, render targets, read-back, two painters on two windows (each
// keeps its own GL context current), resize, and refusals. Exit 77 = skipped (no display, no GLX).
//
// OKAY_GL_PAINTER_SHOTS=<dir> writes gl.ppm and soft.ppm of the compared frame there (to look at, never committed).
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "okay/Platform/Painter.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace okay::platform;
namespace ui2d = okay::ui2d;

namespace {
constexpr int W = 320, H = 240;

void Pump(Backend& b, int ms) {
    const auto t0 = std::chrono::steady_clock::now();
    do {
        Event e;
        while (b.PollEvent(e)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms));
}

std::vector<std::uint8_t> Checker(int n, std::uint8_t a = 255) {   // n x n, 1-texel cells: red / white, top-left texel blue
    std::vector<std::uint8_t> px((size_t)n * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            std::uint8_t* p = &px[((size_t)y * n + x) * 4];
            const bool odd = ((x + y) & 1) != 0;
            p[0] = odd ? 255 : 230; p[1] = odd ? 255 : 20; p[2] = odd ? 255 : 20; p[3] = a;
        }
    px[0] = 0; px[1] = 0; px[2] = 255;
    return px;
}

// Every primitive the apps use, in one frame. `step` limits how far it goes (to locate a difference).
void Scene(Painter& p) {
    p.SetClipRect(nullptr);
    p.SetScale(1, 1);
    p.SetBlendMode(BlendMode::None);
    p.SetDrawColor(20, 40, 60, 255);
    p.Clear();
    // opaque fill, blended fill over it, a "None" fill that writes its alpha as it is
    p.SetDrawColor(220, 30, 30, 255);
    ui2d::Rect r{10, 10, 100, 50}; p.FillRect(&r);
    p.SetBlendMode(BlendMode::Blend);
    p.SetDrawColor(30, 220, 30, 128);
    r = {60, 30, 100, 50}; p.FillRect(&r);
    p.SetBlendMode(BlendMode::None);
    p.SetDrawColor(200, 200, 40, 90);
    r = {170, 10, 20, 20}; p.FillRect(&r);
    // float rectangle (pixel centre rule), outline, lines, points
    p.SetBlendMode(BlendMode::Blend);
    p.SetDrawColor(250, 250, 250, 255);
    ui2d::FRect fr{200.4f, 12.6f, 30.2f, 9.7f}; p.FillRectF(&fr);
    r = {240, 10, 40, 30}; p.DrawRect(&r);
    p.DrawLine(10, 70, 120, 110);
    p.DrawLine(10, 120, 150, 120);
    p.DrawLine(150, 70, 150, 115);
    p.DrawLineF(160.5f, 70.2f, 200.7f, 100.9f);
    p.DrawPoint(5, 5); p.DrawPoint(300, 230);
    // clip
    ui2d::Rect clip{250, 50, 40, 40}; p.SetClipRect(&clip);
    p.SetDrawColor(40, 90, 250, 255);
    r = {230, 30, 100, 100}; p.FillRect(&r);
    p.SetClipRect(nullptr);
    // scale 2
    p.SetScale(2, 2);
    p.SetDrawColor(250, 120, 0, 200);
    r = {150, 65, 10, 8}; p.FillRect(&r);
    p.SetScale(1, 1);
    // textures: nearest checker scaled, flipped, rotated 90, colour and alpha mod, linear
    TextureDesc td; td.width = 8; td.height = 8;
    const auto ck = Checker(8);
    ui2d::Tex t = p.CreateTexture(td, ck.data());
    ui2d::Rect dst{10, 130, 64, 64}; p.Copy(t, nullptr, &dst);
    ui2d::Rect src{0, 0, 4, 4}; ui2d::FRect fdst{80, 130, 32, 32}; p.CopyF(t, &src, &fdst);
    dst = {120, 130, 48, 48}; p.CopyEx(t, nullptr, &dst, 0.0, nullptr, 1);
    dst = {175, 130, 48, 48}; p.CopyEx(t, nullptr, &dst, 90.0, nullptr, 2);
    p.SetTextureColorMod(t, 128, 255, 64); p.SetTextureAlphaMod(t, 160);
    dst = {230, 130, 40, 40}; p.Copy(t, nullptr, &dst);
    p.SetTextureColorMod(t, 255, 255, 255); p.SetTextureAlphaMod(t, 255);
    // a sub-rectangle update with its own pitch
    std::vector<std::uint8_t> up((size_t)3 * 2 * 4 + 8, 0);   // 3 x 2 pixels, pitch 16 bytes
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) { std::uint8_t* q = &up[(size_t)y * 16 + (size_t)x * 4]; q[0] = 0; q[1] = 200; q[2] = 0; q[3] = 255; }
    ui2d::Rect ur{2, 3, 3, 2};
    CHECK(p.UpdateTexture(t, &ur, up.data(), 16));
    dst = {275, 130, 40, 40}; p.Copy(t, nullptr, &dst);
    TextureDesc ld; ld.width = 8; ld.height = 8; ld.scale = ScaleMode::Linear;
    ui2d::Tex tl = p.CreateTexture(ld, ck.data());
    dst = {10, 200, 80, 32}; p.Copy(tl, nullptr, &dst);
    // geometry: coloured triangles with indices, textured triangles, a texture drawn without blending
    const ui2d::RGBA c1{250, 250, 0, 255}, c2{0, 0, 255, 255}, c3{255, 0, 255, 120};
    ui2d::Vertex v[4] = {{{100, 200}, c1, {0, 0}}, {{160, 200}, c2, {1, 0}}, {{160, 236}, c3, {1, 1}}, {{100, 236}, c1, {0, 1}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    p.Geometry(nullptr, v, 4, idx, 6);
    for (auto& q : v) { q.position.x += 70; q.color = ui2d::RGBA{255, 255, 255, 255}; }
    p.Geometry(t, v, 4, idx, 6);
    p.SetTextureBlendMode(t, BlendMode::None);
    const auto half = Checker(8, 100);
    CHECK(p.UpdateTexture(t, nullptr, half.data(), 32));
    dst = {240, 196, 36, 36}; p.Copy(t, nullptr, &dst);
    // a render target: drawn into, then drawn from
    TextureDesc rt; rt.width = 32; rt.height = 24; rt.target = true;
    ui2d::Tex target = p.CreateTexture(rt, nullptr);
    CHECK(target != nullptr);
    CHECK(p.SetRenderTarget(target));
    CHECK(p.GetRenderTarget() == target);
    int ow = 0, oh = 0; p.OutputSize(&ow, &oh);
    CHECK(ow == 32 && oh == 24);
    p.SetBlendMode(BlendMode::None);
    p.SetDrawColor(0, 0, 0, 0); p.Clear();
    p.SetDrawColor(255, 200, 0, 255);
    r = {0, 0, 16, 8}; p.FillRect(&r);
    p.SetDrawColor(0, 200, 255, 255);
    r = {16, 12, 16, 12}; p.FillRect(&r);
    CHECK(p.SetRenderTarget(nullptr));
    p.OutputSize(&ow, &oh);
    CHECK(ow == W && oh == H);
    dst = {280, 196, 32, 24}; p.Copy(target, nullptr, &dst);
    p.DestroyTexture(t); p.DestroyTexture(tl); p.DestroyTexture(target);
}

struct Diff { int bad = 0; double mean = 0; int maxd = 0; };
Diff Compare(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, int w, int h, int tol) {
    Diff d; double sum = 0;
    for (int i = 0; i < w * h; ++i) {
        int m = 0;
        for (int c = 0; c < 3; ++c) { const int e = std::abs((int)a[(size_t)i * 4 + c] - (int)b[(size_t)i * 4 + c]); sum += e; m = std::max(m, e); }
        d.maxd = std::max(d.maxd, m);
        if (m > tol) ++d.bad;
    }
    d.mean = sum / (3.0 * w * h);
    return d;
}
void WritePpm(const std::string& path, const std::vector<std::uint8_t>& px, int w, int h) {
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; ++i) std::fwrite(&px[(size_t)i * 4], 1, 3, f);
        std::fclose(f);
    }
}
const std::uint8_t* At(const std::vector<std::uint8_t>& px, int w, int x, int y) { return &px[((size_t)y * w + x) * 4]; }
} // namespace

int main() {
    RUN_SUITE("OpenGL painter on an own-backend window");
    if (!IsCompiledIn(BackendKind::Own)) { std::cout << "SKIP: own backend not compiled in\n"; return 77; }
    if (!std::getenv("DISPLAY")) { std::cout << "SKIP: no $DISPLAY (run through tests/run_under_xvfb.sh)\n"; return 77; }
    Backend* b = Get(BackendKind::Own);
    if (!b || !b->Init(InitFlags{true, false, false})) { std::cout << "SKIP: cannot open the display\n"; return 77; }

    WindowDesc d;
    d.title = "gl painter test"; d.x = 20; d.y = 20; d.width = W; d.height = H; d.opengl = true;
    std::unique_ptr<Window> win = b->MakeWindow(d);
    if (!win) { std::cout << "SKIP: no GL window: " << b->LastError() << "\n"; return 77; }
    Pump(*b, 200);

    // refusals: a window without a GL pixel format is still accepted on Windows (any window can take one), but never an sdl2 window
    PainterDesc pd; pd.vsync = false;
    std::string err;
    std::unique_ptr<Painter> p = CreateGLPainter(*win, *b, pd, &err);
    CHECK(p != nullptr);
    if (!p) { std::cerr << "  CreateGLPainter: " << err << "\n"; win.reset(); b->Shutdown(); TEST_MAIN_RESULT(); }
    CHECK(p->Kind() == PainterKind::OpenGL);
    CHECK(std::string(p->Name()) == "opengl");
    CHECK(p->DriverName() == "opengl");
    CHECK(p->NativeRenderer() == nullptr);
    int ow = 0, oh = 0; p->OutputSize(&ow, &oh);
    CHECK(ow == W && oh == H);
    std::uint8_t dr = 0, dg = 0, db = 0, da = 0;
    p->SetDrawColor(1, 2, 3, 4);
    CHECK(p->GetDrawColor(&dr, &dg, &db, &da) && dr == 1 && dg == 2 && db == 3 && da == 4);
    {   // textures: size, a non-target cannot be a target, bad sizes refused, bad updates refused
        TextureDesc td; td.width = 5; td.height = 7;
        ui2d::Tex t = p->CreateTexture(td, nullptr);
        int tw = 0, th = 0; p->QueryTexture(t, &tw, &th);
        CHECK(t && tw == 5 && th == 7);
        CHECK(!p->SetRenderTarget(t));
        ui2d::Rect bad{3, 3, 4, 4};
        std::vector<std::uint8_t> px(64 * 4, 255);
        CHECK(!p->UpdateTexture(t, &bad, px.data(), 16));
        CHECK(!p->UpdateTexture(t, nullptr, px.data(), 4));   // pitch smaller than a row
        p->DestroyTexture(t);
        td.width = 0;
        CHECK(p->CreateTexture(td, nullptr) == nullptr);
    }

    // the same frame on both painters
    std::unique_ptr<Painter> ref = OkayUI::soft::CreateBitmapPainter(W, H);
    CHECK(ref != nullptr);
    Scene(*p);
    Scene(*ref);
    std::vector<std::uint8_t> g, s; int gw = 0, gh = 0, sw = 0, sh = 0;
    CHECK(p->ReadPixels(g, gw, gh));
    CHECK(ref->ReadPixels(s, sw, sh));
    CHECK(gw == W && gh == H && sw == W && sh == H);
    if (gw == W && gh == H && sw == W && sh == H) {
        const Diff df = Compare(g, s, W, H, 8);
        std::cout << "  gl vs software: " << df.bad << " pixels differ by more than 8 (of " << W * H << "), mean " << df.mean << ", max " << df.maxd << "\n";
        CHECK(df.bad <= W * H / 200);   // 0.5 percent: edges of the rotated copy and linear sampling round differently
        CHECK(df.mean < 1.0);
        // exact spots: clear colour, opaque fill, blended fill, None fill keeps its colour, clip edge, scale-2 fill, nearest texel, render target
        auto is = [&](int x, int y, int r, int gg, int bb) { const std::uint8_t* q = At(g, W, x, y); return std::abs(q[0] - r) <= 1 && std::abs(q[1] - gg) <= 1 && std::abs(q[2] - bb) <= 1; };
        CHECK(is(315, 100, 20, 40, 60));
        CHECK(is(20, 20, 220, 30, 30));
        CHECK(is(80, 40, 125, 125, 30));     // 30*128 + 220*127 over 255 ...
        CHECK(is(175, 15, 200, 200, 40));
        CHECK(is(260, 60, 40, 90, 250) && !is(240, 60, 40, 90, 250));
        CHECK(is(10, 131, 0, 0, 255));       // the top-left texel of the nearest checker, scaled 8x
        CHECK(is(282, 198, 255, 200, 0) && is(310, 218, 0, 200, 255));
        for (int i = 0; i < 2; ++i) {
            const std::uint8_t* a = At(g, W, 305, 228 + i);
            const std::uint8_t* c = At(s, W, 305, 228 + i);
            CHECK(a[0] == c[0] && a[1] == c[1] && a[2] == c[2]);
        }
        if (const char* dir = std::getenv("OKAY_GL_PAINTER_SHOTS")) {
            WritePpm(std::string(dir) + "/gl.ppm", g, W, H);
            WritePpm(std::string(dir) + "/soft.ppm", s, W, H);
        }
    }
    p->Present();

    // two painters on two windows: each draws into its own window, whichever was used last
    {
        WindowDesc d2 = d; d2.title = "second"; d2.x = 400; d2.width = 160; d2.height = 120;
        std::unique_ptr<Window> win2 = b->MakeWindow(d2);
        CHECK(win2 != nullptr);
        std::unique_ptr<Painter> p2 = win2 ? CreateGLPainter(*win2, *b, pd, &err) : nullptr;
        CHECK(p2 != nullptr);
        if (p2) {
            Pump(*b, 100);
            p->SetBlendMode(BlendMode::None); p2->SetBlendMode(BlendMode::None);
            p->SetDrawColor(200, 0, 0, 255); p->Clear();
            p2->SetDrawColor(0, 0, 200, 255); p2->Clear();
            ui2d::Rect r{10, 10, 20, 20};
            p->SetDrawColor(0, 200, 0, 255); p->FillRect(&r);
            std::vector<std::uint8_t> a, c; int aw = 0, ah = 0, cw = 0, ch = 0;
            CHECK(p2->ReadPixels(c, cw, ch) && cw == 160 && ch == 120);
            CHECK(p->ReadPixels(a, aw, ah) && aw == W && ah == H);
            if (aw == W && cw == 160) {
                CHECK(At(a, W, 5, 5)[0] == 200 && At(a, W, 5, 5)[2] == 0);
                CHECK(At(a, W, 15, 15)[1] == 200);
                CHECK(At(c, 160, 5, 5)[2] == 200 && At(c, 160, 5, 5)[0] == 0);
                CHECK(At(c, 160, 15, 15)[2] == 200);   // the fill went to the first window only
            }
            p2->Present(); p->Present();
            p2.reset();
        }
        win2.reset();
    }

    // the output follows the window's size
    win->SetSize(400, 300);
    Pump(*b, 300);
    p->OutputSize(&ow, &oh);
    CHECK(ow == 400 && oh == 300);
    p->SetDrawColor(10, 20, 30, 255); p->Clear();
    ui2d::Rect r{390, 290, 10, 10};
    p->SetDrawColor(255, 255, 255, 255); p->FillRect(&r);
    std::vector<std::uint8_t> a; int aw = 0, ah = 0;
    CHECK(p->ReadPixels(a, aw, ah) && aw == 400 && ah == 300);
    if (aw == 400 && ah == 300) { CHECK(At(a, 400, 395, 295)[0] == 255); CHECK(At(a, 400, 380, 295)[0] == 10); }
    p->Present();

    p.reset();   // destroys the context before the window
    win.reset();
    b->Shutdown();
    TEST_MAIN_RESULT();
}
