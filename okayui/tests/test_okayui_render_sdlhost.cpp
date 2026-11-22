// The host path with OkayUI's own renderer: an OkayUI frame ended with okay::pb::OkayUIEndFrame (shared/OkayUIFrame.hpp, what the launcher
// and the editor call) on a real SDL_Renderer, with OKAY_UI_OWN_RENDER=1. The frame must be drawn by OkayUI's own backend (OpenGL when SDL
// runs on "opengl" / "opengles2", Direct3D 11 when it runs on "direct3d11", the Windows default), not by SDL_RenderGeometry, and match
// the software reference within the tolerance the OpenGL backend test uses. Frames that show SDL textures as images stay on the own
// renderer too.
// Linux: SDL's "offscreen" video driver with EGL gives a GL context with no display (Mesa's llvmpipe here, a CPU rasteriser); there is no
// direct3d11 renderer, so that part says SKIPPED. Windows (or a MinGW build run under Wine): SDL's normal video driver with a hidden
// window, direct3d11 first, then opengl when SDL can make one. Where SDL cannot make a renderer its part says SKIPPED and passes, and the
// logic checks still run.
#define SDL_MAIN_HANDLED
#include "test_framework.hpp"
#include "RenderScenes.hpp"
#include "OkayUIFrame.hpp"
#include "OwnSwitches.hpp"
#include "okay/UI/OkayUIRenderSDL.hpp"
#include "okay/UI/OkayUIRenderBackends.hpp"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace UI = OkayUI;
namespace R = OkayUI::render;

namespace {

const std::uint8_t kClear[3] = { 12, 14, 20 };

void SetEnv(const char* name, const char* value) {   // value null = unset
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");                 // an empty value removes the variable on Windows
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}
void SetSwitch(bool on) { SetEnv("OKAY_UI_OWN_RENDER", on ? "1" : nullptr); }

bool IsGL(const char* driver) { return std::strcmp(driver, "opengl") == 0 || std::strcmp(driver, "opengles2") == 0; }
const char* PathName(R::HostPath p) {
    switch (p) { case R::HostPath::OwnGL: return "own OpenGL"; case R::HostPath::OwnD3D11: return "own Direct3D 11"; case R::HostPath::Sdl: return "SDL"; default: return "none"; }
}

/// The screen every frame draws: explicit widgets, an auto-layout window with text, radio buttons, a combo, a slider, a header and bars.
void Ui(SDL_Texture* image) {
    static bool cb = true; static float sl = 0.4f, vol = 60.0f; static char txt[16] = "hello"; static int radio = 1, quality = 1;
    static const char* opts[] = { "Low", "Medium", "High" };
    UI::Panel(8, 8, 404, 120);
    UI::Button(1, 20, 20, 90, 30, "Button");
    UI::Checkbox(2, 20, 62, 22, "Check", &cb);
    UI::Slider(3, 130, 24, 160, 22, &sl, 0.0f, 1.0f);
    UI::ProgressBar(130, 62, 160, 14, 0.6f);
    UI::TextField(4, 20, 92, 200, 26, txt, 16);
    UI::Begin("Hosted screen", 20, 140, 330, 230);
    UI::Text("OkayUI through its own OpenGL renderer");
    UI::RadioButton("Easy", &radio, 0); UI::SameLine(); UI::RadioButton("Hard", &radio, 1);
    UI::Combo("Quality", opts, 3, &quality);
    UI::SliderFloat("Volume", &vol, 0.0f, 100.0f);
    if (UI::CollapsingHeader("Stats", true)) { UI::Text("Health"); UI::ProgressBar(0.8f); }
    UI::End();
    if (image) UI::DrawTexture(image, 360, 20, 32, 32);
}

UI::Input InputFor(float w, float h) {
    UI::Input in;
    in.mouseX = -100; in.mouseY = -100; in.displayW = w; in.displayH = h;
    return in;
}

/// A host image the reference knows: the key (the SDL_Texture*) and its pixels.
struct RefImage { const void* key = nullptr; int w = 0, h = 0; std::vector<std::uint8_t> rgba; };

std::vector<std::uint8_t> SoftRef(const UI::DrawData& d, int w, int h, float dpi, const RefImage* img = nullptr) {
    UI::soft::Bitmap bm(w, h); bm.Clear(kClear[0], kClear[1], kClear[2]);
    auto be = R::CreateSoftBackend(bm); R::Renderer r(*be);
    if (img && img->key) r.SetImage(img->key, img->w, img->h, img->rgba.data(), false);
    R::FrameInfo fi; fi.fbWidth = w; fi.fbHeight = h; fi.dpi = dpi;
    r.Render(d, fi);
    return bm.rgba;
}

/// The whole window as RGBA8, top row first (read at scale 1, so the rectangle is the framebuffer).
bool ReadBack(SDL_Renderer* r, std::vector<std::uint8_t>& out, int& w, int& h) {
    float sx = 1, sy = 1; SDL_RenderGetScale(r, &sx, &sy);
    SDL_GetRendererOutputSize(r, &w, &h);
    out.assign((size_t)w * (size_t)h * 4, 0);
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    const int rc = SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ABGR8888, out.data(), w * 4);
    SDL_RenderSetScale(r, sx, sy);
    return rc == 0;
}

