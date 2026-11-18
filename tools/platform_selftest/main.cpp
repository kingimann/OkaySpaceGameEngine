// okay-platform-selftest: tries our own platform backend (okay::platform, Backend "own") on this PC and
// says plainly what worked. It uses ONLY okay::platform (no SDL, no engine). Run it, move the mouse, press
// keys, scroll, click, plug in a pad, listen for a 440 Hz tone. It prints a summary and saves it to
// okay-platform-selftest.txt next to the program.
//
// What it draws: a software frame (colour bars, a moving square, a cross at the mouse) shown with
// Window::PresentPixels (X11 XPutImage / Win32 StretchDIBits). A second, hidden window makes a GL
// context and asks for the GL version string; nothing is drawn with GL.
//
// Options: --seconds N (interactive time, default 20), --auto (no waiting for you: 2 s, no pause, no
// "press Enter"), --no-audio, --no-pause, --sdl2 (run the same checks on the SDL2 backend to compare).
// Exit code: 0 nothing failed, 1 something failed, 2 the backend could not start.
#include "SelfTestLogic.hpp"
#include "okay/Platform/Platform.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define OKAY_GLCALL __stdcall
#else
#define OKAY_GLCALL
#endif

using namespace okay;
using namespace okay::platform;
using namespace okay::selftest;

namespace {

Report g_rep;
EventTally g_tally;

std::string Num(double v, const char* f = "%.1f") { char b[48]; std::snprintf(b, sizeof b, f, v); return b; }

void Draw(std::vector<uint8_t>& px, int w, int h, uint64_t t, int mx, int my, bool mouseIn) {
    static const uint8_t bars[8][3] = {{235,235,235},{235,235,40},{40,235,235},{40,235,40},{235,40,235},{235,40,40},{40,40,235},{30,30,30}};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &px[((size_t)y * w + x) * 4];
            if (y < h * 2 / 3) { const uint8_t* b = bars[x * 8 / w]; p[0] = b[0]; p[1] = b[1]; p[2] = b[2]; }
            else { const uint8_t g = (uint8_t)(x * 255 / (w > 1 ? w - 1 : 1)); p[0] = p[1] = p[2] = g; }
            p[3] = 255;
        }
    const int sz = 40, span = w > sz ? w - sz : 1;
    const int sx = (int)((t / 4) % (uint64_t)(2 * span));
    const int px0 = sx < span ? sx : 2 * span - sx, py0 = h * 2 / 3 + 10;
    for (int y = py0; y < py0 + sz && y < h; ++y)
        for (int x = px0; x < px0 + sz && x < w; ++x) { uint8_t* p = &px[((size_t)y * w + x) * 4]; p[0] = 255; p[1] = 128; p[2] = 0; }
    if (mouseIn)
        for (int d = -8; d <= 8; ++d) {
            if (mx + d >= 0 && mx + d < w && my >= 0 && my < h) { uint8_t* p = &px[((size_t)my * w + mx + d) * 4]; p[0] = 0; p[1] = 0; p[2] = 0; }
            if (my + d >= 0 && my + d < h && mx >= 0 && mx < w) { uint8_t* p = &px[((size_t)(my + d) * w + mx) * 4]; p[0] = 0; p[1] = 0; p[2] = 0; }
        }
}

void CheckGl(Backend* be) {
    WindowDesc d;
    d.title = "okay-platform-selftest GL"; d.width = 64; d.height = 64; d.opengl = true; d.hidden = true; d.resizable = false;
    auto w = be->MakeWindow(d);
    if (!w) { g_rep.Add("OpenGL window", Status::Fail, "could not create a GL-capable window: " + be->LastError()); return; }
    GLAttributes a;
    if (!w->CreateGLContext(a)) { g_rep.Add("OpenGL context", Status::Fail, "CreateGLContext failed: " + be->LastError()); return; }
    if (!w->MakeCurrent()) { g_rep.Add("OpenGL context", Status::Fail, "MakeCurrent failed"); return; }
    using GetString = const unsigned char*(OKAY_GLCALL*)(unsigned);
    GetString gs = reinterpret_cast<GetString>(be->GLProcAddress("glGetString"));
    if (!gs) { g_rep.Add("OpenGL context", Status::Fail, "context made, but glGetString was not found"); return; }
    const unsigned char* v = gs(0x1F02);   // GL_VERSION
    const unsigned char* r = gs(0x1F01);   // GL_RENDERER
    g_rep.Add("OpenGL context", v ? Status::Pass : Status::Fail,
              std::string("version ") + (v ? (const char*)v : "(none)") + ", renderer " + (r ? (const char*)r : "(none)"));
    w->SwapBuffers();
    w->DestroyGLContext();
}

