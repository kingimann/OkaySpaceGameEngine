#pragma once
// The one dropdown look for the launcher and the editor: the chevron (a "v" of two strokes) is drawn inside the same
// rounded box as the chosen value. Dear ImGui's own combo draws its arrow in a separate darker square, which read as a
// second control. The launcher's okl::BeginSlateCombo and the editor's oked::BeginDropdown both call this, so the two
// programs keep one look. Close an open dropdown with ImGui::EndCombo. Written from scratch for this engine.
#include "imgui.h"
#include <algorithm>

namespace okayshared {

/// Same arguments as ImGui::BeginCombo (the width comes from SetNextItemWidth / the item width), plus the smallest corner
/// rounding of the box. The caller shortens a long preview first if it wants to (the launcher does, with its FitText).
// static: the editor compiles this header against two different imgui.h (Dear ImGui, and OkayUI's compatibility header in its
// window-mode panels), so each source file keeps its own copy instead of sharing one by name.
static inline bool BeginChevronCombo(const char* label, const char* preview, int flags, float minRounding) {
#if IMGUI_VERSION_NUM == 0   // OkayUI's compatibility header (window mode): its own combo already draws the chevron inside the box
    (void)minRounding;
    return ImGui::BeginCombo(label, preview ? preview : "", flags);
#else
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::CalcItemWidth(), h = ImGui::GetFrameHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::max(ImGui::GetStyle().FrameRounding, minRounding));
    const bool open = ImGui::BeginCombo(label, preview ? preview : "", flags | ImGuiComboFlags_NoArrowButton);
    ImGui::PopStyleVar();
    // Brighter while the list is open or the mouse is over the box.
    const bool hot = open || ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + h));
    const ImVec2 c(p.x + w - h * 0.5f, p.y + h * 0.5f + 1.0f);
    const float r = std::max(3.5f, h * 0.15f);
    ImDrawList* dl = open ? ImGui::GetForegroundDrawList(ImGui::GetMainViewport())   // the window's own list belongs to the open popup now
                          : ImGui::GetWindowDrawList();
    dl->PathLineTo(ImVec2(c.x - r, c.y - r * 0.5f));
    dl->PathLineTo(ImVec2(c.x, c.y + r * 0.5f));
    dl->PathLineTo(ImVec2(c.x + r, c.y - r * 0.5f));
    dl->PathStroke(ImGui::GetColorU32(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled), 0, 1.8f);
    return open;
#endif
}

} // namespace okayshared
