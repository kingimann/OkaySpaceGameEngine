#pragma once
// The launcher's own Dear ImGui backends (OKAY_LAUNCHER_RENDERER=own; off by default, SDL's backends stay the default). Both
// live in shared/ now (the editor uses them too); this header keeps the launcher's names:
//
//   ImGuiPainterRenderer  draws ImGui's ImDrawData (vertex and index buffers, clip rectangles, texture ids) on any
//                         okay::platform::Painter (the OpenGL painter, the software painter, even the sdl2 one), and
//                         serves ImGui's texture requests (ImTextureData: create, update, destroy) as painter textures.
//                         It lives in shared/ImGuiOwnRenderer.* now, shared with the editor (OKAY_EDITOR_RENDERER=own).
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
#include "ImGuiOwnRenderer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace okay::launchergfx {

// The renderer is shared with the editor (OKAY_EDITOR_RENDERER=own): shared/ImGuiOwnRenderer.hpp.
using okay::imguiown::ImGuiPainterRenderer;
// The platform glue is shared with the editor (OKAY_EDITOR_PLATFORM=own): shared/ImGuiOwnPlatform.hpp.
using okay::imguiown::ImGuiPlatformGlue;
using okay::imguiown::ImGuiKeyFor;

} // namespace okay::launchergfx
