#pragma once
// Port probe stand-in for imgui.h: lets a Dear ImGui panel compile (syntax check only) against OkayUI, so what is missing shows as
// compiler errors. Put this directory first on the include path (scripts/okayui_port_probe.sh does). Not used by any real build.
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
