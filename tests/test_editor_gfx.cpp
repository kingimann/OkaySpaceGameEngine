// The editor's drawing seam (editor/EditorGfx.*, OKAY_EDITOR_RENDERER, docs/sdl_removal_plan.md stage E2), without the editor: this
// program links only the drawing seam, the platform seam (editor/EditorPlatform*), the shared ImGui renderer and glue
// (shared/ImGuiOwnRenderer.*, shared/ImGuiOwnPlatform.*), Dear ImGui with its SDL backends, okay::platform and SDL2. Under Xvfb
// (exit 77 = skipped when there is no display).
//
//   1. The switch: OKAY_EDITOR_RENDERER values; `own` only with the own platform; the fallback to `sdl` and its log line.
//   2. The same editor-like frame drawn through each side (sdl: SDL_Renderer + ImGui's SDL_Renderer backend; own: our GL painter
//      on the main window's own GL context + our ImGui renderer), on the platform seam each side goes with:
//        * the views' hidden GL context renders a picture into a framebuffer object and reads it back (what the 3D views do),
//          then the UI context is current again and the picture goes into a streaming, linearly sampled texture
//          (Render3DTexture);
//        * a thumbnail from RGBA pixels (nearest, blended: GetThumb), a texture with blending off (the first-person hands
//          preview), a texture destroyed and made again at a new size (the sprite editor), the sizes read back (QueryTexture);
//        * ImGui draw lists: filled shapes with alpha, a clip rectangle, text (ImGui's font atlas through the renderer's
//          texture requests), and the four textures as images;
//        * the frame: scale, clear, ImGui, read-back (the screenshot path), present.
//      Each picture is checked at known points (exact colours), and the two pictures are compared pixel by pixel.
//   3. The cost of the copy the views make (read back from the views' context, upload into the UI's texture) at 1920 x 1080, and
//      of a whole frame, on both sides (printed; not a pass / fail check).
// With OKAY_PLATFORM_BACKEND=own (the second ctest entry) the views' GL window and context are our own X11 / GLX code.
#include "test_framework.hpp"

#include "EditorGfx.hpp"
#include "EditorPlatform.hpp"
#include "okay/Platform/Platform.hpp"

#include <SDL.h>

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace eg = okay::editorgfx;
namespace ep = okay::editorplat;
namespace plat = okay::platform;