std::uint32_t Px(const std::vector<std::uint8_t>& img, int w, int x, int y) {
    const std::uint8_t* p = &img[((size_t)y * (size_t)w + (size_t)x) * 4];
    return (std::uint32_t)p[0] << 24 | (std::uint32_t)p[1] << 16 | (std::uint32_t)p[2] << 8 | p[3];
}

const char* g_driver = "";
unsigned GlError() {                // 0 off GL (the Direct3D 11 renderer has no GL context)
    if (!IsGL(g_driver)) return 0;
    auto f = (unsigned (*)())SDL_GL_GetProcAddress("glGetError");
    return f ? f() : 0;
}

/// Runs the screen for a few frames and ends the last one through the host path. Returns the window picture and the reference.
struct Shot { std::vector<std::uint8_t> got, ref; int w = 0, h = 0; R::HostFrame frame; };
Shot HostFrame(SDL_Renderer* r, float logicalW, float logicalH, float dpi, bool launcher, SDL_Texture* image = nullptr, const RefImage* ref = nullptr) {
    okayuitest::Reset();
    UI::UseLauncherLook(launcher);
    UI::Style().caretBlink = false;
    for (int i = 0; i < 2; ++i) { UI::BeginFrame(InputFor(logicalW, logicalH)); Ui(image); UI::EndFrameData(); }
    SDL_SetRenderDrawColor(r, kClear[0], kClear[1], kClear[2], 255);
    SDL_RenderClear(r);
    UI::BeginFrame(InputFor(logicalW, logicalH));
    Ui(image);
    okay::pb::OkayUIEndFrame(r);
    Shot s;
    s.frame = okay::pb::OkayUILastFrame();
    ReadBack(r, s.got, s.w, s.h);
    s.ref = SoftRef(UI::GetDrawData(), s.w, s.h, dpi, ref);
    UI::UseLauncherLook(false);
    return s;
}

void Report(const char* what, const rscenes::Diff& d) {
    char line[256];
    std::snprintf(line, sizeof line, "  %-40s %4dx%-4d max %3d mean %7.4f  >8: %lld (%.3f%%)", what, d.w, d.h, d.maxDiff, d.meanDiff, d.above,
                  d.total ? 100.0 * (double)d.above / (double)d.total : 0.0);
    std::cout << line << "\n";
}
bool WithinTolerance(const rscenes::Diff& d) {   // the OpenGL backend test's tolerance against the software reference
    return d.sameSize && d.meanDiff < 0.5 && (double)d.above / (double)d.total < 0.01;
}

