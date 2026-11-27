// The editor's Problems, Stats, History, Tags and Layers, Physics, Profiler, Import settings, Post-processing, Player and Rendering
// panels, and the small tool windows (Keyboard Shortcuts, Net, Find in Files, Data Asset, Plugin Manager, Timers, Input Actions), drawn with OkayUI (okayui/screens/OkayUIEditorPanels), and the toolkit pieces they needed (docs/own_ui_toolkit.md sections 9
// to 11). Driven headless the way
// the editor drives them: every control is found by its rectangle in the result and used with synthetic input (clicks, typing,
// the wheel, right-click menus); golden images (launcher look) pin what is drawn. The Dear ImGui versions are compared against
// these in tests/test_okayui_editor_panels_imgui.cpp.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIEditorPanels.hpp"
#include "EditorPanelFixtures.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace S = okayuiscreens;
namespace P = okaypanels;

static void Check(const char* name, const Bitmap& b) {
    std::string why;
    const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << " (actual written as " << name << ".actual.png)\n";
    CHECK(ok);
}
template <class R> static float CX(const R& r) { return r.x + r.w * 0.5f; }
template <class R> static float CY(const R& r) { return r.y + r.h * 0.5f; }
static const int kBg[3] = {30, 33, 41};   // the editor's panel colour (Slate kBase)