namespace {

const int kW = 320, kH = 240;
const int kViewW = 64, kViewH = 48;

// ---- GL bits the views' side needs (numbers from the OpenGL 3.2 specification) ----
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
constexpr GLenum kGL_COLOR_BUFFER_BIT = 0x4000, kGL_RGBA = 0x1908, kGL_UNSIGNED_BYTE = 0x1401, kGL_SCISSOR_TEST = 0x0C11;
constexpr GLenum kGL_FRAMEBUFFER = 0x8D40, kGL_RENDERBUFFER = 0x8D41, kGL_COLOR_ATTACHMENT0 = 0x8CE0, kGL_RGBA8 = 0x8058;
constexpr GLenum kGL_FRAMEBUFFER_COMPLETE = 0x8CD5, kGL_PACK_ALIGNMENT = 0x0D05;
#if defined(_WIN32)
#  define TGL_API __stdcall
#else
#  define TGL_API
#endif
struct Gl {
    void(TGL_API* ClearColor)(float, float, float, float) = nullptr;
    void(TGL_API* Clear)(GLenum) = nullptr;
    void(TGL_API* Viewport)(GLint, GLint, GLint, GLint) = nullptr;
    void(TGL_API* Scissor)(GLint, GLint, GLint, GLint) = nullptr;
    void(TGL_API* Enable)(GLenum) = nullptr;
    void(TGL_API* Disable)(GLenum) = nullptr;
    void(TGL_API* PixelStorei)(GLenum, GLint) = nullptr;
    void(TGL_API* ReadPixels)(GLint, GLint, GLint, GLint, GLenum, GLenum, void*) = nullptr;
    void(TGL_API* Finish)() = nullptr;
    void(TGL_API* GenFramebuffers)(GLint, GLuint*) = nullptr;
    void(TGL_API* BindFramebuffer)(GLenum, GLuint) = nullptr;
    void(TGL_API* DeleteFramebuffers)(GLint, const GLuint*) = nullptr;
    void(TGL_API* GenRenderbuffers)(GLint, GLuint*) = nullptr;
    void(TGL_API* BindRenderbuffer)(GLenum, GLuint) = nullptr;
    void(TGL_API* DeleteRenderbuffers)(GLint, const GLuint*) = nullptr;
    void(TGL_API* RenderbufferStorage)(GLenum, GLenum, GLint, GLint) = nullptr;
    void(TGL_API* FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = nullptr;
    GLenum(TGL_API* CheckFramebufferStatus)(GLenum) = nullptr;
    template <class F> bool Get(ep::EditorPlatform& p, F& f, const char* n) { f = reinterpret_cast<F>(p.GLProcAddress(n)); return f != nullptr; }
    bool LoadAll(ep::EditorPlatform& p) {
        return Get(p, ClearColor, "glClearColor") && Get(p, Clear, "glClear") && Get(p, Viewport, "glViewport") &&
               Get(p, Scissor, "glScissor") && Get(p, Enable, "glEnable") && Get(p, Disable, "glDisable") &&
               Get(p, PixelStorei, "glPixelStorei") && Get(p, ReadPixels, "glReadPixels") && Get(p, Finish, "glFinish") &&
               Get(p, GenFramebuffers, "glGenFramebuffers") && Get(p, BindFramebuffer, "glBindFramebuffer") &&
               Get(p, DeleteFramebuffers, "glDeleteFramebuffers") && Get(p, GenRenderbuffers, "glGenRenderbuffers") &&
               Get(p, BindRenderbuffer, "glBindRenderbuffer") && Get(p, DeleteRenderbuffers, "glDeleteRenderbuffers") &&
               Get(p, RenderbufferStorage, "glRenderbufferStorage") &&
               Get(p, FramebufferRenderbuffer, "glFramebufferRenderbuffer") &&
               Get(p, CheckFramebufferStatus, "glCheckFramebufferStatus");
    }
};

// The views' side: an offscreen framebuffer of w x h in the views' context, a picture drawn into it with clears (the left half
// blue, the right half orange, a green square in the top-left quarter), read back top row first, as RGBA8.
struct ViewTarget {
    Gl gl;
    GLuint fbo = 0, rb = 0;
    int w = 0, h = 0;
    bool Make(ep::EditorPlatform& p, int w_, int h_) {
        if (!gl.LoadAll(p)) return false;
        w = w_; h = h_;
        gl.GenFramebuffers(1, &fbo);
        gl.BindFramebuffer(kGL_FRAMEBUFFER, fbo);
        gl.GenRenderbuffers(1, &rb);
        gl.BindRenderbuffer(kGL_RENDERBUFFER, rb);
        gl.RenderbufferStorage(kGL_RENDERBUFFER, kGL_RGBA8, w, h);
        gl.FramebufferRenderbuffer(kGL_FRAMEBUFFER, kGL_COLOR_ATTACHMENT0, kGL_RENDERBUFFER, rb);
        const bool ok = gl.CheckFramebufferStatus(kGL_FRAMEBUFFER) == kGL_FRAMEBUFFER_COMPLETE;
        gl.BindFramebuffer(kGL_FRAMEBUFFER, 0);
        return ok;
    }
    void Draw() {
        gl.BindFramebuffer(kGL_FRAMEBUFFER, fbo);
        gl.Viewport(0, 0, w, h);
        gl.Disable(kGL_SCISSOR_TEST);
        gl.ClearColor(0.2f, 0.4f, 1.0f, 1.0f);   // 51, 102, 255
        gl.Clear(kGL_COLOR_BUFFER_BIT);
        gl.Enable(kGL_SCISSOR_TEST);
        gl.Scissor(w / 2, 0, w - w / 2, h);
        gl.ClearColor(1.0f, 0.6f, 0.0f, 1.0f);   // 255, 153, 0
        gl.Clear(kGL_COLOR_BUFFER_BIT);
        gl.Scissor(0, h / 2, w / 4, h - h / 2);   // GL rows count from the bottom: the top-left quarter of the picture
        gl.ClearColor(0.0f, 0.8f, 0.2f, 1.0f);   // 0, 204, 51
        gl.Clear(kGL_COLOR_BUFFER_BIT);
        gl.Disable(kGL_SCISSOR_TEST);
    }
    void Read(std::vector<std::uint8_t>& out) {
        std::vector<std::uint8_t> up((std::size_t)w * h * 4);
        gl.PixelStorei(kGL_PACK_ALIGNMENT, 1);
        gl.ReadPixels(0, 0, w, h, kGL_RGBA, kGL_UNSIGNED_BYTE, up.data());
        out.resize(up.size());
        const std::size_t row = (std::size_t)w * 4;
        for (int y = 0; y < h; ++y) std::memcpy(&out[(std::size_t)y * row], &up[(std::size_t)(h - 1 - y) * row], row);
        gl.BindFramebuffer(kGL_FRAMEBUFFER, 0);
    }
    void Free() {
        if (rb) gl.DeleteRenderbuffers(1, &rb);
        if (fbo) gl.DeleteFramebuffers(1, &fbo);
        rb = fbo = 0;
    }
};

std::vector<std::uint8_t> Checker(int n) {
    std::vector<std::uint8_t> px((std::size_t)n * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            std::uint8_t* p = &px[((std::size_t)y * n + x) * 4];
            const bool on = ((x / 4) + (y / 4)) % 2 == 0;
            p[0] = on ? 240 : (std::uint8_t)(x * 8); p[1] = on ? 240 : 30; p[2] = on ? 60 : (std::uint8_t)(y * 8);
            p[3] = 255;
        }
    return px;
}

std::vector<std::uint8_t> Solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    std::vector<std::uint8_t> px((std::size_t)w * h * 4);
    for (int i = 0; i < w * h; ++i) { px[(std::size_t)i * 4] = r; px[(std::size_t)i * 4 + 1] = g; px[(std::size_t)i * 4 + 2] = b; px[(std::size_t)i * 4 + 3] = a; }
    return px;
}

double MsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
double Median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
}