void TestLogic() {
    RUN_SUITE("render sdl host: the switch and the GL flavour");
    CHECK(R::OwnRenderSwitchValue("1") && R::OwnRenderSwitchValue("on") && R::OwnRenderSwitchValue("TRUE") && R::OwnRenderSwitchValue("Yes"));
    CHECK(!R::OwnRenderSwitchValue(nullptr) && !R::OwnRenderSwitchValue("") && !R::OwnRenderSwitchValue("0") && !R::OwnRenderSwitchValue("off")
          && !R::OwnRenderSwitchValue("2") && !R::OwnRenderSwitchValue("1 "));
    SetSwitch(false); CHECK(!R::OwnRenderSwitchOn());
    SetSwitch(true);  CHECK(R::OwnRenderSwitchOn());
    SetSwitch(false);

    R::GLFlavor f = R::GLFlavor::ES30; const char* why = nullptr;
    CHECK(R::PickGLFlavor("opengl", "4.5 (Compatibility Profile) Mesa 25.2.8", &f, &why) && f == R::GLFlavor::Core33);
    CHECK(R::PickGLFlavor("opengl", "3.3.0 NVIDIA 535.1", &f, &why) && f == R::GLFlavor::Core33);
    CHECK(R::PickGLFlavor("opengl", "10.0", &f, &why) && f == R::GLFlavor::Core33);
    why = nullptr; CHECK(!R::PickGLFlavor("opengl", "3.2.0", &f, &why) && why && *why);
    CHECK(!R::PickGLFlavor("opengl", "2.1 Mesa 25", &f, &why));
    CHECK(!R::PickGLFlavor("opengl", "", &f, &why) && !R::PickGLFlavor("opengl", nullptr, &f, &why) && !R::PickGLFlavor("opengl", "junk", &f, &why));
    CHECK(R::PickGLFlavor("opengles2", "OpenGL ES 3.2 Mesa 25.2.8", &f, &why) && f == R::GLFlavor::ES30);
    CHECK(R::PickGLFlavor("opengles2", "OpenGL ES 3.0 build 1", &f, &why) && f == R::GLFlavor::ES30);
    CHECK(!R::PickGLFlavor("opengles2", "OpenGL ES 2.0 Mesa", &f, &why));
    CHECK(!R::PickGLFlavor("opengles2", "4.6", &f, &why));             // an ES renderer must say "OpenGL ES"
    why = nullptr; CHECK(!R::PickGLFlavor("direct3d11", "4.6", &f, &why) && why && std::strstr(why, "OpenGL"));
    CHECK(!R::PickGLFlavor("software", "4.6", &f, &why) && !R::PickGLFlavor(nullptr, "4.6", &f, &why));

    // which own backend draws into which SDL renderer: OpenGL into SDL's two GL renderers, Direct3D 11 into SDL's "direct3d11" (the
    // Windows default); everything else keeps the SDL path and says why
    why = nullptr; CHECK(R::HostBackendFor("opengl", &why) == R::HostPath::OwnGL);
    CHECK(R::HostBackendFor("opengles2", &why) == R::HostPath::OwnGL);
    CHECK(R::HostBackendFor("direct3d11", &why) == R::HostPath::OwnD3D11);
    why = nullptr; CHECK(R::HostBackendFor("direct3d", &why) == R::HostPath::Sdl && why && *why);     // SDL's Direct3D 9 renderer
    why = nullptr; CHECK(R::HostBackendFor("direct3d12", &why) == R::HostPath::Sdl && why && *why);
    CHECK(R::HostBackendFor("software", &why) == R::HostPath::Sdl && R::HostBackendFor("metal", &why) == R::HostPath::Sdl);
    why = nullptr; CHECK(R::HostBackendFor(nullptr, &why) == R::HostPath::Sdl && why && *why);
    CHECK(R::HostBackendFor("direct3d11", nullptr) == R::HostPath::OwnD3D11);

    // the switch is one row of the Experimental table, off by default
    const okay::ownswitches::Item* it = okay::ownswitches::Find("ownrender", okay::ownswitches::Items());
    CHECK(it != nullptr);
    if (it) CHECK(std::string(it->env) == "OKAY_UI_OWN_RENDER" && std::string(it->value) == "1" && (it->needs & okay::ownswitches::kNeedsOkayUi));
}

