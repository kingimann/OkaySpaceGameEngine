// The Dear ImGui bodies of the Problems, Stats, History, Tags and Layers, Physics and Profiler panels (see EditorPanelViews.hpp).
// The drawing is what the editor's DrawProblems, DrawStats, DrawHistory, DrawTagsAndLayersSettings, the Physics section of
// DrawProjectSettings and DrawProfiler drew before they were split from the editor's state; the right-click menus of the first
// three are new and the OkayUI ports have the same ones.
#include "EditorPanelViews.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // PushMultiItemsWidths (the Transform's three cells, as the editor's DragVec3Axis)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

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

// ---- Tags and Layers (Project Settings) -------------------------------------------------------------------------------------

TagsLayersIntent DrawTagsLayersBodyImGui(const TagsLayersModel& m, TagsLayersState& st, const PanelViewHooks&) {
    TagsLayersIntent out;
    const ImVec4 bad(0.95f, 0.45f, 0.40f, 1.0f), warn(0.95f, 0.75f, 0.30f, 1.0f);
    bool strict = m.strict;
    if (ImGui::Checkbox("Strict tags##ps", &strict)) out.strict = strict ? 1 : 0;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kStrictTagsTip);
    // Tags the open scene uses that the list lacks (an old scene, a typo): say so, and offer to list them.
    if (!m.unlisted.empty()) {
        ImGui::TextColored(m.strict ? bad : warn, "%s", UnlistedTagsText(m.unlisted).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Add them##unlisted")) out.addUnlisted = true;
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Tags");
    ImGui::TextWrapped("%s", BuiltinTagsText(m.builtin).c_str());   // built-in tags: always there, cannot be renamed or removed
    if (m.custom.empty()) ImGui::TextDisabled("No custom tags yet.");
    const float rowH = ImGui::GetFrameHeightWithSpacing() + 2.0f;
    if (!m.custom.empty())
        ImGui::BeginChild("##pstags", ImVec2(0, std::min(rowH * 4.5f, 12.0f + rowH * (float)m.custom.size())), ImGuiChildFlags_Borders);
    ImGuiListClipper clip;
    clip.Begin((int)m.custom.size(), rowH);
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const TagRow& t = m.custom[(std::size_t)i];
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(t.name.c_str());
            ImGui::SameLine(150.0f);
            ImGui::TextDisabled("%s", TagUseText(t.used).c_str());
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 150.0f);
            if (ImGui::SmallButton("Rename")) {
                ImGui::OpenPopup("##pstagren");
                st.renaming = i; CopyToBuf(st.renameBuf, sizeof(st.renameBuf), t.name); st.renameError.clear(); st.closeRename = false;
            }
            if (ImGui::BeginPopup("##pstagren")) {   // rename it (the list, and the tag on every object of this scene that carries it)
                if (st.closeRename) { ImGui::CloseCurrentPopup(); st.closeRename = false; }
                ImGui::SetNextItemWidth(170);
                const bool enter = ImGui::InputText("##tagrename", st.renameBuf, sizeof(st.renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                const bool click = ImGui::Button("Rename");
                if (enter || click) {
                    if (t.name == st.renameBuf) ImGui::CloseCurrentPopup();
                    else { out.renameTag = i; out.renameTo = st.renameBuf; }
                }
                if (!st.renameError.empty()) ImGui::TextColored(bad, "%s", st.renameError.c_str());
                ImGui::Separator();
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) out.removeTag = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kRemoveTagTip);
            ImGui::PopID();
        }
    }
    if (!m.custom.empty()) ImGui::EndChild();
    ImGui::SetNextItemWidth(190);
    const bool enter = ImGui::InputTextWithHint("##psnewtag", "new tag name", st.newTag, sizeof(st.newTag), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Add tag##ps") || enter) out.addTag = true;
    if (!st.tagError.empty()) ImGui::TextColored(bad, "%s", st.tagError.c_str());

    ImGui::Spacing();
    ImGui::TextDisabled("Layers (0 to 31)");
    ImGui::BeginChild("##pslayers", ImVec2(0, 300), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)m.layers.size(); ++i) {
        const LayerRow& row = m.layers[(std::size_t)i];
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%2d", i);
        ImGui::SameLine(34.0f);
        char buf[kLayerBufSize];
        CopyToBuf(buf, sizeof(buf), row.name);
        ImGui::SetNextItemWidth(200);
        const bool enterHit = ImGui::InputTextWithHint("##lname", LayerHint(i, row).c_str(), buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
        if (enterHit || ImGui::IsItemDeactivatedAfterEdit()) { out.renameLayer = i; out.layerName = buf; }
        if (LayerCanReset(row)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset")) out.resetLayer = i;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (!st.layerError.empty()) ImGui::TextColored(bad, "%s", LayerErrorText(st).c_str());
    if (ImGui::SmallButton("Reset all layer names##ps")) out.resetAllLayers = true;
    return out;
}

// ---- Physics (Project Settings) ---------------------------------------------------------------------------------------------

PhysicsSettingsIntent DrawPhysicsSettingsBodyImGui(PhysicsSettingsModel& m, const PanelViewHooks&) {
    PhysicsSettingsIntent out;
    if (m.gravity2D && ImGui::DragFloat2("Gravity 2D##ps", m.gravity2D, 0.05f)) out.gravityChanged = true;
    if (m.gravity3D && ImGui::DragFloat3("Gravity 3D##ps", m.gravity3D, 0.05f)) out.gravityChanged = true;
    if (m.stepsPerSecond) {
        ImGui::SetNextItemWidth(160);
        if (ImGui::DragFloat("Steps per second##ps", m.stepsPerSecond, 1.0f, 1.0f, 240.0f, "%.0f")) out.rateChanged = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kStepRateTip);
    }
    if (m.smooth) {
        if (ImGui::Checkbox("Smooth the motion##ps", m.smooth)) out.smoothChanged = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kSmoothTip);
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", kPhysicsQualityHeading);
    int q = m.quality == 1 ? 1 : 0;
    ImGui::SetNextItemWidth(330);
    if (ImGui::Combo("Quality##psq", &q, kPhysicsQualityNames, 2)) out.quality = q;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kQualityTip);
    if (q == 1) {
        int it = m.solverIterations;
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderInt("Solver iterations##psq", &it, 4, 30)) out.solverIterations = it;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kIterationsTip);
        bool cont = m.autoContinuous;
        if (ImGui::Checkbox("Catch fast bodies automatically##psq", &cont)) out.autoContinuous = cont ? 1 : 0;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kContinuousTip);
    }
    if (ImGui::Button("Apply to current scene##phys")) out.apply = true;
    return out;
}

// ---- Profiler ---------------------------------------------------------------------------------------------------------------

ProfilerIntent DrawProfilerBodyImGui(ProfilerModel& m, const PanelViewHooks&) {
    ProfilerIntent out;
    const int n = ProfilerFrameCount(m);
    const float top = ProfilerGraphTop(m);
    // ---- Controls row ----
    if (m.paused) {
        if (ImGui::Checkbox("Pause##prof", m.paused)) out.pauseChanged = true;
        ImGui::SameLine();
    }
    ProfilerFixSelection(m);
    if (m.paused && *m.paused && m.selected) {
        ImGui::SetNextItemWidth(220);
        if (n > 1 && ImGui::SliderInt("Frame##prof", m.selected, 0, n - 1, "frame %d")) out.selectChanged = true;
    } else ImGui::TextDisabled("(pause to scrub frames)");
    ImGui::SameLine(); ImGui::TextDisabled("16.7ms=60FPS  33.3ms=30FPS");

    // ---- Timeline graphs (oldest -> newest) ----
    const std::string ov = ProfilerOverlay(m);
    ImGui::PlotLines("##frame", m.total.empty() ? nullptr : m.total.data(), n, 0, ov.c_str(), 0.0f, top, ImVec2(-1, 80));
    {   // budget guide lines over the frame graph (green 60 FPS, amber 30 FPS) and, while paused, the scrubbed frame
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        auto guide = [&](float ms, ImU32 col, const char* lbl) {
            if (top <= ms) return;
            const float y = mx.y - (ms / top) * (mx.y - mn.y);
            pdl->AddLine(ImVec2(mn.x, y), ImVec2(mx.x, y), col, 1.0f);
            pdl->AddText(ImVec2(mn.x + 4.0f, y - ImGui::GetTextLineHeight() - 1.0f), col, lbl);
        };
        guide(16.7f, IM_COL32(110, 220, 130, 170), "16.7 (60)");
        guide(33.3f, IM_COL32(240, 190, 80, 170), "33.3 (30)");
        if (m.paused && *m.paused && m.selected && n > 1 && *m.selected >= 0) {
            const float fx = mn.x + ((float)*m.selected / (float)(n - 1)) * (mx.x - mn.x);
            pdl->AddLine(ImVec2(fx, mn.y), ImVec2(fx, mx.y), IM_COL32(255, 210, 0, 220), 1.5f);
        }
    }
    ImGui::PlotLines("##gpu", m.gpu.empty() ? nullptr : m.gpu.data(), n, 0, "GPU ms", 0.0f, top, ImVec2(-1, 44));

    // ---- Readouts ----
    ImGui::Text("Frame %.2f ms (%.0f FPS)", m.frameMs, m.frameMs > 0.001 ? 1000.0 / m.frameMs : 0.0);
    ImGui::SameLine(); ImGui::TextColored(ImVec4(0.55f, 0.8f, 1, 1), "CPU %.2f", m.cpuMs);
    ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.78f, 0.45f, 1), "GPU %.2f", m.gpuMs);
    ImGui::SameLine(); ImGui::Text("| avg %.2f  peak %.2f", m.avg, m.peak);
    ImGui::Text("Meshes %d    Triangles %d    Particles %d", m.meshes, m.triangles, m.particles);

    // ---- CPU category bars (this frame) ----
    auto bar = [&](const char* label, double ms, ImVec4 col) {
        float frac = 0.0f;
        const std::string t = ProfilerBarText(label, ms, m.cpuMs, &frac);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        ImGui::ProgressBar(frac, ImVec2(-1, 0), t.c_str());
        ImGui::PopStyleColor();
    };
    bar("Scripts", m.scriptsMs, ImVec4(0.45f, 0.75f, 1.0f, 1));
    bar("Physics", m.physicsMs, ImVec4(0.55f, 0.9f, 0.55f, 1));
    bar("Render", m.renderMs, ImVec4(1.0f, 0.75f, 0.4f, 1));
    bar("Other", ProfilerOtherMs(m), ImVec4(0.7f, 0.7f, 0.75f, 1));

    // Plain-language diagnosis: what to look at.
    ImGui::TextColored(ImVec4(0.85f, 0.9f, 1.0f, 1.0f), "Diagnosis:");
    ImGui::SameLine(); ImGui::TextWrapped("%s", ProfilerDiagnosis(m).c_str());
    ImGui::Separator();

    // ---- CSV copy of the displayed frame ----
    if (ImGui::SmallButton("Copy CSV##prof")) out.copyCsv = true;
    ImGui::SameLine(); ImGui::TextDisabled("section breakdown  (?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kProfilerHelp);

    // ---- Per-section table: self/total/calls/%, indented by call depth, with stats ----
    if (ImGui::BeginTable("profrows", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                             ImGuiTableFlags_ScrollY, ImVec2(0, 220))) {
        ImGui::TableSetupColumn(kProfilerColumns[0], ImGuiTableColumnFlags_WidthStretch, 2.2f);
        for (int c = 1; c < 7; ++c) ImGui::TableSetupColumn(kProfilerColumns[c]);
        ImGui::TableHeadersRow();
        for (int r : ProfilerRows(m)) {
            const ProfilerSection& b = m.sections[(std::size_t)r];
            char cells[6][24]; ProfilerCells(m, b, cells);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (b.depth > 0) ImGui::Indent(b.depth * 12.0f);
            if (ProfilerHot(m, b)) ImGui::TextColored(ImVec4(1, 0.5f, 0.45f, 1), "%s", b.name.c_str());   // hot sections in red
            else ImGui::TextUnformatted(b.name.c_str());
            if (b.depth > 0) ImGui::Unindent(b.depth * 12.0f);
            for (int c = 0; c < 6; ++c) { ImGui::TableNextColumn(); ImGui::TextUnformatted(cells[c]); }
        }
        ImGui::EndTable();
    }
    if (m.sections.empty()) ImGui::TextDisabled("%s", ProfilerEmptyText(m));
    return out;
}