// One rig drives a panel: a sim, the panel's draw call and click helpers. The panel fills (16, 12, W - 32, H - 24), the area
// inside the editor window's padding.
template <class Fn>
struct Rig {
    Sim sim;
    Fn draw;
    Rig(float w, float h, Fn d) : sim(w, h), draw(d) { Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
    ~Rig() { UI::UseLauncherLook(false); }
    void Frame() { sim.Frame([&] { UI::PushTextScale(S::kEditorTextScale); draw(16.0f, 12.0f, sim.W - 32.0f, sim.H - 24.0f); UI::PopTextScale(); }); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Click(const UI::Rect& r) { sim.Move(CX(r), CY(r)); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
    void RightClick(const UI::Rect& r) { sim.Move(CX(r), CY(r)); Frame(); sim.rdown = true; Frame(); sim.rdown = false; Frame(); }
    const Bitmap& Draw() { return sim.Draw((std::uint8_t)kBg[0], (std::uint8_t)kBg[1], (std::uint8_t)kBg[2]); }
};
template <class Fn> static Rig<Fn> MakeRig(float w, float h, Fn d) { return Rig<Fn>(w, h, d); }

int main() {
    RUN_SUITE("okayui editor panels");

    // ================= the shared model rules =================
    {
        const P::ProblemsModel m = panelfix::Problems();
        P::ProblemsState st;
        std::vector<P::ProblemGroup> g = P::GroupProblems(m, st);
        // errors first, then warnings; infos hidden by default; the duplicate Crate row shown once
        CHECK(g.size() == 6);
        CHECK(g[0].name == "Crate" && g[0].rows.size() == 1 && g[0].errors == 1);
        CHECK(g[1].name == "MissingScriptComponentFile" && g[2].name == "Player");
        CHECK(g[3].name == "Chaser" && g[3].warnings == 1 && g[3].errors == 0);
        CHECK(P::ProblemGroupHeader(g[0]) == "Crate    1  error" && P::ProblemGroupHeader(g[3]) == "Chaser    1  warning");
        st.showInfo = true; g = P::GroupProblems(m, st);
        CHECK(g.size() == 7 && g.back().name == "Scene" && P::ProblemGroupHeader(g.back()) == "Scene    1");
        std::strcpy(st.filter, "NAVMESH"); g = P::GroupProblems(m, st);      // case-insensitive, message and code
        CHECK(g.size() == 1 && g[0].name == "Walker");
        std::strcpy(st.filter, "zzz"); g = P::GroupProblems(m, st);
        CHECK(g.empty() && std::string(P::ProblemsEmptyText(m)) == "Nothing matches this filter.");
        CHECK(std::string(P::ProblemsEmptyText(P::ProblemsModel())) == "Nothing is wrong in this scene.");
        CHECK(P::ProblemRowTooltip(m.rows[2]) == "Character controller has no collider\n\nAdd a CapsuleCollider3D so the player can stand on things.\n\nplayer.missing_collider");
        CHECK(P::ProblemCopyText(m.rows[2]).find("Player: Error: Character controller") == 0);
        char b[24];
        P::FormatStatNumber(9999, b, sizeof(b)); CHECK(std::string(b) == "9999");
        P::FormatStatNumber(182340, b, sizeof(b)); CHECK(std::string(b) == "182.3k");
        P::FormatStatNumber(2500000, b, sizeof(b)); CHECK(std::string(b) == "2.5M");
        const P::FpsHistory h = panelfix::Fps();
        const P::FpsSummary s = P::SummarizeFps(h);
        CHECK(s.count == 120 && s.min == 41.0f && s.max == 62.0f && s.scale == 75.0f);
        CHECK(P::SummarizeFps(P::FpsHistory()).count == 0 && P::SummarizeFps(P::FpsHistory()).min == 0.0f);
        const std::vector<P::HistoryRow> rows = P::HistoryRows(panelfix::History());
        CHECK(rows.size() == 8 && rows[0].label == "After: Paste" && rows[0].steps == 2 && rows[1].label == "After: Scale Crate");
        CHECK(rows[2].kind == P::HistoryCurrent && rows[3].label == "Before: Move Player" && rows[7].label == "Before: Delete Light" && rows[7].steps == 5);
        P::HistoryModel shortNames; shortNames.undoDepth = 2; shortNames.undoNames = {"A"};
        CHECK(P::HistoryRows(shortNames)[2].label == "Before: Edit");   // fewer names than steps
    }

    // ================= OkayUI gaps closed for these panels =================
    {   // a host's PushTextScale around Begin / End stays the same frame after frame (it used to grow a step each frame)
        Reset(); UI::UseLauncherLook(true);
        Sim sim(300, 200);
        float h0 = 0.0f, h9 = 0.0f;
        for (int f = 0; f < 10; ++f) sim.Frame([&] {
            UI::PushTextScale(1.5f);
            UI::Begin("scaled", 10, 10, 280, 180);
            UI::Button("A");
            (f == 0 ? h0 : h9) = UI::GetItemRect().h;
            UI::End();
            UI::PopTextScale();
        });
        CHECK(h0 > 0.0f && h0 == h9);
        UI::UseLauncherLook(false);
    }
    {   // TextRowHeight: a text line one text line tall; centred after a taller item on its line; popped back to a frame's height
        Reset(); UI::UseLauncherLook(true);
        Sim sim(300, 200);
        UI::Rect a, b, c, btn, after;
        sim.Frame([&] {
            UI::BareNextWindow();
            UI::Begin("rows", 0, 0, 300, 200);
            UI::Text("frame tall"); a = UI::GetItemRect();
            UI::PushStyleVar(UI::StyleVar_TextRowHeight, UI::GetFontSize());
            UI::Text("one line"); b = UI::GetItemRect();
            UI::Button("Tall"); btn = UI::GetItemRect();
            UI::SameLine(); UI::Text("beside"); c = UI::GetItemRect();
            UI::PopStyleVar();
            UI::Text("frame tall again"); after = UI::GetItemRect();
            UI::End();
        });
        CHECK(a.h > b.h + 8.0f && b.h == UI::GetFontSize());
        CHECK(std::abs((c.y + c.h * 0.5f) - (btn.y + btn.h * 0.5f)) < 0.6f);   // centred on the button
        CHECK(after.h == a.h);
        UI::UseLauncherLook(false);
    }
    {   // ButtonTextAlign (x), CellPadding, ChildFlags: the label moves; the cell gap and row height follow; no padding without a frame
        Reset(); UI::UseLauncherLook(true);
        Sim sim(400, 300);
        UI::Rect cellA, cellB, plain, framed, child0, child1;
        sim.Frames(2, [&] {
            UI::BareNextWindow();
            UI::Begin("gaps", 0, 0, 400, 300);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);   // the Dear ImGui-looking forms (child flags, button text align) are the framed ones
            UI::PushStyleVar(UI::StyleVar_CellPadding, UI::Vec2{10.0f, 6.0f});
            if (UI::BeginTable("t", 2, UI::Table_None)) {
                UI::TableNextRow(); UI::TableNextColumn(); UI::Text("a"); cellA = UI::GetItemRect();
                UI::TableNextRow(); UI::TableNextColumn(); UI::Text("b"); cellB = UI::GetItemRect();
                UI::EndTable();
            }
            UI::PopStyleVar();
            const UI::Vec2 p0 = UI::GetCursorScreenPos();
            UI::BeginChild("plain", UI::Vec2{120.0f, 60.0f}, UI::Child_None);
            UI::Text("x"); plain = UI::GetItemRect();
            UI::EndChild();
            child0 = UI::Rect{p0.x, p0.y, 0, 0};
            const UI::Vec2 p1 = UI::GetCursorScreenPos();
            UI::BeginChild("framed", UI::Vec2{120.0f, 60.0f}, UI::Child_Borders);
            UI::Text("y"); framed = UI::GetItemRect();
            UI::EndChild();
            child1 = UI::Rect{p1.x, p1.y, 0, 0};
            UI::PopStyleVar();
            UI::End();
        });
        CHECK(cellA.x >= 10.0f && cellA.y >= 6.0f);                                   // 10 in, 6 down
        CHECK(std::abs((cellB.y - cellA.y) - (cellA.h + 12.0f)) < 0.6f);             // the row: 6 + content + 6, no item spacing
        CHECK(plain.x == child0.x && plain.y == child0.y);                           // Dear ImGui's default: no padding
        CHECK(framed.x > child1.x + 4.0f && framed.y > child1.y + 4.0f);
        // AutoResizeY: as tall as last frame's content (three rows here), no scrollbar room taken, the next item right below it
        UI::Rect autoBox, below;
        float maxScroll = -1.0f;
        sim.Frames(3, [&] {
            UI::BareNextWindow();
            UI::Begin("auto", 0, 0, 400, 300);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);   // the Dear ImGui-looking forms (child flags, button text align) are the framed ones
            const UI::Vec2 p = UI::GetCursorScreenPos();
            UI::BeginChild("grow", UI::Vec2{200.0f, 0.0f}, UI::Child_AutoResizeY);
            UI::Text("one"); UI::Text("two"); UI::Text("three");
            const UI::Rect last = UI::GetItemRect();
            maxScroll = UI::GetScrollMaxY();
            UI::EndChild();
            autoBox = UI::Rect{p.x, p.y, 200.0f, last.y + last.h - p.y};
            UI::Text("after"); below = UI::GetItemRect();
            UI::PopStyleVar();
            UI::End();
        });
        CHECK(maxScroll == 0.0f);
        CHECK(std::abs(below.y - (autoBox.y + autoBox.h + 8.0f)) < 1.0f);           // the child ends where its last row ends
        // the label of a wide button sits left, centred or right
        float xs[3];
        const float al[3] = {0.0f, 0.5f, 1.0f};
        for (int k = 0; k < 3; ++k) {
            Reset(); UI::UseLauncherLook(true);
            Sim s2(300, 80);
            s2.Frame([&] {
                UI::BareNextWindow(); UI::Begin("al", 0, 0, 300, 80); UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
                UI::PushStyleVar(UI::StyleVar_ButtonTextAlign, UI::Vec2{al[k], 0.5f});
                UI::Button("Go##al", UI::Vec2{200.0f, 0.0f});
                UI::PopStyleVar();
                UI::PopStyleVar(); UI::End();
            });
            const Bitmap& bm = s2.Draw(0, 0, 0);
            int first = -1;   // the first column of bright label pixels
            for (int x = 0; x < 300 && first < 0; ++x)
                for (int y = 0; y < 60 && first < 0; ++y) { const unsigned px = PixelAt(bm, x, y); if (((px >> 24) & 255) > 200 && ((px >> 16) & 255) > 200) first = x; }
            xs[k] = (float)first;
        }
        CHECK(xs[0] > 0 && xs[0] < 30 && xs[1] > 80 && xs[1] < 120 && xs[2] > 170);
        UI::UseLauncherLook(false);
    }

    // ================= Problems =================
    {   // the page: groups and rows, the Fix buttons, golden
        P::ProblemsModel m = panelfix::Problems();
        P::ProblemsState st;
        S::ProblemsResult res;
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) { res = S::DrawProblems(m, st, x, y, w, h); });
        r.Frames(3);
        CHECK(res.groupHeaders.size() == 6 && res.rows.size() == 6 && !res.empty);
        CHECK(res.rowIndex[0] == 1 && res.rowIndex[2] == 2);              // Crate (model row 1) first, Player (row 2) third
        CHECK(res.fixButtons[0].w > 0 && res.fixButtons[1].w == 0);       // Crate can be fixed, the missing script cannot
        CHECK(res.rows[0].x > res.fixButtons[0].x + res.fixButtons[0].w);  // the text sits right of its Fix button
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_problems", r.Draw());
        // a click on a row opens it; Fix fixes the row it is on
        r.Click(res.rows[2]);
        CHECK(res.intent.open == -1);                                      // the intent is per frame...
        bool opened = false, fixed = false;
        auto r2 = MakeRig(560, 420, [&](float x, float y, float w, float h) {
            res = S::DrawProblems(m, st, x, y, w, h);
            if (res.intent.open == 2) opened = true;
            if (res.intent.fix == 1) fixed = true;
        });
        r2.Frames(3);
        r2.Click(res.rows[2]);
        CHECK(opened && !fixed);
        r2.Click(res.fixButtons[0]);
        CHECK(fixed);
    }
    {   // the pills toggle their severity; the infos pill shows the Scene note
        P::ProblemsModel m = panelfix::Problems();
        P::ProblemsState st;
        S::ProblemsResult res;
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) { res = S::DrawProblems(m, st, x, y, w, h); });
        r.Frames(3);
        r.Click(res.errorPill);
        r.Frames(1);
        CHECK(!st.showErr && res.rows.size() == 3);                        // warnings only
        r.Click(res.infoPill);
        r.Frames(1);
        CHECK(st.showInfo && res.rows.size() == 4);
        r.Click(res.warningPill);
        r.Frames(1);
        CHECK(!st.showWarn && res.rows.size() == 1 && res.groupHeaders.size() == 1);
        r.Click(res.infoPill);
        r.Frames(1);
        CHECK(res.empty && res.rows.empty());                              // everything filtered out
        Check("editorpanels_problems_filtered", r.Draw());
    }
    {   // typing in the search field filters; Backspace and select-all clear it
        P::ProblemsModel m = panelfix::Problems();
        P::ProblemsState st;
        S::ProblemsResult res;
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) { res = S::DrawProblems(m, st, x, y, w, h); });
        r.Frames(3);
        r.Click(res.searchField);
        r.sim.Type("texture"); r.Frames(2);
        CHECK(std::strcmp(st.filter, "texture") == 0 && res.rows.size() == 1 && res.rowIndex[0] == 1);
        r.sim.Press(Key_Backspace); r.Frame(); r.Frames(1);
        CHECK(std::strcmp(st.filter, "textur") == 0);
        r.sim.Press(Key_SelectAll); r.Frame(); r.sim.Press(Key_Backspace); r.Frame(); r.Frames(1);
        CHECK(st.filter[0] == '\0' && res.rows.size() == 6);
    }
    {   // the right-click menu: Go to source, Fix, Copy message; a row without a fix has no Fix entry
        P::ProblemsModel m = panelfix::Problems();
        P::ProblemsState st;
        S::ProblemsResult res;
        P::ProblemsIntent seen;
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) {
            res = S::DrawProblems(m, st, x, y, w, h);
            if (res.intent.open >= 0) seen.open = res.intent.open;
            if (res.intent.fix >= 0) seen.fix = res.intent.fix;
            if (res.intent.copy >= 0) seen.copy = res.intent.copy;
        });
        r.Frames(3);
        r.RightClick(res.rows[2]);
        CHECK(res.menuOpen.w > 0 && res.menuFix.w > 0 && res.menuCopy.w > 0 && seen.open == -1);
        Check("editorpanels_problems_menu", r.Draw());
        r.Click(res.menuCopy);
        CHECK(seen.copy == 2 && seen.open == -1);                          // the click on the menu did not also open the row under it
        r.Frames(1);
        CHECK(res.menuCopy.w == 0);                                        // and the menu closed
        r.RightClick(res.rows[2]);
        r.Click(res.menuFix);
        CHECK(seen.fix == 2);
        r.RightClick(res.rows[2]);
        r.Click(res.menuOpen);
        CHECK(seen.open == 2);
        r.RightClick(res.rows[1]);                                         // the missing script: no fix
        CHECK(res.menuOpen.w > 0 && res.menuFix.w == 0);
        r.sim.Move(2, 2); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frames(1);   // a click outside closes it
        CHECK(res.menuOpen.w == 0);
    }
    {   // 600 rows in a short panel: the wheel scrolls the list, nothing is dropped
        P::ProblemsModel m;
        for (int i = 0; i < 600; ++i)
            m.rows.push_back(panelfix::Row((std::uint64_t)i + 1, i % 3 == 0 ? P::ProblemError : P::ProblemWarning, "c",
                                           ("Problem number " + std::to_string(i)).c_str(), "", ("Object" + std::to_string(i / 4)).c_str(), i % 2 == 0));
        m.errors = 200; m.warnings = 400;
        P::ProblemsState st;
        S::ProblemsResult res;
        auto r = MakeRig(560, 300, [&](float x, float y, float w, float h) { res = S::DrawProblems(m, st, x, y, w, h); });
        r.Frames(3);
        CHECK(res.rows.size() == 600 && UI::DroppedGeometry() == 0);
        const float y0 = res.rows[0].y;
        r.sim.Move(CX(res.list), CY(res.list)); r.sim.wheel = -5; r.Frame(); r.Frames(2);
        CHECK(res.rows[0].y < y0 - 20.0f);
    }
    {   // hostile text: long, control characters, bad UTF-8, an empty model
        P::ProblemsModel m;
        m.rows.push_back(panelfix::Row(1, P::ProblemError, "x\x01", std::string(4000, 'W').c_str(), "\xff\xfe", "\xc3", true));
        m.rows.push_back(panelfix::Row(2, 7, "", "", "", "", false));       // a severity out of range: shown as Info, hidden by default
        m.errors = 1;
        P::ProblemsState st;
        std::memset(st.filter, 'a', sizeof(st.filter));                    // no terminator: the panel adds one
        S::ProblemsResult res;
        auto r = MakeRig(400, 300, [&](float x, float y, float w, float h) { res = S::DrawProblems(m, st, x, y, w, h); });
        r.Frames(2);
        CHECK(st.filter[sizeof(st.filter) - 1] == '\0' && UI::DroppedGeometry() == 0);
        st.filter[0] = '\0'; r.Frames(2);
        CHECK(res.rows.size() == 1);
        P::ProblemsModel empty;
        auto e = MakeRig(400, 300, [&](float x, float y, float w, float h) { res = S::DrawProblems(empty, st, x, y, w, h); });
        e.Frames(2);
        CHECK(res.empty && res.rows.empty());
        Check("editorpanels_problems_empty", e.Draw());
    }

    // ================= Stats =================
    {   // numbers, the plot, the guide line, golden
        panelfix::StatsData d;
        S::StatsResult res;
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) { res = S::DrawStats(d.model, x, y, w, h); });
        r.Frames(3);
        CHECK(res.valueCells.size() == d.model.counters.size());
        CHECK(res.plot.w > 400 && res.plot.h >= 47.0f && res.plot.h <= 49.0f);
        CHECK(res.guideY > res.plot.y && res.guideY < res.plot.y + res.plot.h);   // 60 of 75: a fifth from the top
        CHECK(std::abs(res.guideY - (res.plot.y + res.plot.h * 0.2f)) < 1.0f);
        CHECK(res.valueCells[1].x > res.valueCells[0].x && res.valueCells[2].y > res.valueCells[0].y);   // two to a row
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_stats", r.Draw());
    }
    {   // the four switches write through their pointers and say so; a null switch is not drawn
        panelfix::StatsData d;
        S::StatsResult res;
        bool changed = false;
        auto r = MakeRig(560, 560, [&](float x, float y, float w, float h) { res = S::DrawStats(d.model, x, y, w, h); changed |= res.intent.changed; });
        r.Frames(3);
        r.Click(res.frustumBox);   CHECK(!d.frustum && changed);
        r.Click(res.occlusionBox); CHECK(d.occlusion);
        r.Click(res.staticBox);    CHECK(!d.staticBatch);
        r.Click(res.dynamicBox);   CHECK(d.dynamicBatch);
        CHECK(d.frustum == false && d.occlusion && !d.staticBatch && d.dynamicBatch);
        d.model.occlusionCulling = nullptr; r.Frames(2);
        CHECK(res.occlusionBox.w == 0);
    }
    {   // right-click the census: Copy stats
        panelfix::StatsData d;
        S::StatsResult res;
        bool copied = false;
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) { res = S::DrawStats(d.model, x, y, w, h); copied |= res.intent.copy; });
        r.Frames(3);
        r.RightClick(res.valueCells[4]);
        CHECK(res.menuCopy.w > 0 && !copied);
        r.Click(res.menuCopy);
        CHECK(copied);
        const std::string text = P::StatsCopyText(d.model);
        CHECK(text.find("FPS 60 (16.78 ms)") == 0 && text.find("Triangles: 182340\n") != std::string::npos);
    }
    {   // a short panel scrolls to reach the footer; a low frame rate; no history
        panelfix::StatsData d;
        d.model.framerate = 12.0f;
        S::StatsResult res;
        auto r = MakeRig(560, 200, [&](float x, float y, float w, float h) { res = S::DrawStats(d.model, x, y, w, h); });
        r.Frames(3);
        const float fy = res.footer.y;
        r.sim.Move(280, 100); r.sim.wheel = -10; r.Frame(); r.Frames(2);
        CHECK(res.footer.y < fy - 40.0f);
        d.model.history = nullptr; r.Frames(2);
        CHECK(res.guideY > res.plot.y && UI::DroppedGeometry() == 0);     // an empty history still scales to 75
    }

    // ================= History =================
    {   // the rows, golden; Undo and Redo; a click on a row jumps there
        P::HistoryModel m = panelfix::History();
        S::HistoryResult res;
        P::HistoryIntent got;
        auto r = MakeRig(460, 380, [&](float x, float y, float w, float h) {
            res = S::DrawHistory(m, x, y, w, h);
            if (res.intent.undo || res.intent.redo || res.intent.copy >= 0) got = res.intent;
        });
        r.Frames(3);
        CHECK(res.rows.size() == 8 && !res.empty);
        for (std::size_t i = 1; i < res.rows.size(); ++i) CHECK(res.rows[i].y > res.rows[i - 1].y);
        Check("editorpanels_history", r.Draw());
        r.Click(res.undoButton); CHECK(got.undo == 1 && got.redo == 0);
        got = P::HistoryIntent();
        r.Click(res.redoButton); CHECK(got.redo == 1 && got.undo == 0);
        got = P::HistoryIntent();
        r.Click(res.rows[6]);    CHECK(got.undo == 4);                    // "Before: Create Empty" is four undos back
        got = P::HistoryIntent();
        r.Click(res.rows[0]);    CHECK(got.redo == 2);                    // "After: Paste" is two redos ahead
        got = P::HistoryIntent();
        r.Click(res.rows[2]);    CHECK(got.undo == 0 && got.redo == 0);   // the current state: nothing to do
        // the right-click menu
        r.RightClick(res.rows[4]);
        CHECK(res.menuJump.w > 0 && res.menuCopy.w > 0);
        r.Click(res.menuJump);
        CHECK(got.undo == 2);
        got = P::HistoryIntent();
        r.RightClick(res.rows[2]);
        CHECK(res.menuJump.w == 0 && res.menuCopy.w > 0);                 // no jump to where you are
        r.Click(res.menuCopy);
        CHECK(got.copy == 2 && got.undo == 0);
    }
    {   // disabled buttons; the empty state
        P::HistoryModel m;
        S::HistoryResult res;
        int asked = 0;
        auto r = MakeRig(460, 300, [&](float x, float y, float w, float h) { res = S::DrawHistory(m, x, y, w, h); asked += res.intent.undo + res.intent.redo; });
        r.Frames(3);
        CHECK(res.empty && res.rows.empty());
        r.Click(res.undoButton); r.Click(res.redoButton);
        CHECK(asked == 0);
        Check("editorpanels_history_empty", r.Draw());
    }
    {   // 200 states in a short panel: the wheel scrolls
        P::HistoryModel m;
        for (int i = 0; i < 200; ++i) m.undoNames.push_back("Edit " + std::to_string(i));
        m.undoDepth = 200;
        S::HistoryResult res;
        auto r = MakeRig(460, 260, [&](float x, float y, float w, float h) { res = S::DrawHistory(m, x, y, w, h); });
        r.Frames(3);
        CHECK(res.rows.size() == 201 && UI::DroppedGeometry() == 0);
        const float y0 = res.rows[0].y;
        r.sim.Move(CX(res.list), CY(res.list)); r.sim.wheel = -6; r.Frame(); r.Frames(2);
        CHECK(res.rows[0].y < y0 - 20.0f);
    }
    // ================= section 10: the toolkit pieces the next three panels needed =================
    {   // SearchField: the text starts after the magnifier; the clear button empties it and drops the keyboard
        Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false;
        Sim sim(400, 120);
        char buf[32] = "";
        UI::Rect field; bool changed = false;
        auto ui = [&] {
            UI::BareNextWindow(); UI::Begin("sf", 0, 0, 400, 120);
            if (UI::SearchField("##s", "Search", buf, (int)sizeof(buf), -100.0f)) changed = true;
            field = UI::GetItemRect();
            UI::End();
        };
        sim.Frames(2, ui);
        CHECK(field.w > 250.0f && field.w < 310.0f);                       // the width left less 100
        CHECK(CountNot(sim.Draw(30, 33, 41), (int)field.x + 4, (int)field.y + 4, (int)(field.x + field.h * 0.9f), (int)(field.y + field.h - 4), Rgba(30, 33, 41)) > 20);   // a magnifier
        sim.Click(field.x + field.w * 0.5f, field.y + field.h * 0.5f, ui);
        sim.Type("crate"); sim.Frame(ui);
        CHECK(std::strcmp(buf, "crate") == 0 && changed);
        changed = false;
        sim.Click(field.x + field.w - field.h * 0.5f, field.y + field.h * 0.5f, ui);   // the cross
        CHECK(buf[0] == '\0' && changed && UI::GetKeyboardFocusId() == 0);
        sim.Type("x"); sim.Frame(ui);
        CHECK(buf[0] == '\0');                                              // no longer typing into it
        UI::UseLauncherLook(false);
    }
    {   // IsItemDeactivatedAfterEdit: once, after an edit, when the field loses the keyboard; not without an edit; Enter commits first
        Reset(); UI::UseLauncherLook(true);
        Sim sim(400, 200);
        char a[32] = "one", b[32] = "two";
        UI::Rect ra, rb; int deactA = 0, deactB = 0, enterA = 0;
        auto ui = [&] {
            UI::BareNextWindow(); UI::Begin("de", 0, 0, 400, 200);
            if (UI::InputText("##a", a, (int)sizeof(a), UI::InputText_EnterReturnsTrue)) ++enterA;
            ra = UI::GetItemRect(); if (UI::IsItemDeactivatedAfterEdit()) ++deactA;
            UI::InputText("##b", b, (int)sizeof(b)); rb = UI::GetItemRect(); if (UI::IsItemDeactivatedAfterEdit()) ++deactB;
            UI::End();
        };
        sim.Frames(2, ui);
        sim.Click(ra.x + 10, ra.y + 10, ui);
        sim.Click(rb.x + 10, rb.y + 10, ui);                               // focus moves without an edit
        CHECK(deactA == 0);
        sim.Type("!"); sim.Frame(ui);
        sim.Click(390, 190, ui); sim.Frames(1, ui);                        // a click on empty space
        CHECK(deactB == 1 && std::strchr(b, '!') != nullptr);
        sim.Frames(3, ui);
        CHECK(deactB == 1);                                                 // once
        sim.Click(ra.x + 10, ra.y + 10, ui);
        sim.Type("?"); sim.Frame(ui);
        sim.Click(rb.x + 10, rb.y + 10, ui); sim.Frames(1, ui);            // another field takes the keyboard (drawn after it)
        CHECK(deactA == 1);
        sim.Click(ra.x + 10, ra.y + 10, ui);
        sim.Type("#"); sim.Frame(ui); sim.Tap(Key_Enter, ui);
        CHECK(enterA == 1);
        sim.Click(390, 190, ui); sim.Frames(1, ui);
        CHECK(deactA == 1);                                                 // Enter already committed it
        sim.Click(ra.x + 10, ra.y + 10, ui);
        sim.Type("$"); sim.Frame(ui); sim.Tap(Key_Escape, ui); sim.Frames(1, ui);
        CHECK(deactA == 2);                                                 // Escape lets go too
        UI::UseLauncherLook(false);
    }
    {   // 40 text fields: the caret keeps working in each (the slot table recycles the oldest, never the focused one)
        Reset(); UI::UseLauncherLook(true);
        Sim sim(400, 2600);
        char bufs[70][16] = {};
        UI::Rect last;
        auto ui = [&] {
            UI::BareNextWindow(); UI::Begin("many", 0, 0, 400, 2600);
            UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{4.0f, 1.0f});
            for (int i = 0; i < 70; ++i) { UI::PushID(i); UI::SetNextItemWidth(120); UI::InputText("##f", bufs[i], 16); if (i == 69) last = UI::GetItemRect(); UI::PopID(); }
            UI::PopStyleVar();
            UI::End();
        };
        sim.Frames(2, ui);
        sim.Click(last.x + 60, last.y + last.h * 0.5f, ui);
        sim.Type("ab"); sim.Frame(ui); sim.Type("cd"); sim.Frame(ui);
        CHECK(std::strcmp(bufs[69], "abcd") == 0);                          // typed in order (a lost caret typed "cdab")
        UI::UseLauncherLook(false);
    }
    {   // formatted fields: the value centred in the given format, drag one per pixel times the speed, clamp, Ctrl+click types;
        // "##" hides the rest of a label; the framed slider jumps to a click; FramePadding sets the frame height
        Reset(); UI::UseLauncherLook(true);
        Sim sim(500, 300);
        float hz = 50.0f, g3[3] = {0.0f, -9.81f, 0.0f}; int it = 10;
        UI::Rect rHz, rG, rIt; float hzW = 0;
        auto ui = [&] {
            UI::BareNextWindow(); UI::Begin("fmt", 0, 0, 500, 300);
            UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{12.0f, 8.0f});
            UI::SetNextItemWidth(160.0f);
            UI::DragFloat("##hz", &hz, 1.0f, 1.0f, 240.0f, "%.0f"); rHz = UI::GetItemRect();
            UI::DragFloat3("Gravity 3D##g", g3, 0.05f, 0.0f, 0.0f, "%.3f"); rG = UI::GetItemRect();
            UI::SetNextItemWidth(200.0f);
            UI::SliderInt("##it", &it, 4, 30, "%d"); rIt = UI::GetItemRect();
            UI::PopStyleVar();
            hzW = rHz.w;
            UI::End();
        };
        sim.Frames(2, ui);
        CHECK(rHz.h == UI::GetFontSize() + 16.0f && hzW == 160.0f);       // a text line plus 2 x 8; no label, so the frame is the width
        sim.Move(rHz.x + 50, rHz.y + 10); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Move(rHz.x + 80, rHz.y + 10); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        CHECK(hz == 80.0f);
        sim.Frames(20, ui);   // a third of a second: the next press is not the second click of a double click (that types a number)
        sim.Move(rHz.x + 50, rHz.y + 10); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Move(rHz.x + 450, rHz.y + 10); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        CHECK(hz == 240.0f);                                               // clamped
        sim.ctrl = true; sim.Click(rHz.x + 50, rHz.y + 10, ui); sim.ctrl = false;
        sim.Press(Key_SelectAll); sim.Frame(ui); sim.Type("33"); sim.Frame(ui); sim.Tap(Key_Enter, ui);
        CHECK(hz == 33.0f);
        const Bitmap& b = sim.Draw(30, 33, 41);
        CHECK(CountNot(b, (int)(rG.x + rG.w - 90), (int)rG.y, (int)(rG.x + rG.w), (int)(rG.y + rG.h), Rgba(30, 33, 41)) > 30);   // "Gravity 3D" drawn
        sim.Click(rIt.x + rIt.w - 3, rIt.y + 10, ui);
        CHECK(it == 30);
        sim.Click(rIt.x + 3, rIt.y + 10, ui);
        CHECK(it == 4);
        UI::UseLauncherLook(false);
    }
    {   // the menu look (StyleVar_PopupPadding / PopupWidth, header rows) and Selectable_Header's colours
        Reset(); UI::UseLauncherLook(true);
        Sim sim(400, 300);
        UI::Rect row, item, sel;
        bool picked = false;
        auto ui = [&] {
            UI::BareNextWindow(); UI::Begin("menu", 0, 0, 400, 300);
            UI::PushStyleColor(UI::Col_Header, UI::RGBA(200, 0, 0));
            UI::Selectable("selected row", true, UI::Selectable_Header, UI::Vec2{0.0f, 26.0f}); sel = UI::GetItemRect();
            UI::Text("right-click me"); row = UI::GetItemRect();
            UI::PushStyleVar(UI::StyleVar_PopupPadding, UI::Vec2{10.0f, 8.0f});
            UI::PushStyleVar(UI::StyleVar_PopupWidth, 150.0f);
            if (UI::BeginPopupContextItem("##m")) {
                if (UI::Selectable("Pick", false, UI::Selectable_Header | UI::Selectable_ClosePopup, UI::Vec2{0.0f, 26.0f})) picked = true;
                item = UI::GetItemRect();
                UI::EndPopup();
            }
            UI::PopStyleVar(2);
            UI::PopStyleColor();
            UI::End();
        };
        sim.Frames(2, ui);
        const Bitmap& b0 = sim.Draw(30, 33, 41);
        CHECK(PixelAt(b0, (int)(sel.x + 2), (int)(sel.y + 2)) == Rgba(200, 0, 0));   // flat and square: the corner is the header colour
        sim.Move(CX(row), CY(row)); sim.Frame(ui); sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frame(ui);
        CHECK(item.w == 130.0f && item.x == CX(row) + 10.0f && item.y == CY(row) + 8.0f);   // 150 wide less 2 x 10; 10 and 8 in
        sim.Click(CX(item), CY(item), ui);
        CHECK(picked);
        sim.Frames(1, ui);
        CHECK(!UI::IsPopupOpen("##m"));
        UI::UseLauncherLook(false);
    }
    {   // CenterNextOnLine, SameLine + SetCursorPosX keeping the line's height, the framed child, Table_BordersOuter, wrapped rows
        Reset(); UI::UseLauncherLook(true);
        Sim sim(400, 400);
        UI::Rect tall, small, next, child, inner, w1, w2, tbl;
        auto ui = [&] {
            UI::BareNextWindow(); UI::Begin("lay", 0, 0, 400, 400);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);   // the Dear ImGui-looking forms (child flags, button text align) are the framed ones
            UI::Button("Tall", UI::Vec2{60.0f, 40.0f}); tall = UI::GetItemRect();
            UI::SameLine(); UI::SetCursorPosX(200.0f); UI::CenterNextOnLine();
            UI::Button("s", UI::Vec2{30.0f, 16.0f}); small = UI::GetItemRect();
            UI::Text("next"); next = UI::GetItemRect();
            UI::PushStyleVar(UI::StyleVar_WindowPadding, UI::Vec2{16.0f, 12.0f});
            UI::BeginChild("box", UI::Vec2{200.0f, 60.0f}, UI::Child_Borders); child = UI::Rect{UI::GetItemRectMin().x, 0, 0, 0};
            UI::Text("in"); inner = UI::GetItemRect();
            UI::EndChild(); child = UI::GetItemRect();
            UI::PopStyleVar();
            UI::PushStyleVar(UI::StyleVar_TextRowHeight, UI::GetFontSize());
            UI::PushTextWrapPos(0.0f);
            UI::TextUnformatted("wrapped words wrapped words wrapped words wrapped words wrapped words wrapped words");
            UI::PopTextWrapPos();
            UI::PopStyleVar();
            if (UI::BeginTable("t", 2, UI::Table_BordersOuter)) { UI::TableNextRow(); UI::TableNextColumn(); UI::Text("a"); UI::TableNextColumn(); UI::Text("b"); UI::EndTable(); }
            tbl = UI::GetItemRect();
            UI::PopStyleVar();
            UI::End();
        };
        sim.Frames(3, ui);
        CHECK(std::abs(CY(small) - CY(tall)) < 0.6f && small.x == 200.0f);
        CHECK(next.y >= tall.y + tall.h);                                   // the next line is below the tall item, not the short one
        CHECK(inner.x == child.x + 16.0f && inner.y == child.y + 12.0f);   // the pushed padding
        const Bitmap& b = sim.Draw(30, 33, 41);
        CHECK(PixelAt(b, (int)child.x + 100, (int)child.y) != Rgba(30, 33, 41));   // the outline
        (void)w1; (void)w2; (void)tbl;
        UI::UseLauncherLook(false);
    }
    {   // a host clip around a bare window: drawing and clicks only inside it (Project Settings' sections scrolled part out of view)
        Reset(); UI::UseLauncherLook(true);
        Sim sim(300, 300);
        bool hitTop = false, hitLow = false;
        UI::Rect a, c;
        auto ui = [&] {
            UI::PushClipRect(0, 0, 300, 100);
            UI::BareNextWindow(); UI::Begin("clipped", 0, 0, 300, 300);
            if (UI::Button("top")) hitTop = true;
            a = UI::GetItemRect();
            UI::SetCursorScreenPos(UI::Vec2{0.0f, 200.0f});
            if (UI::Button("low")) hitLow = true;
            c = UI::GetItemRect();
            UI::End();
            UI::PopClipRect();
        };
        sim.Frames(2, ui);
        sim.Click(CX(a), CY(a), ui);
        sim.Click(CX(c), CY(c), ui);
        CHECK(hitTop && !hitLow);
        CHECK(CountNot(sim.Draw(0, 0, 0), 0, 150, 300, 300, Rgba(0, 0, 0)) == 0);
        UI::UseLauncherLook(false);
    }

    // ================= Tags and Layers =================
    {   // the section, golden; Strict, Add them, Remove, Add tag, Reset, Reset all through the intent
        P::TagsLayersModel m = panelfix::TagsLayers();
        P::TagsLayersState st;
        S::TagsLayersResult res;
        P::TagsLayersIntent seen;
        auto r = MakeRig(560, 780, [&](float x, float y, float w, float h) {
            res = S::DrawTagsLayers(m, st, x, y, w, h);
            const P::TagsLayersIntent& in = res.intent;
            if (in.strict >= 0) seen.strict = in.strict;
            if (in.addUnlisted) seen.addUnlisted = true;
            if (in.removeTag >= 0) seen.removeTag = in.removeTag;
            if (in.addTag) seen.addTag = true;
            if (in.resetLayer >= 0) seen.resetLayer = in.resetLayer;
            if (in.resetAllLayers) seen.resetAllLayers = true;
            if (in.renameLayer >= 0) { seen.renameLayer = in.renameLayer; seen.layerName = in.layerName; }
            if (in.renameTag >= 0) { seen.renameTag = in.renameTag; seen.renameTo = in.renameTo; }
        });
        r.Frames(3);
        CHECK(res.removeButtons.size() == 4 && res.renameButtons.size() == 4 && res.layerFields.size() == 32);
        CHECK(res.layerResets[4].w > 0 && res.layerResets[0].w == 0 && res.layerResets[8].w > 0);   // renamed built-in, default, new name
        CHECK(res.contentHeight > 600.0f && UI::DroppedGeometry() == 0);
        CHECK(std::strcmp(st.layerBuf[8], "Ground") == 0);                  // the fields take the model's names
        Check("editorpanels_tagslayers", r.Draw());
        r.Click(res.strictBox);        CHECK(seen.strict == 1);
        r.Click(res.addUnlisted);      CHECK(seen.addUnlisted);
        r.Click(res.removeButtons[2]); CHECK(seen.removeTag == 2);
        r.Click(res.newTagField); r.sim.Type("Door"); r.Frame(); r.sim.Press(Key_Enter); r.Frame();
        CHECK(seen.addTag && std::strcmp(st.newTag, "Door") == 0);
        r.Click(res.layerResets[4]);   CHECK(seen.resetLayer == 4);
        r.Click(res.resetAll);         CHECK(seen.resetAllLayers);
        // a layer field: typing, then a click elsewhere commits it once; the field keeps the typed text until the model changes
        r.Click(res.layerFields[5]); r.sim.Press(Key_SelectAll); r.Frame(); r.sim.Type("Coins"); r.Frame();
        CHECK(seen.renameLayer == -1);
        r.sim.Move(540, 770); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frames(1);
        CHECK(seen.renameLayer == 5 && seen.layerName == "Coins");
        m.layers[5].name = "Coins"; r.Frames(2);                          // the host renamed it
        CHECK(std::strcmp(st.layerBuf[5], "Coins") == 0);
        m.layers[5].name = "UI"; r.Frames(1);                               // the host turned it down: the field shows the model's name again
        CHECK(std::strcmp(st.layerBuf[5], "UI") == 0);
        seen.renameLayer = -1;
        r.Click(res.layerFields[3]); r.sim.Type("Glass"); r.Frame(); r.sim.Press(Key_Enter); r.Frame(); r.Frames(1);
        CHECK(seen.renameLayer == 3 && seen.layerName == "Glass");
        // the rename popup: opens with the tag's name, a new name asks for the rename, the host's error is shown, closeRename closes it
        r.Click(res.renameButtons[0]); r.Frames(1);
        CHECK(res.renameField.w > 0 && res.popup.w > 0 && std::strcmp(st.renameBuf, "Pickup") == 0 && st.renaming == 0);
        Check("editorpanels_tagslayers_rename", r.Draw());
        r.Click(res.renameField); r.sim.Press(Key_SelectAll); r.Frame(); r.sim.Type("Loot"); r.Frame();
        r.Click(res.renameButton);
        CHECK(seen.renameTag == 0 && seen.renameTo == "Loot");
        st.closeRename = true; r.Frames(2);
        CHECK(res.renameField.w == 0 && !st.closeRename);
    }
    {   // empty lists, long names, hostile buffers
        P::TagsLayersModel m;
        P::TagsLayersState st;
        std::memset(st.newTag, 'x', sizeof(st.newTag));                    // no terminator: the panel adds one
        m.custom.push_back({std::string(300, 'L'), 1});
        m.unlisted = {"a", "b", "c", "d", "e", "f"};
        S::TagsLayersResult res;
        auto r = MakeRig(400, 300, [&](float x, float y, float w, float h) { res = S::DrawTagsLayers(m, st, x, y, w, h); });
        r.Frames(2);
        CHECK(st.newTag[sizeof(st.newTag) - 1] == '\0' && res.layerFields.empty() && UI::DroppedGeometry() == 0);
        CHECK(P::UnlistedTagsText(m.unlisted).find("a, b, c, d, ...") != std::string::npos);
        char tinyBuf[4]; P::CopyToBuf(tinyBuf, sizeof(tinyBuf), "\xC3\xA9\xC3\xA9");   // two two-byte characters into 3 bytes: one whole one
        CHECK(std::strcmp(tinyBuf, "\xC3\xA9") == 0);
    }

    // ================= Physics =================
    {   // the section, golden; drags, the step-rate clamp and the solver slider (one physics path: no quality list, no continuous box)
        panelfix::PhysicsData d;
        S::PhysicsSettingsResult res;
        P::PhysicsSettingsIntent seen;
        float labelW = 0.0f;
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) {
            labelW = UI::CalcTextWidth("Gravity 2D");
            res = S::DrawPhysicsSettings(d.model, x, y, w, h);
            const P::PhysicsSettingsIntent& in = res.intent;
            if (in.gravityChanged) seen.gravityChanged = true;
            if (in.rateChanged) seen.rateChanged = true;
            if (in.smoothChanged) seen.smoothChanged = true;
            if (in.solverIterations >= 0) seen.solverIterations = in.solverIterations;
            if (in.apply) seen.apply = true;
        });
        r.Frames(3);
        CHECK(res.iterations.w > 0 && res.contentHeight > 250.0f);
        CHECK(std::abs(res.gravity2D.w - (P::DefaultItemWidth(528.0f) + 8.0f + labelW)) < 1.0f);   // Dear ImGui's 65%, then the label
        Check("editorpanels_physics", r.Draw());
        // the first gravity cell: drag 20 pixels right, 0.05 a pixel
        const float gx = res.gravity2D.x + 30.0f, gy = CY(res.gravity2D);
        r.sim.Move(gx, gy); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Move(gx + 20.0f, gy); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(seen.gravityChanged && std::abs(d.g2[0] - 1.0f) < 1e-4f);
        r.Click(res.smooth);  CHECK(seen.smoothChanged && !d.smooth);
        r.sim.Move(res.iterations.x + 4.0f, CY(res.iterations)); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(seen.solverIterations == 4);
        r.Click(res.apply); CHECK(seen.apply);
        CHECK(P::ClampStepsPerSecond(0.0f) == 1.0f && P::ClampStepsPerSecond(900.0f) == 500.0f && P::ClampStepsPerSecond(std::nanf("")) == 50.0f);
    }
    {   // missing pointers: those rows are not drawn
        P::PhysicsSettingsModel m;
        S::PhysicsSettingsResult res;
        auto r = MakeRig(400, 300, [&](float x, float y, float w, float h) { res = S::DrawPhysicsSettings(m, x, y, w, h); });
        r.Frames(2);
        CHECK(res.gravity2D.w == 0 && res.stepRate.w == 0 && res.smooth.w == 0 && res.apply.w > 0);
    }

    // ================= Profiler =================
    {   // the window, golden; the rules; Pause and the scrub slider; Copy CSV; the hot row; an empty profile
        panelfix::ProfilerData d;
        S::ProfilerResult res;
        P::ProfilerIntent seen;
        auto r = MakeRig(560, 780, [&](float x, float y, float w, float h) {
            res = S::DrawProfiler(d.model, x, y, w, h);
            if (res.intent.pauseChanged) seen.pauseChanged = true;
            if (res.intent.selectChanged) seen.selectChanged = true;
            if (res.intent.copyCsv) seen.copyCsv = true;
        });
        r.Frames(3);
        CHECK(res.rows.size() == 7 && res.guide60 > res.framePlot.y && res.guide30 == 0.0f && res.scrubX == 0.0f && !res.empty);
        CHECK(res.bars[2].w > 0 && UI::DroppedGeometry() == 0);
        const std::vector<int> order = P::ProfilerRows(d.model);
        CHECK(order[0] == 4 && order[1] == 1 && d.model.sections[(std::size_t)order.back()].name == "Frame");   // heaviest self first
        CHECK(P::ProfilerHot(d.model, d.model.sections[4]) && !P::ProfilerHot(d.model, d.model.sections[0]));
        CHECK(P::ProfilerDiagnosis(d.model) == "CPU-bound: Render is heaviest (top: Render.Scene).  Running smoothly (60+ FPS).");
        CHECK(P::ProfilerCsv(d.model).find("Physics.Step,1.4000,1.4000,2,1.3000,1.1200,2.2000") != std::string::npos);
        float frac = 0.0f;
        CHECK(P::ProfilerBarText("Render", 2.9, 7.9, &frac) == "Render 2.90 ms (37%)" && std::abs(frac - 2.9f / 7.9f) < 1e-5f);
        Check("editorpanels_profiler", r.Draw());
        r.Click(UI::Rect{res.pauseBox.x, res.pauseBox.y, 30.0f, res.pauseBox.h});
        r.Frames(1);
        CHECK(d.paused && seen.pauseChanged && d.selected == 239 && res.frameSlider.w > 0 && res.scrubX > 0.0f);
        r.sim.Move(res.frameSlider.x + 4.0f, CY(res.frameSlider)); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(seen.selectChanged && d.selected == 0);
        Check("editorpanels_profiler_paused", r.Draw());
        r.Click(res.copyCsv); CHECK(seen.copyCsv);
    }
    {   // nothing recorded yet, no pause pointer
        P::ProfilerModel m;
        S::ProfilerResult res;
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) { res = S::DrawProfiler(m, x, y, w, h); });
        r.Frames(2);
        CHECK(res.empty && res.rows.empty() && res.pauseBox.w == 0 && UI::DroppedGeometry() == 0);
        CHECK(P::ProfilerDiagnosis(m) == "Waiting for frames..." && std::string(P::ProfilerEmptyText(m)).find("Press Play") == 0);
    }
    {   // the Textures section (the editor's texture library): folded at first and not asked for; opened, its line, the budget
        // warning, the hot-reload note and one row per texture, the file name with its path as the tooltip
        panelfix::ProfilerData d;
        int asked = 0;
        d.model.textures = [&](P::ProfilerTextures& t) {
            ++asked;
            t.line = "Textures: 2, 5.25 MB of 256 MB";
            t.overBudget = true; t.hotReload = true; t.reloads = 3; t.trimmed = 1;
            P::ProfilerTexture a; a.path = "Assets/Art/crate.png"; a.width = 512; a.height = 512; a.mipLevels = 10; a.megabytes = 1.33; a.kind = "colour";
            P::ProfilerTexture b; b.path = "Assets/Art/crate_n.png"; b.width = 1024; b.height = 1024; b.mipLevels = 11; b.megabytes = 5.33; b.kind = "normal";
            t.rows = {a, b};
        };
        S::ProfilerResult res;
        auto r = MakeRig(560, 1100, [&](float x, float y, float w, float h) { res = S::DrawProfiler(d.model, x, y, w, h); });
        r.Frames(3);
        CHECK(res.texturesHeader.w > 0 && asked == 0 && res.textureRows.empty());
        r.Click(res.texturesHeader);
        r.Frames(2);
        CHECK(asked > 0 && res.textureRows.size() == 2 && res.texturesOverBudget);
        CHECK(P::ProfilerTextureName(d.model.textures ? "Assets/Art/crate.png" : "") == "crate.png");
        CHECK(P::ProfilerTexturesNote(P::ProfilerTextures{}) == "Hot reload off, 0 reloaded, 0 let go of for the budget");
        P::ProfilerTextures tn; tn.hotReload = true; tn.reloads = 3; tn.trimmed = 1;
        CHECK(P::ProfilerTexturesNote(tn) == "Hot reload on, 3 reloaded, 1 let go of for the budget");
        CHECK(UI::DroppedGeometry() == 0);
        r.Click(res.texturesHeader);   // folded again: not asked any more
        const int before = asked;
        r.Frames(2);
        CHECK(asked == before && res.textureRows.empty());
    }

    // ================= section 11: the toolkit pieces (framed check boxes and combos, scroll bar size, framed InputInt and SliderFloat3,
    // fields outside the clip) =================
    {   // a framed check box: the frame in Col_FrameBg, the accent at 45% once ticked, the tick in Col_CheckMark; a click toggles
        Reset(); UI::UseLauncherLook(true);
        Sim sim(300, 120);
        bool on = false, ch = false;
        UI::Rect box;
        auto ui = [&] {
            UI::BareNextWindow(); UI::Begin("fw", 0, 0, 300, 120);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
            UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{12.0f, 8.0f});
            UI::PushStyleVar(UI::StyleVar_FrameRounding, 0.0f);
            UI::PushStyleColor(UI::Col_FrameBg, UI::RGBA(41, 46, 59));
            UI::PushStyleColor(UI::Col_CheckMark, UI::RGBA(115, 159, 218));
            UI::PushStyleColor(UI::Col_Accent, UI::RGBA(60, 121, 204));
            if (UI::Checkbox("Tick##fw", &on)) ch = true;
            box = UI::GetItemRect();
            UI::PopStyleColor(3); UI::PopStyleVar(3);
            UI::End();
        };
        sim.Frames(2, ui);
        const float sz = box.h;
        sim.Move(250, 100); sim.Frame(ui);
        CHECK(PixelAt(sim.Draw(30, 33, 41), (int)(box.x + sz * 0.5f), (int)(box.y + sz * 0.5f)) == Rgba(41, 46, 59));   // unticked: the field colour, no tick
        sim.Click(box.x + 8.0f, CY(box), ui);
        CHECK(on && ch);
        sim.Move(250, 100); sim.Frame(ui);
        const Bitmap& b = sim.Draw(30, 33, 41);
        CHECK(PixelAt(b, (int)(box.x + sz * 0.5f), (int)(box.y + sz * 0.5f)) != Rgba(41, 46, 59));    // ticked: the box fills
        int tick = 0;
        for (int y = (int)box.y; y < (int)(box.y + sz); ++y)
            for (int x = (int)box.x; x < (int)(box.x + sz); ++x) if (PixelAt(b, x, y) == Rgba(115, 159, 218)) ++tick;
        CHECK(tick > (int)(sz * 2.0f));                                                 // a tick in the check-mark colour
        UI::UseLauncherLook(false);
    }
    {   // a framed combo: one field (no arrow block) with the design's chevron at its right end; the list still opens and picks
        Reset(); UI::UseLauncherLook(true);
        Sim sim(400, 300);
        const char* items[3] = {"One", "Two", "Three"};
        int cur = 0;
        UI::Rect r;
        auto ui = [&] {
            UI::BareNextWindow(); UI::Begin("fc", 0, 0, 400, 300);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
            UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{12.0f, 8.0f});
            UI::PushStyleColor(UI::Col_FrameBg, UI::RGBA(41, 46, 59));
            UI::PushStyleColor(UI::Col_Button, UI::RGBA(51, 56, 71));
            UI::SetNextItemWidth(200.0f);
            UI::Combo("Pick##fc", items, 3, &cur);
            r = UI::GetItemRect();
            UI::PopStyleColor(2); UI::PopStyleVar(2);
            UI::End();
        };
        sim.Frames(2, ui);
        sim.Move(390, 290); sim.Frame(ui);
        const float boxW = 200.0f - (UI::CalcTextWidth("Pick") + 8.0f);
        const Bitmap& b = sim.Draw(0, 0, 0);
        CHECK(PixelAt(b, (int)(r.x + boxW - r.h + 3.0f), (int)(r.y + r.h - 4.0f)) == Rgba(41, 46, 59));   // the field runs under the arrow
        CHECK(PixelAt(b, (int)(r.x + 4.0f), (int)(r.y + r.h - 4.0f)) == Rgba(41, 46, 59));               // the field
        int light = 0;   // the chevron: light strokes in the arrow's square
        for (int y = (int)r.y; y < (int)(r.y + r.h); ++y)
            for (int x = (int)(r.x + boxW - r.h); x < (int)(r.x + boxW); ++x) { const unsigned p = PixelAt(b, x, y); if ((p & 255u) > 120u) ++light; }
        CHECK(light > 6);
        sim.Click(r.x + 20.0f, CY(r), ui);
        sim.Click(r.x + 20.0f, r.y + r.h * 3.5f, ui);   // the third row of the list under the box
        CHECK(cur == 2);
        UI::UseLauncherLook(false);
    }
    {   // ScrollbarSize: a framed child's bar that wide and inside its outline, only while the content overflows; OkayUI's 10 without it
        Reset(); UI::UseLauncherLook(true);
        Sim sim(400, 400);
        int rows = 20;
        float sb = 14.0f, availIn = 0.0f;
        UI::Rect child;
        auto ui = [&] {
            UI::BareNextWindow(); UI::Begin("sb", 0, 0, 400, 400);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);   // the Dear ImGui-looking forms (child flags, button text align) are the framed ones
            UI::PushStyleVar(UI::StyleVar_ScrollbarSize, sb);
            UI::PushStyleVar(UI::StyleVar_WindowPadding, UI::Vec2{16.0f, 12.0f});
            UI::BeginChild("##list", UI::Vec2{300.0f, 200.0f}, UI::Child_Borders);
            availIn = UI::GetContentRegionAvail().x;
            UI::Button("a row wider than the list", UI::Vec2{400.0f, 0.0f});   // cut where the bar starts
            for (int i = 1; i < rows; ++i) { char t[16]; std::snprintf(t, sizeof t, "row %d", i); UI::Text(t); }
            UI::EndChild();
            child = UI::GetItemRect();
            UI::PopStyleVar(2);
            UI::PopStyleVar();
            UI::End();
        };
        sim.Frames(3, ui);
        CHECK(std::abs(availIn - (300.0f - 32.0f - 14.0f)) < 0.5f);
        const Bitmap& b = sim.Draw(0, 0, 0);
        CHECK(PixelAt(b, (int)(child.x + 300.0f - 1.0f - 7.0f), (int)(child.y + 20.0f)) != Rgba(0, 0, 0));   // the grab, inside the outline
        CHECK(PixelAt(b, (int)(child.x + 300.0f - 1.0f - 1.0f), (int)(child.y + 20.0f)) == Rgba(0, 0, 0));   // 2 pixels of bar either side,
        CHECK(PixelAt(b, (int)(child.x + 300.0f - 1.0f - 13.0f), (int)(child.y + 20.0f)) == Rgba(0, 0, 0));  // and the wide row cut at the bar
        CHECK(PixelAt(b, (int)(child.x + 300.0f - 1.0f - 16.0f), (int)(child.y + 20.0f)) != Rgba(0, 0, 0));  // (it is drawn up to it)
        rows = 2; sim.Frames(3, ui);
        CHECK(std::abs(availIn - (300.0f - 32.0f)) < 0.5f);                                                      // no overflow: no bar
        rows = 20; sb = 0.0f; sim.Frames(3, ui);
        CHECK(std::abs(availIn - (300.0f - 32.0f - 10.0f)) < 0.5f);                                              // not pushed: OkayUI's 10
        UI::UseLauncherLook(false);
    }
    {   // a framed InputInt: the label without its "##" part, the steppers, the number cut at the field's edge; SliderFloat3 with a format
        Reset(); UI::UseLauncherLook(true);
        Sim sim(500, 200);
        int n = 1280;
        float v3[3] = {0.0f, 0.5f, 1.0f};
        bool ch3 = false;
        UI::Rect ri, rs;
        auto ui = [&] {
            UI::BareNextWindow(); UI::Begin("ii", 0, 0, 500, 200);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
            UI::PushStyleVar(UI::StyleVar_ItemInnerSpacing, UI::Vec2{8.0f, 6.0f});
            UI::SetNextItemWidth(110.0f + 8.0f + UI::CalcTextWidth("Width"));
            UI::InputInt("Width##ii", &n);
            ri = UI::GetItemRect();
            UI::SetNextItemWidth(400.0f);
            if (UI::SliderFloat3("Lift##ii", v3, -1.0f, 1.0f, "%.2f")) ch3 = true;
            rs = UI::GetItemRect();
            UI::PopStyleVar(2);
            UI::End();
        };
        sim.Frames(2, ui);
        const float labelX = ri.x + ri.w - UI::CalcTextWidth("Width");
        CHECK(CountNot(sim.Draw(0, 0, 0), (int)(labelX + UI::CalcTextWidth("Width") + 2.0f), (int)ri.y, (int)(ri.x + ri.w + 60.0f), (int)(ri.y + ri.h), Rgba(0, 0, 0)) == 0);   // no "##ii" drawn
        const float plusX = labelX - 8.0f - ri.h * 0.5f;
        sim.Click(plusX, CY(ri), ui);           CHECK(n == 1281);
        sim.Click(plusX - ri.h - 8.0f, CY(ri), ui); CHECK(n == 1280);
        const float cw = (rs.w - UI::CalcTextWidth("Lift") - 8.0f - 16.0f) / 3.0f;
        CHECK(rs.w == 400.0f);
        sim.Click(rs.x + cw + 8.0f + 2.0f, CY(rs), ui);   // the second cell's left end
        CHECK(ch3 && v3[1] == -1.0f && v3[0] == 0.0f && v3[2] == 1.0f);
        UI::UseLauncherLook(false);
    }
    {   // a field scrolled out of a child's view costs nothing but still takes the keyboard while it has it
        Reset(); UI::UseLauncherLook(true);
        Sim sim(400, 300);
        static char bufs[40][16];
        for (int i = 0; i < 40; ++i) std::snprintf(bufs[i], sizeof bufs[i], "f%d", i);
        UI::Rect first;
        int dropped = 0;
        auto ui = [&] {
            UI::BareNextWindow(); UI::Begin("oc", 0, 0, 400, 300);
            UI::BeginChild("##fields", UI::Vec2{300.0f, 200.0f}, UI::Child_Borders);
            for (int i = 0; i < 40; ++i) { UI::PushID(i); UI::InputText("##f", bufs[i], 16); if (i == 0) first = UI::GetItemRect(); UI::PopID(); }
            UI::EndChild();
            UI::End();
            dropped = UI::DroppedGeometry();
        };
        sim.Frames(2, ui);
        sim.Click(first.x + 40.0f, CY(first), ui);
        sim.Move(100, 100); sim.wheel = -30.0f; sim.Frame(ui); sim.wheel = 0.0f; sim.Frames(2, ui);
        CHECK(first.y + first.h < 0.0f + 50.0f);                       // scrolled up out of the view
        sim.Type("!"); sim.Frame(ui);
        CHECK(std::strcmp(bufs[0], "f0!") == 0 && dropped == 0);
        UI::UseLauncherLook(false);
    }

    // ================= Import settings =================
    {   // the rules
        P::ImportFields a, b;
        a.kind = b.kind = P::ImportModel;
        CHECK(P::ImportSame(a, b));
        b.maxSize = 64; CHECK(P::ImportSame(a, b));             // a texture field does not count for a model
        b.scale = 2.0f; CHECK(!P::ImportSame(a, b));
        b = a; b.kind = P::ImportAudio; CHECK(!P::ImportSame(a, b));
        CHECK(P::ImportSizeIndex(0) == 0 && P::ImportSizeIndex(512) == 5 && P::ImportSizeIndex(777) == 7);
        // a texture: Kind, Filter (Anisotropic is Trilinear with more than one sample), Anisotropy, the combo width
        P::ImportFields t; t.kind = P::ImportTexture;
        CHECK(P::ImportTexKindIndex(t) == 0);
        P::ImportSetTexKind(t, 1); CHECK(t.normalMap && !t.sRGB && P::ImportTexKindIndex(t) == 1);
        P::ImportSetTexKind(t, 2); CHECK(!t.normalMap && !t.sRGB && P::ImportTexKindIndex(t) == 2);
        t.filter = 2; t.anisotropy = 8; CHECK(P::ImportFilterIndex(t) == 3);
        P::ImportSetFilter(t, 2); CHECK(t.filter == 2 && t.anisotropy == 1 && P::ImportFilterIndex(t) == 2);
        P::ImportSetFilter(t, 3); CHECK(t.filter == 2 && t.anisotropy == 8);
        P::ImportSetFilter(t, 0); CHECK(t.filter == 0 && t.anisotropy == 8 && P::ImportFilterIndex(t) == 0);   // Point keeps the level
        CHECK(P::ImportAnisoIndex(16) == 3 && P::ImportAnisoIndex(3) == 2);
        P::ImportFields u = t; CHECK(P::ImportSame(t, u));
        u.compression = 4; CHECK(!P::ImportSame(t, u));
        u = t; u.mipFilter = 1; CHECK(!P::ImportSame(t, u));
        CHECK(P::ImportComboWidth(20.0f, 12.0f, 34.0f, 900.0f, 40.0f, 8.0f) == 160.0f);    // never under 160
        CHECK(P::ImportComboWidth(300.0f, 12.0f, 34.0f, 900.0f, 40.0f, 8.0f) == 358.0f);   // the longest choice, padding and arrow
        CHECK(P::ImportComboWidth(300.0f, 12.0f, 34.0f, 300.0f, 40.0f, 8.0f) == 252.0f);   // the room left of the label
    }
    {   // a model: golden; Import animations, Apply only while changed, Revert, the normals list, the failure popup
        panelfix::ImportData d;
        S::ImportSettingsResult res;
        P::ImportSettingsIntent seen;
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) {
            res = S::DrawImportSettings(d.model, d.state, x, y, w, h);
            if (res.intent.changed) seen.changed = true;
            if (res.intent.apply) seen.apply = true;
            if (res.intent.revert) seen.revert = true;
        });
        r.Frames(3);
        CHECK(res.scale.w > 0 && res.normalsCombo.w > 0 && res.smoothing.w > 0 && res.animations.w > 0 && !res.enabled);
        CHECK(res.contentHeight > 250.0f && UI::DroppedGeometry() == 0);
        Check("editorpanels_import", r.Draw());
        r.Click(res.apply); CHECK(!seen.apply);                     // disabled while nothing changed
        r.Click(res.animations); CHECK(seen.changed && !d.edit.importAnimations);
        r.Frames(1); CHECK(res.enabled);
        r.Click(res.apply); CHECK(seen.apply);
        r.Click(res.revert); CHECK(seen.revert);
        // the normals list: Flat hides the smoothing angle
        seen = P::ImportSettingsIntent();
        r.Click(UI::Rect{res.normalsCombo.x, res.normalsCombo.y, 60.0f, res.normalsCombo.h});
        r.Click(UI::Rect{res.normalsCombo.x, res.normalsCombo.y + res.normalsCombo.h * 3.0f, 60.0f, res.normalsCombo.h});
        CHECK(seen.changed && d.edit.normals == 2);
        r.Frames(1); CHECK(res.smoothing.w == 0);
        // a scale drag: 0.01 a pixel, Dear ImGui's speed
        const float sx = res.scale.x + 40.0f, sy = CY(res.scale);
        r.sim.Move(sx, sy); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Move(sx + 10.0f, sy); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(std::abs(d.edit.scale - 0.11f) < 1e-4f);
        // the host could not save: the popup says why
        d.state.failMsg = "The settings could not be saved."; d.state.showFail = true;
        r.Frames(2);
        CHECK(res.popup.w > 0 && !d.state.showFail);
        Check("editorpanels_import_fail", r.Draw());
    }
    {   // a texture: every row (Kind, Filter with Anisotropy, Mip filter, compression, what is loaded, the tools), a sound, a newer file, no edit
        panelfix::ImportData t(P::ImportTexture), a(P::ImportAudio), n(P::ImportModel);
        n.model.newer = true;
        S::ImportSettingsResult rt, ra, rn, r0;
        P::ImportSettingsModel none;
        P::ImportSettingsState st0;
        P::ImportSettingsIntent seen;
        auto r = MakeRig(560, 1120, [&](float x, float y, float w, float h) {
            rt = S::DrawImportSettings(t.model, t.state, x, y, w, h);
            if (rt.intent.generate) seen.generate = true;
            if (rt.intent.changed) seen.changed = true;
        });
        r.Frames(2);
        CHECK(rt.sizeCombo.w > 0 && rt.kindCombo.w > 0 && rt.filterCombo.w > 0 && rt.anisoCombo.w > 0 && rt.wrapCombo.w > 0 && rt.mipmaps.w > 0);
        CHECK(rt.mipFilterCombo.w > 0 && rt.compress.w > 0 && rt.formatCombo.w > 0 && rt.generate.w > 0 && rt.ddsCutout.w > 0);
        CHECK(rt.contentHeight > 800.0f && rt.contentHeight < 1100.0f && UI::DroppedGeometry() == 0);
        CHECK(rt.filterCombo.w >= 160.0f && rt.kindCombo.w > rt.wrapCombo.w);   // as wide as the longest choice
        Check("editorpanels_import_texture", r.Draw());
        r.Click(UI::Rect{rt.sizeCombo.x, rt.sizeCombo.y, 60.0f, rt.sizeCombo.h});
        r.Click(UI::Rect{rt.sizeCombo.x, rt.sizeCombo.y + rt.sizeCombo.h * 6.0f, 60.0f, rt.sizeCombo.h});   // the sixth entry: 512
        CHECK(t.edit.maxSize == 512);
        // Kind: the second entry is the normal map
        r.Click(UI::Rect{rt.kindCombo.x, rt.kindCombo.y, 60.0f, rt.kindCombo.h});
        r.Click(UI::Rect{rt.kindCombo.x, rt.kindCombo.y + rt.kindCombo.h * 2.0f, 60.0f, rt.kindCombo.h});
        CHECK(t.edit.normalMap && !t.edit.sRGB);
        // Filter: Trilinear (the third entry) takes one sample, so the Anisotropy row goes
        r.Click(UI::Rect{rt.filterCombo.x, rt.filterCombo.y, 60.0f, rt.filterCombo.h});
        r.Click(UI::Rect{rt.filterCombo.x, rt.filterCombo.y + rt.filterCombo.h * 3.0f, 60.0f, rt.filterCombo.h});
        CHECK(t.edit.filter == 2 && t.edit.anisotropy == 1);
        r.Frames(1); CHECK(rt.anisoCombo.w == 0);
        // Compress off hides the format; Mipmaps off hides the mip filter
        r.Click(rt.compress); CHECK(!t.edit.compress);
        r.Frames(1); CHECK(rt.formatCombo.w == 0);
        r.Click(rt.mipmaps); CHECK(!t.edit.mipmaps);
        r.Frames(1); CHECK(rt.mipFilterCombo.w == 0);
        // the tools: their choices stay in the state, not in the settings; Generate asks the host
        seen = P::ImportSettingsIntent();
        r.Click(rt.ddsNormal); CHECK(t.state.ddsNormal && !seen.changed);
        r.Click(rt.generate); CHECK(seen.generate);
        t.model.toolMessage = "Wrote Hero.dds";
        r.Frames(2);
        CHECK(rt.contentHeight > 500.0f);
        auto r2 = MakeRig(560, 420, [&](float x, float y, float w, float h) {
            ra = S::DrawImportSettings(a.model, a.state, x, y, w, h);
            rn = S::DrawImportSettings(n.model, n.state, x, y + 200.0f, w, 100.0f);
            r0 = S::DrawImportSettings(none, st0, x, y + 320.0f, w, 60.0f);
        });
        r2.Frames(2);
        r2.Click(ra.mono); CHECK(a.edit.forceMono && ra.loadCombo.w > 0);
        CHECK(rn.scale.w == 0 && rn.apply.w == 0 && rn.contentHeight > 0.0f);   // the note only
        CHECK(r0.apply.w == 0 && r0.contentHeight == 0.0f);
    }

    // ================= Post-processing =================
    {   // the section, golden; the preset list, Reset, Bloom hiding its rows, a slider, the tonemap list, depth of field
        panelfix::PostData d;
        S::PostProcessResult res;
        P::PostProcessIntent seen;
        auto r = MakeRig(560, 1240, [&](float x, float y, float w, float h) {
            res = S::DrawPostProcess(d.model, x, y, w, h);
            if (res.intent.changed) seen.changed = true;
            if (res.intent.preset >= 0) seen.preset = res.intent.preset;
            if (res.intent.reset) seen.reset = true;
        });
        r.Frames(3);
        CHECK(res.lift.w > 0 && res.focus.w == 0 && res.note.w > 0 && res.contentHeight > 1000.0f && UI::DroppedGeometry() == 0);
        Check("editorpanels_post", r.Draw());
        r.Click(UI::Rect{res.preset.x, res.preset.y, 80.0f, res.preset.h}); r.Frames(1);
        CHECK(res.popup.w > 0);
        Check("editorpanels_post_presets", r.Draw());
        r.Click(UI::Rect{res.popup.x + 10.0f, res.popup.y + 8.0f + (UI::GetFontSize() * S::kEditorTextScale + 8.0f) * 2.0f, 80.0f, 8.0f});
        CHECK(seen.preset == 2 && !seen.changed);
        r.Click(res.reset); CHECK(seen.reset);
        const float lo = res.lift.y;
        r.Click(UI::Rect{res.bloom.x, res.bloom.y, 30.0f, res.bloom.h}); CHECK(!d.v.bloom && seen.changed);
        r.Frames(1); CHECK(res.lift.y < lo - 100.0f);               // the four bloom rows went
        r.sim.Move(res.exposure.x + 3.0f, CY(res.exposure)); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(std::abs(d.v.exposure - 0.1f) < 1e-4f);
        r.Click(UI::Rect{res.tonemap.x, res.tonemap.y, 60.0f, res.tonemap.h});
        r.Click(UI::Rect{res.tonemap.x, res.tonemap.y + res.tonemap.h, 60.0f, res.tonemap.h});
        CHECK(d.v.tonemap == 0);
        r.Click(UI::Rect{res.dof.x, res.dof.y, 30.0f, res.dof.h}); r.Frames(1);
        CHECK(d.v.dof && res.focus.w > 0);
    }
    {   // no values: nothing drawn; an empty preset list
        P::PostProcessModel m;
        S::PostProcessResult res;
        auto r = MakeRig(400, 300, [&](float x, float y, float w, float h) { res = S::DrawPostProcess(m, x, y, w, h); });
        r.Frames(2);
        CHECK(res.preset.w == 0 && res.contentHeight == 0.0f);
        CHECK(std::string(P::PostChainText(true)) == "GPU post chain: on");
    }

    // ================= Player and Rendering (Project Settings) =================
    {   // the sections, golden; typing in the fields (the size fields are plain numbers, no steppers), Ambient, Skybox, Apply
        panelfix::PlayerData d;
        panelfix::RenderingData rd;
        S::PlayerSettingsResult res;
        S::RenderingSettingsResult rres;
        bool changed = false, apply = false;
        float widthW = 0.0f;   // what the Width row should take: "000000" and the frame padding, the 8-pixel gap, the label
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) {
            widthW = UI::CalcTextWidth("000000") + 24.0f + 8.0f + UI::CalcTextWidth("Width");
            res = S::DrawPlayerSettings(d.model, x, y, w, 180.0f);
            rres = S::DrawRenderingSettings(rd.model, x, y + 200.0f, w, h - 200.0f);
            if (res.intent.changed || rres.intent.changed) changed = true;
            if (rres.intent.apply) apply = true;
        });
        r.Frames(3);
        CHECK(res.company.w > 0 && res.height.x > res.width.x + res.width.w && rres.apply.w > 0);
        // five digits wide and no -/+ buttons: the field, the gap and the label only (editor/main.cpp)
        CHECK(std::abs(res.width.w - widthW) < 1.0f);
        Check("editorpanels_player_rendering", r.Draw());
        r.Click(res.version); r.sim.Press(Key_End); r.Frame(); r.sim.Type("-beta"); r.Frame();
        CHECK(changed && std::strcmp(d.version, "1.4.2-beta") == 0);
        r.Click(res.width); r.sim.Press(Key_SelectAll); r.Frame(); r.sim.Type("1920"); r.Frame(); r.sim.Press(Key_Enter); r.Frame();
        CHECK(d.width == 1920 && d.height == 720);
        r.Click(res.height); r.sim.Press(Key_SelectAll); r.Frame(); r.sim.Type("1080"); r.Frame(); r.sim.Press(Key_Enter); r.Frame();
        CHECK(d.height == 1080);
        r.sim.Move(rres.ambient.x + 3.0f, CY(rres.ambient)); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(rd.ambient == 0.0f);
        r.Click(rres.skybox); CHECK(!rd.skybox);
        r.Click(rres.apply);  CHECK(apply);
    }
    {   // missing pointers and an unterminated buffer
        char company[8]; std::memset(company, 'C', sizeof company);
        P::PlayerSettingsModel m; m.company = company; m.companySize = sizeof company;
        P::RenderingSettingsModel rm;
        S::PlayerSettingsResult res;
        S::RenderingSettingsResult rres;
        auto r = MakeRig(400, 300, [&](float x, float y, float w, float h) {
            res = S::DrawPlayerSettings(m, x, y, w, 100.0f);
            rres = S::DrawRenderingSettings(rm, x, y + 120.0f, w, h - 120.0f);
        });
        r.Frames(2);
        CHECK(company[7] == '\0' && res.company.w > 0 && res.version.w == 0 && res.width.w == 0);
        CHECK(rres.ambient.w == 0 && rres.skybox.w == 0 && rres.apply.w > 0);
    }

    // ================= Loading screen (Project Settings) =================
    {   // the section, golden; Browse asks the host, Clear empties the logo, the accent box hides its colour, typing a path
        panelfix::LoadingData d;
        S::LoadingScreenResult res;
        P::LoadingScreenIntent seen;
        auto r = MakeRig(620, 360, [&](float x, float y, float w, float h) {
            res = S::DrawLoadingScreen(d.model, x, y, w, h);
            if (res.intent.changed) seen.changed = true;
            if (res.intent.browse) seen.browse = true;
        });
        r.Frames(3);
        CHECK(res.logo.w == 360.0f && res.browse.x > res.logo.x + res.logo.w && res.clear.x > res.browse.x + res.browse.w);
        CHECK(res.accent.w > 0 && res.accent.x > res.accentBox.x + res.accentBox.w && res.bgTop.y > res.accentBox.y && res.showBar.y > res.bgBottom.y);
        CHECK(res.contentHeight > 150.0f && res.contentHeight < 340.0f && UI::DroppedGeometry() == 0);
        Check("editorpanels_loadingscreen", r.Draw());
        r.Click(res.browse); CHECK(seen.browse && !seen.changed && d.logo[0] == 'A');
        r.Click(res.clear); CHECK(seen.changed && d.logo[0] == '\0');
        r.Frames(1);
        r.Click(UI::Rect{res.logo.x, res.logo.y, 60.0f, res.logo.h});
        r.sim.Type("Logo.png"); r.Frame();
        CHECK(std::strcmp(d.logo, "Logo.png") == 0);
        seen = P::LoadingScreenIntent();
        r.Click(UI::Rect{res.accentBox.x, res.accentBox.y, 30.0f, res.accentBox.h});
        CHECK(!d.accentOverride && seen.changed);
        r.Frames(1); CHECK(res.accent.w == 0);
        r.Click(UI::Rect{res.showBar.x, res.showBar.y, 30.0f, res.showBar.h}); CHECK(!d.showBar);
    }
    {   // no values: the note only; an unterminated logo buffer is ended
        char logo[6]; std::memset(logo, 'L', sizeof logo);
        P::LoadingScreenModel m; m.logo = logo; m.logoSize = sizeof logo;
        S::LoadingScreenResult res, r0;
        P::LoadingScreenModel none;
        auto r = MakeRig(400, 300, [&](float x, float y, float w, float h) {
            res = S::DrawLoadingScreen(m, x, y, w, 120.0f);
            r0 = S::DrawLoadingScreen(none, x, y + 140.0f, w, h - 140.0f);
        });
        r.Frames(2);
        CHECK(logo[5] == '\0' && res.logo.w > 0 && res.bgTop.w == 0 && res.showBar.w == 0);
        CHECK(r0.logo.w == 0 && r0.contentHeight > 0.0f);
    }

    // ================= Keyboard Shortcuts =================
    {   // the filter rule: any case, keys or description; a section with no match is left out
        const std::vector<P::ShortcutSection>& all = P::KeyboardShortcuts();
        CHECK(all.size() == 8 && all[0].rows.size() == 11 && std::string(all[7].title) == "Sprite Editor");
        CHECK(P::FilterShortcuts(all, "").size() == 8);
        std::vector<P::ShortcutSection> f = P::FilterShortcuts(all, "KNIFE");
        CHECK(f.size() == 1 && std::string(f[0].title) == "Mesh edit mode" && f[0].rows.size() == 1 && std::string(f[0].rows[0].keys) == "K");
        f = P::FilterShortcuts(all, "ctrl+shift+f");
        CHECK(f.size() == 1 && std::string(f[0].title) == "Script Editor");
        CHECK(P::FilterShortcuts(all, "zzz").empty() && P::FilterShortcuts(all, nullptr).size() == 8);
    }
    {   // the window, golden; typing in the filter narrows the list; the descriptions start at one column
        P::ShortcutsState st;
        S::ShortcutsResult res;
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) { res = S::DrawShortcuts(P::KeyboardShortcuts(), st, x, y, w, h); });
        r.Frames(3);
        CHECK(res.titles.size() == 8 && res.keys.size() == 55 && res.whats.size() == 55);
        CHECK(res.titles[0].y > res.filter.y + res.filter.h && res.keys[0].y > res.titles[0].y);
        CHECK(std::abs(res.whats[0].x - res.whats[5].x) < 0.5f && res.whats[0].x >= res.keys[0].x + 140.0f);
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_shortcuts", r.Draw());
        r.Click(res.filter);
        r.sim.Type("bevel"); r.Frames(2);
        CHECK(std::strcmp(st.filter, "bevel") == 0 && res.titles.size() == 2 && res.keys.size() == 2);
        Check("editorpanels_shortcuts_filtered", r.Draw());
    }

    // ================= Net =================
    {   // the window, golden; Simulate and the sliders write through their pointers; the percentages are fractions in the model
        panelfix::NetData d;
        S::NetSimResult res;
        bool changed = false;
        auto r = MakeRig(560, 480, [&](float x, float y, float w, float h) { res = S::DrawNetSim(d.model, x, y, w, h); changed |= res.intent.changed; });
        r.Frames(3);
        CHECK(res.rows.size() == 3 && res.table.h >= 200.0f && res.wire.y > res.duplicate.y);
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_net", r.Draw());
        CHECK(!changed);
        r.Click(res.simulate); CHECK(!d.enabled && changed);
        changed = false;
        r.sim.Move(res.latency.x + 3.0f, CY(res.latency)); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(d.latency == 0.0f && changed);
        d.loss = 0.3f; r.Frames(1);
        r.sim.Move(res.loss.x + 3.0f, CY(res.loss)); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(d.loss == 0.0f && d.jitter == 10.0f);
        P::NetSimModel none;
        P::ClampNetSim(none);   // no pointers: nothing to do
        d.jitter = -5.0f; d.duplicate = 3.0f; d.loss = std::nanf("");
        P::ClampNetSim(d.model);
        CHECK(d.jitter == 0.0f && d.duplicate == 1.0f && d.loss == 0.0f);
        CHECK(P::NetWireText(d.model) == "Wire  in 5.2 KB/s    out 2.1 KB/s");
    }
    {   // no simulator values: only the traffic
        P::NetSimModel m;
        S::NetSimResult res;
        auto r = MakeRig(400, 360, [&](float x, float y, float w, float h) { res = S::DrawNetSim(m, x, y, w, h); });
        r.Frames(2);
        CHECK(res.simulate.w == 0 && res.latency.w == 0 && res.wire.w > 0 && res.rows.empty());
    }

    // ================= Find in Files =================
    {   // the shared rules: the status line, a hit's label and preview
        P::FindInFilesModel m;
        CHECK(P::FindInFilesStatus(m, "x").find("Type a term") == 0);
        m.ran = true;
        CHECK(P::FindInFilesStatus(m, "zap") == "No matches for \"zap\".");
        m = panelfix::FindHits();
        CHECK(P::FindInFilesStatus(m, "health") == "3 matches");
        m.hits.resize(1); CHECK(P::FindInFilesStatus(m, "health") == "1 match");
        m.hits.resize(P::kFindInFilesCap); CHECK(P::FindInFilesStatus(m, "health") == "200 matches (capped at 200)");
        const P::FindInFilesModel f = panelfix::FindHits();
        CHECK(P::FindHitLabel(f.hits[0]) == "Player.okay:12" && P::FindHitLabel(f.hits[2]) == "Hud.cs:7");
        P::FindHit longHit{"a.okay", 1, std::string(300, 'q')};
        CHECK(P::FindHitPreview(longHit).size() == 120 && P::FindHitLabel(longHit) == "a.okay:1");
        // a bad regex: its error replaces the status line; Replace in All Files needs a term and hits
        P::FindInFilesModel e = panelfix::FindHits();
        e.error = "regex: missing )";
        CHECK(P::FindInFilesStatus(e, "(a") == "regex: missing )");
        P::FindInFilesState fs;
        CHECK(!P::FindInFilesCanReplace(f, fs));
        std::strcpy(fs.query, "health");
        CHECK(P::FindInFilesCanReplace(f, fs) && !P::FindInFilesCanReplace(P::FindInFilesModel(), fs));
    }
    {   // the window, golden; Enter or Search asks for a search only with a term; a click on a hit opens it
        P::FindInFilesModel m = panelfix::FindHits();
        P::FindInFilesState st;
        std::strcpy(st.query, "health");
        S::FindInFilesResult res;
        P::FindInFilesIntent seen;
        auto r = MakeRig(600, 420, [&](float x, float y, float w, float h) {
            res = S::DrawFindInFiles(m, st, x, y, w, h);
            seen.search |= res.intent.search;
            if (res.intent.open >= 0) seen.open = res.intent.open;
        });
        r.Frames(3);
        CHECK(res.hits.size() == 3 && res.search.x > res.query.x + res.query.w && res.search.w == 80.0f);
        CHECK(res.status.y > res.query.y && res.hits[0].y > res.status.y && res.hits[1].y > res.hits[0].y);
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_findinfiles", r.Draw());
        CHECK(!seen.search && seen.open < 0);
        r.Click(res.hits[1]); CHECK(seen.open == 1 && !seen.search);
        r.Click(res.search); CHECK(seen.search);
        seen = P::FindInFilesIntent();
        st.query[0] = '\0'; r.Frames(1);
        r.Click(res.search); CHECK(!seen.search);                 // nothing typed: no search
        r.Click(res.query); r.sim.Type("die"); r.Frame();
        CHECK(std::strcmp(st.query, "die") == 0 && !seen.search);   // typing alone does not search
        r.sim.Press(Key_Enter); r.Frame();
        CHECK(seen.search);
    }
    {   // Match case, Whole word and Regex: each change searches again (with a term); Replace in All Files only with hits
        P::FindInFilesModel m = panelfix::FindHits();
        P::FindInFilesState st;
        std::strcpy(st.query, "health");
        S::FindInFilesResult res;
        P::FindInFilesIntent seen;
        auto r = MakeRig(600, 420, [&](float x, float y, float w, float h) {
            res = S::DrawFindInFiles(m, st, x, y, w, h);
            seen.search |= res.intent.search; seen.replaceAll |= res.intent.replaceAll;
        });
        r.Frames(3);
        CHECK(res.matchCase.w > 0 && res.wholeWord.x > res.matchCase.x && res.regex.x > res.wholeWord.x);
        CHECK(res.matchCase.y > res.query.y && res.replaceField.y > res.matchCase.y && res.replace.x > res.replaceField.x);
        CHECK(res.replace.w == 160.0f && res.status.y > res.replace.y);
        r.Click(res.matchCase); CHECK(st.matchCase && seen.search);
        seen = P::FindInFilesIntent();
        r.Click(res.wholeWord); CHECK(st.wholeWord && seen.search);
        seen = P::FindInFilesIntent();
        r.Click(res.regex); CHECK(st.regex && seen.search && !seen.replaceAll);
        r.Click(res.replaceField); r.sim.Type("hp"); r.Frame();
        CHECK(std::strcmp(st.replaceWith, "hp") == 0);
        r.Click(res.replace); CHECK(seen.replaceAll);
        seen = P::FindInFilesIntent();
        m.hits.clear(); r.Frames(1);
        r.Click(res.replace); CHECK(!seen.replaceAll);              // nothing found: the button does nothing
        st.query[0] = '\0'; seen = P::FindInFilesIntent(); r.Frames(1);
        r.Click(res.regex); CHECK(!st.regex && !seen.search);       // no term: an option change does not search
        m.error = "regex: missing )"; r.Frames(2);
        Check("editorpanels_findinfiles_error", r.Draw());
    }
    {   // before the first search: the hint; focusQuery puts the keyboard in the field
        P::FindInFilesModel m;
        P::FindInFilesState st;
        S::FindInFilesResult res;
        auto r = MakeRig(600, 300, [&](float x, float y, float w, float h) { res = S::DrawFindInFiles(m, st, x, y, w, h); m.focusQuery = false; });
        r.Frames(2);
        CHECK(res.hits.empty() && res.status.w > 300.0f);
        r.sim.Type("abc"); r.Frame();
        CHECK(st.query[0] == '\0');                                 // no field has the keyboard yet
        m.focusQuery = true; r.Frame();
        r.sim.Type("abc"); r.Frame();
        CHECK(std::strcmp(st.query, "abc") == 0);
    }

    // ================= Data Asset =================
    {   // the window, golden; typing edits the field in place; X, Add Field and Save are asked of the host
        panelfix::DataAssetData d;
        S::DataAssetResult res;
        P::DataAssetIntent seen;
        auto r = MakeRig(560, 320, [&](float x, float y, float w, float h) {
            res = S::DrawDataAsset(d.model, x, y, w, h);
            seen.changed |= res.intent.changed; seen.add |= res.intent.add; seen.save |= res.intent.save;
            if (res.intent.remove >= 0) seen.remove = res.intent.remove;
        });
        r.Frames(3);
        CHECK(res.keys.size() == 3 && res.values.size() == 3 && res.removes.size() == 3);
        CHECK(res.keys[0].w == 140.0f && res.values[0].x > res.keys[0].x + res.keys[0].w && res.values[0].w == 200.0f);
        CHECK(res.removes[0].x > res.values[0].x + res.values[0].w && res.add.y > res.keys[2].y && res.save.x > res.add.x);
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_dataasset", r.Draw());
        CHECK(!seen.changed && !seen.add && !seen.save && seen.remove < 0);
        r.Click(UI::Rect{res.values[1].x + res.values[1].w - 20.0f, res.values[1].y, 10.0f, res.values[1].h});
        r.sim.Type("0"); r.Frames(2);
        CHECK(d.fields[1].second == "4.50" && seen.changed && d.fields[1].first == "speed");
        r.Click(res.removes[2]); CHECK(seen.remove == 2 && d.fields.size() == 3);   // the host erases it
        r.Click(res.add); CHECK(seen.add);
        r.Click(res.save); CHECK(seen.save);
    }
    {   // no fields; a value longer than the field holds is shown cut, and left alone until typed in
        panelfix::DataAssetData d;
        d.fields = {{"long", std::string(400, 'v')}};
        P::DataAssetModel none; none.path = "x.okaydata";
        S::DataAssetResult res, r0;
        auto r = MakeRig(560, 320, [&](float x, float y, float w, float h) {
            res = S::DrawDataAsset(d.model, x, y, w, 140.0f);
            r0 = S::DrawDataAsset(none, x, y + 160.0f, w, h - 160.0f);
        });
        r.Frames(2);
        CHECK(res.keys.size() == 1 && d.fields[0].second.size() == 400 && !res.intent.changed);
        CHECK(r0.keys.empty() && r0.add.w > 0);
    }

    // ================= Plugin Manager =================
    {   // the window, golden; every button and switch comes back as the intent (the host does the work)
        P::PluginManagerModel m = panelfix::Plugins();
        S::PluginManagerResult res;
        P::PluginManagerIntent seen;
        auto r = MakeRig(560, 360, [&](float x, float y, float w, float h) {
            res = S::DrawPluginManager(m, x, y, w, h);
            const P::PluginManagerIntent& in = res.intent;
            seen.newPlugin |= in.newPlugin; seen.reloadAll |= in.reloadAll; seen.openFolder |= in.openFolder;
            if (in.toggle >= 0) seen.toggle = in.toggle;
            if (in.edit >= 0) seen.edit = in.edit;
            if (in.openClose >= 0) seen.openClose = in.openClose;
        });
        r.Frames(3);
        CHECK(res.switches.size() == 3 && res.names.size() == 3 && res.edits.size() == 3 && !res.empty);
        CHECK(res.openCloses[0].w > 0 && res.openCloses[1].w == 0 && res.openCloses[2].w == 0);
        CHECK(res.names[1].y > res.names[0].y + 30.0f && res.edits[0].x > res.names[0].x + res.names[0].w);
        CHECK(res.reloadAll.x > res.newPlugin.x + res.newPlugin.w && res.table.y > res.newPlugin.y);
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_plugins", r.Draw());
        CHECK(seen.toggle < 0 && seen.edit < 0 && seen.openClose < 0 && !seen.newPlugin);
        r.Click(res.switches[1]); CHECK(seen.toggle == 1 && !m.rows[1].enabled);   // the model is not changed: the host switches it
        r.Click(res.edits[2]); CHECK(seen.edit == 2);
        r.Click(res.openCloses[0]); CHECK(seen.openClose == 0);
        r.Click(res.newPlugin); CHECK(seen.newPlugin);
        r.Click(res.reloadAll); CHECK(seen.reloadAll);
        r.Click(res.openFolder); CHECK(seen.openFolder);
        CHECK(std::string(P::PluginScopeText(m.rows[2])) == "All projects" && std::string(P::PluginScopeText(m.rows[0])) == "This project");
    }
    {   // no plugins: the hint line and an empty table
        P::PluginManagerModel m;
        S::PluginManagerResult res;
        auto r = MakeRig(560, 300, [&](float x, float y, float w, float h) { res = S::DrawPluginManager(m, x, y, w, h); });
        r.Frames(2);
        CHECK(res.empty && res.switches.empty());
    }

    // ================= Map Humanoid Bones =================
    {   // the shared rules: a pick's closed text, the verdict line
        CHECK(P::BonePickText("", "Bone.000") == "Auto (Bone.000)" && P::BonePickText("-", "x") == "None" && P::BonePickText("joint12", "x") == "joint12");
        panelfix::BoneMapData d;
        CHECK(P::BoneMapStatus(d.model) == "Not a humanoid yet: no left thigh");
        d.model.humanoid = true; d.model.found = 15;
        CHECK(P::BoneMapStatus(d.model) == "Humanoid: 15/15 bones mapped");
        CHECK(std::string(P::kBoneSlotNames[0]) == "Hips" && std::string(P::kBoneSlotNames[P::kBoneSlotCount - 1]) == "Right foot");
    }
    {   // the window, golden; a pick from a slot's list is written in place; Clear sets every slot to Auto; the action button only works
        // while the picks make a humanoid
        panelfix::BoneMapData d;
        S::BoneMapResult res;
        P::BoneMapIntent seen;
        auto r = MakeRig(460, 860, [&](float x, float y, float w, float h) {
            res = S::DrawBoneMap(d.model, x, y, w, h);
            seen.changed |= res.intent.changed; seen.apply |= res.intent.apply;
        });
        r.Frames(3);
        CHECK(res.slots.size() == 15 && !res.empty && res.status.y > res.slots[14].y && res.clear.y > res.status.y);
        CHECK(res.slots[1].x == res.slots[0].x && res.slots[1].y > res.slots[0].y + 20.0f && res.slots[0].x > 16.0f + 100.0f);
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_bonemap", r.Draw());
        r.Click(res.apply); CHECK(!seen.apply);   // not a humanoid: the button is off
        r.Click(res.slots[9]); r.Frames(2);       // (the list takes its place a frame after it opens)
        CHECK(res.listRows.size() == 9 && res.popup.h > 0.0f);
        Check("editorpanels_bonemap_list", r.Draw());
        r.Click(res.listRows[7]);                // "joint13" (Auto, None, then the bones)
        r.Frames(1);
        CHECK(d.picks[9] == "joint13" && seen.changed && res.listRows.empty());   // the list closed
        seen.changed = false;
        r.Click(res.clear);
        CHECK(seen.changed);
        for (int b = 0; b < P::kBoneSlotCount; ++b) CHECK(d.picks[b].empty());
        d.model.humanoid = true; d.model.found = 15; r.Frames(1);
        r.Click(res.apply); CHECK(seen.apply);
    }
    {   // a retarget's map says so on its button; no model: only the hint
        panelfix::BoneMapData d;
        d.model.forRetarget = true;
        S::BoneMapResult res;
        auto r = MakeRig(460, 700, [&](float x, float y, float w, float h) { res = S::DrawBoneMap(d.model, x, y, w, h); });
        r.Frames(2);
        CHECK(res.apply.w > res.clear.w);
        P::BoneMapModel none;
        auto r2 = MakeRig(460, 200, [&](float x, float y, float w, float h) { res = S::DrawBoneMap(none, x, y, w, h); });
        r2.Frames(2);
        CHECK(res.empty && res.slots.empty() && res.apply.w == 0);
    }

    // ================= Save Manager =================
    {   // the shared rules: the rows sorted by key with their type and payload, the type names, the filter, a stored value
        const P::SaveManagerModel m = panelfix::Saves();
        CHECK(m.rows.size() == 5 && m.rows[0].key == "coins" && m.rows[4].key == "spawn");
        CHECK(m.rows[0].type == 'i' && m.rows[0].payload == "120" && m.rows[3].payload == "Ada");
        CHECK(std::string(P::SaveTypeName('v')) == "Vector3" && std::string(P::SaveTypeName('s')) == "string" && std::string(P::SaveTypeName('?')) == "string");
        CHECK(P::SaveKeyMatches("player.name", "NAME") && !P::SaveKeyMatches("coins", "name") && P::SaveKeyMatches("x", "") && P::SaveKeyMatches("x", nullptr));
        CHECK(P::SaveRawValue('f', "2.5") == "f:2.5");
        std::vector<std::pair<std::string, std::string>> odd = {{"e", ""}, {"t", "i"}};
        const std::vector<P::SaveKeyRow> rows = P::SaveKeyRows(odd);
        CHECK(rows[0].type == 's' && rows[0].payload.empty() && rows[1].type == 'i' && rows[1].payload.empty());
    }
    {   // the window, golden; a value typed in comes back with its type; X; a new key; the filter; Clear asks first
        P::SaveManagerModel m = panelfix::Saves();
        P::SaveManagerState st;
        S::SaveManagerResult res;
        P::SaveManagerIntent seen;
        auto r = MakeRig(620, 520, [&](float x, float y, float w, float h) {
            res = S::DrawSaveManager(m, st, x, y, w, h);
            const P::SaveManagerIntent& in = res.intent;
            if (in.choose >= 0) seen.choose = in.choose;
            if (in.edit >= 0) { seen.edit = in.edit; seen.editRaw = in.editRaw; }
            if (in.remove >= 0) seen.remove = in.remove;
            if (in.add) { seen.add = true; seen.addKey = in.addKey; seen.addRaw = in.addRaw; }
            seen.reload |= in.reload; seen.save |= in.save; seen.clearAll |= in.clearAll;
        });
        r.Frames(3);
        CHECK(res.values.size() == 5 && res.removes.size() == 5 && !res.noFile && res.reload.w > 0);
        CHECK(res.reload.x > res.fileCombo.x + res.fileCombo.w && res.reload.x + res.reload.w <= 620.0f - 16.0f + 0.5f);   // beside the list, inside
        CHECK(res.save.y > res.add.y && res.count.x > res.clearAll.x);
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_savemanager", r.Draw());
        r.Click(res.values[0]); r.sim.Press(Key_End); r.Frame(); r.sim.Type("5"); r.Frames(2);
        CHECK(seen.edit == 0 && seen.editRaw == "i:1205");
        r.Click(res.removes[2]); CHECK(seen.remove == 2);
        r.Click(res.newKey); r.sim.Type("level"); r.Frame();
        r.Click(res.newType); r.Frames(2);
        CHECK(res.listRows.size() == 5);
        r.Click(res.listRows[1]); CHECK(st.newType == 1);
        r.Click(res.newValue); r.sim.Press(Key_SelectAll); r.Frame(); r.sim.Type("3"); r.Frame();
        r.Click(res.add);
        CHECK(seen.add && seen.addKey == "level" && seen.addRaw == "i:3" && st.newKey[0] == '\0');
        seen.add = false;
        r.Click(res.add); CHECK(!seen.add);       // no key typed: nothing to add
        r.Click(res.reload); CHECK(seen.reload);
        r.Click(res.save); CHECK(seen.save);
        r.Click(res.clearAll); r.Frames(2);       // (the box is centred a frame after it opens)
        CHECK(res.confirmClear.w > 0 && !seen.clearAll);
        Check("editorpanels_savemanager_clear", r.Draw());
        r.Click(res.cancelClear); r.Frames(1);
        CHECK(!seen.clearAll); CHECK(res.confirmClear.w == 0);
        r.Click(res.clearAll); r.Frames(2); r.Click(res.confirmClear); CHECK(seen.clearAll);
        r.Click(res.filter); r.sim.Type("PLAY"); r.Frames(2);
        CHECK(res.values.size() == 1 && res.rowIndex[0] == 3);
        r.Click(res.fileCombo); r.Frames(2);
        CHECK(res.listRows.size() == 3);
        r.Click(res.listRows[2]); CHECK(seen.choose == 2);
    }
    {   // no file chosen yet: the list and the hint only
        P::SaveManagerModel m = panelfix::Saves();
        m.selected.clear();
        P::SaveManagerState st;
        S::SaveManagerResult res;
        auto r = MakeRig(620, 300, [&](float x, float y, float w, float h) { res = S::DrawSaveManager(m, st, x, y, w, h); });
        r.Frames(2);
        CHECK(res.noFile && res.values.empty() && res.reload.w == 0 && res.save.w == 0);
    }

    // ================= Material =================
    {   // the shared rules: the preview colour (albedo plus 60% emissive, at most 1) and where the ball goes
        P::MaterialModel m = panelfix::Material();
        float c[3]; P::MaterialPreviewColor(m, c);
        CHECK(std::abs(c[0] - 0.91f) < 1e-5f && std::abs(c[1] - 0.20f) < 1e-5f);
        m.color[0] = 0.9f; m.emissive[0] = 1.0f; P::MaterialPreviewColor(m, c); CHECK(c[0] == 1.0f);
        const P::MaterialBall b = P::MaterialBallIn(0.0f, 0.0f, 200.0f, 74.0f);
        CHECK(b.cx == 100.0f && b.cy == 37.0f && std::abs(b.r - 23.68f) < 1e-4f && b.spotX < b.cx && b.spotY < b.cy && b.glintR < b.spotR);
    }
    {   // the window, golden; the fields edit the model in place; "..." asks the host for the file dialog; Apply only with a mesh selected
        P::MaterialModel m = panelfix::Material();
        S::MaterialResult res;
        P::MaterialIntent seen;
        auto r = MakeRig(460, 520, [&](float x, float y, float w, float h) {
            res = S::DrawMaterial(m, x, y, w, h);
            seen.changed |= res.intent.changed; seen.browse |= res.intent.browse; seen.save |= res.intent.save; seen.apply |= res.intent.apply;
        });
        r.Frames(3);
        CHECK(res.preview.h == 74.0f && res.albedo.y > res.preview.y + 74.0f && res.shininess.w > 0 && res.tiling.y > res.texture.y);
        CHECK(res.browse.x > res.texture.x + 200.0f && res.apply.x > res.save.x);
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_material", r.Draw());
        CHECK(!seen.changed);
        r.Click(res.apply); CHECK(!seen.apply);
        r.Click(res.unlit); CHECK(m.unlit && seen.changed);
        r.Click(res.texture); r.sim.Press(Key_End); r.Frame(); r.sim.Press(Key_Backspace); r.Frame(); r.sim.Press(Key_Backspace); r.Frame(); r.sim.Press(Key_Backspace); r.Frame();
        r.sim.Type("jpg"); r.Frame();
        CHECK(m.texture == "Assets/Textures/paint.jpg");
        r.Click(res.browse); CHECK(seen.browse);
        r.Click(res.save); CHECK(seen.save);
        m.canApply = true; r.Frames(1);
        r.Click(res.apply); CHECK(seen.apply);
        r.sim.Move(res.specular.x + 2.0f, CY(res.specular)); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frames(2);
        CHECK(m.specular == 0.0f && res.shininess.w == 0);   // no specular: no Shininess
    }

    // ================= Frame time =================
    {   // the shared rules: the colours, the lines, the sections biggest first (the Scene view always listed)
        const P::PerfOverlayModel m = panelfix::Perf();
        float c[3];
        P::PerfFpsColor(142.0, c); CHECK(c[1] == 0.85f);
        P::PerfFpsColor(75.0, c); CHECK(c[0] == 0.95f && c[1] == 0.80f);
        P::PerfFpsColor(30.0, c); CHECK(c[1] == 0.45f);
        CHECK(P::PerfTimesText(m) == "7.04 ms avg   95%: 8.90   worst: 14.20   1% low: 74 FPS");
        CHECK(P::PerfSleepText(m) == "Without the limiter's sleep: 4.10 ms = 244 FPS possible");
        P::PerfOverlayModel q; CHECK(P::PerfSleepText(q).empty() && P::PerfPlotTop(q) == (float)16.7);
        CHECK(P::PerfPlotCaption(m) == "last 120 frames, top 17 ms");
        CHECK(m.sections.size() == 4 && m.sections[0].name == "Sleep" && m.sections[1].name == "Scene view" && m.sections[3].name == "Scripts");
        const std::vector<P::PerfSection> idle = P::PerfSections({"Scene view", "UI"}, {0.0, 0.0}, 0.0, 0);
        CHECK(idle.size() == 1 && idle[0].name == "Scene view" && idle[0].share == 0.0f);
        CHECK(P::PerfSectionBar(m.sections[1]) == "2.40 ms  34%");
        CHECK(P::PerfViewText(m) == "Scene view 1280x720, scale 1.00");
    }
    {   // the window, golden; it says how big it needs to be; the two buttons come back as the intent
        P::PerfOverlayModel m = panelfix::Perf();
        S::PerfOverlayResult res;
        P::PerfOverlayIntent seen;
        auto r = MakeRig(640, 480, [&](float x, float y, float w, float h) {
            res = S::DrawPerfOverlay(m, x, y, w, h);
            seen.copyReport |= res.intent.copyReport; seen.openProfiler |= res.intent.openProfiler;
        });
        r.Frames(3);
        CHECK(res.bars.size() == 4 && res.plot.w == 360.0f && res.bars[0].w == 150.0f);
        CHECK(res.contentW >= 360.0f && res.contentW < 608.0f && res.contentH > res.openProfiler.y + res.openProfiler.h - 12.0f - 1.0f);
        CHECK(res.contentH <= res.openProfiler.y + res.openProfiler.h - 12.0f + 0.5f);
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_perfoverlay", r.Draw());
        r.Click(res.copyReport); CHECK(seen.copyReport && !seen.openProfiler);
        r.Click(res.openProfiler); CHECK(seen.openProfiler);
        m.plot.clear(); m.collecting = true; r.Frames(2);
        CHECK(res.plot.w == 0);
    }

    // ================= Input =================
    {   // the window, golden; the first three sections open; Clear; the Response curves section edits the chosen action's response
        P::InputPanelModel m = panelfix::InputPanel();
        P::InputPanelState st;
        S::InputPanelResult res;
        P::InputPanelIntent seen;
        auto r = MakeRig(620, 760, [&](float x, float y, float w, float h) {
            res = S::DrawInputPanel(m, st, x, y, w, h);
            const P::InputPanelIntent& in = res.intent;
            seen.clearEvents |= in.clearEvents; seen.record |= in.record; seen.stop |= in.stop; seen.replay |= in.replay;
            seen.copyText |= in.copyText; seen.responseChanged |= in.responseChanged; seen.resetCurve |= in.resetCurve;
        });
        r.Frames(3);
        CHECK(res.headers[1].y > res.headers[0].y + 60.0f && res.events.h > 100.0f && res.headers[3].y > res.events.y + res.events.h);
        CHECK(res.record.w == 0 && res.deadzone.w == 0);   // the last two start closed
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_input", r.Draw());
        r.Click(res.clear); CHECK(seen.clearEvents);
        r.Click(res.headers[0]); r.Click(res.headers[1]); r.Click(res.headers[2]);   // close the first three
        r.Click(res.headers[3]);
        CHECK(res.record.w > 0);
        r.Click(res.record); CHECK(seen.record);
        r.Click(res.stop); CHECK(seen.stop);
        r.Click(res.replay); CHECK(seen.replay);
        r.Click(res.copyText); CHECK(seen.copyText);
        r.Click(res.headers[4]);
        CHECK(res.deadzone.w > 0 && res.invertX.w > 0 && res.plot.h == 90.0f);
        Check("editorpanels_input_curves", r.Draw());
        CHECK(!seen.responseChanged);
        r.Click(res.invertY); CHECK(m.response.invertY && seen.responseChanged);
        r.sim.Move(res.deadzone.x + res.deadzone.w * 0.5f, CY(res.deadzone)); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(m.response.deadzone > 0.2f && m.response.deadzone < 0.7f);
        r.Click(res.reset); CHECK(seen.resetCurve);
        r.Click(res.action); r.Frames(2);
        CHECK(res.listRows.size() == 3);
        r.Click(res.listRows[2]); CHECK(st.action == 2);
        m.direction = false; r.Frames(2);
        CHECK(res.invertX.w == 0);
    }
    {   // not playing: the hint; no player, no actions
        P::InputPanelModel m;
        P::InputPanelState st;
        st.action = 7;
        S::InputPanelResult res;
        auto r = MakeRig(500, 700, [&](float x, float y, float w, float h) { res = S::DrawInputPanel(m, st, x, y, w, h); });
        r.Frames(2);
        CHECK(res.headers[4].w > 0);
        r.Click(res.headers[4]);
        CHECK(st.action == 0); CHECK(res.deadzone.w == 0); CHECK(res.action.w > 0);
    }

    // ================= Timers =================
    {   // the shared rule: the dump split into lines
        CHECK(P::TextLines("").empty());
        CHECK(P::TextLines("a\nb\n") == std::vector<std::string>({"a", "b"}));
        CHECK(P::TextLines("a\r\n\nb") == std::vector<std::string>({"a", "", "b"}));
        CHECK(P::TextLines(panelfix::kTimersText).size() == 6);
    }
    {   // the window, golden: a line each, top to bottom; a long dump scrolls inside the body
        S::TimersResult res;
        auto r = MakeRig(560, 260, [&](float x, float y, float w, float h) { res = S::DrawTimers(panelfix::kTimersText, x, y, w, h); });
        r.Frames(3);
        CHECK(res.lines.size() == 6 && res.lines[1].y > res.lines[0].y && res.lines[5].y > res.lines[4].y);
        CHECK(res.body.w > 400.0f && res.lines[0].y >= res.body.y);
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_timers", r.Draw());
        std::string many;
        for (int i = 0; i < 60; ++i) many += "  #" + std::to_string(i) + " invoke 0.1s / 1s\n";
        auto r2 = MakeRig(560, 260, [&](float x, float y, float w, float h) { res = S::DrawTimers(many, x, y, w, h); });
        r2.Frames(2);
        CHECK(res.lines.size() == 60);
        const float before = res.lines[0].y;
        r2.sim.Move(CX(res.body), CY(res.body)); r2.sim.wheel = -3.0f; r2.Frame(); r2.sim.wheel = 0.0f; r2.Frames(2);
        CHECK(res.lines[0].y < before);                                 // the wheel scrolls the body
    }

    // ================= Input Actions =================
    {   // the shared rules: a map's title, an action's row, where each binding's fields start
        const P::InputActionsModel m = panelfix::InputActions();
        CHECK(P::InputMapTitle(m.maps[0]) == "Gameplay  (priority 0)");
        CHECK(P::InputMapTitle(m.maps[1]) == "UI  (priority 10, consumes, off)");
        CHECK(P::InputActionLabel(m.maps[0].actions[0]) == "    Move  [vector2]");
        CHECK(P::InputPartOffsets(m) == std::vector<std::size_t>({0, 4, 5}));
        CHECK(P::InputPartOffsets(P::InputActionsModel()) == std::vector<std::size_t>({0}));
    }
    {   // the window, golden; every button and the action rows answer with their intent
        P::InputActionsModel m = panelfix::InputActions();
        P::InputActionsState st;
        std::strcpy(st.file, "Assets/input.okayinput");
        st.selected = "Move";
        st.status = "Loaded.";
        S::InputActionsResult res;
        P::InputActionsIntent seen;
        auto r = MakeRig(960, 680, [&](float x, float y, float w, float h) {   // tall enough for Delete action without scrolling
            res = S::DrawInputActions(m, st, x, y, w, h);
            const P::InputActionsIntent& i = res.intent;
            seen.load |= i.load; seen.save |= i.save; seen.newDefault |= i.newDefault; seen.migrate |= i.migrate; seen.useInGame |= i.useInGame;
            seen.addMap |= i.addMap; seen.addScheme |= i.addScheme; seen.addAction |= i.addAction; seen.deleteAction |= i.deleteAction;
            if (i.select) { seen.select = true; seen.selectName = i.selectName; }
            if (i.partBinding >= 0) { seen.partBinding = i.partBinding; seen.partIndex = i.partIndex; seen.partText = i.partText; }
            if (i.procBinding >= 0) { seen.procBinding = i.procBinding; seen.procText = i.procText; }
            if (i.removeBinding >= 0) seen.removeBinding = i.removeBinding;
            if (i.addBinding >= 0) seen.addBinding = i.addBinding;
            if (i.addInteraction) { seen.addInteraction = true; seen.interactionText = i.interactionText; }
            if (i.removeInteraction >= 0) seen.removeInteraction = i.removeInteraction;
        });
        r.Frames(3);
        CHECK(res.file.w > 800.0f && res.load.y > res.file.y && res.save.x > res.load.x && res.useInGame.x > res.migrate.x);
        CHECK(res.unsaved.x > res.useInGame.x && res.status.y > res.load.y);
        CHECK(res.left.y > res.status.y && res.right.x > res.left.x + res.left.w && res.right.y == res.left.y);
        CHECK(res.mapTitles.size() == 2 && res.actions.size() == 4 && res.actions[3].y > res.mapTitles[1].y);
        CHECK(res.tabs[0].w > 0 && res.tabs[1].x > res.tabs[0].x && res.tabs[2].x > res.tabs[1].x);
        CHECK(res.parts.size() == 5 && res.processors.size() == 2 && res.removeBindings.size() == 2);
        CHECK(res.parts[1].x > res.parts[0].x && res.parts[4].y > res.parts[0].y && res.processors[0].y > res.parts[0].y);
        CHECK(res.interactions.size() == 1 && res.removeInteractions.size() == 1 && res.deleteAction.y > res.interactions[0].y);
        CHECK(st.partBufs.size() == 5 && std::strcmp(st.partBufs[3].data(), "key:d") == 0);
        CHECK(std::strcmp(st.procBufs[1].data(), "deadzone(0.2,0.9)") == 0);
        CHECK(UI::DroppedGeometry() == 0);
        Check("editorpanels_inputactions", r.Draw());
        r.Click(res.load); CHECK(seen.load);
        r.Click(res.save); CHECK(seen.save);
        r.Click(res.newDefault); CHECK(seen.newDefault);
        r.Click(res.migrate); CHECK(seen.migrate);
        r.Click(res.useInGame); CHECK(seen.useInGame);
        r.Click(res.addMap); CHECK(seen.addMap);
        r.Click(res.addScheme); CHECK(seen.addScheme);
        r.Click(res.addAction); CHECK(seen.addAction);
        r.Click(res.actions[1]); CHECK(seen.select && seen.selectName == "Jump");
        r.Click(res.removeBindings[1]); CHECK(seen.removeBinding == 1);
        r.Click(res.addBinding[4]); CHECK(seen.addBinding == 4);
        r.Click(res.removeInteractions[0]); CHECK(seen.removeInteraction == 0);
        r.Click(res.deleteAction); CHECK(seen.deleteAction);
        CHECK(!seen.addInteraction && seen.partBinding < 0);
        // the name field and the file field edit the state; the type list picks + Action's type
        r.Click(res.name); r.sim.Type("Dash"); r.Frame();
        CHECK(std::strcmp(st.name, "Dash") == 0);
        r.Click(res.file); r.sim.Press(Key_End); r.Frame(); r.sim.Type("2"); r.Frame();
        CHECK(std::strcmp(st.file, "Assets/input.okayinput2") == 0);
        // a binding field: typing keeps the text while the field has the keyboard, Enter hands it on
        r.Click(res.parts[2]); r.sim.Press(Key_End); r.Frame();
        for (int k = 0; k < 8; ++k) { r.sim.Press(Key_Backspace); r.Frame(); }
        r.sim.Type("key:left"); r.Frame();
        CHECK(std::strcmp(st.partBufs[2].data(), "key:left") == 0 && seen.partBinding < 0);
        r.sim.Press(Key_Enter); r.Frame();
        CHECK(seen.partBinding == 0 && seen.partIndex == 2 && seen.partText == "key:left");
        // leaving the field without the host taking the text: it shows the model's text again
        r.Click(res.interaction); r.Frame();
        CHECK(std::strcmp(st.partBufs[2].data(), "key:a") == 0);
        r.sim.Type("tap(0.2)"); r.Frame(); r.sim.Press(Key_Enter); r.Frame();
        CHECK(seen.addInteraction && seen.interactionText == "tap(0.2)");
        r.Click(res.processors[0]); r.sim.Type("invert(y)"); r.Frame(); r.sim.Press(Key_Enter); r.Frame();
        CHECK(seen.procBinding == 0 && seen.procText == "invert(y)");
        // the Check and Live tabs
        r.Click(res.tabs[1]); r.Frames(1);
        CHECK(res.problems.size() == 1 && res.parts.empty() && res.deleteAction.w == 0);
        Check("editorpanels_inputactions_check", r.Draw());
        r.Click(res.tabs[2]); r.Frames(1);
        CHECK(res.liveLines.size() == 3 && res.problems.empty());   // the controller, "Player 1" and its line
        Check("editorpanels_inputactions_live", r.Draw());
        st.forceTab = 0; r.Frames(2);                                   // a tool can choose the tab
        CHECK(res.parts.size() == 5 && st.forceTab == -1);
    }
    {   // nothing selected, no problems, no player: the hints
        P::InputActionsModel m = panelfix::InputActions();
        m.hasAction = false; m.problems.clear(); m.players.clear(); m.dirty = false;
        P::InputActionsState st;
        S::InputActionsResult res;
        auto r = MakeRig(960, 600, [&](float x, float y, float w, float h) { res = S::DrawInputActions(m, st, x, y, w, h); });
        r.Frames(2);
        CHECK(res.parts.empty() && res.deleteAction.w == 0 && res.unsaved.w == 0 && res.status.w == 0);
        st.forceTab = 1; r.Frames(2);
        CHECK(res.problems.size() == 1);                                // "No problems found."
        st.forceTab = 2; r.Frames(2);
        CHECK(res.liveLines.size() == 2);                               // the controller line and the no-player hint
        st.newType = 9; r.Frames(1);
        CHECK(st.newType == 0);                                         // a type out of range is put back
    }

    TEST_MAIN_RESULT();
}