struct Shot {
    bool ok = false;
    std::vector<std::uint8_t> px;
    std::string name;
};

const std::uint8_t* At(const Shot& s, int x, int y) { return &s.px[((std::size_t)y * kW + x) * 4]; }

// Exact colour at (x, y) within `tol` per channel (GPU rounding).
bool ColourAt(const Shot& s, int x, int y, int r, int g, int b, int tol = 1) {
    const std::uint8_t* p = At(s, x, y);
    const bool ok = std::abs(p[0] - r) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[2] - b) <= tol;
    if (!ok) std::fprintf(stderr, "  %s: pixel %d,%d is %d,%d,%d, expected %d,%d,%d\n", s.name.c_str(), x, y, p[0], p[1], p[2], r, g, b);
    return ok;
}

// ---- 1. the switch ----
void TestSwitch() {
    RUN_SUITE("OKAY_EDITOR_RENDERER: the switch");
    CHECK(eg::ParseRenderer(nullptr) == eg::RendererKind::Sdl);
    CHECK(eg::ParseRenderer("") == eg::RendererKind::Sdl);
    CHECK(eg::ParseRenderer("sdl") == eg::RendererKind::Sdl);
    CHECK(eg::ParseRenderer("OWN") == eg::RendererKind::Sdl);
    CHECK(eg::ParseRenderer("opengl") == eg::RendererKind::Sdl);
    CHECK(eg::ParseRenderer("own") == eg::RendererKind::Own);
    CHECK(std::strcmp(eg::RendererName(eg::RendererKind::Own), "own") == 0);
    CHECK(std::strcmp(eg::RendererName(eg::RendererKind::Sdl), "sdl") == 0);
    CHECK(eg::OwnRendererAllowed(eg::RendererKind::Own, true));
    CHECK(!eg::OwnRendererAllowed(eg::RendererKind::Own, false));   // own renderer needs the own platform
    CHECK(!eg::OwnRendererAllowed(eg::RendererKind::Sdl, true));
    const plat::TextureDesc d = eg::TexDesc(8, 4, true, true, false);
    CHECK(d.width == 8 && d.height == 4 && d.scale == plat::ScaleMode::Linear && d.streaming && d.blend == plat::BlendMode::None);
    const plat::TextureDesc t = eg::TexDesc(2, 3, false);
    CHECK(t.scale == plat::ScaleMode::Nearest && !t.streaming && t.blend == plat::BlendMode::Blend);
}


