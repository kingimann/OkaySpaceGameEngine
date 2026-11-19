// Our own Dear ImGui renderer (shared/ImGuiPainterRenderer.hpp) on both painters, headless: a real ImGui context draws a frame
// (text, a button, flat rectangles, a clipped rectangle) and the draw data goes through okay::platform::Painter. Checked against
// numbers worked out by hand (flat colours, clip edges, framebuffer scale 1 and 2), against each other (software painter vs SDL's
// software renderer on a surface: close, not equal, text is bilinear), and for the texture request protocol of imgui.h.
#include "ImGuiPainterRenderer.hpp"
#include "okay/Platform/Painter.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace plat = okay::platform;
namespace ui2d = okay::ui2d;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

struct Img { int w = 0, h = 0; std::vector<std::uint8_t> px; };
static Img Read(plat::Painter& p) { Img i; p.ReadPixels(i.px, i.w, i.h); return i; }
static bool Is(const Img& i, int x, int y, int r, int g, int b, int tol = 0) {
    if (x < 0 || y < 0 || x >= i.w || y >= i.h) return false;
    const std::uint8_t* q = &i.px[((size_t)y * i.w + x) * 4];
    return std::abs(q[0] - r) <= tol && std::abs(q[1] - g) <= tol && std::abs(q[2] - b) <= tol;
}

// One frame on a fresh context at `fb` (framebuffer scale), drawn onto `p` (already cleared by us).
static void Frame(plat::Painter& p, float fb, bool withWidgets, ImGuiContext*& keep) {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    okay::imguirender::Init(io);
    io.DisplaySize = ImVec2(320, 240);
    io.DisplayFramebufferScale = ImVec2(fb, fb);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(30, 150), ImVec2(70, 170), IM_COL32(255, 0, 0, 255));                       // red
    bg->PushClipRect(ImVec2(100, 150), ImVec2(120, 170), true);                                           // only 20 x 20 of the next shows
    bg->AddRectFilled(ImVec2(90, 140), ImVec2(160, 180), IM_COL32(0, 255, 0, 255));                     // green
    bg->PopClipRect();
    if (withWidgets) {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(200, 110));
        ImGui::Begin("Window", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("Hello, painter");
        ImGui::Button("Press");
        ImGui::End();
    }
    ImGui::Render();
    p.SetBlendMode(plat::BlendMode::None);
    p.SetDrawColor(30, 30, 34, 255);
    p.Clear();
    p.SetBlendMode(plat::BlendMode::Blend);
    okay::imguirender::Render(p, ImGui::GetDrawData());
    keep = ctx;
}

static void EndFrame(plat::Painter& p, ImGuiContext* ctx) {
    ImGui::SetCurrentContext(ctx);
    // every atlas texture was answered
    bool allOk = true; int n = 0;
    for (ImTextureData* t : ImGui::GetPlatformIO().Textures) { ++n; if (t->Status != ImTextureStatus_OK || t->TexID == ImTextureID_Invalid) allOk = false; }
    CHECK(n >= 1 && allOk);
    okay::imguirender::Shutdown(p, ImGui::GetIO());
    for (ImTextureData* t : ImGui::GetPlatformIO().Textures) CHECK(t->TexID == ImTextureID_Invalid);
    ImGui::DestroyContext(ctx);
}

static void Flat(plat::Painter& p, float fb, const char* name) {
    std::printf("[%s] scale %.0f\n", name, fb);
    ImGuiContext* ctx = nullptr;
    Frame(p, fb, false, ctx);
    Img im = Read(p);
    CHECK(im.w == (int)(320 * fb) && im.h == (int)(240 * fb));
    auto L = [&](float v) { return (int)std::lround(v * fb); };
    // the red rectangle (30,150)-(70,170): inside red, one pixel outside the background
    CHECK(Is(im, L(31), L(151), 255, 0, 0) && Is(im, L(69) , L(169), 255, 0, 0) && Is(im, L(50), L(160), 255, 0, 0));
    CHECK(Is(im, L(30) - 1, L(160), 30, 30, 34) && Is(im, L(70) + 1, L(160), 30, 30, 34) && Is(im, L(50), L(150) - 1, 30, 30, 34) && Is(im, L(50), L(170) + 1, 30, 30, 34));
    // the green one is cut to its clip (100,150)-(120,170)
    CHECK(Is(im, L(110), L(160), 0, 255, 0) && Is(im, L(101), L(151), 0, 255, 0) && Is(im, L(119), L(169), 0, 255, 0));
    CHECK(Is(im, L(95), L(160), 30, 30, 34) && Is(im, L(125), L(160), 30, 30, 34) && Is(im, L(110), L(145), 30, 30, 34) && Is(im, L(110), L(175), 30, 30, 34));
    EndFrame(p, ctx);
}

static double MeanDiff(const Img& a, const Img& b) {
    if (a.w != b.w || a.h != b.h) return 1e9;
    double s = 0;
    for (size_t i = 0; i < a.px.size(); ++i) if (i % 4 != 3) s += std::abs((int)a.px[i] - (int)b.px[i]);
    return s / ((double)a.px.size() * 0.75);
}
static int Lit(const Img& a, int x0, int y0, int x1, int y1) {   // pixels that differ from the clear colour
    int n = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) if (!Is(a, x, y, 30, 30, 34, 0)) ++n;
    return n;
}