void CheckClipboard(Backend* be) {
    const std::string s = "okay-selftest-" + std::to_string((unsigned long long)be->TicksMs()) + " \xC3\xA4\xE2\x82\xAC";
    if (!be->SetClipboardText(s)) { g_rep.Add("clipboard", Status::Fail, "SetClipboardText returned false"); return; }
    const std::string back = be->GetClipboardText();
    g_rep.Add("clipboard", back == s ? Status::Pass : Status::Fail,
              back == s ? "wrote text (with a non-ASCII letter and the euro sign) and read it back" : "wrote \"" + s + "\" but read back \"" + back + "\"");
}

void CheckTimers(Backend* be) {
    const uint64_t t0 = be->TicksMs(), p0 = be->PerfCounter();
    be->DelayMs(100);
    const uint64_t dt = be->TicksMs() - t0, dp = be->PerfCounter() - p0;
    const double perfMs = be->PerfFrequency() ? 1000.0 * (double)dp / (double)be->PerfFrequency() : 0.0;
    const bool ok = dt >= 90 && dt <= 400 && perfMs >= 90 && perfMs <= 400;
    g_rep.Add("timers", ok ? Status::Pass : Status::Fail,
              "a 100 ms delay measured " + std::to_string(dt) + " ms (ticks) and " + Num(perfMs) + " ms (performance counter, " + std::to_string((unsigned long long)be->PerfFrequency()) + " Hz)");
}

} // namespace