// OKAY_EDITOR_RENDERER read by CreateEditorGfxFromEnv: own needs the own platform; anything else is sdl.
void TestFromEnv() {
    RUN_SUITE("CreateEditorGfxFromEnv");
    for (int ownPlat = 0; ownPlat < 2; ++ownPlat)
        for (const char* v : {"own", "sdl", (const char*)nullptr}) {
            std::unique_ptr<ep::EditorPlatform> p = ownPlat ? ep::CreateOwnEditorPlatform() : ep::CreateSdlEditorPlatform();
            CHECK(p != nullptr);
            if (!p) continue;
            const bool wantOwn = v && std::strcmp(v, "own") == 0;
            void* win = p->CreateMainWindow("editor-gfx-env", 160, 120, ownPlat && wantOwn);
            CHECK(win != nullptr);
            if (!win) continue;
            if (v) setenv("OKAY_EDITOR_RENDERER", v, 1); else unsetenv("OKAY_EDITOR_RENDERER");
            std::string note, err;
            std::unique_ptr<eg::EditorGfx> g = eg::CreateEditorGfxFromEnv(win, p->MainWindow(), p->MainWindowBackend(), ownPlat != 0,
                                                                          false, &note, &err);
            CHECK(g != nullptr);
            std::printf("  platform %s, OKAY_EDITOR_RENDERER=%s: %s\n", ownPlat ? "own" : "sdl", v ? v : "(unset)", note.c_str());
            if (g) {
                const bool own = ownPlat && wantOwn;
                CHECK(g->Kind() == (own ? eg::RendererKind::Own : eg::RendererKind::Sdl));
                CHECK((g->SdlRenderer() == nullptr) == own);
                CHECK(note.rfind("editor: renderer ", 0) == 0);
                CHECK((note.find("could not start") != std::string::npos) == (wantOwn && !own));
                if (wantOwn && !own) CHECK(note.find("needs OKAY_EDITOR_PLATFORM=own") != std::string::npos);
            }
            g.reset();
            p->DestroyMainWindow();
        }
    unsetenv("OKAY_EDITOR_RENDERER");
}

struct Timing { double readMs = 0, uploadMs = 0, frameMs = 0; bool ok = false; };

void PumpSdl(int n) {
    for (int i = 0; i < n; ++i) { SDL_Event e; while (SDL_PollEvent(&e)) {} SDL_Delay(5); }
}

// The scene, in ImGui's coordinates (one framebuffer pixel each).
void DrawScene(ImDrawList* dl, okay::ui2d::Tex thumb, okay::ui2d::Tex view, okay::ui2d::Tex opaque, okay::ui2d::Tex remade) {
    auto id = [](okay::ui2d::Tex t) { return (ImTextureID)(std::uintptr_t)t; };
    dl->AddRectFilled(ImVec2(10, 10), ImVec2(90, 60), IM_COL32(200, 40, 40, 255));
    dl->AddRectFilled(ImVec2(60, 40), ImVec2(140, 90), IM_COL32(40, 200, 60, 128));
    dl->PushClipRect(ImVec2(150, 10), ImVec2(200, 50), false);
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(320, 240), IM_COL32(220, 180, 40, 255));
    dl->PopClipRect();
    dl->AddText(ImVec2(10, 100), IM_COL32(255, 255, 255, 255), "Scene  Game  Inspector 0123");
    dl->AddCircleFilled(ImVec2(270, 70), 30.0f, IM_COL32(90, 140, 220, 200));
    dl->AddImage(id(thumb), ImVec2(10, 130), ImVec2(74, 194));          // 32 x 32 at 2x, nearest
    dl->AddImage(id(view), ImVec2(90, 130), ImVec2(218, 226));          // 64 x 48 at 2x, linear
    dl->AddImage(id(opaque), ImVec2(230, 130), ImVec2(262, 162));       // blending off: alpha 0 texels still show
    dl->AddImage(id(remade), ImVec2(270, 130), ImVec2(306, 166));       // 12 x 12 at 3x
}

