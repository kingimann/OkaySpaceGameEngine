// The editor's Problems, Stats, History, Tags and Layers, Physics and Profiler panels drawn with OkayUI. Written from what the
// editor's own Dear ImGui panels show and do (editor/EditorPanelViews.cpp draws the same models) and from Dear ImGui's documented
// behaviour where the look had to match; no code from any other project.
#include "OkayUIEditorPanels.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

namespace UI = OkayUI;
using namespace okaypanels;

namespace okayuiscreens {

namespace {

unsigned char B(float v) { return (unsigned char)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); }
UI::Color32 C32(const float c[3], float a = 1.0f) { return UI::RGBA(B(c[0]), B(c[1]), B(c[2]), B(a)); }

// Dear ImGui's spacing in the editor (12 across, 8 down), its frames (a text line and 8 pixels above and below), its text rows (a
// text line is one line tall, not a frame tall) and its framed children (16 and 12 pixels of padding), for as long as the panel draws.
struct EditorRows {
    EditorRows() {
        UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 8.0f});
        UI::PushStyleVar(UI::StyleVar_TextRowHeight, UI::GetFontSize());
        UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{12.0f, 8.0f});
        UI::PushStyleVar(UI::StyleVar_WindowPadding, UI::Vec2{16.0f, 12.0f});
    }
    ~EditorRows() { UI::PopStyleVar(4); }
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
    UI::CenterNextOnLine();   // on a frame-tall row it sits on the row's text, as Dear ImGui's does
    return UI::Button(label, UI::Vec2{UI::CalcTextWidth(label) + 24.0f, UI::GetFontSize()});
}

// The editor's Slate theme values (shared/OkayTheme.hpp) that OkayUI's launcher look does not have, as bytes.
const UI::Color32 kPopupBg = UI::RGBA(35, 39, 48, 251);         // Mix(kRaised, kBase, 0.35), alpha 0.985
const UI::Color32 kHeader = UI::RGBA(53, 68, 95);               // Mix(kControl, accent, 0.18): a selected row
const UI::Color32 kHeaderHovered = UI::RGBA(65, 70, 84);        // Mix(kControl, white, 0.07)
const UI::Color32 kHeaderActive = UI::RGBA(55, 85, 131);        // Mix(kControl, accent, 0.45)
const UI::Color32 kFieldBg = UI::RGBA(41, 46, 59);              // kField
const UI::Color32 kFieldHovered = UI::RGBA(52, 56, 68);         // Mix(kField, white, 0.05)
const UI::Color32 kFieldActive = UI::RGBA(60, 65, 76);          // Mix(kField, white, 0.09)
const UI::Color32 kAccent = UI::RGBA(60, 121, 204);             // the editor's first accent (0.235, 0.475, 0.800)
const UI::Color32 kAccentLit = UI::RGBA(115, 159, 218);         // Mix(accent, white, 0.28): slider grab held, check marks

