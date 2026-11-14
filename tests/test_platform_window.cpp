// okay::platform with a real X server (run under Xvfb by tests/run_under_xvfb.sh):
// window create / resize / title, GL context + clear colour read back, keyboard,
// text input, mouse buttons / wheel / motion, the clipboard, all with events
// injected by xdotool (XTEST) so they travel the server's real input path.
//
//   test_platform_window sdl2|own
//
// Exit code 77 = skipped (no display, no xdotool, backend not compiled in).
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "okay/Input/PlatformKeys.hpp"
#include "Platform/Internal.hpp"
#if defined(__linux__)
#include "Platform/OwnLinux.hpp"
#endif
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#if defined(__has_include)
#if __has_include(<SDL.h>)
#include <SDL.h>
#define OKAY_TEST_HAVE_SDL_HEADERS 1
#endif
#endif

using namespace okay::platform;

namespace {

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Run a shell command, return its stdout.
std::string Sh(const std::string& cmd) {
    std::string out;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[256];
        while (size_t n = fread(buf, 1, sizeof buf, p)) out.append(buf, n);
        pclose(p);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// Run xdotool, then pump events for a moment and return what arrived.
std::vector<Event> Inject(Backend& b, const std::string& args, int settleMs = 150) {
    const std::string cmd = "LC_ALL=C.UTF-8 xdotool " + args + " >/dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) std::cout << "  (xdotool " << args << " failed)\n";
    std::vector<Event> evs;
    const auto t0 = std::chrono::steady_clock::now();
    do {
        Event e;
        while (b.PollEvent(e)) evs.push_back(std::move(e));
        SleepMs(10);
    } while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(settleMs));
    return evs;
}

std::vector<Event> Pump(Backend& b, int ms) {
    std::vector<Event> evs;
    const auto t0 = std::chrono::steady_clock::now();
    do {
        Event e;
        while (b.PollEvent(e)) evs.push_back(std::move(e));
        SleepMs(10);
    } while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms));
    return evs;
}

int Count(const std::vector<Event>& v, EventType t) { int n = 0; for (auto& e : v) if (e.type == t) ++n; return n; }
const Event* Find(const std::vector<Event>& v, EventType t, const std::function<bool(const Event&)>& pred = nullptr) {
    for (auto& e : v) if (e.type == t && (!pred || pred(e))) return &e;
    return nullptr;
}
std::string TextOf(const std::vector<Event>& v) {
    std::string s;
    for (auto& e : v) if (e.type == EventType::TextInput) s += e.text;
    return s;
}

// GL entry points come through the backend, as the engine's GLRenderer does.
struct GL {
    void (*ClearColor)(float, float, float, float) = nullptr;
    void (*Clear)(unsigned) = nullptr;
    void (*Finish)() = nullptr;
    void (*ReadPixels)(int, int, int, int, unsigned, unsigned, void*) = nullptr;
    void (*Viewport)(int, int, int, int) = nullptr;
    const unsigned char* (*GetString)(unsigned) = nullptr;
    bool Load(Backend& b) {
        ClearColor = (decltype(ClearColor))b.GLProcAddress("glClearColor");
        Clear = (decltype(Clear))b.GLProcAddress("glClear");
        Finish = (decltype(Finish))b.GLProcAddress("glFinish");
        ReadPixels = (decltype(ReadPixels))b.GLProcAddress("glReadPixels");
        Viewport = (decltype(Viewport))b.GLProcAddress("glViewport");
        GetString = (decltype(GetString))b.GLProcAddress("glGetString");
        return ClearColor && Clear && Finish && ReadPixels && Viewport && GetString;
    }
};

