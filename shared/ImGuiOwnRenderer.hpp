#pragma once
// Our own Dear ImGui renderer, shared by the launcher (OKAY_LAUNCHER_RENDERER=own) and the editor (OKAY_EDITOR_RENDERER=own);
// both are off by default, SDL's backends stay the default.
//
//   ImGuiPainterRenderer  draws ImGui's ImDrawData (vertex and index buffers, clip rectangles, texture ids) on any
//                         okay::platform::Painter (the OpenGL painter, the software painter, even the sdl2 one), and
//                         serves ImGui's texture requests (ImTextureData: create, update, destroy) as painter textures.
//
// Moved here from launcher/ImGuiOwnBackend.* (where it was written) so both programs use one copy. Written from Dear ImGui's
// public API contract (imgui.h: ImDrawData, ImDrawCmd, ImTextureData, ImGuiIO, ImGuiPlatformIO) and the okay::platform
// interfaces. Not copied or ported from ImGui's example backends or from SDL.
//
// Header rules (MinGW): no <mutex> / <thread> here.
#include "okay/Platform/Painter.hpp"

#include <cstdint>
#include <vector>

struct ImDrawData;
struct ImTextureData;
struct ImGuiViewport;

namespace okay::imguiown {

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
    /// Honour one texture request (create, update, destroy) without drawing. Render does this for every texture in the draw
    /// data; this is for a host that draws some draw data another way.
    void UpdateTexture(ImTextureData* t);

    /// Draw-command counters of the last Render (for tests and the frame-time log).
    int LastCommands() const { return m_lastCmds; }

    // ---- multi-window panels (ImGui viewports) ----
    /// The window that shows a viewport (ImGuiPlatformGlue::WindowOf: the platform side keeps it in the viewport).
    using ViewportWindowFn = okay::platform::Window* (*)(ImGuiViewport* vp);
    /// Draw ImGui's panel windows too: install Renderer_RenderWindow / Renderer_SwapBuffers and set
    /// ImGuiBackendFlags_RendererHasViewports. Each panel window is drawn by THIS painter with its own context and textures,
    /// retargeted to the panel's window (Painter::SetTarget, Window::MakeCurrentWithContextOf), so every texture the main window
    /// shows (ImGui's font, the editor's thumbnails and 3D views) shows in a panel window too. False (nothing installed) when the
    /// painter cannot draw into another window (only the OpenGL painter can). `clearR/G/B`: the colour a panel window is cleared
    /// to before its draw data (unless ImGui says ImGuiViewportFlags_NoRendererClear).
    bool EnableViewports(ViewportWindowFn windowOf, std::uint8_t clearR = 15, std::uint8_t clearG = 16, std::uint8_t clearB = 20);
    bool ViewportsEnabled() const { return m_windowOf != nullptr; }
    /// Panel-window draws since EnableViewports, and how many could not be done (the window refused the context). For tests and
    /// the log.
    int PanelsDrawn() const { return m_panelsDrawn; }
    int PanelsFailed() const { return m_panelsFailed; }

private:
    okay::platform::Painter& m_p;
    std::vector<okay::ui2d::Vertex> m_vtx;
    std::vector<int> m_idx;
    std::vector<std::uint8_t> m_rgba;   // an Alpha8 texture expanded to RGBA
    int m_lastCmds = 0;
    ViewportWindowFn m_windowOf = nullptr;
    std::uint8_t m_clear[3] = {15, 16, 20};
    int m_panelsDrawn = 0, m_panelsFailed = 0;
    bool m_onPanel = false;   // the painter targets a panel window (between RenderWindow and SwapBuffers)
    static void VpRender(ImGuiViewport* vp, void* arg);
    static void VpSwap(ImGuiViewport* vp, void* arg);
};

} // namespace okay::imguiown
