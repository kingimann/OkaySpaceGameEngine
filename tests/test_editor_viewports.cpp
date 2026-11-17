// Dear ImGui's multi-window panels ("viewports") on our own code (docs/sdl_removal_plan.md, stage E4), without the editor: this
// program links only the shared ImGui glue and renderer (shared/ImGuiOwnPlatform.*, shared/ImGuiOwnRenderer.*), Dear ImGui,
// okay::platform and the engine library (for the OpenGL painter). Under Xvfb (exit 77 = skipped when there is no display).
//
//   test_editor_viewports [own|sdl2]     (default: the build's default backend)
//
// One main window with our GL painter, ImGui with ImGuiConfigFlags_ViewportsEnable, the glue's and the renderer's viewport side on.
//   1. Set-up: the backend flags, the monitor list against Backend::Displays(), the main viewport tied to the main window.
//   2. A panel placed outside the main window becomes its own OS window (Platform_CreateWindow): one panel window, at the place and
//      size ImGui asked, shown, its title; ImGui's viewport and the window agree.
//   3. Rendering (Renderer_RenderWindow / SwapBuffers with the MAIN window's context): the panel window's own pixels hold the
//      panel's coloured box and a texture made on the main window (shared context), at the right places; the main window keeps its
//      own picture; afterwards the main window's context draws the main window again.
//   4. Events of the panel window reach ImGui with desktop coordinates: a real pointer move from the X server (a warp into the panel
//      window), then injected clicks (a button in the panel), typing into a text field in the panel ("crate", then Backspace),
//      a drag of the panel's title bar (the window follows), the OS asking to resize and to close.
//   5. ImGui moves and sizes the panel (SetNextWindowPos / Size): the OS window follows.
//   6. The panel moved back over the main window merges into it: the panel window is destroyed.
//   7. Shut-down: every panel window goes, the hooks are removed.
// With OKAY_XVFB_SCREENS set to two screens (the _2mon ctest entry), the monitor list has both and a panel on the second one opens there.
#include "test_framework.hpp"

#include "ImGuiOwnPlatform.hpp"
#include "ImGuiOwnRenderer.hpp"
#include "okay/Platform/Painter.hpp"
#include "okay/Platform/Platform.hpp"

#include "imgui.h"

#if defined(__linux__) && __has_include(<X11/Xlib.h>)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#undef None      // X11's macros: plat::BlendMode::None and friends below
#undef Bool
#undef Status
#undef Success
#define OKAY_TEST_READ_X 1
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace plat = okay::platform;
namespace io = okay::imguiown;