void RunDriver(const char* driver, R::HostPath wantPath, R::GLFlavor want) {
    std::string label = std::string("render sdl host: SDL renderer \"") + driver + "\"";
    RUN_SUITE(label);
    g_driver = driver;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, driver);
    const int W = 420, H = 380;
    const Uint32 winFlags = SDL_WINDOW_HIDDEN | (IsGL(driver) ? SDL_WINDOW_OPENGL : 0);
    SDL_Window* win = SDL_CreateWindow("okayui host", 0, 0, W, H, winFlags);
    SDL_Renderer* r = win ? SDL_CreateRenderer(win, -1, 0) : nullptr;
    SDL_RendererInfo info; std::memset(&info, 0, sizeof info);
    if (r) SDL_GetRendererInfo(r, &info);
    if (!r || std::strcmp(info.name ? info.name : "", driver) != 0) {
        std::cout << "  SKIPPED: no " << driver << " SDL renderer here (" << SDL_GetError() << ")\n";
        if (r) okay::pb::DestroyRenderer(r);
        if (win) SDL_DestroyWindow(win);
        return;
    }
    if (IsGL(driver)) {
        auto glString = (const unsigned char* (*)(unsigned))SDL_GL_GetProcAddress("glGetString");
        const char* version = glString ? (const char*)glString(0x1F02) : nullptr;
        std::cout << "  context: " << (version ? version : "?") << " / " << (glString ? (const char*)glString(0x1F01) : "?") << "\n";
        R::GLFlavor f = R::GLFlavor::Core33; const char* why = "";
        if (!R::PickGLFlavor(driver, version, &f, &why)) {      // e.g. Windows' built-in OpenGL 1.1 on a machine with no GPU driver
            std::cout << "  SKIPPED: " << why << "\n";
            okay::pb::DestroyRenderer(r); SDL_DestroyWindow(win);
            return;
        }
    }

    // ---- switch on: the own renderer draws, the picture matches the reference ----
    SetSwitch(true);
    for (int look = 0; look < 2; ++look) {
        Shot s = HostFrame(r, (float)W, (float)H, 1.0f, look == 1);
        CHECK(s.frame.path == wantPath);
        if (s.frame.path != wantPath) std::cout << "  not drawn by the own renderer (" << PathName(s.frame.path) << "): " << s.frame.reason << "\n";
        if (wantPath == R::HostPath::OwnGL) CHECK(s.frame.flavor == want);
        CHECK(!s.frame.stats.refused && s.frame.stats.spansDrawn > 0 && s.frame.stats.unresolvedSpans == 0);
        const rscenes::Diff d = rscenes::Compare(s.got, s.ref, s.w, s.h, 8);
        Report(look ? "own, launcher look, scale 1" : "own, classic look, scale 1", d);
        CHECK(WithinTolerance(d));
        CHECK(GlError() == 0);
    }

    // ---- SDL's renderer still works after the own renderer drew (its cached GL state was put back) ----
    {
        Shot s = HostFrame(r, (float)W, (float)H, 1.0f, true);
        SDL_SetRenderDrawColor(r, 255, 0, 255, 255);
        SDL_Rect box{ 4, 300, 12, 10 };
        SDL_RenderFillRect(r, &box);
        std::vector<std::uint8_t> after; int w = 0, h = 0;
        CHECK(ReadBack(r, after, w, h) && w == s.w && h == s.h);
        if (w == s.w && h == s.h) {
            CHECK(Px(after, w, 8, 304) == 0xff00ffffu);                  // SDL's own fill landed, unclipped by the last OkayUI scissor
            int changed = 0;
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                const bool inBox = x >= 4 && x < 16 && y >= 300 && y < 310;
                if (!inBox && Px(after, w, x, y) != Px(s.got, w, x, y)) ++changed;
            }
            std::cout << "  SDL fill after an own-renderer frame: pixels changed outside the box: " << changed << "\n";
            CHECK(changed == 0);
        }
        CHECK(GlError() == 0);
    }

    // ---- a frame with an SDL texture as a host image: the own renderer draws it too (the SDL texture's pixels, no fallback) ----
    {
        SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 32, 32);
        CHECK(tex != nullptr);
        if (tex) {
            SDL_UpdateTexture(tex, nullptr, rscenes::TestImage().data(), 32 * 4);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            RefImage ref; ref.key = tex; ref.w = 32; ref.h = 32; ref.rgba = rscenes::TestImage();
            for (int look = 0; look < 2; ++look) {
                Shot s = HostFrame(r, (float)W, (float)H, 1.0f, look == 1, tex, &ref);
                CHECK(s.frame.path == wantPath);
                if (s.frame.path != wantPath) std::cout << "  host image frame not drawn by the own renderer: " << s.frame.reason << "\n";
                CHECK(s.frame.stats.unresolvedSpans == 0);
                const std::uint32_t p = Px(s.got, s.w, 375, 20);          // the image's top border row is white
                CHECK((p >> 24) > 200 && ((p >> 16) & 255) > 200 && ((p >> 8) & 255) > 200);
                const rscenes::Diff d = rscenes::Compare(s.got, s.ref, s.w, s.h, 8);
                Report(look ? "own, host image, launcher look" : "own, host image, classic look", d);
                CHECK(WithinTolerance(d));
                CHECK(GlError() == 0);
            }
            // SDL draws the same texture right after: its texture and sampler state were put back
            {
                Shot s = HostFrame(r, (float)W, (float)H, 1.0f, false, tex, &ref);
                SDL_Rect dst{ 360, 300, 32, 32 };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                std::vector<std::uint8_t> after; int w = 0, h = 0;
                CHECK(ReadBack(r, after, w, h) && w == s.w);
                if (w == s.w) {
                    const std::uint32_t q = Px(after, w, 370, 300);       // top border: white
                    const std::uint32_t c = Px(after, w, 365, 305);       // inside: x = 5, y = 5 of the test image
                    CHECK(q == 0xffffffffu);
                    CHECK((c >> 24) == 40 && ((c >> 16) & 255) == 40 && ((c >> 8) & 255) == 215);
                }
                CHECK(GlError() == 0);
            }
            // the texture's pixels change between frames: the next frame shows the new ones
            {
                std::vector<std::uint8_t> blue((size_t)32 * 32 * 4);
                for (size_t i = 0; i < blue.size(); i += 4) { blue[i] = 10; blue[i + 1] = 20; blue[i + 2] = 230; blue[i + 3] = 255; }
                SDL_UpdateTexture(tex, nullptr, blue.data(), 32 * 4);
                RefImage refB; refB.key = tex; refB.w = 32; refB.h = 32; refB.rgba = blue;
                Shot s = HostFrame(r, (float)W, (float)H, 1.0f, false, tex, &refB);
                CHECK(s.frame.path == wantPath);
                const std::uint32_t p = Px(s.got, s.w, 375, 30);
                CHECK(p == 0x0a14e6ffu);
                const rscenes::Diff d = rscenes::Compare(s.got, s.ref, s.w, s.h, 8);
                CHECK(WithinTolerance(d));
            }
            // the next frame without the image stays on the own renderer
            Shot t = HostFrame(r, (float)W, (float)H, 1.0f, false);
            CHECK(t.frame.path == wantPath);
            SDL_DestroyTexture(tex);
        }
        // OpenGL ES: SDL keeps ARGB textures with red and blue swapped and swaps them back in its own shader (SDL_GL_BindTexture's
        // documentation says so), so such an image cannot be sampled as it is: that frame takes the SDL path and says why
        const bool es = std::strcmp(driver, "opengles2") == 0;
        if (es) {
            SDL_Texture* argb = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 32, 32);
            if (argb) {
                SDL_UpdateTexture(argb, nullptr, rscenes::TestImage().data(), 32 * 4);
                Shot s = HostFrame(r, (float)W, (float)H, 1.0f, false, argb);
                CHECK(s.frame.path == R::HostPath::Sdl && std::strstr(s.frame.reason, "image") != nullptr);
                std::cout << "  ARGB image on OpenGL ES: " << s.frame.reason << "\n";
                SDL_DestroyTexture(argb);
            }
        }
        // a render target texture SDL drew into, shown as an image (ARGB is what SDL's Direct3D 11 renderer keeps targets in; ABGR on ES)
        SDL_Texture* tgt = SDL_CreateTexture(r, es ? SDL_PIXELFORMAT_ABGR8888 : SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 32, 32);
        CHECK(tgt != nullptr);
        if (tgt && SDL_SetRenderTarget(r, tgt) == 0) {
            SDL_SetRenderDrawColor(r, 200, 40, 60, 255); SDL_RenderClear(r);
            SDL_SetRenderDrawColor(r, 30, 220, 90, 255); SDL_Rect half{ 0, 16, 32, 16 }; SDL_RenderFillRect(r, &half);
            SDL_SetRenderTarget(r, nullptr);
            RefImage ref; ref.key = tgt; ref.w = 32; ref.h = 32; ref.rgba.resize((size_t)32 * 32 * 4);
            for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
                std::uint8_t* q = &ref.rgba[((size_t)y * 32 + x) * 4];
                if (y < 16) { q[0] = 200; q[1] = 40; q[2] = 60; } else { q[0] = 30; q[1] = 220; q[2] = 90; }
                q[3] = 255;
            }
            Shot s = HostFrame(r, (float)W, (float)H, 1.0f, true, tgt, &ref);
            CHECK(s.frame.path == wantPath);
            if (s.frame.path != wantPath) std::cout << "  target image frame not drawn by the own renderer: " << s.frame.reason << "\n";
            CHECK(Px(s.got, s.w, 370, 24) == 0xc8283cffu);                // top half (y = 20 + 4)
            CHECK(Px(s.got, s.w, 370, 44) == 0x1edc5affu);                // bottom half (y = 20 + 24)
            const rscenes::Diff d = rscenes::Compare(s.got, s.ref, s.w, s.h, 8);
            Report("own, render-target image", d);
            CHECK(WithinTolerance(d));
            CHECK(GlError() == 0);
        }
        if (tgt) SDL_DestroyTexture(tgt);
        // images that are gone are let go of: a frame without images leaves none held after the next frames
        for (int i = 0; i < 3; ++i) HostFrame(r, (float)W, (float)H, 1.0f, false);
        std::cout << "  host images still held after frames without images: " << okay::pb::OkayUIHostState().ImageCount() << "\n";
        CHECK(okay::pb::OkayUIHostState().ImageCount() == 0);
    }

    // ---- switch off: nothing changes, the old SDL path draws ----
    SetSwitch(false);
    {
        Shot s = HostFrame(r, (float)W, (float)H, 1.0f, true);
        CHECK(s.frame.path == R::HostPath::Sdl);
        const rscenes::Diff d = rscenes::Compare(s.got, s.ref, s.w, s.h, 8);
        Report("switch off: SDL_RenderGeometry (old path)", d);
        CHECK(d.sameSize);
    }
    SetSwitch(true);

    // ---- the renderer goes away: its own-renderer state is freed while it is still alive ----
    CHECK(okay::pb::OkayUIHostState().Count() == 1);
    okay::pb::DestroyRenderer(r);
    CHECK(okay::pb::OkayUIHostState().Count() == 0);
    SDL_DestroyWindow(win);

    // ---- DPI 2: a window twice the size, renderer scale 2, OkayUI in logical pixels ----
    win = SDL_CreateWindow("okayui host 2x", 0, 0, W * 2, H * 2, winFlags);
    r = win ? SDL_CreateRenderer(win, -1, 0) : nullptr;
    CHECK(r != nullptr);
    if (r) {
        SDL_RenderSetScale(r, 2.0f, 2.0f);
        Shot s = HostFrame(r, (float)W, (float)H, 2.0f, true);
        CHECK(s.frame.path == wantPath);
        CHECK(s.w == W * 2 && s.h == H * 2);
        const rscenes::Diff d = rscenes::Compare(s.got, s.ref, s.w, s.h, 8);
        Report("own, launcher look, scale 2", d);
        CHECK(d.sameSize && d.meanDiff < 0.5);
        // scales that differ in x and y: the old path
        SDL_RenderSetScale(r, 2.0f, 1.0f);
        Shot t = HostFrame(r, (float)W, (float)H * 2, 1.0f, false);
        CHECK(t.frame.path == R::HostPath::Sdl);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        // a render target set: the old path
        SDL_Texture* target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, 64, 64);
        if (target && SDL_SetRenderTarget(r, target) == 0) {
            okayuitest::Reset();
            UI::BeginFrame(InputFor(64, 64)); UI::Button(1, 2, 2, 40, 20, "x");
            okay::pb::OkayUIEndFrame(r);
            CHECK(okay::pb::OkayUILastFrame().path == R::HostPath::Sdl);
            SDL_SetRenderTarget(r, nullptr);
        }
        if (target) SDL_DestroyTexture(target);
        CHECK(GlError() == 0);
        okay::pb::DestroyRenderer(r);
    }
    if (win) SDL_DestroyWindow(win);
    SetSwitch(false);
    CHECK(okay::pb::OkayUIHostState().Count() == 0);
}

} // namespace

int main() {
    RUN_SUITE("okayui render: own renderer (OpenGL, Direct3D 11) through the SDL host path");
    TestLogic();
#if defined(_WIN32)
    SetEnv("SDL_VIDEODRIVER", nullptr);          // the normal Windows video driver, hidden windows
#else
    SetEnv("SDL_VIDEODRIVER", "offscreen");      // a GL context through EGL, no display (the test list sets "dummy" for the other tests)
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cout << "  SKIPPED: SDL video did not start (" << SDL_GetError() << ")\n";
    } else {
        RunDriver("direct3d11", R::HostPath::OwnD3D11, R::GLFlavor::Core33);   // SKIPPED off Windows
        RunDriver("opengl", R::HostPath::OwnGL, R::GLFlavor::Core33);
        RunDriver("opengles2", R::HostPath::OwnGL, R::GLFlavor::ES30);
        SDL_Quit();
    }
    TEST_MAIN_RESULT();
}
