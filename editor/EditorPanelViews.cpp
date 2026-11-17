// The Dear ImGui bodies of the Problems, Stats, History, Tags and Layers, Physics and Profiler panels (see EditorPanelViews.hpp).
// The drawing is what the editor's DrawProblems, DrawStats, DrawHistory, DrawTagsAndLayersSettings, the Physics section of
// DrawProjectSettings and DrawProfiler drew before they were split from the editor's state; the right-click menus of the first
// three are new and the OkayUI ports have the same ones.
#include "EditorPanelViews.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
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