namespace {

const int kMainX = 100, kMainY = 100, kMainW = 320, kMainH = 240;
const std::uint8_t kBoxR = 200, kBoxG = 40, kBoxB = 160;    // the panel's coloured box
const std::uint8_t kTexR = 20, kTexG = 180, kTexB = 60;     // a texture made on the main window, shown in the panel

struct Ctx {
    plat::Backend* b = nullptr;
    std::unique_ptr<plat::Window> main;
    std::unique_ptr<plat::Painter> painter;
    std::unique_ptr<io::ImGuiPlatformGlue> glue;
    std::unique_ptr<io::ImGuiPainterRenderer> ren;
    okay::ui2d::Tex tex = nullptr;
    std::vector<plat::Event> inject;
    // what the panel's UI saw in the last frame
    ImVec2 buttonMin, buttonMax, inputMin, inputMax, titleMin, winPos, winSize;
    bool hovered = false;
    int hits = 0;
    char search[64] = "";
    bool open = true;
};

ImGuiViewport* PanelViewport() {
    for (ImGuiViewport* vp : ImGui::GetPlatformIO().Viewports)
        if (vp != ImGui::GetMainViewport()) return vp;
    return nullptr;
}

// The panel: a coloured box, the main window's texture as an image, a button and a search field.
void PanelUi(Ctx& c, ImVec2 pos, ImVec2 size, ImGuiCond cond) {
    if (!c.open) return;
    ImGui::SetNextWindowPos(pos, cond);
    ImGui::SetNextWindowSize(size, cond);
    if (ImGui::Begin("Panel", &c.open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        c.winPos = ImGui::GetWindowPos();
        c.winSize = ImGui::GetWindowSize();
        c.titleMin = ImVec2(c.winPos.x + 40, c.winPos.y + 6);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 bp(c.winPos.x + 10, c.winPos.y + 40);
        dl->AddRectFilled(bp, ImVec2(bp.x + 40, bp.y + 40), IM_COL32(kBoxR, kBoxG, kBoxB, 255));
        ImGui::SetCursorScreenPos(ImVec2(c.winPos.x + 60, c.winPos.y + 40));
        ImGui::Image(ImTextureRef((ImTextureID)(std::uintptr_t)c.tex), ImVec2(40, 40));
        ImGui::SetCursorScreenPos(ImVec2(c.winPos.x + 10, c.winPos.y + 90));
        if (ImGui::Button("Hit", ImVec2(60, 24))) ++c.hits;
        c.buttonMin = ImGui::GetItemRectMin(); c.buttonMax = ImGui::GetItemRectMax();
        ImGui::SetNextItemWidth(120);
        ImGui::InputText("##search", c.search, sizeof(c.search));
        c.inputMin = ImGui::GetItemRectMin(); c.inputMax = ImGui::GetItemRectMax();
        c.hovered = ImGui::IsWindowHovered();
    }
    ImGui::End();
}

// One frame: events (the backend's, then injected ones), ImGui, the main window, then the panel windows. `readPanel` / `readMain`:
// the panel's and the main window's pixels as drawn this frame (before their swaps; with several panel windows, the last one's).
void Frame(Ctx& c, const std::function<void()>& ui, std::vector<std::uint8_t>* readPanel = nullptr, int* pw = nullptr, int* ph = nullptr,
           std::vector<std::uint8_t>* readMain = nullptr) {
    plat::Event e;
    while (c.b->PollEvent(e)) c.glue->ProcessEvent(e);
    for (const plat::Event& j : c.inject) c.glue->ProcessEvent(j);
    c.inject.clear();
    c.glue->NewFrame();
    ImGui::NewFrame();
    ui();
    ImGui::Render();
    c.painter->SetScale(1.0f, 1.0f);
    c.painter->SetDrawColor(30, 30, 34, 255);
    c.painter->Clear();
    c.ren->Render(ImGui::GetDrawData());
    if (readMain) { int w = 0, h = 0; c.painter->ReadPixels(*readMain, w, h); }
    c.painter->Present();
    ImGui::UpdatePlatformWindows();
    // Each panel window's pixels as drawn, read between its draw and its swap (the renderer's test hook).
    if (readPanel)
        c.ren->SetPanelReadback([&](ImGuiViewport*) {
            int w = 0, h = 0;
            c.painter->ReadPixels(*readPanel, w, h);
            if (pw) *pw = w;
            if (ph) *ph = h;
        });
    ImGui::RenderPlatformWindowsDefault();
    c.ren->SetPanelReadback(nullptr);
}

bool PixelIs(const std::vector<std::uint8_t>& px, int w, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const std::size_t i = ((std::size_t)y * (std::size_t)w + (std::size_t)x) * 4;
    if (i + 3 >= px.size()) return false;
    const bool ok = px[i] == r && px[i + 1] == g && px[i + 2] == b;
    if (!ok) std::printf("    pixel %d,%d is %d,%d,%d (want %d,%d,%d)\n", x, y, px[i], px[i + 1], px[i + 2], r, g, b);
    return ok;
}

plat::Event MouseEv(plat::EventType t, std::uint32_t win, int x, int y, int button = 0) {
    plat::Event e;
    e.type = t; e.windowId = win; e.x = x; e.y = y; e.button = button; e.clicks = 1;
    return e;
}

#ifdef OKAY_TEST_READ_X
// What the X server shows at (x, y) of a window (its real on-screen pixels, after the swaps), through libX11 loaded at run time
// (the engine loads it the same way). False when it cannot be read.
bool ScreenPixel(unsigned long xwin, int x, int y, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    static void* lib = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    using OpenFn = Display* (*)(const char*);
    using CloseFn = int (*)(Display*);
    using GetImageFn = XImage* (*)(Display*, Drawable, int, int, unsigned, unsigned, unsigned long, int);
    auto open = (OpenFn)dlsym(lib, "XOpenDisplay");
    auto close = (CloseFn)dlsym(lib, "XCloseDisplay");
    auto getImage = (GetImageFn)dlsym(lib, "XGetImage");
    if (!open || !close || !getImage) return false;
    Display* d = open(nullptr);
    if (!d) return false;
    XImage* img = getImage(d, (Drawable)xwin, x, y, 1, 1, ~0ul, ZPixmap);
    bool ok = false;
    if (img) {
        const unsigned long px = img->f.get_pixel(img, 0, 0);
        r = (std::uint8_t)((px & img->red_mask) >> 16); g = (std::uint8_t)((px & img->green_mask) >> 8); b = (std::uint8_t)(px & img->blue_mask);
        ok = img->red_mask == 0xff0000ul && img->green_mask == 0xff00ul && img->blue_mask == 0xfful;   // 24-bit TrueColor (Xvfb's)
        img->f.destroy_image(img);
    }
    close(d);
    return ok;
}
#endif

void Run(plat::BackendKind kind) {
    std::printf("[suite] viewports on the %s backend\n", plat::BackendName(kind));
    Ctx c;
    c.b = plat::Get(kind);
    CHECK(c.b != nullptr);
    if (!c.b) return;
    plat::InitFlags f; f.audio = false; f.gamepad = false;
    if (!c.b->Init(f)) { std::printf("  SKIP %s: %s\n", plat::BackendName(kind), c.b->LastError().c_str()); return; }
    plat::WindowDesc d;
    d.title = "viewports-main"; d.x = kMainX; d.y = kMainY; d.width = kMainW; d.height = kMainH; d.opengl = true;
    c.main = c.b->MakeWindow(d);
    CHECK(c.main != nullptr);
    if (!c.main) return;
    std::string err;
    plat::PainterDesc pd; pd.vsync = false;
    c.painter = plat::CreateGLPainter(*c.main, *c.b, pd, &err);
    CHECK(c.painter != nullptr);
    if (!c.painter) { std::printf("  no GL painter: %s\n", err.c_str()); return; }
    for (int i = 0; i < 20; ++i) { plat::Event e; while (c.b->PollEvent(e)) {} c.b->DelayMs(10); }   // mapped

    ImGui::CreateContext();
    ImGuiIO& gio = ImGui::GetIO();
    gio.IniFilename = nullptr;
    gio.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    c.glue.reset(new io::ImGuiPlatformGlue(*c.b, *c.main));
    c.ren.reset(new io::ImGuiPainterRenderer(*c.painter));
    c.glue->EnableViewports(true);
    CHECK(c.ren->EnableViewports(&io::ImGuiPlatformGlue::WindowOf));

    // 1. set-up
    CHECK((gio.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) != 0);
    CHECK((gio.BackendFlags & ImGuiBackendFlags_RendererHasViewports) != 0);
    CHECK((gio.BackendFlags & ImGuiBackendFlags_HasMouseHoveredViewport) == 0);   // not claimed: see EnableViewports
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    const std::vector<plat::DisplayInfo> ds = c.b->Displays();
    CHECK(pio.Monitors.Size >= 1);
    if (!ds.empty()) {
        CHECK(pio.Monitors.Size == (int)ds.size());
        for (int i = 0; i < pio.Monitors.Size && i < (int)ds.size(); ++i) {
            CHECK(pio.Monitors[i].MainPos.x == (float)ds[(size_t)i].bounds.x && pio.Monitors[i].MainPos.y == (float)ds[(size_t)i].bounds.y);
            CHECK(pio.Monitors[i].MainSize.x == (float)ds[(size_t)i].bounds.w && pio.Monitors[i].MainSize.y == (float)ds[(size_t)i].bounds.h);
        }
    }
    std::printf("  monitors: %d (backend lists %d)\n", pio.Monitors.Size, (int)ds.size());
    for (const plat::DisplayInfo& di : ds)
        std::printf("    display \"%s\" %d,%d %dx%d%s\n", di.name.c_str(), di.bounds.x, di.bounds.y, di.bounds.w, di.bounds.h, di.primary ? " (primary)" : "");
    CHECK(io::ImGuiPlatformGlue::WindowOf(ImGui::GetMainViewport()) == c.main.get());
    CHECK(ImGui::GetMainViewport()->PlatformHandle == (void*)(std::uintptr_t)c.main->Id());

    // the texture made on the main window (a thumbnail, the 3D view): shown in the panel through the shared context
    std::vector<std::uint8_t> solid(16 * 16 * 4);
    for (std::size_t i = 0; i < solid.size(); i += 4) { solid[i] = kTexR; solid[i + 1] = kTexG; solid[i + 2] = kTexB; solid[i + 3] = 255; }
    plat::TextureDesc td; td.width = 16; td.height = 16; td.blend = plat::BlendMode::None;
    c.tex = c.painter->CreateTexture(td, solid.data());
    CHECK(c.tex != nullptr);

    // 2. a panel outside the main window: its own OS window
    const ImVec2 p0(500, 150), s0(200, 150);
    for (int i = 0; i < 4; ++i) Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    CHECK(gio.ConfigFlags & ImGuiConfigFlags_ViewportsEnable);   // ImGui kept it on (both sides said yes)
    CHECK(c.glue->PanelWindowCount() == 1);
    CHECK(c.glue->PanelWindowsMade() == 1);
    ImGuiViewport* vp = PanelViewport();
    CHECK(vp != nullptr);
    plat::Window* pw = vp ? io::ImGuiPlatformGlue::WindowOf(vp) : nullptr;
    CHECK(pw != nullptr);
    if (!vp || !pw) return;
    int x = 0, y = 0, w = 0, h = 0;
    pw->GetPosition(x, y); pw->GetSize(w, h);
    std::printf("  panel window %u at %d,%d size %dx%d (viewport %.0f,%.0f %.0fx%.0f)\n", pw->Id(), x, y, w, h, vp->Pos.x, vp->Pos.y,
                vp->Size.x, vp->Size.y);
    CHECK(x == 500 && y == 150);
    CHECK(w == 200 && h == 150);
    CHECK(pw->IsVisible());
    CHECK(vp->PlatformHandle == (void*)(std::uintptr_t)pw->Id());
    CHECK(pw->Id() != c.main->Id());

    // 3. rendering with the main window's context
    std::vector<std::uint8_t> pp, mp;
    int ppw = 0, pph = 0;
    const int before = c.ren->PanelsDrawn();
    Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); }, &pp, &ppw, &pph, &mp);
    CHECK(c.ren->PanelsDrawn() == before + 1);
    CHECK(c.ren->PanelsFailed() == 0);
    CHECK(ppw == 200 && pph == 150);   // the panel window's drawable, not the main window's
    CHECK(PixelIs(pp, ppw, 30, 60, kBoxR, kBoxG, kBoxB));      // the box at (10,40)+(20,20)
    CHECK(PixelIs(pp, ppw, 80, 60, kTexR, kTexG, kTexB));      // the main window's texture at (60,40)+(20,20)
    CHECK(PixelIs(mp, kMainW, kMainW / 2, kMainH / 2, 30, 30, 34));   // the main window: only its clear colour, no panel in it
    // the main window draws with its own drawable again
    int ow = 0, oh = 0;
    c.painter->OutputSize(&ow, &oh);
    CHECK(ow == kMainW && oh == kMainH);
    if (const char* dir = std::getenv("OKAY_TEST_SHOT_DIR")) {   // the panel's picture, for a person to look at
        const std::string path = std::string(dir) + "/editor_viewports-" + plat::BackendName(kind) + "-panel.ppm";
        if (FILE* fp = std::fopen(path.c_str(), "wb")) {
            std::fprintf(fp, "P6\n%d %d\n255\n", ppw, pph);
            for (std::size_t i = 0; i + 3 < pp.size(); i += 4) std::fwrite(&pp[i], 1, 3, fp);
            std::fclose(fp);
        }
    }

    // 4. events of the panel window
    // a real pointer move from the display server: warp into the panel window
    c.b->WarpMouse(pw, 30, 60);
    for (int i = 0; i < 6; ++i) { c.b->DelayMs(15); Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); }); }
    std::printf("  after the warp: ImGui mouse %.0f,%.0f (want 530,210), panel hovered %d\n", gio.MousePos.x, gio.MousePos.y, (int)c.hovered);
    CHECK(gio.MousePos.x == 530.0f && gio.MousePos.y == 210.0f);
    CHECK(c.hovered);
    // a click on the panel's button (window-relative positions, no desktop position: the glue adds the window's corner)
    const int bx = (int)((c.buttonMin.x + c.buttonMax.x) * 0.5f) - 500, by = (int)((c.buttonMin.y + c.buttonMax.y) * 0.5f) - 150;
    c.inject.push_back(MouseEv(plat::EventType::MouseMove, pw->Id(), bx, by));
    Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    c.inject.push_back(MouseEv(plat::EventType::MouseButtonDown, pw->Id(), bx, by, plat::MouseLeft));
    Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    c.inject.push_back(MouseEv(plat::EventType::MouseButtonUp, pw->Id(), bx, by, plat::MouseLeft));
    Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    CHECK(c.hits == 1);
    // typing into the panel's search field
    const int ix = (int)((c.inputMin.x + c.inputMax.x) * 0.5f) - 500, iy = (int)((c.inputMin.y + c.inputMax.y) * 0.5f) - 150;
    c.inject.push_back(MouseEv(plat::EventType::MouseMove, pw->Id(), ix, iy));
    c.inject.push_back(MouseEv(plat::EventType::MouseButtonDown, pw->Id(), ix, iy, plat::MouseLeft));
    Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    c.inject.push_back(MouseEv(plat::EventType::MouseButtonUp, pw->Id(), ix, iy, plat::MouseLeft));
    Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    { plat::Event t; t.type = plat::EventType::TextInput; t.windowId = pw->Id(); t.text = "crate"; c.inject.push_back(t); }
    Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    std::printf("  typed: \"%s\"\n", c.search);
    CHECK(std::strcmp(c.search, "crate") == 0);
    {
        plat::Event k; k.type = plat::EventType::KeyDown; k.windowId = pw->Id(); k.key = plat::Key::Backspace; c.inject.push_back(k);
        Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
        k.type = plat::EventType::KeyUp; c.inject.push_back(k);
        Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    }
    CHECK(std::strcmp(c.search, "crat") == 0);
    // an event of a window that is neither the main window nor a panel: not ImGui's
    {
        plat::Event other = MouseEv(plat::EventType::MouseMove, 0x7fff1234u, 1, 1);
        CHECK(!c.glue->ProcessEvent(other));
    }

    // a drag of the panel's title bar, with desktop positions (what the OS reports while the button is held): the window follows
    {
        // Let the panel be placed by ImGui from now on (no ImGuiCond_Always), at where it is.
        auto ui = [&] { PanelUi(c, p0, s0, ImGuiCond_Once); };
        Frame(c, ui);
        const int tx = (int)c.titleMin.x, ty = (int)c.titleMin.y;
        plat::Event m = MouseEv(plat::EventType::MouseMove, pw->Id(), tx - 500, ty - 150);
        m.hasScreenPos = true; m.screenX = tx; m.screenY = ty;
        c.inject.push_back(m);
        Frame(c, ui);
        plat::Event dn = m; dn.type = plat::EventType::MouseButtonDown; dn.button = plat::MouseLeft;
        c.inject.push_back(dn);
        Frame(c, ui);
        for (int step = 1; step <= 4; ++step) {
            plat::Event mv = m;
            mv.screenX = tx + step * 10; mv.screenY = ty + step * 8;
            mv.x = tx - 500; mv.y = ty - 150;   // the window-relative position is stale on purpose: the glue must use the desktop one
            c.inject.push_back(mv);
            Frame(c, ui);
        }
        plat::Event up = m; up.type = plat::EventType::MouseButtonUp; up.button = plat::MouseLeft;
        up.screenX = tx + 40; up.screenY = ty + 32;
        c.inject.push_back(up);
        Frame(c, ui);
        Frame(c, ui);
        pw->GetPosition(x, y);
        std::printf("  after the title-bar drag: panel window at %d,%d (want 540,182), ImGui window at %.0f,%.0f\n", x, y, c.winPos.x, c.winPos.y);
        CHECK(x == 540 && y == 182);
        CHECK(c.winPos.x == 540.0f && c.winPos.y == 182.0f);
        CHECK(c.glue->PanelWindowCount() == 1);
    }

    // 5. ImGui moves and sizes the panel: the OS window follows
    const ImVec2 p1(560, 200), s1(240, 180);
    for (int i = 0; i < 3; ++i) Frame(c, [&] { PanelUi(c, p1, s1, ImGuiCond_Always); });
    vp = PanelViewport();
    pw = vp ? io::ImGuiPlatformGlue::WindowOf(vp) : nullptr;
    CHECK(pw != nullptr);
    if (!pw) return;
    pw->GetPosition(x, y); pw->GetSize(w, h);
    std::printf("  moved and sized: panel window at %d,%d size %dx%d\n", x, y, w, h);
    CHECK(x == 560 && y == 200);
    CHECK(w == 240 && h == 180);
    CHECK(c.glue->PanelWindowsMade() == 1);   // the same window, moved: not made again
    // the OS sizes the window (a window manager's border): ImGui takes the window's size
    pw->SetSize(260, 190);
    for (int i = 0; i < 4; ++i) { c.b->DelayMs(10); Frame(c, [&] { PanelUi(c, p1, s1, ImGuiCond_Once); }); }
    std::printf("  sized by the OS: ImGui viewport %.0fx%.0f, window %.0fx%.0f\n", vp->Size.x, vp->Size.y, c.winSize.x, c.winSize.y);
    CHECK(vp->Size.x == 260.0f && vp->Size.y == 190.0f);
    CHECK(c.winSize.x == 260.0f && c.winSize.y == 190.0f);

    // 6. back over the main window: it merges into the main viewport and its OS window goes
    const ImVec2 p2(kMainX + 20, kMainY + 20), s2(160, 120);
    for (int i = 0; i < 5; ++i) Frame(c, [&] { PanelUi(c, p2, s2, ImGuiCond_Always); });
    std::printf("  over the main window: %d panel window(s), %d viewport(s)\n", c.glue->PanelWindowCount(), pio.Viewports.Size);
    CHECK(c.glue->PanelWindowCount() == 0);
    CHECK(pio.Viewports.Size == 1);
    {   // ... and it is drawn in the main window now
        std::vector<std::uint8_t> mp2;
        Frame(c, [&] { PanelUi(c, p2, s2, ImGuiCond_Always); }, nullptr, nullptr, nullptr, &mp2);
        CHECK(PixelIs(mp2, kMainW, 20 + 30, 20 + 60, kBoxR, kBoxG, kBoxB));
        CHECK(PixelIs(mp2, kMainW, 20 + 80, 20 + 60, kTexR, kTexG, kTexB));
    }

    // out again, then the OS asks to close the panel window (a window manager's close button): the panel's close flag
    for (int i = 0; i < 4; ++i) Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    CHECK(c.glue->PanelWindowCount() == 1);
    CHECK(c.glue->PanelWindowsMade() == 2);
    vp = PanelViewport();
    pw = vp ? io::ImGuiPlatformGlue::WindowOf(vp) : nullptr;
    if (pw) {
        plat::Event cl; cl.type = plat::EventType::WindowClose; cl.windowId = pw->Id(); c.inject.push_back(cl);
    }
    for (int i = 0; i < 4; ++i) Frame(c, [&] { PanelUi(c, p0, s0, ImGuiCond_Always); });
    CHECK(!c.open);
    CHECK(c.glue->PanelWindowCount() == 0);

    // with two monitors (the _2mon entry): a panel on the second one opens there
    if (ds.size() >= 2) {
        c.open = true;
        const ImVec2 p3((float)ds[1].bounds.x + 30, (float)ds[1].bounds.y + 40);
        for (int i = 0; i < 4; ++i) Frame(c, [&] { PanelUi(c, p3, s0, ImGuiCond_Always); });
        vp = PanelViewport();
        pw = vp ? io::ImGuiPlatformGlue::WindowOf(vp) : nullptr;
        CHECK(pw != nullptr);
        if (pw) {
            pw->GetPosition(x, y);
            std::printf("  on the second monitor: panel window at %d,%d (want %.0f,%.0f), display %d\n", x, y, p3.x, p3.y, pw->DisplayIndex());
            CHECK(x == (int)p3.x && y == (int)p3.y);
            // Xvfb's +xinerama puts every screen at 0,0 (they overlap), so which display holds the window cannot be told there.
            const bool apart = ds[1].bounds.x >= ds[0].bounds.x + ds[0].bounds.w || ds[1].bounds.y >= ds[0].bounds.y + ds[0].bounds.h ||
                               ds[0].bounds.x >= ds[1].bounds.x + ds[1].bounds.w || ds[0].bounds.y >= ds[1].bounds.y + ds[1].bounds.h;
            if (apart) CHECK(pw->DisplayIndex() == 1);
            else std::printf("    (the two displays overlap here: which one holds the window is not checked)\n");
        }
    }

    // 8. Two panel windows at once (in the editor: a dragged-out panel and a tooltip over it). ImGui draws every panel window first,
    //    then swaps each one: every one must be swapped, not only the last one drawn. Then the first one is sized by the OS (the X
    //    server clears a resized window): the next frame shows its panel again. Before the fix the first window was never swapped
    //    while the second existed and stayed black after a resize until the tooltip went away.
    {
        bool openA = true, openB = true;
        const std::uint8_t kBR = 30, kBG = 150, kBB = 220;   // panel B's box
        auto box = [](std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            const ImVec2 wp = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(wp.x + 10, wp.y + 40), ImVec2(wp.x + 50, wp.y + 80), IM_COL32(r, g, b, 255));
        };
        auto two = [&] {
            ImGui::SetNextWindowPos(ImVec2(500, 150), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(200, 150), ImGuiCond_Once);
            if (openA) {
                if (ImGui::Begin("Panel A", &openA, ImGuiWindowFlags_NoSavedSettings)) box(kBoxR, kBoxG, kBoxB);
                ImGui::End();
            }
            ImGui::SetNextWindowPos(ImVec2(500, 340), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(200, 150), ImGuiCond_Always);
            if (openB) {
                if (ImGui::Begin("Panel B", &openB, ImGuiWindowFlags_NoSavedSettings)) box(kBR, kBG, kBB);
                ImGui::End();
            }
        };
        for (int i = 0; i < 4; ++i) Frame(c, two);
        CHECK(c.glue->PanelWindowCount() == 2);
        const int presented = c.ren->PanelsPresented();
        Frame(c, two);
        std::printf("  two panel windows: %d swapped in one frame (want 2)\n", c.ren->PanelsPresented() - presented);
        CHECK(c.ren->PanelsPresented() == presented + 2);
        int ow2 = 0, oh2 = 0;
        c.painter->OutputSize(&ow2, &oh2);
        CHECK(ow2 == kMainW && oh2 == kMainH);   // the painter is back on the main window after the swaps
        plat::Window* wa = nullptr;
        for (ImGuiViewport* v : pio.Viewports)
            if (v != ImGui::GetMainViewport() && v->Pos.x == 500.0f && v->Pos.y == 150.0f) wa = io::ImGuiPlatformGlue::WindowOf(v);
        CHECK(wa != nullptr);