int main() {
    {   // flat shapes at scale 1 and 2, on each painter
        auto sw1 = OkayUI::soft::CreateBitmapPainter(320, 240);
        auto sd1 = plat::CreateSdl2SurfacePainter(320, 240);
        auto sw2 = OkayUI::soft::CreateBitmapPainter(640, 480);
        auto sd2 = plat::CreateSdl2SurfacePainter(640, 480);
        CHECK(sw1 && sd1 && sw2 && sd2);
        if (sw1) Flat(*sw1, 1.0f, "software");
        if (sw2) Flat(*sw2, 2.0f, "software");
        if (sd1) Flat(*sd1, 1.0f, "sdl2 surface");
        if (sd2) Flat(*sd2, 2.0f, "sdl2 surface");
    }
    {   // the whole frame (text, window, button): painted at all, and the two painters agree closely
        auto sw = OkayUI::soft::CreateBitmapPainter(320, 240);
        auto sd = plat::CreateSdl2SurfacePainter(320, 240);
        CHECK(sw && sd);
        if (sw && sd) {
            ImGuiContext *c1 = nullptr, *c2 = nullptr;
            Frame(*sw, 1.0f, true, c1);
            Img a = Read(*sw);
            EndFrame(*sw, c1);
            Frame(*sd, 1.0f, true, c2);
            Img b = Read(*sd);
            EndFrame(*sd, c2);
            CHECK(Lit(a, 10, 10, 210, 120) > 1500 && Lit(b, 10, 10, 210, 120) > 1500);   // a window with text is not blank
            const double d = MeanDiff(a, b);
            std::printf("[frame] mean difference software vs sdl2 surface: %.3f levels\n", d);
            CHECK(d < 3.0);
        }
    }
    {   // the texture request protocol, Alpha8 and RGBA32, on both painters
        auto sw = OkayUI::soft::CreateBitmapPainter(16, 16);
        auto sd = plat::CreateSdl2SurfacePainter(16, 16);
        for (plat::Painter* p : {sw.get(), sd.get()}) {
            if (!p) { CHECK(false); continue; }
            ImTextureData td;
            td.Create(ImTextureFormat_Alpha8, 4, 4);
            for (int i = 0; i < 16; ++i) td.Pixels[i] = (unsigned char)(i * 16);
            td.SetStatus(ImTextureStatus_WantCreate);
            okay::imguirender::UpdateTexture(*p, &td);
            CHECK(td.Status == ImTextureStatus_OK && td.TexID != ImTextureID_Invalid);
            ui2d::Tex t = (ui2d::Tex)(std::uintptr_t)td.TexID;
            int w = 0, h = 0; p->QueryTexture(t, &w, &h);
            CHECK(w == 4 && h == 4);
            // draw it 1:1 and read a texel back: white with alpha i * 16 over black
            p->SetBlendMode(plat::BlendMode::None); p->SetDrawColor(0, 0, 0, 255); p->Clear();
            p->SetBlendMode(plat::BlendMode::Blend);
            ui2d::Rect dst{0, 0, 4, 4};
            p->Copy(t, nullptr, &dst);
            Img im = Read(*p);
            CHECK(Is(im, 3, 3, 240, 240, 240, 2) && Is(im, 0, 0, 0, 0, 0, 0) && Is(im, 3, 1, 112, 112, 112, 2));
            // an update of one changed rectangle
            td.Pixels[5] = 255;   // texel (1, 1)
            td.UpdateRect = {1, 1, 1, 1}; td.Updates.push_back(td.UpdateRect);
            td.SetStatus(ImTextureStatus_WantUpdates);
            okay::imguirender::UpdateTexture(*p, &td);
            CHECK(td.Status == ImTextureStatus_OK);
            p->SetDrawColor(0, 0, 0, 255); p->SetBlendMode(plat::BlendMode::None); p->Clear(); p->SetBlendMode(plat::BlendMode::Blend);
            p->Copy(t, nullptr, &dst);
            CHECK(Is(Read(*p), 1, 1, 255, 255, 255, 1));
            // WantDestroy waits until ImGui says it is unused, then frees and reports Destroyed
            td.WantDestroyNextFrame = true;   // (ImGui sets this when it destroys a texture for good; otherwise SetStatus(Destroyed) asks for a re-create)
            td.SetStatus(ImTextureStatus_WantDestroy);
            td.UnusedFrames = 0;
            okay::imguirender::UpdateTexture(*p, &td);
            CHECK(td.Status == ImTextureStatus_WantDestroy && td.TexID != ImTextureID_Invalid);
            td.UnusedFrames = 2;
            okay::imguirender::UpdateTexture(*p, &td);
            CHECK(td.Status == ImTextureStatus_Destroyed && td.TexID == ImTextureID_Invalid);
        }
    }
    std::printf("%d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
