#pragma once
// OkayUI's imgui.h: code written in Dear ImGui's names (the editor, the launcher, the Visual Script and Script Editor apps) compiles
// against OkayUI through it. Put this directory first on the include path (every such target does; scripts/okayui_port_probe.sh too).
// Dear ImGui itself is not in the repository (2026-11-27).
#include "okay/UI/OkayUIImGuiNames.hpp"
#ifndef IMGUI_IMPL_API
#define IMGUI_IMPL_API
#endif
// The Dear ImGui draw output types are OkayUI's own shapes now (OkayUI.hpp, "Dear ImGui's draw-data shapes"); the backend calls that take them
// are host-specific: see backends/.

// Dear ImGui's SameLine takes the offset from the window's left edge first (SameLine(x) puts the next item at x), OkayUI's one-argument
// SameLine takes the spacing: Dear ImGui code gets Dear ImGui's meaning (declared in ImGui itself, so it hides OkayUI's).
namespace ImGui {
inline void SameLine(float offsetFromStartX = 0.0f, float spacing = -1.0f) { ::OkayUI::SameLine(offsetFromStartX, spacing); }
}
