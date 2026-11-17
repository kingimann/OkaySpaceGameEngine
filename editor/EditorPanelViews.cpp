// The Dear ImGui bodies of the Problems, Stats, History, Tags and Layers, Physics and Profiler panels (see EditorPanelViews.hpp).
// The drawing is what the editor's DrawProblems, DrawStats, DrawHistory, DrawTagsAndLayersSettings, the Physics section of
// DrawProjectSettings and DrawProfiler drew before they were split from the editor's state; the right-click menus of the first
// three are new and the OkayUI ports have the same ones.
#include "EditorPanelViews.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
        case PanelMenuHeading: ImGui::TextDisabled("%s", e.label.c_str()); break;
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
    if (m.rowCount == 0 && !m.searching) {   // editor/main.cpp EmptyState, in short
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.34f);
        ImGui::SetWindowFontScale(1.25f);
        ImGui::SetCursorPosX(listX + std::max(0.0f, (avail.x - ImGui::CalcTextSize(kHierEmptyTitle).x) * 0.5f));
        ImGui::TextUnformatted(kHierEmptyTitle);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SetCursorPosX(listX + std::max(0.0f, (avail.x - ImGui::CalcTextSize(kHierEmptyHint).x) * 0.5f));
        ImGui::TextDisabled("%s", kHierEmptyHint);
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
