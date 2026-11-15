// Drawing of the Project panel's right-click menus and of the Properties rows: just ImGui and the lists from
// ProjectContextModel.hpp, no file operations and no engine, so a small test program can draw them headless
// (the screenshots of docs/project_panel.md are made that way).
#include "ProjectPanelUI.hpp"

#include <imgui.h>

#include <string>
#include <vector>

namespace oked::projectpanel {

std::string DrawContextMenu(const std::vector<okaypb::ctx::Item>& items) {
    std::string chosen;
    for (const okaypb::ctx::Item& it : items) {
        if (it.separator) { ImGui::Separator(); continue; }
        if (it.heading) { ImGui::TextDisabled("%s", it.label.c_str()); continue; }
        if (!it.submenu.empty()) {
            const bool open = ImGui::BeginMenu(it.label.c_str(), it.enabled);
            if (!it.enabled && !it.reason.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", it.reason.c_str());
            if (open) {
                const std::string sub = DrawContextMenu(it.children);
                if (!sub.empty()) chosen = sub;
                ImGui::EndMenu();
            }
            continue;
        }
        if (ImGui::MenuItem(it.label.c_str(), it.shortcut.empty() ? nullptr : it.shortcut.c_str(), it.checked, it.enabled)) chosen = it.id;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            const std::string& tip = it.enabled ? it.tip : it.reason;
            if (!tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    return chosen;
}

void DrawPropertyRows(const std::vector<okaypb::ctx::Property>& rows) {
    if (!ImGui::BeginTable("##proptable", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) return;
    ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch);
    for (const okaypb::ctx::Property& p : rows) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", p.label.c_str());
        ImGui::TableSetColumnIndex(1);
        if (p.warn) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.25f, 1.0f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0f);
        ImGui::TextWrapped("%s", p.value.c_str());
        ImGui::PopTextWrapPos();
        if (p.warn) ImGui::PopStyleColor();
    }
    ImGui::EndTable();
}

} // namespace oked::projectpanel