// ---- Import settings (the Project panel) -----------------------------------------------------------------------------------

namespace {
// The import panel's combo: 160 pixels, the label after it (BeginCombo and a Selectable per entry, as the editor drew it).
bool ImportCombo(const char* label, int& value, const char* const* items, int count) {
    bool changed = false;
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo(label, value >= 0 && value < count ? items[value] : "")) {
        for (int i = 0; i < count; ++i)
            if (ImGui::Selectable(items[i], value == i)) { value = i; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}
} // namespace

ImportSettingsIntent DrawImportSettingsBodyImGui(ImportSettingsModel& m, ImportSettingsState& st, const PanelViewHooks&) {
    ImportSettingsIntent out;
    if (!m.edit) return out;
    ImportFields& e = *m.edit;
    ImGui::TextDisabled("%s", kImportHeading);
    if (m.newer) { ImGui::TextDisabled("%s", kImportNewerNote); return out; }
    switch (e.kind) {
        case ImportTexture: {
            int si = ImportSizeIndex(e.maxSize);
            if (ImportCombo("Largest side", si, kImportSizeNames, 11)) { e.maxSize = kImportSizes[si]; out.changed = true; }
            ImGui::TextDisabled("Bigger pictures are made smaller when loaded.");
            if (m.showSampler) {
                if (ImportCombo("Filter", e.filter, kImportFilterNames, 3)) out.changed = true;
                ImGui::TextDisabled("Point keeps hard pixel edges.");
                if (ImportCombo("Wrap", e.wrap, kImportWrapNames, 3)) out.changed = true;
                ImGui::TextDisabled("What is drawn past the picture's edge.");
                if (ImGui::Checkbox("Mipmaps", &e.mipmaps)) out.changed = true;
                ImGui::TextDisabled("Smaller copies for far away. Off: always full size.");
                ImGui::TextDisabled("Applies in the editor view and OpenGL / software games.");
            }
            break;
        }
        case ImportModel: {
            ImGui::SetNextItemWidth(160);
            if (ImGui::DragFloat("Scale", &e.scale, 0.01f, 0.0001f, 10000.0f, "%.4f")) out.changed = true;
            ImGui::TextDisabled("Multiplies its size when you add it to a scene.");
            if (ImportCombo("Normals", e.normals, kImportNormalsNames, 3)) out.changed = true;
            if (e.normals == 1) {
                ImGui::SetNextItemWidth(160);
                if (ImGui::DragFloat("Smoothing angle", &e.smoothingAngle, 0.5f, 0.0f, 180.0f, "%.0f deg")) out.changed = true;
                ImGui::TextDisabled("Sharper edges stay hard. Not for rigged parts.");
            } else if (e.normals == 2) {
                ImGui::TextDisabled("Each face shaded alone. Not for rigged parts.");
            }
            if (ImGui::Checkbox("Import animations", &e.importAnimations)) out.changed = true;
            ImGui::TextDisabled("Off: no clips, rest pose.");
            ImGui::TextDisabled("Used when the model is next added to a scene.");
            break;
        }
        case ImportAudio: {
            if (ImportCombo("Loading", e.loadMode, kImportLoadNames, 2)) out.changed = true;
            ImGui::TextDisabled("Read while it plays keeps a long file in a small fixed amount of memory.");
            if (ImGui::Checkbox("Mix down to one channel", &e.forceMono)) out.changed = true;
            ImGui::TextDisabled("Left and right become a single channel. Off keeps the recording as it is.");
            ImGui::TextDisabled("Used the next time the file is loaded.");
            break;
        }
        default: return out;
    }
    const bool dirty = !ImportSame(e, m.original);
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Apply")) out.apply = true;
    ImGui::SameLine();
    if (ImGui::Button("Revert")) out.revert = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", kImportSavedNote);
    if (st.showFail) { ImGui::OpenPopup("##impfail"); st.showFail = false; }
    if (ImGui::BeginPopup("##impfail")) { ImGui::TextUnformatted(st.failMsg.c_str()); ImGui::EndPopup(); }
    return out;
}

// ---- Post-processing (Lighting) -----------------------------------------------------------------------------------------------

PostProcessIntent DrawPostProcessBodyImGui(PostProcessModel& m, const PanelViewHooks&) {
    PostProcessIntent out;
    if (!m.v) return out;
    PostValues& p = *m.v;
    auto tip = [](const char* t) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t); };
    bool& ch = out.changed;
    // Look presets fill sky / fog / post / weather in one go; Reset puts them back.
    if (ImGui::BeginCombo("Look Preset", "Choose...")) {
        for (int i = 0; i < (int)m.presets.size(); ++i)
            if (ImGui::Selectable(m.presets[(std::size_t)i].c_str())) out.preset = i;
        ImGui::EndCombo();
    }
    tip(kLookPresetTip);
    ImGui::SameLine();
    if (ImGui::Button("Reset##look")) out.reset = true;
    tip(kLookResetTip);
    ImGui::TextDisabled("%s", PostChainText(m.chainActive));

    if (ImGui::Checkbox("Filmic Rendering (linear lighting + tonemap)", &p.filmic)) ch = true;
    tip(kFilmicTip);
    if (ImGui::SliderFloat("Exposure", &p.exposure, 0.1f, 4.0f, "%.2f")) ch = true;
    if (ImGui::Combo("Tonemap", &p.tonemap, kTonemapNames, 3)) ch = true;
    tip(kTonemapTip);
    if (ImGui::Checkbox("Bloom", &p.bloom)) ch = true;
    if (p.bloom) {
        if (ImGui::SliderFloat("Threshold##bloom", &p.bloomThreshold, 0.0f, 3.0f, "%.2f")) ch = true;
        tip(kBloomThresholdTip);
        if (ImGui::SliderFloat("Intensity##bloom", &p.bloomIntensity, 0.0f, 3.0f, "%.2f")) ch = true;
        if (ImGui::SliderFloat("Radius##bloom", &p.bloomRadius, 0.25f, 3.0f, "%.2f")) ch = true;
        if (ImGui::SliderFloat("Softness##bloom", &p.bloomSoftness, 0.0f, 1.0f, "%.2f")) ch = true;
        tip(kBloomSoftnessTip);
    }
    if (ImGui::Checkbox("Colour Grading", &p.grading)) ch = true;
    if (p.grading) {
        if (ImGui::SliderFloat("Temperature", &p.temperature, -1.0f, 1.0f, "%.2f")) ch = true;
        tip(kTemperatureTip);
        if (ImGui::SliderFloat("Tint", &p.tint, -1.0f, 1.0f, "%.2f")) ch = true;
        if (ImGui::SliderFloat("Saturation", &p.saturation, 0.0f, 2.0f, "%.2f")) ch = true;
        if (ImGui::SliderFloat("Contrast", &p.contrast, 0.5f, 2.0f, "%.2f")) ch = true;
        if (ImGui::SliderFloat3("Lift", p.lift, -0.5f, 0.5f, "%.2f")) ch = true;
        tip(kLiftTip);
        if (ImGui::SliderFloat3("Gamma", p.gamma, -1.0f, 1.0f, "%.2f")) ch = true;
        tip(kGammaTip);
        if (ImGui::SliderFloat3("Gain", p.gain, -0.5f, 0.5f, "%.2f")) ch = true;
        tip(kGainTip);
    }
    if (ImGui::SliderFloat("Vignette", &p.vignette, 0.0f, 1.0f, "%.2f")) ch = true;
    tip(kVignetteTip);
    auto note = [](const std::string& n) {
        if (n.empty()) return;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", n.c_str());
        ImGui::PopTextWrapPos();
    };
    if (p.vignette > 0.0f) {
        if (ImGui::SliderFloat("Vignette Smoothness", &p.vignetteSmooth, 0.05f, 1.0f, "%.2f")) ch = true;
        note(m.vignetteNote);
    }
    if (ImGui::Checkbox("Anti-aliasing (FXAA)", &p.fxaa)) ch = true;
    if (ImGui::Checkbox("Ambient Occlusion (SSAO)", &p.ssao)) ch = true;
    note(m.ssaoNote);
    if (p.ssao) {
        if (ImGui::SliderFloat("Radius##ssao", &p.ssaoRadius, 0.05f, 3.0f, "%.2f")) ch = true;
        if (ImGui::SliderFloat("Intensity##ssao", &p.ssaoIntensity, 0.0f, 2.0f, "%.2f")) ch = true;
        if (ImGui::SliderFloat("Power##ssao", &p.ssaoPower, 0.25f, 4.0f, "%.2f")) ch = true;
        tip(kSsaoPowerTip);
    }
    if (ImGui::Checkbox("Depth of Field", &p.dof)) ch = true;
    note(m.dofNote);
    if (p.dof) {
        if (ImGui::DragFloat("Focus Distance", &p.dofFocus, 0.1f, 0.1f, 500.0f, "%.1f")) ch = true;
        if (ImGui::DragFloat("Focus Range", &p.dofRange, 0.1f, 0.1f, 500.0f, "%.1f")) ch = true;
        tip(kDofRangeTip);
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", kPostRendererNote);
    ImGui::PopTextWrapPos();
    return out;
}

// ---- Player and Rendering (Project Settings) --------------------------------------------------------------------------------

PlayerSettingsIntent DrawPlayerSettingsBodyImGui(PlayerSettingsModel& m, const PanelViewHooks&) {
    PlayerSettingsIntent out;
    if (m.company && ImGui::InputText("Company##ps", m.company, m.companySize)) out.changed = true;
    if (m.version) {
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputText("Version##ps", m.version, m.versionSize)) out.changed = true;
    }
    if (m.width && m.height) {
        ImGui::SetNextItemWidth(110); if (ImGui::InputInt("Width##ps", m.width)) out.changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110); if (ImGui::InputInt("Height##ps", m.height)) out.changed = true;
    }
    return out;
}

RenderingSettingsIntent DrawRenderingSettingsBodyImGui(RenderingSettingsModel& m, const PanelViewHooks&) {
    RenderingSettingsIntent out;
    if (m.ambient && ImGui::SliderFloat("Ambient##ps", m.ambient, 0.0f, 1.0f)) out.changed = true;
    if (m.skybox && ImGui::Checkbox("Skybox##ps", m.skybox)) out.changed = true;
    if (ImGui::Button("Apply to current scene##rend")) out.apply = true;
    return out;
}

} // namespace oked

// ---- Hierarchy --------------------------------------------------------------------------------------------------------------------
// editor/main.cpp DrawHierarchy, drawn from the model (shared/HierarchyPanelModel.hpp): the same toolbar, scene strip, tree rows
// (Dear ImGui tree nodes, flat, placed at their own y), icons, badges, eye, padlock, drop lines, search list and menus.

namespace oked {

namespace {

struct ImPen {
    ImDrawList* dl;
    void line(float x0, float y0, float x1, float y1, std::uint32_t c, float t) { dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), c, t); }
    void rect(float x0, float y0, float x1, float y1, std::uint32_t c, float r, float t) { dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), c, r, 0, t); }
    void rectFilled(float x0, float y0, float x1, float y1, std::uint32_t c, float r) { dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), c, r); }
    void circle(float cx, float cy, float r, std::uint32_t c, int seg, float t) { dl->AddCircle(ImVec2(cx, cy), r, c, seg, t); }
    void circleFilled(float cx, float cy, float r, std::uint32_t c, int seg) { dl->AddCircleFilled(ImVec2(cx, cy), r, c, seg); }
    void triangleFilled(float ax, float ay, float bx, float by, float cx, float cy, std::uint32_t c) { dl->AddTriangleFilled(ImVec2(ax, ay), ImVec2(bx, by), ImVec2(cx, cy), c); }
    void polyline(const float* xy, int n, std::uint32_t c, bool closed, float t) {
        ImVec2 pts[16]; n = std::min(n, 16);
        for (int i = 0; i < n; ++i) pts[i] = ImVec2(xy[i * 2], xy[i * 2 + 1]);
        dl->AddPolyline(pts, n, c, closed ? ImDrawFlags_Closed : 0, t);
    }
    void arc(float cx, float cy, float r, float a0, float a1, int seg, std::uint32_t c, float t) { dl->PathArcTo(ImVec2(cx, cy), r, a0, a1, seg); dl->PathStroke(c, 0, t); }
    void text(float x, float y, std::uint32_t c, const char* s) { dl->AddText(ImVec2(x, y), c, s); }
};

