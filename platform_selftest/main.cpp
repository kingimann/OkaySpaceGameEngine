// OkayPlatformSelfTest: a small program that exercises okay::platform on the machine it runs on and writes a
// plain report. It exists so the own Windows backend (never run by the developers on real Windows) can be
// checked by the owner in a few minutes and by the Windows CI job without a person.
//
//   OkayPlatformSelfTest                  interactive: opens a window, asks you to press keys, move the mouse,
//                                         type, scroll, drop a file, use a controller, listens for a tone, and
//                                         writes OkayPlatformSelfTest-report.txt
//   OkayPlatformSelfTest --auto           unattended (CI): no questions; on Windows it injects keys and mouse
//                                         clicks with SendInput; steps that need a person are NOTSEEN, not FAIL
//   --backend own|sdl2                    which okay::platform backend (default: own)
//   --report <file>                       where the report goes
//   --seconds <n>                         length of the "try everything" phase (default 60; 0 = skip)
//   --no-audio, --no-gl                   skip those parts
//
// Result words in the report: PASS (checked and right), FAIL (checked and wrong), SKIP (this machine cannot do
// it: no sound card, no microphone, no GL), NOTSEEN (needs a person and nobody did it), INFO (a value to read).
// The exit code is 1 only when something FAILed.
//
// Written from the okay::platform interface (engine/include/okay/Platform/Platform.hpp). Nothing taken from
// other projects. Off by default: it is a separate program, nothing starts it.
#include "okay/Platform/Platform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <io.h>
#  define OKAY_ST_ISATTY(fd) _isatty(fd)
#  define OKAY_GLCALL __stdcall
#else
#  include <unistd.h>
#  define OKAY_ST_ISATTY(fd) isatty(fd)
#  define OKAY_GLCALL
#endif

using namespace okay::platform;

namespace {

// ---- the report -----------------------------------------------------------------------------------
struct Report {
    std::FILE* file = nullptr;
    std::string path;
    int pass = 0, fail = 0, skip = 0, notseen = 0, info = 0;
    void Open(const std::string& want) {
        std::vector<std::string> tries;
        tries.push_back(want);
        if (Backend* b = Default()) { const std::string p = b->PrefPath("OkaySpace", "platform-selftest"); if (!p.empty()) tries.push_back(p + "OkayPlatformSelfTest-report.txt"); }
        for (const auto& t : tries) { file = std::fopen(t.c_str(), "w"); if (file) { path = t; return; } }
    }
    void Raw(const std::string& s) {
        std::printf("%s\n", s.c_str());
        std::fflush(stdout);
        if (file) { std::fprintf(file, "%s\n", s.c_str()); std::fflush(file); }   // flushed each line: a crash leaves a partial report
    }
    void Line(const char* word, const std::string& name, const std::string& detail) {
        std::string s = std::string("[") + word + "] " + name;
        if (!detail.empty()) s += "  --  " + detail;
        Raw(s);
    }
    void Pass(const std::string& n, const std::string& d = "") { ++pass; Line("PASS", n, d); }
    void Fail(const std::string& n, const std::string& d = "") { ++fail; Line("FAIL", n, d); }
    void Skip(const std::string& n, const std::string& d = "") { ++skip; Line("SKIP", n, d); }
    void NotSeen(const std::string& n, const std::string& d = "") { ++notseen; Line("NOTSEEN", n, d); }
    void Info(const std::string& n, const std::string& d = "") { ++info; Line("INFO", n, d); }
    void Check(bool ok, const std::string& n, const std::string& d = "") { if (ok) Pass(n, d); else Fail(n, d); }
    void Head(const std::string& s) { Raw(""); Raw("== " + s); }
};

struct Options {
    BackendKind kind = BackendKind::Own;
    bool autoRun = false, audio = true, gl = true;
    int seconds = 60;
    std::string report = "OkayPlatformSelfTest-report.txt";
};

Report g_r;
Backend* g_b = nullptr;
Options g_o;

// What the event pump has seen so far.
struct Seen {
    bool key = false, keyUp = false, text = false, move = false, left = false, right = false, wheel = false, dbl = false,
         drop = false, resize = false, focusGained = false, focusLost = false, padAdded = false, padRemoved = false, quit = false, close = false;
    std::string lastText, lastDrop;
    int maxClicks = 0, lastW = 0, lastH = 0, keys = 0, events = 0;
    float wheelY = 0;
    Key lastKey = Key::Unknown;
    bool yes = false, no = false;   // Y / N pressed
};
Seen g_s;

void Feed(const Event& e) {
    ++g_s.events;
    switch (e.type) {
    case EventType::KeyDown:
        g_s.key = true; ++g_s.keys; g_s.lastKey = e.key;
        if (e.key == Key::Y) g_s.yes = true;
        if (e.key == Key::N) g_s.no = true;
        break;
    case EventType::KeyUp: g_s.keyUp = true; break;
    case EventType::TextInput: g_s.text = true; g_s.lastText = e.text; break;
    case EventType::MouseMove: g_s.move = true; break;
    case EventType::MouseButtonDown:
        if (e.button == MouseLeft) g_s.left = true;
        if (e.button == MouseRight) g_s.right = true;
        g_s.maxClicks = std::max(g_s.maxClicks, e.clicks);
        if (e.clicks >= 2) g_s.dbl = true;
        break;
    case EventType::MouseWheel: g_s.wheel = true; g_s.wheelY += e.wheelY; break;
    case EventType::DropFile: g_s.drop = true; g_s.lastDrop = e.text; break;
    case EventType::WindowResized: g_s.resize = true; g_s.lastW = e.width; g_s.lastH = e.height; break;
    case EventType::WindowFocusGained: g_s.focusGained = true; break;
    case EventType::WindowFocusLost: g_s.focusLost = true; break;
    case EventType::GamepadAdded: g_s.padAdded = true; break;
    case EventType::GamepadRemoved: g_s.padRemoved = true; break;
    case EventType::Quit: g_s.quit = true; break;
    case EventType::WindowClose: g_s.close = true; break;
    default: break;
    }
}

/// Poll events for `ms` milliseconds (at least once), feeding the Seen flags.
void Pump(int ms) {
    const uint64_t end = g_b->TicksMs() + (uint64_t)ms;
    do {
        Event e;
        while (g_b->PollEvent(e)) Feed(e);
        g_b->DelayMs(1);
    } while (g_b->TicksMs() < end);
}
template <class F> bool WaitFor(int ms, F cond) {
    const uint64_t end = g_b->TicksMs() + (uint64_t)ms;
    do { Pump(0); if (cond()) return true; g_b->DelayMs(2); } while (g_b->TicksMs() < end);
    return cond();
}

// ---- a tiny software frame, so the window shows something and PresentPixels is exercised -----------------------
struct Frame {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    void Reset(int W, int H, uint8_t r, uint8_t g, uint8_t b) {
        w = W; h = H; px.assign((size_t)w * h * 4, 255);
        for (size_t i = 0; i < (size_t)w * h; ++i) { px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; }
    }
    void Rect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
        x0 = std::max(0, x0); y0 = std::max(0, y0); x1 = std::min(w, x1); y1 = std::min(h, y1);
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { uint8_t* p = &px[((size_t)y * w + x) * 4]; p[0] = r; p[1] = g; p[2] = b; p[3] = 255; }
    }
};

