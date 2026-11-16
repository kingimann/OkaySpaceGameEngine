// The Dear ImGui bodies of the Problems, Stats and History panels (see EditorPanelViews.hpp). The drawing is what the editor's
// DrawProblems, DrawStats and DrawHistory drew before they were split from the editor's state; the right-click menus are new
// and the OkayUI ports have the same ones.
#include "EditorPanelViews.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace oked {

using namespace okaypanels;

namespace {

ImVec4 Rgb(const float c[3], float a = 1.0f) { return ImVec4(c[0], c[1], c[2], a); }

bool SearchFieldOr(const PanelViewHooks& hooks, const char* id, char* buf, std::size_t n, const char* hint, float width) {
    if (hooks.searchField) return hooks.searchField(id, buf, n, hint, width);
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x + width;
    ImGui::SetNextItemWidth(std::max(w, ImGui::GetFrameHeight() * 3.0f));
    return ImGui::InputTextWithHint(id, hint, buf, n);
}

} // namespace

// ---- Problems ---------------------------------------------------------------------------------------------------------------

ProblemsIntent DrawProblemsBodyImGui(const ProblemsModel& m, ProblemsState& st, const PanelViewHooks& hooks) {
    ProblemsIntent out;
    const ImGuiStyle& sty = ImGui::GetStyle();
    auto pillW = [&](int count) {
        char b[16]; std::snprintf(b, sizeof(b), "%d", count);
        return ImGui::CalcTextSize(b).x + ImGui::GetFontSize() * 0.9f + sty.FramePadding.x * 2.0f;
    };
    const float pills = pillW(m.errors) + pillW(m.warnings) + pillW(m.infos) + sty.ItemSpacing.x;
    SearchFieldOr(hooks, "##pfilter", st.filter, sizeof(st.filter), "Search problems", -(pills + sty.ItemSpacing.x));
    auto pill = [&](const char* id, bool& on, int sev, int count, const char* tip) {
        float c[3]; ProblemSeverityColor(sev, c);
        const ImVec4 col = Rgb(c);
        char buf[32]; std::snprintf(buf, sizeof(buf), "%d%s", count, id);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(1.0f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(on ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? col : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Button(buf, ImVec2(pillW(count), 0))) on = !on;
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        const ImVec2 mn = ImGui::GetItemRectMin();
        const float dotR = ImGui::GetFontSize() * 0.2f;
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(mn.x + sty.FramePadding.x + dotR, (mn.y + ImGui::GetItemRectMax().y) * 0.5f), dotR,
            ImGui::GetColorU32(on ? col : ImVec4(col.x, col.y, col.z, 0.35f)));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };
    ImGui::SameLine();
    pill("##pe", st.showErr, ProblemError, m.errors, "Show errors");
    ImGui::SameLine(0, sty.ItemSpacing.x * 0.5f);
    pill("##pw", st.showWarn, ProblemWarning, m.warnings, "Show warnings");
    ImGui::SameLine(0, sty.ItemSpacing.x * 0.5f);
    pill("##pi", st.showInfo, ProblemInfo, m.infos, "Show notes");
    ImGui::Separator();

    const std::vector<ProblemGroup> groups = GroupProblems(m, st);
    if (groups.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ProblemsEmptyText(m));
    }
    for (const ProblemGroup& g : groups) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ProblemGroupHeader(g).c_str());
        ImGui::Indent(8.0f);
        for (int i : g.rows) {
            const ProblemRow& d = m.rows[(std::size_t)i];
            ImGui::PushID((int)d.id);
            ImGui::PushID(i);
            if (d.canFix) {
                if (ImGui::SmallButton("Fix")) out.fix = i;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.hint.c_str());
                ImGui::SameLine();
            }
            float c[3]; ProblemSeverityColor(d.severity, c);
            ImGui::PushStyleColor(ImGuiCol_Text, Rgb(c));
            ImGui::TextUnformatted(ProblemRowText(d).c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked()) out.open = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ProblemRowTooltip(d).c_str());
            if (ImGui::BeginPopupContextItem("##prowmenu")) {
                if (ImGui::MenuItem("Go to source")) out.open = i;
                if (d.canFix && ImGui::MenuItem("Fix")) out.fix = i;
                if (ImGui::MenuItem("Copy message")) out.copy = i;
                ImGui::EndPopup();
            }
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::Unindent(8.0f);
    }
    return out;
}

// ---- Stats ------------------------------------------------------------------------------------------------------------------

