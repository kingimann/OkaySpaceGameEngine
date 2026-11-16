// The editor's Problems, Stats and History panels drawn with OkayUI (okayui/screens/OkayUIEditorPanels). Driven headless the way
// the editor drives them: every control is found by its rectangle in the result and used with synthetic input (clicks, typing,
// the wheel, right-click menus); golden images (launcher look) pin what is drawn. The Dear ImGui versions are compared against
// these in tests/test_okayui_editor_panels_imgui.cpp.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIEditorPanels.hpp"
#include "EditorPanelFixtures.hpp"

#include <cmath>
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
    TEST_MAIN_RESULT();
}