std::unique_ptr<Window> g_win;
Frame g_frame;

// The twelve things the "try everything" phase looks for, drawn as squares: grey = not seen yet, green = seen.
struct Item { const char* name; bool Seen::*flag; const char* how; };
const Item kItems[] = {
    {"key down", &Seen::key, "press any key"},
    {"key up", &Seen::keyUp, "release a key"},
    {"text input", &Seen::text, "type a letter (text arrives as characters)"},
    {"mouse move", &Seen::move, "move the mouse over the window"},
    {"left click", &Seen::left, "click the left button in the window"},
    {"right click", &Seen::right, "click the right button in the window"},
    {"double click", &Seen::dbl, "double-click in the window"},
    {"mouse wheel", &Seen::wheel, "turn the mouse wheel over the window"},
    {"resize", &Seen::resize, "drag a window edge to resize it"},
    {"focus lost", &Seen::focusLost, "click another window"},
    {"focus gained", &Seen::focusGained, "click this window again"},
    {"file drop", &Seen::drop, "drag any file from Explorer onto the window"},
};
constexpr int kItemCount = (int)(sizeof(kItems) / sizeof(kItems[0]));

void Draw(int pulse) {
    if (!g_win) return;
    int w = 0, h = 0;
    g_win->GetSize(w, h);
    if (w < 8 || h < 8) return;
    g_frame.Reset(w, h, 24, 26, 34);
    const int cell = std::max(8, std::min(w / (kItemCount + 1), 48));
    for (int i = 0; i < kItemCount; ++i) {
        const bool on = g_s.*(kItems[i].flag);
        g_frame.Rect(8 + i * (cell + 4), 8, 8 + i * (cell + 4) + cell, 8 + cell, on ? 60 : 70, on ? 200 : 70, on ? 90 : 75);
    }
    int mx = 0, my = 0;
    const uint32_t buttons = g_b->MouseState(&mx, &my);
    g_frame.Rect(mx - 12, my - 1, mx + 13, my + 2, 255, buttons ? 80 : 220, 60);
    g_frame.Rect(mx - 1, my - 12, mx + 2, my + 13, 255, buttons ? 80 : 220, 60);
    g_frame.Rect(8, h - 24, 8 + (pulse % 64) * 4, h - 8, 90, 140, 230);   // proves frames keep coming
    g_win->PresentPixels(g_frame.px.data(), g_frame.w, g_frame.h);
}

// ---- steps --------------------------------------------------------------------------------------------------
std::string Num(double v) { char b[64]; std::snprintf(b, sizeof(b), "%.4g", v); return b; }

