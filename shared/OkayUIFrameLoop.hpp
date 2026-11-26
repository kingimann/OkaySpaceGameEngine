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
#ifdef OKAYUI_HOST_PLATFORM
#include "OkayUIHost.hpp"
#include "okay/Platform/Platform.hpp"

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
        m_host.EndFrame();
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

} // namespace platform
} // namespace okayuihost
#endif // OKAYUI_HOST_PLATFORM
