#pragma once
// The frame loop of an OkayUI window that OkayUI owns outright ("window mode", OKAY_UI_HOST=okayui): events come from an okay::platform::Backend,
// the frame goes out through an okay::platform::Painter, and no Dear ImGui is involved. Own code, off by default, written from what a frame has to
// do (docs/ui/own_ui_toolkit.md 6.23, step 1). Needs OKAYUI_HOST_PLATFORM like the host it builds on. No <mutex>/<thread> (Windows build rule).
//
//     okayuihost::platform::FrameLoop loop(*backend, *painter);
//     while (loop.Frame([&] { ...OkayUI calls... })) {}
//
// One Frame(): drain the backend's events into the host (at most 4096 per frame, so a flood cannot starve the frame), stop when a quit was asked for,
// begin the frame with the backend clock as OkayUI's time, run the UI function, tell the backend whether a text field wants the keyboard (and where its
// caret is, for the input method), then finish, draw and present.
// RunProgram (at the end) is a whole program around one such window: the browser program and the Script Editor program use it.
#ifdef OKAYUI_HOST_PLATFORM
#include "OkayUIHost.hpp"
#include "okay/Platform/Platform.hpp"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

namespace okayuihost {
namespace platform {

/// The window and painter of a window that OkayUI owns (the editor's window mode, the browser program). False with `error` (and
/// nothing kept) when the backend cannot make either.
inline bool OpenWindow(okay::platform::Backend& backend, const okay::platform::WindowDesc& desc, std::unique_ptr<okay::platform::Window>& window,
                       std::unique_ptr<okay::platform::Painter>& painter, std::string& error) {
    window = backend.MakeWindow(desc);
    if (!window) { error = "no window: " + backend.LastError(); return false; }
    std::string why;
    painter = okay::platform::CreateSdl2Painter(*window, okay::platform::PainterDesc(), &why);
    if (!painter) { window.reset(); error = "no painter: " + why; return false; }
    return true;
}

class FrameLoop {
public:
    FrameLoop(okay::platform::Backend& backend, okay::platform::Painter& painter) : m_backend(backend), m_host(painter) {}
    /// True while the window should stay open; false (and no frame drawn) once a Quit / WindowClose event arrived.
    bool Frame(const std::function<void()>& ui) {
        okay::platform::Event e; int guard = 0;
        while (guard++ < 4096 && m_backend.PollEvent(e)) m_host.Feed(e);
        if (m_host.QuitRequested()) return false;
        m_host.BeginFrame(false, true, (double)m_backend.TicksMs() / 1000.0);
        if (m_host.TakeResized()) ++m_resizes;
        if (ui) ui();
        const bool want = m_host.WantsTextInput();
        if (want != m_textOn) { if (want) m_backend.StartTextInput(); else m_backend.StopTextInput(); m_textOn = want; }
        if (want) {
            const OkayUI::ImeRect r = OkayUI::GetImeRect();
            if (r.valid) m_backend.SetTextInputRect((int)r.caret.x, (int)r.caret.y, (int)(r.caret.w < 1.0f ? 1.0f : r.caret.w), (int)(r.caret.h < 1.0f ? 1.0f : r.caret.h));
        }
        const unsigned char* bg = OkayUI::Style().track;   // the window behind the panels: the design's sunken surface
        m_host.EndFrame(bg[0], bg[1], bg[2]);
        return true;
    }
    Host& GetHost() { return m_host; }
    bool TextInputOn() const { return m_textOn; }
    int Resizes() const { return m_resizes; }
private:
    okay::platform::Backend& m_backend;
    Host m_host;
    bool m_textOn = false;
    int m_resizes = 0;
};

/// A program that is one OkayUI app drawn over its whole window (the browser program, the Script Editor program): the backend,
/// the window and painter, the frame loop, the window's title, and the order things end in (the app's own end runs while the
/// painter still exists, so it can free its textures). Every hook is optional.
struct ProgramHooks {
    std::function<bool(okay::platform::Backend&, okay::platform::Painter&)> start;   ///< after the window opened; false = quit with 1
    std::function<void(float w, float h)> frame;                                      ///< draw the app over (0, 0, w, h)
    std::function<bool()> wantsQuit;                                                  ///< the app asked to end (its Quit item)
    std::function<std::string()> title;                                               ///< the window's title, set when it changes
    std::function<void()> stop;                                                       ///< at the end, before the frame loop and the painter go
};
struct ProgramDesc {
    okay::platform::WindowDesc window;
    const char* tag = "program";   ///< the "[tag]" of its error lines
    int maxFrames = 0;             ///< > 0: end after this many frames (a headless start-and-quit check); 0 = until closed
};
/// The usual hooks for an app object with Frame(x, y, w, h), WantsQuit() and WindowTitle() (BrowserApp, ScriptIdeApp) that `start`
/// makes into `app`: drawn over the window, its Quit ends the program, its title is the window's, and it is destroyed at the end.
template <class App>
void AppHooks(ProgramHooks& hooks, std::unique_ptr<App>& app) {
    hooks.frame = [&app](float w, float h) { if (app) app->Frame(0, 0, w, h); };
    hooks.wantsQuit = [&app] { return app && app->WantsQuit(); };
    hooks.title = [&app] { return app ? app->WindowTitle() : std::string(); };
    hooks.stop = [&app] { app.reset(); };
}
/// Run the program; the exit code for main (0, or 1 when the backend or the window could not be made or `start` said no).
inline int RunProgram(const ProgramDesc& desc, const ProgramHooks& hooks) {
    okay::platform::Backend* be = okay::platform::Default();
    if (!be || !be->Init(okay::platform::InitFlags())) {
        std::fprintf(stderr, "[%s] no platform backend: %s\n", desc.tag, be ? be->LastError().c_str() : "none");
        return 1;
    }
    std::unique_ptr<okay::platform::Window> win;
    std::unique_ptr<okay::platform::Painter> painter;
    std::string err;
    if (!OpenWindow(*be, desc.window, win, painter, err)) { std::fprintf(stderr, "[%s] %s\n", desc.tag, err.c_str()); be->Shutdown(); return 1; }
    int code = 0;
    if (hooks.start && !hooks.start(*be, *painter)) code = 1;
    if (code == 0) {   // the frame loop (and its OkayUI host, which uses the painter) ends before the painter does
        FrameLoop loop(*be, *painter);
        std::string shownTitle;
        int frames = 0;
        while (!(hooks.wantsQuit && hooks.wantsQuit()) && (desc.maxFrames <= 0 || frames < desc.maxFrames) && loop.Frame([&] {
            if (hooks.frame) hooks.frame((float)loop.GetHost().DisplayWidth(), (float)loop.GetHost().DisplayHeight());
            if (hooks.title) {
                const std::string t = hooks.title();
                if (t != shownTitle) { win->SetTitle(t); shownTitle = t; }
            }
        })) ++frames;
        if (hooks.stop) hooks.stop();   // while the host and the painter still exist (the browser frees its textures here)
    } else if (hooks.stop) hooks.stop();
    painter.reset();
    win.reset();
    be->Shutdown();
    return code;
}

} // namespace platform
} // namespace okayuihost
#endif // OKAYUI_HOST_PLATFORM
