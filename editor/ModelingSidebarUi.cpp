// The Modeling window's page list (ModelingSidebarUi.hpp): open rows with icon, name and hint; folded, icons only.
#include "ModelingSidebarUi.hpp"
#include "ModelingToolIndex.hpp"
#include "EditorWidgets.hpp"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <string>

namespace oked::modeltools {

namespace {
TbIcon IconFor(PageIcon p) {
    switch (p) {
        case PageIcon::Star:    return TbIcon::Star;
        case PageIcon::Shapes:  return TbIcon::Shapes;
        case PageIcon::Steps:   return TbIcon::Steps;
        case PageIcon::Spin:    return TbIcon::Spin;
        case PageIcon::Resize:  return TbIcon::Scale;
        case PageIcon::Bend:    return TbIcon::Bend;
        case PageIcon::Brush:   return TbIcon::Brush;
        case PageIcon::Sparkle: return TbIcon::Sparkle;
        case PageIcon::Import:  return TbIcon::Import;
    }
    return TbIcon::More;
}
// The open page gets an accent bar on its left edge (on top of the normal selected fill), so it stands out in both themes.
void MarkCurrent(ImDrawList* dl, ImVec2 a, ImVec2 b, float scale) {
    dl->AddRectFilled(a, ImVec2(a.x + 3.0f * scale, b.y), ImGui::GetColorU32(AccentCol(1.0f)), 1.5f);
}
} // namespace

float PageListOpenWidth(float uiScale) {
    float longest = 0.0f;
    for (const Page& p : Pages()) {
        longest = std::max(longest, ImGui::CalcTextSize(p.name).x);
        longest = std::max(longest, ImGui::CalcTextSize(p.desc).x);
        longest = std::max(longest, ImGui::CalcTextSize(p.job).x);
    }
    const ImGuiStyle& st = ImGui::GetStyle();
    // The list's own padding and border, and room for a scrollbar when the window is short.
    const float chrome = st.WindowPadding.x * 2.0f + 2.0f + st.ScrollbarSize;
    return SidebarOpenWidth(longest, uiScale, chrome);
}

PageListResult DrawPageList(SidebarState& s, int current, const std::function<const char*(int)>& whyNot, float uiScale, float windowW) {
    PageListResult r;
    const float openW = PageListOpenWidth(uiScale);
    SidebarAuto(s, windowW, uiScale, openW);
    const bool folded = s.folded;
    if (folded) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * uiScale, 6.0f * uiScale));
    ImGui::BeginChild("##mdlnav", ImVec2(SidebarWidth(s, windowW, uiScale, openW), 0), ImGuiChildFlags_Borders);
    if (folded) ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lineH = ImGui::GetTextLineHeight();

    // The fold button: a small "page list" picture; open, the word "Pages" sits beside it.
    {
        const float b = kFoldBtn * uiScale;
        if (ImGui::Button("##mdlfold", ImVec2(folded ? -FLT_MIN : b, b))) { SidebarToggle(s); r.toggled = true; }
        const ImVec2 a = ImGui::GetItemRectMin(), z = ImGui::GetItemRectMax();
        DrawTbIcon(dl, TbIcon::PanelLeft, ImVec2((a.x + z.x) * 0.5f, (a.y + z.y) * 0.5f), b * 0.5f, ImGui::GetColorU32(ImGuiCol_Text));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s  (Ctrl+\\)", s.folded ? "Show the page names" : "Show icons only");
        if (!folded) { ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Pages"); }
    }

    const char* lastJob = "";
    for (const Page& pi : Pages()) {
        const int i = pi.legacy;
        const char* why = whyNot ? whyNot(i) : "";
        const bool usable = !why || !why[0];
        const bool sel = current == i;
        const ImU32 tc = ImGui::GetColorU32(usable ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        ImGui::PushID(i);
        if (folded) {   // the strip: the page's icon, its name in the tooltip
            ImGui::BeginDisabled(!usable);
            if (ImGui::Selectable("##pg", sel, 0, ImVec2(0, StripRowHeight(uiScale, lineH)))) r.clicked = i;
            ImGui::EndDisabled();
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            if (sel) MarkCurrent(dl, a, b, uiScale);
            DrawTbIcon(dl, IconFor(pi.icon), ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), 18.0f * uiScale, tc);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s\n%s", pi.name, usable ? pi.desc : why);
            ImGui::PopID();
            continue;
        }
        if (std::string(pi.job) != lastJob) { lastJob = pi.job; ImGui::Dummy(ImVec2(0, 2)); ImGui::TextDisabled("%s", pi.job); }
        ImGui::BeginDisabled(!usable);
        if (ImGui::Selectable("##pg", sel, 0, ImVec2(0, RowHeight(uiScale, lineH)))) r.clicked = i;
        ImGui::EndDisabled();
        if (!usable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", why);
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        if (sel) MarkCurrent(dl, a, b, uiScale);
        const float padX = kRowPadX * uiScale, iconW = kIconColW * uiScale, rowH = b.y - a.y;
        DrawTbIcon(dl, IconFor(pi.icon), ImVec2(a.x + padX + iconW * 0.5f - 2.0f * uiScale, (a.y + b.y) * 0.5f), 18.0f * uiScale, tc);
        const float tx = a.x + padX + iconW, ty = a.y + (rowH - 2.0f * lineH) * 0.5f;
        dl->AddText(ImVec2(tx, ty), tc, pi.name);
        dl->AddText(ImVec2(tx, ty + lineH), ImGui::GetColorU32(ImGuiCol_TextDisabled), pi.desc);
        ImGui::PopID();
    }
    ImGui::EndChild();
    return r;
}

} // namespace oked::modeltools
