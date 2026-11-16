#pragma once
// The launcher's own Dear ImGui backends (OKAY_LAUNCHER_RENDERER=own; off by default, SDL's backends stay the default):
//
//   ImGuiPainterRenderer  draws ImGui's ImDrawData (vertex and index buffers, clip rectangles, texture ids) on any
//                         okay::platform::Painter (the OpenGL painter, the software painter, even the sdl2 one), and
//                         serves ImGui's texture requests (ImTextureData: create, update, destroy) as painter textures.
//   ImGuiPlatformGlue     feeds ImGui from okay::platform (input, size, clipboard, cursor shape, the IME caret). It lives in
//                         shared/ImGuiOwnPlatform.* now, shared with the editor (OKAY_EDITOR_PLATFORM=own); the names
//                         below keep working here. One glue per ImGui context and window: the launcher and its chat
//                         window each have one.
//
// Written from Dear ImGui's public API contract (imgui.h: ImDrawData, ImDrawCmd, ImTextureData, ImGuiIO,
// ImGuiPlatformIO) and the okay::platform interfaces. Not copied or ported from ImGui's example backends or from SDL.
//
// Header rules (MinGW): no <mutex> / <thread> here.
#include "okay/Platform/Painter.hpp"
#include "okay/Platform/Platform.hpp"
#include "ImGuiOwnPlatform.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct ImDrawData;
struct ImTextureData;

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

// The platform glue is shared with the editor (OKAY_EDITOR_PLATFORM=own): shared/ImGuiOwnPlatform.hpp.
using okay::imguiown::ImGuiPlatformGlue;
using okay::imguiown::ImGuiKeyFor;

} // namespace okay::launchergfx