ImVec4 Unpack(std::uint32_t c) { return ImGui::ColorConvertU32ToFloat4(c); }

// editor/EditorWidgets.cpp IconButton with the plus or the three dots.
bool HierToolButton(const char* id, bool plus, bool active, const char* tip, std::uint32_t accent) {
    const float h = ImGui::GetFrameHeight();
    if (active) {
        ImVec4 a = Unpack(accent);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(a.x, a.y, a.z, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(a.x, a.y, a.z, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(a.x, a.y, a.z, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    }
    const bool clicked = ImGui::Button(id, ImVec2(h, h));
    if (active) ImGui::PopStyleColor(4);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImU32 col = ImGui::GetColorU32(active ? ImVec4(1, 1, 1, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (!active && !ImGui::IsItemHovered()) col = ImGui::GetColorU32(ImGuiCol_Text, 0.8f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    const float s = h * 0.5f;
    if (plus) {
        dl->AddLine(ImVec2(c.x - s * 0.4f, c.y), ImVec2(c.x + s * 0.4f, c.y), col, 2.0f);
        dl->AddLine(ImVec2(c.x, c.y - s * 0.4f), ImVec2(c.x, c.y + s * 0.4f), col, 2.0f);
    } else {
        for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(ImVec2(c.x + i * s * 0.36f, c.y), s * 0.1f, col);
    }
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return clicked;
}

// MenuEntriesUI.hpp DrawEntries on the panel's data form of a menu.
void HierMenuEntries(const std::vector<PanelMenuEntry>& es, std::string& chosen) {
    for (const PanelMenuEntry& e : es) {
        switch (e.kind) {
        case PanelMenuSeparator: ImGui::Separator(); break;
        case PanelMenuHeading:
            if (e.color) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(e.color | 0xff000000u), "%s", e.label.c_str());
            else ImGui::TextDisabled("%s", e.label.c_str());
            break;
        case PanelMenuSub: {
            const bool open = ImGui::BeginMenu(e.label.c_str(), e.enabled);
            if (!open && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (!e.enabled && !e.reason.empty()) ImGui::SetTooltip("%s", e.reason.c_str());
                else if (!e.tip.empty()) ImGui::SetTooltip("%s", e.tip.c_str());
            }
            if (open) { HierMenuEntries(e.children, chosen); ImGui::EndMenu(); }
            break;
        }
        default: {
            const bool hit = ImGui::MenuItem(e.label.c_str(), e.shortcut.empty() ? nullptr : e.shortcut.c_str(), e.kind == PanelMenuToggle && e.checked, e.enabled);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (!e.enabled && !e.reason.empty()) ImGui::SetTooltip("%s", e.reason.c_str());
                else if (!e.tip.empty()) ImGui::SetTooltip("%s", e.tip.c_str());
            }
            if (hit && e.enabled) chosen = e.id;
            break;
        }
        }
    }
}

std::string HierEllipsizeIm(const std::string& s, float maxW) {
    return HierEllipsize(s, maxW, [](const std::string& t) { return ImGui::CalcTextSize(t.c_str()).x; });
}

} // namespace

HierarchyIntent DrawHierarchyBodyImGui(const HierarchyModel& m, HierarchyState& st, const PanelViewHooks& hooks) {
    HierarchyIntent in;
    st.rowsFilled = 0;
    const std::uint32_t accent = hooks.accent;
    bool openMenu = false;
    // One toolbar row: [+] [search.............] [...]
    if (HierToolButton("##hcreate", true, false, "Create object", accent)) { st.menuKind = HierMenuCreate; st.menuRow = 0; openMenu = true; }
    ImGui::SameLine();
    if (SearchFieldOr(hooks, "##hfilter", st.filter, sizeof(st.filter), "Search", -(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x)))
        in.filterChanged = true;
    if (ImGui::IsItemHovered() && !st.filter[0]) ImGui::SetTooltip("%s", kHierSearchTip);
    ImGui::SameLine();
    if (HierToolButton("##hmore", false, m.sortAZ, "Hierarchy options", accent)) { st.menuKind = HierMenuOptions; st.menuRow = 0; openMenu = true; }
    // Scene header: name (with unsaved marker) and object count, as a quiet strip.
    {
        ImGui::Spacing();
        char cnt[24]; std::snprintf(cnt, sizeof(cnt), "%d", m.objectCount);
        const std::string title = HierSceneTitle(m.sceneName, m.dirty);
        ImGui::TextUnformatted(title.c_str());
        if (m.sceneDiag > 0) {
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            ImPen pen{ImGui::GetWindowDrawList()};
            const ImVec2 bang = ImGui::CalcTextSize("!");
            HierDrawDiag(pen, m.sceneDiag, mx.x + 16.0f, (mn.y + mx.y) * 0.5f, bang.x, bang.y);
        }
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(cnt).x);
        ImGui::TextDisabled("%s", cnt);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Objects in the scene");
    }
    ImGui::Separator();
    // The search bar and scene header stay put; only the rows scroll.
    ImGui::BeginChild("##hiertree", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImPen pen{dl};
    const float rowH = ImGui::GetTextLineHeightWithSpacing();
    const float listX = ImGui::GetCursorPosX(), listY = ImGui::GetCursorPosY();
    const float viewH = ImGui::GetWindowHeight();
    const float fs = ImGui::GetFontSize();
    const ImVec2 bang = ImGui::CalcTextSize("!");
    if (m.revealRow >= 0 && !m.searching) {
        const float rel = ImGui::GetScrollY() - listY;
        const float want = HierRevealScroll(m.revealRow, rowH, rel, viewH);
        if (want != rel) ImGui::SetScrollY(want + listY);
    }
    st.scrollY = ImGui::GetScrollY();
    const HierClipRange clip = HierVisibleRange(m.rowCount, rowH, ImGui::GetScrollY() - listY, viewH);
    if (m.rowCount == 0 && !m.searching) {   // editor/main.cpp EmptyState: a diamond, the title, the hint wrapped, all centred
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y < 40.0f) ImGui::TextDisabled("%s", kHierEmptyTitle);
        else {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.34f);
            auto centred = [&](const char* txt, ImVec4 col) {
                const float x0 = ImGui::GetCursorPosX();
                for (const std::string& l : HierWrapWords(txt, std::max(40.0f, avail.x - 16.0f), [](const std::string& t) { return ImGui::CalcTextSize(t.c_str()).x; })) {
                    ImGui::SetCursorPosX(x0 + std::max(0.0f, (avail.x - ImGui::CalcTextSize(l.c_str()).x) * 0.5f));
                    ImGui::TextColored(col, "%s", l.c_str());
                }
            };
            const float gs = fs * 1.6f;
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            const ImVec2 c(cp.x + avail.x * 0.5f, cp.y + gs * 0.6f);
            const ImU32 gc = ImGui::GetColorU32(ImVec4(0.40f, 0.42f, 0.47f, 1.0f));
            dl->AddQuad(ImVec2(c.x, c.y - gs * 0.55f), ImVec2(c.x + gs * 0.55f, c.y), ImVec2(c.x, c.y + gs * 0.55f), ImVec2(c.x - gs * 0.55f, c.y), gc, 2.0f);
            dl->AddCircleFilled(c, gs * 0.14f, gc);
            ImGui::Dummy(ImVec2(0, gs * 1.3f));
            ImGui::Spacing();
            centred(kHierEmptyTitle, ImVec4(0.66f, 0.68f, 0.72f, 1.0f));
            ImGui::Spacing();
            centred(kHierEmptyHint, ImVec4(0.46f, 0.47f, 0.51f, 1.0f));
        }
    }
    const ImGuiPayload* live = ImGui::GetDragDropPayload();
    HierRow row;
    for (int ri = clip.first; ri < clip.last; ++ri) {
        row = HierRow();
        if (m.fillRow) m.fillRow(ri, row);
        ++st.rowsFilled;
        ImGui::SetCursorPos(ImVec2(listX, listY + ri * rowH));
        if (row.header) {   // a combined scene's group title
            const ImU32 col = row.headerMain ? IM_COL32(150, 152, 160, 255) : IM_COL32(120, 190, 255, 255);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(("    " + row.name).c_str());
            ImGui::PopStyleColor();
            const ImVec2 hmn = ImGui::GetItemRectMin(), hmx = ImGui::GetItemRectMax();
            const float cy2 = hmn.y + ImGui::GetTextLineHeight() * 0.52f;
            dl->AddTriangleFilled(ImVec2(hmn.x + 2.0f, cy2 - fs * 0.14f), ImVec2(hmn.x + 2.0f + fs * 0.42f, cy2 - fs * 0.14f),
                                  ImVec2(hmn.x + 2.0f + fs * 0.21f, cy2 + fs * 0.22f), col);
            dl->AddLine(ImVec2(hmn.x, hmx.y + 1.0f), ImVec2(hmn.x + ImGui::GetContentRegionAvail().x, hmx.y + 1.0f), ImGui::GetColorU32(ImGuiCol_Separator));
            continue;
        }
        ImGui::PushID((int)ri);
        if (m.searching) {   // a flat list of the matches, each with its parents after it
            const float searchRoom = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("     ").x - (row.diag ? 28.0f : 6.0f);
            const std::string shown = HierEllipsizeIm(row.name, searchRoom);
            if (ImGui::Selectable(("     " + shown).c_str(), row.selected)) {
                if (ImGui::GetIO().KeyCtrl) in.toggle = row.id; else in.select = row.id;
            }
            if (shown != row.name && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row.name.c_str());
            const ImVec2 smn = ImGui::GetItemRectMin(), smx = ImGui::GetItemRectMax();
            HierDrawIcon(pen, row.icon, smn.x + 2.0f, smn.y, fs);
            float hv[4]; HierDrawPrefabMark(pen, row, smn.x + 2.0f, smn.y, fs, hv);
            if (!row.path.empty()) {
                const float nx = smn.x + ImGui::CalcTextSize(("     " + shown).c_str()).x + 12.0f;
                dl->AddText(ImVec2(nx, smn.y + 1.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.75f), ("in " + row.path).c_str());
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) in.frame = row.id;
            HierDrawBadges(pen, row.badges, smx.x, smn.y, smx.y, 0.0f);
            HierDrawDiag(pen, row.diag, smx.x - 48.0f, (smn.y + smx.y) * 0.5f, bang.x, bang.y);
            ImGui::PopID();
            continue;
        }
        ImGui::SetCursorPosX(listX + row.depth * ImGui::GetStyle().IndentSpacing);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (row.selected) flags |= ImGuiTreeNodeFlags_Selected;
        if (row.childCount == 0) flags |= ImGuiTreeNodeFlags_Leaf;
        if (row.dim) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
        else if (row.prefabBlue) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.76f, 1.0f, 1.0f));
        {   // zebra striping: a faint band on every other object row
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if ((row.objectIndex & 1) != 0)
                dl->AddRectFilled(ImVec2(ImGui::GetWindowPos().x, p.y), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), p.y + rowH), IM_COL32(255, 255, 255, 5));
        }
        const std::string tail = HierTail(row.childCount, row.active);
        const float nameRoom = ImGui::GetContentRegionAvail().x - (fs + ImGui::GetStyle().FramePadding.x * 2.0f) - ImGui::CalcTextSize("     ").x -
                               ImGui::CalcTextSize(tail.c_str()).x - kHierEyeW * 2.0f - 8.0f - (row.diag ? 22.0f : 0.0f);
        const std::string shown = HierEllipsizeIm(row.name, nameRoom);
        const bool renaming = st.renameId != 0 && st.renameId == row.id;
        if (row.childCount > 0) ImGui::SetNextItemOpen(row.open, ImGuiCond_Always);   // the model owns the open state
        const bool open = ImGui::TreeNodeEx("##node", flags, "     %s%s", renaming ? "" : shown.c_str(), renaming ? "" : tail.c_str());
        if (row.childCount > 0 && open != row.open) { in.openId = row.id; in.open = open ? 1 : 0; }
        if (shown != row.name && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row.name.c_str());
        if (row.dim || row.prefabBlue) ImGui::PopStyleColor();
        const ImVec2 rowMin = ImGui::GetItemRectMin(), rowMax = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();
        {   // the type icon after the arrow, and the prefab mark
            const float ax = fs + ImGui::GetStyle().FramePadding.x * 2.0f;
            const float ix = rowMin.x + ax, iy = rowMin.y + (rowMax.y - rowMin.y - fs) * 0.5f;
            HierDrawIcon(pen, row.icon, ix, iy, fs);
            float hv[4]; HierDrawPrefabMark(pen, row, ix, iy, fs, hv);
            if (hv[2] > hv[0] && !row.prefabTip.empty() && ImGui::IsMouseHoveringRect(ImVec2(hv[0], hv[1]), ImVec2(hv[2], hv[3])) && ImGui::IsWindowHovered())
                ImGui::SetTooltip("%s", row.prefabTip.c_str());
        }
        if (row.selected) dl->AddRectFilled(rowMin, ImVec2(rowMin.x + 3.0f, rowMax.y), accent);   // the accent bar
        {
            const float labelEnd = rowMin.x + fs + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::CalcTextSize(("     " + shown + tail).c_str()).x + 8.0f;
            HierDrawBadges(pen, row.badges, rowMax.x - kHierEyeW * 2.0f, rowMin.y, rowMax.y, labelEnd);
        }
        HierDrawDiag(pen, row.diag, rowMax.x - 48.0f, (rowMin.y + rowMax.y) * 0.5f, bang.x, bang.y);
        HierDrawEye(pen, row.active, rowMax.x, rowMin.y, rowMax.y);
        HierDrawLock(pen, row.locked, hovered, rowMax.x, rowMin.y, rowMax.y);
        const float mx = ImGui::GetIO().MousePos.x;
        const bool inEye = mx >= rowMax.x - kHierEyeW && mx <= rowMax.x;
        const bool inLock = mx >= rowMax.x - kHierEyeW * 2.0f && mx < rowMax.x - kHierEyeW;
        const bool inArrow = row.childCount > 0 && mx >= rowMin.x && mx < rowMin.x + fs + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (hovered && inEye) ImGui::SetTooltip("%s", HierEyeTip(row.active));
        if (hovered && inLock) ImGui::SetTooltip("%s", HierLockTip(row.locked));
        if (renaming) {   // the name becomes a field in place
            ImGui::SetCursorScreenPos(ImVec2(rowMin.x + fs + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::CalcTextSize("     ").x - 4.0f, rowMin.y - 2.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
            ImGui::SetNextItemWidth(std::max(60.0f, rowMax.x - kHierEyeW * 2.0f - ImGui::GetCursorScreenPos().x));
            if (st.renameAge == 0) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##rename", st.renameBuf, sizeof(st.renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleVar();
            const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            if (enter || (st.renameAge > 0 && !ImGui::IsItemActive() && !escape)) {
                if (st.renameBuf[0]) { in.renameId = row.id; in.renameTo = st.renameBuf; }
                st.renameId = 0;
            } else if (escape) st.renameId = 0;
            else ++st.renameAge;
            ImGui::PopID();
            continue;
        }
        if (ImGui::IsItemClicked()) {
            if (inEye) in.toggleActive = row.id;
            else if (inLock) in.toggleLock = row.id;
            else if (ImGui::GetIO().KeyShift && m.activeRow >= 0) in.rangeTo = row.id;
            else if (ImGui::GetIO().KeyCtrl) in.toggle = row.id;
            else if (row.selected && m.selectedCount > 1) st.pressedMulti = row.id;
            else in.select = row.id;
            (void)inArrow;
        }
        if (st.pressedMulti == row.id && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const ImVec2 moved = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 4.0f);
            if (hovered && moved.x == 0.0f && moved.y == 0.0f) in.select = row.id;
            st.pressedMulti = 0;
        }
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("GO_PTR", &row.id, sizeof(row.id));
            ImGui::TextUnformatted(HierDragLabel(row.name, row.selected, m.selectedCount).c_str());
            st.dragging = row.id;
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            const float ty = (ImGui::GetIO().MousePos.y - rowMin.y) / (rowMax.y - rowMin.y);
            const int where = HierDropZone(ty);
            bool bad = false;
            if (live && live->IsDataType("GO_PTR") && live->Data && live->DataSize == (int)sizeof(std::uint64_t)) {
                std::uint64_t dg = 0; std::memcpy(&dg, live->Data, sizeof dg);
                bad = dg == row.id;
            }
            const ImU32 col = bad ? IM_COL32(235, 90, 80, 255) : accent;
            if (where != HierDropInside) {
                const float ly = where == HierDropBefore ? rowMin.y : rowMax.y;
                dl->AddLine(ImVec2(rowMin.x, ly), ImVec2(rowMax.x, ly), col, 2.5f);
                dl->AddCircleFilled(ImVec2(rowMin.x + 4.0f, ly), 3.5f, col);
            } else {
                dl->AddRectFilled(rowMin, rowMax, (col & 0x00FFFFFF) | 0x30000000, 3.0f);
                dl->AddRect(rowMin, rowMax, col, 3.0f, 0, 2.0f);
            }
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GO_PTR", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (p->DataSize == (int)sizeof(std::uint64_t)) { std::memcpy(&in.dragged, p->Data, sizeof in.dragged); in.dropTarget = row.id; in.dropWhere = where; }
            }
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                in.dropAsset.assign((const char*)p->Data, strnlen((const char*)p->Data, (std::size_t)std::max(0, p->DataSize)));
                in.dropTarget = row.id; in.dropWhere = where;
            }
            ImGui::EndDragDropTarget();
        }
        if (hovered && !inEye && !inLock && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            st.renameId = row.id; st.renameAge = 0; in.renameBegan = row.id;
            std::snprintf(st.renameBuf, sizeof st.renameBuf, "%s", row.name.c_str());
        }
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && st.menuKind == HierMenuNone) {
            if (!row.selected) in.select = row.id;
            st.menuKind = HierMenuRow; st.menuRow = row.id; openMenu = true;
        }
        ImGui::PopID();
    }
    ImGui::SetCursorPos(ImVec2(listX, listY + m.rowCount * rowH));   // below the last row, drawn or not
    if (m.searching) {
        ImGui::TextDisabled("%s", HierSearchFooter(m.rowCount, st.filter).c_str());
    } else {
        if (st.pressedMulti && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) st.pressedMulti = 0;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(std::max(1.0f, avail.x), std::max(24.0f, avail.y - ImGui::GetStyle().ItemSpacing.y * 2.0f)));
        if (ImGui::BeginDragDropTarget()) {
            dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), (accent & 0x00FFFFFFu) | ((std::uint32_t)(0.8f * 255.0f + 0.5f) << 24), 4.0f, 0, 2.0f);
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GO_PTR", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (p->DataSize == (int)sizeof(std::uint64_t)) { std::memcpy(&in.dragged, p->Data, sizeof in.dragged); in.dropTarget = 0; in.dropWhere = HierDropRoot; }
            }
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                in.dropAsset.assign((const char*)p->Data, strnlen((const char*)p->Data, (std::size_t)std::max(0, p->DataSize)));
                in.dropTarget = 0; in.dropWhere = HierDropRoot;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && st.menuKind == HierMenuNone) {
            st.menuKind = HierMenuEmpty; st.menuRow = 0; openMenu = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) st.dragging = 0;
    // the keys: the arrows walk the rows, Left / Right fold, Ctrl+A selects every listed object, F2 renames
    if (m.focused && st.renameId == 0 && !ImGui::GetIO().WantTextInput && m.rowCount > 0) {
        if (!m.searching && m.activeRow >= 0) {
            auto objectAt = [&](int from, int step) -> std::uint64_t {
                for (int i = from + step; i >= 0 && i < m.rowCount; i += step) {
                    HierRow r; if (m.fillRow) m.fillRow(i, r); ++st.rowsFilled;
                    if (!r.header) return r.id;
                }
                return 0;
            };
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { if (std::uint64_t id = objectAt(m.activeRow, 1)) in.select = id; }
            else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { if (std::uint64_t id = objectAt(m.activeRow, -1)) in.select = id; }
            HierRow cur; if (m.fillRow) m.fillRow(m.activeRow, cur); ++st.rowsFilled;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && cur.childCount > 0) { in.openId = cur.id; in.open = 1; }
            else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { in.openId = cur.id; in.open = 0; }
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                st.renameId = cur.id; st.renameAge = 0; in.renameBegan = cur.id;
                std::snprintf(st.renameBuf, sizeof st.renameBuf, "%s", cur.name.c_str());
            }
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) in.selectAll = true;
    }
    ImGui::EndChild();
    // the open menu
    if (openMenu) ImGui::OpenPopup("##hiermenu");
    if (st.menuKind != HierMenuNone) {
        if (ImGui::BeginPopup("##hiermenu")) {
            std::vector<PanelMenuEntry> entries;
            if (st.menuKind == HierMenuOptions) entries = HierOptionsMenu(m.sortAZ);
            else if (m.buildMenu) m.buildMenu(st.menuKind, st.menuRow, entries);
            std::string chosen;
            HierMenuEntries(entries, chosen);
            if (!chosen.empty()) { in.menuId = chosen; in.menuKind = st.menuKind; in.menuRow = st.menuRow; }
            ImGui::EndPopup();
        } else { st.menuKind = HierMenuNone; st.menuRow = 0; }
    }
    return in;
}

} // namespace oked