void StepSystem() {
    g_r.Head("1. The platform");
    g_r.Info("backend", g_b->Name());
    g_r.Info("compiled in", std::string(IsCompiledIn(BackendKind::Sdl2) ? "sdl2 " : "") + (IsCompiledIn(BackendKind::Own) ? "own" : ""));
    g_r.Info("default backend of this build", BackendName(DefaultKind()));
    const char* os = std::getenv("OS");
    const char* arch = std::getenv("PROCESSOR_ARCHITECTURE");
    g_r.Info("system", std::string(os ? os : "(OS variable not set)") + " " + (arch ? arch : ""));
    const std::time_t t = std::time(nullptr);
    char tb[64]; std::strftime(tb, sizeof(tb), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    g_r.Info("time", tb);
    InitFlags f;
    const bool ok = g_b->Init(f);
    g_r.Check(ok, "Init(video, audio, gamepad)", ok ? "" : g_b->LastError());
    g_r.Check(g_b->Init(f), "Init twice is safe");
    g_r.Info("base path", g_b->BasePath());
    const std::string pref = g_b->PrefPath("OkaySpace", "platform-selftest");
    g_r.Check(!pref.empty() && pref.back() != '\0', "PrefPath creates a user folder", pref);
    std::string loc;
    for (const auto& l : g_b->PreferredLocales()) loc += l + " ";
    g_r.Info("preferred languages", loc.empty() ? "(none)" : loc);
    g_r.Check(g_b->BasePath().size() > 1, "BasePath is not empty");
}

void StepTimers() {
    g_r.Head("2. Timers");
    const uint64_t a = g_b->TicksMs(), pa = g_b->PerfCounter();
    g_b->DelayMs(30);
    const uint64_t b = g_b->TicksMs(), pb = g_b->PerfCounter();
    const double elapsed = (double)(b - a);
    g_r.Check(elapsed >= 25 && elapsed <= 250, "DelayMs(30) took about 30 ms", Num(elapsed) + " ms");
    const uint64_t freq = g_b->PerfFrequency();
    g_r.Check(freq > 0, "PerfFrequency is not zero", std::to_string(freq) + " Hz");
    const double perfMs = freq ? (double)(pb - pa) * 1000.0 / (double)freq : 0;
    g_r.Check(std::fabs(perfMs - elapsed) < 40, "PerfCounter agrees with TicksMs", Num(perfMs) + " ms vs " + Num(elapsed) + " ms");
    bool mono = true;
    uint64_t prev = g_b->TicksMs();
    uint64_t t0 = g_b->PerfCounter();
    for (int i = 0; i < 40; ++i) { g_b->DelayMs(1); const uint64_t n = g_b->TicksMs(); if (n < prev) mono = false; prev = n; }
    const uint64_t t1 = g_b->PerfCounter();
    g_r.Check(mono, "TicksMs never goes backwards");
    g_r.Info("DelayMs(1) really takes", Num(freq ? (double)(t1 - t0) * 1000.0 / (double)freq / 40.0 : 0) + " ms each (about 1 to 2 ms is good; 15.6 ms means the 1 ms timer period did not take)");
}

void StepWindow() {
    g_r.Head("3. Window, resize, title, DPI");
    WindowDesc d;
    d.title = "Okay platform self-test";
    d.width = 640; d.height = 420; d.resizable = true;
    g_win = g_b->MakeWindow(d);
    if (!g_win) { g_r.Fail("create a 640x420 window", g_b->LastError()); return; }
    g_r.Pass("create a 640x420 window");
    int w = 0, h = 0;
    g_win->GetSize(w, h);
    g_r.Check(w == 640 && h == 420, "client size is 640x420", std::to_string(w) + "x" + std::to_string(h));
    int x = 0, y = 0;
    g_win->GetPosition(x, y);
    g_r.Info("window position", std::to_string(x) + "," + std::to_string(y));
    g_win->SetTitle(std::string("Okay platform self-test ") + "\xC3\xA4\xE2\x82\xAC");   // non-ASCII title must not crash
    g_r.Pass("title with non-ASCII characters accepted");
    g_win->Show(); g_win->Raise();
    Pump(300);
    g_s.resize = false;
    g_win->SetSize(700, 500);
    WaitFor(1500, [] { return g_s.resize; });
    g_win->GetSize(w, h);
    g_r.Check(w == 700 && h == 500, "SetSize(700, 500) gives a 700x500 client area", std::to_string(w) + "x" + std::to_string(h));
    g_r.Check(g_s.resize && g_s.lastW == 700 && g_s.lastH == 500, "a WindowResized event carries 700x500", std::to_string(g_s.lastW) + "x" + std::to_string(g_s.lastH));
    int dw = 0, dh = 0;
    g_win->GetDrawableSize(dw, dh);
    g_r.Info("drawable size", std::to_string(dw) + "x" + std::to_string(dh));
    g_r.Info("content scale (1.0 = 96 DPI)", Num(g_win->ContentScale()));
    g_r.Info("refresh rate (0 = unknown)", std::to_string(g_win->RefreshRateHz()) + " Hz");
    g_r.Info("SetPerMonitorDpiAware (call before the first window; true = applied)", g_b->SetPerMonitorDpiAware() ? "true" : "false");
    g_win->SetMinimumSize(320, 240);
    g_r.Pass("SetMinimumSize accepted");
    // Pixels on screen: a frame in four colours; the person (or nobody in CI) can look at it.
    g_frame.Reset(700, 500, 20, 20, 20);
    g_frame.Rect(0, 0, 350, 250, 220, 30, 30); g_frame.Rect(350, 0, 700, 250, 30, 200, 30);
    g_frame.Rect(0, 250, 350, 500, 30, 30, 220); g_frame.Rect(350, 250, 700, 500, 230, 200, 30);
    const bool pres = g_win->PresentPixels(g_frame.px.data(), g_frame.w, g_frame.h);
    if (g_b->Kind() == BackendKind::Own) g_r.Check(pres, "PresentPixels shows a software frame", "the window should now show red, green, blue and yellow quarters");
    else g_r.Skip("PresentPixels", "the sdl2 backend draws through SDL_Renderer, not pixels");
    Pump(200);
    std::vector<uint8_t> icon(32 * 32 * 4);
    for (int i = 0; i < 32 * 32; ++i) { icon[i * 4] = (uint8_t)(i % 32 * 8); icon[i * 4 + 1] = (uint8_t)(i / 32 * 8); icon[i * 4 + 2] = 128; icon[i * 4 + 3] = 255; }
    const bool ic = g_win->SetIcon(icon.data(), 32, 32);
    g_r.Info("SetIcon (a 32x32 gradient)", ic ? "accepted: look at the title bar and task bar" : "not supported by this backend");
#ifdef _WIN32
    // Fullscreen only means something on a real desktop (Xvfb has no window manager).
    const bool wasFull = g_win->IsFullscreen();
    g_win->SetFullscreen(true);
    Pump(500);
    int fw = 0, fh = 0;
    g_win->GetSize(fw, fh);
    g_r.Check(g_win->IsFullscreen() && fw >= 640 && fh >= 480, "fullscreen covers the monitor", std::to_string(fw) + "x" + std::to_string(fh));
    g_win->SetFullscreen(false);
    Pump(500);
    g_win->GetSize(w, h);
    g_r.Check(!g_win->IsFullscreen() && !wasFull && w == 700 && h == 500, "leaving fullscreen restores 700x500", std::to_string(w) + "x" + std::to_string(h));
#else
    g_r.Skip("fullscreen", "needs a window manager (this run has none)");
#endif
    g_r.Check(g_win->IsFocused() || !g_s.focusGained, "focus state is consistent", g_win->IsFocused() ? "window has focus" : "window has no focus");
}

typedef void (OKAY_GLCALL *GlClearColorFn)(float, float, float, float);
typedef void (OKAY_GLCALL *GlClearFn)(unsigned);
typedef void (OKAY_GLCALL *GlReadPixelsFn)(int, int, int, int, unsigned, unsigned, void*);
typedef const unsigned char* (OKAY_GLCALL *GlGetStringFn)(unsigned);
typedef void (OKAY_GLCALL *GlFinishFn)();

void StepGl() {
    g_r.Head("4. OpenGL context");
    if (!g_o.gl) { g_r.Skip("OpenGL", "--no-gl"); return; }
    WindowDesc d; d.title = "gl"; d.width = 64; d.height = 64; d.opengl = true; d.hidden = true; d.resizable = false;
    auto glw = g_b->MakeWindow(d);
    if (!glw) { g_r.Skip("OpenGL window", g_b->LastError()); return; }
    GLAttributes a;
    if (!glw->CreateGLContext(a)) { g_r.Skip("OpenGL context", "no OpenGL here (a remote or headless desktop): " + g_b->LastError()); return; }
    g_r.Pass("create a GL context (3.2 compatibility asked for; older is accepted)");
    g_r.Check(glw->MakeCurrent(), "MakeCurrent");
    auto clearColor = (GlClearColorFn)g_b->GLProcAddress("glClearColor");
    auto clear = (GlClearFn)g_b->GLProcAddress("glClear");
    auto readPixels = (GlReadPixelsFn)g_b->GLProcAddress("glReadPixels");
    auto getString = (GlGetStringFn)g_b->GLProcAddress("glGetString");
    auto finish = (GlFinishFn)g_b->GLProcAddress("glFinish");
    g_r.Check(clearColor && clear && readPixels && getString, "GLProcAddress finds the core functions");
    if (getString) {
        const unsigned char* v = getString(0x1F02), *r = getString(0x1F01), *rd = getString(0x1F00);
        g_r.Info("GL_VERSION", v ? (const char*)v : "(null)");
        g_r.Info("GL_RENDERER", rd ? (const char*)rd : "(null)");
        g_r.Info("GL_VENDOR", r ? (const char*)r : "(null)");
    }
    if (clearColor && clear && readPixels) {
        clearColor(0.2f, 0.4f, 0.6f, 1.0f);
        clear(0x4000);   // GL_COLOR_BUFFER_BIT
        if (finish) finish();
        unsigned char px[4] = {0, 0, 0, 0};
        readPixels(10, 10, 1, 1, 0x1908, 0x1401, px);   // GL_RGBA, GL_UNSIGNED_BYTE
        const bool ok = std::abs((int)px[0] - 51) <= 3 && std::abs((int)px[1] - 102) <= 3 && std::abs((int)px[2] - 153) <= 3;
        g_r.Check(ok, "the clear colour reads back (51,102,153)", std::to_string(px[0]) + "," + std::to_string(px[1]) + "," + std::to_string(px[2]));
    }
    glw->SwapBuffers();
    g_r.Pass("SwapBuffers did not crash");
    g_r.Info("SetSwapInterval(1)", glw->SetSwapInterval(1) ? "accepted" : "refused (no vsync extension)");
    g_b->ReleaseCurrent();
    glw->DestroyGLContext();
    g_r.Pass("destroy the GL context");
}

void StepClipboard() {
    g_r.Head("5. Clipboard");
    const std::string sample = "Okay \xC3\xA4\xE2\x82\xAC \xF0\x9F\x98\x80 line one\nline two\n\nend";
    const bool set = g_b->SetClipboardText(sample);
    g_r.Check(set, "SetClipboardText accepted text with accents, euro, emoji and line breaks");
    g_r.Check(g_b->GetClipboardText() == sample, "GetClipboardText returns exactly that text (line breaks stay LF)");
    std::string big(200000, 'x');
    g_r.Check(g_b->SetClipboardText(big) && g_b->GetClipboardText() == big, "a 200,000 character text round-trips");
    g_r.Check(g_b->SetClipboardText("") && g_b->GetClipboardText().empty(), "empty text round-trips");
    g_b->SetClipboardText("Okay clipboard test: paste me into Notepad");
    g_r.Info("by hand", "the clipboard now holds 'Okay clipboard test: paste me into Notepad' - paste it somewhere to confirm");
}

struct ToneState { double phase = 0; volatile long calls = 0; volatile long bytes = 0; int channels = 2; int freq = 48000; double hz = 440; };
void ToneCb(void* user, uint8_t* stream, int len) {
    ToneState* t = static_cast<ToneState*>(user);
    int16_t* s = reinterpret_cast<int16_t*>(stream);
    const int frames = len / (2 * t->channels);
    for (int i = 0; i < frames; ++i) {
        const int16_t v = (int16_t)(std::sin(t->phase) * 6000.0);
        t->phase += 6.283185307179586 * t->hz / t->freq;
        if (t->phase > 6.283185307179586) t->phase -= 6.283185307179586;
        for (int c = 0; c < t->channels; ++c) s[i * t->channels + c] = v;
    }
    t->calls = t->calls + 1;
    t->bytes = t->bytes + len;
}
struct CapState { volatile long calls = 0; volatile long bytes = 0; };
void CapCb(void* user, uint8_t*, int len) { CapState* c = static_cast<CapState*>(user); c->calls = c->calls + 1; c->bytes = c->bytes + len; }

void StepAudio() {
    g_r.Head("6. Audio");
    if (!g_o.audio) { g_r.Skip("audio", "--no-audio"); return; }
    const int outs = g_b->AudioDeviceCount(false), ins = g_b->AudioDeviceCount(true);
    g_r.Info("playback devices", std::to_string(outs));
    for (int i = 0; i < outs && i < 8; ++i) g_r.Info("  playback " + std::to_string(i), g_b->AudioDeviceName(i, false));
    g_r.Info("capture devices", std::to_string(ins));
    for (int i = 0; i < ins && i < 8; ++i) g_r.Info("  capture " + std::to_string(i), g_b->AudioDeviceName(i, true));

    ToneState tone;
    AudioSpec want; want.freq = 48000; want.channels = 2; want.format = SampleFormat::S16; want.frames = 1024; want.callback = ToneCb; want.user = &tone;
    AudioSpec got;
    auto dev = g_b->OpenAudio(want, false, nullptr, &got);
    if (!dev) { g_r.Skip("open the default playback device", g_b->LastError() + " (no sound card or no audio service?)"); }
    else {
        g_r.Pass("open the default playback device (callback mode)", "got " + std::to_string(got.freq) + " Hz, " + std::to_string(got.channels) + " channel(s), " + std::to_string(got.frames) + " frames");
        tone.channels = got.channels; tone.freq = got.freq;
        const uint64_t t0 = g_b->TicksMs();
        dev->Pause(false);
        Pump(g_o.autoRun ? 700 : 1500);
        const double secs = (double)(g_b->TicksMs() - t0) / 1000.0;
        dev->Pause(true);
        const double expect = secs * got.freq * got.channels * 2;
        g_r.Check(tone.calls > 3, "the device asks for audio again and again", std::to_string(tone.calls) + " callbacks");
        g_r.Check(tone.bytes > expect * 0.4 && tone.bytes < expect * 1.8 + 65536, "it consumes about real time worth of audio", std::to_string(tone.bytes) + " bytes in " + Num(secs) + " s (expected about " + Num(expect) + ")");
        const long afterPause = tone.calls;
        Pump(300);
        g_r.Check(tone.calls - afterPause <= tone.calls / 2 + 12, "Pause(true) stops the callbacks within a few buffers", std::to_string(tone.calls - afterPause) + " more");
        g_r.Check(!dev->Queue("x", 1), "Queue is refused in callback mode");
        if (!g_o.autoRun) {
            g_r.Raw("   A 440 Hz tone will play for 2 seconds. Listen for it, then press Y (heard it) or N (heard nothing) in the window.");
            g_s.yes = g_s.no = false;
            dev->Pause(false);
            Pump(2000);
            dev->Pause(true);
            g_r.Raw("   Did you hear the tone? Press Y or N in the window (15 seconds).");
            WaitFor(15000, [] { return g_s.yes || g_s.no; });
            if (g_s.yes) g_r.Pass("you heard the callback tone");
            else if (g_s.no) g_r.Fail("you did not hear the callback tone", "check the volume and the default playback device; the callback counts above say whether the device ran");
            else g_r.NotSeen("hearing the callback tone", "no answer");
        }
        dev.reset();
        g_r.Pass("close the device without hanging");
    }

    // Queue mode: queue 0.4 s of tone, the queue must drain.
    ToneState q; q.channels = 2;
    AudioSpec qw; qw.freq = 44100; qw.channels = 2; qw.format = SampleFormat::S16; qw.frames = 512;
    auto qd = g_b->OpenAudio(qw, false, nullptr, &got);
    if (!qd) g_r.Skip("open a playback device in queue mode", g_b->LastError());
    else {
        std::vector<uint8_t> buf((size_t)(0.4 * 44100) * 4);
        q.freq = got.freq; q.channels = got.channels; q.hz = 660;
        ToneCb(&q, buf.data(), (int)buf.size());
        g_r.Check(qd->Queue(buf.data(), (uint32_t)buf.size()), "Queue accepts 0.4 s of audio");
        g_r.Check(qd->QueuedBytes() == buf.size(), "QueuedBytes counts what was queued (device paused)", std::to_string(qd->QueuedBytes()));
        qd->Pause(false);
        const bool drained = WaitFor(3000, [&] { return qd->QueuedBytes() == 0; });
        g_r.Check(drained, "the queue drains while the device plays", std::to_string(qd->QueuedBytes()) + " bytes left");
        qd->Pause(true);
        qd->Queue(buf.data(), (uint32_t)buf.size());
        qd->ClearQueued();
        g_r.Check(qd->QueuedBytes() == 0, "ClearQueued empties the queue");
        g_r.Check(qd->Dequeue(buf.data(), 16) == 0, "Dequeue on a playback device returns nothing");
    }

    // Capture: a microphone may not exist.
    CapState cap;
    AudioSpec cw; cw.freq = 16000; cw.channels = 1; cw.format = SampleFormat::S16; cw.frames = 512; cw.callback = CapCb; cw.user = &cap;
    auto cd = g_b->OpenAudio(cw, true, nullptr, &got);
    if (!cd) g_r.Skip("open the default capture device", g_b->LastError() + " (no microphone, or access to it is blocked)");
    else {
        g_r.Pass("open the default capture device", "got " + std::to_string(got.freq) + " Hz, " + std::to_string(got.channels) + " channel(s)");
        cd->Pause(false);
        Pump(600);
        cd->Pause(true);
        g_r.Check(cap.bytes > 0, "the microphone delivers audio", std::to_string(cap.bytes) + " bytes in " + std::to_string(cap.calls) + " callbacks");
        cd.reset();
        g_r.Pass("close the capture device");
    }
    AudioSpec bad; bad.freq = 1000; bad.channels = 9;
    auto bd = g_b->OpenAudio(bad, false, nullptr, nullptr);
    g_r.Info("a nonsense format (1000 Hz, 9 channels)", bd ? "accepted by this backend" : "refused: " + g_b->LastError());
    g_r.Pass("a bad format did not crash");
}

void StepGamepad() {
    g_r.Head("7. Game controllers");
    Pump(1200);   // hot-plug scanning runs about once a second
    const int n = g_b->GamepadDeviceCount();
    g_r.Info("controllers connected", std::to_string(n));
    g_r.Info("GamepadAdded events seen", g_s.padAdded ? "yes" : "no");
    if (n > 0) g_r.Check(g_s.padAdded, "a connected controller was announced with a GamepadAdded event");
    if (n <= 0) { g_r.Skip("controller input and rumble", "no controller connected: plug in an Xbox-style controller and run again"); return; }
    auto pad = g_b->OpenGamepad(0);
    if (!pad) { g_r.Fail("open controller 0", g_b->LastError()); return; }
    g_r.Pass("open controller 0", pad->Name());
    g_r.Info("instance id", std::to_string(pad->InstanceId()));
    const int16_t lx = pad->Axis(GamepadAxis::LeftX), ly = pad->Axis(GamepadAxis::LeftY), lt = pad->Axis(GamepadAxis::TriggerLeft);
    g_r.Info("axes at rest: left X, left Y, left trigger", std::to_string(lx) + ", " + std::to_string(ly) + ", " + std::to_string(lt));
    g_r.Check(lt >= 0, "triggers are never negative");
    if (g_o.autoRun) { g_r.NotSeen("moving sticks and pressing buttons", "needs a person"); }
    else {
        g_r.Raw("   For 8 seconds: move both sticks, squeeze both triggers, press A, B, X, Y, the shoulders and the D-pad. Up on the left stick must read negative.");
        bool moved = false, up = false, btn = false, trig = false;
        const uint64_t end = g_b->TicksMs() + 8000;
        while (g_b->TicksMs() < end) {
            Pump(10);
            if (std::abs(pad->Axis(GamepadAxis::LeftX)) > 8000 || std::abs(pad->Axis(GamepadAxis::RightX)) > 8000 || std::abs(pad->Axis(GamepadAxis::RightY)) > 8000) moved = true;
            if (pad->Axis(GamepadAxis::LeftY) < -16000) up = true;
            if (pad->Axis(GamepadAxis::TriggerLeft) > 8000 || pad->Axis(GamepadAxis::TriggerRight) > 8000) trig = true;
            for (int b = 0; b <= (int)GamepadButton::DpadRight; ++b) if (pad->Button((GamepadButton)b)) btn = true;
        }
        moved ? g_r.Pass("a stick moved") : g_r.NotSeen("a stick moved");
        up ? g_r.Pass("pushing the left stick up reads negative (+y is down)") : g_r.NotSeen("left stick pushed up");
        trig ? g_r.Pass("a trigger reads above 8000") : g_r.NotSeen("a trigger squeezed");
        btn ? g_r.Pass("a button read as pressed") : g_r.NotSeen("a button pressed");
    }
    const bool rumbled = pad->Rumble(40000, 40000, 400);
    g_r.Info("Rumble(40000, 40000, 400 ms)", rumbled ? "started: the controller should buzz for about 0.4 s and then stop by itself" : "refused (controller cannot rumble, or wired wrongly)");
    Pump(900);
    g_r.Info("rumble stopped", "if it kept buzzing after about half a second, the timer is broken: tell us");
    pad.reset();
}

#ifdef _WIN32
// Windows only: drive the window with the system's own input injection, so the events the backend produces can be
// compared with what was sent. Needs the window to be in front; if the desktop will not give it focus, the steps are SKIPped.
void SendKey(WORD scan, bool ext, bool up) {
    INPUT in; ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = scan;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (ext ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
    SendInput(1, &in, sizeof(in));
}
void SendMouse(DWORD flags, DWORD data = 0) {
    INPUT in; ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE; in.mi.dwFlags = flags; in.mi.mouseData = data;
    SendInput(1, &in, sizeof(in));
}
void StepInjected() {
    g_r.Head("8. Keyboard and mouse (injected by this program, Windows only)");
    g_win->Raise();
    WaitFor(1500, [] { return g_win->IsFocused(); });
    if (!g_win->IsFocused()) { g_r.Skip("injected input", "this window could not get the keyboard focus (a locked screen, a remote session or another program in front)"); return; }
    g_b->StartTextInput();
    g_s = Seen();
    SendKey(0x1E, false, false);   // A, set-1 make code 0x1E
    WaitFor(500, [] { return g_s.key; });
    SendKey(0x1E, false, true);
    WaitFor(500, [] { return g_s.keyUp; });
    g_r.Check(g_s.key && g_s.lastKey == Key::A, "a pressed A key arrives as KeyDown Key::A", g_s.key ? "key number " + std::to_string((int)g_s.lastKey) : "no key event");
    g_r.Check(g_s.keyUp, "its release arrives as KeyUp");
    g_r.Check(g_s.text && g_s.lastText.size() == 1 && (g_s.lastText[0] == 'a' || g_s.lastText[0] == 'q'), "typing A produces a text event", "text: '" + g_s.lastText + "'");
    SendKey(0x48, true, false);   // extended Up arrow
    WaitFor(500, [] { return g_s.lastKey == Key::Up; });
    SendKey(0x48, true, true);
    g_r.Check(g_s.lastKey == Key::Up, "the extended Up arrow is Key::Up (not keypad 8)");
    g_b->StopTextInput();

    int cx = 0, cy = 0;
    g_win->GetSize(cx, cy);
    POINT p = {cx / 2, cy / 2};
    ClientToScreen((HWND)g_win->NativeHandle(), &p);
    SetCursorPos(p.x, p.y);
    g_s = Seen();
    SendMouse(MOUSEEVENTF_LEFTDOWN); SendMouse(MOUSEEVENTF_LEFTUP);
    WaitFor(500, [] { return g_s.left; });
    g_r.Check(g_s.left, "an injected left click arrives as MouseButtonDown, button 1");
    g_b->DelayMs(60);
    SendMouse(MOUSEEVENTF_LEFTDOWN); SendMouse(MOUSEEVENTF_LEFTUP);
    Pump(200);
    g_r.Check(g_s.maxClicks >= 2, "a second quick click counts as a double click", "clicks = " + std::to_string(g_s.maxClicks));
    g_s.right = false;
    SendMouse(MOUSEEVENTF_RIGHTDOWN); SendMouse(MOUSEEVENTF_RIGHTUP);
    WaitFor(500, [] { return g_s.right; });
    g_r.Check(g_s.right, "an injected right click arrives");
    SendMouse(MOUSEEVENTF_WHEEL, (DWORD)WHEEL_DELTA);
    WaitFor(500, [] { return g_s.wheel; });
    g_r.Check(g_s.wheel && std::fabs(g_s.wheelY - 1.0f) < 0.01f, "one wheel notch up is wheelY = +1", "wheelY = " + Num(g_s.wheelY));
    g_s.move = false;
    SetCursorPos(p.x + 20, p.y + 10);
    WaitFor(500, [] { return g_s.move; });
    g_r.Check(g_s.move, "moving the cursor arrives as MouseMove");
    int mx = 0, my = 0;
    g_b->MouseState(&mx, &my);
    g_r.Check(mx == cx / 2 + 20 && my == cy / 2 + 10, "MouseState reports the position in window coordinates", std::to_string(mx) + "," + std::to_string(my));

    // A key held while the window loses focus must be reported as released.
    g_s = Seen();
    SendKey(0x11, false, false);   // W held
    WaitFor(500, [] { return g_s.key; });
    g_r.Check(g_b->IsKeyDown(Key::W), "IsKeyDown(W) is true while W is held");
    SendKey(0x11, false, true);
    Pump(100);
    g_r.Check(!g_b->IsKeyDown(Key::W), "IsKeyDown(W) is false after release");
    g_r.Check(g_b->KeyForChar('w') == Key::W, "KeyForChar('w') is the W key on this layout", "key number " + std::to_string((int)g_b->KeyForChar('w')));
}
#else
void StepInjected() {
    g_r.Head("8. Keyboard and mouse (injected)");
    g_r.Skip("injected input", "no input injector on this system; the Linux tests (platform_window_own) drive a real X server with xdotool instead");
}
#endif

void StepFree() {
    g_r.Head("9. Try everything (you)");
    if (g_o.autoRun || g_o.seconds <= 0) { g_r.Skip("hands-on phase", g_o.autoRun ? "--auto" : "--seconds 0"); return; }
    if (!g_win) { g_r.Skip("hands-on phase", "no window"); return; }
    g_r.Raw("   The window shows " + std::to_string(kItemCount) + " grey squares. Each turns green when its event arrives. Do these, in any order (Esc finishes early):");
    for (int i = 0; i < kItemCount; ++i) g_r.Raw("     - " + std::string(kItems[i].name) + ": " + kItems[i].how);
    g_b->StartTextInput();
    g_win->Raise();
    g_s.key = g_s.keyUp = g_s.text = g_s.move = g_s.left = g_s.right = g_s.dbl = g_s.wheel = g_s.drop = g_s.resize = g_s.focusLost = g_s.focusGained = false;
    const uint64_t end = g_b->TicksMs() + (uint64_t)g_o.seconds * 1000;
    int pulse = 0;
    bool esc = false;
    bool announced[kItemCount] = {};
    while (g_b->TicksMs() < end && !esc && !g_s.quit && !g_s.close) {
        Event e;
        while (g_b->PollEvent(e)) { Feed(e); if (e.type == EventType::KeyDown && e.key == Key::Escape) esc = true; }
        for (int i = 0; i < kItemCount; ++i)
            if ((g_s.*(kItems[i].flag)) && !announced[i]) {
                announced[i] = true;
                std::string extra = i == 2 ? " '" + g_s.lastText + "'" : i == 11 ? " '" + g_s.lastDrop + "'" : i == 8 ? " " + std::to_string(g_s.lastW) + "x" + std::to_string(g_s.lastH) : i == 6 ? "" : "";
                g_r.Raw(std::string("   seen: ") + kItems[i].name + extra);
            }
        Draw(pulse++);
        g_b->DelayMs(16);
    }
    g_b->StopTextInput();
    for (int i = 0; i < kItemCount; ++i)
        if (g_s.*(kItems[i].flag)) g_r.Pass(std::string("you did: ") + kItems[i].name);
        else g_r.NotSeen(std::string("you did: ") + kItems[i].name, kItems[i].how);
    g_r.Info("most clicks in a row", std::to_string(g_s.maxClicks));
    g_r.Info("wheel total", Num(g_s.wheelY) + " notches (up is positive)");
}

void Usage() {
    std::printf("OkayPlatformSelfTest [--auto] [--backend own|sdl2] [--report file] [--seconds n] [--no-audio] [--no-gl]\n"
                "Runs okay::platform checks on this machine and writes a report. See docs/try_it.md.\n");
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--auto") g_o.autoRun = true;
        else if (a == "--no-audio") g_o.audio = false;
        else if (a == "--no-gl") g_o.gl = false;
        else if (a == "--backend" && i + 1 < argc) { if (!ParseBackendName(argv[++i], g_o.kind)) { Usage(); return 2; } }
        else if (a == "--report" && i + 1 < argc) g_o.report = argv[++i];
        else if (a == "--seconds" && i + 1 < argc) g_o.seconds = std::atoi(argv[++i]);
        else if (a == "-h" || a == "--help") { Usage(); return 0; }
        else { Usage(); return 2; }
    }
    g_b = Get(g_o.kind);
    if (!g_b) { std::printf("backend '%s' is not compiled into this build\n", BackendName(g_o.kind)); return 2; }
    g_r.Open(g_o.report);
    g_r.Raw("Okay platform self-test (backend: " + std::string(g_b->Name()) + (g_o.autoRun ? ", unattended" : ", interactive") + ")");
    g_r.Raw("Report file: " + (g_r.file ? g_r.path : std::string("(could not be written)")));

    StepSystem();
    StepTimers();
    StepWindow();
    StepGl();
    StepClipboard();
    StepAudio();
    StepGamepad();
    if (g_win) { if (g_o.autoRun) StepInjected(); else g_r.Skip("injected input", "interactive run: you do the input in step 9"); }
    StepFree();

    g_win.reset();
    g_b->Shutdown();
    g_r.Head("Summary");
    g_r.Raw(std::to_string(g_r.pass) + " PASS, " + std::to_string(g_r.fail) + " FAIL, " + std::to_string(g_r.skip) + " SKIP, " +
            std::to_string(g_r.notseen) + " NOTSEEN, " + std::to_string(g_r.info) + " INFO");
    g_r.Raw(g_r.fail ? "RESULT: FAIL - send us this report file" : "RESULT: no failures (SKIP and NOTSEEN lines are things this run could not check)");
    if (g_r.file) { std::fclose(g_r.file); g_r.file = nullptr; }
    if (!g_o.autoRun && OKAY_ST_ISATTY(0)) { std::printf("\nPress Enter to close.\n"); std::fflush(stdout); std::getchar(); }
    return g_r.fail ? 1 : 0;
}
