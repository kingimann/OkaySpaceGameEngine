// The editor's Problems, Stats, History, Tags and Layers, Physics, Profiler, Import settings, Post-processing, Player and Rendering panels
// and sections drawn with OkayUI. Written from what the
// editor's own Dear ImGui panels showed and did (the reference bodies were deleted with Dear ImGui on 2026-11-27) and from Dear ImGui's documented
// behaviour where the look had to match; no code from any other project.
#include "okay/UI/OkayUIDesign.hpp"
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
void FollowDesign();   // below: the editor colours from the design in force
struct EditorRows {
    EditorRows() {
        FollowDesign();
        UI::PushStyleVar(UI::StyleVar_ItemSpacing, g_metrics.itemSpacing);
        UI::PushStyleVar(UI::StyleVar_TextRowHeight, UI::GetFontSize());
        UI::PushStyleVar(UI::StyleVar_FramePadding, g_metrics.framePadding);
        UI::PushStyleVar(UI::StyleVar_WindowPadding, UI::Vec2{16.0f, 12.0f});
        UI::PushStyleVar(UI::StyleVar_ScrollbarSize, 14.0f);
        UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
        const UI::Theme& t = UI::Style();
        UI::PushStyleColor(UI::Col_ScrollbarGrab, UI::RGBA(t.ink[0], t.ink[1], t.ink[2], 31));          // the ink at 0.12
        UI::PushStyleColor(UI::Col_ScrollbarGrabHovered, UI::RGBA(t.ink[0], t.ink[1], t.ink[2], 61));   // the ink at 0.24
        UI::PushStyleColor(UI::Col_TextDisabled, UI::RGBA(t.textDim[0], t.textDim[1], t.textDim[2]));   // the design's dim text
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

// The editor's Dear ImGui colours (shared/OkayTheme.hpp ApplyColors) that OkayUI's theme has no field for, worked out from the design
// in force by the same formulas, so a panel drawn by OkayUI matches the editor's Dear ImGui panels in every design (FollowDesign,
// at the start of every panel). The values below are the Slate design's, before the first panel.
UI::Color32 kPopupBg = UI::RGBA(35, 39, 48, 255);         // Mix(kRaised, kBase, 0.35), opaque (what lies under a menu must not show through)
UI::Color32 kHeader = UI::RGBA(53, 68, 95);               // Mix(kControl, accent, 0.18): a selected row
UI::Color32 kHeaderHovered = UI::RGBA(65, 70, 84);        // Mix(kControl, ink, 0.07)
UI::Color32 kHeaderActive = UI::RGBA(55, 85, 131);        // Mix(kControl, accent, 0.45)
UI::Color32 kFieldBg = UI::RGBA(41, 46, 59);              // kField
UI::Color32 kFieldHovered = UI::RGBA(52, 56, 68);         // Mix(kField, ink, 0.05)
UI::Color32 kFieldActive = UI::RGBA(60, 65, 76);          // Mix(kField, ink, 0.09)
UI::Color32 kAccent = UI::RGBA(60, 121, 204);             // the accent
UI::Color32 kAccentLit = UI::RGBA(115, 159, 218);         // Mix(accent, white, 0.28) (light schemes: toward black): slider grab held, check marks
UI::Color32 kCardBg = UI::RGBA(41, 43, 51);               // the object header card: the raised surface
void FollowDesign() {
    const UI::Theme& t = UI::Style();
    auto mix = [](const unsigned char* a, const unsigned char* b, float k) { return OkayUI::design::PackedMix(a, b, k); };
    const unsigned char white[4] = {255, 255, 255, 255}, black[4] = {0, 0, 0, 255};
    const unsigned char* ink = t.ink;
    kPopupBg = UI::RGBA(t.overlay[0], t.overlay[1], t.overlay[2], 255);
    kHeader = mix(t.bg, t.accent, 0.18f); kHeaderHovered = mix(t.bg, ink, 0.07f); kHeaderActive = mix(t.bg, t.accent, 0.45f);
    kFieldBg = UI::RGBA(t.field[0], t.field[1], t.field[2]); kFieldHovered = mix(t.field, ink, 0.05f); kFieldActive = mix(t.field, ink, 0.09f);
    kAccent = UI::RGBA(t.accent[0], t.accent[1], t.accent[2]);
    kAccentLit = t.lightScheme ? mix(t.accent, black, 0.18f) : mix(t.accent, white, 0.28f);
    kCardBg = UI::RGBA(t.raised[0], t.raised[1], t.raised[2]);
}

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
// Dear ImGui's line plot: a rounded field, the line inside the frame padding, the overlay text centred at the top. `width` > 0: that
// wide (else the region's width).
UI::Rect FramedPlot(const char* id, const std::vector<float>& v, int n, const char* overlay, float top, float height, float width = 0.0f) {
    const UI::Vec2 p = UI::GetCursorScreenPos();
    const float pw = width > 0.0f ? width : UI::GetContentRegionAvail().x;
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
// `width` > 0: that wide (else the region's width).
UI::Rect FramedBar(float frac, const char* text, UI::Color32 fill, float width = 0.0f) {
    const UI::Vec2 p = UI::GetCursorScreenPos();
    const float bw = width > 0.0f ? width : UI::GetContentRegionAvail().x, bh = UI::GetFontSize() + 16.0f;
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
// The import panel's combo: as wide as its longest choice, at least 160 pixels, never wider than the room left of its label
// (ImportComboWidth, the editor's rule), the label after it.
bool ImportCombo(const char* label, int& value, const char* const* items, int count, UI::Rect& rect) {
    float widest = 0.0f;
    for (int i = 0; i < count; ++i) widest = std::max(widest, UI::CalcTextWidth(items[i]));
    const float labelW = WithLabel(0.0f, label);
    const float frameW = ImportComboWidth(widest, g_metrics.framePadding.x, UI::GetFontSize() + 2.0f * g_metrics.framePadding.y,
                                          UI::GetContentRegionAvail().x, labelW > 0.0f ? labelW - g_metrics.innerSpacing.x : 0.0f,
                                          g_metrics.innerSpacing.x);
    UI::SetNextItemWidth(WithLabel(frameW, label));
    int v = value >= 0 && value < count ? value : -1;
    const bool ch = UI::Combo(label, items, count, &v);
    rect = UI::GetItemRect();
    if (ch && v >= 0 && v < count && v != value) { value = v; return true; }
    return false;
}
// A grey note under a setting, wrapped at the panel's edge.
void ImportHint(const char* text) {
    UI::PushTextWrapPos(0.0f);
    UI::TextDisabled(text);
    UI::PopTextWrapPos();
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
        if (body && m.newer) { ImportHint(kImportNewerNote); body = false; }
        ImportFields* e = body ? m.edit : nullptr;
        auto box = [&](const char* label, bool* v, UI::Rect& rect) {
            const bool ch = UI::Checkbox(label, v);
            rect = UI::GetItemRect();
            return ch;
        };
        if (e) switch (e->kind) {
            case ImportTexture: {
                int si = ImportSizeIndex(e->maxSize);
                if (ImportCombo("Largest side", si, kImportSizeNames, 11, res.sizeCombo)) { e->maxSize = kImportSizes[si]; res.intent.changed = true; }
                ImportHint("Bigger pictures are made smaller when loaded.");
                if (m.showSampler) {
                    int kind = ImportTexKindIndex(*e);
                    if (ImportCombo("Kind", kind, kImportKindNames, 3, res.kindCombo)) { ImportSetTexKind(*e, kind); res.intent.changed = true; }
                    ImportHint("Colour pictures are filtered in linear light. Normal maps stay unit length in every mip.");
                    int fi = ImportFilterIndex(*e);
                    if (ImportCombo("Filter", fi, kImportFilterNames, 4, res.filterCombo)) { ImportSetFilter(*e, fi); res.intent.changed = true; }
                    if (fi == 3) {
                        int li = ImportAnisoIndex(e->anisotropy);
                        if (ImportCombo("Anisotropy", li, kImportAnisoNames, 4, res.anisoCombo)) { e->anisotropy = kImportAnisoLevels[li]; res.intent.changed = true; }
                    }
                    ImportHint("Point keeps hard pixel edges. Anisotropic keeps floors sharp far away.");
                    if (ImportCombo("Wrap", e->wrap, kImportWrapNames, 3, res.wrapCombo)) res.intent.changed = true;
                    ImportHint("What is drawn past the picture's edge.");
                    if (box("Mipmaps", &e->mipmaps, res.mipmaps)) res.intent.changed = true;
                    if (e->mipmaps && ImportCombo("Mip filter", e->mipFilter, kImportMipFilterNames, 2, res.mipFilterCombo)) res.intent.changed = true;
                    ImportHint("Smaller copies for far away, made by our own filter. Off: always full size.");
                    if (box("Compress on the GPU", &e->compress, res.compress)) res.intent.changed = true;
                    if (e->compress) {
                        if (ImportCombo("Format", e->compression, kImportFormatNames, 5, res.formatCombo)) res.intent.changed = true;
                        ImportHint(kImportCompressNote);
                    }
                    if (!m.loadedNote.empty()) ImportHint(m.loadedNote.c_str());
                }
                Divider();
                UI::TextDisabled(kImportToolsHeading);
                ImportCombo("Write", st.ddsOut, kImportDdsOutNames, 2, res.ddsOutCombo);
                ImportCombo("Filter##ddstool", st.ddsFilter, kImportDdsFilterNames, 3, res.ddsFilterCombo);
                box("Colour picture (filter in linear light)", &st.ddsColour, res.ddsColour);
                box("Normal map", &st.ddsNormal, res.ddsNormal);
                box("Cut-out (keep see-through share)", &st.ddsCutout, res.ddsCutout);
                if (UI::Button("Generate mipmaps / compress")) res.intent.generate = true;
                res.generate = UI::GetItemRect();
                ImportHint("Writes a .dds next to the picture. Open it in any DDS viewer.");
                if (!m.toolMessage.empty()) UI::TextWrapped(m.toolMessage.c_str());
                break;
            }
            case ImportModel: {
                UI::SetNextItemWidth(WithLabel(160.0f, "Scale"));
                if (UI::DragFloat("Scale", &e->scale, 0.01f, 0.0001f, 10000.0f, "%.4f")) res.intent.changed = true;
                res.scale = UI::GetItemRect();
                ImportHint("Multiplies its size when you add it to a scene.");
                if (ImportCombo("Normals", e->normals, kImportNormalsNames, 3, res.normalsCombo)) res.intent.changed = true;
                if (e->normals == 1) {
                    UI::SetNextItemWidth(WithLabel(160.0f, "Smoothing angle"));
                    if (UI::DragFloat("Smoothing angle", &e->smoothingAngle, 0.5f, 0.0f, 180.0f, "%.0f deg")) res.intent.changed = true;
                    res.smoothing = UI::GetItemRect();
                    ImportHint("Sharper edges stay hard. Not for rigged parts.");
                } else if (e->normals == 2) {
                    ImportHint("Each face shaded alone. Not for rigged parts.");
                }
                if (box("Import animations", &e->importAnimations, res.animations)) res.intent.changed = true;
                ImportHint("Off: no clips, rest pose.");
                ImportHint("Used when the model is next added to a scene.");
                break;
            }
            case ImportAudio: {
                if (ImportCombo("Loading", e->loadMode, kImportLoadNames, 2, res.loadCombo)) res.intent.changed = true;
                ImportHint("Read while it plays keeps a long file in a small fixed amount of memory.");
                if (box("Mix down to one channel", &e->forceMono, res.mono)) res.intent.changed = true;
                ImportHint("Left and right become a single channel. Off keeps the recording as it is.");
                ImportHint("Used the next time the file is loaded.");
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
        box("Anti-aliasing (edge smoothing)", &p.fxaa, &res.fxaa);
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
            // Plain number fields wide enough for five digits at any text size, with no -/+ buttons (editor/main.cpp: at 110 pixels the
            // buttons took the room and 1280 x 720 showed as "12" and "72").
            const float numW = UI::CalcTextWidth("000000") + 2.0f * g_metrics.framePadding.x;
            UI::SetNextItemWidth(WithLabel(numW, "Width"));
            if (UI::InputInt("Width##ps", m.width, 0)) res.intent.changed = true;
            res.width = UI::GetItemRect();
            UI::SameLine();
            UI::SetNextItemWidth(WithLabel(numW, "Height"));
            if (UI::InputInt("Height##ps", m.height, 0)) res.intent.changed = true;
            res.height = UI::GetItemRect();
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
                UI::DrawChevron(dl, UI::Vec2{origin.x + 2.0f + fs * 0.21f, cy}, fs * 0.42f, 1, hc);
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
                    const float acx = rx + 12.0f + fs * 0.5f, acy = ry + fs * 0.15f + fs * 0.5f * 0.70f;
                    UI::DrawChevron(dl, UI::Vec2{acx, acy}, fs * 0.45f, row.open ? 1 : 0, tc);
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


// ---- Pieces the Project Settings and tool windows share -----------------------------------------------------------------------
// (Round 6: the library's own Inspector views that stood here, stage 1 and 2 of docs/ui/own_ui_toolkit.md sections 14 to 23, were
// deleted: the editor draws every component with its one body since round 5, so they had no caller. These three are what the Loading
// screen, Map Humanoid Bones and Material windows below still use.)

namespace {

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

} // namespace


// ---- Loading screen (Project Settings) -----------------------------------------------------------------------------------------

LoadingScreenResult DrawLoadingScreen(LoadingScreenModel& m, float x, float y, float w, float h) {
    LoadingScreenResult res;
    UI::BareNextWindow();
    if (UI::Begin("LoadingScreen##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        const float top = UI::GetCursorScreenPos().y;
        {   // two lines of one text item (OkayUI's TextDisabled draws one line: split at the line break, no spacing between the lines)
            const std::string note = kLoadingScreenNote;
            std::size_t a = 0;
            UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{g_metrics.itemSpacing.x, 0.0f});
            for (std::size_t b; (b = note.find('\n', a)) != std::string::npos; a = b + 1) UI::TextDisabled(note.substr(a, b - a).c_str());
            UI::PopStyleVar();
            UI::TextDisabled(note.substr(a).c_str());
        }
        if (m.logo && m.logoSize > 0) {
            m.logo[m.logoSize - 1] = '\0';
            UI::SetNextItemWidth(360.0f);
            if (UI::InputTextWithHint("##pssplashlogo", kSplashLogoHint, m.logo, (int)std::min<std::size_t>(m.logoSize, 1u << 20))) res.intent.changed = true;
            res.logo = UI::GetItemRect();
            UI::SameLine();
            if (UI::Button("...##pssplash")) res.intent.browse = true;
            res.browse = UI::GetItemRect();
            if (UI::IsItemHovered()) UI::SetTooltip("%s", "Browse...");
            UI::SameLine();
            UI::PushID("pssplash");   // LineButton measures its whole label: the id goes on the stack, not after "##"
            if (LineButton("Clear")) { if (m.logo[0]) res.intent.changed = true; m.logo[0] = '\0'; }
            UI::PopID();
            res.clear = UI::GetItemRect();
        }
        // a colour row 220 pixels wide (Dear ImGui's SetNextItemWidth(220) before ColorEdit3)
        auto colour = [&](const char* label, float* c, UI::Rect* rect) {
            const float was = g_metrics.itemWidth;
            g_metrics.itemWidth = 220.0f;
            if (InspColorEdit(label, c, 3, rect)) res.intent.changed = true;
            g_metrics.itemWidth = was;
        };
        if (m.accentOverride) {
            if (UI::Checkbox("Custom splash accent##ps", m.accentOverride)) res.intent.changed = true;
            res.accentBox = UI::GetItemRect();
            if (*m.accentOverride && m.accent) { UI::SameLine(); colour("##pssplashaccent", m.accent, &res.accent); }
        }
        if (m.bgTop) colour("Background top##ps", m.bgTop, &res.bgTop);
        if (m.bgBottom) colour("Background bottom##ps", m.bgBottom, &res.bgBottom);
        if (m.showBar) {
            if (UI::Checkbox("Show progress bar##ps", m.showBar)) res.intent.changed = true;
            res.showBar = UI::GetItemRect();
        }
        res.contentHeight = ContentHeight(top);
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

// ---- the small tool windows (docs/ui/own_ui_toolkit.md section 28) ---------------------------------------------------------------

namespace {
const UI::Color32 kShortcutTitle = UI::RGBA(140, 204, 255);       // ImVec4(0.55, 0.80, 1.00): a section's title
const unsigned char kShortcutKeys[3] = {217, 230, 184};            // ImVec4(0.85, 0.90, 0.72): the keys
// The text of a hit or a row a fixed distance from the left of the list (Dear ImGui's SameLine(offset)).
void AtColumn(float offset) { UI::SameLine(); UI::SetCursorPosX(offset); }
} // namespace

ShortcutsResult DrawShortcuts(const std::vector<ShortcutSection>& all, ShortcutsState& st, float x, float y, float w, float h) {
    ShortcutsResult res;
    st.filter[sizeof(st.filter) - 1] = '\0';
    UI::BareNextWindow();
    if (UI::Begin("Shortcuts##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::SetNextItemWidth(UI::GetContentRegionAvail().x);
        UI::InputTextWithHint("##scfilter", "filter...", st.filter, (int)sizeof(st.filter));
        res.filter = UI::GetItemRect();
        Divider();
        UI::BeginChild("##shortcutlist", UI::Vec2{0.0f, 0.0f}, UI::Child_None);
        for (const ShortcutSection& sec : FilterShortcuts(all, st.filter)) {
            UI::PushStyleColor(UI::Col_Text, kShortcutTitle);
            UI::SeparatorText(sec.title);
            UI::PopStyleColor();
            res.titles.push_back(UI::GetItemRect());
            for (const ShortcutRow& r : sec.rows) {
                UI::TextColored(kShortcutKeys[0], kShortcutKeys[1], kShortcutKeys[2], r.keys);
                res.keys.push_back(UI::GetItemRect());
                AtColumn(150.0f);
                UI::TextDisabled(r.what);
                res.whats.push_back(UI::GetItemRect());
            }
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

NetSimResult DrawNetSim(NetSimModel& m, float x, float y, float w, float h) {
    NetSimResult res;
    UI::BareNextWindow();
    if (UI::Begin("Net##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        const float itemW = ItemWidthFor(w);
        bool& ch = res.intent.changed;
        if (m.enabled) {
            if (FrameCheckbox("Simulate", m.enabled)) ch = true;
            res.simulate = UI::GetItemRect();
            UI::SameLine();
            UI::TextDisabled(kNetSimOffNote);
        }
        auto slider = [&](const char* label, float* v, float scale, float hi, const char* fmt, UI::Rect& rect) {
            if (!v) return;
            float shown = *v * scale;   // loss and duplicate are fractions, shown as percentages
            UI::SetNextItemWidth(WithLabel(itemW, label));
            if (UI::SliderFloat(label, &shown, 0.0f, hi, fmt)) { *v = shown / scale; ch = true; }
            rect = UI::GetItemRect();
        };
        slider("Latency (ms)", m.latencyMs, 1.0f, 500.0f, "%.0f", res.latency);
        slider("Jitter (ms)", m.jitterMs, 1.0f, 200.0f, "%.0f", res.jitter);
        slider("Loss (%)", m.loss, 100.0f, 50.0f, "%.1f", res.loss);
        slider("Duplicate (%)", m.duplicate, 100.0f, 50.0f, "%.1f", res.duplicate);
        ClampNetSim(m);
        Divider();
        UI::Text(NetWireText(m).c_str());
        res.wire = UI::GetItemRect();
        UI::TextDisabled(kNetTrafficNote);
        if (UI::BeginTable("nettraffic", 3, UI::Table_RowBg | UI::Table_NoResize | UI::Table_ScrollY, UI::Vec2{0.0f, 220.0f})) {
            UI::TableSetupColumn("Object");
            UI::TableSetupColumn("Msg/s", UI::Col_WidthFixed, 70.0f);
            UI::TableSetupColumn("B/s", UI::Col_WidthFixed, 80.0f);
            UI::TableHeadersRow();
            char b[32];
            for (const NetTrafficRow& r : m.rows) {
                UI::TableNextRow();
                UI::TableSetColumnIndex(0);
                UI::TextUnformatted(r.name.c_str());
                res.rows.push_back(UI::GetItemRect());
                UI::TableSetColumnIndex(1);
                std::snprintf(b, sizeof b, "%.1f", r.messagesPerSec);
                UI::TextUnformatted(b);
                UI::TableSetColumnIndex(2);
                std::snprintf(b, sizeof b, "%.0f", r.bytesPerSec);
                UI::TextUnformatted(b);
            }
            UI::EndTable();
        }
        res.table = UI::GetItemRect();
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

FindInFilesResult DrawFindInFiles(const FindInFilesModel& m, FindInFilesState& st, float x, float y, float w, float h) {
    FindInFilesResult res;
    st.query[sizeof(st.query) - 1] = '\0';
    UI::BareNextWindow();
    if (UI::Begin("FindInFiles##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        if (m.focusQuery) UI::SetKeyboardFocusHere();
        const float buttonW = 80.0f;
        UI::SetNextItemWidth(std::max(UI::GetContentRegionAvail().x - buttonW - g_metrics.itemSpacing.x, 40.0f));
        bool go = UI::InputTextWithHint("##fifq", "Search every project script...", st.query, (int)sizeof(st.query), UI::InputText_EnterReturnsTrue);
        res.query = UI::GetItemRect();
        UI::SameLine();
        if (UI::Button("Search", UI::Vec2{buttonW, 0.0f})) go = true;
        res.search = UI::GetItemRect();
        go |= UI::Checkbox("Match case", &st.matchCase);
        res.matchCase = UI::GetItemRect();
        UI::SameLine();
        go |= UI::Checkbox("Whole word", &st.wholeWord);
        res.wholeWord = UI::GetItemRect();
        UI::SameLine();
        go |= UI::Checkbox("Regex", &st.regex);
        res.regex = UI::GetItemRect();
        if (UI::IsItemHovered()) UI::SetTooltip("A regular expression (ECMAScript); the replacement can use $1, $2 and $&.");
        if (go && st.query[0]) res.intent.search = true;
        const float replaceW = 160.0f;
        UI::SetNextItemWidth(std::max(UI::GetContentRegionAvail().x - replaceW - g_metrics.itemSpacing.x, 40.0f));
        UI::InputTextWithHint("##fifr", "Replace with...", st.replaceWith, (int)sizeof(st.replaceWith));
        res.replaceField = UI::GetItemRect();
        UI::SameLine();
        UI::BeginDisabled(!FindInFilesCanReplace(m, st));
        if (UI::Button("Replace in All Files", UI::Vec2{replaceW, 0.0f}) && FindInFilesCanReplace(m, st)) res.intent.replaceAll = true;
        res.replace = UI::GetItemRect();
        UI::EndDisabled();
        Divider();
        if (!m.error.empty()) {
            UI::TextColored(kBad[0], kBad[1], kBad[2], m.error.c_str());
        } else {
            UI::TextDisabled(FindInFilesStatus(m, st.query).c_str());
        }
        res.status = UI::GetItemRect();
        UI::BeginChild("##fifres", UI::Vec2{0.0f, 0.0f}, UI::Child_None);
        res.list = UI::Rect{UI::GetCursorScreenPos().x, UI::GetCursorScreenPos().y, UI::GetContentRegionAvail().x, UI::GetContentRegionAvail().y};
        MenuLook look_(0.0f);   // the header colours of a hovered row
        for (std::size_t i = 0; i < m.hits.size(); ++i) {
            const FindHit& hit = m.hits[i];
            UI::PushID((int)i);
            if (UI::Selectable(FindHitLabel(hit).c_str(), false, UI::Selectable_Header, UI::Vec2{0.0f, UI::GetFontSize()})) res.intent.open = (int)i;   // one text line, as Dear ImGui's
            res.hits.push_back(UI::GetItemRect());
            if (UI::IsItemHovered()) UI::SetTooltip("%s", hit.path.c_str());
            AtColumn(180.0f);
            UI::TextDisabled(FindHitPreview(hit).c_str());
            UI::PopID();
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

DataAssetResult DrawDataAsset(DataAssetModel& m, float x, float y, float w, float h) {
    DataAssetResult res;
    UI::BareNextWindow();
    if (UI::Begin("DataAsset##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::TextDisabled(m.path.c_str());
        UI::TextDisabled(kDataAssetScriptNote);
        Divider();
        if (m.fields) {
            for (std::size_t i = 0; i < m.fields->size(); ++i) {
                std::pair<std::string, std::string>& f = (*m.fields)[i];
                UI::PushID((int)i);
                char k[kDataKeySize]; std::snprintf(k, sizeof k, "%s", f.first.c_str());
                UI::SetNextItemWidth(140.0f);
                if (UI::InputText("##k", k, (int)sizeof k)) { f.first = k; res.intent.changed = true; }
                res.keys.push_back(UI::GetItemRect());
                UI::SameLine();
                char v[kDataValueSize]; std::snprintf(v, sizeof v, "%s", f.second.c_str());
                UI::SetNextItemWidth(200.0f);
                if (UI::InputText("##v", v, (int)sizeof v)) { f.second = v; res.intent.changed = true; }
                res.values.push_back(UI::GetItemRect());
                UI::SameLine();
                if (LineButton("X")) res.intent.remove = (int)i;
                res.removes.push_back(UI::GetItemRect());
                UI::PopID();
            }
        }
        if (UI::Button("Add Field")) res.intent.add = true;
        res.add = UI::GetItemRect();
        UI::SameLine();
        if (UI::Button("Save")) res.intent.save = true;
        res.save = UI::GetItemRect();
    }
    UI::End();
    return res;
}

PluginManagerResult DrawPluginManager(const PluginManagerModel& m, float x, float y, float w, float h) {
    PluginManagerResult res;
    UI::BareNextWindow();
    if (UI::Begin("PluginManager##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        if (UI::Button("+ New Plugin")) res.intent.newPlugin = true;
        res.newPlugin = UI::GetItemRect();
        UI::SameLine();
        if (UI::Button("Reload All")) res.intent.reloadAll = true;
        res.reloadAll = UI::GetItemRect();
        UI::SameLine();
        if (UI::Button("Open Folder")) res.intent.openFolder = true;
        res.openFolder = UI::GetItemRect();
        UI::PushTextWrapPos(0.0f);
        UI::TextDisabled(kPluginManagerNote);
        UI::PopTextWrapPos();
        Divider();
        if (m.rows.empty()) { UI::TextDisabled(kPluginManagerEmpty); res.empty = true; }
        if (UI::BeginTable("##plugins", 4, UI::Table_RowBg | UI::Table_NoResize)) {
            UI::TableSetupColumn("On", UI::Col_WidthFixed, 34.0f);
            UI::TableSetupColumn("Plugin", 0, 3.0f);
            UI::TableSetupColumn("Scope", 0, 0.9f);
            UI::TableSetupColumn("", 0, 1.4f);
            UI::TableHeadersRow();
            for (std::size_t i = 0; i < m.rows.size(); ++i) {
                const PluginRow& p = m.rows[i];
                UI::PushID((int)i);
                UI::TableNextRow();
                UI::TableNextColumn();
                bool on = p.enabled;
                if (UI::Checkbox("##en", &on)) res.intent.toggle = (int)i;
                res.switches.push_back(UI::GetItemRect());
                UI::TableNextColumn();
                UI::TextUnformatted(p.name.c_str());
                res.names.push_back(UI::GetItemRect());
                if (!p.error.empty()) UI::TextColored(242, 115, 107, p.error.c_str());   // ImVec4(0.95, 0.45, 0.42)
                else if (!p.description.empty()) UI::TextDisabled(p.description.c_str());
                UI::TableNextColumn();
                UI::TextDisabled(PluginScopeText(p));
                UI::TableNextColumn();
                if (LineButton("Edit")) res.intent.edit = (int)i;
                res.edits.push_back(UI::GetItemRect());
                UI::Rect oc;
                if (p.hasWindow) {
                    UI::SameLine();
                    if (LineButton(p.windowOpen ? "Close" : "Open")) res.intent.openClose = (int)i;
                    oc = UI::GetItemRect();
                }
                res.openCloses.push_back(oc);
                UI::PopID();
            }
            UI::EndTable();
        }
        res.table = UI::GetItemRect();
    }
    UI::End();
    return res;
}

// ---- more tool windows: Map Humanoid Bones, Save Manager, Material, Frame time, Input (docs/ui/own_ui_toolkit.md section 29) -------

namespace {
// A list box `boxW` wide with `count` rows (`label(k)` names row k), `current` marked; at most 8 rows show at a time (ListWindow).
// Returns the row picked this frame (-1: none); `box` gets the box, `rows` the open list's row rectangles, `popup` the list.
template <class Label>
int PickList(const char* id, const char* preview, float boxW, int count, int current, Label label, UI::Rect& box, std::vector<UI::Rect>& rows,
             UI::Rect& popup) {
    MenuLook look_(0.0f);
    UI::PushStyleVar(UI::StyleVar_PopupWidth, boxW);
    UI::SetNextItemWidth(boxW);
    int picked = -1;
    const bool open = UI::BeginCombo(id, preview);
    box = UI::GetItemRect();
    if (open) {
        rows.assign((std::size_t)count, UI::Rect{});
        ListWindow(id, count, current, [&](int k) {
            UI::PushID(k);
            if (ListRow(label(k), k == current)) picked = k;
            rows[(std::size_t)k] = UI::GetItemRect();
            UI::PopID();
        });
        UI::EndCombo();
        float y0 = 1e30f, y1 = -1e30f, x0 = 0.0f;   // the rows in view (out of view they are zero size)
        for (const UI::Rect& r : rows) if (r.h > 0.0f) { x0 = r.x; y0 = std::min(y0, r.y); y1 = std::max(y1, r.y + r.h); }
        if (y1 > y0) popup = UI::Rect{x0, y0, boxW, y1 - y0};
    }
    UI::PopStyleVar();
    return picked;
}
const unsigned char kGood[3] = {115, 217, 140};                  // ImVec4(0.45, 0.85, 0.55)
const unsigned char kAmber[3] = {242, 153, 89};                  // ImVec4(0.95, 0.60, 0.35)
const unsigned char kUnsaved[3] = {242, 204, 89};                // ImVec4(0.95, 0.80, 0.35)
const UI::Color32 kDanger = UI::RGBA(166, 56, 56), kDangerHovered = UI::RGBA(204, 71, 71);   // (0.65, 0.22, 0.22) and (0.80, 0.28, 0.28)
// The right edge of the last item, for a body that measures how wide it is.
void Widest(float& right) { right = std::max(right, UI::GetItemRectMax().x); }
} // namespace

BoneMapResult DrawBoneMap(BoneMapModel& m, float x, float y, float w, float h) {
    BoneMapResult res;
    UI::BareNextWindow();
    if (UI::Begin("BoneMap##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        if (!m.hasModel || !m.picks) {
            UI::TextDisabled(kBoneMapNoModel);
            res.empty = true;
        } else {
            UI::BeginChild("##bonemapbody", UI::Vec2{0.0f, 0.0f}, UI::Child_None);
            UI::Text(("Model: " + m.modelName).c_str());
            UI::TextDisabled(kBoneMapNote);
            // the slot names in the first column (as wide as the longest), the list boxes filling the rest
            float nameW = 0.0f;
            for (const char* n : kBoneSlotNames) nameW = std::max(nameW, UI::CalcTextWidth(n));
            const float boxW = std::max(UI::GetContentRegionAvail().x - nameW - 2.0f * g_metrics.itemSpacing.x, 60.0f);
            if (UI::BeginTable("##bonemap", 2, UI::Table_RowBg | UI::Table_NoResize)) {
                UI::TableSetupColumn("slot", UI::Col_WidthFixed, nameW + g_metrics.itemSpacing.x);
                UI::TableSetupColumn("bone");
                for (int b = 0; b < kBoneSlotCount; ++b) {
                    UI::PushID(b);
                    UI::TableNextRow();
                    UI::TableNextColumn();
                    UI::AlignTextToFramePadding();
                    UI::TextUnformatted(kBoneSlotNames[b]);
                    UI::TableNextColumn();
                    std::string& pick = m.picks[b];
                    const std::string autoText = "Auto (" + m.autoName[b] + ")";
                    int current = pick.empty() ? 0 : pick == "-" ? 1 : -1;
                    for (std::size_t k = 0; current < 0 && k < m.bones.size(); ++k) if (m.bones[k] == pick) current = (int)k + 2;
                    UI::Rect slot;
                    const int picked = PickList("##bm", BonePickText(pick, m.autoName[b]).c_str(), boxW, (int)m.bones.size() + 2, current,
                        [&](int k) { return k == 0 ? autoText.c_str() : k == 1 ? "None" : m.bones[(std::size_t)k - 2].c_str(); },
                        slot, res.listRows, res.popup);
                    res.slots.push_back(slot);
                    if (picked >= 0 && picked != current) {
                        pick = picked == 0 ? std::string() : picked == 1 ? std::string("-") : m.bones[(std::size_t)picked - 2];
                        res.intent.changed = true;
                    }
                    UI::PopID();
                }
                UI::EndTable();
            }
            const std::string status = BoneMapStatus(m);
            if (m.humanoid) UI::TextColored(kGood[0], kGood[1], kGood[2], status.c_str());
            else UI::TextColored(kAmber[0], kAmber[1], kAmber[2], status.c_str());
            res.status = UI::GetItemRect();
            if (UI::Button("Clear (all Auto)")) {
                for (int b = 0; b < kBoneSlotCount; ++b) m.picks[b].clear();
                res.intent.changed = true;
            }
            res.clear = UI::GetItemRect();
            UI::SameLine();
            UI::BeginDisabled(!m.humanoid);
            if (UI::Button(m.forRetarget ? "Apply to Humanoid Retarget" : "Use Built-in Animations") && m.humanoid) res.intent.apply = true;
            res.apply = UI::GetItemRect();
            UI::EndDisabled();
            UI::EndChild();
        }
    }
    UI::End();
    return res;
}

SaveManagerResult DrawSaveManager(const SaveManagerModel& m, SaveManagerState& st, float x, float y, float w, float h) {
    SaveManagerResult res;
    st.filter[sizeof(st.filter) - 1] = '\0';
    st.newKey[sizeof(st.newKey) - 1] = '\0';
    st.newValue[sizeof(st.newValue) - 1] = '\0';
    if (st.newType < 0 || st.newType >= kSaveTypeCount) st.newType = 0;
    UI::BareNextWindow();
    if (UI::Begin("SaveManager##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::BeginChild("##savebody", UI::Vec2{0.0f, 0.0f}, UI::Child_None);
        UI::TextDisabled(kSaveManagerNote);
        // the file list, with Reload beside it once a file is chosen
        const bool chosen = !m.selected.empty();
        const float reloadW = UI::CalcTextWidth("Reload") + 24.0f;
        const float boxW = std::max(UI::GetContentRegionAvail().x - (chosen ? reloadW + g_metrics.itemSpacing.x : 0.0f), 60.0f);
        int current = -1;
        for (std::size_t i = 0; i < m.files.size(); ++i) if (m.files[i] == m.selected) current = (int)i;
        const int picked = PickList("##savefile", chosen ? m.selected.c_str() : "(choose a save file)", boxW, (int)m.files.size(), current,
                                    [&](int k) { return m.files[(std::size_t)k].c_str(); }, res.fileCombo, res.listRows, res.popup);
        if (picked >= 0) res.intent.choose = picked;
        if (!chosen) {
            UI::TextDisabled(kSaveManagerNoFile);
            res.noFile = true;
        } else {
            UI::SameLine();
            if (LineButton("Reload")) res.intent.reload = true;
            res.reload = UI::GetItemRect();
            UI::SetNextItemWidth(UI::GetContentRegionAvail().x);
            UI::InputTextWithHint("##savefilter", "filter keys...", st.filter, (int)sizeof(st.filter));
            res.filter = UI::GetItemRect();
            Divider();
            // the keys: name, type, the value as typed text (stored again with its type on a change) and X
            if (UI::BeginTable("savekeys", 3, UI::Table_RowBg | UI::Table_BordersInnerH | UI::Table_BordersOuter | UI::Table_NoResize)) {
                UI::TableSetupColumn("Key", UI::Col_WidthStretch, 0.4f);
                UI::TableSetupColumn("Type", UI::Col_WidthFixed, 70.0f);
                UI::TableSetupColumn("Value", UI::Col_WidthStretch, 0.6f);
                UI::TableHeadersRow();
                for (std::size_t i = 0; i < m.rows.size(); ++i) {
                    const SaveKeyRow& r = m.rows[i];
                    if (!SaveKeyMatches(r.key, st.filter)) continue;
                    UI::PushID(r.key.c_str());
                    UI::TableNextRow();
                    UI::TableNextColumn();
                    UI::AlignTextToFramePadding();
                    UI::TextUnformatted(r.key.c_str());
                    UI::TableNextColumn();
                    UI::AlignTextToFramePadding();
                    UI::TextDisabled(SaveTypeName(r.type));
                    UI::TableNextColumn();
                    char v[256];
                    std::snprintf(v, sizeof v, "%s", r.payload.c_str());
                    UI::SetNextItemWidth(std::max(UI::GetContentRegionAvail().x - 44.0f, 20.0f));
                    if (UI::InputText("##v", v, (int)sizeof v)) { res.intent.edit = (int)i; res.intent.editRaw = SaveRawValue(r.type, v); }
                    res.rowIndex.push_back((int)i);
                    res.values.push_back(UI::GetItemRect());
                    UI::SameLine();
                    if (LineButton("X")) res.intent.remove = (int)i;
                    res.removes.push_back(UI::GetItemRect());
                    UI::PopID();
                }
                UI::EndTable();
            }
            res.table = UI::GetItemRect();
            // a new key: its name, its type and its value
            UI::Spacing(0.0f);
            UI::SetNextItemWidth(140.0f);
            UI::InputTextWithHint("##nk", "new key", st.newKey, (int)sizeof(st.newKey));
            res.newKey = UI::GetItemRect();
            UI::SameLine();
            std::vector<UI::Rect> typeRows;
            UI::Rect typePopup;
            const int t = PickList("##nt", kSaveTypeNames[st.newType], 90.0f, kSaveTypeCount, st.newType,
                                   [&](int k) { return kSaveTypeNames[k]; }, res.newType, typeRows, typePopup);
            if (!typeRows.empty()) { res.listRows = typeRows; res.popup = typePopup; }
            if (t >= 0) st.newType = t;
            UI::SameLine();
            UI::SetNextItemWidth(140.0f);
            UI::InputText("##nv", st.newValue, (int)sizeof(st.newValue));
            res.newValue = UI::GetItemRect();
            UI::SameLine();
            if (LineButton("Add") && st.newKey[0]) {
                res.intent.add = true;
                res.intent.addKey = st.newKey;
                res.intent.addRaw = SaveRawValue(kSaveTypeCodes[st.newType], st.newValue);
                st.newKey[0] = '\0';
            }
            res.add = UI::GetItemRect();
            Divider();
            if (UI::Button("Save to disk")) res.intent.save = true;
            res.save = UI::GetItemRect();
            UI::SameLine();
            if (UI::Button("Clear all keys")) UI::OpenPopup("Clear save keys?");
            res.clearAll = UI::GetItemRect();
            {
                MenuLook look_(0.0f);
                if (UI::BeginPopupModal("Clear save keys?")) {
                    char q[64];
                    std::snprintf(q, sizeof q, "Remove all %d keys from this save?", (int)m.rows.size());
                    UI::Text(q);
                    UI::TextDisabled(kSaveClearNote);
                    UI::Dummy(0.0f, 4.0f);
                    UI::PushStyleColor(UI::Col_Button, kDanger);
                    UI::PushStyleColor(UI::Col_ButtonHovered, kDangerHovered);
                    if (UI::Button("Clear", UI::Vec2{110.0f, 0.0f})) { res.intent.clearAll = true; UI::CloseCurrentPopup(); }
                    res.confirmClear = UI::GetItemRect();
                    UI::PopStyleColor(2);
                    UI::SameLine();
                    if (UI::Button("Cancel", UI::Vec2{110.0f, 0.0f})) UI::CloseCurrentPopup();
                    res.cancelClear = UI::GetItemRect();
                    UI::EndPopup();
                }
            }
            UI::SameLine();
            char n[32];
            std::snprintf(n, sizeof n, "%d keys", (int)m.rows.size());
            UI::TextDisabled(n);
            res.count = UI::GetItemRect();
            if (m.dirty) { UI::SameLine(); UI::TextColored(kUnsaved[0], kUnsaved[1], kUnsaved[2], "unsaved edits"); }
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

MaterialResult DrawMaterial(MaterialModel& m, float x, float y, float w, float h) {
    MaterialResult res;
    UI::BareNextWindow();
    if (UI::Begin("Material##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        UI::BeginChild("##matbody", UI::Vec2{0.0f, 0.0f}, UI::Child_None);
        const float itemW = ItemWidthFor(w);
        bool& ch = res.intent.changed;
        UI::TextDisabled(m.path.c_str());
        Divider();
        {   // the preview ball: the albedo brightened by the emissive, a soft light spot, the specular glint
            const UI::Vec2 p = UI::GetCursorScreenPos();
            const float pw = UI::GetContentRegionAvail().x, ph = 74.0f;
            const UI::DrawList& dl = UI::GetWindowDrawList();
            dl.AddRectFilled(p, UI::Vec2{p.x + pw, p.y + ph}, UI::RGBA(28, 29, 34), 5.0f);
            float c[3]; MaterialPreviewColor(m, c);
            const MaterialBall b = MaterialBallIn(p.x, p.y, p.x + pw, p.y + ph);
            dl.AddCircleFilled(UI::Vec2{b.cx, b.cy}, b.r, C32(c));
            dl.AddCircleFilled(UI::Vec2{b.spotX, b.spotY}, b.spotR, UI::RGBA(255, 255, 255, 70));
            dl.AddCircle(UI::Vec2{b.cx, b.cy}, b.r, UI::RGBA(0, 0, 0, 90), 0, 1.5f);
            if (m.specular > 0.0f)
                dl.AddCircleFilled(UI::Vec2{b.glintX, b.glintY}, b.glintR, UI::RGBA(255, 255, 255, (unsigned char)std::clamp(120.0f + 130.0f * m.specular, 0.0f, 255.0f)));
            res.preview = UI::Rect{p.x, p.y, pw, ph};
            UI::Dummy(pw, ph + 6.0f);
        }
        if (InspColorEdit("Albedo", m.color, 4, &res.albedo)) ch = true;
        if (InspColorEdit("Emissive", m.emissive, 3, &res.emissive)) ch = true;
        UI::SetNextItemWidth(WithLabel(itemW, "Specular"));
        if (UI::SliderFloat("Specular", &m.specular, 0.0f, 1.0f, "%.3f")) ch = true;
        res.specular = UI::GetItemRect();
        if (m.specular > 0.0f) {
            UI::SetNextItemWidth(WithLabel(itemW, "Shininess"));
            if (UI::SliderFloat("Shininess", &m.shininess, 1.0f, 128.0f, "%.3f")) ch = true;
            res.shininess = UI::GetItemRect();
        }
        if (FrameCheckbox("Unlit", &m.unlit)) ch = true;
        res.unlit = UI::GetItemRect();
        UI::SameLine();
        if (FrameCheckbox("Double-sided", &m.doubleSided)) ch = true;
        res.doubleSided = UI::GetItemRect();
        char tex[256];
        std::snprintf(tex, sizeof tex, "%s", m.texture.c_str());
        UI::SetNextItemWidth(210.0f);
        if (UI::InputText("##mattex", tex, (int)sizeof tex)) { m.texture = tex; ch = true; }
        res.texture = UI::GetItemRect();
        UI::SameLine(0.0f, 4.0f);
        if (UI::Button("...##mattexbrowse")) res.intent.browse = true;
        res.browse = UI::GetItemRect();
        if (UI::IsItemHovered()) UI::SetTooltip("%s", "Browse...");
        UI::SameLine(0.0f, 4.0f);
        UI::AlignTextToFramePadding();
        UI::TextUnformatted("Texture");
        UI::SetNextItemWidth(WithLabel(itemW, "Tiling"));
        if (UI::DragFloat2("Tiling", m.tiling, 0.05f, 0.01f, 64.0f, "%.3f")) ch = true;
        res.tiling = UI::GetItemRect();
        Divider();
        if (UI::Button("Save")) res.intent.save = true;
        res.save = UI::GetItemRect();
        UI::SameLine();
        UI::BeginDisabled(!m.canApply);
        if (UI::Button("Apply to Selection") && m.canApply) res.intent.apply = true;
        res.apply = UI::GetItemRect();
        UI::EndDisabled();
        if (!m.canApply) { UI::SameLine(); UI::TextDisabled(kMaterialApplyNote); }
        UI::EndChild();
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

PerfOverlayResult DrawPerfOverlay(const PerfOverlayModel& m, float x, float y, float w, float h) {
    PerfOverlayResult res;
    UI::BareNextWindow();
    if (UI::Begin("PerfOverlay##okayui", x, y, w, h)) {
        EditorRows rows_;
        float right = x;
        float c[3]; PerfFpsColor(m.fps, c);
        char fps[32]; std::snprintf(fps, sizeof fps, "%.0f FPS", m.fps);
        UI::TextColored(B(c[0]), B(c[1]), B(c[2]), fps);
        res.fps = UI::GetItemRect();
        UI::SameLine();
        UI::TextDisabled(PerfTimesText(m).c_str());
        Widest(right);
        const std::string sleep = PerfSleepText(m);
        if (!sleep.empty()) { UI::TextDisabled(sleep.c_str()); Widest(right); }
        if (m.collecting) { UI::TextDisabled("Collecting frames ..."); Widest(right); }
        if (!m.plot.empty()) {
            res.plot = FramedPlot("##perfms", m.plot, (int)m.plot.size(), PerfPlotCaption(m).c_str(), PerfPlotTop(m), 56.0f, 360.0f);
            right = std::max(right, res.plot.x + res.plot.w);
        }
        Divider();
        char where[96];
        std::snprintf(where, sizeof where, "Where the frame goes (average of the last %d frames)", m.breakFrames);
        UI::TextDisabled(where);
        Widest(right);
        for (const PerfSection& s : m.sections) {
            res.bars.push_back(FramedBar(s.share, PerfSectionBar(s).c_str(), UI::RGBA(230, 179, 0), 150.0f));   // Dear ImGui's histogram colour
            UI::SameLine();
            UI::AlignTextToFramePadding();
            UI::TextUnformatted(s.name.c_str());
            Widest(right);
        }
        Divider();
        UI::TextDisabled(("Renderer: " + m.renderer).c_str());
        Widest(right);
        UI::TextDisabled(PerfViewText(m).c_str());
        Widest(right);
        if (UI::Button("Copy report")) res.intent.copyReport = true;
        res.copyReport = UI::GetItemRect();
        UI::SameLine();
        if (UI::Button("Open Profiler")) res.intent.openProfiler = true;
        res.openProfiler = UI::GetItemRect();
        Widest(right);
        res.contentW = std::ceil(right - x);
        res.contentH = std::ceil(ContentHeight(y));
    }
    UI::End();
    return res;
}

InputPanelResult DrawInputPanel(InputPanelModel& m, InputPanelState& st, float x, float y, float w, float h) {
    InputPanelResult res;
    UI::BareNextWindow();
    if (UI::Begin("Input##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        MenuLook look_(0.0f);   // the header colours of the section bars
        UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, g_metrics.innerSpacing);
        UI::BeginChild("##inputbody", UI::Vec2{0.0f, 0.0f}, UI::Child_None);
        const float top = UI::GetCursorScreenPos().y;
        const float itemW = ItemWidthFor(w);
        auto lines = [](const std::vector<std::string>& v) { for (const std::string& l : v) UI::TextWrapped(l.c_str()); };
        if (!m.playing) UI::TextDisabled(kInputNotPlaying);
        bool open = UI::CollapsingHeader("Right now", true);
        res.headers[0] = UI::GetItemRect();
        if (open) lines(m.snapshot);
        open = UI::CollapsingHeader("Controllers and typed actions", true);
        res.headers[1] = UI::GetItemRect();
        if (open) {
            lines(m.controllers);
            for (const InputPanelModel::Player& p : m.players) {
                Divider();
                UI::Text(("Player " + std::to_string(p.number)).c_str());
                lines(p.lines);
            }
            if (m.players.empty()) UI::TextDisabled(kInputNoPlayer);
        }
        open = UI::CollapsingHeader("Last events", true);
        res.headers[2] = UI::GetItemRect();
        if (open) {
            if (LineButton("Clear")) res.intent.clearEvents = true;
            res.clear = UI::GetItemRect();
            UI::BeginChild("##inputevents", UI::Vec2{0.0f, 130.0f}, UI::Child_Borders);
            res.events = UI::Rect{UI::GetCursorScreenPos().x, UI::GetCursorScreenPos().y, UI::GetContentRegionAvail().x, UI::GetContentRegionAvail().y};
            for (const std::string& e : m.events) UI::TextUnformatted(e.c_str());
            UI::EndChild();
        }
        open = UI::CollapsingHeader("Record and replay", false);
        res.headers[3] = UI::GetItemRect();
        if (open) {
            UI::TextWrapped(kInputRecordNote);
            if (UI::Button("Record")) res.intent.record = true;
            res.record = UI::GetItemRect();
            UI::SameLine();
            if (UI::Button("Stop")) res.intent.stop = true;
            res.stop = UI::GetItemRect();
            UI::SameLine();
            if (UI::Button("Replay")) res.intent.replay = true;
            res.replay = UI::GetItemRect();
            UI::SameLine();
            if (UI::Button("Copy text")) res.intent.copyText = true;
            res.copyText = UI::GetItemRect();
            UI::Text(m.recordStatus.c_str());
        }
        open = UI::CollapsingHeader("Response curves", false);
        res.headers[4] = UI::GetItemRect();
        if (open) {
            if (st.action < 0 || st.action >= (int)m.actions.size()) st.action = 0;
            const char* name = m.actions.empty() ? "" : m.actions[(std::size_t)st.action].c_str();
            const float labelGap = g_metrics.innerSpacing.x;
            UI::Rect listRect;
            const int picked = PickList("##iaaction", name, itemW, (int)m.actions.size(), st.action,
                                        [&](int k) { return m.actions[(std::size_t)k].c_str(); }, res.action, res.listRows, listRect);
            UI::SameLine(0.0f, labelGap);
            UI::AlignTextToFramePadding();
            UI::TextUnformatted("Action");
            if (picked >= 0) st.action = picked;
            if (!m.actions.empty()) {
                InputResponse& r = m.response;
                bool& ch = res.intent.responseChanged;
                UI::SetNextItemWidth(WithLabel(itemW, "Dead zone"));
                if (UI::SliderFloat("Dead zone", &r.deadzone, 0.0f, 0.9f, "%.3f")) ch = true;
                res.deadzone = UI::GetItemRect();
                UI::SetNextItemWidth(WithLabel(itemW, "Curve"));
                if (UI::SliderFloat("Curve", &r.curve, 0.3f, 4.0f, "%.2f (1 = straight, 2 = gentle near centre)")) ch = true;
                res.curve = UI::GetItemRect();
                UI::SetNextItemWidth(WithLabel(itemW, "Sensitivity"));
                if (UI::SliderFloat("Sensitivity", &r.sensitivity, 0.1f, 5.0f, "%.3f")) ch = true;
                res.sensitivity = UI::GetItemRect();
                if (m.direction) {
                    if (FrameCheckbox("Invert X", &r.invertX)) ch = true;
                    res.invertX = UI::GetItemRect();
                    UI::SameLine();
                    if (FrameCheckbox("Invert Y", &r.invertY)) ch = true;
                    res.invertY = UI::GetItemRect();
                }
                if (LineButton("Reset curve")) res.intent.resetCurve = true;
                res.reset = UI::GetItemRect();
                std::vector<float> pts(64, 0.0f);
                if (m.shape) for (int i = 0; i < 64; ++i) pts[(std::size_t)i] = m.shape((float)i / 63.0f, r);
                res.plot = FramedPlot("##curve", pts, 64, kInputCurveCaption, std::max(1.0f, r.sensitivity), 90.0f);
                char now[48];
                std::snprintf(now, sizeof now, "Reads now: %.2f", m.value);
                UI::Text(now);
            }
        }
        res.contentHeight = ContentHeight(top);
        UI::EndChild();
        UI::PopStyleVar();
    }
    UI::End();
    return res;
}

// ---- Timers --------------------------------------------------------------------------------------------------------------------

TimersResult DrawTimers(const std::string& text, float x, float y, float w, float h) {
    TimersResult res;
    UI::BareNextWindow();
    if (UI::Begin("Timers##okayui", x, y, w, h)) {
        EditorRows rows_;
        UI::BeginChild("##timersbody", UI::Vec2{0.0f, 0.0f}, UI::Child_None);
        res.body = UI::Rect{UI::GetCursorScreenPos().x, UI::GetCursorScreenPos().y, UI::GetContentRegionAvail().x, UI::GetContentRegionAvail().y};
        for (const std::string& l : TextLines(text)) {
            UI::TextUnformatted(l.c_str());
            res.lines.push_back(UI::GetItemRect());
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

// ---- Input Actions -------------------------------------------------------------------------------------------------------------

namespace {
// Dear ImGui's BulletText in a text row: a small round dot in the first font-size column, the text after it, one text line tall
// (OkayUI's BulletText is a frame row tall).
void TextBullet(const char* s) {
    const UI::Vec2 p = UI::GetCursorScreenPos();
    const float fs = UI::GetFontSize();
    UI::Indent(fs);
    UI::TextUnformatted(s);
    UI::Unindent(fs);
    UI::GetWindowDrawList().AddCircleFilled(UI::Vec2{p.x + fs * 0.5f, p.y + fs * 0.5f}, fs * 0.15f, UI::GetColorU32(UI::Col_Text));
}
// A binding field's text while it is edited: the state's copy, which takes the model's text whenever the field is not being typed in.
// Enter hands the typed text to the host (it checks it; the field shows the model's text again once it lets the keyboard go).
bool IaField(const char* id, std::vector<char>& buf, const std::string& value, float width, std::string& entered) {
    if (buf.size() != kIaPartSize) buf.assign(kIaPartSize, '\0');
    buf.back() = '\0';
    UI::SetNextItemWidth(width);
    const bool enter = UI::InputText(id, buf.data(), (int)buf.size(), UI::InputText_EnterReturnsTrue);
    if (enter) entered = buf.data();
    else if (!UI::IsItemFocused()) std::snprintf(buf.data(), buf.size(), "%s", value.c_str());
    return enter;
}
} // namespace

InputActionsResult DrawInputActions(const InputActionsModel& m, InputActionsState& st, float x, float y, float w, float h) {
    InputActionsResult res;
    st.file[sizeof(st.file) - 1] = '\0';
    st.name[sizeof(st.name) - 1] = '\0';
    st.interaction[sizeof(st.interaction) - 1] = '\0';
    if (st.newType < 0 || st.newType > 3) st.newType = 0;
    UI::BareNextWindow();
    if (UI::Begin("InputActions##okayui", x, y, w, h)) {
        EditorRows rows_;
        FieldColors fields_;
        MenuLook look_(0.0f);   // the header colours of the selected and hovered action rows
        InputActionsIntent& in = res.intent;
        // ---- the file row ----
        UI::SetNextItemWidth(UI::GetContentRegionAvail().x);
        UI::InputText("##inputfile", st.file, (int)sizeof(st.file));
        res.file = UI::GetItemRect();
        if (UI::Button("Load")) in.load = true;
        res.load = UI::GetItemRect();
        UI::SameLine();
        if (UI::Button("Save")) in.save = true;
        res.save = UI::GetItemRect();
        UI::SameLine();
        if (UI::Button("New from default")) in.newDefault = true;
        res.newDefault = UI::GetItemRect();
        UI::SameLine();
        if (UI::Button("Migrate Input Actions")) in.migrate = true;
        res.migrate = UI::GetItemRect();
        UI::SameLine();
        if (UI::Button("Use in game")) in.useInGame = true;
        res.useInGame = UI::GetItemRect();
        if (m.dirty) { UI::SameLine(); UI::TextDisabled("(unsaved changes)"); res.unsaved = UI::GetItemRect(); }
        if (!st.status.empty()) { UI::TextDisabled(st.status.c_str()); res.status = UI::GetItemRect(); }
        Divider();

        // ---- left: schemes, maps and actions ----
        UI::BeginChild("##ia_left", UI::Vec2{UI::GetFontSize() * 21.0f, 0.0f}, UI::Child_Borders);
        res.left = UI::Rect{UI::GetCursorScreenPos().x, UI::GetCursorScreenPos().y, UI::GetContentRegionAvail().x, UI::GetContentRegionAvail().y};
        UI::TextDisabled("Schemes");
        for (const std::string& c : m.schemes) TextBullet(c.c_str());
        UI::SetNextItemWidth(UI::GetContentRegionAvail().x);
        UI::InputTextWithHint("##newname", "name (no spaces)", st.name, (int)sizeof(st.name));
        res.name = UI::GetItemRect();
        if (LineButton("+ Map")) in.addMap = true;
        res.addMap = UI::GetItemRect();
        UI::SameLine();
        if (LineButton("+ Scheme")) in.addScheme = true;
        res.addScheme = UI::GetItemRect();
        Divider();
        for (const InputActionMapRow& map : m.maps) {
            UI::Text(InputMapTitle(map).c_str());
            res.mapTitles.push_back(UI::GetItemRect());
            for (const InputActionRow& a : map.actions) {
                UI::PushID((map.name + "/" + a.name).c_str());
                if (UI::Selectable(InputActionLabel(a).c_str(), st.selected == a.name, UI::Selectable_Header, UI::Vec2{0.0f, UI::GetFontSize()})) {
                    in.select = true; in.selectName = a.name;
                }
                res.actions.push_back(UI::GetItemRect());
                UI::PopID();
            }
        }
        Divider();
        UI::SetNextItemWidth(110.0f);
        UI::Combo("##newtype", kIaNewTypes, 4, &st.newType);
        res.newType = UI::GetItemRect();
        UI::SameLine();
        if (LineButton("+ Action")) in.addAction = true;
        res.addAction = UI::GetItemRect();
        UI::EndChild();
        UI::SameLine();

        // ---- right: the Action, Check and Live tabs ----
        UI::BeginChild("##ia_right", UI::Vec2{0.0f, 0.0f}, UI::Child_Borders);
        res.right = UI::Rect{UI::GetCursorScreenPos().x, UI::GetCursorScreenPos().y, UI::GetContentRegionAvail().x, UI::GetContentRegionAvail().y};
        if (UI::BeginTabBar("##iatabs")) {
            const int want = st.forceTab;
            st.forceTab = -1;
            const bool actionTab = UI::BeginTabItem("Action", nullptr, want == 0 ? UI::TabItem_SetSelected : 0);
            res.tabs[0] = UI::GetItemRect();
            if (actionTab) {
                if (!m.hasAction) UI::TextDisabled(kIaPickAction);
                else {
                    for (const std::string& l : m.describe) UI::TextWrapped(l.c_str());
                    Divider();
                    const std::vector<std::size_t> off = InputPartOffsets(m);
                    if (st.partBufs.size() != off.back()) st.partBufs.resize(off.back());
                    if (st.procBufs.size() != m.bindings.size()) st.procBufs.resize(m.bindings.size());
                    for (std::size_t b = 0; b < m.bindings.size(); ++b) {
                        const InputBindingRow& bs = m.bindings[b];
                        UI::PushID((int)b);
                        UI::Text(("Binding " + std::to_string(b + 1)).c_str());
                        const float n = (float)std::max<std::size_t>(1, bs.parts.size());
                        for (std::size_t p = 0; p < bs.parts.size(); ++p) {
                            UI::PushID((int)p);
                            const float pw = std::max(70.0f, (UI::GetContentRegionAvail().x - g_metrics.itemSpacing.x * n) / n - 4.0f);
                            std::string typed;
                            if (IaField("##part", st.partBufs[off[b] + p], bs.parts[p], pw, typed) && in.partBinding < 0) {
                                in.partBinding = (int)b; in.partIndex = (int)p; in.partText = typed;
                            }
                            res.parts.push_back(UI::GetItemRect());
                            UI::PopID();
                            if (p + 1 < bs.parts.size()) UI::SameLine();
                        }
                        std::string typed;
                        if (IaField("##proc", st.procBufs[b], bs.processors, UI::GetContentRegionAvail().x * 0.5f, typed) && in.procBinding < 0) {
                            in.procBinding = (int)b; in.procText = typed;
                        }
                        res.processors.push_back(UI::GetItemRect());
                        UI::SameLine(0.0f, g_metrics.innerSpacing.x);
                        UI::TextUnformatted("processors");
                        UI::SameLine();
                        if (LineButton("Remove binding") && in.removeBinding < 0) in.removeBinding = (int)b;
                        res.removeBindings.push_back(UI::GetItemRect());
                        UI::PopID();
                    }
                    for (int i = 0; i < 5; ++i) {
                        if (i) UI::SameLine();
                        if (LineButton(kIaBindingKinds[i])) in.addBinding = i;
                        res.addBinding[i] = UI::GetItemRect();
                    }
                    Divider();
                    UI::SetNextItemWidth(UI::GetContentRegionAvail().x);
                    if (UI::InputTextWithHint("##inter", kIaInteractionHint, st.interaction, (int)sizeof(st.interaction), UI::InputText_EnterReturnsTrue)) {
                        in.addInteraction = true; in.interactionText = st.interaction;
                    }
                    res.interaction = UI::GetItemRect();
                    for (std::size_t i = 0; i < m.interactions.size(); ++i) {
                        UI::PushID(1000 + (int)i);
                        TextBullet(m.interactions[i].c_str());
                        res.interactions.push_back(UI::GetItemRect());
                        UI::SameLine();
                        if (LineButton("x") && in.removeInteraction < 0) in.removeInteraction = (int)i;
                        res.removeInteractions.push_back(UI::GetItemRect());
                        UI::PopID();
                    }
                    if (LineButton("Delete action")) in.deleteAction = true;
                    res.deleteAction = UI::GetItemRect();
                }
                UI::EndTabItem();
            }
            const bool checkTab = UI::BeginTabItem("Check", nullptr, want == 1 ? UI::TabItem_SetSelected : 0);
            res.tabs[1] = UI::GetItemRect();
            if (checkTab) {
                if (m.problems.empty()) { UI::TextDisabled(kIaNoProblems); res.problems.push_back(UI::GetItemRect()); }
                for (const std::string& p : m.problems) { TextBullet(p.c_str()); res.problems.push_back(UI::GetItemRect()); }
                UI::EndTabItem();
            }
            const bool liveTab = UI::BeginTabItem("Live", nullptr, want == 2 ? UI::TabItem_SetSelected : 0);
            res.tabs[2] = UI::GetItemRect();
            if (liveTab) {
                for (const std::string& l : m.controllers) { UI::TextWrapped(l.c_str()); res.liveLines.push_back(UI::GetItemRect()); }
                Divider();
                if (m.players.empty()) { UI::TextDisabled(kIaNoPlayer); res.liveLines.push_back(UI::GetItemRect()); }
                for (const InputActionsModel::Player& p : m.players) {
                    UI::Text(("Player " + std::to_string(p.number)).c_str());
                    res.liveLines.push_back(UI::GetItemRect());
                    for (const std::string& l : p.lines) { UI::TextWrapped(l.c_str()); res.liveLines.push_back(UI::GetItemRect()); }
                }
                UI::EndTabItem();
            }
            UI::EndTabBar();
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

} // namespace okayuiscreens
