// The editor's Problems, Stats, History, Tags and Layers, Physics, Profiler, Import settings, Post-processing, Player and Rendering panels
// and sections drawn with OkayUI. Written from what the
// editor's own Dear ImGui panels show and do (editor/EditorPanelViews.cpp draws the same models) and from Dear ImGui's documented
// behaviour where the look had to match; no code from any other project.
#include "OkayUIEditorPanels.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

namespace UI = OkayUI;
using namespace okaypanels;

namespace okayuiscreens {

namespace {
EditorMetrics g_metrics;
} // namespace

void SetEditorMetrics(const EditorMetrics& in) {
    const EditorMetrics d;
    auto ok = [](float v, float hi) { return std::isfinite(v) && v >= 0.0f && v <= hi; };
    auto pick = [&](OkayUI::Vec2 v, OkayUI::Vec2 def) { return OkayUI::Vec2{ok(v.x, 200.0f) ? v.x : def.x, ok(v.y, 200.0f) ? v.y : def.y}; };
    g_metrics.itemSpacing = pick(in.itemSpacing, d.itemSpacing);
    g_metrics.framePadding = pick(in.framePadding, d.framePadding);
    g_metrics.innerSpacing = pick(in.innerSpacing, d.innerSpacing);
    g_metrics.itemWidth = ok(in.itemWidth, 100000.0f) ? in.itemWidth : 0.0f;
    g_metrics.indentSpacing = ok(in.indentSpacing, 200.0f) ? in.indentSpacing : d.indentSpacing;
    g_metrics.sepTextPadding = pick(in.sepTextPadding, d.sepTextPadding);
    g_metrics.sepTextBorder = ok(in.sepTextBorder, 20.0f) ? in.sepTextBorder : d.sepTextBorder;
}
const EditorMetrics& GetEditorMetrics() { return g_metrics; }

namespace {

unsigned char B(float v) { return (unsigned char)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); }
UI::Color32 C32(const float c[3], float a = 1.0f) { return UI::RGBA(B(c[0]), B(c[1]), B(c[2]), B(a)); }

// Dear ImGui's spacing in the editor (EditorMetrics: by default 12 across, 8 down; frames a text line and 8 pixels above and below,
// the text 12 in; the host passes the real values), its
// text rows (a text line is one line tall, not a frame tall), its framed children (16 and 12 pixels of padding), its 14-pixel scroll
// bars with a faint grab, its dim text colour, and its check boxes and combos (a frame with a tick; a field with a square arrow button),
// for as long as the panel draws.
struct EditorRows {
    EditorRows() {
        UI::PushStyleVar(UI::StyleVar_ItemSpacing, g_metrics.itemSpacing);
        UI::PushStyleVar(UI::StyleVar_TextRowHeight, UI::GetFontSize());
        UI::PushStyleVar(UI::StyleVar_FramePadding, g_metrics.framePadding);
        UI::PushStyleVar(UI::StyleVar_WindowPadding, UI::Vec2{16.0f, 12.0f});
        UI::PushStyleVar(UI::StyleVar_ScrollbarSize, 14.0f);
        UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
        UI::PushStyleColor(UI::Col_ScrollbarGrab, UI::RGBA(255, 255, 255, 31));          // white at 0.12
        UI::PushStyleColor(UI::Col_ScrollbarGrabHovered, UI::RGBA(255, 255, 255, 61));   // white at 0.24
        UI::PushStyleColor(UI::Col_TextDisabled, UI::RGBA(179, 189, 209));               // the editor's brighter dim text (0.70, 0.74, 0.82)
    }
    ~EditorRows() { UI::PopStyleColor(3); UI::PopStyleVar(6); }
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
const UI::Color32 kPopupBg = UI::RGBA(35, 39, 48, 255);         // Mix(kRaised, kBase, 0.35), opaque (what lies under a menu must not show through)
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
        UI::PushStyleColor(UI::Col_CheckMark, kAccentLit);
        UI::PushStyleColor(UI::Col_Accent, kAccent);   // a ticked box's frame (the accent at 45%) and a focused field's edge
    }
    ~FieldColors() { UI::PopStyleColor(7); }
    FieldColors(const FieldColors&) = delete;
    FieldColors& operator=(const FieldColors&) = delete;
};
// The width to give OkayUI for a framed widget `frameW` wide with its label after it (OkayUI's item width includes the label).
float WithLabel(float frameW, const char* label, float gap = -1.0f) {
    if (gap < 0.0f) gap = g_metrics.innerSpacing.x;
    std::string vis(label);
    const std::size_t hh = vis.find("##");
    if (hh != std::string::npos) vis.resize(hh);
    return frameW + (vis.empty() ? 0.0f : gap + UI::CalcTextWidth(vis.c_str()));
}
// Dear ImGui's default field width where the panel is hosted: the host's (EditorMetrics::itemWidth) or 65% of the window.
float ItemWidthFor(float bodyW) { return g_metrics.itemWidth > 0.0f ? g_metrics.itemWidth : DefaultItemWidth(bodyW); }
// The hosted panels report their height: the cursor's distance from the top they were given, less the item spacing after the last
// row. The host puts one Dear ImGui item that tall in its window, and Dear ImGui adds its own spacing after that item; counting the
// last row's spacing too made every hosted section 8 pixels taller than the Dear ImGui version.
float ContentHeight(float top) { return std::max(0.0f, UI::GetCursorScreenPos().y - top - g_metrics.itemSpacing.y); }

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
        UI::BeginChild("##problist", UI::Vec2{0.0f, 0.0f}, UI::Child_None);
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
        FieldColors fields_;
        // A short panel scrolls instead of cutting off. Its bar stays OkayUI's narrow one: the editor's window puts its 14-pixel bar in the
        // window's right padding, outside the area a host hands to OkayUI, and its table keeps the whole width.
        UI::PushStyleVar(UI::StyleVar_ScrollbarSize, 0.0f);
        UI::BeginChild("##statsbody", UI::Vec2{0.0f, 0.0f}, UI::Child_None);
        {   // the FPS number, the frame time, average and minimum at the right: one frame-tall row, as in the editor. The number is
            // one text line at the row's top (the editor pushes its heading face for it, and Dear ImGui keeps the current size when a
            // face is pushed without one, so it comes out at the body size); the rest is a frame-tall row with the text centred.
            float c[3]; FpsColor(m.framerate, c);
            char big[24]; std::snprintf(big, sizeof(big), "%.0f", m.framerate);
            UI::TextColored(B(c[0]), B(c[1]), B(c[2]), big);
            res.fpsText = UI::GetItemRect();
            UI::PushStyleVar(UI::StyleVar_TextRowHeight, 0.0f);
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
        // Dear ImGui's proportional columns (weights 1, 0.8, 1, 0.8), measured from its output at two widths: the first column's text
        // starts at the left edge, each later one 2 x CellPadding.x (10) after the previous column's share, and the shares split what is
        // left once those gaps are taken: share = (width - 3 x 20) x weight / 3.6. Written as fixed columns with no cell padding across.
        UI::BeginGroup();
        UI::PushStyleVar(UI::StyleVar_CellPadding, UI::Vec2{0.0f, 6.0f});
        UI::PushStyleColor(UI::Col_TableBorderStrong, UI::RGBA(0, 0, 0, 0));   // no lines between the columns
        const float gridW = UI::GetContentRegionAvail().x, gap = 20.0f;
        const float unit = std::max(0.0f, gridW - 3.0f * gap) / 3.6f;
        if (UI::BeginTable("##statgrid", 4, UI::Table_NoResize)) {
            UI::TableSetupColumn("n1", UI::Col_WidthFixed, unit * 1.0f + gap);
            UI::TableSetupColumn("v1", UI::Col_WidthFixed, unit * 0.8f + gap);
            UI::TableSetupColumn("n2", UI::Col_WidthFixed, unit * 1.0f + gap);
            UI::TableSetupColumn("v2", UI::Col_WidthFixed, std::max(1.0f, gridW - unit * 2.8f - 3.0f * gap));
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
        UI::PushStyleVar(UI::StyleVar_FramePadding, g_metrics.framePadding);
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
        UI::PopStyleVar();
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
        UI::BeginChild("##histlist", UI::Vec2{0.0f, 0.0f}, UI::Child_None);
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
    UI::PushStyleVar(UI::StyleVar_FramePadding, g_metrics.framePadding);
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
            const float rowH = UI::GetFontSize() + 2.0f * g_metrics.framePadding.y + g_metrics.itemSpacing.y + 2.0f;   // a frame, the spacing, and 2
            const float listH = std::min(rowH * 4.5f, 12.0f + rowH * (float)m.custom.size());
            UI::BeginChild("##pstags", UI::Vec2{0.0f, listH}, UI::Child_Borders);
            res.tagList = UI::Rect{UI::GetItemRectMin().x, UI::GetItemRectMin().y, 0.0f, 0.0f};
            const float right = UI::GetCursorPosX() + UI::GetContentRegionAvail().x;
            const float rowsTop = UI::GetCursorScreenPos().y;
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
            // The editor's version steps its list clipper by rowH, so its content ends n rows of rowH below the top (2 pixels a row more
            // than the rows take): four rows overflow the list by a few pixels there and it shows its scroll bar. The same here.
            const float short_ = rowsTop + rowH * (float)m.custom.size() - UI::GetCursorScreenPos().y;
            if (short_ > 0.0f) UI::Dummy(1.0f, short_);
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
        UI::BeginChild("##pslayers", UI::Vec2{0.0f, 300.0f}, UI::Child_Borders);
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
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        const float top = UI::GetCursorScreenPos().y;
        const float itemW = ItemWidthFor(w);
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
        UI::TextDisabled(kPhysicsSolverHeading);
        int it = m.solverIterations;
        UI::SetNextItemWidth(WithLabel(160.0f, "Solver iterations"));
        if (UI::SliderInt("Solver iterations##psq", &it, 4, 30, "%d")) res.intent.solverIterations = it;
        res.iterations = UI::GetItemRect();
        if (UI::IsItemHovered()) UI::SetTooltip("%s", kIterationsTip);
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
        UI::BeginChild("##profbody", UI::Vec2{0.0f, 0.0f}, UI::Child_None);   // a short window scrolls instead of cutting off
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
            // widest cell plus the cell padding on both sides (10 and 10), the section names the rest.
            float cw[7] = {0, 0, 0, 0, 0, 0, 0};
            for (int c = 1; c < 7; ++c) cw[c] = UI::CalcTextWidth(kProfilerColumns[c]);
            for (int r : order) {
                char cells[6][24]; ProfilerCells(m, m.sections[(std::size_t)r], cells);
                for (int c = 0; c < 6; ++c) cw[c + 1] = std::max(cw[c + 1], UI::CalcTextWidth(cells[c]));
            }
            UI::TableSetupColumn(kProfilerColumns[0], UI::Col_None, 1.0f);
            for (int c = 1; c < 7; ++c) UI::TableSetupColumn(kProfilerColumns[c], UI::Col_WidthFixed, cw[c] + 20.0f);
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
        // ---- the texture library: every picture it holds, its size and memory, and the budget (asked only while open) ----
        if (m.textures) {
            const bool open = UI::CollapsingHeader("Textures");
            res.texturesHeader = UI::GetItemRect();
            if (open) {
                ProfilerTextures tx;
                m.textures(tx);
                UI::TextUnformatted(tx.line.c_str());
                if (tx.overBudget) { UI::TextColored(255, 128, 115, kProfilerOverBudget); res.texturesOverBudget = true; }
                UI::TextDisabled(ProfilerTexturesNote(tx).c_str());
                UI::PushStyleVar(UI::StyleVar_CellPadding, UI::Vec2{10.0f, 6.0f});
                if (UI::BeginTable("proftex", 5, UI::Table_RowBg | UI::Table_BordersInnerH | UI::Table_BordersOuter | UI::Table_ScrollY | UI::Table_NoResize,
                                   UI::Vec2{0.0f, 180.0f})) {
                    UI::TableSetupColumn(kProfilerTextureColumns[0], UI::Col_None, 3.0f);
                    for (int c = 1; c < 5; ++c) UI::TableSetupColumn(kProfilerTextureColumns[c], UI::Col_None, 1.0f);
                    UI::TableSetupScrollFreeze(0, 1);
                    UI::TableHeadersRow();
                    for (const ProfilerTexture& t : tx.rows) {
                        char cell[48];
                        UI::TableNextRow();
                        UI::TableNextColumn();
                        UI::TextUnformatted(ProfilerTextureName(t.path).c_str());
                        res.textureRows.push_back(UI::GetItemRect());
                        if (UI::IsItemHovered()) UI::SetTooltip("%s", t.path.c_str());
                        UI::TableNextColumn(); std::snprintf(cell, sizeof(cell), "%dx%d", t.width, t.height); UI::Text(cell);
                        UI::TableNextColumn(); std::snprintf(cell, sizeof(cell), "%d", t.mipLevels); UI::Text(cell);
                        UI::TableNextColumn(); std::snprintf(cell, sizeof(cell), "%.2f", t.megabytes); UI::Text(cell);
                        UI::TableNextColumn(); UI::TextUnformatted(t.kind.c_str());
                    }
                    UI::EndTable();
                }
                UI::PopStyleVar();
            }
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

// ---- Import settings (the Project panel) ----------------------------------------------------------------------------------------

namespace {
// The import panel's combo: a 160-pixel field and the label after it.
bool ImportCombo(const char* label, int& value, const char* const* items, int count, UI::Rect& rect) {
    UI::SetNextItemWidth(WithLabel(160.0f, label));
    int v = value >= 0 && value < count ? value : -1;
    const bool ch = UI::Combo(label, items, count, &v);
    rect = UI::GetItemRect();
    if (ch && v >= 0 && v < count && v != value) { value = v; return true; }
    return false;
}
} // namespace

ImportSettingsResult DrawImportSettings(ImportSettingsModel& m, ImportSettingsState& st, float x, float y, float w, float h) {
    ImportSettingsResult res;
    UI::BareNextWindow();
    if (UI::Begin("ImportSettings##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        const float top = UI::GetCursorScreenPos().y;
        bool body = m.edit != nullptr;
        if (body) UI::TextDisabled(kImportHeading);
        if (body && m.newer) { UI::TextDisabled(kImportNewerNote); body = false; }
        ImportFields* e = body ? m.edit : nullptr;
        if (e) switch (e->kind) {
            case ImportTexture: {
                int si = ImportSizeIndex(e->maxSize);
                if (ImportCombo("Largest side", si, kImportSizeNames, 11, res.sizeCombo)) { e->maxSize = kImportSizes[si]; res.intent.changed = true; }
                UI::TextDisabled("Bigger pictures are made smaller when loaded.");
                if (m.showSampler) {
                    if (ImportCombo("Filter", e->filter, kImportFilterNames, 3, res.filterCombo)) res.intent.changed = true;
                    UI::TextDisabled("Point keeps hard pixel edges.");
                    if (ImportCombo("Wrap", e->wrap, kImportWrapNames, 3, res.wrapCombo)) res.intent.changed = true;
                    UI::TextDisabled("What is drawn past the picture's edge.");
                    if (UI::Checkbox("Mipmaps", &e->mipmaps)) res.intent.changed = true;
                    res.mipmaps = UI::GetItemRect();
                    UI::TextDisabled("Smaller copies for far away. Off: always full size.");
                    UI::TextDisabled("Applies in the editor view and OpenGL / software games.");
                }
                break;
            }
            case ImportModel: {
                UI::SetNextItemWidth(WithLabel(160.0f, "Scale"));
                if (UI::DragFloat("Scale", &e->scale, 0.01f, 0.0001f, 10000.0f, "%.4f")) res.intent.changed = true;
                res.scale = UI::GetItemRect();
                UI::TextDisabled("Multiplies its size when you add it to a scene.");
                if (ImportCombo("Normals", e->normals, kImportNormalsNames, 3, res.normalsCombo)) res.intent.changed = true;
                if (e->normals == 1) {
                    UI::SetNextItemWidth(WithLabel(160.0f, "Smoothing angle"));
                    if (UI::DragFloat("Smoothing angle", &e->smoothingAngle, 0.5f, 0.0f, 180.0f, "%.0f deg")) res.intent.changed = true;
                    res.smoothing = UI::GetItemRect();
                    UI::TextDisabled("Sharper edges stay hard. Not for rigged parts.");
                } else if (e->normals == 2) {
                    UI::TextDisabled("Each face shaded alone. Not for rigged parts.");
                }
                if (UI::Checkbox("Import animations", &e->importAnimations)) res.intent.changed = true;
                res.animations = UI::GetItemRect();
                UI::TextDisabled("Off: no clips, rest pose.");
                UI::TextDisabled("Used when the model is next added to a scene.");
                break;
            }
            case ImportAudio: {
                if (ImportCombo("Loading", e->loadMode, kImportLoadNames, 2, res.loadCombo)) res.intent.changed = true;
                UI::TextDisabled("Read while it plays keeps a long file in a small fixed amount of memory.");
                if (UI::Checkbox("Mix down to one channel", &e->forceMono)) res.intent.changed = true;
                res.mono = UI::GetItemRect();
                UI::TextDisabled("Left and right become a single channel. Off keeps the recording as it is.");
                UI::TextDisabled("Used the next time the file is loaded.");
                break;
            }
            default: e = nullptr; break;
        }
        if (e) {
            res.enabled = !ImportSame(*e, m.original);
            UI::BeginDisabled(!res.enabled);
            if (UI::Button("Apply")) res.intent.apply = true;
            res.apply = UI::GetItemRect();
            UI::SameLine();
            if (UI::Button("Revert")) res.intent.revert = true;
            res.revert = UI::GetItemRect();
            UI::EndDisabled();
            UI::SameLine();
            UI::TextDisabled(kImportSavedNote);
            if (st.showFail) { UI::OpenPopup("##impfail"); st.showFail = false; }
            MenuLook look_(UI::CalcTextWidth(st.failMsg.c_str()) - UI::GetFontSize() - 2.0f);
            if (UI::BeginPopup("##impfail")) {
                const UI::Vec2 p0 = UI::GetCursorScreenPos();
                UI::TextUnformatted(st.failMsg.c_str());
                res.popup = UI::Rect{p0.x - 10.0f, p0.y - 8.0f, UI::CalcTextWidth(st.failMsg.c_str()) + 32.0f, UI::GetCursorScreenPos().y - p0.y + 16.0f};
                UI::EndPopup();
            }
        }
        res.contentHeight = ContentHeight(top);
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

// ---- Post-processing (Lighting) -------------------------------------------------------------------------------------------------

PostProcessResult DrawPostProcess(PostProcessModel& m, float x, float y, float w, float h) {
    PostProcessResult res;
    UI::BareNextWindow();
    if (UI::Begin("PostProcess##okayui", x, y, w, h) && m.v) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        const float top = UI::GetCursorScreenPos().y;
        const float itemW = ItemWidthFor(w);
        PostValues& p = *m.v;
        bool& ch = res.intent.changed;
        auto tip = [](const char* t) { if (UI::IsItemHovered()) UI::SetTooltip("%s", t); };
        auto slider = [&](const char* label, float* v, float lo, float hi, const char* fmt, UI::Rect* rect = nullptr) {
            UI::SetNextItemWidth(WithLabel(itemW, label));
            if (UI::SliderFloat(label, v, lo, hi, fmt)) ch = true;
            if (rect) *rect = UI::GetItemRect();
        };
        auto slider3 = [&](const char* label, float* v, float lo, float hi, UI::Rect* rect = nullptr) {
            UI::SetNextItemWidth(WithLabel(itemW, label));
            if (UI::SliderFloat3(label, v, lo, hi, "%.2f")) ch = true;
            if (rect) *rect = UI::GetItemRect();
        };
        auto box = [&](const char* label, bool* v, UI::Rect* rect = nullptr) {
            if (UI::Checkbox(label, v)) ch = true;
            if (rect) *rect = UI::GetItemRect();
        };
        auto note = [](const std::string& n) {
            if (n.empty()) return;
            UI::PushTextWrapPos(0.0f);
            UI::TextDisabled(n.c_str());
            UI::PopTextWrapPos();
        };
        {   // the look presets: a list to choose from (the box always says "Choose..."), then Reset
            MenuLook look_(itemW - 32.0f - UI::GetFontSize() - 2.0f);   // the list as wide as the box
            UI::SetNextItemWidth(WithLabel(itemW, "Look Preset"));
            if (UI::BeginCombo("Look Preset", "Choose...")) {
                const UI::Vec2 p0 = UI::GetCursorScreenPos();
                for (int i = 0; i < (int)m.presets.size(); ++i)
                    if (MenuRow(m.presets[(std::size_t)i].c_str())) res.intent.preset = i;
                res.popup = UI::Rect{p0.x - 10.0f, p0.y - 8.0f, itemW, UI::GetCursorScreenPos().y - p0.y + 16.0f};
                UI::EndCombo();
            }
        }
        res.preset = UI::GetItemRect();
        tip(kLookPresetTip);
        UI::SameLine();
        if (UI::Button("Reset##look")) res.intent.reset = true;
        res.reset = UI::GetItemRect();
        tip(kLookResetTip);
        UI::TextDisabled(PostChainText(m.chainActive));

        box("Filmic Rendering (linear lighting + tonemap)", &p.filmic, &res.filmic);
        tip(kFilmicTip);
        slider("Exposure", &p.exposure, 0.1f, 4.0f, "%.2f", &res.exposure);
        {
            int t = p.tonemap >= 0 && p.tonemap < 3 ? p.tonemap : -1;
            UI::SetNextItemWidth(WithLabel(itemW, "Tonemap"));
            if (UI::Combo("Tonemap", kTonemapNames, 3, &t) && t != p.tonemap) { p.tonemap = t; ch = true; }
            res.tonemap = UI::GetItemRect();
            tip(kTonemapTip);
        }
        box("Bloom", &p.bloom, &res.bloom);
        if (p.bloom) {
            slider("Threshold##bloom", &p.bloomThreshold, 0.0f, 3.0f, "%.2f");
            tip(kBloomThresholdTip);
            slider("Intensity##bloom", &p.bloomIntensity, 0.0f, 3.0f, "%.2f");
            slider("Radius##bloom", &p.bloomRadius, 0.25f, 3.0f, "%.2f");
            slider("Softness##bloom", &p.bloomSoftness, 0.0f, 1.0f, "%.2f");
            tip(kBloomSoftnessTip);
        }
        box("Colour Grading", &p.grading, &res.grading);
        if (p.grading) {
            slider("Temperature", &p.temperature, -1.0f, 1.0f, "%.2f");
            tip(kTemperatureTip);
            slider("Tint", &p.tint, -1.0f, 1.0f, "%.2f");
            slider("Saturation", &p.saturation, 0.0f, 2.0f, "%.2f");
            slider("Contrast", &p.contrast, 0.5f, 2.0f, "%.2f");
            slider3("Lift", p.lift, -0.5f, 0.5f, &res.lift);
            tip(kLiftTip);
            slider3("Gamma", p.gamma, -1.0f, 1.0f);
            tip(kGammaTip);
            slider3("Gain", p.gain, -0.5f, 0.5f);
            tip(kGainTip);
        }
        slider("Vignette", &p.vignette, 0.0f, 1.0f, "%.2f", &res.vignette);
        tip(kVignetteTip);
        if (p.vignette > 0.0f) {
            slider("Vignette Smoothness", &p.vignetteSmooth, 0.05f, 1.0f, "%.2f");
            note(m.vignetteNote);
        }
        box("Anti-aliasing (FXAA)", &p.fxaa, &res.fxaa);
        box("Ambient Occlusion (SSAO)", &p.ssao, &res.ssao);
        note(m.ssaoNote);
        if (p.ssao) {
            slider("Radius##ssao", &p.ssaoRadius, 0.05f, 3.0f, "%.2f");
            slider("Intensity##ssao", &p.ssaoIntensity, 0.0f, 2.0f, "%.2f");
            slider("Power##ssao", &p.ssaoPower, 0.25f, 4.0f, "%.2f");
            tip(kSsaoPowerTip);
        }
        box("Depth of Field", &p.dof, &res.dof);
        note(m.dofNote);
        if (p.dof) {
            UI::SetNextItemWidth(WithLabel(itemW, "Focus Distance"));
            if (UI::DragFloat("Focus Distance", &p.dofFocus, 0.1f, 0.1f, 500.0f, "%.1f")) ch = true;
            res.focus = UI::GetItemRect();
            UI::SetNextItemWidth(WithLabel(itemW, "Focus Range"));
            if (UI::DragFloat("Focus Range", &p.dofRange, 0.1f, 0.1f, 500.0f, "%.1f")) ch = true;
            tip(kDofRangeTip);
        }
        UI::Spacing(0.0f);
        UI::PushTextWrapPos(0.0f);
        UI::TextDisabled(kPostRendererNote);
        UI::PopTextWrapPos();
        res.note = UI::GetItemRect();
        res.contentHeight = ContentHeight(top);
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

// ---- Player and Rendering (Project Settings) -------------------------------------------------------------------------------------

PlayerSettingsResult DrawPlayerSettings(PlayerSettingsModel& m, float x, float y, float w, float h) {
    PlayerSettingsResult res;
    UI::BareNextWindow();
    if (UI::Begin("PlayerSettings##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        const float top = UI::GetCursorScreenPos().y;
        if (m.company && m.companySize > 0) {
            m.company[m.companySize - 1] = '\0';
            UI::SetNextItemWidth(WithLabel(ItemWidthFor(w), "Company"));
            if (UI::InputText("Company##ps", m.company, (int)std::min<std::size_t>(m.companySize, 1u << 20))) res.intent.changed = true;
            res.company = UI::GetItemRect();
        }
        if (m.version && m.versionSize > 0) {
            m.version[m.versionSize - 1] = '\0';
            UI::SetNextItemWidth(WithLabel(160.0f, "Version"));
            if (UI::InputText("Version##ps", m.version, (int)std::min<std::size_t>(m.versionSize, 1u << 20))) res.intent.changed = true;
            res.version = UI::GetItemRect();
        }
        if (m.width && m.height) {
            UI::SetNextItemWidth(WithLabel(110.0f, "Width"));
            // the step buttons: two squares a frame tall before the label, 8 pixels apart (the framed InputInt)
            auto steps = [](const UI::Rect& r, const char* label, UI::Rect& minus, UI::Rect& plus) {
                const float right = r.x + r.w - (UI::CalcTextWidth(label) + 8.0f);
                plus = UI::Rect{right - r.h, r.y, r.h, r.h};
                minus = UI::Rect{plus.x - 8.0f - r.h, r.y, r.h, r.h};
            };
            if (UI::InputInt("Width##ps", m.width)) res.intent.changed = true;
            res.width = UI::GetItemRect();
            steps(res.width, "Width", res.widthMinus, res.widthPlus);
            UI::SameLine();
            UI::SetNextItemWidth(WithLabel(110.0f, "Height"));
            if (UI::InputInt("Height##ps", m.height)) res.intent.changed = true;
            res.height = UI::GetItemRect();
            steps(res.height, "Height", res.heightMinus, res.heightPlus);
        }
        res.contentHeight = ContentHeight(top);
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

RenderingSettingsResult DrawRenderingSettings(RenderingSettingsModel& m, float x, float y, float w, float h) {
    RenderingSettingsResult res;
    UI::BareNextWindow();
    if (UI::Begin("RenderingSettings##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        const float top = UI::GetCursorScreenPos().y;
        if (m.ambient) {
            UI::SetNextItemWidth(WithLabel(ItemWidthFor(w), "Ambient"));
            if (UI::SliderFloat("Ambient##ps", m.ambient, 0.0f, 1.0f, "%.3f")) res.intent.changed = true;
            res.ambient = UI::GetItemRect();
        }
        if (m.skybox) {
            if (UI::Checkbox("Skybox##ps", m.skybox)) res.intent.changed = true;
            res.skybox = UI::GetItemRect();
        }
        if (UI::Button("Apply to current scene##rend")) res.intent.apply = true;
        res.apply = UI::GetItemRect();
        res.contentHeight = ContentHeight(top);
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

// ---- Hierarchy -------------------------------------------------------------------------------------------------------------------
// Written from what the editor's Hierarchy draws and does (editor/main.cpp DrawHierarchy, its row helpers and Dear ImGui's tree
// node geometry as the editor uses it); the rows, the icons and the rules come from shared/HierarchyPanelModel.hpp.

namespace {

// The Pen of HierarchyPanelModel.hpp on OkayUI's window draw list.
struct OkPen {
    const UI::DrawList& dl;
    void line(float x0, float y0, float x1, float y1, std::uint32_t c, float t) { dl.AddLine(UI::Vec2{x0, y0}, UI::Vec2{x1, y1}, c, t); }
    void rect(float x0, float y0, float x1, float y1, std::uint32_t c, float r, float t) { dl.AddRect(UI::Vec2{x0, y0}, UI::Vec2{x1, y1}, c, r, t); }
    void rectFilled(float x0, float y0, float x1, float y1, std::uint32_t c, float r) { dl.AddRectFilled(UI::Vec2{x0, y0}, UI::Vec2{x1, y1}, c, r); }
    void circle(float cx, float cy, float r, std::uint32_t c, int seg, float t) { dl.AddCircle(UI::Vec2{cx, cy}, r, c, seg, t); }
    void circleFilled(float cx, float cy, float r, std::uint32_t c, int seg) { dl.AddCircleFilled(UI::Vec2{cx, cy}, r, c, seg); }
    void triangleFilled(float ax, float ay, float bx, float by, float cx, float cy, std::uint32_t c) {
        dl.AddTriangleFilled(UI::Vec2{ax, ay}, UI::Vec2{bx, by}, UI::Vec2{cx, cy}, c);
    }
    void polyline(const float* xy, int n, std::uint32_t c, bool closed, float t) {
        UI::Vec2 pts[16];
        n = std::min(n, 16);
        for (int i = 0; i < n; ++i) pts[i] = UI::Vec2{xy[i * 2], xy[i * 2 + 1]};
        dl.AddPolyline(pts, n, c, closed, t);
    }
    void arc(float cx, float cy, float r, float a0, float a1, int seg, std::uint32_t c, float t) {
        dl.PathClear(); dl.PathArcTo(UI::Vec2{cx, cy}, r, a0, a1, seg); dl.PathStroke(c, false, t);
    }
    void text(float x, float y, std::uint32_t c, const char* s) { dl.AddText(UI::Vec2{x, y}, c, s); }
};

// The toolbar's square icon buttons: a frame-tall button with a drawn plus or three dots (editor/EditorWidgets.cpp IconButton),
// the accent fill while `active`.
bool ToolIconButton(const char* id, bool plus, bool active, UI::Rect& rect) {
    const float fh = UI::GetFrameHeight();
    if (active) {
        UI::PushStyleColor(UI::Col_Button, UI::ColorMulAlpha(kAccent, 0.90f));
        UI::PushStyleColor(UI::Col_ButtonHovered, kAccent);
        UI::PushStyleColor(UI::Col_ButtonActive, UI::ColorMulAlpha(kAccent, 0.80f));
    }
    const bool hit = UI::Button(id, UI::Vec2{fh, fh});
    if (active) UI::PopStyleColor(3);
    rect = UI::GetItemRect();
    const bool hot = UI::IsItemHovered();
    const UI::Color32 col = active ? UI::RGBA(255, 255, 255) : UI::GetColorU32(UI::Col_Text, hot ? 1.0f : 0.8f);
    const UI::DrawList& dl = UI::GetWindowDrawList();
    const float cx = rect.x + rect.w * 0.5f, cy = rect.y + rect.h * 0.5f, s = fh * 0.5f;
    if (plus) {
        dl.AddLine(UI::Vec2{cx - s * 0.4f, cy}, UI::Vec2{cx + s * 0.4f, cy}, col, 2.0f);
        dl.AddLine(UI::Vec2{cx, cy - s * 0.4f}, UI::Vec2{cx, cy + s * 0.4f}, col, 2.0f);
    } else {
        for (int i = -1; i <= 1; ++i) dl.AddCircleFilled(UI::Vec2{cx + i * s * 0.36f, cy}, s * 0.1f, col);
    }
    return hit;
}

// How wide a menu of these entries is, in Dear ImGui's columns: the longest label, the spacing and the longest shortcut, the spacing
// (MenuLook adds the padding and the room for a check mark or an arrow).
float PanelMenuWidth(const std::vector<PanelMenuEntry>& es) {
    float label = 0.0f, shortcut = 0.0f;
    for (const PanelMenuEntry& e : es) {
        label = std::max(label, UI::CalcTextWidth(e.label.c_str()));
        if (!e.shortcut.empty()) shortcut = std::max(shortcut, UI::CalcTextWidth(e.shortcut.c_str()));
    }
    return label + (shortcut > 0.0f ? 12.0f + shortcut : 0.0f) + 12.0f;
}

// A menu's entries, drawn with OkayUI: rows in the popup body (MenuItem inside a popup), sub-menus to the right (BeginMenu). `top`:
// the popup's own rows (their ids and rectangles go into `topIds` and `topRows`). The chosen item's id goes to `chosen`.
void DrawPanelMenu(const std::vector<PanelMenuEntry>& es, std::string& chosen, std::vector<std::string>* topIds, std::vector<UI::Rect>* topRows) {
    const bool top = topIds != nullptr && topRows != nullptr;
    for (const PanelMenuEntry& e : es) {
        switch (e.kind) {
        case PanelMenuSeparator:
            if (top) Divider(); else UI::MenuSeparator();
            break;
        case PanelMenuHeading:
            if (top && e.color) UI::TextColored((unsigned char)(e.color & 0xff), (unsigned char)((e.color >> 8) & 0xff), (unsigned char)((e.color >> 16) & 0xff), e.label.c_str());
            else if (top) UI::TextDisabled(e.label.c_str());
            else UI::MenuItem(e.label.c_str(), nullptr, false, false);
            break;
        case PanelMenuSub:
            if (!e.enabled) { UI::MenuItem(e.label.c_str(), nullptr, false, false); break; }
            if (UI::BeginMenu(e.label.c_str())) {
                if (top) { topIds->push_back(e.id); topRows->push_back(UI::GetItemRect()); }
                UI::PushStyleVar(UI::StyleVar_PopupWidth, PanelMenuWidth(e.children) + 40.0f);
                DrawPanelMenu(e.children, chosen, nullptr, nullptr);
                UI::PopStyleVar();
                UI::EndMenu();
            } else if (top) { topIds->push_back(e.id); topRows->push_back(UI::GetItemRect()); }
            break;
        default: {
            const bool hit = UI::MenuItem(e.label.c_str(), e.shortcut.empty() ? nullptr : e.shortcut.c_str(), e.kind == PanelMenuToggle && e.checked, e.enabled);
            if (top) {
                topIds->push_back(e.id); topRows->push_back(UI::GetItemRect());
                if (UI::IsItemHovered() || (!e.enabled && UI::IsMouseHoveringRect(UI::GetItemRectMin(), UI::GetItemRectMax()))) {
                    if (!e.enabled && !e.reason.empty()) UI::SetTooltip("%s", e.reason.c_str());
                    else if (!e.tip.empty()) UI::SetTooltip("%s", e.tip.c_str());
                }
            }
            if (hit && e.enabled) chosen = e.id;
            break;
        }
        }
    }
}

const UI::Color32 kDimText = UI::RGBA(140, 140, 140);           // ImVec4(0.55, 0.55, 0.55): an object that is off or outside the isolated one
const UI::Color32 kPrefabText = UI::RGBA(140, 194, 255);        // ImVec4(0.55, 0.76, 1.0): a prefab instance's root
const UI::Color32 kHeaderMain = UI::RGBA(150, 152, 160);
const UI::Color32 kHeaderOther = UI::RGBA(120, 190, 255);
const float kRowTextDy = 1.0f;   // OkayUI's glyphs sit a pixel higher in the line than Dear ImGui's (measured on the comparison pictures)

} // namespace

HierarchyResult DrawHierarchy(const HierarchyModel& m, HierarchyState& st, float x, float y, float w, float h) {
    HierarchyResult res;
    HierarchyIntent& in = res.intent;
    st.rowsFilled = 0;
    UI::BareNextWindow();
    if (UI::Begin("Hierarchy##okayui", x, y, w, h)) {
        EditorRows rows_;
        const float fh = UI::GetFrameHeight(), fs = UI::GetFontSize();
        bool openMenu = false;
        // ---- the toolbar: [+] [search......] [...]
        if (ToolIconButton("##hcreate", true, false, res.createButton)) { st.menuKind = HierMenuCreate; st.menuRow = 0; openMenu = true; }
        if (UI::IsItemHovered()) UI::SetTooltip("Create object");
        UI::SameLine();
        {
            FieldColors fc_;
            const float sw = UI::GetContentRegionAvail().x - (fh + 12.0f);
            if (UI::SearchField("##hfilter", "Search", st.filter, (int)sizeof st.filter, sw)) in.filterChanged = true;
            res.search = UI::GetItemRect();
            if (!st.filter[0] && UI::IsMouseHoveringRect(UI::Vec2{res.search.x, res.search.y}, UI::Vec2{res.search.x + res.search.w, res.search.y + res.search.h}))
                UI::SetTooltip("%s", kHierSearchTip);
        }
        UI::SameLine();
        if (ToolIconButton("##hmore", false, m.sortAZ, res.optionsButton)) { st.menuKind = HierMenuOptions; st.menuRow = 0; openMenu = true; }
        if (UI::IsItemHovered()) UI::SetTooltip("Hierarchy options");
        // ---- the scene strip: its name (and the unsaved marker), the object count at the right
        UI::Dummy(0.0f, 0.0f);
        {
            const std::string title = HierSceneTitle(m.sceneName, m.dirty);
            UI::TextUnformatted(title.c_str());
            const UI::Rect tr = UI::GetItemRect();
            OkPen pen{UI::GetWindowDrawList()};
            if (m.sceneDiag > 0) {
                const UI::Vec2 bang = UI::CalcTextSize("!");
                const float cx = tr.x + UI::CalcTextWidth(title.c_str()) + 16.0f, cy = tr.y + tr.h * 0.5f;
                HierDrawDiag(pen, m.sceneDiag, cx, cy, bang.x, bang.y);
                if (!m.sceneDiagTip.empty() && UI::IsMouseHoveringRect(UI::Vec2{cx - 9.0f, cy - 9.0f}, UI::Vec2{cx + 9.0f, cy + 9.0f})) UI::SetTooltip("%s", m.sceneDiagTip.c_str());
            }
            char cnt[24]; std::snprintf(cnt, sizeof cnt, "%d", m.objectCount);
            const float cw = UI::CalcTextWidth(cnt);
            UI::SameLine();
            UI::SetCursorScreenPos(UI::Vec2{x + w - 16.0f - cw, tr.y});
            UI::TextDisabled(cnt);
            const UI::Rect cr = UI::GetItemRect();
            if (UI::IsMouseHoveringRect(UI::Vec2{cr.x, cr.y}, UI::Vec2{cr.x + cr.w, cr.y + cr.h})) UI::SetTooltip("Objects in the scene");
        }
        Divider();
        // ---- the list: only the rows in view are asked for and drawn, each at its own y, so the scroll bar covers them all
        UI::BeginChild("##hiertree", UI::Vec2{0.0f, 0.0f}, UI::Child_None);
        // The cursor starts scrolled with the content: the visible top is where it would be unscrolled, the view what is left of it.
        const float scroll = UI::GetScrollY();
        const UI::Vec2 at = UI::GetCursorScreenPos();
        const UI::Vec2 origin{at.x, at.y + scroll};
        const float listW = UI::GetContentRegionAvail().x, viewH = std::max(0.0f, UI::GetContentRegionAvail().y - scroll);
        res.list = UI::Rect{origin.x, origin.y, listW, viewH};
        const float rowH = fs + 8.0f;          // a text line and the spacing (Dear ImGui's GetTextLineHeightWithSpacing)
        res.rowHeight = rowH;
        if (m.revealRow >= 0 && !m.searching) {
            const float want = HierRevealScroll(m.revealRow, rowH, scroll, viewH);
            if (want != scroll) UI::SetScrollY(want);   // takes effect next frame (the child measures this frame's content first)
        }
        res.scrollY = st.scrollY = scroll;
        const UI::DrawList& dl = UI::GetWindowDrawList();
        OkPen pen{dl};
        const float spaceW = UI::CalcTextWidth("     ");
        const UI::Vec2 bang = UI::CalcTextSize("!");
        const bool shift = UI::IsKeyDown(UI::KeyId_Shift), ctrl = UI::IsKeyDown(UI::KeyId_Ctrl);
        const UI::Vec2 mouse = UI::GetMousePos();
        const UI::DragPayload* live = UI::GetDragDropPayload();
        const bool liveRow = live && std::strcmp(live->type, "GO_PTR") == 0 && live->size == (int)sizeof(std::uint64_t);
        std::uint64_t liveId = 0;
        if (liveRow) std::memcpy(&liveId, live->data, sizeof liveId);
        // An outside drag (a file from the Project panel): handed to OkayUI's drag and drop for this frame.
        if (!m.externalDragType.empty() && !(live && !UI::IsDragDropExternal()))
            UI::SetExternalDragDrop(m.externalDragType.c_str(), m.externalDragText.c_str(), (int)m.externalDragText.size() + 1, m.externalDragReleased);
        bool overRow = false;
        HierRow row;
        const HierClipRange clip = HierVisibleRange(m.rowCount, rowH, scroll, viewH);
        if (m.rowCount == 0 && !m.searching) {   // the editor's empty-panel note: a diamond, the title, the hint wrapped, all centred
            res.empty = true;
            float ty = origin.y + viewH * 0.34f;
            if (viewH >= 40.0f) {
                const float gs = fs * 1.6f, cx = origin.x + listW * 0.5f, cy = ty + gs * 0.6f;
                const UI::Color32 gc = UI::RGBA(102, 107, 120);   // (0.40, 0.42, 0.47)
                dl.AddQuad(UI::Vec2{cx, cy - gs * 0.55f}, UI::Vec2{cx + gs * 0.55f, cy}, UI::Vec2{cx, cy + gs * 0.55f}, UI::Vec2{cx - gs * 0.55f, cy}, gc, 2.0f);
                dl.AddCircleFilled(UI::Vec2{cx, cy}, gs * 0.14f, gc);
                ty += gs * 1.3f + 16.0f;
                auto centred = [&](const char* t, UI::Color32 c) {
                    for (const std::string& l : HierWrapWords(t, std::max(40.0f, listW - 16.0f), [](const std::string& x) { return UI::CalcTextWidth(x.c_str()); })) {
                        dl.AddText(UI::Vec2{origin.x + std::max(0.0f, (listW - UI::CalcTextWidth(l.c_str())) * 0.5f), ty + kRowTextDy}, c, l.c_str());
                        ty += fs + 8.0f;
                    }
                };
                centred(kHierEmptyTitle, UI::RGBA(168, 173, 184));   // (0.66, 0.68, 0.72)
                ty += 8.0f;
                centred(kHierEmptyHint, UI::RGBA(117, 120, 130));    // (0.46, 0.47, 0.51)
            } else dl.AddText(UI::Vec2{origin.x, origin.y + kRowTextDy}, UI::GetColorU32(UI::Col_TextDisabled), kHierEmptyTitle);
        }
        for (int ri = clip.first; ri < clip.last; ++ri) {
            row = HierRow();
            if (m.fillRow) m.fillRow(ri, row);
            ++st.rowsFilled;
            const float ry = origin.y + ri * rowH - scroll;
            if (row.header) {   // a combined scene's group title, a rule under it
                const UI::Color32 hc = row.headerMain ? kHeaderMain : kHeaderOther;
                dl.AddText(UI::Vec2{origin.x + spaceW * 0.8f, ry}, hc, row.name.c_str());
                const float cy = ry + fs * 0.52f;
                dl.AddTriangleFilled(UI::Vec2{origin.x + 2.0f, cy - fs * 0.14f}, UI::Vec2{origin.x + 2.0f + fs * 0.42f, cy - fs * 0.14f},
                                     UI::Vec2{origin.x + 2.0f + fs * 0.21f, cy + fs * 0.22f}, hc);
                dl.AddLine(UI::Vec2{origin.x, ry + fs + 1.0f}, UI::Vec2{origin.x + listW, ry + fs + 1.0f}, UI::GetColorU32(UI::Col_Separator), 1.0f);
                continue;
            }
            const float rx = m.searching ? origin.x : origin.x + row.depth * 22.0f;   // Dear ImGui's IndentSpacing in the editor
            const float rw = origin.x + listW - rx;
            UI::PushID(ri);
            UI::SetCursorScreenPos(UI::Vec2{m.searching ? rx - 6.0f : rx, m.searching ? ry - 4.0f : ry});
            UI::InvisibleButton("##row", m.searching ? rw + 6.0f : rw, m.searching ? fs + 8.0f : fs);
            const UI::Rect rr = UI::GetItemRect();
            const bool hovered = UI::IsItemHovered();
            const bool held = UI::IsItemActive();
            overRow = overRow || hovered;
            res.rowIndex.push_back(ri); res.rows.push_back(rr);
            const float x1 = rr.x + rr.w;
            const bool inEye = !m.searching && mouse.x >= x1 - kHierEyeW && mouse.x <= x1;
            const bool inLock = !m.searching && mouse.x >= x1 - kHierEyeW * 2.0f && mouse.x < x1 - kHierEyeW;
            const bool inArrow = !m.searching && row.childCount > 0 && mouse.x >= rx && mouse.x < rx + fs + 24.0f;
            res.arrows.push_back(row.childCount > 0 && !m.searching ? UI::Rect{rx, rr.y, fs + 24.0f, rr.h} : UI::Rect{});
            res.eyes.push_back(m.searching ? UI::Rect{} : UI::Rect{x1 - kHierEyeW, rr.y, kHierEyeW, rr.h});
            res.locks.push_back(m.searching ? UI::Rect{} : UI::Rect{x1 - kHierEyeW * 2.0f, rr.y, kHierEyeW, rr.h});
            // the band, the highlight, the arrow
            if (!m.searching && (row.objectIndex & 1) != 0)
                dl.AddRectFilled(UI::Vec2{origin.x, ry}, UI::Vec2{origin.x + listW, ry + rowH}, UI::RGBA(255, 255, 255, 5));
            if (hovered || row.selected)
                dl.AddRectFilled(UI::Vec2{rr.x, rr.y}, UI::Vec2{x1, rr.y + rr.h}, (held && hovered) ? kHeaderActive : hovered ? kHeaderHovered : kHeader);
            const UI::Color32 tc = row.dim ? kDimText : row.prefabBlue ? kPrefabText : UI::GetColorU32(UI::Col_Text);
            float iconX, textX;
            if (m.searching) { iconX = rx + 2.0f; textX = rx; }
            else {
                if (row.childCount > 0) {   // Dear ImGui's arrow: 0.7 of a text line, 12 pixels in
                    const float r = fs * 0.40f * 0.70f, acx = rx + 12.0f + fs * 0.5f, acy = ry + fs * 0.15f + fs * 0.5f * 0.70f;
                    if (row.open) dl.AddTriangleFilled(UI::Vec2{acx, acy + 0.750f * r}, UI::Vec2{acx - 0.866f * r, acy - 0.750f * r}, UI::Vec2{acx + 0.866f * r, acy - 0.750f * r}, tc);
                    else dl.AddTriangleFilled(UI::Vec2{acx + 0.750f * r, acy}, UI::Vec2{acx - 0.750f * r, acy + 0.866f * r}, UI::Vec2{acx - 0.750f * r, acy - 0.866f * r}, tc);
                }
                iconX = rx + fs + 24.0f; textX = iconX;
            }
            // the name, cut before the eye, the padlock and the problems marker
            const std::string tail = m.searching ? std::string() : HierTail(row.childCount, row.active);
            const float room = m.searching ? rw - spaceW - (row.diag ? 28.0f : 6.0f)
                                           : rw - (fs + 24.0f) - spaceW - UI::CalcTextWidth(tail.c_str()) - kHierEyeW * 2.0f - 8.0f - (row.diag ? 22.0f : 0.0f);
            const bool renaming = st.renameId != 0 && st.renameId == row.id;
            const std::string shown = HierEllipsize(row.name, room, [](const std::string& t) { return UI::CalcTextWidth(t.c_str()); });
            if (!renaming) {
                const std::string label = shown + tail;
                dl.AddText(UI::Vec2{textX + spaceW, ry + kRowTextDy}, tc, label.c_str());
            }
            if (shown != row.name && hovered && !renaming) UI::SetTooltip("%s", row.name.c_str());
            {   // the type icon and the prefab mark
                const float iy = m.searching ? ry : rr.y + (rr.h - fs) * 0.5f;
                HierDrawIcon(pen, row.icon, iconX, iy, fs);
                float hv[4];
                HierDrawPrefabMark(pen, row, iconX, iy, fs, hv);
                if (hv[2] > hv[0] && !row.prefabTip.empty() && UI::IsMouseHoveringRect(UI::Vec2{hv[0], hv[1]}, UI::Vec2{hv[2], hv[3]})) UI::SetTooltip("%s", row.prefabTip.c_str());
            }
            const float ty0 = m.searching ? ry : rr.y, ty1 = m.searching ? ry + fs : rr.y + rr.h;
            if (m.searching) {   // the parents after the name, dim, so same-named results can be told apart
                if (!row.path.empty()) {
                    const float nx = rx + UI::CalcTextWidth(("     " + shown).c_str()) + 12.0f;
                    dl.AddText(UI::Vec2{nx, ry + 1.0f + kRowTextDy}, UI::GetColorU32(UI::Col_TextDisabled, 0.75f), ("in " + row.path).c_str());
                }
                HierDrawBadges(pen, row.badges, x1, ty0, ty1, 0.0f);
                HierDrawDiag(pen, row.diag, x1 - 48.0f, (ty0 + ty1) * 0.5f, bang.x, bang.y);
            } else {
                if (row.selected) dl.AddRectFilled(UI::Vec2{rr.x, rr.y}, UI::Vec2{rr.x + 3.0f, rr.y + rr.h}, kAccent);   // the accent bar
                const float labelEnd = rx + fs + 24.0f + UI::CalcTextWidth(("     " + shown + tail).c_str()) + 8.0f;
                HierDrawBadges(pen, row.badges, x1 - kHierEyeW * 2.0f, ty0, ty1, labelEnd);
                HierDrawDiag(pen, row.diag, x1 - 48.0f, (ty0 + ty1) * 0.5f, bang.x, bang.y);
                HierDrawEye(pen, row.active, x1, ty0, ty1);
                HierDrawLock(pen, row.locked, hovered, x1, ty0, ty1);
                if (hovered && inEye) UI::SetTooltip("%s", HierEyeTip(row.active));
                if (hovered && inLock) UI::SetTooltip("%s", HierLockTip(row.locked));
            }
            if (!row.diagTip.empty() && row.diag > 0 && UI::IsMouseHoveringRect(UI::Vec2{x1 - 57.0f, ty0}, UI::Vec2{x1 - 39.0f, ty1})) UI::SetTooltip("%s", row.diagTip.c_str());
            // ---- the mouse on the row
            if (renaming) {   // the name becomes a field: Enter or leaving it renames, Escape gives up
                UI::SetCursorScreenPos(UI::Vec2{textX + spaceW - 4.0f, ry - 2.0f});
                FieldColors fc_;
                UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{4.0f, 1.0f});
                UI::SetNextItemWidth(std::max(60.0f, x1 - kHierEyeW * 2.0f - (textX + spaceW - 4.0f)));
                if (st.renameAge == 0) UI::SetKeyboardFocusHere();
                const bool enter = UI::InputText("##rename", st.renameBuf, (int)sizeof st.renameBuf, UI::InputText_EnterReturnsTrue);
                UI::PopStyleVar();
                res.renameField = UI::GetItemRect();
                const bool escape = UI::GetFrameInput().escape;
                if (enter || (st.renameAge > 0 && !UI::IsItemFocused() && !escape)) {
                    if (st.renameBuf[0]) { in.renameId = row.id; in.renameTo = st.renameBuf; }
                    st.renameId = 0;
                } else if (escape) st.renameId = 0;
                else ++st.renameAge;
            } else if (UI::IsItemClicked(0)) {
                if (m.searching) {
                    if (ctrl) in.toggle = row.id; else in.select = row.id;
                } else {
                    if (inArrow) { in.openId = row.id; in.open = row.open ? 0 : 1; }
                    if (inEye) in.toggleActive = row.id;
                    else if (inLock) in.toggleLock = row.id;
                    else if (shift && m.activeRow >= 0) in.rangeTo = row.id;
                    else if (ctrl) in.toggle = row.id;
                    else if (row.selected && m.selectedCount > 1) { st.pressedMulti = row.id; st.pressX = mouse.x; st.pressY = mouse.y; }   // maybe a drag of them all: decided on release
                    else in.select = row.id;
                }
            }
            if (!renaming && st.pressedMulti == row.id && UI::IsMouseReleased(0)) {   // a plain click on one of several selected rows
                const float dx = mouse.x - st.pressX, dy = mouse.y - st.pressY;
                if (hovered && dx * dx + dy * dy < 16.0f && st.dragging == 0) in.select = row.id;   // Dear ImGui's 4-pixel drag threshold
                st.pressedMulti = 0;
            }
            if (!renaming && hovered && UI::IsMouseDoubleClicked(0)) {
                if (m.searching) in.frame = row.id;
                else if (!inEye && !inLock && !inArrow) {
                    st.renameId = row.id; st.renameAge = 0; in.renameBegan = row.id;
                    std::snprintf(st.renameBuf, sizeof st.renameBuf, "%s", row.name.c_str());
                }
            }
            if (!renaming && hovered && UI::IsMouseReleased(1) && st.menuKind == HierMenuNone) {   // its right-click menu (it selects the row first)
                if (!row.selected) in.select = row.id;
                st.menuKind = HierMenuRow; st.menuRow = row.id; openMenu = true;
            }
            // ---- drag and drop: a row is a source; a row takes a dropped row (above, inside or below) or a file
            if (!renaming && !inEye && !inLock && UI::BeginDragDropSource()) {
                UI::SetDragDropPayload("GO_PTR", &row.id, (int)sizeof row.id);
                UI::SetDragDropPreview(HierDragLabel(row.name, row.selected, m.selectedCount).c_str());
                st.dragging = row.id;
                UI::EndDragDropSource();
            }
            if (!m.searching && UI::BeginDragDropTarget(UI::DragDrop_NoDefaultRect)) {
                const float t = (mouse.y - rr.y) / std::max(1.0f, rr.h);
                const int where = HierDropZone(t);
                const bool bad = liveRow && liveId == row.id;   // onto itself (the host refuses a drop into its own children too)
                const UI::Color32 col = bad ? UI::RGBA(235, 90, 80) : kAccent;
                if (where != HierDropInside) {   // a line above or below the row, or the row outlined
                    const float ly = where == HierDropBefore ? rr.y : rr.y + rr.h;
                    dl.AddLine(UI::Vec2{rr.x, ly}, UI::Vec2{x1, ly}, col, 2.5f);
                    dl.AddCircleFilled(UI::Vec2{rr.x + 4.0f, ly}, 3.5f, col);
                } else {
                    dl.AddRectFilled(UI::Vec2{rr.x, rr.y}, UI::Vec2{x1, rr.y + rr.h}, (col & 0x00FFFFFFu) | 0x30000000u, 3.0f);
                    dl.AddRect(UI::Vec2{rr.x, rr.y}, UI::Vec2{x1, rr.y + rr.h}, col, 3.0f, 2.0f);
                }
                if (const UI::DragPayload* p = UI::AcceptDragDropPayload("GO_PTR")) {
                    if (p->size == (int)sizeof(std::uint64_t)) {
                        std::uint64_t dragged = 0; std::memcpy(&dragged, p->data, sizeof dragged);
                        in.dragged = dragged; in.dropTarget = row.id; in.dropWhere = where;
                    }
                }
                if (const UI::DragPayload* p = UI::AcceptDragDropPayload("ASSET_PATH")) {
                    in.dropAsset.assign((const char*)p->data, strnlen((const char*)p->data, (std::size_t)std::max(0, p->size)));
                    in.dropTarget = row.id; in.dropWhere = where;
                }
                UI::EndDragDropTarget();
            }
            UI::PopID();
        }
        // below the last row, drawn or not: the footer of the search, or the space that takes drops to the top level
        UI::SetCursorScreenPos(UI::Vec2{origin.x, origin.y + m.rowCount * rowH - scroll});
        if (m.searching) {
            const std::string foot = HierSearchFooter(m.rowCount, st.filter);
            UI::TextDisabled(foot.c_str());
        } else {
            const float avail = origin.y + viewH - (origin.y + m.rowCount * rowH - scroll);
            UI::InvisibleButton("##hierempty", std::max(1.0f, listW), std::max(24.0f, avail - 24.0f));   // (Dear ImGui: the room less twice the spacing, and its own)
            res.emptyArea = UI::GetItemRect();
            if (UI::BeginDragDropTarget(UI::DragDrop_NoDefaultRect)) {
                const UI::Rect er = res.emptyArea;
                dl.AddRect(UI::Vec2{er.x, er.y}, UI::Vec2{er.x + er.w, er.y + er.h}, UI::ColorMulAlpha(kAccent, 0.8f), 4.0f, 2.0f);
                if (const UI::DragPayload* p = UI::AcceptDragDropPayload("GO_PTR")) {
                    if (p->size == (int)sizeof(std::uint64_t)) { std::memcpy(&in.dragged, p->data, sizeof in.dragged); in.dropTarget = 0; in.dropWhere = HierDropRoot; }
                }
                if (const UI::DragPayload* p = UI::AcceptDragDropPayload("ASSET_PATH")) {
                    in.dropAsset.assign((const char*)p->data, strnlen((const char*)p->data, (std::size_t)std::max(0, p->size)));
                    in.dropTarget = 0; in.dropWhere = HierDropRoot;
                }
                UI::EndDragDropTarget();
            }
            // right-click the empty space: the create menu
            if (!overRow && st.menuKind == HierMenuNone && UI::IsMouseReleased(1) &&
                UI::IsMouseHoveringRect(UI::Vec2{origin.x, origin.y}, UI::Vec2{origin.x + listW, origin.y + viewH})) {
                st.menuKind = HierMenuEmpty; st.menuRow = 0; openMenu = true;
            }
        }
        if (!UI::IsMouseDown(0)) st.dragging = 0;
        if (st.pressedMulti && UI::IsMouseReleased(0)) st.pressedMulti = 0;   // the pressed row scrolled away
        UI::EndChild();
        // ---- the keys, while the panel has the keyboard and no field does
        if (m.focused && st.renameId == 0 && !UI::WantsTextInput() && m.rowCount > 0) {
            if (!m.searching && m.activeRow >= 0) {
                auto objectAt = [&](int from, int step) -> std::uint64_t {   // the next object row (headers skipped)
                    for (int i = from + step; i >= 0 && i < m.rowCount; i += step) {
                        HierRow r; if (m.fillRow) m.fillRow(i, r); ++st.rowsFilled;
                        if (!r.header) return r.id;
                    }
                    return 0;
                };
                if (UI::IsKeyPressed(UI::KeyId_Down)) { if (std::uint64_t id = objectAt(m.activeRow, 1)) in.select = id; }
                else if (UI::IsKeyPressed(UI::KeyId_Up)) { if (std::uint64_t id = objectAt(m.activeRow, -1)) in.select = id; }
                HierRow cur; if (m.fillRow) m.fillRow(m.activeRow, cur); ++st.rowsFilled;
                if (UI::IsKeyPressed(UI::KeyId_Right) && cur.childCount > 0) { in.openId = cur.id; in.open = 1; }
                else if (UI::IsKeyPressed(UI::KeyId_Left)) { in.openId = cur.id; in.open = 0; }
                if (UI::IsKeyPressed(UI::KeyId_F2, false)) {
                    st.renameId = cur.id; st.renameAge = 0; in.renameBegan = cur.id;
                    std::snprintf(st.renameBuf, sizeof st.renameBuf, "%s", cur.name.c_str());
                }
            }
            if (UI::IsKeyDown(UI::KeyId_Ctrl) && UI::IsKeyPressed(UI::KeyId_A, false)) in.selectAll = true;
        }
        // ---- the open menu (one popup: the row menu, the empty-space menu, the [+] menu or the options)
        if (openMenu) UI::OpenPopup("##hiermenu");
        if (st.menuKind != HierMenuNone) {
            std::vector<PanelMenuEntry> entries;
            if (st.menuKind == HierMenuOptions) entries = HierOptionsMenu(m.sortAZ);
            else if (m.buildMenu) m.buildMenu(st.menuKind, st.menuRow, entries);
            MenuLook look_(PanelMenuWidth(entries));
            UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 8.0f});
            UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{16.0f, 12.0f});   // Dear ImGui's popup padding (the rows reach half the spacing past it)
            if (UI::BeginPopup("##hiermenu")) {
                std::string chosen;
                DrawPanelMenu(entries, chosen, &res.menuIds, &res.menuRows);
                res.menu = UI::Rect{};
                if (!res.menuRows.empty()) {
                    const UI::Rect a = res.menuRows.front(), b = res.menuRows.back();
                    res.menu = UI::Rect{a.x, a.y, a.w, b.y + b.h - a.y};
                }
                if (!chosen.empty()) { in.menuId = chosen; in.menuKind = st.menuKind; in.menuRow = st.menuRow; }
                UI::EndPopup();
            }
            UI::PopStyleVar(2);
            if (!UI::IsPopupOpen("##hiermenu")) { st.menuKind = HierMenuNone; st.menuRow = 0; }
        }
    }
    UI::End();
    return res;
}


// ---- Inspector, stage 1 (docs/own_ui_toolkit.md section 14) -----------------------------------------------------------------------
// Written from what the editor's Inspector draws and does (editor/main.cpp DrawInspector, CompHeader, DragVec3Axis, the Mesh
// Renderer's fields) and Dear ImGui's documented collapsing header, drag field and colour field behaviour as the editor uses them;
// sizes measured on the comparison pictures. The rules and the small drawings come from shared/InspectorPanelModel.hpp.

namespace {

const UI::Color32 kCardBg = UI::RGBA(41, 43, 51);                // ImVec4(0.16, 0.17, 0.20): the object header card
const UI::Color32 kOffText = UI::RGBA(153, 158, 168);            // ImVec4(0.60, 0.62, 0.66): a component that is off
const UI::Color32 kOffBar = UI::RGBA(115, 120, 130, 204);        // ImVec4(0.45, 0.47, 0.51, 0.8): its left bar
const UI::Color32 kPrefabBar = UI::RGBA(77, 158, 255);           // ImVec4(0.30, 0.62, 1.0): changed from its prefab
const unsigned char kMixed[3] = {242, 199, 102};                 // ImVec4(0.95, 0.78, 0.40): values that differ
const unsigned char kManaged[3] = {242, 191, 89};                // ImVec4(0.95, 0.75, 0.35)

// Dear ImGui's SmallButton with the host's frame padding: one text line tall, the label FramePadding.x in on each side.
// How far a CollapsingHeader's bar reaches into the window's padding on each side (Dear ImGui: half the window padding, less a pixel).
constexpr float kCollapsingBleed = 8.0f;   // measured on the editor's Inspector: 8 pixels on each side

bool InspSmallButton(const char* label) {
    UI::CenterNextOnLine();
    std::string vis(label);
    const std::size_t hh = vis.find("##");
    if (hh != std::string::npos) vis.resize(hh);
    return UI::Button(label, UI::Vec2{UI::CalcTextWidth(vis.c_str()) + 2.0f * g_metrics.framePadding.x, UI::GetFontSize()});
}

// Dear ImGui's SameLine-or-wrap of the editor (SameLineOrWrap): stay on the line when `nextW` still fits before `right`.
bool InspSameLineOrWrap(const UI::Rect& last, float right, float nextW) {
    if (right - (last.x + last.w + g_metrics.itemSpacing.x) < nextW) return false;
    UI::SameLine();
    return true;
}

// Dear ImGui's tree arrow at (x, y) in a `size` square: down when open, right when closed.
void InspArrow(const UI::DrawList& dl, float x, float y, float size, bool open, UI::Color32 col) {
    const float r = size * 0.40f, cx = x + size * 0.5f, cy = y + size * 0.5f;
    if (open) dl.AddTriangleFilled(UI::Vec2{cx, cy + 0.75f * r}, UI::Vec2{cx + 0.866f * r, cy - 0.75f * r}, UI::Vec2{cx - 0.866f * r, cy - 0.75f * r}, col);
    else dl.AddTriangleFilled(UI::Vec2{cx + 0.75f * r, cy}, UI::Vec2{cx - 0.75f * r, cy + 0.866f * r}, UI::Vec2{cx - 0.75f * r, cy - 0.866f * r}, col);
}

// The frame of Dear ImGui's collapsing header at (x, y), w by h: the header colours (held, hovered, or plain), the frame rounding.
UI::Rect InspHeaderFrameAt(float x, float y, float w, float h, bool hovered, bool held) {
    UI::GetWindowDrawList().AddRectFilled(UI::Vec2{x, y}, UI::Vec2{x + w, y + h}, held ? kHeaderActive : hovered ? kHeaderHovered : kHeader, 6.0f);
    return UI::Rect{x, y, w, h};
}

// A row of an open list (Dear ImGui's Selectable in a combo): one text line and the item spacing tall as one flat row, the selected one
// in the header colour; a click closes the list.
bool ListRow(const char* label, bool selected) {
    UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{g_metrics.itemSpacing.x, 0.0f});
    const bool hit = UI::Selectable(label, selected, UI::Selectable_Header | UI::Selectable_ClosePopup, UI::Vec2{0.0f, UI::GetFontSize() + g_metrics.itemSpacing.y});
    UI::PopStyleVar();
    return hit;
}

// The rows of an open list, at most 8 at a time as Dear ImGui's combo shows them (a taller list scrolls): `row(k)` draws row k. The
// wheel over the list moves it by whole rows, the list opens with the current row in view, and a thin bar at its right shows where it
// is. Rows out of view are not drawn at all (they cannot take a click).
constexpr int kListRows = 8;
template <class Row>
void ListWindow(const char* key, int count, int current, Row row) {
    struct Slot { unsigned id = 0; int first = 0; int frame = -10; };
    static Slot slots[8];
    const unsigned id = UI::GetID(key);
    const int frame = UI::GetFrameCount();
    Slot* sl = nullptr;
    for (Slot& s : slots) if (s.id == id) sl = &s;
    if (!sl) { sl = &slots[0]; for (Slot& s : slots) if (s.frame < sl->frame) sl = &s; *sl = Slot(); sl->id = id; }
    const int maxFirst = std::max(0, count - kListRows);
    if (sl->frame < frame - 1) sl->first = std::clamp(current - kListRows / 2 + 1, 0, maxFirst);   // just opened: the current row in view
    sl->frame = frame;
    const float rowH = UI::GetFontSize() + g_metrics.itemSpacing.y;
    const UI::Vec2 p0 = UI::GetCursorScreenPos();
    const float listW = UI::GetContentRegionAvail().x;
    const int shown = std::min(count, kListRows);
    const float wheel = UI::GetFrameInput().wheel;
    if (count > kListRows && wheel != 0.0f && UI::IsMouseHoveringRect(p0, UI::Vec2{p0.x + listW, p0.y + rowH * (float)shown}))
        sl->first = std::clamp(sl->first - (int)std::lround(wheel * 3.0f), 0, maxFirst);
    for (int k = sl->first; k < sl->first + shown && k < count; ++k) row(k);
    if (count > kListRows) {   // the bar: a faint track and a grab as long as the share in view
        const UI::DrawList& dl = UI::GetWindowDrawList();
        const float h = rowH * (float)shown, x = p0.x + listW + 2.0f;
        const float gh = std::max(16.0f, h * (float)shown / (float)count), gy = p0.y + (h - gh) * (float)sl->first / (float)maxFirst;
        dl.AddRectFilled(UI::Vec2{x, gy}, UI::Vec2{x + 4.0f, gy + gh}, UI::RGBA(255, 255, 255, 61), 2.0f);
    }
}

// The editor's colour field (Dear ImGui's ColorEdit3/4 with its default look): the channels as whole numbers 0 to 255 in drag cells,
// each with its colour marker at the left edge, a swatch a frame tall that opens a picker, the label after it. True on a change.
bool InspColorEdit(const char* label, float* c, int n, UI::Rect* rowRect = nullptr) {
    UI::PushID(label);
    const float fh = UI::GetFrameHeight(), inner = g_metrics.innerSpacing.x;
    const float full = g_metrics.itemWidth > 0.0f ? g_metrics.itemWidth : 200.0f;
    const float inputs = std::max(full - (fh + inner), 1.0f);
    const float items = inputs - inner * (float)(n - 1);
    bool changed = false;
    const UI::Vec2 p0 = UI::GetCursorScreenPos();
    float prev = 0.0f;
    for (int i = 0; i < n; ++i) {
        if (i > 0) UI::SameLine(inner);
        const float next = std::floor(items * (float)(i + 1) / (float)n);
        UI::SetNextItemWidth(std::max(next - prev, 1.0f));
        prev = next;
        UI::PushID(i);
        // While a cell is dragged its unrounded value is kept (Dear ImGui's drag keeps the part of a step the mouse moved), so a slow drag
        // adds up exactly; the colour gets the whole number.
        static unsigned s_dragId = 0;
        static float s_dragVal = 0.0f;
        const unsigned cid = UI::GetID("##ch");
        float b = s_dragId == cid ? s_dragVal : (float)ColorByte(c[i]);
        if (UI::DragFloat("##ch", &b, 1.0f, 0.0f, 255.0f, "%3.0f")) {
            const float nb = std::round(std::min(255.0f, std::max(0.0f, b)));
            if (nb != (float)ColorByte(c[i])) { c[i] = nb / 255.0f; changed = true; }
        }
        if (UI::IsItemActive()) { s_dragId = cid; s_dragVal = b; }
        else if (s_dragId == cid) s_dragId = 0;
        const UI::Rect r = UI::GetItemRect();
        {   // Dear ImGui's marker: the left 3 pixels of the cell's rounded frame, in the channel's colour
            const UI::DrawList& dl = UI::GetWindowDrawList();
            dl.PushClipRect(UI::Vec2{r.x, r.y}, UI::Vec2{r.x + 3.0f, r.y + r.h});
            dl.AddRectFilled(UI::Vec2{r.x, r.y}, UI::Vec2{r.x + r.w, r.y + r.h}, kColorMarker[i], 6.0f);
            dl.PopClipRect();
        }
        UI::PopID();
    }
    UI::SameLine(inner);
    float rgba[4] = {c[0], c[1], c[2], n == 4 ? c[3] : 1.0f};
    if (UI::ColorButton("##swatch", rgba, UI::ColorEdit_NoAlpha, fh)) UI::OpenPopup("##picker");
    if (n == 4 && rgba[3] < 1.0f) {   // Dear ImGui's swatch of a see-through colour: the colour over a light and dark grey check, cells a third of it
        const UI::Rect b = UI::GetItemRect();
        const UI::DrawList& dl = UI::GetWindowDrawList();
        auto blend = [&](float g) {
            auto ch = [&](float v) { return ColorByte(g + (v - g) * rgba[3]); };
            return UI::RGBA(ch(rgba[0]), ch(rgba[1]), ch(rgba[2]), 255);
        };
        const UI::Color32 light = blend(204.0f / 255.0f), dark = blend(128.0f / 255.0f);
        const float step = std::min(b.w, b.h) / 2.99f;
        dl.PushClipRect(UI::Vec2{b.x, b.y}, UI::Vec2{b.x + b.w, b.y + b.h});
        dl.AddRectFilled(UI::Vec2{b.x, b.y}, UI::Vec2{b.x + b.w, b.y + b.h}, light, 6.0f);   // the corner cells are light: the frame's rounding
        int yi = 0;
        for (float yy = b.y; yy < b.y + b.h; yy += step, ++yi) {
            int xi = 0;
            for (float xx = b.x; xx < b.x + b.w; xx += step, ++xi)
                if ((xi + yi) & 1) dl.AddRectFilled(UI::Vec2{xx, yy}, UI::Vec2{std::min(xx + step, b.x + b.w), std::min(yy + step, b.y + b.h)}, dark, 0.0f);
        }
        dl.PopClipRect();
    }
    {
        MenuLook look_(220.0f);
        if (UI::BeginPopup("##picker")) {
            float tmp[4] = {c[0], c[1], c[2], n == 4 ? c[3] : 1.0f};
            if (UI::ColorPicker4("##pick", tmp, (n == 4 ? 0 : UI::ColorEdit_NoAlpha) | UI::ColorEdit_NoLabel)) {
                for (int i = 0; i < n; ++i) c[i] = tmp[i];
                changed = true;
            }
            UI::EndPopup();
        }
    }
    std::string vis(label);
    const std::size_t hh = vis.find("##");
    if (hh != std::string::npos) vis.resize(hh);
    if (!vis.empty()) { UI::SameLine(inner); UI::Text(vis.c_str()); }
    const UI::Vec2 p1 = UI::GetItemRectMax();
    if (rowRect) *rowRect = UI::Rect{p0.x, p0.y, p1.x - p0.x, fh};
    UI::PopID();
    return changed;
}

// One of the Transform's three rows (the editor's DragVec3Axis): three drag cells sharing the field width, each with its axis
// colour bar, the label after them. `activated`: a cell became active this frame.
bool InspVec3Row(const char* label, float v[3], float speed, const char* fmt, bool* activated, UI::Rect cells[3], UI::Rect* labelRect = nullptr) {
    UI::PushID(label);
    const float inner = g_metrics.innerSpacing.x;
    const float full = g_metrics.itemWidth > 0.0f ? g_metrics.itemWidth : 200.0f;
    float widths[3];
    InspCellWidths(full, 3, inner, widths);
    bool changed = false;
    for (int i = 0; i < 3; ++i) {
        if (i > 0) UI::SameLine(inner);
        UI::PushID(i);
        const float room = widths[i] - g_metrics.framePadding.x * 2.0f - 4.0f;
        const char* f = InspAxisFormat(fmt, v[i], room, [](const char* t) { return UI::CalcTextWidth(t); });
        UI::SetNextItemWidth(widths[i]);
        if (UI::DragFloat("##c", &v[i], speed, 0.0f, 0.0f, f)) changed = true;
        if (activated && UI::IsItemActivated()) *activated = true;
        const UI::Rect r = UI::GetItemRect();
        if (cells) cells[i] = r;
        UI::GetWindowDrawList().AddRectFilled(UI::Vec2{r.x, r.y}, UI::Vec2{r.x + 3.0f, r.y + r.h}, kInspAxisCol[i], 2.0f);
        UI::PopID();
    }
    UI::SameLine(inner);
    UI::Text(label);
    if (labelRect) *labelRect = UI::GetItemRect();
    UI::PopID();
    return changed;
}

// stage 2 (below): a field's registry tooltip and its prefab mark and menu
void FieldExtras(const char* label, const UI::Rect& row, float regionX, bool overridden, bool tipLookup, const InspFieldHooks* hooks,
                 std::string& field, std::string& chosen, UI::Rect* popup);

} // namespace

// ---- the object header card ------------------------------------------------------------------------------------------------------

InspectorHeaderResult DrawInspectorHeader(InspectorHeaderModel& m, InspectorHeaderState& st, float x, float y, float w, float h) {
    InspectorHeaderResult res;
    InspectorHeaderIntent& in = res.intent;
    st.newTag[sizeof(st.newTag) - 1] = '\0';
    st.renameBuf[sizeof(st.renameBuf) - 1] = '\0';
    UI::BareNextWindow();
    if (UI::Begin("InspectorHeader##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        const float top = UI::GetCursorScreenPos().y;
        const float fh = UI::GetFrameHeight();
        const float sx = g_metrics.itemSpacing.x, ix = g_metrics.innerSpacing.x;
        UI::PushStyleColor(UI::Col_ChildBg, kCardBg);
        UI::PushStyleVar(UI::StyleVar_WindowPadding, UI::Vec2{14.0f, 10.0f});
        UI::PushStyleVar(UI::StyleVar_ChildRounding, 8.0f);
        UI::BeginChild("##objheader", UI::Vec2{0.0f, 0.0f}, UI::Child_Borders | UI::Child_AutoResizeY);
        UI::PopStyleVar();   // the rounding: the outline is drawn; frames inside round as usual
        const float left = UI::GetCursorScreenPos().x;
        const float right = left + UI::GetContentRegionAvail().x;
        // row 1: the active box, the type icon, the name, Static and Lock
        bool act = m.active;
        if (UI::Checkbox("##active", &act)) in.toggleActive = true;
        res.activeBox = UI::GetItemRect();
        if (UI::IsItemHovered()) UI::SetTooltip("%s", kInspActiveTip);
        UI::SameLine();
        {
            const UI::Vec2 ip = UI::GetCursorScreenPos();
            const float isz = 17.0f;
            OkPen pen{UI::GetWindowDrawList()};
            HierDrawIcon(pen, m.icon, ip.x, ip.y + (fh - isz) * 0.5f, isz);
            UI::Dummy(isz + 2.0f, fh);
            UI::SameLine();
        }
        const float staticW = UI::CalcTextWidth("Static") + UI::CalcTextWidth("Lock") + fh * 2.0f + (ix + sx) * 2.0f;
        const float avail = right - UI::GetCursorScreenPos().x;
        const bool narrow = avail - staticW < 120.0f;
        UI::SetNextItemWidth(narrow ? avail : avail - staticW);
        if (m.name && m.nameSize > 0) {
            m.name[m.nameSize - 1] = '\0';
            if (UI::InputText("##name", m.name, (int)std::min<std::size_t>(m.nameSize, 1u << 20))) in.nameChanged = true;
        } else {
            static char none[2] = "";
            UI::InputText("##name", none, 2);
        }
        res.nameField = UI::GetItemRect();
        if (!narrow) UI::SameLine();
        bool stat = m.isStatic;
        if (UI::Checkbox("Static", &stat)) in.toggleStatic = true;
        res.staticBox = UI::GetItemRect();
        if (UI::IsItemHovered()) UI::SetTooltip("%s", kInspStaticTip);
        UI::SameLine();
        bool lk = m.locked;
        if (UI::Checkbox("Lock", &lk)) in.toggleLock = true;
        res.lockBox = UI::GetItemRect();
        if (UI::IsItemHovered()) UI::SetTooltip("%s", kInspLockTip);

        // row 2: Tag, Layer, Save as Prefab
        const float tagLblW = UI::CalcTextWidth("Layer") + ix;
        const float comboW = std::clamp(right - left - tagLblW, 90.0f, 150.0f);
        const std::string curTag = m.tag.empty() ? std::string("Untagged") : m.tag;
        UI::Rect last;
        {
            std::vector<std::string> builtin, custom;   // asked for only while the list shows
            UI::SetNextItemWidth(WithLabel(comboW, "Tag"));
            MenuLook look_(0.0f);
            // the list is as wide as the box, or as its add field and button when they need more (Dear ImGui fits it to its content)
            UI::PushStyleVar(UI::StyleVar_PopupWidth, std::max(comboW, 120.0f + sx + UI::CalcTextWidth("Add") + 2.0f * g_metrics.framePadding.x + 20.0f));
            const bool open = UI::BeginCombo("Tag", curTag.c_str());
            res.tagCombo = UI::Rect{left, UI::GetItemRectMin().y, comboW, fh};
            if (open) {
                if (st.closeTagList) { UI::CloseCurrentPopup(); st.closeTagList = false; }
                if (m.tags) m.tags(builtin, custom);
                const UI::Vec2 p0 = UI::GetCursorScreenPos();
                auto row = [&](const std::string& t, const char* label) {
                    const bool hit = ListRow(label, curTag == t);
                    res.tagRows.push_back(UI::GetItemRect());
                    return hit;
                };
                if (row("Untagged", "Untagged")) { in.setTag = true; in.tag.clear(); }
                for (const std::string& t : builtin)
                    if (row(t, t.c_str())) { in.setTag = true; in.tag = t; }
                if (!custom.empty()) {
                    Divider();
                    for (std::size_t i = 0; i < custom.size(); ++i) {
                        const std::string& t = custom[i];
                        UI::PushID((int)i);
                        if (row(t, t.c_str())) { in.setTag = true; in.tag = t; }
                        // the right-click menu as wide as its field and button (Dear ImGui fits a popup to its content)
                        UI::PushStyleVar(UI::StyleVar_PopupWidth, 170.0f + sx + UI::CalcTextWidth("Rename") + 2.0f * g_metrics.framePadding.x + 20.0f);
                        const bool ctx = UI::BeginPopupContextItem("##tagctx");
                        UI::PopStyleVar();
                        if (ctx) {
                            if (st.renameKind != 1 || st.renameFrom != t) {
                                st.renameKind = 1; st.renameFrom = t; st.renameError.clear();
                                CopyToBuf(st.renameBuf, sizeof(st.renameBuf), t);
                            }
                            if (st.closeRename) { UI::CloseCurrentPopup(); st.closeRename = false; st.renameKind = 0; }
                            UI::SetNextItemWidth(170.0f);
                            const bool enter = UI::InputText("##tagrename", st.renameBuf, (int)sizeof(st.renameBuf), UI::InputText_EnterReturnsTrue);
                            res.renameField = UI::GetItemRect();
                            UI::SameLine();
                            const bool click = UI::Button("Rename");
                            res.renameButton = UI::GetItemRect();
                            if (enter || click) {
                                if (t == st.renameBuf) { UI::CloseCurrentPopup(); st.renameKind = 0; }
                                else { in.renameTag = true; in.tagFrom = t; in.tagTo = st.renameBuf; }
                            }
                            if (!st.renameError.empty()) UI::TextColored(kBad[0], kBad[1], kBad[2], st.renameError.c_str());
                            Divider();
                            if (MenuRow("Remove tag from list")) { in.removeTag = true; in.removeTagName = t; st.renameKind = 0; }
                            res.removeTagRow = UI::GetItemRect();
                            UI::EndPopup();
                        }
                        UI::PopID();
                    }
                }
                Divider();
                UI::SetNextItemWidth(120.0f);
                const bool entered = UI::InputTextWithHint("##newtag", "new tag...", st.newTag, (int)sizeof(st.newTag), UI::InputText_EnterReturnsTrue);
                res.newTagField = UI::GetItemRect();
                UI::SameLine();
                const bool clicked = UI::Button("Add");
                res.addTagButton = UI::GetItemRect();
                if ((entered || clicked) && st.newTag[0]) { in.addTag = true; in.newTag = st.newTag; }
                if (!st.tagError.empty()) UI::TextColored(kBad[0], kBad[1], kBad[2], st.tagError.c_str());
                res.popup = UI::Rect{p0.x - 10.0f, p0.y - 8.0f, std::max(comboW, 120.0f + sx + UI::CalcTextWidth("Add") + 2.0f * g_metrics.framePadding.x + 20.0f),
                                     UI::GetCursorScreenPos().y - p0.y + 16.0f};
                UI::EndCombo();
            } else st.tagError.clear();
            UI::PopStyleVar();
            last = UI::Rect{left, res.tagCombo.y, comboW + tagLblW, fh};
        }
        InspSameLineOrWrap(last, right, comboW + tagLblW);
        {
            UI::SetNextItemWidth(WithLabel(comboW, "Layer"));
            MenuLook look_(0.0f);
            UI::PushStyleVar(UI::StyleVar_PopupWidth, std::max(comboW, UI::CalcTextWidth(kInspLayerListNote) + 20.0f));
            const float lx = UI::GetCursorScreenPos().x;
            const bool open = UI::BeginCombo("Layer", m.layerName.c_str());
            res.layerCombo = UI::Rect{lx, UI::GetItemRectMin().y, comboW, fh};
            if (open) {
                const UI::Vec2 p0 = UI::GetCursorScreenPos();
                UI::TextDisabled(kInspLayerListNote);
                res.layerRows.assign(32, UI::Rect{});   // rows out of view stay zero size
                ListWindow("##layerrows", 32, m.layer, [&](int i) {
                    const std::string nm = m.layerNameOf ? m.layerNameOf(i) : std::to_string(i);
                    UI::PushID(i);
                    if (ListRow(nm.c_str(), m.layer == i)) in.setLayer = i;
                    res.layerRows[(std::size_t)i] = UI::GetItemRect();
                    UI::PushStyleVar(UI::StyleVar_PopupWidth, 170.0f + 20.0f);
                    const bool ctx = UI::BeginPopupContextItem("##layerctx");
                    UI::PopStyleVar();
                    if (ctx) {
                        std::string text, hint;
                        if (m.layerRenameInfo) m.layerRenameInfo(i, text, hint);
                        if (st.renameKind != 2 || st.renameLayer != i) {
                            st.renameKind = 2; st.renameLayer = i; st.renameError.clear();
                            CopyToBuf(st.renameBuf, sizeof(st.renameBuf), text);
                        }
                        if (st.closeRename) { UI::CloseCurrentPopup(); st.closeRename = false; st.renameKind = 0; }
                        char head[32];
                        std::snprintf(head, sizeof head, "Layer %d name", i);
                        UI::TextDisabled(head);
                        UI::SetNextItemWidth(170.0f);
                        const bool enter = UI::InputTextWithHint("##layername", hint.c_str(), st.renameBuf, (int)sizeof(st.renameBuf), UI::InputText_EnterReturnsTrue);
                        res.renameField = UI::GetItemRect();
                        if (enter) { in.renameLayer = true; in.layerIndex = i; in.layerTo = st.renameBuf; }
                        if (InspSmallButton("Reset to default")) { in.resetLayer = true; in.layerIndex = i; }
                        res.renameButton = UI::GetItemRect();
                        if (!st.renameError.empty()) UI::TextColored(kBad[0], kBad[1], kBad[2], st.renameError.c_str());
                        UI::EndPopup();
                    }
                    UI::PopID();
                });
                res.popup = UI::Rect{p0.x - 10.0f, p0.y - 8.0f, std::max(comboW, UI::CalcTextWidth(kInspLayerListNote) + 20.0f),
                                     UI::GetCursorScreenPos().y - p0.y + 16.0f};
                UI::EndCombo();
            }
            UI::PopStyleVar();
            last = UI::Rect{lx, res.layerCombo.y, comboW + tagLblW, fh};
        }
        {
            const float btnW = UI::CalcTextWidth("Save as Prefab") + g_metrics.framePadding.x * 2.0f;
            const bool same = InspSameLineOrWrap(last, right, btnW);
            const float cx = same ? last.x + last.w + sx : left;
            const float room = right - cx;
            if (room > btnW) UI::SetCursorPosX(UI::GetCursorPosX() + (room - btnW));
            if (UI::Button("Save as Prefab", UI::Vec2{btnW, fh})) in.savePrefab = true;
            res.savePrefab = UI::GetItemRect();
        }
        UI::EndChild();
        res.card = UI::GetItemRect();
        UI::PopStyleVar();
        UI::PopStyleColor();
        res.contentHeight = ContentHeight(top);
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

// ---- a component's header bar ------------------------------------------------------------------------------------------------------

CompHeaderResult DrawComponentHeader(const CompHeaderModel& m, const char* id, float bleed, float x, float y, float w, float h) {
    CompHeaderResult res;
    CompHeaderIntent& in = res.intent;
    UI::BareNextWindow();
    // the window reaches `bleed` past the region on both sides (the bar is drawn there); the content starts `bleed` in
    if (UI::Begin(id ? id : "CompHeader##okayui", x - bleed, y, w + 2.0f * bleed, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        if (bleed > 0.0f) UI::Indent(bleed);
        const float top = UI::GetCursorScreenPos().y;
        const float fh = UI::GetFrameHeight(), fs = UI::GetFontSize(), px = g_metrics.framePadding.x;
        if (m.sectionGap > 0.0f) UI::Dummy(0.0f, m.sectionGap);
        const UI::Vec2 p0 = UI::GetCursorScreenPos();
        const float barW = w;
        const float arrowW = fs + px * 2.0f;
        const float cbSize = fs + 4.0f;                                   // the enable box: a frame with 2-pixel padding
        const UI::Rect cb{p0.x + arrowW, p0.y + (fh - cbSize) * 0.5f, cbSize, cbSize};
        const UI::Rect menuBtn{p0.x + barW - fh, p0.y, fh, fh};
        const float glyphX = cb.x + cbSize + 6.0f;
        // the bar's own clicks: the parts left of the box and between the mark and the "..." button fold it
        // While its menu is open nothing on the bar is hovered (Dear ImGui blocks the windows under an open popup): no highlight, no tooltip.
        const bool menuOpen = UI::IsPopupOpen("##compctx");
        const bool overBar = !menuOpen && UI::IsMouseHoveringRect(UI::Vec2{p0.x - bleed, p0.y}, UI::Vec2{p0.x + barW + bleed, p0.y + fh});
        const bool overCb = UI::IsMouseHoveringRect(UI::Vec2{cb.x, cb.y}, UI::Vec2{cb.x + cb.w, cb.y + cb.h});
        const bool overMenu = !menuOpen && UI::IsMouseHoveringRect(UI::Vec2{menuBtn.x, menuBtn.y}, UI::Vec2{menuBtn.x + menuBtn.w, menuBtn.y + menuBtn.h});
        UI::SetCursorScreenPos(UI::Vec2{p0.x - bleed, p0.y});
        UI::PushID("##bar");
        const bool barClick = UI::InvisibleButton("##hdr", barW + 2.0f * bleed, fh);
        const bool barHeld = UI::IsItemActive();
        UI::PopID();
        if (barClick && !overCb && !overMenu) in.toggleOpen = true;
        const bool hot = overBar && !overCb && !overMenu;
        res.bar = InspHeaderFrameAt(p0.x - bleed, p0.y, barW + 2.0f * bleed, fh, hot, barHeld && !overCb && !overMenu);
        const UI::DrawList& dl = UI::GetWindowDrawList();
        if (hot && !m.tip.empty()) UI::SetTooltip("%s", m.tip.c_str());
        else if (hot && !m.managed.empty()) UI::SetTooltip("Managed by Character Model: you normally don't edit this.\n%s", m.managed.c_str());
        else if (overBar && m.prefabChanged && !overCb && !overMenu) UI::SetTooltip("Changed from the prefab");
        // the bar down the left edge (the family colour; blue when changed from the prefab; grey while off)
        {
            UI::Color32 bar = m.prefabChanged ? kPrefabBar : UI::ColorMulAlpha(m.color | 0xff000000u, 0.95f);
            if (!m.enabled) bar = kOffBar;
            dl.AddRectFilled(UI::Vec2{res.bar.x, p0.y}, UI::Vec2{res.bar.x + 3.0f, p0.y + fh}, bar, 1.5f);
        }
        InspArrow(dl, p0.x + px, p0.y + g_metrics.framePadding.y, fs, m.open, m.enabled ? UI::GetColorU32(UI::Col_Text) : kOffText);   // grey with the name while off
        res.arrow = UI::Rect{p0.x, p0.y, arrowW, fh};
        // the name, after the room the box and the mark take (Dear ImGui's header text starts after the arrow)
        {
            const float lead = fh + 4.0f + fh * 0.66f + 6.0f;
            const float spaceW = std::max(1.0f, UI::CalcTextWidth(" "));
            const float leadPx = std::ceil(lead / spaceW) * spaceW;
            const float badgeW = m.diag > 0 ? 22.0f : 0.0f;
            const float room = barW - arrowW - lead - fh - 10.0f - badgeW;
            const std::string title = CompHeaderTitle(m.label, room, [](const char* t) { return UI::CalcTextWidth(t); });
            const float tx = p0.x + fs + px * 3.0f + leadPx;
            UI::PushClipRect(p0.x - bleed, p0.y, barW + 2.0f * bleed, fh);
            dl.AddText(UI::Vec2{tx, p0.y + g_metrics.framePadding.y + kRowTextDy}, m.enabled ? UI::GetColorU32(UI::Col_Text) : kOffText, title.c_str());
            UI::PopClipRect();
        }
        // the enable box
        UI::SetCursorScreenPos(UI::Vec2{cb.x, cb.y});
        UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{2.0f, 2.0f});
        bool en = m.enabled;
        if (UI::Checkbox("##enabled", &en)) in.toggleEnabled = true;
        res.enableBox = UI::GetItemRect();
        if (!menuOpen && UI::IsItemHovered()) UI::SetTooltip("%s", m.enabled ? "Enabled (click to turn this component off)" : "Disabled (click to turn it on)");
        UI::PopStyleVar();
        // the family mark
        {
            OkPen pen{dl};
            InspDrawGlyph(pen, m.glyph, m.color, m.enabled, glyphX, p0.y, fh);
        }
        // the "..." button
        UI::SetCursorScreenPos(UI::Vec2{menuBtn.x, menuBtn.y});
        bool openMenu = UI::InvisibleButton("##compmenu", fh, fh);
        res.menuButton = menuBtn;
        {
            const bool mh = overMenu;
            if (mh) dl.AddRectFilled(UI::Vec2{menuBtn.x, menuBtn.y}, UI::Vec2{menuBtn.x + fh, menuBtn.y + fh}, UI::RGBA(255, 255, 255, 24), 4.0f);
            for (int d = -1; d <= 1; ++d)
                dl.AddCircleFilled(UI::Vec2{menuBtn.x + fh * 0.5f + (float)d * 4.5f, menuBtn.y + fh * 0.5f}, 1.6f,
                                   UI::GetColorU32(mh ? UI::Col_Text : UI::Col_TextDisabled), 8);
            if (mh) UI::SetTooltip("Component options (or right-click the header)");
        }
        if (overBar && UI::IsMouseReleased(1)) openMenu = true;
        // the problems marker, left of the "..." button
        if (m.diag > 0) {
            OkPen pen{dl};
            const float cx = p0.x + barW + bleed - fh - 18.0f, cy = p0.y + fh * 0.5f;
            HierDrawDiag(pen, m.diag, cx, cy + kRowTextDy * 0.0f, UI::CalcTextWidth("!"), fs);
            if (!m.diagTip.empty() && UI::IsMouseHoveringRect(UI::Vec2{cx - 9.0f, cy - 9.0f}, UI::Vec2{cx + 9.0f, cy + 9.0f})) UI::SetTooltip("%s", m.diagTip.c_str());
        }
        // back below the bar
        UI::SetCursorScreenPos(p0);
        UI::Dummy(barW, fh);
        // the menu
        if (openMenu) UI::OpenPopup("##compctx");
        if (UI::IsPopupOpen("##compctx")) {
            std::vector<PanelMenuEntry> entries;
            if (m.buildMenu) m.buildMenu(entries);
            MenuLook look_(PanelMenuWidth(entries));
            UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 8.0f});
            UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{16.0f, 12.0f});
            if (UI::BeginPopup("##compctx")) {
                std::string chosen;
                DrawPanelMenu(entries, chosen, &res.menuIds, &res.menuRows);
                if (!res.menuRows.empty()) {
                    const UI::Rect a = res.menuRows.front(), b = res.menuRows.back();
                    res.menu = UI::Rect{a.x - 16.0f, a.y - 12.0f, a.w + 32.0f, b.y + b.h - a.y + 24.0f};
                }
                if (!chosen.empty()) in.menuId = chosen;
                UI::EndPopup();
            }
            UI::PopStyleVar(2);
        }
        // the notes under an open bar
        if (m.open && !m.managed.empty()) {
            UI::TextColored(kManaged[0], kManaged[1], kManaged[2], "Managed by Character Model - you normally don't edit this.");
            UI::TextDisabled(m.managed.c_str());
        }
        if (m.open && !m.mixedNote.empty()) {
            UI::PushTextWrapPos(0.0f);
            UI::TextColored(kMixed[0], kMixed[1], kMixed[2], m.mixedNote.c_str());
            UI::PopTextWrapPos();
            if (UI::IsItemHovered()) UI::SetTooltip("What you change here is set on every selected object.");
        }
        res.contentHeight = ContentHeight(top);
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

// ---- Transform -----------------------------------------------------------------------------------------------------------------

TransformResult DrawTransform(TransformModel& m, float bleed, float x, float y, float w, float h) {
    TransformResult res;
    TransformIntent& in = res.intent;
    UI::BareNextWindow();
    if (UI::Begin("Transform##okayui", x - bleed, y, w + 2.0f * bleed, h)) {   // reaching `bleed` past the region, as the bar does
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        if (bleed > 0.0f) UI::Indent(bleed);
        const float top = UI::GetCursorScreenPos().y;
        const float fh = UI::GetFrameHeight(), fs = UI::GetFontSize(), px = g_metrics.framePadding.x;
        const UI::Vec2 p0 = UI::GetCursorScreenPos();
        const float barW = w;
        const float right = p0.x + barW;
        // the header: Dear ImGui's collapsing header, "Transform"
        UI::SetCursorScreenPos(UI::Vec2{p0.x - bleed, p0.y});
        const bool click = UI::InvisibleButton("##tfhdr", barW + 2.0f * bleed, fh);
        const bool held = UI::IsItemActive(), hov = UI::IsItemHovered() && !UI::IsPopupOpen("##tfctx");
        if (click) in.toggleOpen = true;
        res.header = InspHeaderFrameAt(p0.x - bleed, p0.y, barW + 2.0f * bleed, fh, hov, held);
        const UI::DrawList& dl = UI::GetWindowDrawList();
        InspArrow(dl, p0.x + px, p0.y + g_metrics.framePadding.y, fs, m.open, UI::GetColorU32(UI::Col_Text));
        dl.AddText(UI::Vec2{p0.x + fs + px * 3.0f, p0.y + g_metrics.framePadding.y + kRowTextDy}, UI::GetColorU32(UI::Col_Text), "Transform");
        // right-click the header: copy the whole transform, paste it onto another object
        {
            MenuLook look_(MenuWidth({"Copy Transform Values", "Paste Transform Values"}));
            UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 8.0f});
            UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{16.0f, 12.0f});
            if (UI::BeginPopupContextItem("##tfctx")) {
                if (UI::MenuItem("Copy Transform Values")) in.copy = true;
                res.menuCopy = UI::GetItemRect();
                if (UI::MenuItem("Paste Transform Values", nullptr, false, m.pasteValid)) in.paste = true;
                res.menuPaste = UI::GetItemRect();
                UI::EndPopup();
            }
            UI::PopStyleVar(2);
        }
        UI::SetCursorScreenPos(p0);
        UI::Dummy(barW, fh);
        if (m.open) {
            if (m.selectedCount > 1) {
                char note[64];
                std::snprintf(note, sizeof(note), "(editing %d selected objects)", m.selectedCount);
                UI::TextDisabled(note);
            }
            bool act = false;
            if (InspVec3Row("Position", m.pos, 0.05f, "%.3f", &act, res.pos, &res.posLabel)) in.posChanged = true;
            if (InspVec3Row("Rotation", m.rot, 1.0f, "%.2f", &act, res.rot, &res.rotLabel)) in.rotChanged = true;
            const float before[3] = {m.scale[0], m.scale[1], m.scale[2]};
            if (m.isCamera) UI::BeginDisabled(true);
            if (InspVec3Row("Scale", m.scale, 0.05f, "%.3f", &act, res.scale, &res.scaleLabel)) {
                if (m.uniform) TransformUniformScale(before, m.scale);
                in.scaleChanged = true;
            }
            if (m.isCamera) { UI::EndDisabled(); UI::TextDisabled("(camera scale is locked)"); }
            in.activated = act;
            {   // stage 2: each row's tooltip and prefab mark, on its label as the editor's FieldTip after DragVec3Axis
                UI::Rect pop;
                FieldExtras("Position", res.posLabel, x, m.posOverridden, true, m.hooks, in.prefabField, in.prefabChosen, &pop);
                FieldExtras("Rotation", res.rotLabel, x, m.rotOverridden, true, m.hooks, in.prefabField, in.prefabChosen, &pop);
                FieldExtras("Scale", res.scaleLabel, x, m.scaleOverridden, true, m.hooks, in.prefabField, in.prefabChosen, &pop);
                res.fieldMenu = pop;
            }
            // the quick resets
            if (InspSmallButton("Reset Pos")) in.resetPos = true;
            UI::Rect last = res.resetPos = UI::GetItemRect();
            auto btnW = [&](const char* l) { return UI::CalcTextWidth(l) + px * 2.0f; };
            InspSameLineOrWrap(last, right, btnW("Reset Rot"));
            if (InspSmallButton("Reset Rot")) in.resetRot = true;
            last = res.resetRot = UI::GetItemRect();
            if (!m.isCamera) {
                InspSameLineOrWrap(last, right, btnW("Reset Scale"));
                if (InspSmallButton("Reset Scale")) in.resetScale = true;
                last = res.resetScale = UI::GetItemRect();
                InspSameLineOrWrap(last, right, fh + UI::CalcTextWidth("Uniform") + 8.0f);
                bool uni = m.uniform;
                if (UI::Checkbox("Uniform##scl", &uni)) in.toggleUniform = true;
                res.uniform = UI::GetItemRect();
                if (UI::IsItemHovered()) UI::SetTooltip("Link the scale axes: dragging X/Y/Z scales all three by the same ratio.");
            }
        }
        res.contentHeight = ContentHeight(top);
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

// ---- Mesh Renderer (the shading part) ------------------------------------------------------------------------------------------------

MeshRendererResult DrawMeshRenderer(MeshRendererModel& m, float x, float y, float w, float h) {
    MeshRendererResult res;
    MeshRendererIntent& in = res.intent;
    UI::BareNextWindow();
    // stage 2: the window reaches 6 pixels past the region on both sides, where a field's prefab bar is drawn
    if (UI::Begin("MeshRenderer##okayui", x - 6.0f, y, w + 12.0f, h) && m.v) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        UI::Indent(6.0f);
        const float top = UI::GetCursorScreenPos().y;
        const float itemW = g_metrics.itemWidth > 0.0f ? g_metrics.itemWidth : DefaultItemWidth(w);
        MeshRendererValues& v = *m.v;
        bool& ch = in.changed;
        auto tip = [](const char* t) { if (UI::IsItemHovered()) UI::SetTooltip("%s", t); };
        // stage 2: the field's registry tooltip and prefab mark, after the fields the editor calls FieldTip on
        auto extras = [&](const char* label, const UI::Rect* rect = nullptr) {
            if (!m.hooks && !m.overridden) return;
            const UI::Rect r = rect ? *rect : UI::GetItemRect();
            UI::Rect pop;
            FieldExtras(label, r, x, m.overridden && m.overridden(label), true, m.hooks, in.prefabField, in.prefabChosen, &pop);
            if (pop.w > 0.0f) res.popup = pop;
        };
        auto slider = [&](const char* label, float* f, float lo, float hi, UI::Rect* rect = nullptr) {
            UI::SetNextItemWidth(WithLabel(itemW, label));
            if (UI::SliderFloat(label, f, lo, hi, "%.3f")) ch = true;
            if (rect) *rect = UI::GetItemRect();
        };
        auto drag = [&](const char* label, float* f, float speed, float lo, float hi, UI::Rect* rect = nullptr) {
            UI::SetNextItemWidth(WithLabel(itemW, label));
            if (UI::DragFloat(label, f, speed, lo, hi, "%.3f")) ch = true;
            if (rect) *rect = UI::GetItemRect();
        };
        auto box = [&](const char* label, bool* b, UI::Rect* rect = nullptr) {
            if (UI::Checkbox(label, b)) ch = true;
            if (rect) *rect = UI::GetItemRect();
        };
        // Dear ImGui's Combo: the box with the item's name, a list of rows one text line and the spacing tall, the current one marked
        auto combo = [&](const char* label, int* cur, const char* const* items, int count, UI::Rect* rect) {
            MenuLook look_(0.0f);
            UI::PushStyleVar(UI::StyleVar_PopupWidth, itemW);
            UI::SetNextItemWidth(WithLabel(itemW, label));
            const int c = *cur >= 0 && *cur < count ? *cur : -1;
            const float bx = UI::GetCursorScreenPos().x;
            int picked = -1;
            const bool open = UI::BeginCombo(label, c >= 0 ? items[c] : "");
            const float by = UI::GetItemRectMin().y;
            if (open) {
                res.listRows.assign((std::size_t)count, UI::Rect{});   // rows out of view stay zero size
                ListWindow(label, count, c, [&](int k) {
                    UI::PushID(k);
                    if (ListRow(items[k], k == c)) picked = k;
                    res.listRows[(std::size_t)k] = UI::GetItemRect();
                    UI::PopID();
                });
                UI::EndCombo();
            }
            UI::PopStyleVar();
            if (rect) *rect = UI::Rect{bx, by, itemW, UI::GetFrameHeight()};
            if (picked < 0 || picked == *cur) return false;
            *cur = picked;
            return true;
        };
        const std::string stats = MeshStatsText(m);
        if (!stats.empty()) UI::TextDisabled(stats.c_str());
        if (InspColorEdit("Color##mesh", v.color, 4, &res.color)) ch = true;
        box("Wireframe", &v.wireframe, &res.wireframe);
        if (combo("Shader##mesh", &v.shader, kMeshShaderNames, kMeshShaderCount, &res.shader)) { ch = true; in.shaderChanged = true; }
        if (v.shader == MeshShaderToon) {
            UI::SetNextItemWidth(WithLabel(itemW, "Cel Bands##mesh"));
            if (UI::SliderInt("Cel Bands##mesh", &v.toonBands, 2, 6, "%d")) ch = true;
            extras("Cel Bands##mesh");
        }
        if (v.shader == MeshShaderGradient) {
            UI::Rect cr;
            if (InspColorEdit("Gradient Top##mesh", v.gradientTop, 3, &cr)) ch = true;
            extras("Gradient Top##mesh", &cr);
            if (InspColorEdit("Gradient Bottom##mesh", v.gradientBottom, 3, &cr)) ch = true;
            extras("Gradient Bottom##mesh", &cr);
        }
        if (v.shader == MeshShaderFresnel) UI::TextDisabled(kMeshFresnelNote);
        slider("Rim##mesh", &v.rimStrength, 0.0f, 2.0f, &res.rim);
        extras("Rim##mesh");
        if (v.rimStrength > 0.0f) {
            slider("Rim Power##mesh", &v.rimPower, 1.0f, 8.0f);
            extras("Rim Power##mesh");
            UI::Rect cr;
            if (InspColorEdit("Rim Color##mesh", v.rimColor, 3, &cr)) ch = true;
            extras("Rim Color##mesh", &cr);
        }
        box("Outline##mesh", &v.outline, &res.outline);
        extras("Outline##mesh");
        if (v.outline) {
            drag("Outline Width##mesh", &v.outlineWidth, 0.002f, 0.0f, 1.0f);
            extras("Outline Width##mesh");
            UI::Rect cr;
            if (InspColorEdit("Outline Color##mesh", v.outlineColor, 3, &cr)) ch = true;
            extras("Outline Color##mesh", &cr);
        }
        box("Cast Shadows##mesh", &v.castShadows, &res.castShadows);
        tip(kMeshCastTip);
        UI::SameLine();
        box("Receive Shadows##mesh", &v.receiveShadows, &res.receiveShadows);
        tip(kMeshReceiveTip);
        box("Ground Shadow##mesh", &v.groundShadow, &res.groundShadow);
        tip(kMeshGroundTip);
        if (v.groundShadow) {
            drag("Shadow Ground Y##mesh", &v.groundShadowY, 0.05f, -1000.0f, 1000.0f, &res.groundShadowY);
            extras("Shadow Ground Y##mesh");
            slider("Shadow Strength##mesh", &v.groundShadowStrength, 0.0f, 1.0f, &res.shadowStrength);
            extras("Shadow Strength##mesh");
        }
        if (InspColorEdit("Emissive##mesh", v.emissive, 3, &res.emissive)) ch = true;
        slider("Specular##mesh", &v.specular, 0.0f, 1.0f, &res.specular);
        extras("Specular##mesh");
        if (v.specular > 0.0f) { slider("Shininess##mesh", &v.shininess, 1.0f, 128.0f); extras("Shininess##mesh"); }
        if (combo("Primitive", &v.primitive, kMeshPrimitiveNames, kMeshPrimitiveCount, &res.primitive)) { ch = true; in.primitiveChanged = true; }
        res.contentHeight = ContentHeight(top);
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

// ---- Add Component -------------------------------------------------------------------------------------------------------------------

AddComponentResult DrawAddComponentButton(std::uint32_t accent, float x, float y, float w, float h) {
    AddComponentResult res;
    UI::BareNextWindow();
    if (UI::Begin("AddComponent##okayui", x, y, w, h)) {
        EditorRows rows_;
        const float top = UI::GetCursorScreenPos().y;
        const float avail = UI::GetContentRegionAvail().x;
        const float bw = AddComponentWidth(avail);
        if (avail > 300.0f) UI::SetCursorPosX(UI::GetCursorPosX() + (avail - 260.0f) * 0.5f);
        UI::PushStyleColor(UI::Col_Button, UI::ColorMulAlpha(accent | 0xff000000u, 0.85f));
        UI::PushStyleColor(UI::Col_ButtonHovered, accent | 0xff000000u);
        UI::PushStyleColor(UI::Col_ButtonActive, UI::ColorMulAlpha(accent | 0xff000000u, 0.75f));
        UI::PushStyleColor(UI::Col_Text, UI::RGBA(255, 255, 255));
        if (UI::Button(kAddComponentLabel, UI::Vec2{bw, 30.0f})) res.open = true;
        res.button = UI::GetItemRect();
        UI::PopStyleColor(4);
        res.contentHeight = ContentHeight(top);
    }
    UI::End();
    return res;
}


// ---- Inspector, stage 2: a component body as field rows (docs/own_ui_toolkit.md section 15) ---------------------------------------
// Written from what the editor's component bodies draw with Dear ImGui (editor/main.cpp DrawInspector: SectionHeader, the drag,
// slider, combo, colour and text fields with their labels after them, the small buttons, AssetField, TexFieldThumb, TreeNode; editor/
// InspectorPanel.cpp PropertyField, PrefabFieldMark) and Dear ImGui's documented widget behaviour, measured on the comparison pictures.

namespace {

// The value column, or the row's own width.
// A negative width is the room left from where the row starts (Dear ImGui's SetNextItemWidth(-n): from the cursor, after SameLine and
// indents), less n.
float RowFrameW(const InspRow& r, float itemW, float regionW, float startX = 0.0f) {
    if (r.width > 0.0f) return r.width;
    if (r.width < 0.0f) return std::max(1.0f, regionW - startX + r.width);
    return itemW;
}

std::string Visible(const char* label) { return std::string(label ? label : "", InspVisibleLen(label)); }

// A body's window reaches this far past its region on both sides: the prefab bar of a changed field sits 5 pixels left of the region.
constexpr float kMarkBleed = 9.0f;   // 6 was enough for the prefab bars; a collapsing header reaches 8 (stage 8)
// On the right the window reaches further, for a line that does not wrap and runs into the Inspector's padding (the host offers up to 2
// pixels short of the padding's end, 14 at the editor's 16; stage 9: the last "." of "Methods: ... SetGodMode." was cut at 9).
constexpr float kRightBleed = 14.0f;

// n drag cells sharing the value column (Dear ImGui's DragFloat2..4 and the editor's DragVec3Axis): each cell a whole number of pixels,
// the last taking the rest; with axisBars the axis colour down each cell's left edge and fewer decimals in a narrow cell; a part that
// differs on the selected objects shows "-". The label after them.
bool DragCells(InspRow& r, float frameW, UI::Rect* rowRect) {
    const int n = std::clamp((int)r.n, 1, 4);
    const float inner = g_metrics.innerSpacing.x;
    float widths[4];
    InspCellWidths(frameW, n, inner, widths);
    const char* fmt = r.fmt ? r.fmt : "%.3f";
    float* v = r.F();
    bool changed = false;
    UI::PushID(r.label);
    for (int i = 0; i < n; ++i) {
        if (i > 0) UI::SameLine(inner);
        UI::PushID(i);
        const bool mx = (r.mixedAxes >> i) & 1u || (n == 1 && r.mixed);
        const char* f = fmt;
        char tagged[24];
        static const char* const kTags[4] = {"X", "Y", "Z", "W"};
        if (r.axisTags) { std::snprintf(tagged, sizeof tagged, mx ? "%s -" : "%s %s", kTags[i], fmt); f = tagged; }
        else if (mx) f = "-";
        else if (r.axisBars) f = InspAxisFormat(fmt, v[i], widths[i] - g_metrics.framePadding.x * 2.0f - 4.0f, [](const char* t) { return UI::CalcTextWidth(t); });
        UI::SetNextItemWidth(widths[i]);
        if (UI::DragFloat("##c", &v[i], r.speed, r.lo, r.hi, f, r.logarithmic ? UI::Slider_Logarithmic : 0)) { changed = true; r.axes |= 1u << i; }
        if (UI::IsItemActivated()) r.activated = true;
        if (i == 0 && rowRect) *rowRect = UI::GetItemRect();
        if (r.axisBars && i < 3) {
            const UI::Rect c = UI::GetItemRect();
            UI::GetWindowDrawList().AddRectFilled(UI::Vec2{c.x, c.y}, UI::Vec2{c.x + 3.0f, c.y + c.h}, kInspAxisCol[i], 2.0f);
        }
        UI::PopID();
    }
    UI::PopID();
    const std::string vis = Visible(r.label);
    if (!vis.empty()) { UI::SameLine(inner); UI::Text(vis.c_str()); }
    if (rowRect) {   // from the first cell to the label: the row as one item (Dear ImGui's DragFloatN is one)
        const UI::Vec2 mx = UI::GetItemRectMax();
        rowRect->w = std::max(rowRect->w, mx.x - rowRect->x);
    }
    return changed;
}

// A scrolling window of fixed-height rows inside a popup: `shown` rows at a time, the wheel moves it, `first` kept per popup.
template <class Row>
void ScrollRows(const char* key, int count, int shown, float rowH, int startAt, Row row) {
    struct Slot { unsigned id = 0; int first = 0; int frame = -10; };
    static Slot slots[4];
    const unsigned id = UI::GetID(key);
    const int frame = UI::GetFrameCount();
    Slot* sl = nullptr;
    for (Slot& s : slots) if (s.id == id) sl = &s;
    if (!sl) { sl = &slots[0]; for (Slot& s : slots) if (s.frame < sl->frame) sl = &s; *sl = Slot(); sl->id = id; }
    const int maxFirst = std::max(0, count - shown);
    if (sl->frame < frame - 1) sl->first = std::clamp(startAt, 0, maxFirst);
    sl->frame = frame;
    const UI::Vec2 p0 = UI::GetCursorScreenPos();
    const float listW = UI::GetContentRegionAvail().x;
    const float wheel = UI::GetFrameInput().wheel;
    if (count > shown && wheel != 0.0f && UI::IsMouseHoveringRect(p0, UI::Vec2{p0.x + listW, p0.y + rowH * (float)shown}))
        sl->first = std::clamp(sl->first - (int)std::lround(wheel * 3.0f), 0, maxFirst);
    for (int k = sl->first; k < sl->first + shown && k < count; ++k) row(k);
    if (count > shown) {
        const float h = rowH * (float)std::min(shown, count), x = p0.x + listW + 2.0f;
        const float gh = std::max(16.0f, h * (float)shown / (float)count), gy = p0.y + (h - gh) * (float)sl->first / (float)std::max(1, maxFirst);
        UI::GetWindowDrawList().AddRectFilled(UI::Vec2{x, gy}, UI::Vec2{x + 4.0f, gy + gh}, UI::RGBA(255, 255, 255, 61), 2.0f);
    }
}

// What an open picker lists, fetched from the host once when it opens (a project's files are walked once, not every frame).
struct PickCache {
    unsigned id = 0;
    int frame = -10;
    std::vector<std::string> values, names;
    std::vector<void*> thumbs;
    char filter[96] = {};
};
PickCache& Picks() { static PickCache c; return c; }

// After a field whose row is `row`: the registry's tooltip while it is hovered (tipLookup), and for a field changed from the prefab the
// editor's PrefabFieldMark: a 3-pixel blue bar 5 pixels left of the region, and on a right-click the menu (Overridden from, the
// differences, Revert Property, Apply Property to Prefab) asked of the hooks while it is open. A chosen row goes to field / chosen.
void FieldExtras(const char* label, const UI::Rect& row, float regionX, bool overridden, bool tipLookup, const InspFieldHooks* hooks,
                 std::string& field, std::string& chosen, UI::Rect* popup) {
    const bool over = row.w > 0.0f && UI::IsMouseHoveringRect(UI::Vec2{row.x, row.y}, UI::Vec2{row.x + row.w, row.y + row.h});
    UI::PushID(label);
    // no tooltip while the field's own menu is open (Dear ImGui shows none over an open popup; seen in the real editor)
    if (tipLookup && over && hooks && hooks->tipFor && !UI::IsPopupOpen("##prefabprop")) {
        const std::string t = hooks->tipFor(label);
        if (!t.empty()) UI::SetTooltip("%s", t.c_str());
    }
    if (!overridden) { UI::PopID(); return; }
    UI::GetWindowDrawList().AddRectFilled(UI::Vec2{regionX - 5.0f, row.y}, UI::Vec2{regionX - 2.0f, row.y + row.h}, kInspPrefabMark, 1.5f);
    if (over && UI::IsMouseClicked(1)) UI::OpenPopup("##prefabprop");
    if (UI::IsPopupOpen("##prefabprop")) {
        std::vector<PanelMenuEntry> entries;
        if (hooks && hooks->prefabMenu) hooks->prefabMenu(label, entries);
        MenuLook look_(PanelMenuWidth(entries));
        UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 8.0f});
        UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{16.0f, 12.0f});
        if (UI::BeginPopup("##prefabprop")) {
            std::string pick;
            std::vector<std::string> ids; std::vector<UI::Rect> mrows;
            const UI::Vec2 mp = UI::GetCursorScreenPos();
            DrawPanelMenu(entries, pick, &ids, &mrows);
            const UI::Vec2 me = UI::GetCursorScreenPos();
            if (popup) *popup = UI::Rect{mp.x - 16.0f, mp.y - 12.0f, PanelMenuWidth(entries) + 32.0f, me.y - mp.y + 24.0f};
            if (!pick.empty()) { field = label; chosen = pick; }
            UI::EndPopup();
        }
        UI::PopStyleVar(2);
    }
    UI::PopID();
}

// Text with line breaks, as Dear ImGui's Text draws it: one item, each line one text line (the font size) below the one before.
void LinesText(const char* t, UI::Color32 col) {
    const UI::Vec2 p = UI::GetCursorScreenPos();
    const float fs = UI::GetFontSize();
    std::vector<std::string> lines;
    for (const char* a = t;;) {
        const char* e = std::strchr(a, '\n');
        lines.emplace_back(a, e ? (std::size_t)(e - a) : std::strlen(a));
        if (!e) break;
        a = e + 1;
    }
    float wmax = 1.0f;
    for (const std::string& l : lines) wmax = std::max(wmax, UI::CalcTextWidth(l.c_str()));
    UI::Dummy(wmax, fs * (float)lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) UI::GetWindowDrawList().AddText(UI::Vec2{p.x, p.y + fs * (float)i}, col, lines[i].c_str());
}

// The small cross of a clear button in a frame-tall square (the editor's ClearButton).
void DrawCross(const UI::Rect& b) {
    const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f, r = b.h * 0.17f;
    const UI::Color32 col = UI::GetColorU32(UI::Col_Text);
    const UI::DrawList& dl = UI::GetWindowDrawList();
    dl.AddLine(UI::Vec2{cx - r, cy - r}, UI::Vec2{cx + r, cy + r}, col, 1.5f);
    dl.AddLine(UI::Vec2{cx - r, cy + r}, UI::Vec2{cx + r, cy - r}, col, 1.5f);
}

// Wrapped text ends at the region's right edge, as Dear ImGui's PushTextWrapPos(0) ends it at the window's content edge: the body's window
// reaches past the region by kMarkBleed (for the prefab bars), so wrapping at the window's edge ran 6 pixels wider than Dear ImGui and a
// line broke later (seen in the real editor: the walk-speed hint took two lines where Dear ImGui took three).
void WrapAtRegion(float right) { UI::PushTextWrapPos(std::max(1.0f, right - UI::GetCursorScreenPos().x)); }

// The editor's SectionHeader inside an open list (the UI Button's Function list: "Native (gameplay)", "Script"): Spacing, the accent
// tick, the heading face, a rule across the list.
void PopupHeading(const char* text) {
    UI::Dummy(1.0f, 0.0f);
    UI::PushTextScale(22.0f / 18.0f);
    UI::PushStyleVar(UI::StyleVar_TextRowHeight, UI::GetFontSize());
    const UI::Vec2 p = UI::GetCursorScreenPos();
    const float th = UI::GetFontSize();
    UI::GetWindowDrawList().AddRectFilled(UI::Vec2{p.x, p.y + 1.0f}, UI::Vec2{p.x + 3.0f, p.y + th}, kAccent, 1.5f);
    UI::Indent(9.0f);
    UI::TextColored(230, 235, 247, text);
    UI::Unindent(9.0f);
    UI::PopStyleVar();
    UI::PopTextScale();
    Divider();
}

} // namespace

FieldListResult DrawFieldList(InspFieldList& list, const char* id, float x, float y, float w, float h) {
    FieldListResult res;
    res.rows.assign(list.rows.size(), UI::Rect{});
    UI::BareNextWindow();
    // the window reaches kMarkBleed past the region on the left, where a field's prefab bar is drawn (the content starts that far in), and
    // kRightBleed on the right
    if (UI::Begin(id, x - kMarkBleed, y, w + kMarkBleed + kRightBleed, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        UI::Indent(kMarkBleed);
        const float top = UI::GetCursorScreenPos().y;
        const float regionX = UI::GetCursorScreenPos().x;
        const float itemW0 = g_metrics.itemWidth > 0.0f ? g_metrics.itemWidth : DefaultItemWidth(w);
        const float fs = UI::GetFontSize(), fh = UI::GetFrameHeight();
        int indent = 0;             // fold-outs we are inside of
        int skipDepth = 0;          // > 0: inside a closed fold-out (its rows are not drawn)
        static std::vector<std::pair<unsigned, bool>> s_trees;   // fold-out open states
        auto treeOpen = [&](unsigned tid, bool def) -> bool& {
            for (auto& t : s_trees) if (t.first == tid) return t.second;
            s_trees.emplace_back(tid, def);
            return s_trees.back().second;
        };
        auto setPopup = [&](const UI::Rect& r) { if (r.w > 0.0f) res.popup = r; };
        // a closed fold-out's line: the rows put on it (the Modifiers' "...") and the rows marked keepClosed right after it (the
        // Modifiers' cost, stage 9) are still drawn
        bool closedLead = false;
        for (std::size_t k = 0; k < list.rows.size(); ++k) {
            InspRow& r = list.rows[k];
            if (skipDepth == 1 && closedLead && (r.sameLine || r.keepClosed)) {
            } else if (skipDepth > 0) {
                closedLead = false;
                if (r.kind == InspRowTree) ++skipDepth;
                else if (r.kind == InspRowTreeEnd) --skipDepth;
                continue;
            }
            const float itemW = std::max(1.0f, itemW0 - (float)indent * g_metrics.indentSpacing);
            if (r.sameLine) {
                const float gap = r.sameLineGap >= 0.0f ? r.sameLineGap : g_metrics.itemSpacing.x;
                bool fits = true;
                if (r.sameLineWrap) {   // the editor's SameLineOrWrap: only when this row's item fits before the region's right edge
                    const std::string vis = Visible(r.label);
                    // in whole pixels, as Dear ImGui lays items out (a label measured 29.48 against Dear ImGui's 29 wrapped Stats' Max,
                    // stage 7)
                    const float tw = std::round(UI::CalcTextWidth(vis.c_str()));
                    float nextW = tw + 2.0f * g_metrics.framePadding.x;                        // a button
                    if (r.kind == InspRowCheck) nextW = fh + (vis.empty() ? 0.0f : g_metrics.innerSpacing.x + tw);
                    else if (r.kind == InspRowLabel || r.kind == InspRowNote) nextW = tw;
                    else if (r.width > 0.0f) nextW = r.width + (vis.empty() ? 0.0f : g_metrics.innerSpacing.x + tw);   // a field of its own width, its label
                    const UI::Rect pr = UI::GetItemRect();
                    fits = regionX + w - (std::round(pr.x + pr.w) + gap) >= nextW;
                }
                if (fits && r.alignRight) {   // a small button ending at the region's right edge (stage 8: the Modifiers' "...")
                    const UI::Rect pr = UI::GetItemRect();
                    const float bw = UI::CalcTextWidth(Visible(r.label).c_str()) + 2.0f * g_metrics.framePadding.x;
                    UI::SameLine(std::max(0.0f, regionX + w - bw - (pr.x + pr.w)));
                } else if (fits) {
                    if (r.sameLineAt >= 0.0f) {
                        const UI::Rect pr = UI::GetItemRect();
                        UI::SameLine(std::max(0.0f, regionX + r.sameLineAt - (pr.x + pr.w)));
                    } else UI::SameLine(gap);
                }
            }
            if (r.disabled) UI::BeginDisabled(true);
            // A fold-out's state is kept by its label alone (as Dear ImGui keeps the editor's, whose id does not depend on the component):
            // rows coming and going above it, and an undo that rebuilds the component, keep it open.
            unsigned treeKey = 0u;
            if (r.kind == InspRowTree) {   // a "###id" part alone keys it (a count in the shown text must not fold it again)
                const char* key = std::strstr(r.label, "###");
                treeKey = 2166136261u;
                for (const char* q = key ? key : r.label; *q; ++q) treeKey = (treeKey ^ (unsigned char)*q) * 16777619u;
            }
            UI::PushID((int)k);
            float frameW = RowFrameW(r, itemW, w, UI::GetCursorScreenPos().x - regionX);
            // a field of the usual width put on a line after something else ends where the value column ends, as Dear ImGui's relative
            // item width does (stage 9: Builder Mode's Grid after "Snap to grid" ran across the label column)
            if (r.sameLine && r.width == 0.0f && r.kind != InspRowCheck && r.kind != InspRowSmallButton && r.kind != InspRowButton &&
                r.kind != InspRowLabel && r.kind != InspRowNote && r.kind != InspRowTree && r.kind != InspRowSwatch)
                frameW = std::max(1.0f, itemW0 - (UI::GetCursorScreenPos().x - regionX));
            bool hoverTip = true;   // the row's tooltip on hover (an asset field and an image show their own)
            UI::Rect whole{};       // a row made of several items (cells, a colour field, a file field): all of it
            switch (r.kind) {
            case InspRowDrag:
                if (DragCells(r, frameW, &whole)) r.changed = true;
                break;
            case InspRowSlider:
                UI::SetNextItemWidth(WithLabel(frameW, r.label));
                if (UI::SliderFloat(r.label, r.F(), r.lo, r.hi, r.fmt ? r.fmt : "%.3f", r.logarithmic ? UI::Slider_Logarithmic : 0)) r.changed = true;
                if (UI::IsItemActivated()) r.activated = true;
                break;
            case InspRowDragInt: {   // Dear ImGui's DragInt: a framed drag cell with the number in its middle; the part of a step the
                                     // pointer moved is kept while it is held, so a slow drag adds up
                static unsigned s_intId = 0;
                static float s_intVal = 0.0f;
                const unsigned iid = UI::GetID(r.label);
                float fv = s_intId == iid ? s_intVal : (float)*r.I();
                UI::SetNextItemWidth(WithLabel(frameW, r.label));
                if (UI::DragFloat(r.label, &fv, r.speed, r.lo, r.hi, r.mixed ? "-" : "%.0f")) {
                    const int nv = (int)std::lround(fv);
                    if (nv != *r.I()) { *r.I() = nv; r.changed = true; }
                }
                if (UI::IsItemActive()) { s_intId = iid; s_intVal = fv; }
                else if (s_intId == iid) s_intId = 0;
                if (UI::IsItemActivated()) r.activated = true;
                break;
            }
            case InspRowSliderInt:
                UI::SetNextItemWidth(WithLabel(frameW, r.label));
                if (UI::SliderInt(r.label, r.I(), (int)r.lo, (int)r.hi, r.fmt ? r.fmt : "%d")) r.changed = true;
                if (UI::IsItemActivated()) r.activated = true;
                break;
            case InspRowCheck: {
                if (UI::Checkbox(r.label, r.B())) r.changed = true;
                if (r.mixed && !*r.B()) {   // Dear ImGui's mixed value: a bar across the box
                    const UI::Rect c = UI::GetItemRect();
                    const float pad = std::max(1.0f, std::floor(fh / 3.6f));
                    UI::GetWindowDrawList().AddRectFilled(UI::Vec2{c.x + pad, c.y + (fh - pad) * 0.5f}, UI::Vec2{c.x + fh - pad, c.y + (fh + pad) * 0.5f}, kAccentLit, 0.0f);
                }
                break;
            }
            case InspRowCombo: {
                MenuLook look_(0.0f);
                // the list as wide as the box, or as its widest item when that is wider (Dear ImGui fits a combo's list to its items: the
                // Modifiers' "Subdivision Surface" ran under the bar of a 160-wide list, stage 8)
                float listW = frameW;
                if (r.items && r.count <= 256)
                    for (int q = 0; q < r.count; ++q) listW = std::max(listW, UI::CalcTextWidth(r.items[q]) + 20.0f + (r.count > kListRows ? 16.0f : 0.0f));   // the list's padding, its bar
                UI::PushStyleVar(UI::StyleVar_PopupWidth, listW);
                UI::SetNextItemWidth(WithLabel(frameW, r.label));
                int* cur = r.I();
                const int c = *cur >= 0 && *cur < r.count ? *cur : -1;
                const float bx = UI::GetCursorScreenPos().x;
                int picked = -1;
                const char* preview = r.mixed ? "Mixed" : r.preview ? r.preview : (c >= 0 && r.items ? r.items[c] : "");
                // the closed text cut before the arrow with "...", and tinted when the host asks (an unknown item id)
                const std::string fitted = InspFitText(preview, frameW - fh - g_metrics.framePadding.x, [](const char* t) { return UI::CalcTextWidth(t); });
                if (r.color) UI::PushStyleColor(UI::Col_Text, r.color | 0xff000000u);
                const bool open = UI::BeginCombo(r.label, fitted.c_str());
                if (r.color) UI::PopStyleColor();
                const UI::Rect box = UI::GetItemRect();
                if (open && r.search) {   // the item list (stage 7): a search field over the rows, as the editor's ItemPicker
                    struct Find { unsigned id = 0; int frame = -10, age = 0; char text[64] = {}; };
                    static Find s_find;
                    const unsigned fid = UI::GetID(r.label);
                    const int now = UI::GetFrameCount();   // just opened (or the frame count started again): an empty search
                    const bool fresh = s_find.id != fid || s_find.frame < now - 1 || s_find.frame > now;
                    if (fresh) { s_find = Find(); s_find.id = fid; }
                    s_find.frame = UI::GetFrameCount();
                    const UI::Vec2 lp = UI::GetCursorScreenPos();
                    // the search takes the keyboard as the list opens (asked again on the next frames: the press that opened the list
                    // ends on the box, outside the field, and would otherwise take the focus away again)
                    if (s_find.age++ < 3) UI::SetKeyboardFocusHere();
                    UI::SetNextItemWidth(std::max(40.0f, frameW - 20.0f));
                    UI::InputTextWithHint("##itemfind", r.searchHint ? r.searchHint : "Search", s_find.text, (int)sizeof s_find.text);
                    std::string needle(s_find.text);
                    for (char& ch : needle) ch = (char)std::tolower((unsigned char)ch);
                    std::vector<int> hits;
                    bool exact = false;
                    for (int q = 0; q < r.count; ++q) {
                        const char* key = r.searchKeys ? r.searchKeys[q] : "";
                        if (s_find.text[0] && std::strcmp(key, s_find.text) == 0) exact = true;
                        if (!needle.empty()) {
                            std::string hay = std::string(key) + " " + r.items[q];
                            for (char& ch : hay) ch = (char)std::tolower((unsigned char)ch);
                            if (hay.find(needle) == std::string::npos) continue;
                        }
                        hits.push_back(q);
                    }
                    res.listRows.assign((std::size_t)r.count, UI::Rect{});
                    ListWindow(r.label, (int)hits.size(), -1, [&](int h) {
                        const int q = hits[(std::size_t)h];
                        UI::PushID(q);
                        if (ListRow(r.items[q], !r.mixed && q == c)) picked = q;
                        res.listRows[(std::size_t)q] = UI::GetItemRect();
                        if (r.itemTips && r.itemTips[q] && *r.itemTips[q] && UI::IsItemHovered()) UI::SetTooltip("%s", r.itemTips[q]);
                        UI::PopID();
                    });
                    bool useTyped = false;
                    if (s_find.text[0] && !exact) {
                        const std::string use = std::string("Use \"") + s_find.text + "\"";
                        ListWindow("##itemuse", 1, -1, [&](int) {   // laid out as the rows above (the list's height counts it)
                            if (ListRow(use.c_str(), false)) useTyped = true;
                            res.listRows.push_back(UI::GetItemRect());   // after the items' rows: the typed id's
                        });
                    }
                    // the rows are laid with no spacing under them, and the list's measured height takes one spacing off its last item:
                    // an empty item puts it back, so the list's last row is inside the list (the field under it showed through, stage 7)
                    UI::Dummy(0.0f, 0.0f);
                    const UI::Vec2 le = UI::GetCursorScreenPos();
                    setPopup(UI::Rect{lp.x - 10.0f, lp.y - 8.0f, frameW, le.y - lp.y + 16.0f});
                    UI::EndCombo();
                    if (useTyped) { r.sv = s_find.text; *cur = -1; r.changed = true; }
                } else if (open && r.heads) {   // a list with the editor's section headings inside it (the UI Button's Function): every row drawn
                    res.listRows.assign((std::size_t)r.count, UI::Rect{});
                    const UI::Vec2 lp = UI::GetCursorScreenPos();
                    for (int q = 0; q < r.count; ++q) {
                        if (r.heads[q]) PopupHeading(r.heads[q]);
                        UI::PushID(q);
                        if (ListRow(r.items[q], !r.mixed && q == c)) picked = q;
                        res.listRows[(std::size_t)q] = UI::GetItemRect();
                        if (r.itemTips && r.itemTips[q] && *r.itemTips[q] && UI::IsItemHovered()) UI::SetTooltip("%s", r.itemTips[q]);
                        UI::PopID();
                    }
                    UI::Dummy(0.0f, 0.0f);   // the last row inside the list (see the search list above)
                    const UI::Vec2 le = UI::GetCursorScreenPos();
                    setPopup(UI::Rect{lp.x - 10.0f, lp.y - 8.0f, frameW, le.y - lp.y + 16.0f});
                    UI::EndCombo();
                } else if (open) {
                    res.listRows.assign((std::size_t)r.count, UI::Rect{});
                    const UI::Vec2 lp = UI::GetCursorScreenPos();
                    ListWindow(r.label, r.count, c, [&](int q) {
                        UI::PushID(q);
                        if (ListRow(r.items[q], !r.mixed && q == c)) picked = q;
                        res.listRows[(std::size_t)q] = UI::GetItemRect();
                        if (r.itemTips && r.itemTips[q] && *r.itemTips[q] && UI::IsItemHovered()) UI::SetTooltip("%s", r.itemTips[q]);
                        UI::PopID();
                    });
                    UI::Dummy(0.0f, 0.0f);   // the last row inside the list: it was cut by the list's edge ("Slider (prismatic)", stage 7)
                    setPopup(UI::Rect{lp.x - 10.0f, lp.y - 8.0f, frameW, (fs + g_metrics.itemSpacing.y) * (float)std::min(r.count, kListRows) + 16.0f});
                    UI::EndCombo();
                }
                UI::PopStyleVar();
                (void)bx; (void)box;
                if (picked >= 0 && (picked != *cur || r.mixed)) { *cur = picked; r.changed = true; }
                break;
            }
            case InspRowColor: {
                const int n = r.n == 4 ? 4 : 3;
                float* c = r.F();
                if (r.noInputs) {   // ColorEdit3 with NoInputs: the swatch (a click opens the picker) and the label
                    float rgba[4] = {c[0], c[1], c[2], n == 4 ? c[3] : 1.0f};
                    UI::PushID(r.label);
                    if (UI::ColorButton("##swatch", rgba, UI::ColorEdit_NoAlpha, fh)) UI::OpenPopup("##picker");
                    whole = UI::GetItemRect();
                    {
                        MenuLook look_(220.0f);
                        if (UI::BeginPopup("##picker")) {
                            float tmp[4] = {c[0], c[1], c[2], n == 4 ? c[3] : 1.0f};
                            if (UI::ColorPicker4("##pick", tmp, (n == 4 ? 0 : UI::ColorEdit_NoAlpha) | UI::ColorEdit_NoLabel)) {
                                for (int q = 0; q < n; ++q) { if (c[q] != tmp[q]) r.axes |= 1u << q; c[q] = tmp[q]; }
                                r.changed = true;
                            }
                            UI::EndPopup();
                        }
                    }
                    UI::PopID();
                    const std::string vis = Visible(r.label);
                    if (!vis.empty()) {   // the label on the swatch's line, where a frame's text sits (a plain Text drifted down a row of these)
                        UI::SameLine(g_metrics.innerSpacing.x);
                        UI::Dummy(UI::CalcTextWidth(vis.c_str()), fh);
                        const UI::Rect lb = UI::GetItemRect();
                        UI::GetWindowDrawList().AddText(UI::Vec2{lb.x, lb.y + g_metrics.framePadding.y}, UI::GetColorU32(UI::Col_Text), vis.c_str());
                        whole.w = lb.x + lb.w - whole.x;
                    }
                    break;
                }
                float before[4] = {c[0], c[1], c[2], n == 4 ? c[3] : 1.0f};
                const float keep = g_metrics.itemWidth;
                g_metrics.itemWidth = frameW;
                if (InspColorEdit(r.label, c, n, &whole)) {
                    r.changed = true;
                    for (int q = 0; q < n; ++q) if (c[q] != before[q]) r.axes |= 1u << q;
                }
                g_metrics.itemWidth = keep;
                break;
            }
            case InspRowText: {
                char buf[512];
                std::snprintf(buf, sizeof buf, "%s", r.mixed ? "" : r.S()->c_str());
                UI::SetNextItemWidth(WithLabel(frameW, r.label, 8.0f));
                bool edited;
                if (r.wrap) {   // a multi-line property (texH: its height in text lines); a box the region wide shows no label (Dear ImGui's is
                                // drawn past the right edge)
                    const std::string hidden = std::string("##") + r.label;
                    edited = UI::InputTextMultiline(r.width < 0.0f ? hidden.c_str() : r.label, buf, (int)sizeof buf,
                                                    UI::Vec2{frameW, fs * (r.texH > 0.0f ? r.texH : 4.0f)});
                }
                else edited = (r.hint || r.mixed) ? UI::InputTextWithHint(r.label, r.mixed ? "Mixed" : r.hint, buf, (int)sizeof buf)
                                                  : UI::InputText(r.label, buf, (int)sizeof buf);
                if (edited) { *r.S() = buf; r.changed = true; }
                if (UI::IsItemActivated()) r.activated = true;
                // Dear ImGui's one-line text field lets the keyboard go on Enter (the editor's shortcuts, Ctrl+Z among them, work again
                // at once); OkayUI's keeps it, so the row gives it up (seen in the real editor: Ctrl+Z after typing a script's text
                // variable went to the field)
                if (!r.wrap && UI::IsItemFocused() && UI::IsKeyPressed(UI::KeyId_Enter, false)) UI::SetKeyboardFocusId(0);
                if (r.pick != InspPickNone) {   // the editor's AssetPathPicker: a small arrow at the right end, and a menu
                    const UI::Rect b = UI::GetItemRect();
                    const float bx1 = b.x + frameW;   // at the field's right end (the editor's arrow once sat on the label's last letters)
                    const bool boxHot = UI::IsMouseHoveringRect(UI::Vec2{b.x, b.y}, UI::Vec2{bx1, b.y + b.h});
                    const bool over = boxHot && UI::IsMouseHoveringRect(UI::Vec2{bx1 - 22.0f, b.y}, UI::Vec2{bx1, b.y + b.h});
                    const float cx = bx1 - 12.0f, cy = b.y + b.h * 0.5f;
                    UI::GetWindowDrawList().AddTriangleFilled(UI::Vec2{cx - 4.0f, cy - 2.0f}, UI::Vec2{cx + 4.0f, cy - 2.0f}, UI::Vec2{cx, cy + 3.0f},
                                                              UI::GetColorU32(over ? UI::Col_Text : UI::Col_TextDisabled));
                    if (over && !UI::IsPopupOpen("##pathmenu")) UI::SetTooltip("Choose a file from the project (or right-click)");
                    if (boxHot && (UI::IsMouseClicked(1) || (over && UI::IsMouseClicked(0)))) { UI::SetKeyboardFocusId(0); UI::OpenPopup("##pathmenu"); }
                    if (UI::IsPopupOpen("##pathmenu")) {
                        MenuLook look_(MenuWidth({"Choose from project...", "Clear"}));
                        UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 8.0f});
                        UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{16.0f, 12.0f});
                        if (UI::BeginPopup("##pathmenu")) {
                            const UI::Vec2 mp = UI::GetCursorScreenPos();
                            bool choose = false;
                            if (MenuRow("Choose from project...")) choose = true;
                            if (r.S()->empty()) UI::BeginDisabled(true);
                            if (MenuRow("Clear")) { r.S()->clear(); r.changed = true; }
                            if (r.S()->empty()) UI::EndDisabled();
                            const UI::Vec2 me = UI::GetCursorScreenPos();
                            setPopup(UI::Rect{mp.x - 16.0f, mp.y - 12.0f, MenuWidth({"Choose from project...", "Clear"}) + 32.0f, me.y - mp.y + 24.0f});
                            UI::EndPopup();
                            if (choose) { UI::OpenPopup("##pick"); Picks().id = 0; }
                        }
                        UI::PopStyleVar(2);
                    }
                }
                break;
            }
            case InspRowSection: {   // the editor's SectionHeader: Spacing, the accent tick, the heading face (22 pixels), a rule
                if (r.small) {   // the First Person Hands window's Section (stage 8): Spacing, a thin tick, the label in the body face, a rule
                    UI::Dummy(1.0f, 0.0f);
                    const UI::Vec2 p = UI::GetCursorScreenPos();
                    UI::GetWindowDrawList().AddRectFilled(UI::Vec2{p.x, p.y + 1.0f}, UI::Vec2{p.x + 3.0f, p.y + fs}, UI::RGBA(120, 170, 230), 1.5f);
                    UI::Indent(9.0f);
                    UI::Text(r.Text());
                    UI::Unindent(9.0f);
                    const UI::Vec2 q = UI::GetCursorScreenPos();
                    UI::Dummy(std::max(1.0f, regionX + w - q.x), 1.0f);
                    UI::GetWindowDrawList().AddLine(UI::Vec2{q.x, q.y + 0.5f}, UI::Vec2{regionX + w, q.y + 0.5f}, UI::GetColorU32(UI::Col_Separator), 1.0f);
                    break;
                }
                UI::Dummy(1.0f, 0.0f);
                UI::PushTextScale(22.0f / 18.0f);
                UI::PushStyleVar(UI::StyleVar_TextRowHeight, UI::GetFontSize());   // the heading's line is the heading face tall
                const UI::Vec2 p = UI::GetCursorScreenPos();
                const float th = UI::GetFontSize();
                UI::GetWindowDrawList().AddRectFilled(UI::Vec2{p.x, p.y + 1.0f}, UI::Vec2{p.x + 3.0f, p.y + th}, kAccent, 1.5f);
                UI::Indent(9.0f);
                UI::TextColored(230, 235, 247, r.Text());
                UI::Unindent(9.0f);
                UI::PopStyleVar();
                UI::PopTextScale();
                {   // Dear ImGui's Separator: one pixel across the region, then the item spacing
                    const UI::Vec2 q = UI::GetCursorScreenPos();
                    UI::Dummy(w, 1.0f);
                    UI::GetWindowDrawList().AddLine(UI::Vec2{regionX, q.y + 0.5f}, UI::Vec2{regionX + w, q.y + 0.5f}, UI::GetColorU32(UI::Col_Separator), 1.0f);
                }
                break;
            }
            case InspRowGroup: {   // Dear ImGui's SeparatorText: a short rule, the title SeparatorTextPadding.x in, a rule to the right edge;
                                   // the rules SeparatorTextBorderSize thick, the row the text and the padding's y above and below tall (the
                                   // host's style: 22 / 8 / 1 in the editor, 20 / 3 / 3 by default)
                const UI::Vec2 q = UI::GetCursorScreenPos();
                const float py = g_metrics.sepTextPadding.y, bt = g_metrics.sepTextBorder;
                const float rh = fs + 2.0f * py, tx = regionX + g_metrics.sepTextPadding.x, tw = UI::CalcTextWidth(r.Text());
                UI::Dummy(w, rh);
                const UI::DrawList& dl = UI::GetWindowDrawList();
                const UI::Color32 sc = UI::GetColorU32(UI::Col_Separator);
                const float ly = q.y + rh * 0.5f - bt * 0.5f;
                dl.AddRectFilled(UI::Vec2{regionX, ly}, UI::Vec2{tx - g_metrics.itemSpacing.x, ly + bt}, sc, 0.0f);
                dl.AddRectFilled(UI::Vec2{tx + tw + g_metrics.itemSpacing.x, ly}, UI::Vec2{regionX + w, ly + bt}, sc, 0.0f);
                dl.AddText(UI::Vec2{tx, q.y + py}, UI::GetColorU32(UI::Col_Text), r.Text());
                break;
            }
            case InspRowNote:
                if (r.alignFrame && !r.wrap) {   // AlignTextToFramePadding, then TextDisabled (the style bar's "Look")
                    UI::Dummy(UI::CalcTextWidth(r.Text()), fh);
                    const UI::Rect b = UI::GetItemRect();
                    UI::GetWindowDrawList().AddText(UI::Vec2{b.x, b.y + g_metrics.framePadding.y}, UI::GetColorU32(UI::Col_TextDisabled), r.Text());
                    break;
                }
                if (!r.wrap && std::strchr(r.Text(), '\n')) { LinesText(r.Text(), UI::GetColorU32(UI::Col_TextDisabled)); break; }
                if (r.wrap) WrapAtRegion(regionX + w);
                UI::TextDisabled(r.Text());
                if (r.wrap) UI::PopTextWrapPos();
                break;
            case InspRowColorText:
            case InspRowLabel: {
                if (r.alignFrame && !r.wrap) {   // Dear ImGui's AlignTextToFramePadding: a frame-tall line, the text where a frame's text sits
                    const std::uint32_t c = r.color ? (r.color | 0xff000000u) : UI::GetColorU32(UI::Col_Text);
                    UI::Dummy(UI::CalcTextWidth(r.Text()), fh);
                    const UI::Rect b = UI::GetItemRect();
                    UI::GetWindowDrawList().AddText(UI::Vec2{b.x, b.y + g_metrics.framePadding.y}, c, r.Text());
                    break;
                }
                if (!r.wrap && std::strchr(r.Text(), '\n')) {
                    LinesText(r.Text(), (r.kind == InspRowColorText || r.color) ? (r.color | 0xff000000u) : UI::GetColorU32(UI::Col_Text));
                    break;
                }
                if (r.wrap) WrapAtRegion(regionX + w);
                if (r.kind == InspRowColorText || r.color) {
                    const std::uint32_t c = r.color;
                    UI::TextColored((unsigned char)(c & 0xff), (unsigned char)((c >> 8) & 0xff), (unsigned char)((c >> 16) & 0xff), r.Text());
                } else if (r.wrap) UI::Text(r.Text());
                else UI::Text(r.Text());
                if (r.wrap) UI::PopTextWrapPos();
                break;
            }
            case InspRowSmallButton:
                if (r.color) UI::PushStyleColor(UI::Col_Text, r.color | 0xff000000u);
                if (InspSmallButton(r.label)) { r.clicked = true; if (r.menu) UI::OpenPopup("##rowmenu"); }
                if (r.color) UI::PopStyleColor();
                if (r.menu && UI::IsPopupOpen("##rowmenu")) {   // stage 8: the entries as a menu (the Modifiers' "...")
                    MenuLook look_(PanelMenuWidth(*r.menu));
                    UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 8.0f});
                    UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{16.0f, 12.0f});
                    if (UI::BeginPopup("##rowmenu")) {
                        std::string pick;
                        std::vector<std::string> ids; std::vector<UI::Rect> mrows;
                        const UI::Vec2 mp = UI::GetCursorScreenPos();
                        DrawPanelMenu(*r.menu, pick, &ids, &mrows);
                        const UI::Vec2 me = UI::GetCursorScreenPos();
                        setPopup(UI::Rect{mp.x - 16.0f, mp.y - 12.0f, PanelMenuWidth(*r.menu) + 32.0f, me.y - mp.y + 24.0f});
                        res.listRows = mrows;
                        if (!pick.empty()) { r.sv = pick; r.changed = true; }
                        UI::EndPopup();
                    }
                    UI::PopStyleVar(2);
                }
                if (r.dragType) {   // a drag handle (stage 7): the Inventory's slot order
                    if (UI::BeginDragDropSource()) {
                        UI::SetDragDropPayload(r.dragType, &r.tag, sizeof(int));
                        UI::SetDragDropPreview(r.hint ? r.hint : "");
                        UI::EndDragDropSource();
                    }
                    if (UI::BeginDragDropTarget()) {
                        if (const UI::DragPayload* p = UI::AcceptDragDropPayload(r.dragType))
                            if (p->size == (int)sizeof(int)) std::memcpy(&r.dropped, p->data, sizeof(int));
                        UI::EndDragDropTarget();
                    }
                }
                break;
            case InspRowProgress: {   // Dear ImGui's ProgressBar: the region wide and a frame tall, in the frame colour, the done part in
                                      // the histogram colour, its text just after the done part (kept inside the bar)
                constexpr UI::Color32 kImGuiHistogram = UI::RGBA(230, 179, 0);   // Dear ImGui's dark style's (0.90, 0.70, 0.00), which the editor keeps
                const UI::Vec2 p = UI::GetCursorScreenPos();
                const float bw = std::max(1.0f, w - (p.x - regionX)), t = std::clamp(r.fv[0], 0.0f, 1.0f);
                UI::Dummy(bw, fh);
                const UI::DrawList& dl = UI::GetWindowDrawList();
                const float rd = 6.0f;   // the frame rounding
                dl.AddRectFilled(UI::Vec2{p.x, p.y}, UI::Vec2{p.x + bw, p.y + fh}, UI::GetColorU32(UI::Col_FrameBg), rd);
                const float fw = std::floor(bw * t);
                if (fw > 0.0f)
                    dl.AddRectFilled(UI::Vec2{p.x, p.y}, UI::Vec2{p.x + fw, p.y + fh}, kImGuiHistogram, rd,
                                     fw >= bw - rd ? UI::DrawFlags_RoundCornersAll : UI::DrawFlags_RoundCornersLeft);
                const char* txt = r.Text();
                const float tw = UI::CalcTextWidth(txt);
                const float tx = std::clamp(p.x + fw + g_metrics.itemSpacing.x, p.x, p.x + bw - tw - g_metrics.innerSpacing.x);
                dl.AddText(UI::Vec2{tx, p.y + (fh - fs) * 0.5f}, UI::GetColorU32(UI::Col_Text), txt);
                break;
            }
            case InspRowButton: {
                if (r.on) {
                    UI::PushStyleColor(UI::Col_Button, UI::ColorMulAlpha(kAccent, 0.90f));
                    UI::PushStyleColor(UI::Col_ButtonHovered, kAccent);
                    UI::PushStyleColor(UI::Col_ButtonActive, UI::ColorMulAlpha(kAccent, 0.80f));
                    UI::PushStyleColor(UI::Col_Text, UI::RGBA(255, 255, 255));
                }
                const std::string vis = Visible(r.label);
                const float bw = r.width != 0.0f ? frameW : UI::CalcTextWidth(vis.c_str()) + 2.0f * g_metrics.framePadding.x;
                if (UI::Button(r.label, UI::Vec2{bw, fh})) r.clicked = true;
                if (r.on) UI::PopStyleColor(4);
                break;
            }
            case InspRowSwatch: {   // Dear ImGui's ColorButton: the colour in a rounded frame
                const float sw = r.texW > 0.0f ? r.texW : fh, sh = r.texH > 0.0f ? r.texH : fh;
                if (UI::InvisibleButton(r.label, sw, sh)) r.clicked = true;
                const UI::Rect b = UI::GetItemRect();
                const UI::Color32 col = UI::RGBA(ColorByte(r.fv[0]), ColorByte(r.fv[1]), ColorByte(r.fv[2]), ColorByte(r.fv[3]));
                UI::GetWindowDrawList().AddRectFilled(UI::Vec2{b.x, b.y}, UI::Vec2{b.x + b.w, b.y + b.h}, col, std::min(3.0f, sh * 0.5f));
                break;
            }
            case InspRowAsset: {   // the editor's AssetField: the name on a button (a click opens the picker), a clear button, the label
                std::string* v = r.S();
                const bool hasValue = !r.mixed && !v->empty();
                const float bw = std::max(40.0f, frameW - (hasValue ? fh + 2.0f : 0.0f));
                std::string shown = r.mixed ? "Mixed" : v->empty() ? std::string("None") : *v;
                if (!r.mixed && !v->empty()) {
                    if (r.s && !r.sv.empty()) shown = r.sv;   // the host's name for the value (an object field: the object's name)
                    else {
                        const std::size_t sl = shown.find_last_of("/\\");
                        if (sl != std::string::npos) shown = shown.substr(sl + 1);
                    }
                }
                const UI::Vec2 p = UI::GetCursorScreenPos();
                const bool hit = UI::InvisibleButton("##asset", bw, fh);
                const UI::Rect b = UI::GetItemRect();
                whole = b;
                const bool hov = UI::IsItemHovered(), held = UI::IsItemActive();
                UI::GetWindowDrawList().AddRectFilled(UI::Vec2{b.x, b.y}, UI::Vec2{b.x + b.w, b.y + b.h},
                                                      UI::GetColorU32(held ? UI::Col_ButtonActive : hov ? UI::Col_ButtonHovered : UI::Col_Button), 6.0f);
                UI::PushClipRect(b.x, b.y, std::max(1.0f, b.w - g_metrics.framePadding.x), b.h);
                UI::GetWindowDrawList().AddText(UI::Vec2{p.x + g_metrics.framePadding.x, p.y + (fh - fs) * 0.5f},
                                                UI::GetColorU32(hasValue ? UI::Col_Text : UI::Col_TextDisabled), shown.c_str());
                UI::PopClipRect();
                if (hov) UI::SetTooltip("%s\nDrop a file from the Project panel here, or click to pick one.",
                                        hasValue ? v->c_str() : (r.tip && *r.tip ? r.tip : "No file"));
                hoverTip = false;
                if (hit) { UI::OpenPopup("##pick"); Picks().id = 0; }
                if (hasValue) {
                    UI::SameLine(2.0f);
                    if (UI::Button("##clear", UI::Vec2{fh, fh})) { v->clear(); r.changed = true; }
                    DrawCross(UI::GetItemRect());
                    if (UI::IsItemHovered()) UI::SetTooltip("Clear");
                }
                const std::string vis = Visible(r.label);
                if (!vis.empty()) { UI::SameLine(g_metrics.innerSpacing.x); UI::Text(vis.c_str()); }
                whole.w = std::max(whole.w, UI::GetItemRectMax().x - whole.x);
                break;
            }
            case InspRowImage: {   // the editor's TexFieldThumb
                if (r.texture) UI::Image(static_cast<SDL_Texture*>(r.texture), r.texW, r.texH);
                else UI::Dummy(r.texW, r.texH);
                const UI::Rect b = UI::GetItemRect();
                // On hover: the path and the hint. Dear ImGui's tooltip also shows the picture 180 pixels wide; OkayUI cannot draw a texture
                // over other windows yet (textured quads always go to the main batch, under the overlay), so the tooltip is text only.
                if (UI::IsItemHovered() && r.texture) UI::SetTooltip("%s  (right-click to find in Project)", r.Text());
                hoverTip = false;
                if (UI::IsMouseClicked(1) && UI::IsMouseHoveringRect(UI::Vec2{b.x, b.y}, UI::Vec2{b.x + b.w, b.y + b.h})) UI::OpenPopup("##thumbctx");
                if (UI::IsPopupOpen("##thumbctx")) {
                    MenuLook look_(MenuWidth({"Show in Project"}));
                    UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 8.0f});
                    UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{16.0f, 12.0f});
                    if (UI::BeginPopup("##thumbctx")) {
                        if (MenuRow("Show in Project")) r.clicked = true;
                        const UI::Rect mr = UI::GetItemRect();
                        setPopup(UI::Rect{mr.x - 16.0f, mr.y - 12.0f, mr.w + 32.0f, mr.h + 24.0f});
                        UI::EndPopup();
                    }
                    UI::PopStyleVar(2);
                }
                break;
            }
            case InspRowMask: {   // the Camera's Culling Mask: Everything, Nothing, then a check box per layer
                int* bits = r.I();
                const int nbits = r.count > 0 && r.count < 32 ? r.count : 32;
                const bool presets = r.presetCount > 0;
                std::vector<std::string> names;
                auto fetch = [&] {   // the layer names, asked of the host only when they are shown (or the row's own)
                    if (!names.empty()) return;
                    if (r.items) { for (int i = 0; i < nbits; ++i) names.emplace_back(r.items[i] ? r.items[i] : ""); }
                    else {
                        std::vector<std::string> vals; std::vector<void*> thumbs;
                        if (list.hooks.choices) list.hooks.choices(r, vals, names, thumbs);
                    }
                    names.resize(32);
                };
                if (!r.preview && *bits != ~0 && *bits != 0) fetch();
                const std::string preview = r.preview ? std::string(r.preview) : InspMaskPreview(*bits, [&](int i) { return names[(std::size_t)i]; });
                MenuLook look_(0.0f);
                // the list is as wide as its widest line, as Dear ImGui's grows to its content: the rename note is wider than a narrow
                // box (seen in the real editor: the note ran out of the list over the Inspector)
                const bool renameNote = !presets && list.hooks.renameLayer && list.hooks.layerName;
                float listW = renameNote ? std::max(frameW, UI::CalcTextWidth("Right-click a layer to rename it") + 34.0f) : frameW;
                if (presets) {   // as wide as the preset buttons' line (the second one ran out of the list in the real editor)
                    float pw = 0.0f;
                    for (int q = 0; q < r.presetCount; ++q) pw += UI::CalcTextWidth(r.presetNames[q]) + 2.0f * g_metrics.framePadding.x + (q ? g_metrics.itemSpacing.x : 0.0f);
                    listW = std::max(listW, pw + 20.0f);
                }
                UI::PushStyleVar(UI::StyleVar_PopupWidth, listW);
                UI::SetNextItemWidth(WithLabel(frameW, r.label));
                if (UI::BeginCombo(r.label, preview.c_str())) {
                    fetch();
                    const UI::Vec2 lp = UI::GetCursorScreenPos();
                    if (presets) {   // the Nav Mesh Agent's Area Mask: preset buttons on one line, a click sets the bits
                        for (int q = 0; q < r.presetCount; ++q) {
                            if (q) UI::SameLine();
                            UI::PushID(q);
                            if (InspSmallButton(r.presetNames[q])) { *bits = r.presetBits[q]; r.changed = true; }
                            UI::PopID();
                        }
                    } else {
                        if (ListRow("Everything", *bits == ~0)) { *bits = ~0; r.changed = true; }
                        if (ListRow("Nothing", *bits == 0)) { *bits = 0; r.changed = true; }
                        Divider();
                    }
                    // the editor's LayerRenameMenu (stage 3): a right-click on a layer renames it, as in the Dear ImGui list
                    const bool rename = renameNote;
                    if (rename) UI::TextDisabled("Right-click a layer to rename it");
                    const float rowH = fh + g_metrics.itemSpacing.y;
                    // as many check box rows as fit in Dear ImGui's list height (eight text rows) after Everything, Nothing and the rule
                    const int shown = std::max(4, (int)std::floor(((float)kListRows - (presets ? 1.0f : 2.0f) - (rename ? 1.0f : 0.0f)) * (fs + g_metrics.itemSpacing.y) / rowH));
                    res.listRows.assign((std::size_t)nbits, UI::Rect{});
                    UI::Rect renameRect{};
                    ScrollRows("##maskrows", nbits, shown, rowH, 0, [&](int i) {
                        bool on = (*bits & (1 << i)) != 0;
                        UI::PushID(i);
                        if (UI::Checkbox(names[(std::size_t)i].c_str(), &on)) {
                            if (on) *bits |= (1 << i); else *bits &= ~(1 << i);
                            r.changed = true;
                        }
                        res.listRows[(std::size_t)i] = UI::GetItemRect();
                        if (rename) {
                            MenuLook look_(152.0f - fs);   // the popup 186 wide: the 170-pixel name field and the padding
                            UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{8.0f, 8.0f});
                            if (UI::BeginPopupContextItem("##layerctx")) {
                                static char buf[128] = "";
                                static std::string err, hint, def;
                                static int s_layer = -1, s_frame = -10;
                                if (s_layer != i || UI::GetFrameCount() > s_frame + 1) {   // the menu opened: the name to edit
                                    std::string text;
                                    list.hooks.layerName(i, text, hint, def);
                                    std::snprintf(buf, sizeof buf, "%s", text.c_str());
                                    err.clear();
                                }
                                s_layer = i; s_frame = UI::GetFrameCount();
                                const UI::Vec2 mp = UI::GetCursorScreenPos();
                                char head[32]; std::snprintf(head, sizeof head, "Layer %d name", i);
                                UI::TextDisabled(head);
                                UI::SetNextItemWidth(170.0f);
                                if (UI::InputTextWithHint("##layername", hint.c_str(), buf, (int)sizeof buf, UI::InputText_EnterReturnsTrue))
                                    if (list.hooks.renameLayer(i, buf, err)) { r.changed = true; UI::CloseCurrentPopup(); }
                                if (InspSmallButton("Reset to default"))
                                    if (list.hooks.renameLayer(i, def, err)) { r.changed = true; UI::CloseCurrentPopup(); }
                                if (!err.empty()) UI::TextColored(242, 115, 102, err.c_str());
                                const UI::Vec2 me = UI::GetCursorScreenPos();
                                renameRect = UI::Rect{mp.x - 8.0f, mp.y - 8.0f, 186.0f, me.y - mp.y + 16.0f};
                                UI::EndPopup();
                            }
                            UI::PopStyleVar();
                        }
                        UI::PopID();
                    });
                    const UI::Vec2 le = UI::GetCursorScreenPos();
                    UI::Rect lr{lp.x - 10.0f, lp.y - 8.0f, listW, le.y - lp.y + 8.0f};
                    if (renameRect.w > 0.0f) {   // the host keeps both the list and the rename menu (one rectangle round both)
                        const float x0 = std::min(lr.x, renameRect.x), y0 = std::min(lr.y, renameRect.y);
                        const float x1 = std::max(lr.x + lr.w, renameRect.x + renameRect.w), y1 = std::max(lr.y + lr.h, renameRect.y + renameRect.h);
                        lr = UI::Rect{x0, y0, x1 - x0, y1 - y0};
                    }
                    setPopup(lr);
                    UI::EndCombo();
                }
                UI::PopStyleVar();
                break;
            }
            case InspRowTree: {   // Dear ImGui's TreeNode: an arrow and the label, one text line tall; the rows inside indented
                bool& open = treeOpen(treeKey, r.on);
                if (r.collapsing) {   // Dear ImGui's CollapsingHeader (stage 8): a frame-tall bar across the region and a little into the
                                      // window's padding on both sides, the arrow and label inside; the rows under it are not indented
                    const UI::Vec2 p = UI::GetCursorScreenPos();
                    const float ext = kCollapsingBleed;
                    UI::SetCursorScreenPos(UI::Vec2{regionX - ext, p.y});
                    if (UI::InvisibleButton("##hdr", w + 2.0f * ext, fh)) open = !open;
                    const bool hov = UI::IsItemHovered(), held = UI::IsItemActive();
                    InspHeaderFrameAt(regionX - ext, p.y, w + 2.0f * ext, fh, hov, held);
                    const std::string vis = Visible(r.label);
                    // Dear ImGui's framed tree node: the arrow a text line square, the frame padding in from the region's left (not the
                    // bar's); the label the text line and three frame paddings in
                    InspArrow(UI::GetWindowDrawList(), regionX + g_metrics.framePadding.x, p.y + g_metrics.framePadding.y, fs, open, UI::GetColorU32(UI::Col_Text));
                    UI::GetWindowDrawList().AddText(UI::Vec2{regionX + fs + 3.0f * g_metrics.framePadding.x, p.y + g_metrics.framePadding.y}, UI::GetColorU32(UI::Col_Text), vis.c_str());
                    *r.B() = open;
                    if (!open) { skipDepth = 1; closedLead = false; }
                    break;
                }
                const std::string vis = Visible(r.label);
                const float tw = fs + g_metrics.framePadding.x * 2.0f + UI::CalcTextWidth(vis.c_str());
                // on a frame's line (a check box before it: the Modifiers) Dear ImGui puts the node on the line's text: centred on the frame
                if (r.sameLine) UI::CenterNextOnLine();
                // the item is the arrow and the label, as Dear ImGui's TreeNode: a row put on its line (SameLine: Auto Fit beside a closed
                // Physics Material) goes after the label; it used to be the whole width, so such a row was drawn past the right edge
                if (UI::InvisibleButton("##tree", tw, fs)) open = !open;
                const UI::Vec2 p{UI::GetItemRect().x, UI::GetItemRect().y};
                const bool hov = UI::IsItemHovered();
                if (hov) {
                    const UI::Rect b = UI::GetItemRect();
                    UI::GetWindowDrawList().AddRectFilled(UI::Vec2{b.x, b.y}, UI::Vec2{b.x + b.w, b.y + b.h}, kHeaderHovered, 0.0f);
                }
                // Dear ImGui's tree arrow: 70% of a text line, its box the frame padding in
                InspArrow(UI::GetWindowDrawList(), p.x + g_metrics.framePadding.x + 0.15f * fs, p.y, 0.7f * fs, open, UI::GetColorU32(UI::Col_Text));
                UI::GetWindowDrawList().AddText(UI::Vec2{p.x + fs + g_metrics.framePadding.x * 2.0f, p.y}, UI::GetColorU32(UI::Col_Text), vis.c_str());
                *r.B() = open;
                if (r.menu) {   // stage 9: a right-click on the fold-out opens its menu (the Modifiers': the same as its "...")
                    if (UI::IsItemClicked(1)) UI::OpenPopup("##treemenu");
                    if (UI::IsPopupOpen("##treemenu")) {
                        MenuLook look_(PanelMenuWidth(*r.menu));
                        UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{12.0f, 8.0f});
                        UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{16.0f, 12.0f});
                        if (UI::BeginPopup("##treemenu")) {
                            std::string pick;
                            std::vector<std::string> ids; std::vector<UI::Rect> mrows;
                            const UI::Vec2 mp = UI::GetCursorScreenPos();
                            DrawPanelMenu(*r.menu, pick, &ids, &mrows);
                            const UI::Vec2 me = UI::GetCursorScreenPos();
                            setPopup(UI::Rect{mp.x - 16.0f, mp.y - 12.0f, PanelMenuWidth(*r.menu) + 32.0f, me.y - mp.y + 24.0f});
                            res.listRows = mrows;
                            if (!pick.empty()) { r.sv = pick; r.changed = true; }
                            UI::EndPopup();
                        }
                        UI::PopStyleVar(2);
                    }
                }
                if (open) { UI::Indent(g_metrics.indentSpacing); ++indent; }
                else { skipDepth = 1; closedLead = true; }
                break;
            }
            case InspRowTreeEnd:
                if (!r.collapsing && indent > 0) { UI::Unindent(g_metrics.indentSpacing); --indent; }
                break;
            case InspRowSpacing:
                UI::Dummy(1.0f, 0.0f);
                break;
            case InspRowSeparator: {   // Dear ImGui's Separator: one pixel from the cursor to the region's right edge, then the item spacing
                const UI::Vec2 q = UI::GetCursorScreenPos();
                UI::Dummy(std::max(1.0f, regionX + w - q.x), 1.0f);
                UI::GetWindowDrawList().AddLine(UI::Vec2{q.x, q.y + 0.5f}, UI::Vec2{regionX + w, q.y + 0.5f}, UI::GetColorU32(UI::Col_Separator), 1.0f);
                hoverTip = false;
                break;
            }
            case InspRowBullet: {   // Dear ImGui's BulletText: a dot in a text-line square, the text after it and the frame padding
                const UI::Vec2 p = UI::GetCursorScreenPos();
                const float tw = UI::CalcTextWidth(r.Text());
                UI::Dummy(fs + (tw > 0.0f ? tw + 2.0f * g_metrics.framePadding.x : 0.0f), fs);
                const UI::DrawList& dl = UI::GetWindowDrawList();
                dl.AddCircleFilled(UI::Vec2{p.x + g_metrics.framePadding.x + fs * 0.5f, p.y + fs * 0.5f}, fs * 0.20f, UI::GetColorU32(UI::Col_Text), 12);
                dl.AddText(UI::Vec2{p.x + fs + 2.0f * g_metrics.framePadding.x, p.y}, UI::GetColorU32(UI::Col_Text), r.Text());
                break;
            }
            case InspRowGradient: {   // the editor's ParticleGradientEditor: a check under the bar, the gradient in 32 strips, a marker per stop
                float* st = r.F();
                int& cnt = *r.I();
                const int maxStops = std::max(1, (int)r.hi);
                int sel = r.sel ? *r.sel : 0;
                if (sel >= cnt) sel = cnt - 1;
                const UI::Vec2 p0 = UI::GetCursorScreenPos();
                const float gw = std::max(60.0f, regionX + w - p0.x), barH = 18.0f, mkH = 9.0f;
                const UI::Vec2 p1{p0.x + gw, p0.y + barH};
                const UI::DrawList& dl = UI::GetWindowDrawList();
                for (int cx = 0; (float)cx * 6.0f < gw; ++cx)
                    for (int cy = 0; cy < 3; ++cy)
                        dl.AddRectFilled(UI::Vec2{p0.x + (float)cx * 6.0f, p0.y + (float)cy * 6.0f},
                                         UI::Vec2{std::min(p1.x, p0.x + (float)cx * 6.0f + 6.0f), p0.y + (float)cy * 6.0f + 6.0f},
                                         ((cx + cy) & 1) ? UI::RGBA(150, 150, 150) : UI::RGBA(95, 95, 95), 0.0f);
                auto col32 = [](const float c[4]) { auto b = [](float v) { return (int)(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
                                                     return UI::RGBA(b(c[0]), b(c[1]), b(c[2]), b(c[3])); };
                for (int i = 0; i < 32; ++i) {
                    const float ta = (float)i / 32.0f, tb = (float)(i + 1) / 32.0f;
                    float ca[4], cb[4];
                    InspGradientAt(st, cnt, ta, ca); InspGradientAt(st, cnt, tb, cb);
                    dl.AddRectFilledMultiColor(UI::Vec2{p0.x + ta * gw, p0.y}, UI::Vec2{p0.x + tb * gw, p1.y}, col32(ca), col32(cb), col32(cb), col32(ca));
                }
                dl.AddRect(p0, p1, UI::RGBA(20, 20, 24), 0.0f, 1.0f);
                for (int i = 0; i < cnt; ++i) {
                    const float x = p0.x + std::clamp(st[i * 5], 0.0f, 1.0f) * gw;
                    const UI::Vec2 a{x, p1.y}, b{x - 5.0f, p1.y + mkH}, c{x + 5.0f, p1.y + mkH};
                    const float rgb[4] = {st[i * 5 + 1], st[i * 5 + 2], st[i * 5 + 3], 1.0f};
                    dl.AddTriangleFilled(a, b, c, col32(rgb));
                    dl.AddTriangle(a, b, c, i == sel ? kAccent : UI::RGBA(220, 220, 225), i == sel ? 2.0f : 1.0f);
                }
                UI::InvisibleButton("##gradbar", gw, barH + mkH + 2.0f);
                const UI::Vec2 m = UI::GetMousePos();
                const float mt = std::clamp((m.x - p0.x) / gw, 0.0f, 1.0f);
                static unsigned s_dragId = 0;
                const unsigned gid = UI::GetID("##gradbar");
                if (UI::IsItemActivated()) {   // a press: the marker under it, else a new stop with the colour showing there
                    r.activated = true;
                    int hit = InspNearestStop(st, cnt, p0.x, gw, m.x);
                    if (hit < 0) { float c[4]; InspGradientAt(st, cnt, mt, c); hit = InspAddStop(st, cnt, maxStops, mt, c); if (hit >= 0) r.changed = true; }
                    if (hit >= 0) sel = hit;
                    s_dragId = hit >= 0 ? gid : 0;
                }
                if (UI::IsItemActive() && s_dragId == gid && sel >= 0 && UI::IsMouseDragging(0, 1.0f) && st[sel * 5] != mt) { st[sel * 5] = mt; r.changed = true; }
                if (!UI::IsMouseDown(0) && s_dragId == gid) s_dragId = 0;
                if (UI::IsItemHovered() && UI::IsMouseClicked(1)) {   // a right-click on a marker deletes it (one stop always stays)
                    const int hit = InspNearestStop(st, cnt, p0.x, gw, m.x);
                    if (hit >= 0 && cnt > 1) { r.activated = true; InspRemoveAt(st, cnt, 5, hit); r.changed = true; if (sel >= cnt) sel = cnt - 1; }
                }
                if (UI::IsItemHovered()) UI::SetTooltip("Click the bar to add a stop, drag a marker to move it, right-click a marker to delete it.");
                hoverTip = false;
                if (r.sel) *r.sel = sel;
                break;
            }
            case InspRowCurve: {   // the editor's ParticleCurveEditor: a 64-pixel plot, the x1 line, quarter lines, the curve, a dot per key
                float* ks = r.F();
                int& cnt = *r.I();
                const int maxKeys = std::max(1, (int)r.hi);
                int sel = r.sel ? *r.sel : 0;
                if (sel >= cnt) sel = cnt - 1;
                const UI::Vec2 c0 = UI::GetCursorScreenPos();
                const UI::Vec2 p0{c0.x, c0.y + fs};   // the scale label ("x1.8") sits on its own line above the plot's right end: inside the plot it covered the first key
                const float gw = std::max(60.0f, regionX + w - p0.x), gh = 64.0f;
                const UI::Vec2 p1{p0.x + gw, p0.y + gh};
                static unsigned s_dragId = 0;
                static float s_top = 1.0f;
                const unsigned cid = UI::GetID("##curveplot");
                const bool dragging = s_dragId == cid && UI::IsMouseDown(0);
                const float top = dragging ? s_top : InspCurveTop(ks, cnt);   // frozen while a key is dragged, so it does not jump
                s_top = dragging ? s_top : top;
                auto at = [&](float t, float v) { return UI::Vec2{p0.x + t * gw, p1.y - (v / top) * gh}; };
                const UI::DrawList& dl = UI::GetWindowDrawList();
                dl.AddRectFilled(p0, p1, UI::RGBA(28, 30, 36), 3.0f);
                const float y1 = at(0.0f, 1.0f).y;
                dl.AddLine(UI::Vec2{p0.x, y1}, UI::Vec2{p1.x, y1}, UI::RGBA(70, 74, 86), 1.0f);
                for (int gx = 1; gx < 4; ++gx)
                    dl.AddLine(UI::Vec2{p0.x + gw * (float)gx * 0.25f, p0.y}, UI::Vec2{p0.x + gw * (float)gx * 0.25f, p1.y}, UI::RGBA(48, 50, 60), 1.0f);
                const bool smooth = *r.B();
                UI::Vec2 prev = at(0.0f, InspCurveAt(ks, cnt, smooth, 0.0f));
                for (int i = 1; i <= 48; ++i) {
                    const float t = (float)i / 48.0f;
                    const UI::Vec2 cur = at(t, InspCurveAt(ks, cnt, smooth, t));
                    dl.AddLine(prev, cur, kAccent, 2.0f);
                    prev = cur;
                }
                for (int i = 0; i < cnt; ++i) {
                    const UI::Vec2 kp = at(ks[i * 2], ks[i * 2 + 1]);
                    dl.AddCircleFilled(kp, i == sel ? 5.0f : 4.0f, i == sel ? UI::RGBA(255, 255, 255) : UI::RGBA(200, 205, 215));
                    if (i == sel) dl.AddCircle(kp, 6.5f, kAccent, 12, 1.5f);
                }
                dl.AddRect(p0, p1, UI::RGBA(20, 20, 24), 3.0f, 1.0f);
                char ylab[16]; std::snprintf(ylab, sizeof ylab, "x%.2g", top);
                dl.AddText(UI::Vec2{p1.x - UI::CalcTextWidth(ylab) - 3.0f, c0.y}, UI::RGBA(140, 145, 160), ylab);
                UI::InvisibleButton("##curveplot", gw, fs + gh);
                const UI::Vec2 m = UI::GetMousePos();
                const float mt = std::clamp((m.x - p0.x) / gw, 0.0f, 1.0f);
                const float mv = std::max(0.0f, (p1.y - m.y) / gh * top);
                if (UI::IsItemActivated() && m.y >= p0.y) {   // not on the label's line
                    r.activated = true;
                    int hit = InspNearestKey(ks, cnt, p0.x, p1.y, gw, gh, top, m.x, m.y);
                    if (hit < 0) { hit = InspAddKey(ks, cnt, maxKeys, mt, mv); if (hit >= 0) r.changed = true; }
                    if (hit >= 0) sel = hit;
                    s_dragId = hit >= 0 ? cid : 0;
                    s_top = top;
                }
                if (UI::IsItemActive() && s_dragId == cid && sel >= 0 && UI::IsMouseDragging(0, 1.0f)) {
                    ks[sel * 2] = mt; ks[sel * 2 + 1] = std::min(mv, top); r.changed = true;
                }
                if (!UI::IsMouseDown(0) && s_dragId == cid) s_dragId = 0;
                if (UI::IsItemHovered() && UI::IsMouseClicked(1)) {
                    const int hit = InspNearestKey(ks, cnt, p0.x, p1.y, gw, gh, top, m.x, m.y);
                    if (hit >= 0 && cnt > 1) { r.activated = true; InspRemoveAt(ks, cnt, 2, hit); r.changed = true; if (sel >= cnt) sel = cnt - 1; }
                }
                if (UI::IsItemHovered()) UI::SetTooltip("Click to add a key, drag a key to move it, right-click a key to delete it.");
                hoverTip = false;
                if (r.sel) *r.sel = sel;
                break;
            }
            case InspRowAnchor: {   // the editor's AnchorCombo: the anchor's name on a button, its picture at the left, the label; a grid
                int* a = r.I();
                const int ai = (*a < 0 || *a > 11) ? 0 : *a;
                const UI::Vec2 bp = UI::GetCursorScreenPos();
                if (UI::Button(kInspAnchorNames[ai], UI::Vec2{frameW, fh})) UI::OpenPopup("##anchorpop");
                whole = UI::GetItemRect();
                auto picture = [&](float x0, float y0, float x1, float y1, int which, bool active) {
                    const UI::DrawList& dl = UI::GetWindowDrawList();
                    dl.AddRect(UI::Vec2{x0, y0}, UI::Vec2{x1, y1}, active ? UI::RGBA(150, 190, 255) : UI::RGBA(150, 154, 166), 2.0f, 1.2f);
                    const InspAnchorPicture f = InspAnchorFill(which, x0, y0, x1, y1);
                    dl.AddRectFilled(UI::Vec2{f.fx0, f.fy0}, UI::Vec2{f.fx1, f.fy1}, active ? UI::RGBA(110, 160, 255) : UI::RGBA(205, 208, 216), 1.5f);
                };
                picture(bp.x + 4.0f, bp.y + 3.0f, bp.x + fh - 2.0f, bp.y + fh - 3.0f, ai, false);
                if (UI::IsItemHovered() && !UI::IsPopupOpen("##anchorpop"))
                    UI::SetTooltip(ai >= 9 ? "Stretch: the widget fills its parent / the canvas on the stretched axis;\nPosition and Size become margins. Click to change."
                                           : "Which point of its parent / the screen this widget sticks to when the\nwindow resizes. Click to change.");
                hoverTip = false;
                const std::string vis = Visible(r.label);
                if (!vis.empty()) { UI::SameLine(g_metrics.innerSpacing.x); UI::Text(vis.c_str()); whole.w = UI::GetItemRectMax().x - whole.x; }
                if (UI::IsPopupOpen("##anchorpop")) {
                    MenuLook look_(0.0f);
                    const float cell = 40.0f;
                    const float popW = 3.0f * cell + 2.0f * 4.0f + 10.0f + cell * 1.4f;
                    UI::PushStyleVar(UI::StyleVar_PopupWidth, std::max(popW, UI::CalcTextWidth("ANCHOR  (Shift+click: also move it there)")) + 20.0f);
                    if (UI::BeginPopup("##anchorpop")) {
                        const UI::Vec2 pp = UI::GetCursorScreenPos();
                        UI::TextDisabled("ANCHOR");
                        UI::SameLine(); UI::TextDisabled("(Shift+click: also move it there)");
                        auto cellButton = [&](int which, float cw, float ch) {
                            UI::PushID(which);
                            const UI::Vec2 p = UI::GetCursorScreenPos();
                            const bool hit = UI::InvisibleButton("##a", cw, ch);
                            const bool hov = UI::IsItemHovered();
                            UI::GetWindowDrawList().AddRectFilled(p, UI::Vec2{p.x + cw, p.y + ch}, hov ? UI::RGBA(70, 76, 92) : UI::RGBA(44, 46, 54), 4.0f);
                            picture(p.x + 6.0f, p.y + 6.0f, p.x + cw - 6.0f, p.y + ch - 6.0f, which, which == ai);
                            if (hov) UI::SetTooltip("%s", kInspAnchorNames[which]);
                            UI::PopID();
                            if (hit) {
                                *a = which; r.changed = true; r.activated = true;
                                if (UI::GetIO().keyShift) r.axes = 1u;
                                UI::CloseCurrentPopup();
                            }
                        };
                        for (int row = 0; row < 3; ++row) {   // the rows the Inspector's item spacing apart, as Dear ImGui's popup has them
                            for (int col = 0; col < 3; ++col) { if (col) UI::SameLine(4.0f); cellButton(row * 3 + col, cell, cell); }
                            UI::SameLine(10.0f);
                            if (row == 1) cellButton(9, cell * 1.4f, cell); else UI::Dummy(cell * 1.4f, cell);
                        }
                        cellButton(10, cell, cell * 1.4f); UI::SameLine(4.0f);
                        cellButton(11, cell * 1.4f, cell * 1.4f);
                        UI::SameLine(); UI::TextDisabled("Stretch vertical / Fill");
                        const UI::Vec2 pe = UI::GetCursorScreenPos();
                        setPopup(UI::Rect{pp.x - 10.0f, pp.y - 8.0f, std::max(popW, UI::CalcTextWidth("ANCHOR  (Shift+click: also move it there)")) + 20.0f, pe.y - pp.y + 16.0f});
                        UI::EndPopup();
                    }
                    UI::PopStyleVar();
                }
                break;
            }
            case InspRowSlot: {   // the editor's AssetSlot: the file's name centred on a button 28 short of the region, an "x" small button
                std::string* v = r.S();
                std::string name = *v;
                const std::size_t sl = name.find_last_of("/\\");
                if (sl != std::string::npos) name = name.substr(sl + 1);
                if (v->empty()) name = std::string("None (") + (r.hint ? r.hint : "") + ")";
                const UI::Vec2 p = UI::GetCursorScreenPos();
                const float bw = std::max(1.0f, regionX + w - p.x - 28.0f);
                UI::PushID("##slot");
                UI::Button((name + "##slotbtn").c_str(), UI::Vec2{bw, fh});
                whole = UI::GetItemRect();
                if (UI::IsItemHovered()) UI::SetTooltip("Drag a %s from the Project panel here", r.hint ? r.hint : "file");
                hoverTip = false;
                UI::SameLine();
                if (InspSmallButton("x") && !v->empty()) { v->clear(); r.changed = true; r.activated = true; }
                UI::PopID();
                break;
            }
            case InspRowInputInt: {   // Dear ImGui's InputInt: the field, then - and + buttons a frame tall
                UI::SetNextItemWidth(WithLabel(frameW, r.label));
                if (UI::InputInt(r.label, r.I(), 1)) r.changed = true;
                if (UI::IsItemActivated()) r.activated = true;
                break;
            }
            case InspRowRadio: {   // Dear ImGui's RadioButton
                if (UI::RadioButton(r.label, r.I(), r.iv)) r.changed = true;
                break;
            }
            case InspRowRange: {   // Dear ImGui's DragFloatRange2: two cells sharing the value column, the label after them
                float* v = r.F();
                const float inner = g_metrics.innerSpacing.x;
                float widths[2];
                InspCellWidths(frameW, 2, inner, widths);
                UI::PushID(r.label);
                UI::SetNextItemWidth(widths[0]);
                if (UI::DragFloat("##min", &v[0], r.speed, r.lo, std::min(v[1], r.hi), r.fmt ? r.fmt : "%.3f")) { v[0] = std::min(v[0], v[1]); r.changed = true; }
                if (UI::IsItemActivated()) r.activated = true;
                whole = UI::GetItemRect();
                UI::SameLine(inner);
                UI::SetNextItemWidth(widths[1]);
                if (UI::DragFloat("##max", &v[1], r.speed, std::max(v[0], r.lo), r.hi, r.hint ? r.hint : (r.fmt ? r.fmt : "%.3f"))) { v[1] = std::max(v[1], v[0]); r.changed = true; }
                if (UI::IsItemActivated()) r.activated = true;
                UI::PopID();
                const std::string vis = Visible(r.label);
                if (!vis.empty()) {   // the label a frame tall, its text where a frame's sits: a button put after it stays on the line's top
                    UI::SameLine(inner);
                    UI::Dummy(UI::CalcTextWidth(vis.c_str()), fh);
                    const UI::Rect lb = UI::GetItemRect();
                    UI::GetWindowDrawList().AddText(UI::Vec2{lb.x, lb.y + g_metrics.framePadding.y}, UI::GetColorU32(UI::Col_Text), vis.c_str());
                }
                whole.w = UI::GetItemRectMax().x - whole.x;
                break;
            }
            }
            const UI::Rect rr = whole.w > 0.0f ? whole : UI::GetItemRect();
            res.rows[k] = rr;
            if (r.dropState) {   // a file from the Project panel is being dragged: the editor's DropOutline round the field
                const float fx1 = r.kind == InspRowAsset || r.kind == InspRowText ? rr.x + frameW : rr.x + rr.w;
                const bool over = UI::IsMouseHoveringRect(UI::Vec2{rr.x, rr.y}, UI::Vec2{fx1, rr.y + rr.h});
                const UI::Color32 col = r.dropState == 1 ? UI::ColorMulAlpha(kAccent, over ? 1.0f : 0.7f) : UI::RGBA(220, 90, 80, over ? 255 : 150);
                UI::GetWindowDrawList().AddRect(UI::Vec2{rr.x, rr.y}, UI::Vec2{fx1, rr.y + rr.h}, col, 3.0f, over ? 2.5f : 1.5f);
            }
            // the tooltip: the row's own, else the registry's (asked only now); the prefab mark and its menu
            if (hoverTip && r.tip && *r.tip && UI::IsItemHovered()) UI::SetTooltip("%s", r.tip);
            if ((hoverTip && r.tipLookup && !(r.tip && *r.tip)) || r.overridden) {
                UI::Rect pop;
                FieldExtras(r.label, rr, regionX, r.overridden, hoverTip && r.tipLookup && !(r.tip && *r.tip), &list.hooks, list.prefabField, list.prefabChosen, &pop);
                setPopup(pop);
            }
            // a picker opened from this row (an asset field, or a small button that picks): a list with a search field, or thumbnails
            if (r.pick != InspPickNone && r.kind == InspRowSmallButton && r.clicked) { UI::OpenPopup("##pick"); Picks().id = 0; }
            if (r.pick != InspPickNone && UI::IsPopupOpen("##pick")) {
                PickCache& pc = Picks();
                const unsigned pid = UI::GetID("##pick");
                bool fresh = false;   // the picker opened this frame: its search field takes the keyboard, as Dear ImGui's does
                if (pc.id != pid || pc.frame < UI::GetFrameCount() - 1) {
                    pc = PickCache(); pc.id = pid; fresh = true;
                    if (list.hooks.choices) list.hooks.choices(r, pc.values, pc.names, pc.thumbs);
                    pc.names.resize(pc.values.size()); pc.thumbs.resize(pc.values.size(), nullptr);
                }
                pc.frame = UI::GetFrameCount();
                const bool grid = r.pick == InspPickGrid;
                const float cell = 48.0f + 2.0f * g_metrics.framePadding.x;
                const float popW = grid ? 4.0f * cell + 3.0f * g_metrics.itemSpacing.x : 360.0f;
                MenuLook look_(0.0f);
                UI::PushStyleVar(UI::StyleVar_PopupWidth, popW + 32.0f);
                UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{16.0f, 12.0f});
                if (UI::BeginPopup("##pick")) {
                    const UI::Vec2 pp = UI::GetCursorScreenPos();
                    std::string* target = r.S();
                    if (grid) {
                        for (std::size_t q = 0; q < pc.values.size(); ++q) {
                            if (q % 4 != 0) UI::SameLine();
                            UI::PushID((int)q);
                            bool hit;
                            if (pc.thumbs[q]) hit = UI::ImageButton("##pt", static_cast<SDL_Texture*>(pc.thumbs[q]), 48.0f, 48.0f);
                            else hit = UI::Button(pc.names[q].c_str(), UI::Vec2{54.0f, 54.0f});
                            if (UI::IsItemHovered()) UI::SetTooltip("%s", pc.values[q].c_str());
                            if (hit) { *target = pc.values[q]; r.changed = true; UI::CloseCurrentPopup(); }
                            UI::PopID();
                        }
                    } else {
                        if (fresh) UI::SetKeyboardFocusHere();
                        UI::SetNextItemWidth(popW);
                        UI::InputTextWithHint("##find", "Search files...", pc.filter, (int)sizeof pc.filter);
                        if (ListRow("None", target->empty())) { target->clear(); r.changed = true; }
                        Divider();
                        std::string needle(pc.filter);
                        for (char& ch : needle) ch = (char)std::tolower((unsigned char)ch);
                        std::vector<int> hits;
                        for (std::size_t q = 0; q < pc.values.size() && hits.size() < 400; ++q) {
                            if (!needle.empty()) {
                                std::string low = pc.names[q];
                                for (char& ch : low) ch = (char)std::tolower((unsigned char)ch);
                                if (low.find(needle) == std::string::npos) continue;
                            }
                            hits.push_back((int)q);
                        }
                        const float rowH = fs + g_metrics.itemSpacing.y;
                        const int shown = std::max(1, (int)std::floor(260.0f / rowH));
                        res.listRows.assign(hits.size(), UI::Rect{});
                        ScrollRows("##pickrows", (int)hits.size(), shown, rowH, 0, [&](int q) {
                            const int at = hits[(std::size_t)q];
                            UI::PushID(at);
                            if (ListRow(pc.names[(std::size_t)at].c_str(), pc.values[(std::size_t)at] == *target)) { *target = pc.values[(std::size_t)at]; r.changed = true; }
                            res.listRows[(std::size_t)q] = UI::GetItemRect();
                            UI::PopID();
                        });
                        if (hits.empty()) UI::TextDisabled(pc.values.empty() ? "Open a project to pick files." : "No matching files in this project.");
                    }
                    const UI::Vec2 pe = UI::GetCursorScreenPos();
                    setPopup(UI::Rect{pp.x - 16.0f, pp.y - 12.0f, popW + 32.0f, pe.y - pp.y + 24.0f});
                    UI::EndPopup();
                }
                UI::PopStyleVar(2);
            }
            UI::PopID();
            if (r.disabled) UI::EndDisabled();
        }
        while (indent-- > 0) UI::Unindent(g_metrics.indentSpacing);
        res.contentHeight = ContentHeight(top);
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

} // namespace okayuiscreens
