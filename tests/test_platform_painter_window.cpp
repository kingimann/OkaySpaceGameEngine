// The software okay::platform::Painter on a real own-backend window, under Xvfb (tests/run_under_xvfb.sh):
// what the painter drew is what the X server holds for the window (XPutImage path, read back with XGetImage),
// through a resize, a frame smaller than the window, and the frame pacing that stands in for vsync.
// Also: the painters refuse the windows they cannot draw on. Exit 77 = skipped (no display / own backend).
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "okay/Platform/Painter.hpp"
#include "OkayUI_SoftPainter.hpp"
#include "Platform/OwnLinux.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using namespace okay::platform;
namespace ui2d = okay::ui2d;

namespace {
void Pump(Backend& b, int ms) {
    const auto t0 = std::chrono::steady_clock::now();
    do {
        Event e;
        while (b.PollEvent(e)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms));
}

void DrawScene(Painter& p, int phase) {
    p.SetClipRect(nullptr);
    p.SetBlendMode(BlendMode::None);
    p.SetDrawColor(20, 40, 60, 255);
    p.Clear();
    p.SetDrawColor(220, 30, 30, 255);
    const ui2d::Rect r{10 + phase, 10, 100, 50};
    p.FillRect(&r);
    p.SetBlendMode(BlendMode::Blend);
    p.SetDrawColor(30, 220, 30, 128);
    const ui2d::Rect r2{60, 30, 100, 50};
    p.FillRect(&r2);
    const ui2d::RGBA c{250, 250, 0, 255};
    ui2d::Vertex tri[3] = {{{200, 20}, c, {0, 0}}, {{260, 20}, c, {0, 0}}, {{200, 100}, ui2d::RGBA{0, 0, 255, 255}, {0, 0}}};
    p.Geometry(nullptr, tri, 3, nullptr, 0);
}

// The window as the X server holds it against the painter's own frame buffer; counts differing pixels.
int Mismatches(Backend& b, Window& w, Painter& p, int* gw = nullptr, int* gh = nullptr) {
    std::vector<uint8_t> shot, mine;
    int sw = 0, sh = 0, mw = 0, mh = 0;
    if (!okay::platform::own::GrabWindowPixels(b, w, shot, sw, sh)) return -1;
    if (!p.ReadPixels(mine, mw, mh)) return -1;
    if (gw) *gw = sw;
    if (gh) *gh = sh;
    if (sw != mw || sh != mh) return -2;
    int bad = 0;
    for (size_t i = 0; i < (size_t)sw * sh; ++i)
        if (shot[i * 4] != mine[i * 4] || shot[i * 4 + 1] != mine[i * 4 + 1] || shot[i * 4 + 2] != mine[i * 4 + 2]) ++bad;
    return bad;
}
} // namespace

