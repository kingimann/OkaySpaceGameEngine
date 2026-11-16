// The editor's Problems, Stats and History panels drawn with OkayUI. Written from what the editor's own Dear ImGui panels show and
// do (editor/EditorPanelViews.cpp draws the same models); no code from any other project.
#include "OkayUIEditorPanels.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace UI = OkayUI;
using namespace okaypanels;

namespace okayuiscreens {

namespace {

unsigned char B(float v) { return (unsigned char)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); }
UI::Color32 C32(const float c[3], float a = 1.0f) { return UI::RGBA(B(c[0]), B(c[1]), B(c[2]), B(a)); }

// Dear ImGui's spacing in the editor (12 across, 8 down) and its text rows (a text line is one line tall, not a frame tall), for
// as long as the panel draws.
struct EditorRows {
    EditorRows() {
        UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 8.0f});
        UI::PushStyleVar(UI::StyleVar_TextRowHeight, UI::GetFontSize());
    }
    ~EditorRows() { UI::PopStyleVar(2); }
    EditorRows(const EditorRows&) = delete;
    EditorRows& operator=(const EditorRows&) = delete;
};

// Dear ImGui's divider: a one-pixel line at the cursor, then the item spacing (OkayUI's Separator leaves a gap above and below).
void Divider() {
    const UI::Vec2 p = UI::GetCursorScreenPos();
    const float w = UI::GetContentRegionAvail().x;
    UI::Dummy(w, 1.0f);
    UI::GetWindowDrawList().AddLine(UI::Vec2{p.x, p.y + 0.5f}, UI::Vec2{p.x + w, p.y + 0.5f}, UI::GetColorU32(UI::Col_Separator), 1.0f);
}

// Dear ImGui's SmallButton: one text line tall, the label with 12 pixels each side.
bool LineButton(const char* label) {
    return UI::Button(label, UI::Vec2{UI::CalcTextWidth(label) + 24.0f, UI::GetFontSize()});
}

} // namespace

// ---- Problems ---------------------------------------------------------------------------------------------------------------

