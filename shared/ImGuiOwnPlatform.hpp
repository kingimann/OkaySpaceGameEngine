#pragma once
// Dear ImGui's platform side on okay::platform, shared by the launcher (OKAY_LAUNCHER_RENDERER=own) and the editor
// (OKAY_EDITOR_PLATFORM=own). It feeds ImGui from okay::platform: window size and DPI, mouse, keys, text input, wheel, focus,
// clipboard and the mouse cursor shape; and tells the window where ImGui's text caret is, so the input method's candidate
// window opens there (Window::SetTextInputArea). One glue per ImGui context and window.
//
// Moved here from launcher/ImGuiOwnBackend.* (where it was written) so both programs use the same code. Written from Dear
// ImGui's public API contract (imgui.h: ImGuiIO, ImGuiPlatformIO) and the okay::platform interfaces; not copied or ported from
// ImGui's example backends or from SDL.
//
// Multi-window panels (Dear ImGui's "viewports", stage E4 of docs/sdl_removal_plan.md): EnableViewports() installs ImGui's platform
// window functions (ImGuiPlatformIO::Platform_CreateWindow and the rest) on okay::platform windows made by the same backend as the
// main window, fills ImGui's monitor list from Backend::Displays(), and routes each panel window's events to ImGui with desktop
// coordinates (Event::screenX / screenY). The renderer side is ImGuiPainterRenderer::EnableViewports (ImGuiOwnRenderer.hpp).
// Written from imgui.h's comments on ImGuiPlatformIO, ImGuiViewport and ImGuiPlatformMonitor (the "Multi-Viewport Support"
// section), not from its example backends.
//
// Header rules (MinGW): no <mutex> / <thread> here.
#include "okay/Platform/Platform.hpp"

#include <cstdint>
#include <string>

struct ImGuiContext;
struct ImGuiViewport;
struct ImVec2;
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

    // ---- multi-window panels (ImGui viewports) ----
    /// Install ImGui's platform window functions (Platform_CreateWindow / DestroyWindow / ShowWindow / SetWindowPos / GetWindowPos /
    /// SetWindowSize / GetWindowSize / GetWindowFramebufferScale / SetWindowFocus / GetWindowFocus / GetWindowMinimized /
    /// SetWindowTitle / SetWindowAlpha), set ImGuiBackendFlags_PlatformHasViewports, tie the main viewport to the main window and fill
    /// the monitor list. `opengl`: every panel window can carry a GL context (WindowDesc::opengl), so a renderer can draw it with the
    /// main window's context (Window::MakeCurrentWithContextOf). The app still decides with ImGuiConfigFlags_ViewportsEnable (set it
    /// before the first ImGui::NewFrame) and calls ImGui::UpdatePlatformWindows / RenderPlatformWindowsDefault after each frame.
    /// The mouse-hovered viewport is NOT reported (ImGuiBackendFlags_HasMouseHoveredViewport stays off): our backends cannot let
    /// the pointer pass through a panel being dragged (ImGuiViewportFlags_NoInputs), and while a button is held the OS keeps sending
    /// the mouse to the window it was pressed in, so ImGui's own rule (the viewports' rectangles and focus order) is the better one.
    void EnableViewports(bool opengl);
    bool ViewportsEnabled() const { return m_viewports; }
    /// The okay::platform window that shows `vp` (the main window for the main viewport); null when it has none (yet).
    static okay::platform::Window* WindowOf(ImGuiViewport* vp);
    /// How many panel windows exist now / were made since EnableViewports (tests, the log).
    int PanelWindowCount() const { return m_panelWindows; }
    int PanelWindowsMade() const { return m_panelWindowsMade; }

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
    // multi-window panels
    bool m_viewports = false;
    bool m_viewportGL = false;
    int m_panelWindows = 0, m_panelWindowsMade = 0;
    std::uint32_t m_buttonsDown = 0;    // mouse buttons held (ImGui's numbering, bits): the pointer may be outside every window
    std::uint64_t m_monitorsAt = 0;     // when the monitor list was last read (PerfCounter), 0 = never
    bool ViewportsActive() const;
    void UpdateMonitors(bool force);
    ImGuiViewport* ViewportOfWindowId(std::uint32_t id) const;
    static ImGuiPlatformGlue* Current();
    static void VpCreate(ImGuiViewport* vp);
    static void VpDestroy(ImGuiViewport* vp);
    static void VpShow(ImGuiViewport* vp);
    static void VpSetPos(ImGuiViewport* vp, ImVec2 pos);
    static ImVec2 VpGetPos(ImGuiViewport* vp);
    static void VpSetSize(ImGuiViewport* vp, ImVec2 size);
    static ImVec2 VpGetSize(ImGuiViewport* vp);
    static ImVec2 VpGetScale(ImGuiViewport* vp);
    static void VpSetFocus(ImGuiViewport* vp);
    static bool VpGetFocus(ImGuiViewport* vp);
    static bool VpGetMinimized(ImGuiViewport* vp);
    static void VpSetTitle(ImGuiViewport* vp, const char* title);
    static void VpSetAlpha(ImGuiViewport* vp, float alpha);
    static const char* GetClipboard(ImGuiContext* ctx);
    static void SetClipboard(ImGuiContext* ctx, const char* text);
    static void SetImeData(ImGuiContext* ctx, ImGuiViewport* vp, ImGuiPlatformImeData* data);
};

/// The ImGuiKey for one of our physical keys (USB HID numbering); ImGuiKey_None (0) when ImGui has no such key.
int ImGuiKeyFor(okay::platform::Key k);

} // namespace okay::imguiown
