#pragma once
// The launcher's own Dear ImGui backends (OKAY_LAUNCHER_RENDERER=own; off by default, SDL's backends stay the default):
//
//   ImGuiPainterRenderer  draws ImGui's ImDrawData (vertex and index buffers, clip rectangles, texture ids) on any
//                         okay::platform::Painter (the OpenGL painter, the software painter, even the sdl2 one), and
//                         serves ImGui's texture requests (ImTextureData: create, update, destroy) as painter textures.
//   ImGuiPlatformGlue     feeds ImGui from okay::platform: window size and DPI, mouse, keys, text input, wheel, focus,
//                         clipboard and the mouse cursor shape; and tells the window where ImGui's text caret is, so the
//                         input method's candidate window opens there (Window::SetTextInputArea). One glue per ImGui
//                         context and window: the launcher and its chat window each have one.
//
// Written from Dear ImGui's public API contract (imgui.h: ImDrawData, ImDrawCmd, ImTextureData, ImGuiIO,
// ImGuiPlatformIO) and the okay::platform interfaces. Not copied or ported from ImGui's example backends or from SDL.
//
// Header rules (MinGW): no <mutex> / <thread> here.
#include "okay/Platform/Painter.hpp"
#include "okay/Platform/Platform.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct ImDrawData;
struct ImTextureData;
struct ImGuiContext;
struct ImGuiViewport;
struct ImGuiPlatformImeData;

namespace okay::launchergfx {

class ImGuiPainterRenderer {
public:
    /// Registers itself with the current ImGui context (io.BackendRendererName, the texture flags). One context only.
    explicit ImGuiPainterRenderer(okay::platform::Painter& p);
    /// Destroys every ImGui texture it made and unregisters (call with the same ImGui context current).
    ~ImGuiPainterRenderer();
    ImGuiPainterRenderer(const ImGuiPainterRenderer&) = delete;
    ImGuiPainterRenderer& operator=(const ImGuiPainterRenderer&) = delete;

    /// Draw one frame's draw data. Honours the texture requests first, then every command: its clip rectangle (in
    /// framebuffer pixels, DisplayPos and FramebufferScale applied), its texture (an okay::ui2d::Tex as ImTextureID)
    /// and its vertex / index range. Leaves the painter with no clip and scale 1.
    void Render(ImDrawData* dd);

    /// Draw-command counters of the last Render (for tests and the frame-time log).
    int LastCommands() const { return m_lastCmds; }

private:
    void UpdateTexture(ImTextureData* t);
    okay::platform::Painter& m_p;
    std::vector<okay::ui2d::Vertex> m_vtx;
    std::vector<int> m_idx;
    std::vector<std::uint8_t> m_rgba;   // an Alpha8 texture expanded to RGBA
    int m_lastCmds = 0;
};

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

    /// OKAY_LAUNCHER_INPUT_LOG=1: log every mouse button, key and text event ImGui is given (the Xvfb test reads it).
    void SetInputLog(bool on) { m_log = on; }

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
    bool m_mouseInside = false;
    bool m_log = false;
    std::string m_clip;         // keeps the clipboard text alive for ImGui
    bool m_imeSet = false, m_imeTaken = false;
    int m_imeX = 0, m_imeY = 0, m_imeH = 0;
    static const char* GetClipboard(ImGuiContext* ctx);
    static void SetClipboard(ImGuiContext* ctx, const char* text);
    static void SetImeData(ImGuiContext* ctx, ImGuiViewport* vp, ImGuiPlatformImeData* data);
};

/// The ImGuiKey for one of our physical keys (USB HID numbering); ImGuiKey_None (0) when ImGui has no such key.
int ImGuiKeyFor(okay::platform::Key k);

} // namespace okay::launchergfx
