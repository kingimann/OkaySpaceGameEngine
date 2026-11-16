// The editor's Problems, Stats, History, Tags and Layers, Physics, Profiler, Import settings, Post-processing, Player and Rendering
// panels drawn with OkayUI (okayui/screens/OkayUIEditorPanels), and the toolkit pieces they needed (docs/own_ui_toolkit.md sections 9
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
            UI::PushStyleVar(UI::StyleVar_CellPadding, UI::Vec2{10.0f, 6.0f});
            if (UI::BeginTable("t", 2, UI::Table_None)) {
                UI::TableNextRow(); UI::TableNextColumn(); UI::Text("a"); cellA = UI::GetItemRect();
                UI::TableNextRow(); UI::TableNextColumn(); UI::Text("b"); cellB = UI::GetItemRect();
                UI::EndTable();
            }
            UI::PopStyleVar();
            const UI::Vec2 p0 = UI::GetCursorScreenPos();
            UI::BeginChild("plain", UI::Vec2{120.0f, 60.0f}, UI::ChildFlags_None);
            UI::Text("x"); plain = UI::GetItemRect();
            UI::EndChild();
            child0 = UI::Rect{p0.x, p0.y, 0, 0};
            const UI::Vec2 p1 = UI::GetCursorScreenPos();
            UI::BeginChild("framed", UI::Vec2{120.0f, 60.0f}, UI::ChildFlags_Borders);
            UI::Text("y"); framed = UI::GetItemRect();
            UI::EndChild();
            child1 = UI::Rect{p1.x, p1.y, 0, 0};
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
            const UI::Vec2 p = UI::GetCursorScreenPos();
            UI::BeginChild("grow", UI::Vec2{200.0f, 0.0f}, UI::ChildFlags_AutoResizeY);
            UI::Text("one"); UI::Text("two"); UI::Text("three");
            const UI::Rect last = UI::GetItemRect();
            maxScroll = UI::GetScrollMaxY();
            UI::EndChild();
            autoBox = UI::Rect{p.x, p.y, 200.0f, last.y + last.h - p.y};
            UI::Text("after"); below = UI::GetItemRect();
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
                UI::BareNextWindow(); UI::Begin("al", 0, 0, 300, 80);
                UI::PushStyleVar(UI::StyleVar_ButtonTextAlign, UI::Vec2{al[k], 0.5f});
                UI::Button("Go##al", UI::Vec2{200.0f, 0.0f});
                UI::PopStyleVar();
                UI::End();
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
            UI::Button("Tall", UI::Vec2{60.0f, 40.0f}); tall = UI::GetItemRect();
            UI::SameLine(); UI::SetCursorPosX(200.0f); UI::CenterNextOnLine();
            UI::Button("s", UI::Vec2{30.0f, 16.0f}); small = UI::GetItemRect();
            UI::Text("next"); next = UI::GetItemRect();
            UI::PushStyleVar(UI::StyleVar_WindowPadding, UI::Vec2{16.0f, 12.0f});
            UI::BeginChild("box", UI::Vec2{200.0f, 60.0f}, UI::ChildFlags_Borders); child = UI::Rect{UI::GetItemRectMin().x, 0, 0, 0};
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
        char small[4]; P::CopyToBuf(small, sizeof(small), "\xC3\xA9\xC3\xA9");   // two two-byte characters into 3 bytes: one whole one
        CHECK(std::strcmp(small, "\xC3\xA9") == 0);
    }

    // ================= Physics =================
    {   // the section, golden; drags, the step-rate clamp, the quality list, the solver slider and the continuous box
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
            if (in.quality >= 0) seen.quality = in.quality;
            if (in.solverIterations >= 0) seen.solverIterations = in.solverIterations;
            if (in.autoContinuous >= 0) seen.autoContinuous = in.autoContinuous;
            if (in.apply) seen.apply = true;
        });
        r.Frames(3);
        CHECK(res.iterations.w > 0 && res.continuous.w > 0 && res.contentHeight > 300.0f);
        CHECK(std::abs(res.gravity2D.w - (P::DefaultItemWidth(528.0f) + 8.0f + labelW)) < 1.0f);   // Dear ImGui's 65%, then the label
        Check("editorpanels_physics", r.Draw());
        // the first gravity cell: drag 20 pixels right, 0.05 a pixel
        const float gx = res.gravity2D.x + 30.0f, gy = CY(res.gravity2D);
        r.sim.Move(gx, gy); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Move(gx + 20.0f, gy); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(seen.gravityChanged && std::abs(d.g2[0] - 1.0f) < 1e-4f);
        r.Click(res.smooth);  CHECK(seen.smoothChanged && !d.smooth);
        r.Click(res.continuous); CHECK(seen.autoContinuous == 0);
        r.sim.Move(res.iterations.x + 4.0f, CY(res.iterations)); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(seen.solverIterations == 4);
        r.Click(res.apply); CHECK(seen.apply);
        // the quality list: open it, choose Classic; the solver rows go
        r.Click(UI::Rect{res.quality.x, res.quality.y, 100.0f, res.quality.h});
        r.Click(UI::Rect{res.quality.x, res.quality.y + res.quality.h, 100.0f, res.quality.h});
        CHECK(seen.quality == 0);
        d.model.quality = 0; r.Frames(2);
        CHECK(res.iterations.w == 0 && res.continuous.w == 0);
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
        CHECK(PixelAt(sim.Draw(30, 33, 41), (int)(box.x + 3), (int)(box.y + 3)) == Rgba(41, 46, 59));   // unticked: the field colour, no tick
        sim.Click(box.x + 8.0f, CY(box), ui);
        CHECK(on && ch);
        sim.Move(250, 100); sim.Frame(ui);
        const Bitmap& b = sim.Draw(30, 33, 41);
        CHECK(PixelAt(b, (int)(box.x + 3), (int)(box.y + 3)) != Rgba(41, 46, 59));    // ticked: the tinted frame
        int tick = 0;
        for (int y = (int)box.y; y < (int)(box.y + sz); ++y)
            for (int x = (int)box.x; x < (int)(box.x + sz); ++x) if (PixelAt(b, x, y) == Rgba(115, 159, 218)) ++tick;
        CHECK(tick > (int)(sz * 2.0f));                                                 // a tick in the check-mark colour
        UI::UseLauncherLook(false);
    }
    {   // a framed combo: a field, a square arrow button in Col_Button with a triangle in the text colour; the list still opens and picks
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
        CHECK(PixelAt(b, (int)(r.x + boxW - r.h + 3.0f), (int)(r.y + r.h - 4.0f)) == Rgba(51, 56, 71));   // the arrow button
        CHECK(PixelAt(b, (int)(r.x + 4.0f), (int)(r.y + r.h - 4.0f)) == Rgba(41, 46, 59));               // the field
        const unsigned arrow = PixelAt(b, (int)(r.x + boxW - r.h * 0.5f), (int)(r.y + r.h * 0.5f - 3.0f));
        CHECK(arrow != Rgba(51, 56, 71) && ((arrow >> 24) & 255) > 150);                                 // the light triangle
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
            UI::PushStyleVar(UI::StyleVar_ScrollbarSize, sb);
            UI::PushStyleVar(UI::StyleVar_WindowPadding, UI::Vec2{16.0f, 12.0f});
            UI::BeginChild("##list", UI::Vec2{300.0f, 200.0f}, UI::ChildFlags_Borders);
            availIn = UI::GetContentRegionAvail().x;
            UI::Button("a row wider than the list", UI::Vec2{400.0f, 0.0f});   // cut where the bar starts
            for (int i = 1; i < rows; ++i) { char t[16]; std::snprintf(t, sizeof t, "row %d", i); UI::Text(t); }
            UI::EndChild();
            child = UI::GetItemRect();
            UI::PopStyleVar(2);
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
            UI::BeginChild("##fields", UI::Vec2{300.0f, 200.0f}, UI::ChildFlags_Borders);
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
    {   // a texture with the sampler rows, a sound, a newer file, no edit
        panelfix::ImportData t(P::ImportTexture), a(P::ImportAudio), n(P::ImportModel);
        n.model.newer = true;
        t.model.showSampler = true;
        S::ImportSettingsResult rt, ra, rn, r0;
        P::ImportSettingsModel none;
        P::ImportSettingsState st0;
        auto r = MakeRig(560, 600, [&](float x, float y, float w, float h) {
            rt = S::DrawImportSettings(t.model, t.state, x, y, w, h);
        });
        r.Frames(2);
        CHECK(rt.sizeCombo.w > 0 && rt.filterCombo.w > 0 && rt.wrapCombo.w > 0 && rt.mipmaps.w > 0);
        r.Click(UI::Rect{rt.sizeCombo.x, rt.sizeCombo.y, 60.0f, rt.sizeCombo.h});
        r.Click(UI::Rect{rt.sizeCombo.x, rt.sizeCombo.y + rt.sizeCombo.h * 6.0f, 60.0f, rt.sizeCombo.h});   // the sixth entry: 512
        CHECK(t.edit.maxSize == 512);
        r.Click(rt.mipmaps); CHECK(!t.edit.mipmaps);
        Check("editorpanels_import_texture", r.Draw());
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
    {   // the sections, golden; typing in the fields, the steppers, Ambient, Skybox, Apply
        panelfix::PlayerData d;
        panelfix::RenderingData rd;
        S::PlayerSettingsResult res;
        S::RenderingSettingsResult rres;
        bool changed = false, apply = false;
        auto r = MakeRig(560, 420, [&](float x, float y, float w, float h) {
            res = S::DrawPlayerSettings(d.model, x, y, w, 180.0f);
            rres = S::DrawRenderingSettings(rd.model, x, y + 200.0f, w, h - 200.0f);
            if (res.intent.changed || rres.intent.changed) changed = true;
            if (rres.intent.apply) apply = true;
        });
        r.Frames(3);
        CHECK(res.company.w > 0 && res.widthPlus.w > 0 && res.heightPlus.x > res.widthPlus.x && rres.apply.w > 0);
        Check("editorpanels_player_rendering", r.Draw());
        r.Click(res.version); r.sim.Press(Key_End); r.Frame(); r.sim.Type("-beta"); r.Frame();
        CHECK(changed && std::strcmp(d.version, "1.4.2-beta") == 0);
        r.Click(res.widthPlus);   CHECK(d.width == 1281);
        r.Click(res.heightMinus); CHECK(d.height == 719);
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

    TEST_MAIN_RESULT();
}