// ---- Inspector, stage 1 ----------------------------------------------------------------------------------------------------------
// editor/main.cpp DrawInspector's object header card, CompHeader, the Transform (DragVec3Axis), the Mesh Renderer's shading fields and
// the Add Component button, drawn from shared/InspectorPanelModel.hpp. The editor still draws its own Dear ImGui Inspector; these
// bodies are the same parts written against the model, which the comparison test runs against the OkayUI port.

namespace oked {

namespace {

bool InspSameLineOrWrapIm(float nextW) {
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float used = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
    if (right - used < nextW) return false;
    ImGui::SameLine();
    return true;
}
float InspButtonWIm(const char* label) { return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f; }

// editor/main.cpp DragVec3Axis.
bool InspVec3Im(const char* label, float v[3], float speed, const char* fmt, bool* activated) {
    bool changed = false;
    if (activated) *activated = false;
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::PushID(label);
    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        if (i > 0) ImGui::SameLine(0, st.ItemInnerSpacing.x);
        const float room = ImGui::CalcItemWidth() - st.FramePadding.x * 2.0f - 4.0f;
        const char* f = InspAxisFormat(fmt, v[i], room, [](const char* t) { return ImGui::CalcTextSize(t).x; });
        if (ImGui::DragFloat("##c", &v[i], speed, 0.0f, 0.0f, f)) changed = true;
        if (activated && ImGui::IsItemActivated()) *activated = true;
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(mn, ImVec2(mn.x + 3.0f, mx.y), kInspAxisCol[i], 2.0f);
        ImGui::PopID();
        ImGui::PopItemWidth();
    }
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
    ImGui::PopID();
    return changed;
}

} // namespace