bool Near(int a, int b, int tol = 2) { return std::abs(a - b) <= tol; }

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("platform_window");
    BackendKind kind = BackendKind::Own;
    if (argc < 2 || !ParseBackendName(argv[1], kind)) { std::cerr << "usage: test_platform_window sdl2|own\n"; return 2; }
    if (!IsCompiledIn(kind)) { std::cout << "SKIP: backend " << argv[1] << " not compiled in\n"; return 77; }
    if (!std::getenv("DISPLAY")) { std::cout << "SKIP: no $DISPLAY (run through tests/run_under_xvfb.sh)\n"; return 77; }
    if (std::system("command -v xdotool >/dev/null 2>&1") != 0) { std::cout << "SKIP: xdotool not installed\n"; return 77; }
    setenv("SDL_VIDEODRIVER", "x11", 1);   // the sdl2 backend must talk to the same server

    Backend* b = Get(kind);
    CHECK(b != nullptr);
    if (!b) return 1;
    std::cout << "[backend] " << b->Name() << "\n";
    if (!b->Init(InitFlags{true, false, false})) {
        std::cout << "SKIP: cannot open the display: " << b->LastError() << "\n";
        return 77;
    }

    // ---- window -------------------------------------------------------------------
    WindowDesc d;
    d.title = "Okay Platform Test \xC3\xA9";   // "é": the title goes out as UTF-8
    d.x = 100; d.y = 80; d.width = 320; d.height = 240; d.opengl = true;
    auto win = b->MakeWindow(d);
    CHECK(win != nullptr);
    if (!win) { std::cout << "MakeWindow failed: " << b->LastError() << "\n"; return 1; }
    CHECK(win->NativeHandle() != nullptr);
    win->Raise();
    Pump(*b, 200);
    int w = 0, h = 0;
    win->GetSize(w, h);
    CHECK(w == 320 && h == 240);
    int px = 0, py = 0;
    win->GetPosition(px, py);
    CHECK(Near(px, 100, 40) && Near(py, 80, 40));   // a window manager may add a frame offset
    if (kind == BackendKind::Own) {   // NativeHandle is the X window id for `own` (an SDL_Window* for sdl2)
        char id[32]; std::snprintf(id, sizeof id, "%lu", (unsigned long)(uintptr_t)win->NativeHandle());
        const std::string name = Sh(std::string("xdotool getwindowname ") + id + " 2>/dev/null");
        std::cout << "  window name as the server sees it: " << name << "\n";
        CHECK(name == d.title);
        win->SetTitle("Renamed");
        Pump(*b, 50);
        CHECK(Sh(std::string("xdotool getwindowname ") + id + " 2>/dev/null") == "Renamed");
    }

    // ---- resize ---------------------------------------------------------------------
    win->SetSize(400, 300);
    auto ev = Pump(*b, 300);
    win->GetSize(w, h);
    CHECK(w == 400 && h == 300);
    const Event* rs = Find(ev, EventType::WindowResized, [](const Event& e) { return e.width == 400 && e.height == 300; });
    CHECK(rs != nullptr);
    win->SetSize(320, 240);
    Pump(*b, 200);

    // ---- GL context + clear colour ---------------------------------------------------
    CHECK(win->CreateGLContext(GLAttributes{}));
    CHECK(win->MakeCurrent());
    GL gl;
    CHECK(gl.Load(*b));
    if (gl.ClearColor) {
        const char* renderer = (const char*)gl.GetString(0x1F01 /*GL_RENDERER*/);
        std::cout << "  GL renderer: " << (renderer ? renderer : "?") << "\n";
        win->GetDrawableSize(w, h);
        gl.Viewport(0, 0, w, h);
        gl.ClearColor(0.2f, 0.4f, 0.8f, 1.0f);     // 51, 102, 204
        gl.Clear(0x4000 /*GL_COLOR_BUFFER_BIT*/);
        unsigned char px4[4] = {0, 0, 0, 0};
        gl.ReadPixels(w / 2, h / 2, 1, 1, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, px4);
        std::cout << "  glReadPixels centre: " << (int)px4[0] << "," << (int)px4[1] << "," << (int)px4[2] << "," << (int)px4[3] << "\n";
        CHECK(Near(px4[0], 51) && Near(px4[1], 102) && Near(px4[2], 204) && px4[3] == 255);
        win->SwapBuffers();
        gl.Finish();
        CHECK(win->SetSwapInterval(0) || true);   // not every driver has the extension; must not crash
#if defined(__linux__)
        if (kind == BackendKind::Own) {
            // The window itself, read back from the X server: the colour we cleared to is on screen.
            Pump(*b, 200);
            gl.ClearColor(0.2f, 0.4f, 0.8f, 1.0f);
            gl.Clear(0x4000);
            win->SwapBuffers();
            gl.Finish();
            Pump(*b, 200);
            std::vector<uint8_t> rgba;
            int gw = 0, gh = 0;
            CHECK(okay::platform::own::GrabWindowPixels(*b, *win, rgba, gw, gh));
            CHECK(gw == 320 && gh == 240);
            if (gw == 320 && gh == 240) {
                const uint8_t* c = &rgba[((size_t)(gh / 2) * gw + gw / 2) * 4];
                std::cout << "  X server pixel (XGetImage) centre: " << (int)c[0] << "," << (int)c[1] << "," << (int)c[2] << "\n";
                CHECK(Near(c[0], 51, 3) && Near(c[1], 102, 3) && Near(c[2], 204, 3));
                const uint8_t* corner = &rgba[((size_t)(gh - 2) * gw + 2) * 4];
                CHECK(Near(corner[0], 51, 3) && Near(corner[2], 204, 3));
                // Keep a screenshot for the record when asked.
                if (const char* out = std::getenv("OKAY_PLATFORM_SHOT")) {
                    if (FILE* f = std::fopen(out, "wb")) {
                        std::fprintf(f, "P6\n%d %d\n255\n", gw, gh);
                        for (int i = 0; i < gw * gh; ++i) std::fwrite(&rgba[(size_t)i * 4], 1, 3, f);
                        std::fclose(f);
                    }
                }
            }
        }
#endif
        // A second colour, so a stale buffer cannot pass for the first.
        gl.ClearColor(0.9f, 0.1f, 0.1f, 1.0f);
        gl.Clear(0x4000);
        gl.ReadPixels(5, 5, 1, 1, 0x1908, 0x1401, px4);
        CHECK(Near(px4[0], 230, 3) && Near(px4[1], 26, 3) && Near(px4[2], 26, 3));
    }

    // ---- mouse ------------------------------------------------------------------------
    win->GetPosition(px, py);
    win->Raise();
    Pump(*b, 100);
    {
        char cmd[96];
        std::snprintf(cmd, sizeof cmd, "mousemove %d %d", px + 60, py + 50);
        ev = Inject(*b, cmd);
        const Event* mv = Find(ev, EventType::MouseMove, [](const Event& e) { return e.x == 60 && e.y == 50; });
        CHECK(mv != nullptr);
        int mx = 0, my = 0;
        b->MouseState(&mx, &my);
        CHECK(mx == 60 && my == 50);
    }
    ev = Inject(*b, "click 1");
    {
        const Event* dn = Find(ev, EventType::MouseButtonDown);
        const Event* up = Find(ev, EventType::MouseButtonUp);
        CHECK(dn && dn->button == MouseLeft && dn->x == 60 && dn->y == 50 && dn->clicks == 1);
        CHECK(up && up->button == MouseLeft);
    }
    ev = Inject(*b, "click 3");
    CHECK(Find(ev, EventType::MouseButtonDown, [](const Event& e) { return e.button == MouseRight; }) != nullptr);
    ev = Inject(*b, "click 2");
    CHECK(Find(ev, EventType::MouseButtonDown, [](const Event& e) { return e.button == MouseMiddle; }) != nullptr);
    ev = Inject(*b, "click 4");
    CHECK(Find(ev, EventType::MouseWheel, [](const Event& e) { return e.wheelY > 0; }) != nullptr);
    ev = Inject(*b, "click 5");
    CHECK(Find(ev, EventType::MouseWheel, [](const Event& e) { return e.wheelY < 0; }) != nullptr);
    if (kind == BackendKind::Own) {
        // Events that arrive together are merged by the queue (EventQueue.hpp): a burst of 12 moves comes out as
        // one or two events whose dx / dy add up and whose position is the last one; 5 wheel notches add up.
        char cmd[1024];
        std::snprintf(cmd, sizeof cmd, "mousemove %d %d", px + 20, py + 20);
        Inject(*b, cmd);
        std::string burst;
        for (int i = 1; i <= 12; ++i) { char one[64]; std::snprintf(one, sizeof one, "mousemove %d %d ", px + 20 + i * 3, py + 20 + i * 2); burst += one; }
        ev = Inject(*b, burst);
        int sdx = 0, sdy = 0, nmv = 0, lastx = -1, lasty = -1;
        for (auto& e : ev) if (e.type == EventType::MouseMove) { sdx += e.dx; sdy += e.dy; nmv++; lastx = e.x; lasty = e.y; }
        std::cout << "  12 injected moves arrived as " << nmv << " event(s), dx=" << sdx << " dy=" << sdy << "\n";
        CHECK(nmv >= 1 && nmv <= 4);
        CHECK(sdx == 36 && sdy == 24);
        CHECK(lastx == 20 + 36 && lasty == 20 + 24);
        ev = Inject(*b, "click --repeat 5 --delay 1 4");
        float wy = 0; int nw = 0;
        for (auto& e : ev) if (e.type == EventType::MouseWheel) { wy += e.wheelY; nw++; }
        std::cout << "  5 wheel notches arrived as " << nw << " event(s), sum " << wy << "\n";
        CHECK(nw >= 1 && nw <= 3);
        CHECK(wy > 4.9f && wy < 5.1f);
        // A click between two moves is never reordered or merged away.
        std::snprintf(cmd, sizeof cmd, "mousemove %d %d click 1 mousemove %d %d", px + 30, py + 30, px + 40, py + 40);
        ev = Inject(*b, cmd);
        int iDown = -1, iMoveBefore = -1, iMoveAfter = -1;
        for (int i = 0; i < (int)ev.size(); ++i) {
            if (ev[i].type == EventType::MouseButtonDown) iDown = i;
            else if (ev[i].type == EventType::MouseMove && iDown < 0) iMoveBefore = i;
            else if (ev[i].type == EventType::MouseMove && iDown >= 0) iMoveAfter = i;
        }
        CHECK(iMoveBefore >= 0 && iDown > iMoveBefore && iMoveAfter > iDown);
    }
    Inject(*b, "mousedown 1", 100);
    {
        int mx, my;
        CHECK((b->MouseState(&mx, &my) & MouseMask(MouseLeft)) != 0);
    }
    Inject(*b, "mouseup 1", 100);
    {
        int mx, my;
        CHECK((b->MouseState(&mx, &my) & MouseMask(MouseLeft)) == 0);
    }

    // ---- keyboard ------------------------------------------------------------------------
    ev = Inject(*b, "key a");
    {
        const Event* kd = Find(ev, EventType::KeyDown, [](const Event& e) { return e.key == Key::A; });
        const Event* ku = Find(ev, EventType::KeyUp, [](const Event& e) { return e.key == Key::A; });
        CHECK(kd != nullptr && ku != nullptr);
        CHECK(Count(ev, EventType::TextInput) == 0);    // text input is off until asked for
    }
    ev = Inject(*b, "key Escape");
    CHECK(Find(ev, EventType::KeyDown, [](const Event& e) { return e.key == Key::Escape; }) != nullptr);
    ev = Inject(*b, "key Return");
    CHECK(Find(ev, EventType::KeyDown, [](const Event& e) { return e.key == Key::Return; }) != nullptr);
    ev = Inject(*b, "key F5");
    CHECK(Find(ev, EventType::KeyDown, [](const Event& e) { return e.key == Key::F5; }) != nullptr);
    ev = Inject(*b, "key Left");
    CHECK(Find(ev, EventType::KeyDown, [](const Event& e) { return e.key == Key::Left; }) != nullptr);
    ev = Inject(*b, "key 5");
    CHECK(Find(ev, EventType::KeyDown, [](const Event& e) { return e.key == Key::N5; }) != nullptr);
    ev = Inject(*b, "key ctrl+c");
    {
        const Event* kd = Find(ev, EventType::KeyDown, [](const Event& e) { return e.key == Key::C; });
        CHECK(kd && (kd->mods & ModCtrl));
        CHECK(Find(ev, EventType::KeyDown, [](const Event& e) { return e.key == Key::LCtrl; }) != nullptr);
        CHECK(Count(ev, EventType::TextInput) == 0);    // Ctrl+C makes no text
    }
    // KeyForChar: the physical key that types a letter on the CURRENT layout (US here).
    CHECK(b->KeyForChar('a') == Key::A && b->KeyForChar('z') == Key::Z && b->KeyForChar('w') == Key::W);
    CHECK(b->KeyForChar('1') == Key::N1 && b->KeyForChar('0') == Key::N0 && b->KeyForChar(' ') == Key::Space);
    CHECK(b->KeyForChar('A') == Key::A || b->KeyForChar('A') == Key::Unknown);   // sdl2 maps the keycode 'A' to nothing
    CHECK(b->KeyForChar('!') == Key::Unknown);
    // ModState follows the held modifier keys.
    Inject(*b, "keydown shift", 100);
    CHECK((b->ModState() & ModShift) != 0 && (b->ModState() & ModCtrl) == 0);
    Inject(*b, "keyup shift", 100);
    CHECK((b->ModState() & ModShift) == 0);
    // The extended keys the player feeds the game (F-keys, navigation block, Tab, L/R modifiers).
    {
        Inject(*b, "keydown F5", 80); Inject(*b, "keydown Tab", 80); Inject(*b, "keydown Home", 80);
        Inject(*b, "keydown Control_R", 80); Inject(*b, "keydown Shift_L", 80);
        std::vector<char> down;
        okay::AppendExtendedKeys(*b, down);
        auto has = [&](char c) { for (char d : down) if (d == c) return true; return false; };
        CHECK(has(okay::Input::KeyF(5)) && has('\t') && has(okay::Input::KeyHome));
        CHECK(has(okay::Input::KeyRCtrl) && has(okay::Input::KeyLShift));
        for (char d : down) std::cout << " " << (int)(unsigned char)d;
        std::cout << "  <- extended keys held\n";
        CHECK(!has(okay::Input::KeyF(6)));
        CHECK(!has(okay::Input::KeyRShift));
        CHECK(!has(okay::Input::KeyEnd));
        Inject(*b, "keyup F5", 80); Inject(*b, "keyup Tab", 80); Inject(*b, "keyup Home", 80);
        Inject(*b, "keyup Control_R", 80); Inject(*b, "keyup Shift_L", 80);
        down.clear();
        okay::AppendExtendedKeys(*b, down);
        CHECK(down.empty());
    }
    Inject(*b, "keydown a", 100);
    CHECK(b->IsKeyDown(Key::A));
    CHECK(!b->IsKeyDown(Key::B));
    Inject(*b, "keyup a", 100);
    CHECK(!b->IsKeyDown(Key::A));

    // ---- text input ------------------------------------------------------------------------
    b->StartTextInput();
    ev = Inject(*b, "type 'Hi 1!'", 400);
    std::cout << "  typed \"Hi 1!\" -> text events: \"" << TextOf(ev) << "\"\n";
    CHECK(TextOf(ev) == "Hi 1!");
    // A character the US layout has no key for: switch the server to the German layout and press
    // the a-umlaut key (xdotool's own "type" remaps a spare keycode and restores it before a slow
    // client reads the event, which would make this test race).
    if (std::system("setxkbmap de >/dev/null 2>&1") == 0) {
        Pump(*b, 200);   // the MappingNotify: the backend must refresh its keymap
        // German layout: the physical key that types "z" is where US has Y, and the other way round.
        CHECK(b->KeyForChar('z') == Key::Y);
        CHECK(b->KeyForChar('y') == Key::Z);
        CHECK(b->KeyForChar('a') == Key::A);
        ev = Inject(*b, "key adiaeresis", 300);
        std::cout << "  German layout, a-umlaut key -> text events: \"" << TextOf(ev) << "\"\n";
        CHECK(TextOf(ev) == "\xC3\xA4");
        ev = Inject(*b, "key shift+adiaeresis", 300);
        CHECK(TextOf(ev) == "\xC3\x84");
        std::system("setxkbmap us >/dev/null 2>&1");
        Pump(*b, 200);
        ev = Inject(*b, "key a", 200);
        CHECK(TextOf(ev) == "a");
    } else {
        std::cout << "  (setxkbmap not available: non-ASCII text input not tested)\n";
    }
    ev = Inject(*b, "key BackSpace");
    CHECK(Count(ev, EventType::TextInput) == 0);    // Backspace is a key, not text
    b->StopTextInput();
    ev = Inject(*b, "key b");
    CHECK(Count(ev, EventType::TextInput) == 0);

    // ---- relative mouse (mouse-look) --------------------------------------------------------
    if (b->SetRelativeMouse(true)) {
        Pump(*b, 100);
        { int fx, fy; b->RelativeMouseState(&fx, &fy); }   // flush motion from before the lock
        ev = Inject(*b, "mousemove_relative 12 7", 200);
        const Event* mv = Find(ev, EventType::MouseMove, [](const Event& e) { return e.dx != 0 || e.dy != 0; });
        std::cout << "  relative motion: dx=" << (mv ? mv->dx : 0) << " dy=" << (mv ? mv->dy : 0) << "\n";
        CHECK(mv != nullptr && mv->dx == 12 && mv->dy == 7);
        // The same motion, summed since the last read (what the game's mouse-look uses).
        CHECK(b->SupportsRelativeMouseState());
        int rdx = -1, rdy = -1;
        b->RelativeMouseState(&rdx, &rdy);
        // Pump() above collected the events but the sum is kept by the backend: it must be 12, 7.
        CHECK(rdx == 12 && rdy == 7);
        b->RelativeMouseState(&rdx, &rdy);
        CHECK(rdx == 0 && rdy == 0);   // read resets it
        ev = Inject(*b, "mousemove_relative -- -5 3", 200);
        b->RelativeMouseState(&rdx, &rdy);
        CHECK(rdx == -5 && rdy == 3);
        b->SetRelativeMouse(false);
    } else {
        std::cout << "  relative mouse: not available on this server\n";
    }
    CHECK(b->IsCursorShown());
    b->ShowCursor(false);
    CHECK(!b->IsCursorShown());
    b->ShowCursor(true);
    CHECK(b->IsCursorShown());

    // ---- clipboard ---------------------------------------------------------------------------
    CHECK(b->SetClipboardText("clip \xC3\xA9 1"));
    CHECK(b->GetClipboardText() == "clip \xC3\xA9 1");