int main() {
    RUN_SUITE("platform painter on a window");
    if (!IsCompiledIn(BackendKind::Own)) { std::cout << "SKIP: own backend not compiled in\n"; return 77; }
    if (!std::getenv("DISPLAY")) { std::cout << "SKIP: no $DISPLAY (run through tests/run_under_xvfb.sh)\n"; return 77; }
    Backend* b = Get(BackendKind::Own);
    CHECK(b != nullptr);
    if (!b) return 1;
    if (!b->Init(InitFlags{true, false, false})) { std::cout << "SKIP: cannot open the display: " << b->LastError() << "\n"; return 77; }

    WindowDesc d;
    d.title = "painter test"; d.x = 50; d.y = 50; d.width = 320; d.height = 240;
    std::unique_ptr<Window> win = b->MakeWindow(d);
    CHECK(win != nullptr);
    if (!win) return 1;
    CHECK(win->Kind() == BackendKind::Own);
    Pump(*b, 300);

    // the painters refuse windows they cannot draw on
    {
        std::string err;
        CHECK(CreateSdl2Painter(*win, PainterDesc(), &err) == nullptr && !err.empty());
        std::cout << "  sdl2 painter on an own window: refused (" << err << ")\n";
    }
    PainterDesc pd; pd.vsync = false;
    std::string err;
    std::unique_ptr<Painter> p = OkayUI::soft::CreateWindowPainter(*win, pd, &err);
    CHECK(p != nullptr);
    if (!p) { std::cerr << err << "\n"; return 1; }
    CHECK(p->Kind() == PainterKind::Software && p->NativeRenderer() == nullptr);
    int w = 0, h = 0;
    p->OutputSize(&w, &h);
    CHECK(w == 320 && h == 240);

    // the picture on the server is the picture the painter drew
    DrawScene(*p, 0);
    p->Present();
    Pump(*b, 150);
    int gw = 0, gh = 0;
    int bad = Mismatches(*b, *win, *p, &gw, &gh);
    std::cout << "  window " << gw << "x" << gh << ", pixels that differ from the frame buffer: " << bad << "\n";
    CHECK(gw == 320 && gh == 240 && bad == 0);
    {
        std::vector<uint8_t> shot; int sw = 0, sh = 0;
        if (okay::platform::own::GrabWindowPixels(*b, *win, shot, sw, sh) && sw == 320) {
            const uint8_t* c = &shot[((size_t)20 * sw + 20) * 4];
            CHECK(c[0] == 220 && c[1] == 30 && c[2] == 30);                 // the red rectangle
            const uint8_t* bg = &shot[((size_t)200 * sw + 300) * 4];
            CHECK(bg[0] == 20 && bg[1] == 40 && bg[2] == 60);               // the clear colour
            if (const char* out = std::getenv("OKAY_PLATFORM_SHOT")) {
                if (FILE* f = std::fopen(out, "wb")) {
                    std::fprintf(f, "P6\n%d %d\n255\n", sw, sh);
                    for (int i = 0; i < sw * sh; ++i) std::fwrite(&shot[(size_t)i * 4], 1, 3, f);
                    std::fclose(f);
                }
            }
        } else CHECK(false);
    }
    // a second frame differs from the first (a stale buffer cannot pass for it)
    DrawScene(*p, 40);
    p->Present();
    Pump(*b, 100);
    CHECK(Mismatches(*b, *win, *p) == 0);

    // the frame buffer follows the window's size
    win->SetSize(400, 300);
    Pump(*b, 300);
    p->OutputSize(&w, &h);
    CHECK(w == 400 && h == 300);
    DrawScene(*p, 10);
    p->Present();
    Pump(*b, 150);
    bad = Mismatches(*b, *win, *p, &gw, &gh);
    std::cout << "  after SetSize: window " << gw << "x" << gh << ", pixels that differ: " << bad << "\n";
    CHECK(gw == 400 && gh == 300 && bad == 0);

    // PresentPixels directly: a frame smaller than the window fills the top-left corner only; bad input is refused
    {
        std::vector<uint8_t> frame((size_t)100 * 50 * 4);
        for (size_t i = 0; i < frame.size(); i += 4) { frame[i] = 255; frame[i + 1] = 0; frame[i + 2] = 255; frame[i + 3] = 255; }
        CHECK(win->PresentPixels(frame.data(), 100, 50));
        Pump(*b, 100);
        std::vector<uint8_t> shot; int sw = 0, sh = 0;
        CHECK(okay::platform::own::GrabWindowPixels(*b, *win, shot, sw, sh));
        if (sw == 400) {
            const uint8_t* in = &shot[((size_t)10 * sw + 10) * 4];
            const uint8_t* out = &shot[((size_t)10 * sw + 150) * 4];
            CHECK(in[0] == 255 && in[1] == 0 && in[2] == 255);
            CHECK(!(out[0] == 255 && out[1] == 0 && out[2] == 255));
        }
        CHECK(!win->PresentPixels(nullptr, 10, 10) && !win->PresentPixels(frame.data(), 0, 10) && !win->PresentPixels(frame.data(), 10, -1));
    }

    // vsync-less frame pacing: with vsync the present waits for the next 60 Hz period, without it does not
    {
        using clock = std::chrono::steady_clock;
        const int frames = 12;
        auto time = [&](Painter& pp) {
            DrawScene(pp, 0);
            pp.Present();
            const auto t0 = clock::now();
            for (int i = 0; i < frames; ++i) { DrawScene(pp, i); pp.Present(); }
            return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
        };
        const long long free_ms = time(*p);
        PainterDesc paced; paced.vsync = true;
        std::unique_ptr<Painter> pv = OkayUI::soft::CreateWindowPainter(*win, paced, &err);
        CHECK(pv != nullptr);
        if (pv) {
            CHECK(!pv->SetVSync(true));                     // paced, never display-synced
            const long long paced_ms = time(*pv);
            std::cout << "  " << frames << " presents: free-running " << free_ms << " ms, paced " << paced_ms << " ms (60 Hz = " << (frames * 1000 / 60) << " ms)\n";
            CHECK(paced_ms >= frames * 1000 / 60 - 20);     // never faster than the refresh period
            CHECK(paced_ms <= frames * 1000 / 60 + 250);    // and does not stall
        }
    }

    // painters destroyed before the window
    p.reset();
    win.reset();
    b->Shutdown();
    TEST_MAIN_RESULT();
}
