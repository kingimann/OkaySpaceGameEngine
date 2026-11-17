#pragma once
// Dear ImGui's platform side on okay::platform, shared by the launcher (OKAY_LAUNCHER_RENDERER=own) and the editor
// (OKAY_EDITOR_PLATFORM=own). It feeds ImGui from okay::platform: window size and DPI, mouse, keys, text input, wheel, focus,
// clipboard and the mouse cursor shape; and tells the window where ImGui's text caret is, so the input method's candidate
// window opens there (Window::SetTextInputArea). One glue per ImGui context and window.
//
// Moved here from launcher/ImGuiOwnBackend.* (where it was written) so both programs use the same code. Written from Dear
// ImGui's public API contract (imgui.h: ImGuiIO, ImGuiPlatformIO) and the okay::platform interfaces; not copied or ported from
// ImGui's example backends or from SDL. It does not do ImGui's multi-viewport platform windows (one window only).
//
// Header rules (MinGW): no <mutex> / <thread> here.
#include "okay/Platform/Platform.hpp"

#include <cstdint>
#include <string>

struct ImGuiContext;
struct ImGuiViewport;
struct ImGuiPlatformImeData;

namespace okay::imguiown {

class ImGuiPlatformGlue {
public:
    /// Registers with the current ImGui context (io.BackendPlatformName, clipboard, cursor flag). `b` made `w`.
    ImGuiPlatformGlue(okay::platform::Backend& b, okay::platform::Window& w);
    ~ImGuiPlatformGlue();
    ImGuiPlatformGlue(const ImGuiPlatformGlue&) = delete;
    ImGuiPlatformGlue& operator=(const ImGuiPlatformGlue&) = delete;

    /// Feed one event of our window to ImGui. Events of other windows are ignored. True when ImGui used it.
    bool ProcessEvent(const okay::platform::Event& e);
    /// Before ImGui::NewFrame: display size, framebuffer scale, time step, mouse leaving, cursor shape, text input on/off.
    void NewFrame();

    /// Keep the window's text input on all the time (on once now, never switched off by the glue). Off (the default): text input
    /// is on only while an ImGui text field wants it (the launcher). The editor turns it on: SDL starts with text input on and the
    /// editor's play mode and OkayUI fields rely on that, as they did with ImGui's SDL2 backend.
    void SetTextInputAlwaysOn(bool on);

    /// Log every mouse button, key and text event ImGui is given, to stderr, each line starting with `prefix` and ": "
    /// (the launcher's OKAY_LAUNCHER_INPUT_LOG=1 uses "launcher-input", which its Xvfb check reads; the editor "editor-input").
    /// `prefix` must outlive the glue (a string literal).
    void SetInputLog(bool on, const char* prefix = "launcher-input") { m_log = on; m_logPrefix = prefix ? prefix : "input"; }

    /// The last caret rectangle given to the window for the input method (window pixels), and whether the window took it.
    /// False when ImGui has not asked for one yet.
    bool LastImeArea(int& x, int& y, int& h, bool* taken = nullptr) const {
        x = m_imeX; y = m_imeY; h = m_imeH;
        if (taken) *taken = m_imeTaken;
        return m_imeSet;
    }

private:
    okay::platform::Backend& m_b;
    okay::platform::Window& m_w;
    std::uint64_t m_lastTicks = 0;
    bool m_textOn = false;
    bool m_textAlways = false;
    bool m_mouseInside = false;
    bool m_log = false;
    const char* m_logPrefix = "launcher-input";
    std::string m_clip;         // keeps the clipboard text alive for ImGui
    bool m_imeSet = false, m_imeTaken = false;
    int m_imeX = 0, m_imeY = 0, m_imeH = 0;
    static const char* GetClipboard(ImGuiContext* ctx);
    static void SetClipboard(ImGuiContext* ctx, const char* text);
    static void SetImeData(ImGuiContext* ctx, ImGuiViewport* vp, ImGuiPlatformImeData* data);
};

/// The ImGuiKey for one of our physical keys (USB HID numbering); ImGuiKey_None (0) when ImGui has no such key.
int ImGuiKeyFor(okay::platform::Key k);

} // namespace okay::imguiown