StatsIntent DrawStatsBodyImGui(StatsModel& m, const PanelViewHooks& hooks) {
    StatsIntent out;
    static const FpsHistory kEmpty;
    const FpsHistory& hist = m.history ? *m.history : kEmpty;
    const FpsSummary fs = SummarizeFps(hist);
    {   // big FPS readout + frame time, min and average at the right
        float c[3]; FpsColor(m.framerate, c);
        if (hooks.headingFont) ImGui::PushFont(hooks.headingFont);
        ImGui::TextColored(Rgb(c), "%.0f", m.framerate);
        if (hooks.headingFont) ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("FPS   %.2f ms", m.framerate > 0 ? 1000.0f / m.framerate : 0.0f);
        char mm[48]; std::snprintf(mm, sizeof(mm), "avg %.0f  min %.0f", fs.avg, fs.min);
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(mm).x);
        ImGui::TextDisabled("%s", mm);
    }
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.45f, 0.70f, 1.0f, 1.0f));
    ImGui::PlotLines("##fps", hist.values, FpsHistory::kCount, hist.head, nullptr, 0.0f, fs.scale, ImVec2(-1, 48));
    ImGui::PopStyleColor();
    {   // 60 FPS guide line over the plot
        const ImVec2 gmn = ImGui::GetItemRectMin(), gmx = ImGui::GetItemRectMax();
        if (fs.scale > 60.0f) {
            const float gy = gmx.y - (60.0f / fs.scale) * (gmx.y - gmn.y);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(gmn.x, gy), ImVec2(gmx.x, gy), IM_COL32(110, 220, 130, 110), 1.0f);
        }
    }
    ImGui::Spacing();
    // Scene census as a two-column grid; zero counts are dimmed so the numbers that matter stand out.
    if (ImGui::BeginTable("##statgrid", 4, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("n1", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("v1", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("n2", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("v2", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        for (std::size_t i = 0; i < m.counters.size(); ++i) {
            if (i % 2 == 0) ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", m.counters[i].name.c_str());
            ImGui::TableNextColumn();
            char num[24]; FormatStatNumber(m.counters[i].value, num, sizeof(num));
            if (m.counters[i].value == 0) ImGui::TextDisabled("%s", num);
            else                          ImGui::TextUnformatted(num);
        }
        ImGui::EndTable();
    }
    if (ImGui::BeginPopupContextItem("##statsmenu")) {   // the table is the last item: right-click anywhere on the census
        if (ImGui::MenuItem("Copy stats")) out.copy = true;
        ImGui::EndPopup();
    }
    ImGui::Spacing();
    auto box = [&](const char* label, bool* v) { if (v && ImGui::Checkbox(label, v)) out.changed = true; };
    box("Frustum culling", m.frustumCulling);
    ImGui::SameLine();
    box("Occlusion", m.occlusionCulling);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kOcclusionTip);
    box("Static batching", m.staticBatching);
    ImGui::SameLine();
    box("Dynamic batching", m.dynamicBatching);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kBatchingTip);
    ImGui::SameLine();
    ImGui::TextDisabled("| skinning: %s", m.gpuSkinning ? "GPU" : "CPU");
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", kStatsFooter);
    ImGui::PopStyleColor();
    return out;
}

// ---- History ----------------------------------------------------------------------------------------------------------------

HistoryIntent DrawHistoryBodyImGui(const HistoryModel& m, const PanelViewHooks&) {
    HistoryIntent out;
    ImGui::BeginDisabled(m.undoDepth <= 0);
    if (ImGui::Button("Undo##hist")) out.undo = 1;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+Z");
    ImGui::SameLine();
    ImGui::BeginDisabled(m.redoDepth <= 0);
    if (ImGui::Button("Redo##hist")) out.redo = 1;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+Y");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", HistoryStatusText(m).c_str());
    ImGui::Separator();
    ImGui::BeginChild("##histlist");
    if (m.undoDepth <= 0 && m.redoDepth <= 0) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextDisabled("%s", kHistoryEmpty);
        ImGui::EndChild();
        return out;
    }
    // Timeline rail down the left: a node per state, the current one filled with the accent colour, future (redo) states
    // hollow and dimmed.
    ImDrawList* hdl = ImGui::GetWindowDrawList();
    const float railX = ImGui::GetCursorScreenPos().x + 9.0f;
    const float railTopY = ImGui::GetCursorScreenPos().y;
    const std::vector<HistoryRow> rows = HistoryRows(m);
    {
        const float rowH = ImGui::GetTextLineHeightWithSpacing();
        hdl->AddLine(ImVec2(railX, railTopY + 4.0f), ImVec2(railX, railTopY + (float)rows.size() * rowH - 4.0f),
                     ImGui::GetColorU32(ImGuiCol_Border), 1.0f);
    }
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const HistoryRow& row = rows[r];
        const bool current = row.kind == HistoryCurrent, ahead = row.kind == HistoryAhead;
        char lbl[220]; std::snprintf(lbl, sizeof(lbl), "     %s##hrow%d", row.label.c_str(), (int)r);
        if (ahead) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const bool hit = ImGui::Selectable(lbl, current);
        if (ahead) ImGui::PopStyleColor();
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        const float cy = (mn.y + mx.y) * 0.5f;
        if (current) hdl->AddCircleFilled(ImVec2(railX, cy), 4.0f, ImGui::GetColorU32(ImGuiCol_CheckMark), 12);
        else hdl->AddCircle(ImVec2(railX, cy), 3.0f,
                            ahead ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ImGui::GetColorU32(ImGuiCol_Text, 0.55f), 12, 1.5f);
        bool jump = hit;
        if (ImGui::BeginPopupContextItem()) {
            if (!current && ImGui::MenuItem("Jump to this state")) jump = true;
            if (ImGui::MenuItem("Copy name")) out.copy = (int)r;
            ImGui::EndPopup();
        }
        if (jump && !current && out.undo == 0 && out.redo == 0) {
            if (ahead) out.redo = row.steps; else out.undo = row.steps;
        }
    }
    ImGui::EndChild();
    return out;
}

} // namespace oked
