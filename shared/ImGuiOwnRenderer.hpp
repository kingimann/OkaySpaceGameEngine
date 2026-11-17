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

private:
    okay::platform::Painter& m_p;
    std::vector<okay::ui2d::Vertex> m_vtx;
    std::vector<int> m_idx;
    std::vector<std::uint8_t> m_rgba;   // an Alpha8 texture expanded to RGBA
    int m_lastCmds = 0;
};

} // namespace okay::imguiown