#if defined(__linux__)
    if (kind == BackendKind::Own) {
        // Another client reads it through the X selection protocol: a second connection of our
        // own backend, while this one serves requests from a helper thread.
        std::unique_ptr<Backend> other = okay::platform::CreateOwnBackend();
        CHECK(other->Init(InitFlags{true, false, false}));
        WindowDesc d2; d2.width = 50; d2.height = 50; d2.x = 700; d2.y = 500;
        auto w2 = other->MakeWindow(d2);
        CHECK(w2 != nullptr);
        std::atomic<bool> stop{false};
        std::thread server([&] { Event e; while (!stop) { while (b->PollEvent(e)) {} SleepMs(2); } });
        const std::string got = other->GetClipboardText();
        stop = true;
        server.join();
        CHECK(got == "clip \xC3\xA9 1");
        // And the reverse: the other client owns it, we read it.
        CHECK(other->SetClipboardText("from the other client"));
        std::atomic<bool> stop2{false};
        std::thread server2([&] { Event e; while (!stop2) { while (other->PollEvent(e)) {} SleepMs(2); } });
        const std::string got2 = b->GetClipboardText();
        stop2 = true;
        server2.join();
        CHECK(got2 == "from the other client");
        // Other formats by MIME type (Exchange.hpp provider behind the selection): b offers a uri list, the other client reads it.
        CHECK(!b->SetClipboardText("bad \xFF utf8"));                         // not UTF-8: refused, clipboard unchanged
        const std::string uris = "file:///tmp/a%20b.txt\r\nfile:///tmp/c.png\r\n";
        CHECK(b->ClipboardPut("text/uri-list", std::vector<uint8_t>(uris.begin(), uris.end())));
        {
            std::atomic<bool> st{false};
            std::thread srv([&] { Event e; while (!st) { while (b->PollEvent(e)) {} SleepMs(2); } });
            std::vector<uint8_t> d;
            const bool okGet = other->ClipboardFetch("text/uri-list", d);
            const auto fmts = other->ClipboardFormats();
            std::vector<uint8_t> none;
            const bool noText = other->ClipboardFetch("text/plain", none);
            st = true; srv.join();
            CHECK(okGet && std::string(d.begin(), d.end()) == uris);
            bool listed = false; for (auto& f : fmts) if (f == "text/uri-list") listed = true;
            CHECK(listed);
            CHECK(!noText);                                                  // we only offered the uri list
        }
        // And binary data with NUL bytes the other way round (b reads what the other client owns).
        {
            std::vector<uint8_t> blob(3000); for (size_t i = 0; i < blob.size(); i++) blob[i] = (uint8_t)(i * 7);
            CHECK(other->ClipboardPut("image/png", blob));
            std::atomic<bool> st{false};
            std::thread srv([&] { Event e; while (!st) { while (other->PollEvent(e)) {} SleepMs(2); } });
            std::vector<uint8_t> d;
            const bool okGet = b->ClipboardFetch("image/png", d);
            const auto fmts = b->ClipboardFormats();
            st = true; srv.join();
            CHECK(okGet && d == blob);
            bool listed = false; for (auto& f : fmts) if (f == "image/png") listed = true;
            CHECK(listed);
        }
        w2.reset();
        other->Shutdown();
    }