Shot RunSide(ep::PlatformKind kind, Timing& tm) {
    Shot shot;
    shot.name = kind == ep::PlatformKind::Own ? "own" : "sdl";
    RUN_SUITE(std::string("EditorGfx ") + shot.name + " (views' GL on " + plat::BackendName(plat::DefaultKind()) + ")");
    std::unique_ptr<ep::EditorPlatform> p = kind == ep::PlatformKind::Own ? ep::CreateOwnEditorPlatform() : ep::CreateSdlEditorPlatform();
    CHECK(p != nullptr);
    if (!p) return shot;
    const bool own = kind == ep::PlatformKind::Own;
    void* win = p->CreateMainWindow("editor-gfx-test", kW, kH, own);
    CHECK(win != nullptr);
    if (!win) { std::fprintf(stderr, "  main window: %s\n", p->LastError().c_str()); return shot; }
    CHECK(p->MainWindow() != nullptr && p->MainWindowBackend() != nullptr);
    std::string err;
    std::unique_ptr<eg::EditorGfx> g = own ? eg::CreateOwnEditorGfx(*p->MainWindow(), *p->MainWindowBackend(), false, &err)
                                           : eg::CreateSdlEditorGfx(win, false, &err);
    CHECK(g != nullptr);
    if (!g) { std::fprintf(stderr, "  gfx: %s\n", err.c_str()); p->DestroyMainWindow(); return shot; }
    std::printf("  %s; driver \"%s\"\n", g->Describe().c_str(), g->DriverName().c_str());
    CHECK(g->Kind() == (own ? eg::RendererKind::Own : eg::RendererKind::Sdl));
    CHECK((g->SdlRenderer() == nullptr) == own);
    CHECK(g->Painter().Kind() == (own ? plat::PainterKind::OpenGL : plat::PainterKind::Sdl2));
    p->MainWindow()->Show();
    PumpSdl(10);
    int ow = 0, oh = 0;
    g->OutputSize(ow, oh);
    CHECK(ow == kW && oh == kH);

    // ---- the views' context: a picture rendered off screen and read back ----
    ViewTarget vt;
    bool glOk = p->CreateViewportGL();
    if (!glOk) std::fprintf(stderr, "  viewport GL: %s\n", p->LastError().c_str());
    CHECK(glOk);
    std::vector<std::uint8_t> viewPx;
    if (glOk) {
        CHECK(p->BeginViewportGL());
        glOk = vt.Make(*p, kViewW, kViewH);
        CHECK(glOk);
        if (glOk) { vt.Draw(); vt.Read(viewPx); }
        p->EndViewportGL();
    }
    if (viewPx.empty()) viewPx = Solid(kViewW, kViewH, 255, 0, 255, 255);
    CHECK(viewPx[0] == 0 && viewPx[1] == 204 && viewPx[2] == 51);   // top-left: the green quarter

    // ---- textures, as the editor makes them ----
    const std::vector<std::uint8_t> chk = Checker(32);
    okay::ui2d::Tex thumb = g->CreateTexture(eg::TexDesc(32, 32, false), chk.data());                  // GetThumb
    okay::ui2d::Tex view = g->CreateTexture(eg::TexDesc(kViewW, kViewH, true, true), nullptr);          // Render3DTexture
    const std::vector<std::uint8_t> clearRed = Solid(16, 16, 200, 50, 50, 0);
    okay::ui2d::Tex opaque = g->CreateTexture(eg::TexDesc(16, 16, false, false, false), clearRed.data()); // the hands preview
    const std::vector<std::uint8_t> small = Solid(8, 8, 255, 0, 0, 255), cyan = Solid(12, 12, 20, 220, 220, 255);
    okay::ui2d::Tex remade = g->CreateTexture(eg::TexDesc(8, 8, false), small.data());                  // the sprite editor
    CHECK(thumb && view && opaque && remade);
    g->DestroyTexture(remade);
    remade = g->CreateTexture(eg::TexDesc(12, 12, false), nullptr);
    CHECK(remade != nullptr);
    CHECK(g->UpdateTexture(remade, cyan.data(), 12 * 4));
    int qw = 0, qh = 0;
    g->QueryTexture(remade, qw, qh);
    CHECK(qw == 12 && qh == 12);
    g->QueryTexture(view, qw, qh);
    CHECK(qw == kViewW && qh == kViewH);
    g->SetTextureLinear(view, true);

    // ---- ImGui through the seam ----
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    CHECK(g->InitImGui());
    CHECK((io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0);
    std::printf("  ImGui renderer: %s\n", io.BackendRendererName ? io.BackendRendererName : "?");
    auto frame = [&](bool read) {
        if (glOk) {   // the views render each frame in their own context, then the UI's context draws
            if (p->BeginViewportGL()) { vt.Draw(); vt.Read(viewPx); }
            p->EndViewportGL();
        }
        CHECK(g->UpdateTexture(view, viewPx.data(), kViewW * 4));
        g->NewFrame();
        io.DisplaySize = ImVec2((float)kW, (float)kH);
        io.DisplayFramebufferScale = ImVec2(1, 1);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        DrawScene(ImGui::GetForegroundDrawList(), thumb, view, opaque, remade);
        ImGui::Render();
        g->BeginFrame(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y, 30, 30, 34);
        g->RenderImGui(ImGui::GetDrawData());
        if (read) {
            int rw = 0, rh = 0;
            shot.ok = g->ReadPixels(shot.px, rw, rh) && rw == kW && rh == kH;
            CHECK(shot.ok);
        }
        g->Present();
    };
    for (int f = 0; f < 3; ++f) frame(f == 2);
    if (ImDrawData* dd = ImGui::GetDrawData())
        if (dd->Textures)
            for (ImTextureData* t : *dd->Textures) CHECK(t->Status == ImTextureStatus_OK && t->TexID != ImTextureID_Invalid);

    // ---- the picture at known points ----
    if (shot.ok) {
        auto over = [](int s, int a, int d) { return (s * a + d * (255 - a) + 127) / 255; };
        CHECK(ColourAt(shot, 3, 3, 30, 30, 34));                                    // the clear colour
        CHECK(ColourAt(shot, 30, 30, 200, 40, 40));                                 // a solid rectangle
        CHECK(ColourAt(shot, 120, 80, over(40, 128, 30), over(200, 128, 30), over(60, 128, 34), 2));   // alpha over the clear colour
        CHECK(ColourAt(shot, 175, 30, 220, 180, 40));                               // inside the clip rectangle
        CHECK(ColourAt(shot, 205, 30, 30, 30, 34));                                 // just outside it
        CHECK(ColourAt(shot, 10 + 1, 130 + 1, 240, 240, 60));                       // thumbnail texel 0,0
        CHECK(ColourAt(shot, 10 + 2 * 5 + 1, 130 + 2 * 1 + 1, 40, 30, 8));          // thumbnail texel 5,1
        CHECK(ColourAt(shot, 10 + 2 * 31 + 1, 130 + 2 * 31 + 1, 240, 240, 60));     // thumbnail texel 31,31
        CHECK(ColourAt(shot, 90 + 2 * 5 + 1, 130 + 2 * 5 + 1, 0, 204, 51));         // view: the green quarter
        CHECK(ColourAt(shot, 90 + 2 * 10 + 1, 130 + 2 * 40 + 1, 51, 102, 255));     // view: the blue half
        CHECK(ColourAt(shot, 90 + 2 * 50 + 1, 130 + 2 * 30 + 1, 255, 153, 0));      // view: the orange half
        CHECK(ColourAt(shot, 246, 146, 200, 50, 50));                               // blending off: the colour of an alpha 0 texel
        CHECK(ColourAt(shot, 288, 148, 20, 220, 220));                              // the texture made again
        int lit = 0;                                                                // the text drew something white-ish
        for (int y = 100; y < 116; ++y) for (int x = 10; x < 200; ++x) if (At(shot, x, y)[0] > 200) ++lit;
        CHECK(lit > 50);
    }

    // ---- costs: the views' copy (read back + upload) at 1920 x 1080, and a whole frame ----
    {
        const int bw = 1920, bh = 1080, n = 15;
        ViewTarget big;
        std::vector<std::uint8_t> bigPx;
        std::vector<double> reads, ups, frames;
        bool bigOk = false;
        if (glOk && p->BeginViewportGL()) {
            bigOk = big.Make(*p, bw, bh);
            if (bigOk) {
                big.Draw(); big.Read(bigPx);
                for (int i = 0; i < n; ++i) {
                    big.Draw();
                    big.gl.Finish();
                    const auto t0 = std::chrono::steady_clock::now();
                    big.Read(bigPx);
                    reads.push_back(MsSince(t0));
                }
            }
        }
        p->EndViewportGL();
        okay::ui2d::Tex bigTex = bigOk ? g->CreateTexture(eg::TexDesc(bw, bh, true, true), nullptr) : nullptr;
        if (bigTex) {
            for (int i = 0; i < n; ++i) {
                const auto t0 = std::chrono::steady_clock::now();
                g->UpdateTexture(bigTex, bigPx.data(), bw * 4);
                // Draw it once so the upload really happens before the clock stops (a driver may defer it).
                const okay::ui2d::Rect one{0, 0, 1, 1};
                g->Painter().SetScale(1, 1);
                g->Painter().Copy(bigTex, &one, &one);
                ups.push_back(MsSince(t0));
            }
            g->DestroyTexture(bigTex);
        }
        for (int i = 0; i < n; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            frame(false);
            frames.push_back(MsSince(t0));
        }
        if (glOk && p->BeginViewportGL()) big.Free();
        p->EndViewportGL();
        tm.readMs = Median(reads); tm.uploadMs = Median(ups); tm.frameMs = Median(frames); tm.ok = bigOk;
        std::printf("  views' copy at %d x %d: read back %.3f ms, upload into the UI texture %.3f ms (medians of %d); "
                    "a whole %d x %d frame (views + ImGui + present) %.3f ms\n",
                    bw, bh, tm.readMs, tm.uploadMs, n, kW, kH, tm.frameMs);
    }

    // ---- shut-down ----
    g->ShutdownImGui();
    CHECK(io.BackendRendererName == nullptr);
    ImGui::DestroyContext(ctx);
    g->DestroyTexture(thumb); g->DestroyTexture(view); g->DestroyTexture(opaque); g->DestroyTexture(remade);
    if (glOk && p->BeginViewportGL()) vt.Free();
    p->EndViewportGL();
    p->DestroyViewportGL();
    g.reset();
    p->DestroyMainWindow();
    return shot;
}

void WritePpm(const Shot& s, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6 %d %d 255\n", kW, kH);
    for (int i = 0; i < kW * kH; ++i) std::fwrite(&s.px[(std::size_t)i * 4], 1, 3, f);
    std::fclose(f);
}

void Compare(const Shot& a, const Shot& b) {
    RUN_SUITE("sdl and own: the same picture");
    CHECK(a.ok && b.ok);
    if (!a.ok || !b.ok) return;
    const int tol = 8;
    int bad = 0, maxd = 0;
    long long total = 0;
    for (int i = 0; i < kW * kH; ++i) {
        int m = 0;
        for (int c = 0; c < 3; ++c) m = std::max(m, std::abs((int)a.px[(std::size_t)i * 4 + c] - (int)b.px[(std::size_t)i * 4 + c]));
        total += m;
        maxd = std::max(maxd, m);
        if (m > tol) ++bad;
    }
    const double frac = (double)bad / (kW * kH);
    std::printf("  pixels off by more than %d: %d of %d (%.3f%%), largest difference %d, mean %.4f\n", tol, bad, kW * kH,
                frac * 100.0, maxd, (double)total / (kW * kH));
    // Edges of shapes, text and the circle may round differently on two renderers; everything else must match.
    CHECK(frac <= 0.01);
    if (const char* dir = std::getenv("OKAY_TEST_DUMP")) {
        WritePpm(a, std::string(dir) + "/editor_gfx-" + a.name + ".ppm");
        WritePpm(b, std::string(dir) + "/editor_gfx-" + b.name + ".ppm");
    }
}

} // namespace

int main() {
    TestSwitch();
    if (!std::getenv("DISPLAY")) { std::printf("SKIP: no display (run under Xvfb)\n"); return 77; }
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        std::printf("SKIP: SDL video: %s\n", SDL_GetError());
        return 77;
    }
    TestFromEnv();
    Timing ts, to;
    const Shot s = RunSide(ep::PlatformKind::Sdl, ts);
    const Shot o = RunSide(ep::PlatformKind::Own, to);
    Compare(s, o);
    SDL_Quit();
    TEST_MAIN_RESULT();
}