ProblemsResult DrawProblems(const ProblemsModel& m, ProblemsState& st, float x, float y, float w, float h) {
    ProblemsResult res;
    st.filter[sizeof(st.filter) - 1] = '\0';
    UI::BareNextWindow();
    if (UI::Begin("Problems##okayui", x, y, w, h)) {
        EditorRows rows_;
        // ---- the search field, then the three severity pills at the right ----
        const float fs = UI::GetFontSize();
        auto pillW = [&](int count) {
            char b[16]; std::snprintf(b, sizeof(b), "%d", count);
            return UI::CalcTextWidth(b) + fs * 0.9f + 24.0f;
        };
        const float gap = 6.0f;
        const float pills = pillW(m.errors) + pillW(m.warnings) + pillW(m.infos) + gap * 2.0f;
        UI::SetNextItemWidth(std::max(UI::GetContentRegionAvail().x - pills - 12.0f, fs * 3.0f));
        UI::InputTextWithHint("##pfilter", "Search problems", st.filter, (int)sizeof(st.filter));
        res.searchField = UI::GetItemRect();
        auto pill = [&](const char* id, bool& on, int sev, int count, const char* tip, UI::Rect& out) {
            float c[3]; ProblemSeverityColor(sev, c);
            char buf[32]; std::snprintf(buf, sizeof(buf), "%d%s", count, id);
            const unsigned char* acc = UI::Style().accent;   // a pill that is on is filled with the accent, one that is off looks like a field
            UI::PushStyleColor(UI::Col_Button, on ? UI::RGBA(acc[0], acc[1], acc[2]) : UI::GetColorU32(UI::Col_FrameBg));
            UI::PushStyleColor(UI::Col_Text, on ? C32(c) : UI::GetColorU32(UI::Col_TextDisabled));
            UI::PushStyleVar(UI::StyleVar_ButtonTextAlign, UI::Vec2{1.0f, 0.5f});   // the count at the right, the dot at the left
            if (UI::Button(buf, UI::Vec2{pillW(count), 0.0f})) on = !on;
            UI::PopStyleVar();
            UI::PopStyleColor(2);
            out = UI::GetItemRect();
            const float dotR = fs * 0.2f;
            UI::GetWindowDrawList().AddCircleFilled(UI::Vec2{out.x + 12.0f + dotR, out.y + out.h * 0.5f}, dotR, C32(c, on ? 1.0f : 0.35f));
            if (UI::IsItemHovered()) UI::SetTooltip("%s", tip);
        };
        UI::SameLine();
        pill("##pe", st.showErr, ProblemError, m.errors, "Show errors", res.errorPill);
        UI::SameLine(gap);
        pill("##pw", st.showWarn, ProblemWarning, m.warnings, "Show warnings", res.warningPill);
        UI::SameLine(gap);
        pill("##pi", st.showInfo, ProblemInfo, m.infos, "Show notes", res.infoPill);
        Divider();

        // ---- the groups, in a scrolling list ----
        UI::BeginChild("##problist", UI::Vec2{0.0f, 0.0f}, UI::ChildFlags_None);
        res.list = UI::Rect{UI::GetCursorScreenPos().x, UI::GetCursorScreenPos().y, UI::GetContentRegionAvail().x, UI::GetContentRegionAvail().y};
        const std::vector<ProblemGroup> groups = GroupProblems(m, st);
        if (groups.empty()) {
            UI::Spacing(0.0f);
            UI::TextDisabled(ProblemsEmptyText(m));
            res.empty = true;
        }
        for (const ProblemGroup& g : groups) {
            UI::Spacing(0.0f);
            UI::TextDisabled(ProblemGroupHeader(g).c_str());
            res.groupHeaders.push_back(UI::GetItemRect());
            UI::Indent(8.0f);
            for (int i : g.rows) {
                const ProblemRow& d = m.rows[(std::size_t)i];
                UI::PushID((int)(d.id & 0x7fffffff));
                UI::PushID(i);
                UI::Rect fixRect;
                if (d.canFix) {
                    if (LineButton("Fix")) res.intent.fix = i;
                    fixRect = UI::GetItemRect();
                    if (UI::IsItemHovered() && !d.hint.empty()) UI::SetTooltip("%s", d.hint.c_str());
                    UI::SameLine();
                }
                float c[3]; ProblemSeverityColor(d.severity, c);
                UI::TextColored(B(c[0]), B(c[1]), B(c[2]), ProblemRowText(d).c_str());
                const UI::Rect row = UI::GetItemRect();
                if (UI::IsItemClicked()) res.intent.open = i;
                if (UI::IsItemHovered()) UI::SetTooltip("%s", ProblemRowTooltip(d).c_str());
                if (UI::BeginPopupContextItem("##prowmenu")) {
                    if (UI::Selectable("Go to source", false)) { res.intent.open = i; UI::CloseCurrentPopup(); }
                    res.menuOpen = UI::GetItemRect();
                    if (d.canFix) {
                        if (UI::Selectable("Fix", false)) { res.intent.fix = i; UI::CloseCurrentPopup(); }
                        res.menuFix = UI::GetItemRect();
                    }
                    if (UI::Selectable("Copy message", false)) { res.intent.copy = i; UI::CloseCurrentPopup(); }
                    res.menuCopy = UI::GetItemRect();
                    UI::EndPopup();
                }
                UI::PopID();
                UI::PopID();
                res.rowIndex.push_back(i);
                res.rows.push_back(row);
                res.fixButtons.push_back(fixRect);
            }
            UI::Unindent(8.0f);
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

// ---- Stats ------------------------------------------------------------------------------------------------------------------

StatsResult DrawStats(StatsModel& m, float x, float y, float w, float h) {
    StatsResult res;
    static const FpsHistory kEmpty;
    const FpsHistory& hist = m.history ? *m.history : kEmpty;
    const FpsSummary fs = SummarizeFps(hist);
    UI::BareNextWindow();
    if (UI::Begin("Stats##okayui", x, y, w, h)) {
        EditorRows rows_;
        UI::BeginChild("##statsbody", UI::Vec2{0.0f, 0.0f}, UI::ChildFlags_None);   // a short panel scrolls instead of cutting off
        {   // the big FPS number, the frame time, average and minimum at the right: one frame-tall row, as in the editor
            UI::PushStyleVar(UI::StyleVar_TextRowHeight, 0.0f);
            float c[3]; FpsColor(m.framerate, c);
            char big[24]; std::snprintf(big, sizeof(big), "%.0f", m.framerate);
            UI::PushTextScale(22.0f / 18.0f);
            UI::TextColored(B(c[0]), B(c[1]), B(c[2]), big);
            UI::PopTextScale();
            res.fpsText = UI::GetItemRect();
            UI::SameLine();
            char ms[48]; std::snprintf(ms, sizeof(ms), "FPS   %.2f ms", m.framerate > 0 ? 1000.0f / m.framerate : 0.0f);
            UI::TextDisabled(ms);
            char mm[48]; std::snprintf(mm, sizeof(mm), "avg %.0f  min %.0f", fs.avg, fs.min);
            UI::SameLine();
            const float right = UI::GetCursorPosX() + UI::GetContentRegionAvail().x - UI::CalcTextWidth(mm);
            if (right > UI::GetCursorPosX()) UI::SetCursorPosX(right);
            UI::TextDisabled(mm);
            UI::PopStyleVar();
        }
        {   // the plot: a rounded field 48 tall, the line inside it with the editor's frame padding (12 across, 8 down)
            const UI::Vec2 p = UI::GetCursorScreenPos();
            const float pw = UI::GetContentRegionAvail().x, ph = 48.0f;
            res.plot = UI::Rect{p.x, p.y, pw, ph};
            UI::GetWindowDrawList().AddRectFilled(p, UI::Vec2{p.x + pw, p.y + ph}, UI::GetColorU32(UI::Col_FrameBg), 6.0f);
            UI::SetCursorScreenPos(UI::Vec2{p.x + 10.0f, p.y + 6.0f});   // the plot keeps 2 pixels of its own inside its rectangle
            UI::PushStyleColor(UI::Col_PlotLines, UI::RGBA(115, 179, 255));
            UI::PushStyleColor(UI::Col_Track, UI::RGBA(0, 0, 0, 0));
            UI::PlotOptions po;
            po.valuesOffset = hist.head; po.scaleMin = 0.0f; po.scaleMax = fs.scale; po.size = UI::Vec2{std::max(pw - 20.0f, 24.0f), ph - 12.0f};
            UI::PlotLines("##fps", hist.values, FpsHistory::kCount, po);
            UI::PopStyleColor(2);
            UI::SetCursorScreenPos(UI::Vec2{p.x, p.y + ph});
            UI::Dummy(pw, 0.0f);                                       // the next item goes below the field
        }
        if (fs.scale > 60.0f) {   // the 60 FPS guide line
            res.guideY = res.plot.y + res.plot.h - (60.0f / fs.scale) * res.plot.h;
            UI::GetWindowDrawList().AddLine(UI::Vec2{res.plot.x, res.guideY}, UI::Vec2{res.plot.x + res.plot.w, res.guideY},
                                            UI::RGBA(110, 220, 130, 110), 1.0f);
        }
        UI::Spacing(0.0f);

        // ---- the census: name, number, name, number; zero counts dimmed ----
        UI::BeginGroup();
        UI::PushStyleVar(UI::StyleVar_CellPadding, UI::Vec2{2.0f, 6.0f});
        UI::PushStyleColor(UI::Col_TableBorderStrong, UI::RGBA(0, 0, 0, 0));   // no lines between the columns
        if (UI::BeginTable("##statgrid", 4, UI::Table_NoResize)) {
            UI::TableSetupColumn("n1", UI::Col_None, 1.0f);
            UI::TableSetupColumn("v1", UI::Col_None, 0.8f);
            UI::TableSetupColumn("n2", UI::Col_None, 1.0f);
            UI::TableSetupColumn("v2", UI::Col_None, 0.8f);
            for (std::size_t i = 0; i < m.counters.size(); ++i) {
                if (i % 2 == 0) UI::TableNextRow();
                UI::TableNextColumn();
                UI::TextDisabled(m.counters[i].name.c_str());
                UI::TableNextColumn();
                char num[24]; FormatStatNumber(m.counters[i].value, num, sizeof(num));
                if (m.counters[i].value == 0) UI::TextDisabled(num); else UI::Text(num);
                res.valueCells.push_back(UI::GetItemRect());
            }
            UI::EndTable();
        }
        UI::PopStyleColor();
        UI::PopStyleVar();
        UI::EndGroup();
        res.table = UI::GetItemRect();
        if (UI::BeginPopupContextItem("##statsmenu")) {
            if (UI::Selectable("Copy stats", false)) { res.intent.copy = true; UI::CloseCurrentPopup(); }
            res.menuCopy = UI::GetItemRect();
            UI::EndPopup();
        }
        UI::Spacing(0.0f);

        // ---- the switches: the editor's frame-tall check boxes ----
        UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{12.0f, 8.0f});
        auto box = [&](const char* label, bool* v, UI::Rect& out) {
            if (!v) return false;
            if (UI::Checkbox(label, v)) res.intent.changed = true;
            out = UI::GetItemRect();
            return true;
        };
        const bool f = box("Frustum culling", m.frustumCulling, res.frustumBox);
        if (f) UI::SameLine();
        if (box("Occlusion", m.occlusionCulling, res.occlusionBox) && UI::IsItemHovered()) UI::SetTooltip("%s", kOcclusionTip);
        const bool s = box("Static batching", m.staticBatching, res.staticBox);
        if (s) UI::SameLine();
        if (box("Dynamic batching", m.dynamicBatching, res.dynamicBox) && UI::IsItemHovered()) UI::SetTooltip("%s", kBatchingTip);
        UI::SameLine();
        char sk[40]; std::snprintf(sk, sizeof(sk), "| skinning: %s", m.gpuSkinning ? "GPU" : "CPU");
        UI::TextDisabled(sk);
        UI::PopStyleVar();
        UI::Spacing(0.0f);
        UI::PushTextWrapPos(0.0f);
        UI::TextDisabled(kStatsFooter);
        UI::PopTextWrapPos();
        res.footer = UI::GetItemRect();
        UI::EndChild();
    }
    UI::End();
    return res;
}

// ---- History ----------------------------------------------------------------------------------------------------------------

HistoryResult DrawHistory(const HistoryModel& m, float x, float y, float w, float h) {
    HistoryResult res;
    UI::BareNextWindow();
    if (UI::Begin("History##okayui", x, y, w, h)) {
        EditorRows rows_;
        UI::BeginDisabled(m.undoDepth <= 0);
        if (UI::Button("Undo")) res.intent.undo = 1;
        UI::EndDisabled();
        res.undoButton = UI::GetItemRect();
        if (UI::IsMouseHoveringRect(UI::Vec2{res.undoButton.x, res.undoButton.y}, UI::Vec2{res.undoButton.x + res.undoButton.w, res.undoButton.y + res.undoButton.h})) UI::SetTooltip("Ctrl+Z");
        UI::SameLine();
        UI::BeginDisabled(m.redoDepth <= 0);
        if (UI::Button("Redo")) res.intent.redo = 1;
        UI::EndDisabled();
        res.redoButton = UI::GetItemRect();
        if (UI::IsMouseHoveringRect(UI::Vec2{res.redoButton.x, res.redoButton.y}, UI::Vec2{res.redoButton.x + res.redoButton.w, res.redoButton.y + res.redoButton.h})) UI::SetTooltip("Ctrl+Y");
        UI::SameLine();
        UI::TextDisabled(HistoryStatusText(m).c_str());
        Divider();
        UI::BeginChild("##histlist", UI::Vec2{0.0f, 0.0f}, UI::ChildFlags_None);
        res.list = UI::Rect{UI::GetCursorScreenPos().x, UI::GetCursorScreenPos().y, UI::GetContentRegionAvail().x, UI::GetContentRegionAvail().y};
        if (m.undoDepth <= 0 && m.redoDepth <= 0) {
            UI::Dummy(0.0f, 6.0f);
            UI::TextDisabled(kHistoryEmpty);
            res.empty = true;
        } else {
            // The rail down the left: a node per state, the current one filled with the accent colour, future states hollow and dim.
            const UI::DrawList& dl = UI::GetWindowDrawList();
            const float railX = UI::GetCursorScreenPos().x + 9.0f, railTop = UI::GetCursorScreenPos().y;
            const std::vector<HistoryRow> rows = HistoryRows(m);
            const float rowH = UI::GetFontSize() + 8.0f;   // a text line and the gap below it, as one selectable row
            UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 0.0f});
            dl.AddLine(UI::Vec2{railX, railTop + 4.0f}, UI::Vec2{railX, railTop + (float)rows.size() * rowH - 4.0f}, UI::GetColorU32(UI::Col_Border), 1.0f);
            for (std::size_t r = 0; r < rows.size(); ++r) {
                const HistoryRow& row = rows[r];
                const bool current = row.kind == HistoryCurrent, ahead = row.kind == HistoryAhead;
                UI::PushID((int)r);
                if (ahead) UI::PushStyleColor(UI::Col_Text, UI::GetColorU32(UI::Col_TextDisabled));
                const std::string label = "  " + row.label;   // OkayUI's rows already start 10 pixels in: two spaces leave room for the rail
                const bool hit = UI::Selectable(label.c_str(), current, 0, UI::Vec2{0.0f, rowH});
                if (ahead) UI::PopStyleColor();
                const UI::Rect rr = UI::GetItemRect();
                res.rows.push_back(rr);
                const float cy = rr.y + rr.h * 0.5f;
                if (current) dl.AddCircleFilled(UI::Vec2{railX, cy}, 4.0f, UI::GetColorU32(UI::Col_CheckMark), 12);
                else dl.AddCircle(UI::Vec2{railX, cy}, 3.0f, ahead ? UI::GetColorU32(UI::Col_TextDisabled) : UI::GetColorU32(UI::Col_Text, 0.55f), 12, 1.5f);
                bool jump = hit;
                if (UI::BeginPopupContextItem("##hrowmenu")) {
                    if (!current) {
                        if (UI::Selectable("Jump to this state", false)) { jump = true; UI::CloseCurrentPopup(); }
                        res.menuJump = UI::GetItemRect();
                    }
                    if (UI::Selectable("Copy name", false)) { res.intent.copy = (int)r; UI::CloseCurrentPopup(); }
                    res.menuCopy = UI::GetItemRect();
                    UI::EndPopup();
                }
                if (jump && !current && res.intent.undo == 0 && res.intent.redo == 0) {
                    if (ahead) res.intent.redo = row.steps; else res.intent.undo = row.steps;
                }
                UI::PopID();
            }
            UI::PopStyleVar();
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

} // namespace okayuiscreens
