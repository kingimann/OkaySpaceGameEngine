// The host path with OkayUI's own renderer: an OkayUI frame ended with okay::pb::OkayUIEndFrame (shared/OkayUIFrame.hpp, what the launcher
// and the editor call) on a real SDL_Renderer that runs on OpenGL, with OKAY_UI_OWN_RENDER=1. The frame must be drawn by OkayUI's own
// OpenGL backend (not by SDL_RenderGeometry) and match the software reference within the tolerance the OpenGL backend test uses.
// SDL's "offscreen" video driver with EGL gives a GL context with no display (Mesa's llvmpipe here, a CPU rasteriser); where SDL cannot
// make an OpenGL renderer the GL part says SKIPPED and passes, and the logic checks still run.
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

void SetSwitch(bool on) {
    if (on) setenv("OKAY_UI_OWN_RENDER", "1", 1); else unsetenv("OKAY_UI_OWN_RENDER");
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

std::vector<std::uint8_t> SoftRef(const UI::DrawData& d, int w, int h, float dpi) {
    UI::soft::Bitmap bm(w, h); bm.Clear(kClear[0], kClear[1], kClear[2]);
    auto be = R::CreateSoftBackend(bm); R::Renderer r(*be);
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

unsigned GlError() {
    auto f = (unsigned (*)())SDL_GL_GetProcAddress("glGetError");
    return f ? f() : 0;
}

/// Runs the screen for a few frames and ends the last one through the host path. Returns the window picture and the reference.
struct Shot { std::vector<std::uint8_t> got, ref; int w = 0, h = 0; R::HostFrame frame; };
Shot HostFrame(SDL_Renderer* r, float logicalW, float logicalH, float dpi, bool launcher, SDL_Texture* image = nullptr) {
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
    s.ref = SoftRef(UI::GetDrawData(), s.w, s.h, dpi);
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

    // the switch is one row of the Experimental table, off by default
    const okay::ownswitches::Item* it = okay::ownswitches::Find("ownrender", okay::ownswitches::Items());
    CHECK(it != nullptr);
    if (it) CHECK(std::string(it->env) == "OKAY_UI_OWN_RENDER" && std::string(it->value) == "1" && (it->needs & okay::ownswitches::kNeedsOkayUi));
}

void RunDriver(const char* driver, R::GLFlavor want) {
    std::string label = std::string("render sdl host: SDL renderer \"") + driver + "\"";
    RUN_SUITE(label);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, driver);
    const int W = 420, H = 380;
    SDL_Window* win = SDL_CreateWindow("okayui host", 0, 0, W, H, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    SDL_Renderer* r = win ? SDL_CreateRenderer(win, -1, 0) : nullptr;
    SDL_RendererInfo info; std::memset(&info, 0, sizeof info);
    if (r) SDL_GetRendererInfo(r, &info);
    if (!r || std::strcmp(info.name ? info.name : "", driver) != 0) {
        std::cout << "  SKIPPED: no " << driver << " SDL renderer here (" << SDL_GetError() << ")\n";
        if (r) okay::pb::DestroyRenderer(r);
        if (win) SDL_DestroyWindow(win);
        return;
    }
    auto glString = (const unsigned char* (*)(unsigned))SDL_GL_GetProcAddress("glGetString");
    std::cout << "  context: " << (glString ? (const char*)glString(0x1F02) : "?") << " / " << (glString ? (const char*)glString(0x1F01) : "?") << "\n";

    // ---- switch on: the own renderer draws, the picture matches the reference ----
    SetSwitch(true);
    for (int look = 0; look < 2; ++look) {
        Shot s = HostFrame(r, (float)W, (float)H, 1.0f, look == 1);
        CHECK(s.frame.path == R::HostPath::OwnGL);
        if (s.frame.path != R::HostPath::OwnGL) std::cout << "  not drawn by the own renderer: " << s.frame.reason << "\n";
        CHECK(s.frame.flavor == want);
        CHECK(!s.frame.stats.refused && s.frame.stats.spansDrawn > 0 && s.frame.stats.unresolvedSpans == 0);
        const rscenes::Diff d = rscenes::Compare(s.got, s.ref, s.w, s.h, 8);
        Report(look ? "own GL, launcher look, scale 1" : "own GL, classic look, scale 1", d);
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
            std::cout << "  SDL fill after an own-GL frame: pixels changed outside the box: " << changed << "\n";
            CHECK(changed == 0);
        }
        CHECK(GlError() == 0);
    }

    // ---- a frame with an SDL texture as a host image: the SDL path draws it (whole frame), and says why ----
    {
        SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 32, 32);
        CHECK(tex != nullptr);
        if (tex) {
            SDL_UpdateTexture(tex, nullptr, rscenes::TestImage().data(), 32 * 4);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            Shot s = HostFrame(r, (float)W, (float)H, 1.0f, false, tex);
            CHECK(s.frame.path == R::HostPath::Sdl);
            CHECK(std::strstr(s.frame.reason, "image") != nullptr);
            std::cout << "  host image frame: " << s.frame.reason << "\n";
            const std::uint32_t p = Px(s.got, s.w, 375, 20);              // the image's top border row is white
            CHECK((p >> 24) > 200 && ((p >> 16) & 255) > 200 && ((p >> 8) & 255) > 200);
            // the next frame without the image goes back to the own renderer
            Shot t = HostFrame(r, (float)W, (float)H, 1.0f, false);
            CHECK(t.frame.path == R::HostPath::OwnGL);
            SDL_DestroyTexture(tex);
        }
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
    win = SDL_CreateWindow("okayui host 2x", 0, 0, W * 2, H * 2, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    r = win ? SDL_CreateRenderer(win, -1, 0) : nullptr;
    CHECK(r != nullptr);
    if (r) {
        SDL_RenderSetScale(r, 2.0f, 2.0f);
        Shot s = HostFrame(r, (float)W, (float)H, 2.0f, true);
        CHECK(s.frame.path == R::HostPath::OwnGL);
        CHECK(s.w == W * 2 && s.h == H * 2);
        const rscenes::Diff d = rscenes::Compare(s.got, s.ref, s.w, s.h, 8);
        Report("own GL, launcher look, scale 2", d);
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
    RUN_SUITE("okayui render: own OpenGL renderer through the SDL host path");
    TestLogic();
    setenv("SDL_VIDEODRIVER", "offscreen", 1);   // a GL context through EGL, no display (the test list sets "dummy" for the other tests)
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cout << "  SKIPPED: SDL offscreen video did not start (" << SDL_GetError() << ")\n";
    } else {
        RunDriver("opengl", R::GLFlavor::Core33);
        RunDriver("opengles2", R::GLFlavor::ES30);
        SDL_Quit();
    }
    TEST_MAIN_RESULT();
}