int main(int argc, char** argv) {
    double seconds = 20;
    bool autoRun = false, noAudio = false, pause = false, useSdl = false;
#ifdef _WIN32
    pause = true;
#endif
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--auto")) { autoRun = true; seconds = 2; pause = false; }
        else if (!std::strcmp(argv[i], "--no-audio")) noAudio = true;
        else if (!std::strcmp(argv[i], "--no-pause")) pause = false;
        else if (!std::strcmp(argv[i], "--sdl2")) useSdl = true;
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) { seconds = std::atof(argv[++i]); if (seconds < 1) seconds = 1; }
        else { std::printf("usage: okay-platform-selftest [--seconds N] [--auto] [--no-audio] [--no-pause] [--sdl2]\n"); return 2; }
    }

    Backend* be = Get(useSdl ? BackendKind::Sdl2 : BackendKind::Own);
    if (!be) { std::printf("This build has no %s backend.\n", useSdl ? "sdl2" : "own"); return 2; }
    std::printf("okay-platform-selftest, backend: %s\n", be->Name());
    if (!autoRun)
        std::printf("A window opens for %.0f seconds. In it: move the mouse, click, scroll, type, press keys, plug in a pad,\n"
                    "press T to replay the 440 Hz tone. Esc or closing the window ends the test early.\n\n", seconds);

    InitFlags fl;
    fl.audio = !noAudio;
    if (!be->Init(fl)) {
        g_rep.Add("start", Status::Fail, "Init failed: " + be->LastError());
        std::printf("%s", g_rep.Format("okay-platform-selftest", be->Name(), "").c_str());
        return 2;
    }
    g_rep.Add("start", Status::Pass, std::string("backend \"") + be->Name() + "\" initialised");

    CheckTimers(be);
    g_rep.Add("folders", Status::Info, "program folder \"" + be->BasePath() + "\", save folder \"" + be->PrefPath("OkaySpace", "selftest") + "\"");
    {
        std::string loc;
        for (const std::string& l : be->PreferredLocales()) loc += (loc.empty() ? "" : ", ") + l;
        g_rep.Add("languages", Status::Info, loc.empty() ? "none reported" : loc);
    }
    CheckGl(be);

    // ---- Window and software frame ----
    WindowDesc wd;
    wd.title = "okay-platform-selftest"; wd.width = 640; wd.height = 400; wd.resizable = true;
    auto win = be->MakeWindow(wd);
    if (!win) {
        g_rep.Add("window", Status::Fail, "could not create a window: " + be->LastError());
    } else {
        win->Show();
        win->Raise();
        int w = 0, h = 0, dw = 0, dh = 0;
        win->GetSize(w, h);
        win->GetDrawableSize(dw, dh);
        g_rep.Add("window", (w == 640 && h == 400) ? Status::Pass : Status::Fail,
                  "asked 640x400, got " + std::to_string(w) + "x" + std::to_string(h) + ", drawable " + std::to_string(dw) + "x" + std::to_string(dh) +
                  (win->RefreshRateHz() ? ", refresh " + std::to_string(win->RefreshRateHz()) + " Hz" : ", refresh rate unknown"));
        g_rep.Add("window DPI", Status::Info, std::to_string(win->Dpi()) + " (96 = 100 percent)");
        win->SetTitle("okay-platform-selftest (own platform layer) \xC3\xA4");
    }

    {
        const int n = be->DisplayCount();
        std::string d;
        for (int i = 0; i < n; ++i) {
            DisplayInfo di;
            if (be->DisplayAt(i, di)) d += (i ? "; " : "") + std::to_string(di.w) + "x" + std::to_string(di.h) + " at " + std::to_string(di.x) + "," + std::to_string(di.y) +
                                          ", " + Num(di.dpi, "%.0f") + " DPI" + (di.primary ? ", primary" : "");
        }
        g_rep.Add("displays", n > 0 ? Status::Pass : Status::Skip, n > 0 ? std::to_string(n) + ": " + d : "this backend lists no displays");
    }
    CheckClipboard(be);   // after the window: on X11 the clipboard owner is a window

    // ---- Audio ----
    std::unique_ptr<AudioDevice> out, in;
    ToneGen tone(440.0, 48000, 2, 0.25);
    uint32_t toneBytes = 0, queuedAtStart = 0;
    auto QueueTone = [&]() {
        std::vector<int16_t> s(48000 * 2);
        tone.FillS16(s.data(), 48000);
        toneBytes = 48000 * 2 * 2;
        out->Queue(s.data(), toneBytes);
        queuedAtStart = out->QueuedBytes();
    };
    if (!noAudio) {
        std::string names;
        const int n = be->AudioDeviceCount(false);
        for (int i = 0; i < n; ++i) names += (i ? "; " : "") + be->AudioDeviceName(i, false);
        g_rep.Add("audio output devices", n > 0 ? Status::Pass : Status::Fail, std::to_string(n) + ": " + names);
        AudioSpec want, got;
        want.freq = 48000; want.channels = 2; want.format = SampleFormat::S16; want.frames = 1024;
        out = be->OpenAudio(want, false, nullptr, &got);
        if (!out) g_rep.Add("audio playback", Status::Fail, "could not open the default output: " + be->LastError());
        else {
            g_rep.Add("audio open", Status::Pass, std::to_string(got.freq) + " Hz, " + std::to_string(got.channels) + " channels, " + (got.format == SampleFormat::F32 ? "float" : "16-bit"));
            QueueTone();
            out->Pause(false);
        }
        AudioSpec cw;
        cw.freq = 48000; cw.channels = 1; cw.format = SampleFormat::S16; cw.frames = 1024;
        in = be->OpenAudio(cw, true, nullptr, nullptr);
        if (in) in->Pause(false);
        else g_rep.Add("audio capture", Status::Skip, "no microphone could be opened: " + be->LastError());
    }

    // ---- Gamepad ----
    std::unique_ptr<Gamepad> pad;
    const int padCount = be->GamepadDeviceCount();
    if (padCount > 0) pad = be->OpenGamepad(0);
    g_rep.Add("gamepads found", Status::Info, std::to_string(padCount) + (pad ? std::string(", first is \"") + pad->Name() + "\"" : std::string()));
    bool padMoved = false;
    int16_t padMaxAxis = 0;

    // ---- Interactive loop ----
    be->StartTextInput();
    // Icon: a small orange square, to see the window's icon change (Win32: title bar and task bar).
    if (win) {
        std::vector<uint8_t> ic(32 * 32 * 4, 0);
        for (int y = 4; y < 28; ++y) for (int x = 4; x < 28; ++x) { uint8_t* p = &ic[((size_t)y * 32 + x) * 4]; p[0] = 255; p[1] = 128; p[2] = 0; p[3] = 255; }
        g_rep.Add("window icon", win->SetIcon(ic.data(), 32, 32) ? Status::Pass : Status::Skip, "set an orange square as the icon (look at the title bar / task bar)");
    }
    std::vector<uint8_t> px;
    int pw = 640, ph = 400, mx = -1, my = -1;
    if (win) win->GetSize(pw, ph);
    bool presentOk = false, presentTried = false, stop = false;
    const uint64_t start = be->TicksMs();
    uint64_t nextPad = 0;
    int printed = 0;
    while (!stop && (double)(be->TicksMs() - start) < seconds * 1000.0) {
        Event e;
        while (be->PollEvent(e)) {
            g_tally.Add(e);
            if (e.type == EventType::MouseMove) { mx = e.x; my = e.y; }
            if (e.type == EventType::MouseButtonDown) { mx = e.x; my = e.y; }
            if (e.type == EventType::WindowResized && win) win->GetSize(pw, ph);
            if (e.type == EventType::KeyDown && e.key == Key::T && !e.repeat && out) { QueueTone(); }
            const std::string line = DescribeEvent(e);
            if (!line.empty() && printed < 200) { std::printf("  %s\n", line.c_str()); ++printed; }
            if (e.type == EventType::Quit || e.type == EventType::WindowClose || (e.type == EventType::KeyDown && e.key == Key::Escape)) stop = true;
        }
        if (pad && be->TicksMs() >= nextPad) {
            nextPad = be->TicksMs() + 250;
            int16_t big = 0;
            for (int a = 0; a <= (int)GamepadAxis::TriggerRight; ++a) { const int16_t v = pad->Axis((GamepadAxis)a); if (std::abs((int)v) > std::abs((int)big)) big = v; }
            bool btn = false;
            for (int b = 0; b <= (int)GamepadButton::DpadRight; ++b) if (pad->Button((GamepadButton)b)) { btn = true; std::printf("  pad button %d down\n", b); }
            if (std::abs((int)big) > 8000) { padMoved = true; padMaxAxis = big; std::printf("  pad axis value %d\n", (int)big); }
            if (btn) padMoved = true;
        }
        if (win && pw > 0 && ph > 0) {
            px.assign((size_t)pw * ph * 4, 0);
            Draw(px, pw, ph, be->TicksMs() - start, mx, my, mx >= 0);
            const bool ok = win->PresentPixels(px.data(), pw, ph);
            if (!presentTried) { presentTried = true; presentOk = ok; }
        }
        be->DelayMs(8);
    }
    be->StopTextInput();

    // ---- Verdicts ----
    if (win) g_rep.Add("software frame", presentOk ? Status::Pass : Status::Fail,
                       presentOk ? "colour bars, a moving square and a mouse cross were handed to the window with PresentPixels (look at the window: did you see them?)"
                                 : "PresentPixels returned false");
    g_rep.Add("keyboard", g_tally.keyDown ? Status::Pass : Status::Skip,
              g_tally.keyDown ? std::to_string(g_tally.keyDown) + " key presses, " + std::to_string(g_tally.distinctKeys) + " different keys" : "no key was pressed");
    g_rep.Add("text input", g_tally.text ? Status::Pass : Status::Skip, g_tally.text ? std::to_string(g_tally.text) + " text events" : "no text arrived (type letters in the window)");
    g_rep.Add("mouse motion", g_tally.mouseMove ? Status::Pass : Status::Skip, std::to_string(g_tally.mouseMove) + " motion events");
    g_rep.Add("mouse buttons", g_tally.mouseDown ? Status::Pass : Status::Skip, std::to_string(g_tally.mouseDown) + " clicks");
    g_rep.Add("mouse wheel", g_tally.wheel ? Status::Pass : Status::Skip, std::to_string(g_tally.wheel) + " wheel events");
    g_rep.Add("window events", Status::Info, std::to_string(g_tally.resized) + " resizes, " + std::to_string(g_tally.focus) + " focus changes, " + std::to_string(g_tally.drop) + " dropped files");
    if (padCount > 0) g_rep.Add("gamepad input", padMoved ? Status::Pass : Status::Skip,
                                padMoved ? "input arrived (largest axis " + std::to_string((int)padMaxAxis) + ")" : "a pad is plugged in but no stick or button was moved");
    else g_rep.Add("gamepad input", Status::Skip, "no pad found (plug one in before starting)");
    if (out) {
        const uint32_t left = out->QueuedBytes();
        const bool consumed = left < queuedAtStart;
        g_rep.Add("audio playback", consumed ? Status::Pass : Status::Fail,
                  consumed ? "the device took " + std::to_string(queuedAtStart - left) + " of " + std::to_string(queuedAtStart) + " queued bytes (one second of 440 Hz). Whether you HEARD it, the program cannot know"
                           : "the device took no data from the queue");
    }
    if (in) {
        std::vector<uint8_t> buf(1 << 16);
        uint32_t total = 0, n;
        int peak = 0;
        while ((n = in->Dequeue(buf.data(), (uint32_t)buf.size())) > 0) {
            total += n;
            const int16_t* s = reinterpret_cast<const int16_t*>(buf.data());
            for (uint32_t i = 0; i < n / 2; ++i) peak = std::max(peak, std::abs((int)s[i]));
        }
        g_rep.Add("audio capture", total ? Status::Pass : Status::Fail, std::to_string(total) + " bytes captured, loudest sample " + std::to_string(peak) + " of 32767");
    }
    out.reset(); in.reset(); pad.reset(); win.reset();
    be->Shutdown();

    char when[64] = "";
    const std::time_t now = std::time(nullptr);
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    const std::string text = g_rep.Format("okay-platform-selftest", be->Name(), when);
    std::printf("\n%s", text.c_str());
    std::string path = be->BasePath() + "okay-platform-selftest.txt";
    std::ofstream f(path, std::ios::binary);
    if (f) { f << text; std::printf("Saved to %s\n", path.c_str()); }
    else std::printf("Could not save %s\n", path.c_str());
    if (pause) { std::printf("Press Enter to close.\n"); std::getchar(); }
    return g_rep.ExitCode();
}