InspectorHeaderIntent DrawInspectorHeaderBodyImGui(InspectorHeaderModel& m, InspectorHeaderState& st, const PanelViewHooks&) {
    InspectorHeaderIntent in;
    st.newTag[sizeof(st.newTag) - 1] = '\0';
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.17f, 0.20f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
    ImGui::BeginChild("##objheader", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    bool act = m.active;
    if (ImGui::Checkbox("##active", &act)) in.toggleActive = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kInspActiveTip);
    ImGui::SameLine();
    {
        const float ih = ImGui::GetFrameHeight(), isz = 17.0f;
        const ImVec2 ip = ImGui::GetCursorScreenPos();
        ImPen pen{ImGui::GetWindowDrawList()};
        HierDrawIcon(pen, m.icon, ip.x, ip.y + (ih - isz) * 0.5f, isz);
        ImGui::Dummy(ImVec2(isz + 2.0f, ih));
        ImGui::SameLine();
    }
    const float staticW = ImGui::CalcTextSize("Static").x + ImGui::CalcTextSize("Lock").x + ImGui::GetFrameHeight() * 2.0f +
                          (ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetStyle().ItemSpacing.x) * 2.0f;
    const bool narrow = ImGui::GetContentRegionAvail().x - staticW < 120.0f;
    ImGui::SetNextItemWidth(narrow ? -1.0f : ImGui::GetContentRegionAvail().x - staticW);
    if (m.name && m.nameSize > 0) { m.name[m.nameSize - 1] = '\0'; if (ImGui::InputText("##name", m.name, m.nameSize)) in.nameChanged = true; }
    if (!narrow) ImGui::SameLine();
    bool stat = m.isStatic;
    if (ImGui::Checkbox("Static", &stat)) in.toggleStatic = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kInspStaticTip);
    ImGui::SameLine();
    bool lk = m.locked;
    if (ImGui::Checkbox("Lock", &lk)) in.toggleLock = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kInspLockTip);

    const std::string curTag = m.tag.empty() ? std::string("Untagged") : m.tag;
    const float tagLblW = ImGui::CalcTextSize("Layer").x + ImGui::GetStyle().ItemInnerSpacing.x;
    const float comboW = std::clamp(ImGui::GetContentRegionAvail().x - tagLblW, 90.0f, 150.0f);
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("Tag", curTag.c_str())) {
        if (st.closeTagList) { ImGui::CloseCurrentPopup(); st.closeTagList = false; }
        std::vector<std::string> builtin, custom;
        if (m.tags) m.tags(builtin, custom);
        if (ImGui::Selectable("Untagged", curTag == "Untagged")) { in.setTag = true; in.tag.clear(); }
        for (const std::string& t : builtin)
            if (ImGui::Selectable(t.c_str(), curTag == t)) { in.setTag = true; in.tag = t; }
        if (!custom.empty()) {
            ImGui::Separator();
            for (std::size_t i = 0; i < custom.size(); ++i) {
                const std::string& t = custom[i];
                if (ImGui::Selectable(t.c_str(), curTag == t)) { in.setTag = true; in.tag = t; }
                if (ImGui::BeginPopupContextItem((std::string("tagctx") + std::to_string(i)).c_str())) {
                    if (ImGui::IsWindowAppearing()) { CopyToBuf(st.renameBuf, sizeof(st.renameBuf), t); st.renameError.clear(); }
                    if (st.closeRename) { ImGui::CloseCurrentPopup(); st.closeRename = false; }
                    ImGui::SetNextItemWidth(170);
                    const bool enter = ImGui::InputText("##tagrename", st.renameBuf, sizeof(st.renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::SameLine();
                    const bool click = ImGui::Button("Rename");
                    if (enter || click) {
                        if (t == st.renameBuf) ImGui::CloseCurrentPopup();
                        else { in.renameTag = true; in.tagFrom = t; in.tagTo = st.renameBuf; }
                    }
                    if (!st.renameError.empty()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", st.renameError.c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove tag from list")) { in.removeTag = true; in.removeTagName = t; }
                    ImGui::EndPopup();
                }
            }
        }
        ImGui::Separator();
        if (ImGui::IsWindowAppearing()) st.tagError.clear();
        ImGui::SetNextItemWidth(120);
        const bool entered = ImGui::InputTextWithHint("##newtag", "new tag...", st.newTag, sizeof(st.newTag), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const bool clicked = ImGui::Button("Add");
        if ((entered || clicked) && st.newTag[0]) { in.addTag = true; in.newTag = st.newTag; }
        if (!st.tagError.empty()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", st.tagError.c_str());
        ImGui::EndCombo();
    }
    InspSameLineOrWrapIm(comboW + tagLblW);
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("Layer", m.layerName.c_str())) {
        ImGui::TextDisabled("%s", kInspLayerListNote);
        for (int i = 0; i < 32; ++i) {
            const std::string nm = m.layerNameOf ? m.layerNameOf(i) : std::to_string(i);
            if (ImGui::Selectable((nm + "##layer" + std::to_string(i)).c_str(), m.layer == i)) in.setLayer = i;
            if (ImGui::BeginPopupContextItem((std::string("layerctx") + std::to_string(i)).c_str())) {
                std::string text, hint;
                if (m.layerRenameInfo) m.layerRenameInfo(i, text, hint);
                if (ImGui::IsWindowAppearing()) { CopyToBuf(st.renameBuf, sizeof(st.renameBuf), text); st.renameError.clear(); }
                if (st.closeRename) { ImGui::CloseCurrentPopup(); st.closeRename = false; }
                ImGui::TextDisabled("Layer %d name", i);
                ImGui::SetNextItemWidth(170);
                if (ImGui::InputTextWithHint("##layername", hint.c_str(), st.renameBuf, sizeof(st.renameBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    in.renameLayer = true; in.layerIndex = i; in.layerTo = st.renameBuf;
                }
                if (ImGui::SmallButton("Reset to default")) { in.resetLayer = true; in.layerIndex = i; }
                if (!st.renameError.empty()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", st.renameError.c_str());
                ImGui::EndPopup();
            }
        }
        ImGui::EndCombo();
    }
    const float btnW = ImGui::CalcTextSize("Save as Prefab").x + ImGui::GetStyle().FramePadding.x * 2;
    {
        const bool same = InspSameLineOrWrapIm(btnW);
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > btnW) {
            if (same) ImGui::SameLine(ImGui::GetCursorPosX() + (avail - btnW));
            else ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - btnW));
        }
    }
    if (ImGui::Button("Save as Prefab")) in.savePrefab = true;
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return in;
}

CompHeaderIntent DrawComponentHeaderBodyImGui(const CompHeaderModel& m, const PanelViewHooks&) {
    CompHeaderIntent in;
    ImGui::PushID(m.label.c_str());
    const float fh = ImGui::GetFrameHeight();
    const float startX = ImGui::GetCursorPosX();
    const float barW = ImGui::GetContentRegionAvail().x;
    const float arrowW = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float lead = fh + 4.0f + fh * 0.66f + 6.0f;
    const float spaceW = std::max(1.0f, ImGui::CalcTextSize(" ").x);
    std::string title((std::size_t)std::ceil(lead / spaceW), ' ');
    {
        const float badgeW = m.diag > 0 ? 22.0f : 0.0f;
        const float room = barW - arrowW - lead - fh - 10.0f - badgeW;
        title += CompHeaderTitle(m.label, room, [](const char* t) { return ImGui::CalcTextSize(t).x; });
    }
    title += "###hdr";
    if (m.sectionGap > 0.0f) ImGui::Dummy(ImVec2(0, m.sectionGap));
    ImGui::SetNextItemOpen(m.open, ImGuiCond_Always);
    if (!m.enabled) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.62f, 0.66f, 1.0f));
    const bool open = ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
    if (open != m.open) in.toggleOpen = true;
    if (!m.tip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.tip.c_str());
    if (!m.enabled) ImGui::PopStyleColor();
    if (!m.managed.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("Managed by Character Model: you normally don't edit this.\n%s", m.managed.c_str());
    const ImVec2 hmn = ImGui::GetItemRectMin(), hmx = ImGui::GetItemRectMax();
    {
        ImVec4 barCol = Unpack(m.color); barCol.w = 0.95f;
        if (m.prefabChanged) barCol = ImVec4(0.30f, 0.62f, 1.0f, 1.0f);
        ImGui::GetWindowDrawList()->AddRectFilled(hmn, ImVec2(hmn.x + 3.0f, hmx.y),
                                                  ImGui::GetColorU32(m.enabled ? barCol : ImVec4(0.45f, 0.47f, 0.51f, 0.8f)), 1.5f);
    }
    const bool hdrRightClick = ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    ImGui::SameLine(startX + arrowW);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
    {
        const float cb = ImGui::GetFrameHeight();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (fh - cb) * 0.5f);
        bool en = m.enabled;
        if (ImGui::Checkbox("##enabled", &en)) in.toggleEnabled = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(m.enabled ? "Enabled (click to turn this component off)" : "Disabled (click to turn it on)");
    }
    ImGui::PopStyleVar();
    ImGui::SameLine(0, 6.0f);
    {
        ImPen pen{ImGui::GetWindowDrawList()};
        InspDrawGlyph(pen, m.glyph, m.color, m.enabled, ImGui::GetCursorScreenPos().x, hmn.y, fh);
    }
    ImGui::SameLine(startX + barW - fh);
    {
        const ImVec2 bp(ImGui::GetCursorScreenPos().x, hmn.y);
        ImGui::SetCursorScreenPos(bp);
        if (ImGui::InvisibleButton("##compmenu", ImVec2(fh, fh))) ImGui::OpenPopup("##compctx");
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hov) dl->AddRectFilled(bp, ImVec2(bp.x + fh, bp.y + fh), IM_COL32(255, 255, 255, 24), 4.0f);
        for (int d = -1; d <= 1; ++d)
            dl->AddCircleFilled(ImVec2(bp.x + fh * 0.5f + d * 4.5f, bp.y + fh * 0.5f), 1.6f, ImGui::GetColorU32(hov ? ImGuiCol_Text : ImGuiCol_TextDisabled), 8);
        if (hov) ImGui::SetTooltip("Component options (or right-click the header)");
    }
    if (m.prefabChanged && ImGui::IsMouseHoveringRect(hmn, hmx) && ImGui::IsWindowHovered()) ImGui::SetTooltip("Changed from the prefab");
    if (hdrRightClick) ImGui::OpenPopup("##compctx");
    if (ImGui::BeginPopup("##compctx")) {
        std::vector<PanelMenuEntry> entries;
        if (m.buildMenu) m.buildMenu(entries);
        std::string chosen;
        HierMenuEntries(entries, chosen);
        if (!chosen.empty()) in.menuId = chosen;
        ImGui::EndPopup();
    }
    if (m.diag > 0) {
        ImPen pen{ImGui::GetWindowDrawList()};
        const ImVec2 bang = ImGui::CalcTextSize("!");
        HierDrawDiag(pen, m.diag, hmx.x - fh - 18.0f, (hmn.y + hmx.y) * 0.5f, bang.x, bang.y);
    }
    ImGui::PopID();
    if (open && !m.managed.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Managed by Character Model - you normally don't edit this.");
        ImGui::TextDisabled("%s", m.managed.c_str());
    }
    if (open && !m.mixedNote.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.40f, 1.0f));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(m.mixedNote.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("What you change here is set on every selected object.");
    }
    return in;
}

TransformIntent DrawTransformBodyImGui(TransformModel& m, const PanelViewHooks&) {
    TransformIntent in;
    ImGui::SetNextItemOpen(m.open, ImGuiCond_Always);
    const bool open = ImGui::CollapsingHeader("Transform");
    if (open != m.open) in.toggleOpen = true;
    if (ImGui::BeginPopupContextItem("##tfctx")) {
        if (ImGui::MenuItem("Copy Transform Values")) in.copy = true;
        if (ImGui::MenuItem("Paste Transform Values", nullptr, false, m.pasteValid)) in.paste = true;
        ImGui::EndPopup();
    }
    if (!open) return in;
    if (m.selectedCount > 1) ImGui::TextDisabled("(editing %d selected objects)", m.selectedCount);
    bool act = false, any = false;
    if (InspVec3Im("Position", m.pos, 0.05f, "%.3f", &act)) in.posChanged = true;
    any |= act;
    if (InspVec3Im("Rotation", m.rot, 1.0f, "%.2f", &act)) in.rotChanged = true;
    any |= act;
    const float before[3] = {m.scale[0], m.scale[1], m.scale[2]};
    if (m.isCamera) ImGui::BeginDisabled();
    if (InspVec3Im("Scale", m.scale, 0.05f, "%.3f", &act)) {
        if (m.uniform) TransformUniformScale(before, m.scale);
        in.scaleChanged = true;
    }
    any |= act;
    in.activated = any;
    if (m.isCamera) { ImGui::EndDisabled(); ImGui::TextDisabled("(camera scale is locked)"); }
    if (ImGui::SmallButton("Reset Pos")) in.resetPos = true;
    InspSameLineOrWrapIm(InspButtonWIm("Reset Rot"));
    if (ImGui::SmallButton("Reset Rot")) in.resetRot = true;
    if (!m.isCamera) InspSameLineOrWrapIm(InspButtonWIm("Reset Scale"));
    if (!m.isCamera && ImGui::SmallButton("Reset Scale")) in.resetScale = true;
    if (!m.isCamera) {
        InspSameLineOrWrapIm(ImGui::GetFrameHeight() + ImGui::CalcTextSize("Uniform").x + 8.0f);
        bool uni = m.uniform;
        if (ImGui::Checkbox("Uniform##scl", &uni)) in.toggleUniform = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Link the scale axes: dragging X/Y/Z scales all three by the same ratio.");
    }
    return in;
}