#endif

    // ---- window extras the player needs: fullscreen at creation, icon, refresh rate -------------
    {
        WindowDesc fd; fd.title = "fs"; fd.width = 200; fd.height = 150; fd.fullscreen = true; fd.hidden = true;
        auto fw = b->MakeWindow(fd);
        CHECK(fw != nullptr);
        if (fw) {
            CHECK(fw->IsFullscreen());            // (no window manager here: the flag, not the pixels)
            fw->SetFullscreen(false);
            Pump(*b, 100);
            CHECK(!fw->IsFullscreen());
            fw->SetFullscreen(true);
            Pump(*b, 100);
            CHECK(fw->IsFullscreen());
            fw.reset();
        }
        WindowDesc md; md.title = "max"; md.width = 200; md.height = 150; md.maximized = true; md.resizable = true;
        auto mw = b->MakeWindow(md);
        CHECK(mw != nullptr);
        if (mw) {
            CHECK(!mw->IsFullscreen());
            const uint8_t icon[4 * 4 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
                                             255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
                                             255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
                                             255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
            CHECK(mw->SetIcon(icon, 4, 4));
            CHECK(!mw->SetIcon(nullptr, 4, 4));
            CHECK(!mw->SetIcon(icon, 0, 4));
            const int hz = mw->RefreshRateHz();
            std::cout << "  refresh rate reported: " << hz << " Hz\n";
            CHECK(hz >= 0 && hz < 1000);
            mw.reset();
        }
        CHECK(b->BasePath().size() > 1);
    }
#ifdef OKAY_TEST_HAVE_SDL_HEADERS
    if (kind == BackendKind::Sdl2) {
        // Touch and natural-scrolling events cannot come from XTEST: push them as SDL would.
        SDL_Event fe; SDL_memset(&fe, 0, sizeof(fe));
        fe.type = SDL_FINGERDOWN; fe.tfinger.fingerId = 7; fe.tfinger.x = 0.25f; fe.tfinger.y = 0.75f;
        SDL_PushEvent(&fe);
        fe.type = SDL_FINGERMOTION; fe.tfinger.x = 0.5f; SDL_PushEvent(&fe);
        fe.type = SDL_FINGERUP; SDL_PushEvent(&fe);
        SDL_Event we; SDL_memset(&we, 0, sizeof(we));
        we.type = SDL_MOUSEWHEEL; we.wheel.y = -2; we.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
        SDL_PushEvent(&we);
        we.wheel.y = 3; we.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        SDL_PushEvent(&we);
        ev = Pump(*b, 50);
        const Event* fd0 = Find(ev, EventType::FingerDown);
        CHECK(fd0 && fd0->fingerId == 7 && fd0->fx == 0.25f && fd0->fy == 0.75f);
        CHECK(Find(ev, EventType::FingerMotion, [](const Event& e) { return e.fx == 0.5f; }) != nullptr);
        CHECK(Count(ev, EventType::FingerUp) == 1);
        // Flipped: wheelY is "up = +y" (so +2) and wheelFlipped says the OS value was -2.
        CHECK(Find(ev, EventType::MouseWheel, [](const Event& e) { return e.wheelFlipped && e.wheelY == 2.0f; }) != nullptr);
        CHECK(Find(ev, EventType::MouseWheel, [](const Event& e) { return !e.wheelFlipped && e.wheelY == 3.0f; }) != nullptr);
    }
#endif

    // ---- visibility / focus / destroy -----------------------------------------------------------
    win->Hide();
    Pump(*b, 100);
    win->Show();
    Pump(*b, 100);
    win->DestroyGLContext();
    win.reset();
    b->Shutdown();
    TEST_MAIN_RESULT();
}