#ifdef OKAY_TEST_READ_X
        if (wa && kind == plat::BackendKind::Own) {   // the own backend's NativeHandle is the X window
            const unsigned long xa = (unsigned long)(std::uintptr_t)wa->NativeHandle();
            // the frame's drawing reaches the server when the backend next talks to it (an event poll sends what is queued)
            auto settle = [&] { c.b->DelayMs(30); plat::Event e; while (c.b->PollEvent(e)) c.glue->ProcessEvent(e); c.b->DelayMs(30); };
            settle();
            std::uint8_t r = 0, g = 0, b = 0;
            if (ScreenPixel(xa, 30, 60, r, g, b)) {
                std::printf("  panel A on screen: %d,%d,%d\n", r, g, b);
                CHECK(r == kBoxR && g == kBoxG && b == kBoxB);
                wa->SetSize(260, 170);   // sized from outside: the server clears it
                for (int i = 0; i < 3; ++i) { c.b->DelayMs(10); Frame(c, two); }
                settle();
                CHECK(ScreenPixel(xa, 30, 60, r, g, b));
                std::printf("  panel A on screen after an outside resize: %d,%d,%d\n", r, g, b);
                CHECK(r == kBoxR && g == kBoxG && b == kBoxB);
                CHECK(ScreenPixel(xa, 240, 150, r, g, b));   // in the part the resize added: the panel's background, not black
                std::printf("  panel A's new corner: %d,%d,%d\n", r, g, b);
                CHECK(r + g + b > 0);
            } else std::printf("  (the X server's pixels could not be read: the on-screen check is skipped)\n");
        }
#endif
        openA = openB = false;
        for (int i = 0; i < 6; ++i) Frame(c, two);
        CHECK(c.glue->PanelWindowCount() == 0);
    }

    // 7. shut-down
    c.ren.reset();
    c.glue.reset();
    CHECK((gio.BackendFlags & (ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports)) == 0);
    CHECK(pio.Platform_CreateWindow == nullptr && pio.Renderer_RenderWindow == nullptr);
    CHECK(ImGui::GetMainViewport()->PlatformUserData == nullptr);
    ImGui::DestroyContext();
    c.painter->DestroyTexture(c.tex);
    c.painter.reset();
    c.main.reset();
}

} // namespace

int main(int argc, char** argv) {
    if (!std::getenv("DISPLAY")) { std::printf("SKIP: no display (run under Xvfb)\n"); return 77; }
    plat::BackendKind kind = plat::DefaultKind();
    if (argc > 1 && !plat::ParseBackendName(argv[1], kind)) { std::printf("usage: %s [own|sdl2]\n", argv[0]); return 2; }
    if (!plat::IsCompiledIn(kind)) { std::printf("SKIP: backend %s not compiled in\n", plat::BackendName(kind)); return 77; }
    Run(kind);
    TEST_MAIN_RESULT();
}