MeshRendererIntent DrawMeshRendererBodyImGui(MeshRendererModel& m, const PanelViewHooks&) {
    MeshRendererIntent in;
    if (!m.v) return in;
    MeshRendererValues& v = *m.v;
    bool& ch = in.changed;
    const std::string stats = MeshStatsText(m);
    if (!stats.empty()) ImGui::TextDisabled("%s", stats.c_str());
    if (ImGui::ColorEdit4("Color##mesh", v.color)) ch = true;
    if (ImGui::Checkbox("Wireframe", &v.wireframe)) ch = true;
    if (ImGui::Combo("Shader##mesh", &v.shader, kMeshShaderNames, 9)) { ch = true; in.shaderChanged = true; }
    if (v.shader == MeshShaderToon && ImGui::SliderInt("Cel Bands##mesh", &v.toonBands, 2, 6)) ch = true;
    if (v.shader == MeshShaderGradient) {
        if (ImGui::ColorEdit3("Gradient Top##mesh", v.gradientTop)) ch = true;
        if (ImGui::ColorEdit3("Gradient Bottom##mesh", v.gradientBottom)) ch = true;
    }
    if (v.shader == MeshShaderFresnel) ImGui::TextDisabled("%s", kMeshFresnelNote);
    if (ImGui::SliderFloat("Rim##mesh", &v.rimStrength, 0.0f, 2.0f)) ch = true;
    if (v.rimStrength > 0.0f) {
        if (ImGui::SliderFloat("Rim Power##mesh", &v.rimPower, 1.0f, 8.0f)) ch = true;
        if (ImGui::ColorEdit3("Rim Color##mesh", v.rimColor)) ch = true;
    }
    if (ImGui::Checkbox("Outline##mesh", &v.outline)) ch = true;
    if (v.outline) {
        if (ImGui::DragFloat("Outline Width##mesh", &v.outlineWidth, 0.002f, 0.0f, 1.0f)) ch = true;
        if (ImGui::ColorEdit3("Outline Color##mesh", v.outlineColor)) ch = true;
    }
    if (ImGui::Checkbox("Cast Shadows##mesh", &v.castShadows)) ch = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kMeshCastTip);
    ImGui::SameLine();
    if (ImGui::Checkbox("Receive Shadows##mesh", &v.receiveShadows)) ch = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kMeshReceiveTip);
    if (ImGui::Checkbox("Ground Shadow##mesh", &v.groundShadow)) ch = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kMeshGroundTip);
    if (v.groundShadow) {
        if (ImGui::DragFloat("Shadow Ground Y##mesh", &v.groundShadowY, 0.05f, -1000.0f, 1000.0f)) ch = true;
        if (ImGui::SliderFloat("Shadow Strength##mesh", &v.groundShadowStrength, 0.0f, 1.0f)) ch = true;
    }
    if (ImGui::ColorEdit3("Emissive##mesh", v.emissive)) ch = true;
    if (ImGui::SliderFloat("Specular##mesh", &v.specular, 0.0f, 1.0f)) ch = true;
    if (v.specular > 0.0f && ImGui::SliderFloat("Shininess##mesh", &v.shininess, 1.0f, 128.0f)) ch = true;
    if (ImGui::Combo("Primitive", &v.primitive, kMeshPrimitiveNames, kMeshPrimitiveCount)) { ch = true; in.primitiveChanged = true; }
    return in;
}