// Dear ImGui's menus and rows for as long as it lives: a context menu's look (the popup colour, 16 and 12 pixels of padding less
// the half spacing the rows reach past their text, the width its longest entry needs) and the header colours of menu rows and
// selected rows.
struct MenuLook {
    explicit MenuLook(float contentW) {
        UI::PushStyleColor(UI::Col_PopupBg, kPopupBg);
        UI::PushStyleColor(UI::Col_Header, kHeader);
        UI::PushStyleColor(UI::Col_HeaderHovered, kHeaderHovered);
        UI::PushStyleColor(UI::Col_HeaderActive, kHeaderActive);
        UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{10.0f, 8.0f});
        UI::PushStyleVar(UI::StyleVar_PopupWidth, contentW + 32.0f + UI::GetFontSize() + 2.0f);   // the padding, and the room Dear ImGui keeps for a check mark
    }
    ~MenuLook() { UI::PopStyleVar(2); UI::PopStyleColor(4); }
    MenuLook(const MenuLook&) = delete;
    MenuLook& operator=(const MenuLook&) = delete;
};
// The width a menu needs for its longest entry.
float MenuWidth(std::initializer_list<const char*> labels) {
    float wmax = 0.0f;
    for (const char* l : labels) wmax = std::max(wmax, UI::CalcTextWidth(l));
    return wmax;
}
// A menu row: one text line and the 8-pixel item spacing as one flat row (Dear ImGui's highlight covers the spacing).
bool MenuRow(const char* label) {
    UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 0.0f});
    const bool hit = UI::Selectable(label, false, UI::Selectable_Header | UI::Selectable_ClosePopup, UI::Vec2{0.0f, UI::GetFontSize() + 8.0f});
    UI::PopStyleVar();
    return hit;
}
// Dear ImGui's frame colours for fields (text fields, drags, sliders, combos).
struct FieldColors {
    FieldColors() {
        UI::PushStyleColor(UI::Col_FrameBg, kFieldBg);
        UI::PushStyleColor(UI::Col_FrameBgHovered, kFieldHovered);
        UI::PushStyleColor(UI::Col_FrameBgActive, kFieldActive);
        UI::PushStyleColor(UI::Col_SliderGrab, kAccent);
        UI::PushStyleColor(UI::Col_SliderGrabActive, kAccentLit);
    }
    ~FieldColors() { UI::PopStyleColor(5); }
    FieldColors(const FieldColors&) = delete;
    FieldColors& operator=(const FieldColors&) = delete;
};
// The width to give OkayUI for a framed widget `frameW` wide with its label after it (OkayUI's item width includes the label).
float WithLabel(float frameW, const char* label, float gap = 8.0f) {
    std::string vis(label);
    const std::size_t hh = vis.find("##");
    if (hh != std::string::npos) vis.resize(hh);
    return frameW + (vis.empty() ? 0.0f : gap + UI::CalcTextWidth(vis.c_str()));
}
// The hosted panels report their height: the cursor's distance from the top they were given.
float ContentHeight(float top) { return UI::GetCursorScreenPos().y - top; }

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
        UI::SearchField("##pfilter", "Search problems", st.filter, (int)sizeof(st.filter), -(pills + 12.0f));   // the editor's field: magnifier, clear button
        res.searchField = UI::GetItemRect();
        auto pill = [&](const char* id, bool& on, int sev, int count, const char* tip, UI::Rect& out) {
            float c[3]; ProblemSeverityColor(sev, c);
            char buf[32]; std::snprintf(buf, sizeof(buf), "%d%s", count, id);
            // a pill that is on is filled with the editor's accent (ButtonActive), one that is off looks like a field
            UI::PushStyleColor(UI::Col_Button, on ? kAccent : UI::GetColorU32(UI::Col_FrameBg));
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
        MenuLook menus_(MenuWidth({"Go to source", "Copy message"}));
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
                    if (MenuRow("Go to source")) res.intent.open = i;
                    res.menuOpen = UI::GetItemRect();
                    if (d.canFix) {
                        if (MenuRow("Fix")) res.intent.fix = i;
                        res.menuFix = UI::GetItemRect();
                    }
                    if (MenuRow("Copy message")) res.intent.copy = i;
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
        {
            MenuLook menus_(MenuWidth({"Copy stats"}));
            if (UI::BeginPopupContextItem("##statsmenu")) {
                if (MenuRow("Copy stats")) res.intent.copy = true;
                res.menuCopy = UI::GetItemRect();
                UI::EndPopup();
            }
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
            UI::SetCursorPosY(UI::GetCursorPosY() - 4.0f);   // the first row's text at the top, its highlight half the spacing above (clipped)
            const float railX = UI::GetCursorScreenPos().x + 9.0f, railTop = UI::GetCursorScreenPos().y + 4.0f;
            const std::vector<HistoryRow> rows = HistoryRows(m);
            const float rowH = UI::GetFontSize() + 8.0f;   // a text line and the gap below it, as one selectable row
            MenuLook menus_(MenuWidth({"Jump to this state", "Copy name"}));   // also the header colours of the selected row
            UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 0.0f});
            dl.AddLine(UI::Vec2{railX, railTop + 4.0f}, UI::Vec2{railX, railTop + (float)rows.size() * rowH - 4.0f}, UI::GetColorU32(UI::Col_Border), 1.0f);
            for (std::size_t r = 0; r < rows.size(); ++r) {
                const HistoryRow& row = rows[r];
                const bool current = row.kind == HistoryCurrent, ahead = row.kind == HistoryAhead;
                UI::PushID((int)r);
                if (ahead) UI::PushStyleColor(UI::Col_Text, UI::GetColorU32(UI::Col_TextDisabled));
                // Dear ImGui's row: the label after five spaces (room for the rail), the flat header tint when current; the row
                // starts half the spacing left of the text, as Dear ImGui's highlight does (clipped by the list, as there).
                const std::string label = "     " + row.label;
                UI::SetCursorPosX(UI::GetCursorPosX() - 6.0f);
                const bool hit = UI::Selectable(label.c_str(), current, UI::Selectable_Header, UI::Vec2{UI::GetContentRegionAvail().x + 6.0f, rowH});
                if (ahead) UI::PopStyleColor();
                const UI::Rect rr = UI::GetItemRect();
                res.rows.push_back(rr);
                const float cy = rr.y + rr.h * 0.5f;
                if (current) dl.AddCircleFilled(UI::Vec2{railX, cy}, 4.0f, UI::GetColorU32(UI::Col_CheckMark), 12);
                else dl.AddCircle(UI::Vec2{railX, cy}, 3.0f, ahead ? UI::GetColorU32(UI::Col_TextDisabled) : UI::GetColorU32(UI::Col_Text, 0.55f), 12, 1.5f);
                bool jump = hit;
                if (UI::BeginPopupContextItem("##hrowmenu")) {
                    if (!current) {
                        if (MenuRow("Jump to this state")) jump = true;
                        res.menuJump = UI::GetItemRect();
                    }
                    if (MenuRow("Copy name")) res.intent.copy = (int)r;
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

// ---- Tags and Layers (Project Settings) ---------------------------------------------------------------------------------------

namespace {
const unsigned char kBad[3] = {242, 115, 102}, kWarn[3] = {242, 191, 77};   // (0.95, 0.45, 0.40) and (0.95, 0.75, 0.30)
// Dear ImGui's frame-tall check box (the editor's frame padding: a text line and 8 pixels above and below).
bool FrameCheckbox(const char* label, bool* v) {
    UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{12.0f, 8.0f});
    const bool ch = UI::Checkbox(label, v);
    UI::PopStyleVar();
    return ch;
}
// The cursor at `x` pixels from the left edge of the region (Dear ImGui's SameLine(offset) inside a window or a framed child).
void AtX(float x) { UI::SameLine(); UI::SetCursorPosX(x); }
} // namespace

TagsLayersResult DrawTagsLayers(const TagsLayersModel& m, TagsLayersState& st, float x, float y, float w, float h) {
    TagsLayersResult res;
    st.newTag[sizeof(st.newTag) - 1] = '\0';
    st.renameBuf[sizeof(st.renameBuf) - 1] = '\0';
    UI::BareNextWindow();
    if (UI::Begin("TagsLayers##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        const float top = UI::GetCursorScreenPos().y;
        bool strict = m.strict;
        if (FrameCheckbox("Strict tags##ps", &strict)) res.intent.strict = strict ? 1 : 0;
        res.strictBox = UI::GetItemRect();
        if (UI::IsItemHovered()) UI::SetTooltip("%s", kStrictTagsTip);
        if (!m.unlisted.empty()) {
            const unsigned char* c = m.strict ? kBad : kWarn;
            UI::TextColored(c[0], c[1], c[2], UnlistedTagsText(m.unlisted).c_str());
            UI::SameLine();
            if (LineButton("Add them")) res.intent.addUnlisted = true;
            res.addUnlisted = UI::GetItemRect();
        }
        UI::Spacing(0.0f);
        UI::TextDisabled("Tags");
        UI::PushTextWrapPos(0.0f);
        UI::TextUnformatted(BuiltinTagsText(m.builtin).c_str());
        UI::PopTextWrapPos();
        if (m.custom.empty()) UI::TextDisabled("No custom tags yet.");
        else {
            const float rowH = UI::GetFontSize() + 16.0f + 8.0f + 2.0f;   // a frame, the spacing, and 2
            const float listH = std::min(rowH * 4.5f, 12.0f + rowH * (float)m.custom.size());
            UI::BeginChild("##pstags", UI::Vec2{0.0f, listH}, UI::ChildFlags_Borders);
            res.tagList = UI::Rect{UI::GetItemRectMin().x, UI::GetItemRectMin().y, 0.0f, 0.0f};
            const float right = UI::GetCursorPosX() + UI::GetContentRegionAvail().x;
            for (std::size_t i = 0; i < m.custom.size(); ++i) {
                const TagRow& t = m.custom[i];
                UI::PushID((int)i);
                UI::PushStyleVar(UI::StyleVar_TextRowHeight, 0.0f);   // the row is a frame tall, the text centred on it
                UI::Text(t.name.c_str());
                AtX(150.0f - 16.0f);
                UI::TextDisabled(TagUseText(t.used).c_str());
                UI::PopStyleVar();
                AtX(std::max(right - 150.0f, UI::GetCursorPosX()));
                if (LineButton("Rename")) {
                    UI::OpenPopup("##pstagren");
                    st.renaming = (int)i; CopyToBuf(st.renameBuf, sizeof(st.renameBuf), t.name); st.renameError.clear(); st.closeRename = false;
                }
                res.renameButtons.push_back(UI::GetItemRect());
                {
                    MenuLook look_(170.0f + 12.0f + UI::CalcTextWidth("Rename") + 24.0f - UI::GetFontSize() - 2.0f);   // the field, the gap and the button
                    if (UI::BeginPopup("##pstagren")) {
                        if (st.closeRename) { UI::CloseCurrentPopup(); st.closeRename = false; }
                        UI::SetNextItemWidth(170.0f);
                        const bool enter = UI::InputText("##tagrename", st.renameBuf, (int)sizeof(st.renameBuf), UI::InputText_EnterReturnsTrue);
                        res.renameField = UI::GetItemRect();
                        UI::SameLine();
                        const bool click = UI::Button("Rename");
                        res.renameButton = UI::GetItemRect();
                        if (enter || click) {
                            if (t.name == st.renameBuf) UI::CloseCurrentPopup();
                            else { res.intent.renameTag = (int)i; res.intent.renameTo = st.renameBuf; }
                        }
                        if (!st.renameError.empty()) UI::TextColored(kBad[0], kBad[1], kBad[2], st.renameError.c_str());
                        Divider();
                        // the popup's rectangle (its padding is 10 and 8; its width is MenuLook's), for a host that hands out the mouse
                        const float popupW = 170.0f + 12.0f + UI::CalcTextWidth("Rename") + 24.0f + 32.0f;
                        res.popup = UI::Rect{res.renameField.x - 10.0f, res.renameField.y - 8.0f, popupW, UI::GetCursorScreenPos().y - res.renameField.y + 8.0f};
                        UI::EndPopup();
                    }
                }
                UI::SameLine();
                if (LineButton("Remove")) res.intent.removeTag = (int)i;
                res.removeButtons.push_back(UI::GetItemRect());
                if (UI::IsItemHovered()) UI::SetTooltip("%s", kRemoveTagTip);
                UI::PopID();
            }
            UI::EndChild();
            res.tagList = UI::GetItemRect();
        }
        UI::SetNextItemWidth(190.0f);
        const bool enter = UI::InputTextWithHint("##psnewtag", "new tag name", st.newTag, (int)sizeof(st.newTag), UI::InputText_EnterReturnsTrue);
        res.newTagField = UI::GetItemRect();
        UI::SameLine();
        if (UI::Button("Add tag") || enter) res.intent.addTag = true;
        res.addTagButton = UI::GetItemRect();
        if (!st.tagError.empty()) UI::TextColored(kBad[0], kBad[1], kBad[2], st.tagError.c_str());

        UI::Spacing(0.0f);
        UI::TextDisabled("Layers (0 to 31)");
        UI::BeginChild("##pslayers", UI::Vec2{0.0f, 300.0f}, UI::ChildFlags_Borders);
        for (int i = 0; i < (int)m.layers.size() && i < kLayerCount; ++i) {
            const LayerRow& row = m.layers[(std::size_t)i];
            UI::PushID(i);
            char num[8]; std::snprintf(num, sizeof(num), "%2d", i);
            UI::PushStyleVar(UI::StyleVar_TextRowHeight, 0.0f);
            UI::TextDisabled(num);
            UI::PopStyleVar();
            AtX(34.0f - 16.0f);
            // The field edits the state's copy of the name; it takes the model's name whenever it is not being edited.
            char* buf = st.layerBuf[i];
            buf[kLayerBufSize - 1] = '\0';
            UI::SetNextItemWidth(200.0f);
            const bool enterHit = UI::InputTextWithHint("##lname", LayerHint(i, row).c_str(), buf, (int)kLayerBufSize, UI::InputText_EnterReturnsTrue);
            const bool committed = enterHit || UI::IsItemDeactivatedAfterEdit();
            const bool editing = UI::IsItemFocused();
            res.layerFields.push_back(UI::GetItemRect());
            if (committed && res.intent.renameLayer < 0) { res.intent.renameLayer = i; res.intent.layerName = buf; }
            if (!editing && !committed) CopyToBuf(buf, kLayerBufSize, row.name);
            UI::Rect reset;
            if (LayerCanReset(row)) {
                UI::SameLine();
                if (LineButton("Reset")) res.intent.resetLayer = i;
                reset = UI::GetItemRect();
            }
            res.layerResets.push_back(reset);
            UI::PopID();
        }
        UI::EndChild();
        res.layerList = UI::GetItemRect();
        if (!st.layerError.empty()) UI::TextColored(kBad[0], kBad[1], kBad[2], LayerErrorText(st).c_str());
        if (LineButton("Reset all layer names")) res.intent.resetAllLayers = true;
        res.resetAll = UI::GetItemRect();
        res.contentHeight = ContentHeight(top);
    }
    UI::End();
    return res;
}

// ---- Physics (Project Settings) -----------------------------------------------------------------------------------------------

PhysicsSettingsResult DrawPhysicsSettings(PhysicsSettingsModel& m, float x, float y, float w, float h) {
    PhysicsSettingsResult res;
    UI::BareNextWindow();
    if (UI::Begin("Physics##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, UI::Vec2{8.0f, 6.0f});
        const float top = UI::GetCursorScreenPos().y;
        const float itemW = DefaultItemWidth(w);
        if (m.gravity2D) {
            UI::SetNextItemWidth(WithLabel(itemW, "Gravity 2D"));
            if (UI::DragFloat2("Gravity 2D##ps", m.gravity2D, 0.05f, 0.0f, 0.0f, "%.3f")) res.intent.gravityChanged = true;
            res.gravity2D = UI::GetItemRect();
        }
        if (m.gravity3D) {
            UI::SetNextItemWidth(WithLabel(itemW, "Gravity 3D"));
            if (UI::DragFloat3("Gravity 3D##ps", m.gravity3D, 0.05f, 0.0f, 0.0f, "%.3f")) res.intent.gravityChanged = true;
            res.gravity3D = UI::GetItemRect();
        }
        if (m.stepsPerSecond) {
            UI::SetNextItemWidth(WithLabel(160.0f, "Steps per second"));
            if (UI::DragFloat("Steps per second##ps", m.stepsPerSecond, 1.0f, 1.0f, 240.0f, "%.0f")) res.intent.rateChanged = true;
            res.stepRate = UI::GetItemRect();
            if (UI::IsItemHovered()) UI::SetTooltip("%s", kStepRateTip);
        }
        if (m.smooth) {
            if (FrameCheckbox("Smooth the motion##ps", m.smooth)) res.intent.smoothChanged = true;
            res.smooth = UI::GetItemRect();
            if (UI::IsItemHovered()) UI::SetTooltip("%s", kSmoothTip);
        }
        Divider();
        UI::TextDisabled(kPhysicsQualityHeading);
        int q = m.quality == 1 ? 1 : 0;
        UI::SetNextItemWidth(WithLabel(330.0f, "Quality"));
        if (UI::Combo("Quality##psq", kPhysicsQualityNames, 2, &q)) res.intent.quality = q;
        res.quality = UI::GetItemRect();
        if (UI::IsItemHovered()) UI::SetTooltip("%s", kQualityTip);
        if (q == 1) {
            int it = m.solverIterations;
            UI::SetNextItemWidth(WithLabel(160.0f, "Solver iterations"));
            if (UI::SliderInt("Solver iterations##psq", &it, 4, 30, "%d")) res.intent.solverIterations = it;
            res.iterations = UI::GetItemRect();
            if (UI::IsItemHovered()) UI::SetTooltip("%s", kIterationsTip);
            bool cont = m.autoContinuous;
            if (FrameCheckbox("Catch fast bodies automatically##psq", &cont)) res.intent.autoContinuous = cont ? 1 : 0;
            res.continuous = UI::GetItemRect();
            if (UI::IsItemHovered()) UI::SetTooltip("%s", kContinuousTip);
        }
        if (UI::Button("Apply to current scene")) res.intent.apply = true;
        res.apply = UI::GetItemRect();
        res.contentHeight = ContentHeight(top);
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

// ---- Profiler -----------------------------------------------------------------------------------------------------------------

namespace {
// Dear ImGui's line plot: a rounded field, the line inside the frame padding, the overlay text centred at the top.
UI::Rect FramedPlot(const char* id, const std::vector<float>& v, int n, const char* overlay, float top, float height) {
    const UI::Vec2 p = UI::GetCursorScreenPos();
    const float pw = UI::GetContentRegionAvail().x;
    UI::GetWindowDrawList().AddRectFilled(p, UI::Vec2{p.x + pw, p.y + height}, kFieldBg, 6.0f);
    if (n > 0 && !v.empty()) {
        UI::SetCursorScreenPos(UI::Vec2{p.x + 10.0f, p.y + 6.0f});
        UI::PushStyleColor(UI::Col_PlotLines, UI::RGBA(156, 156, 156));   // Dear ImGui's default line grey
        UI::PushStyleColor(UI::Col_Track, UI::RGBA(0, 0, 0, 0));
        UI::PlotOptions po;
        po.scaleMin = 0.0f; po.scaleMax = top; po.size = UI::Vec2{std::max(pw - 20.0f, 24.0f), height - 12.0f};
        UI::PlotLines(id, v.data(), n, po);
        UI::PopStyleColor(2);
    }
    if (overlay && overlay[0]) {
        const float tw = UI::CalcTextWidth(overlay);
        UI::GetWindowDrawList().AddText(UI::Vec2{p.x + (pw - tw) * 0.5f, p.y + 8.0f}, UI::GetColorU32(UI::Col_Text), overlay);
    }
    UI::SetCursorScreenPos(UI::Vec2{p.x, p.y + height});
    UI::Dummy(pw, 0.0f);
    return UI::Rect{p.x, p.y, pw, height};
}
// Dear ImGui's progress bar: a frame-tall field, the fill from the left in `fill`, the text just right of the fill (kept inside).
UI::Rect FramedBar(float frac, const char* text, UI::Color32 fill) {
    const UI::Vec2 p = UI::GetCursorScreenPos();
    const float bw = UI::GetContentRegionAvail().x, bh = UI::GetFontSize() + 16.0f;
    const UI::DrawList& dl = UI::GetWindowDrawList();
    dl.AddRectFilled(p, UI::Vec2{p.x + bw, p.y + bh}, kFieldBg, 6.0f);
    const float f = std::isfinite(frac) ? std::clamp(frac, 0.0f, 1.0f) : 0.0f;
    if (f > 0.0f) dl.AddRectFilled(p, UI::Vec2{p.x + bw * f, p.y + bh}, fill, std::min(6.0f, bw * f * 0.5f));
    const float tw = UI::CalcTextWidth(text);
    const float tx = std::clamp(p.x + bw * f + 12.0f, p.x, p.x + bw - tw - 8.0f);
    dl.AddText(UI::Vec2{tx, p.y + 8.0f}, UI::GetColorU32(UI::Col_Text), text);
    UI::Dummy(bw, bh);
    return UI::Rect{p.x, p.y, bw, bh};
}
UI::Color32 Rgbf(float r, float g, float b) { return UI::RGBA(B(r), B(g), B(b)); }
} // namespace

ProfilerResult DrawProfiler(ProfilerModel& m, float x, float y, float w, float h) {
    ProfilerResult res;
    const int n = ProfilerFrameCount(m);
    const float top = ProfilerGraphTop(m);
    UI::BareNextWindow();
    if (UI::Begin("Profiler##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::BeginChild("##profbody", UI::Vec2{0.0f, 0.0f}, UI::ChildFlags_None);   // a short window scrolls instead of cutting off
        // ---- the controls row ----
        if (m.paused) {
            if (FrameCheckbox("Pause##prof", m.paused)) res.intent.pauseChanged = true;
            res.pauseBox = UI::GetItemRect();
            UI::SameLine();
        }
        ProfilerFixSelection(m);
        if (m.paused && *m.paused && m.selected) {
            if (n > 1) {
                UI::SetNextItemWidth(WithLabel(220.0f, "Frame"));
                if (UI::SliderInt("Frame##prof", m.selected, 0, n - 1, "frame %d")) res.intent.selectChanged = true;
                res.frameSlider = UI::GetItemRect();
            }
        } else UI::TextDisabled("(pause to scrub frames)");
        UI::SameLine(); UI::TextDisabled("16.7ms=60FPS  33.3ms=30FPS");

        // ---- the timeline graphs, oldest to newest ----
        const std::string ov = ProfilerOverlay(m);
        res.framePlot = FramedPlot("##frame", m.total, n, ov.c_str(), top, 80.0f);
        {   // the budget guides (green 60 FPS, amber 30 FPS) and, while paused, the scrubbed frame
            const UI::DrawList& dl = UI::GetWindowDrawList();
            const UI::Rect& r = res.framePlot;
            auto guide = [&](float ms, UI::Color32 col, const char* lbl) {
                if (top <= ms) return 0.0f;
                const float gy = r.y + r.h - (ms / top) * r.h;
                dl.AddLine(UI::Vec2{r.x, gy}, UI::Vec2{r.x + r.w, gy}, col, 1.0f);
                dl.AddText(UI::Vec2{r.x + 4.0f, gy - UI::GetFontSize() - 1.0f}, col, lbl);
                return gy;
            };
            res.guide60 = guide(16.7f, UI::RGBA(110, 220, 130, 170), "16.7 (60)");
            res.guide30 = guide(33.3f, UI::RGBA(240, 190, 80, 170), "33.3 (30)");
            if (m.paused && *m.paused && m.selected && n > 1 && *m.selected >= 0) {
                res.scrubX = r.x + ((float)*m.selected / (float)(n - 1)) * r.w;
                dl.AddLine(UI::Vec2{res.scrubX, r.y}, UI::Vec2{res.scrubX, r.y + r.h}, UI::RGBA(255, 210, 0, 220), 1.5f);
            }
        }
        res.gpuPlot = FramedPlot("##gpu", m.gpu, n, "GPU ms", top, 44.0f);

        // ---- the readouts ----
        char t[128];
        std::snprintf(t, sizeof(t), "Frame %.2f ms (%.0f FPS)", m.frameMs, m.frameMs > 0.001 ? 1000.0 / m.frameMs : 0.0);
        UI::Text(t);
        std::snprintf(t, sizeof(t), "CPU %.2f", m.cpuMs); UI::SameLine(); UI::TextColored(140, 204, 255, t);
        std::snprintf(t, sizeof(t), "GPU %.2f", m.gpuMs); UI::SameLine(); UI::TextColored(255, 199, 115, t);
        std::snprintf(t, sizeof(t), "| avg %.2f  peak %.2f", m.avg, m.peak); UI::SameLine(); UI::Text(t);
        std::snprintf(t, sizeof(t), "Meshes %d    Triangles %d    Particles %d", m.meshes, m.triangles, m.particles);
        UI::Text(t);

        // ---- the CPU categories of this frame ----
        auto bar = [&](int k, const char* label, double ms, UI::Color32 col) {
            float frac = 0.0f;
            const std::string bt = ProfilerBarText(label, ms, m.cpuMs, &frac);
            res.bars[k] = FramedBar(frac, bt.c_str(), col);
        };
        bar(0, "Scripts", m.scriptsMs, Rgbf(0.45f, 0.75f, 1.0f));
        bar(1, "Physics", m.physicsMs, Rgbf(0.55f, 0.9f, 0.55f));
        bar(2, "Render", m.renderMs, Rgbf(1.0f, 0.75f, 0.4f));
        bar(3, "Other", ProfilerOtherMs(m), Rgbf(0.7f, 0.7f, 0.75f));

        UI::TextColored(B(0.85f), B(0.9f), 255, "Diagnosis:");
        UI::SameLine();
        UI::PushTextWrapPos(0.0f);
        UI::TextUnformatted(ProfilerDiagnosis(m).c_str());
        UI::PopTextWrapPos();
        Divider();

        if (LineButton("Copy CSV")) res.intent.copyCsv = true;
        res.copyCsv = UI::GetItemRect();
        UI::SameLine(); UI::TextDisabled("section breakdown  (?)");
        res.help = UI::GetItemRect();
        if (UI::IsItemHovered()) UI::SetTooltip("%s", kProfilerHelp);

        // ---- the sections: self, total, share, calls, average and maximum; indented by depth, hot ones in red ----
        UI::PushStyleColor(UI::Col_TableHeaderBg, UI::RGBA(27, 30, 37));        // Mix(kSunken, kBase, 0.5)
        UI::PushStyleColor(UI::Col_TableBorderStrong, UI::RGBA(255, 255, 255, 18));
        UI::PushStyleColor(UI::Col_TableRowBg, UI::RGBA(0, 0, 0, 0));
        UI::PushStyleColor(UI::Col_TableRowBgAlt, UI::RGBA(255, 255, 255, 6));
        UI::PushStyleVar(UI::StyleVar_CellPadding, UI::Vec2{10.0f, 6.0f});
        const std::vector<int> order = ProfilerRows(m);
        if (UI::BeginTable("profrows", 7, UI::Table_RowBg | UI::Table_BordersInnerH | UI::Table_BordersOuter | UI::Table_ScrollY | UI::Table_NoResize,
                           UI::Vec2{0.0f, 220.0f})) {
            // What Dear ImGui's proportional sizing gives here (measured against it): the number columns as wide as their header or
            // widest cell plus the cell padding, the section names the rest.
            float cw[7] = {0, 0, 0, 0, 0, 0, 0};
            for (int c = 1; c < 7; ++c) cw[c] = UI::CalcTextWidth(kProfilerColumns[c]);
            for (int r : order) {
                char cells[6][24]; ProfilerCells(m, m.sections[(std::size_t)r], cells);
                for (int c = 0; c < 6; ++c) cw[c + 1] = std::max(cw[c + 1], UI::CalcTextWidth(cells[c]));
            }
            UI::TableSetupColumn(kProfilerColumns[0], UI::Col_None, 1.0f);
            for (int c = 1; c < 7; ++c) UI::TableSetupColumn(kProfilerColumns[c], UI::Col_WidthFixed, cw[c] + 14.0f);
            UI::TableSetupScrollFreeze(0, 1);
            UI::TableHeadersRow();
            for (int r : order) {
                const ProfilerSection& b = m.sections[(std::size_t)r];
                char cells[6][24]; ProfilerCells(m, b, cells);
                UI::TableNextRow();
                UI::TableNextColumn();
                const UI::Vec2 rp = UI::GetCursorScreenPos();
                if (b.depth > 0) UI::Indent((float)b.depth * 12.0f);
                if (ProfilerHot(m, b)) UI::TextColored(255, 128, 115, b.name.c_str());
                else UI::Text(b.name.c_str());
                if (b.depth > 0) UI::Unindent((float)b.depth * 12.0f);
                res.rows.push_back(UI::Rect{rp.x, rp.y, 0.0f, UI::GetItemRect().h});
                for (int c = 0; c < 6; ++c) { UI::TableNextColumn(); UI::Text(cells[c]); }
            }
            UI::EndTable();
        }
        res.table = UI::GetItemRect();
        UI::PopStyleVar();
        UI::PopStyleColor(4);
        if (m.sections.empty()) { UI::TextDisabled(ProfilerEmptyText(m)); res.empty = true; }
        UI::EndChild();
    }
    UI::End();
    return res;
}

} // namespace okayuiscreens