bool DrawAddComponentButtonImGui(const PanelViewHooks& hooks) {
    const ImVec4 a = Unpack(hooks.accent);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(a.x, a.y, a.z, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(a.x, a.y, a.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(a.x, a.y, a.z, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const float acW = ImGui::GetContentRegionAvail().x;
    const float acPad = acW > 300.0f ? (acW - 260.0f) * 0.5f : 0.0f;
    if (acPad > 0.0f) ImGui::Indent(acPad);
    const bool hit = ImGui::Button(kAddComponentLabel, ImVec2(acPad > 0.0f ? 260.0f : -1, 30));
    if (acPad > 0.0f) ImGui::Unindent(acPad);
    ImGui::PopStyleColor(4);
    return hit;
}


// ---- Inspector, stage 2: field rows ------------------------------------------------------------------------------------------------

namespace {
// The editor's SectionHeader (editor/main.cpp): Spacing, the accent tick, the heading face in its light colour, a rule.
void SectionHeaderImGui(const char* label, const PanelViewHooks& hooks) {
    ImGui::Spacing();
    if (hooks.headingFont) ImGui::PushFont(hooks.headingFont);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 1.0f), ImVec2(p.x + 3.0f, p.y + h), hooks.accent, 1.5f);
    ImGui::Indent(9.0f);
    ImGui::TextColored(ImVec4(0.90f, 0.92f, 0.97f, 1.0f), "%s", label);
    ImGui::Unindent(9.0f);
    if (hooks.headingFont) ImGui::PopFont();
    ImGui::Separator();
}
ImVec4 FromU32(std::uint32_t c) { return ImGui::ColorConvertU32ToFloat4(c | 0xff000000u); }
} // namespace

void DrawFieldListBodyImGui(InspFieldList& list, const PanelViewHooks& hooks) {
    int skip = 0;
    for (std::size_t k = 0; k < list.rows.size(); ++k) {
        InspRow& r = list.rows[k];
        if (skip > 0) {
            if (r.kind == InspRowTree) ++skip;
            else if (r.kind == InspRowTreeEnd) --skip;
            continue;
        }
        if (r.sameLine) {
            bool fits = true;
            if (r.sameLineWrap) {   // the editor's SameLineOrWrap
                const std::size_t vl = InspVisibleLen(r.label);
                const float tw = ImGui::CalcTextSize(r.label, r.label + vl).x;
                const ImGuiStyle& st = ImGui::GetStyle();
                float nextW = tw + st.FramePadding.x * 2.0f;
                if (r.kind == InspRowCheck) nextW = ImGui::GetFrameHeight() + (vl ? st.ItemInnerSpacing.x + tw : 0.0f);
                else if (r.kind == InspRowLabel || r.kind == InspRowNote) nextW = tw;
                else if (r.width > 0.0f) nextW = r.width + (vl ? st.ItemInnerSpacing.x + tw : 0.0f);
                const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                fits = right - (ImGui::GetItemRectMax().x + (r.sameLineGap >= 0.0f ? r.sameLineGap : st.ItemSpacing.x)) >= nextW;
            }
            if (fits) {
                if (r.sameLineAt >= 0.0f) ImGui::SameLine(ImGui::GetWindowContentRegionMin().x + r.sameLineAt);   // from the region's left
                else ImGui::SameLine(0.0f, r.sameLineGap);
            }
        }
        if (r.disabled) ImGui::BeginDisabled();
        const bool pushId = r.kind != InspRowTree && r.kind != InspRowTreeEnd;   // TreeNode and TreePop keep their own id
        if (pushId) ImGui::PushID((int)k);
        if (r.width > 0.0f) ImGui::SetNextItemWidth(r.width);
        else if (r.width < 0.0f) ImGui::SetNextItemWidth(r.width);
        bool hoverTip = true;
        switch (r.kind) {
        case InspRowDrag: {
            const int n = r.n < 1 ? 1 : r.n > 4 ? 4 : r.n;
            float* v = r.F();
            const char* fmt = r.fmt ? r.fmt : "%.3f";
            if (r.axisBars || r.axisTags || r.mixedAxes) {   // the editor's DragVec3Axis, or the generic card's FloatRow
                static const char* const kTags[4] = {"X", "Y", "Z", "W"};
                const ImGuiStyle& st = ImGui::GetStyle();
                ImGui::PushID(r.label);
                ImGui::PushMultiItemsWidths(n, ImGui::CalcItemWidth());
                for (int i = 0; i < n; ++i) {
                    ImGui::PushID(i);
                    if (i > 0) ImGui::SameLine(0, st.ItemInnerSpacing.x);
                    const bool mx = (r.mixedAxes >> i) & 1u;
                    char tagged[24];
                    const char* f = fmt;
                    if (r.axisTags) { std::snprintf(tagged, sizeof tagged, mx ? "%s -" : "%s %s", kTags[i], fmt); f = tagged; }
                    else if (mx) f = "-";
                    else if (r.axisBars)
                        f = InspAxisFormat(fmt, v[i], ImGui::CalcItemWidth() - st.FramePadding.x * 2.0f - 4.0f, [](const char* t) { return ImGui::CalcTextSize(t).x; });
                    if (ImGui::DragFloat("##c", &v[i], r.speed, r.lo, r.hi, f)) { r.changed = true; r.axes |= 1u << i; }
                    if (ImGui::IsItemActivated()) r.activated = true;
                    if (r.axisBars && i < 3) {
                        const ImVec2 mn = ImGui::GetItemRectMin(), mx2 = ImGui::GetItemRectMax();
                        ImGui::GetWindowDrawList()->AddRectFilled(mn, ImVec2(mn.x + 3.0f, mx2.y), kInspAxisCol[i], 2.0f);
                    }
                    ImGui::PopID();
                    ImGui::PopItemWidth();
                }
                const std::size_t vl = InspVisibleLen(r.label);
                if (vl) { ImGui::SameLine(0, st.ItemInnerSpacing.x); ImGui::TextUnformatted(r.label, r.label + vl); }
                ImGui::PopID();
            } else {
                bool ch = false;
                const char* f = r.mixed ? "-" : fmt;
                if (n == 1) ch = ImGui::DragFloat(r.label, v, r.speed, r.lo, r.hi, f, r.logarithmic ? ImGuiSliderFlags_Logarithmic : 0);
                else ch = ImGui::DragScalarN(r.label, ImGuiDataType_Float, v, n, r.speed, &r.lo, &r.hi, f);
                if (ch) r.changed = true;
                if (ImGui::IsItemActivated()) r.activated = true;
            }
            break;
        }
        case InspRowSlider:
            if (ImGui::SliderFloat(r.label, r.F(), r.lo, r.hi, r.fmt ? r.fmt : "%.3f", r.logarithmic ? ImGuiSliderFlags_Logarithmic : 0)) r.changed = true;
            if (ImGui::IsItemActivated()) r.activated = true;
            break;
        case InspRowDragInt:
            if (ImGui::DragInt(r.label, r.I(), r.speed, (int)r.lo, (int)r.hi, r.mixed ? "-" : (r.fmt ? r.fmt : "%d"))) r.changed = true;
            if (ImGui::IsItemActivated()) r.activated = true;
            break;
        case InspRowSliderInt:
            if (ImGui::SliderInt(r.label, r.I(), (int)r.lo, (int)r.hi, r.fmt ? r.fmt : "%d")) r.changed = true;
            if (ImGui::IsItemActivated()) r.activated = true;
            break;
        case InspRowCheck:
            if (r.mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            if (ImGui::Checkbox(r.label, r.B())) r.changed = true;
            if (r.mixed) ImGui::PopItemFlag();
            break;
        case InspRowCombo: {
            int* cur = r.I();
            const char* preview = r.mixed ? "Mixed" : r.preview ? r.preview : (*cur >= 0 && *cur < r.count && r.items) ? r.items[*cur] : "";
            const std::string fitted = InspFitText(preview, ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().FramePadding.x,
                                                   [](const char* t) { return ImGui::CalcTextSize(t).x; });
            if (r.color) ImGui::PushStyleColor(ImGuiCol_Text, FromU32(r.color));
            const bool comboOpen = ImGui::BeginCombo(r.label, fitted.c_str());
            if (r.color) ImGui::PopStyleColor();
            if (comboOpen && r.search) {   // the item list's search field (stage 7), as the editor's ItemPicker
                static char find[64] = "";
                if (ImGui::IsWindowAppearing()) { find[0] = 0; ImGui::SetKeyboardFocusHere(); }
                ImGui::InputTextWithHint("##itemfind", r.searchHint ? r.searchHint : "Search", find, sizeof find);
                std::string needle(find);
                for (char& ch : needle) ch = (char)std::tolower((unsigned char)ch);
                bool exact = false;
                for (int q = 0; q < r.count; ++q) {
                    const char* key = r.searchKeys ? r.searchKeys[q] : "";
                    if (find[0] && std::strcmp(key, find) == 0) exact = true;
                    if (!needle.empty()) {
                        std::string hay = std::string(key) + " " + r.items[q];
                        for (char& ch : hay) ch = (char)std::tolower((unsigned char)ch);
                        if (hay.find(needle) == std::string::npos) continue;
                    }
                    ImGui::PushID(q);
                    if (ImGui::Selectable(r.items[q], !r.mixed && q == *cur) && (q != *cur || r.mixed)) { *cur = q; r.changed = true; }
                    ImGui::PopID();
                }
                if (find[0] && !exact && ImGui::Selectable((std::string("Use \"") + find + "\"").c_str())) { r.sv = find; *cur = -1; r.changed = true; }
                ImGui::EndCombo();
            } else if (comboOpen) {
                for (int q = 0; q < r.count; ++q) {
                    if (r.heads && r.heads[q]) SectionHeaderImGui(r.heads[q], hooks);
                    ImGui::PushID(q);
                    if (ImGui::Selectable(r.items[q], !r.mixed && q == *cur) && (q != *cur || r.mixed)) { *cur = q; r.changed = true; }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case InspRowColor: {
            float* c = r.F();
            const float before[4] = {c[0], c[1], c[2], r.n == 4 ? c[3] : 1.0f};
            const int fl = r.noInputs ? ImGuiColorEditFlags_NoInputs : 0;
            const bool ch = r.n == 4 ? ImGui::ColorEdit4(r.label, c, fl) : ImGui::ColorEdit3(r.label, c, fl);
            if (ch) { r.changed = true; for (int q = 0; q < (r.n == 4 ? 4 : 3); ++q) if (c[q] != before[q]) r.axes |= 1u << q; }
            break;
        }
        case InspRowText: {
            char buf[512];
            std::snprintf(buf, sizeof buf, "%s", r.mixed ? "" : r.S()->c_str());
            bool ed;
            if (r.wrap) ed = ImGui::InputTextMultiline(r.label, buf, sizeof buf, ImVec2(r.width < 0.0f ? r.width : ImGui::CalcItemWidth(),
                                                                                      ImGui::GetTextLineHeight() * (r.texH > 0.0f ? r.texH : 4.0f)));
            else ed = (r.hint || r.mixed) ? ImGui::InputTextWithHint(r.label, r.mixed ? "Mixed" : r.hint, buf, sizeof buf)
                                          : ImGui::InputText(r.label, buf, sizeof buf);
            if (ed) { *r.S() = buf; r.changed = true; }
            if (ImGui::IsItemActivated()) r.activated = true;
            if (r.pick != InspPickNone) {   // the editor's AssetPathPicker arrow
                const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                const float x1 = std::min(mx.x, mn.x + ImGui::CalcItemWidth());   // the field's right end, not the label's
                const bool over = ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(ImVec2(x1 - 22.0f, mn.y), ImVec2(x1, mx.y), false);
                const float cx = x1 - 12.0f, cy = (mn.y + mx.y) * 0.5f;
                ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(cx - 4.0f, cy - 2.0f), ImVec2(cx + 4.0f, cy - 2.0f), ImVec2(cx, cy + 3.0f),
                                                              ImGui::GetColorU32(over ? ImGuiCol_Text : ImGuiCol_TextDisabled));
            }
            break;
        }
        case InspRowSection: SectionHeaderImGui(r.Text(), hooks); break;
        case InspRowGroup: ImGui::SeparatorText(r.Text()); break;
        case InspRowNote:
            if (r.alignFrame && !r.wrap) ImGui::AlignTextToFramePadding();
            if (r.wrap) ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", r.Text());
            if (r.wrap) ImGui::PopTextWrapPos();
            break;
        case InspRowColorText:
        case InspRowLabel:
            if (r.alignFrame) ImGui::AlignTextToFramePadding();
            if (r.kind == InspRowColorText || r.color) {
                if (r.wrap) ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(FromU32(r.color), "%s", r.Text());
                if (r.wrap) ImGui::PopTextWrapPos();
            }
            else if (r.wrap) ImGui::TextWrapped("%s", r.Text());
            else ImGui::TextUnformatted(r.Text());
            break;
        case InspRowSmallButton:
            if (r.color) ImGui::PushStyleColor(ImGuiCol_Text, FromU32(r.color));
            if (ImGui::SmallButton(r.label)) r.clicked = true;
            if (r.color) ImGui::PopStyleColor();
            if (r.dragType) {   // a drag handle (stage 7)
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload(r.dragType, &r.tag, sizeof(int));
                    ImGui::TextUnformatted(r.hint ? r.hint : "");
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(r.dragType))
                        if (p->DataSize == (int)sizeof(int)) r.dropped = *(const int*)p->Data;
                    ImGui::EndDragDropTarget();
                }
            }
            break;
        case InspRowProgress: {
            const char* t = r.Text();
            ImGui::ProgressBar(r.fv[0], ImVec2(-1, 0), t);
            break;
        }
        case InspRowButton: {
            if (r.on) {
                const ImVec4 a = Unpack(hooks.accent);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(a.x, a.y, a.z, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(a.x, a.y, a.z, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(a.x, a.y, a.z, 0.80f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            }
            if (ImGui::Button(r.label, ImVec2(r.width, 0.0f))) r.clicked = true;
            if (r.on) ImGui::PopStyleColor(4);
            break;
        }
        case InspRowSwatch:
            if (ImGui::ColorButton(r.label, ImVec4(r.fv[0], r.fv[1], r.fv[2], r.fv[3]), 0, ImVec2(r.texW, r.texH))) r.clicked = true;
            break;
        case InspRowAsset: {   // the editor's AssetField
            std::string* v = r.S();
            const float fh = ImGui::GetFrameHeight();
            const bool hasValue = !r.mixed && !v->empty();
            float w = ImGui::CalcItemWidth();
            if (hasValue) w -= fh + 2.0f;
            std::string shown = r.mixed ? "Mixed" : v->empty() ? std::string("None") : *v;
            if (hasValue && r.s && !r.sv.empty()) shown = r.sv;   // the host's name for the value (an object's name)
            else if (hasValue) { const std::size_t sl = shown.find_last_of("/\\"); if (sl != std::string::npos) shown = shown.substr(sl + 1); }
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
            if (!hasValue) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::Button((shown + "###asset").c_str(), ImVec2(std::max(40.0f, w), fh));
            if (!hasValue) ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            hoverTip = false;
            if (hasValue) {
                ImGui::SameLine(0, 2.0f);
                if (ImGui::Button("##clear", ImVec2(fh, fh))) { v->clear(); r.changed = true; }
                const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
                const float rr = fh * 0.17f;
                const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
                ImGui::GetWindowDrawList()->AddLine(ImVec2(c.x - rr, c.y - rr), ImVec2(c.x + rr, c.y + rr), col, 1.5f);
                ImGui::GetWindowDrawList()->AddLine(ImVec2(c.x - rr, c.y + rr), ImVec2(c.x + rr, c.y - rr), col, 1.5f);
            }
            const std::size_t vl = InspVisibleLen(r.label);
            if (vl) { ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x); ImGui::TextUnformatted(r.label, r.label + vl); }
            break;
        }
        case InspRowImage:
            if (r.texture) ImGui::Image((ImTextureID)r.texture, ImVec2(r.texW, r.texH));
            else ImGui::Dummy(ImVec2(r.texW, r.texH));
            hoverTip = false;
            break;
        case InspRowMask: {
            int* bits = r.I();
            const int nbits = r.count > 0 && r.count < 32 ? r.count : 32;
            const bool presets = r.presetCount > 0;
            std::vector<std::string> names, vals; std::vector<void*> thumbs;
            if (r.items) { for (int i = 0; i < nbits; ++i) names.emplace_back(r.items[i] ? r.items[i] : ""); }
            else if (list.hooks.choices) list.hooks.choices(r, vals, names, thumbs);
            names.resize(32);
            const std::string preview = r.preview ? std::string(r.preview) : InspMaskPreview(*bits, [&](int i) { return names[(std::size_t)i]; });
            if (ImGui::BeginCombo(r.label, preview.c_str())) {
                if (presets) {   // the editor's AreaMaskCombo: the preset small buttons on one line
                    for (int q = 0; q < r.presetCount; ++q) {
                        if (q) ImGui::SameLine();
                        ImGui::PushID(q);
                        if (ImGui::SmallButton(r.presetNames[q])) { *bits = r.presetBits[q]; r.changed = true; }
                        ImGui::PopID();
                    }
                } else {
                    if (ImGui::Selectable("Everything", *bits == ~0)) { *bits = ~0; r.changed = true; }
                    if (ImGui::Selectable("Nothing", *bits == 0)) { *bits = 0; r.changed = true; }
                    ImGui::Separator();
                }
                const bool rename = !presets && list.hooks.renameLayer && list.hooks.layerName;
                if (rename) ImGui::TextDisabled("Right-click a layer to rename it");
                for (int i = 0; i < nbits; ++i) {
                    bool on = (*bits & (1 << i)) != 0;
                    ImGui::PushID(i);
                    if (ImGui::Checkbox(names[(std::size_t)i].c_str(), &on)) { if (on) *bits |= (1 << i); else *bits &= ~(1 << i); r.changed = true; }
                    if (rename && ImGui::BeginPopupContextItem("##layerctx")) {   // the editor's LayerRenameMenu
                        static char buf[128] = "";
                        static std::string err, hint, def;
                        if (ImGui::IsWindowAppearing()) {
                            std::string text;
                            list.hooks.layerName(i, text, hint, def);
                            std::snprintf(buf, sizeof buf, "%s", text.c_str());
                            err.clear();
                        }
                        ImGui::TextDisabled("Layer %d name", i);
                        ImGui::SetNextItemWidth(170);
                        if (ImGui::InputTextWithHint("##layername", hint.c_str(), buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue))
                            if (list.hooks.renameLayer(i, buf, err)) ImGui::CloseCurrentPopup();
                        if (ImGui::SmallButton("Reset to default"))
                            if (list.hooks.renameLayer(i, def, err)) ImGui::CloseCurrentPopup();
                        if (!err.empty()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", err.c_str());
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case InspRowTree: {
            if (r.on) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            const bool open = ImGui::TreeNode(r.label);
            *r.B() = open;
            if (!open) skip = 1;
            break;
        }
        case InspRowTreeEnd: ImGui::TreePop(); break;
        case InspRowSpacing: ImGui::Spacing(); break;
        case InspRowSeparator: ImGui::Separator(); hoverTip = false; break;
        case InspRowBullet: ImGui::BulletText("%s", r.Text()); break;
        case InspRowGradient: {   // the editor's ParticleGradientEditor (sel kept by the host instead of ImGui's storage)
            float* st = r.F();
            int& cnt = *r.I();
            const int maxStops = r.hi > 0.0f ? (int)r.hi : 1;
            int sel = r.sel ? *r.sel : 0;
            if (sel >= cnt) sel = cnt - 1;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float w = std::max(60.0f, ImGui::GetContentRegionAvail().x), barH = 18.0f, mkH = 9.0f;
            const ImVec2 p0 = ImGui::GetCursorScreenPos(), p1(p0.x + w, p0.y + barH);
            for (int cx = 0; cx * 6.0f < w; ++cx)
                for (int cy = 0; cy < 3; ++cy)
                    dl->AddRectFilled(ImVec2(p0.x + cx * 6.0f, p0.y + cy * 6.0f), ImVec2(std::min(p1.x, p0.x + cx * 6.0f + 6.0f), p0.y + cy * 6.0f + 6.0f),
                                      ((cx + cy) & 1) ? IM_COL32(150, 150, 150, 255) : IM_COL32(95, 95, 95, 255));
            auto col32 = [](const float c[4]) { auto b = [](float v) { return (int)(std::min(1.0f, std::max(0.0f, v)) * 255.0f); };
                                                 return IM_COL32(b(c[0]), b(c[1]), b(c[2]), b(c[3])); };
            for (int i = 0; i < 32; ++i) {
                const float ta = (float)i / 32, tb = (float)(i + 1) / 32;
                float ca[4], cb[4];
                InspGradientAt(st, cnt, ta, ca); InspGradientAt(st, cnt, tb, cb);
                dl->AddRectFilledMultiColor(ImVec2(p0.x + ta * w, p0.y), ImVec2(p0.x + tb * w, p1.y), col32(ca), col32(cb), col32(cb), col32(ca));
            }
            dl->AddRect(p0, p1, IM_COL32(20, 20, 24, 255));
            for (int i = 0; i < cnt; ++i) {
                const float x = p0.x + std::min(1.0f, std::max(0.0f, st[i * 5])) * w;
                const ImVec2 a(x, p1.y), b(x - 5.0f, p1.y + mkH), c(x + 5.0f, p1.y + mkH);
                const float rgb[4] = {st[i * 5 + 1], st[i * 5 + 2], st[i * 5 + 3], 1.0f};
                dl->AddTriangleFilled(a, b, c, col32(rgb));
                dl->AddTriangle(a, b, c, i == sel ? (hooks.accent | 0xff000000u) : IM_COL32(220, 220, 225, 255), i == sel ? 2.0f : 1.0f);
            }
            ImGui::InvisibleButton("##gradbar", ImVec2(w, barH + mkH + 2.0f), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const float mx = ImGui::GetIO().MousePos.x, mt = std::min(1.0f, std::max(0.0f, (mx - p0.x) / w));
            static bool s_drag = false;
            if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                r.activated = true;
                int hit = InspNearestStop(st, cnt, p0.x, w, mx);
                if (hit < 0) { float c[4]; InspGradientAt(st, cnt, mt, c); hit = InspAddStop(st, cnt, maxStops, mt, c); if (hit >= 0) r.changed = true; }
                if (hit >= 0) sel = hit;
                s_drag = hit >= 0;
            }
            if (ImGui::IsItemActive() && s_drag && sel >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f) && st[sel * 5] != mt) { st[sel * 5] = mt; r.changed = true; }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_drag = false;
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                const int hit = InspNearestStop(st, cnt, p0.x, w, mx);
                if (hit >= 0 && cnt > 1) { r.activated = true; InspRemoveAt(st, cnt, 5, hit); r.changed = true; if (sel >= cnt) sel = cnt - 1; }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click the bar to add a stop, drag a marker to move it, right-click a marker to delete it.");
            hoverTip = false;
            if (r.sel) *r.sel = sel;
            break;
        }
        case InspRowCurve: {   // the editor's ParticleCurveEditor
            float* ks = r.F();
            int& cnt = *r.I();
            const int maxKeys = r.hi > 0.0f ? (int)r.hi : 1;
            int sel = r.sel ? *r.sel : 0;
            if (sel >= cnt) sel = cnt - 1;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float w = std::max(60.0f, ImGui::GetContentRegionAvail().x), h = 64.0f, lh = ImGui::GetTextLineHeight();
            const ImVec2 c0 = ImGui::GetCursorScreenPos(), p0(c0.x, c0.y + lh), p1(p0.x + w, p0.y + h);   // the scale label on a line above
            static bool s_drag = false;
            static float s_top = 1.0f;
            const bool dragging = s_drag && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            const float top = dragging ? s_top : InspCurveTop(ks, cnt);
            s_top = top;
            auto at = [&](float t, float v) { return ImVec2(p0.x + t * w, p1.y - (v / top) * h); };
            const ImU32 lineCol = hooks.accent | 0xff000000u;
            dl->AddRectFilled(p0, p1, IM_COL32(28, 30, 36, 255), 3.0f);
            const float y1 = at(0.0f, 1.0f).y;
            dl->AddLine(ImVec2(p0.x, y1), ImVec2(p1.x, y1), IM_COL32(70, 74, 86, 255));
            for (int gx = 1; gx < 4; ++gx) dl->AddLine(ImVec2(p0.x + w * gx * 0.25f, p0.y), ImVec2(p0.x + w * gx * 0.25f, p1.y), IM_COL32(48, 50, 60, 255));
            const bool smooth = *r.B();
            ImVec2 prev = at(0.0f, InspCurveAt(ks, cnt, smooth, 0.0f));
            for (int i = 1; i <= 48; ++i) {
                const float t = i / 48.0f;
                const ImVec2 cur = at(t, InspCurveAt(ks, cnt, smooth, t));
                dl->AddLine(prev, cur, lineCol, 2.0f);
                prev = cur;
            }
            for (int i = 0; i < cnt; ++i) {
                const ImVec2 kp = at(ks[i * 2], ks[i * 2 + 1]);
                dl->AddCircleFilled(kp, i == sel ? 5.0f : 4.0f, i == sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 205, 215, 255));
                if (i == sel) dl->AddCircle(kp, 6.5f, lineCol, 12, 1.5f);
            }
            dl->AddRect(p0, p1, IM_COL32(20, 20, 24, 255), 3.0f);
            char ylab[16]; std::snprintf(ylab, sizeof(ylab), "x%.2g", top);
            dl->AddText(ImVec2(p1.x - ImGui::CalcTextSize(ylab).x - 3.0f, c0.y), IM_COL32(140, 145, 160, 255), ylab);
            ImGui::InvisibleButton("##curveplot", ImVec2(w, lh + h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const ImVec2 m = ImGui::GetIO().MousePos;
            const float mt = std::min(1.0f, std::max(0.0f, (m.x - p0.x) / w)), mv = std::max(0.0f, (p1.y - m.y) / h * top);
            if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m.y >= p0.y) {
                r.activated = true;
                int hit = InspNearestKey(ks, cnt, p0.x, p1.y, w, h, top, m.x, m.y);
                if (hit < 0) { hit = InspAddKey(ks, cnt, maxKeys, mt, mv); if (hit >= 0) r.changed = true; }
                if (hit >= 0) sel = hit;
                s_drag = hit >= 0;
            }
            if (ImGui::IsItemActive() && s_drag && sel >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
                ks[sel * 2] = mt; ks[sel * 2 + 1] = std::min(mv, top); r.changed = true;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_drag = false;
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                const int hit = InspNearestKey(ks, cnt, p0.x, p1.y, w, h, top, m.x, m.y);
                if (hit >= 0 && cnt > 1) { r.activated = true; InspRemoveAt(ks, cnt, 2, hit); r.changed = true; if (sel >= cnt) sel = cnt - 1; }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to add a key, drag a key to move it, right-click a key to delete it.");
            hoverTip = false;
            if (r.sel) *r.sel = sel;
            break;
        }
        case InspRowAnchor: {   // the editor's AnchorCombo
            int* a = r.I();
            const int ai = (*a < 0 || *a > 11) ? 0 : *a;
            auto diagram = [](ImDrawList* dl, ImVec2 q0, ImVec2 q1, int which, bool active) {
                dl->AddRect(q0, q1, active ? IM_COL32(150, 190, 255, 255) : IM_COL32(150, 154, 166, 255), 2.0f, 0, 1.2f);
                const InspAnchorPicture f = InspAnchorFill(which, q0.x, q0.y, q1.x, q1.y);
                dl->AddRectFilled(ImVec2(f.fx0, f.fy0), ImVec2(f.fx1, f.fy1), active ? IM_COL32(110, 160, 255, 255) : IM_COL32(205, 208, 216, 255), 1.5f);
            };
            ImGui::AlignTextToFramePadding();
            const float sz = ImGui::GetFrameHeight();
            const ImVec2 bp = ImGui::GetCursorScreenPos();
            if (ImGui::Button((std::string(kInspAnchorNames[ai]) + "##anchorbtn").c_str(), ImVec2(ImGui::CalcItemWidth(), 0))) ImGui::OpenPopup("##anchorpop");
            diagram(ImGui::GetWindowDrawList(), ImVec2(bp.x + 4, bp.y + 3), ImVec2(bp.x + sz - 2, bp.y + sz - 3), ai, false);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(ai >= 9 ? "Stretch: the widget fills its parent / the canvas on the stretched axis;\nPosition and Size become margins. Click to change."
                                          : "Which point of its parent / the screen this widget sticks to when the\nwindow resizes. Click to change.");
            hoverTip = false;
            const std::size_t vl = InspVisibleLen(r.label);
            if (vl) { ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x); ImGui::TextUnformatted(r.label, r.label + vl); }
            if (ImGui::BeginPopup("##anchorpop")) {
                ImGui::TextDisabled("ANCHOR");
                ImGui::SameLine(); ImGui::TextDisabled("(Shift+click: also move it there)");
                const float cell = 40.0f;
                auto cellButton = [&](int which, ImVec2 size) {
                    ImGui::PushID(which);
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const bool clicked = ImGui::InvisibleButton("##a", size);
                    const bool hov = ImGui::IsItemHovered();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), hov ? IM_COL32(70, 76, 92, 255) : IM_COL32(44, 46, 54, 255), 4.0f);
                    diagram(dl, ImVec2(p.x + 6, p.y + 6), ImVec2(p.x + size.x - 6, p.y + size.y - 6), which, which == ai);
                    if (hov) ImGui::SetTooltip("%s", kInspAnchorNames[which]);
                    ImGui::PopID();
                    if (clicked) { *a = which; r.changed = true; r.activated = true; if (ImGui::GetIO().KeyShift) r.axes = 1u; ImGui::CloseCurrentPopup(); }
                };
                for (int row = 0; row < 3; ++row) {
                    for (int col = 0; col < 3; ++col) { if (col) ImGui::SameLine(0, 4); cellButton(row * 3 + col, ImVec2(cell, cell)); }
                    ImGui::SameLine(0, 10);
                    if (row == 1) cellButton(9, ImVec2(cell * 1.4f, cell)); else ImGui::Dummy(ImVec2(cell * 1.4f, cell));
                }
                cellButton(10, ImVec2(cell, cell * 1.4f)); ImGui::SameLine(0, 4);
                cellButton(11, ImVec2(cell * 1.4f, cell * 1.4f));
                ImGui::SameLine(); ImGui::TextDisabled("Stretch vertical / Fill");
                ImGui::EndPopup();
            }
            break;
        }
        case InspRowSlot: {   // the editor's AssetSlot
            std::string* v = r.S();
            std::string name = *v;
            if (auto sl = name.find_last_of("/\\"); sl != std::string::npos) name = name.substr(sl + 1);
            const std::string lbl = (v->empty() ? (std::string("None (") + (r.hint ? r.hint : "") + ")") : name) + "##slotbtn";
            ImGui::Button(lbl.c_str(), ImVec2(-28, 0));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag a %s from the Project panel here", r.hint ? r.hint : "file");
            hoverTip = false;
            ImGui::SameLine();
            if (ImGui::SmallButton("x") && !v->empty()) { v->clear(); r.changed = true; r.activated = true; }
            break;
        }
        case InspRowInputInt:
            if (ImGui::InputInt(r.label, r.I())) r.changed = true;
            if (ImGui::IsItemActivated()) r.activated = true;
            break;
        case InspRowRadio:
            if (ImGui::RadioButton(r.label, r.I(), r.iv)) r.changed = true;
            break;
        case InspRowRange: {
            float* v = r.F();
            if (ImGui::DragFloatRange2(r.label, &v[0], &v[1], r.speed, r.lo, r.hi, r.fmt ? r.fmt : "%.3f", r.hint)) r.changed = true;
            if (ImGui::IsItemActivated()) r.activated = true;
            break;
        }
        }
        if (hoverTip && (r.tip || r.tipLookup) && ImGui::IsItemHovered()) {
            if (r.tip && *r.tip) ImGui::SetTooltip("%s", r.tip);
            else if (r.tipLookup && list.hooks.tipFor) {
                const std::string t = list.hooks.tipFor(r.label);
                if (!t.empty()) ImGui::SetTooltip("%s", t.c_str());
            }
        }
        if (r.overridden) {   // the editor's PrefabFieldMark bar
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            const float x = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x - 5.0f;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, mn.y), ImVec2(x + 3.0f, mx.y), kInspPrefabMark, 1.5f);
        }
        if (pushId) ImGui::PopID();
        if (r.disabled) ImGui::EndDisabled();
    }
}

} // namespace oked
