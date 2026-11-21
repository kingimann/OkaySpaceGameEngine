// The Dear ImGui-shaped pieces the Inspector work added to OkayUI (docs/ui/own_ui_toolkit.md sections 4.36 and 23): the middle button,
// the flags form of BeginChild (padding, outline, auto height), reorderable and closable tabs, hideable table columns. (The names,
// hover flags, edit marks, tooltips, popups and draw flags are main's, tested in okayui_calls, okayui_names, okayui_itemedit, okayui_scopes.)
// Headless, through the same frame driver as the other OkayUI tests. Written from how these calls behave, not from any other project.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICompat.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {
struct Rects { UI::Rect r[8]; };
void BeginWin(const char* name = "w", float x = 10, float y = 10, float w = 300, float h = 260) {
    UI::BareNextWindow();
    UI::Begin(name, x, y, w, h);
}
}

int main() {
    RUN_SUITE("okayui imcompat");
    {   // the middle button: down, clicked and released (Input::middleDown)
        Reset();
        Sim sim(400, 300);
        auto ui = [&] { BeginWin(); UI::End(); };
        bool d1 = false, c2 = false, r2 = false;
        sim.Move(100, 100); sim.Frame(ui);
        sim.mdown = true; sim.Frame([&] { ui(); d1 = UI::IsMouseDown(UI::MouseButton_Middle); c2 = UI::IsMouseClicked(UI::MouseButton_Middle); });
        CHECK(d1 && c2);
        CHECK(!UI::IsMouseDown(UI::MouseButton_Left));
        sim.mdown = false;
        sim.Frame([&] { ui(); r2 = UI::IsMouseReleased(UI::MouseButton_Middle); });
        CHECK(r2);
    }
    {   // child flags: border, auto resize, negative size
        Reset();
        Sim sim(400, 400);
        UI::Rect rb, rn, ra;
        const unsigned bg = Rgba(255, 0, 0);
        auto ui = [&] {
            BeginWin("w", 0, 0, 400, 400);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);   // the Dear ImGui form of BeginChild(id, size, flags)
            UI::PushStyleColor(UI::Col_ChildBg, UI::RGBA(255, 0, 0));
            if (UI::BeginChild("bordered", UI::Vec2{100, 40}, UI::Child_Borders)) { UI::Text("x"); UI::EndChild(); }
            rb = UI::GetItemRect();
            if (UI::BeginChild("plain", UI::Vec2{100, 40}, UI::Child_None)) { UI::Text("x"); UI::EndChild(); }
            rn = UI::GetItemRect();
            if (UI::BeginChild("auto", UI::Vec2{100, 0}, UI::Child_Borders | UI::Child_AutoResizeY)) { UI::Text("a"); UI::Text("b"); UI::Text("c"); UI::EndChild(); }
            ra = UI::GetItemRect();
            UI::PopStyleColor();
            UI::PopStyleVar();
            UI::End();
        };
        sim.Frames(4, ui);
        CHECK(rb.w == 100 && rb.h == 40);
        CHECK(rn.w == 100 && rn.h == 40);
        const Bitmap& b = sim.Draw();
        // inside the bordered child there is a panel; the plain child has none (its pixels are the window's, not the panel colour)
        const unsigned inBordered = PixelAt(b, (int)(rb.x + 60), (int)(rb.y + rb.h - 6));
        const unsigned inPlain = PixelAt(b, (int)(rn.x + 60), (int)(rn.y + rn.h - 6));
        CHECK(inBordered == bg);
        CHECK(inPlain != bg);                                       // the plain child draws no panel
        {   // the auto child is as tall as its three rows plus its padding, and does not scroll
            const float row = UI::GetFrameHeight();
            CHECK(ra.h > 3 * 8 && ra.h < 3 * (row + 12) + 20);
            const float h1 = ra.h;
            sim.Frames(3, ui);
            CHECK(ra.h == h1);                              // settled
        }
        UI::Rect rneg;
        sim.Frame([&] {
            BeginWin("w", 0, 0, 400, 400);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);   // the Dear ImGui form of BeginChild(id, size, flags)
            if (UI::BeginChild("neg", UI::Vec2{-50, 60}, UI::Child_Borders)) UI::EndChild();
            rneg = UI::GetItemRect();
            UI::PopStyleVar();
            UI::End();
        });
        CHECK(rneg.w > 200 && rneg.w < 400 && rneg.h == 60);
        // hostile numbers
        const float nan = std::numeric_limits<float>::quiet_NaN();
        sim.Frame([&] { BeginWin(); if (UI::BeginChild("n", UI::Vec2{nan, nan}, 0xffff, 0xffff)) UI::EndChild(); UI::End(); });
    }
    {   // tabs: the pure drag target
        const float w[4] = {50, 60, 70, 80};
        CHECK(UI::TabDragTarget(w, 4, 0, 0, 2, 10) == 0);
        CHECK(UI::TabDragTarget(w, 4, 0, 0, 2, 60) == 1);       // over the second tab
        CHECK(UI::TabDragTarget(w, 4, 0, 0, 2, 5000) == 3);     // past the end: last
        CHECK(UI::TabDragTarget(w, 4, 3, 0, 2, -50) == 0);      // before the start: first
        CHECK(UI::TabDragTarget(w, 4, 9, 0, 2, 10) == 9);       // bad index: unchanged
        CHECK(UI::TabDragTarget(nullptr, 4, 1, 0, 2, 10) == 1);
        CHECK(UI::TabDragTarget(w, 0, 0, 0, 2, 10) == 0);
        CHECK(UI::TabDragTarget(w, 4, 1, 0, 2, std::numeric_limits<float>::quiet_NaN()) == 1);
    }
    {   // a reorderable tab bar: drag A to the right of B; the order is kept; the selection follows the click
        Reset();
        Sim sim(500, 300);
        UI::Rect ra, rb, rc; bool aOn = false, bOn = false, cOn = false;
        auto ui = [&] {
            BeginWin("w", 0, 0, 500, 300);
            if (UI::BeginTabBar("bar", UI::TabBar_Reorderable)) {
                aOn = UI::BeginTabItem("Alpha"); ra = UI::GetItemRect(); if (aOn) UI::EndTabItem();
                bOn = UI::BeginTabItem("Beta");  rb = UI::GetItemRect(); if (bOn) UI::EndTabItem();
                cOn = UI::BeginTabItem("Gamma"); rc = UI::GetItemRect(); if (cOn) UI::EndTabItem();
                UI::EndTabBar();
            }
            UI::End();
        };
        sim.Frames(3, ui);
        CHECK(ra.x < rb.x && rb.x < rc.x);
        CHECK(aOn && !bOn && !cOn);
        sim.Move(CenterX(ra), CenterY(ra)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        sim.Move(CenterX(rb) + 4, CenterY(rb)); sim.Frames(2, ui);   // over Beta: they swap
        CHECK(rb.x < ra.x && ra.x < rc.x);
        sim.Move(rc.x + rc.w - 2, CenterY(rc)); sim.Frames(2, ui);   // over Gamma's right end: Alpha goes last
        CHECK(rb.x < rc.x && rc.x < ra.x);
        sim.Up(); sim.Frames(3, ui);
        CHECK(rb.x < rc.x && rc.x < ra.x);                           // stays after the release
        CHECK(aOn);                                                  // A was clicked: it is the chosen one
        // without the flag nothing moves
        Reset();
        Sim s2(500, 300);
        UI::Rect pa, pb;
        auto ui2 = [&] {
            BeginWin("w", 0, 0, 500, 300);
            if (UI::BeginTabBar("plain")) {
                if (UI::BeginTabItem("Alpha")) UI::EndTabItem(); pa = UI::GetItemRect();
                if (UI::BeginTabItem("Beta")) UI::EndTabItem(); pb = UI::GetItemRect();
                UI::EndTabBar();
            }
            UI::End();
        };
        s2.Frames(3, ui2);
        s2.Move(CenterX(pa), CenterY(pa)); s2.Frame(ui2); s2.Down(); s2.Frame(ui2); s2.Move(CenterX(pb), CenterY(pb)); s2.Frames(3, ui2);
        CHECK(pa.x < pb.x);
    }
    {   // closable tabs: middle click closes; a vanished tab leaves no gap; new tabs can take the selection
        Reset();
        Sim sim(500, 300);
        bool aOpen = true, bOpen = true, withC = false;
        UI::Rect ra, rb, rc; bool cOn = false;
        auto ui = [&] {
            BeginWin("w", 0, 0, 500, 300);
            if (UI::BeginTabBar("bar", UI::TabBar_Reorderable | UI::TabBar_AutoSelectNewTabs)) {
                if (aOpen && UI::BeginTabItem("Alpha", &aOpen)) UI::EndTabItem(); ra = UI::GetItemRect();
                if (bOpen && UI::BeginTabItem("Beta", &bOpen)) UI::EndTabItem(); rb = UI::GetItemRect();
                if (withC) { cOn = UI::BeginTabItem("Gamma"); rc = UI::GetItemRect(); if (cOn) UI::EndTabItem(); }
                UI::EndTabBar();
            }
            UI::End();
        };
        sim.Frames(3, ui);
        const float betaX = rb.x;
        sim.Move(CenterX(ra) - 6, CenterY(ra)); sim.Frame(ui); sim.mdown = true; sim.Frame(ui); sim.mdown = false; sim.Frames(3, ui);
        CHECK(!aOpen && bOpen);                                      // the middle click closed Alpha
        CHECK(rb.x < betaX);                                         // and Beta moved left into the gap
        withC = true; sim.Frames(3, ui);
        CHECK(cOn);                                                  // a new tab after the first frame became the chosen one
    }
    {   // table columns: hide, default hide, locked columns, header menu, per-column NoResize, hovered column
        Reset();
        Sim sim(600, 300);
        float cellX[3] = {-1, -1, -1}; bool cellRet[3] = {true, true, true};
        UI::Rect hdr;
        int hovered = -9; int cnt = 0; std::string nm; int fl = -1; bool en1 = true;
        bool hideB = false, hideC = false;
        auto ui = [&] {
            BeginWin("w", 0, 0, 600, 300);
            { const UI::Vec2 tp = UI::GetCursorScreenPos(); hdr = UI::Rect{tp.x, tp.y, 100, UI::GetFrameHeight()}; }
            if (UI::BeginTable("t", 3, UI::Table_Hideable)) {
                UI::TableSetupColumn("Name", UI::Col_NoHide);
                UI::TableSetupColumn("Kind", hideB ? UI::Col_DefaultHide : 0);
                UI::TableSetupColumn("Size", hideC ? UI::Col_DefaultHide : 0);
                UI::TableHeadersRow();
                UI::TableNextRow();
                for (int c = 0; c < 3; ++c) { cellRet[c] = UI::TableNextColumn(); cellX[c] = UI::GetCursorScreenPos().x; UI::Text("x"); }
                hovered = UI::TableGetHoveredColumn(); cnt = UI::TableGetColumnCount(); nm = UI::TableGetColumnName(1); fl = UI::TableGetColumnFlags(0);
                en1 = UI::TableGetColumnIsEnabled(1);
                UI::EndTable();
            }
            UI::End();
        };
        sim.Frames(3, ui);
        CHECK(cellRet[0] && cellRet[1] && cellRet[2]);
        CHECK(cellX[0] < cellX[1] && cellX[1] < cellX[2] && cnt == 3 && nm == "Kind" && (fl & UI::Col_NoHide) && en1);
        sim.Move(cellX[1] + 4, 40); sim.Frames(2, ui);
        CHECK(hovered == 1);
        const float x1 = cellX[1], x2 = cellX[2];
        // hide Kind from code (next frame): the third column slides left into its place and the cell says it is hidden
        sim.Frame([&] { BeginWin("w", 0, 0, 600, 300); if (UI::BeginTable("t", 3, UI::Table_Hideable)) {
            UI::TableSetupColumn("Name", UI::Col_NoHide); UI::TableSetupColumn("Kind"); UI::TableSetupColumn("Size"); UI::TableHeadersRow();
            CHECK(UI::TableSetColumnEnabled(1, false));
            CHECK(!UI::TableSetColumnEnabled(0, false));              // NoHide
            UI::EndTable(); } UI::End(); });
        sim.Frames(3, ui);
        CHECK(!cellRet[1] && cellRet[0] && cellRet[2]);
        CHECK(cellX[2] < x2 && cellX[2] > x1 - 4 && cellX[1] < 0);    // Size took Kind's place; Kind's cell is off screen
        CHECK(!en1);
        // show it again: the old layout is back
        sim.Frame([&] { BeginWin("w", 0, 0, 600, 300); if (UI::BeginTable("t", 3, UI::Table_Hideable)) {
            UI::TableSetupColumn("Name", UI::Col_NoHide); UI::TableSetupColumn("Kind"); UI::TableSetupColumn("Size"); UI::TableHeadersRow();
            CHECK(UI::TableSetColumnEnabled(1, true)); UI::EndTable(); } UI::End(); });
        sim.Frames(3, ui);
        CHECK(cellRet[1] && std::fabs(cellX[1] - x1) < 1 && std::fabs(cellX[2] - x2) < 1);
        // the header menu: a right click on the header opens it, unchecking Size hides it
        sim.Move(hdr.x + 20, hdr.y + 6); sim.Frame(ui); sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frames(2, ui);
        CHECK(UI::IsPopupOpen(nullptr, UI::Popup_AnyPopupId));
        // a column that starts hidden
        Reset(); Sim s2(600, 300); hideB = true;
        s2.Frames(3, ui);
        CHECK(!cellRet[1] && cellRet[0] && cellRet[2] && !en1);
        // hostile: out-of-range columns, outside a table
        CHECK(!UI::TableSetColumnEnabled(0, false) && UI::TableGetHoveredColumn() == -1 && UI::TableGetColumnCount() == 0);
        s2.Frame([&] { BeginWin(); if (UI::BeginTable("h", 2, UI::Table_Hideable)) { UI::TableSetupColumn("a"); UI::TableSetupColumn("b");
            UI::TableHeadersRow(); CHECK(!UI::TableSetColumnEnabled(5, false)); CHECK(!UI::TableSetColumnEnabled(-2, true));
            CHECK(std::string(UI::TableGetColumnName(9)) == ""); CHECK(UI::TableGetColumnFlags(9) == 0);
            // hiding every column is refused: one stays
            CHECK(UI::TableSetColumnEnabled(0, false)); CHECK(!UI::TableSetColumnEnabled(1, false)); UI::EndTable(); } UI::End(); });
    }
    {   // a column with NoResize keeps its width when its border is dragged; a normal one changes
        Reset();
        Sim sim(600, 300);
        float xs[3] = {0, 0, 0}; bool noResize = true;
        auto ui = [&] {
            BeginWin("w", 0, 0, 600, 300);
            if (UI::BeginTable("t", 3, UI::Table_None)) {
                UI::TableSetupColumn("A", noResize ? UI::Col_NoResize : 0); UI::TableSetupColumn("B"); UI::TableSetupColumn("C");
                UI::TableHeadersRow(); UI::TableNextRow();
                for (int c = 0; c < 3; ++c) { UI::TableNextColumn(); xs[c] = UI::GetCursorScreenPos().x; UI::Text("x"); }
                UI::EndTable();
            }
            UI::End();
        };
        sim.Frames(3, ui);
        const float border1 = xs[1] - 4;
        sim.Move(border1, 40); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Move(border1 + 40, 40); sim.Frames(2, ui); sim.Up(); sim.Frames(2, ui);
        CHECK(std::fabs(xs[1] - 4 - border1) < 1);                 // NoResize on A: the border between A and B did not move
        Reset(); Sim s2(600, 300); noResize = false;
        s2.Frames(3, ui);
        const float b2 = xs[1] - 4;
        s2.Move(b2, 40); s2.Frame(ui); s2.Down(); s2.Frame(ui); s2.Move(b2 + 40, 40); s2.Frames(2, ui); s2.Up(); s2.Frames(2, ui);
        CHECK(xs[1] - 4 > b2 + 20);                                // and a normal one moves
    }
    {   // pictures (golden images): a reordered tab bar, a table with a hidden column and its header menu, a tooltip with widgets next to child
        // panels with and without borders, and the rounded-corner and n-gon rectangles
        auto Check = [](const char* name, const Bitmap& b) {
            std::string why;
            const bool ok = MatchesGolden(name, b, &why);
            if (!ok) std::cerr << "  golden " << name << ": " << why << " (actual written as " << name << ".actual.png)\n";
            CHECK(ok);
        };
        {
            Reset(); UI::UseLauncherLook(true);
            Sim sim(520, 150);
            UI::Rect ra, rb, rc;
            auto ui = [&] {
                BeginWin("tabs", 6, 6, 508, 138);
                if (UI::BeginTabBar("bar", UI::TabBar_Reorderable)) {
                    if (UI::BeginTabItem("Scene")) { ra = UI::GetItemRect(); UI::Text("the Scene tab, which was dragged to the end of the bar"); UI::EndTabItem(); } else ra = UI::GetItemRect();
                    if (UI::BeginTabItem("Game")) UI::EndTabItem(); rb = UI::GetItemRect();
                    bool unsaved = true; (void)unsaved;
                    if (UI::BeginTabItem("Script", nullptr, UI::TabItem_UnsavedDocument)) UI::EndTabItem(); rc = UI::GetItemRect();
                    UI::EndTabBar();
                }
                UI::End();
            };
            sim.Frames(3, ui);
            sim.Move(CenterX(ra), CenterY(ra)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
            sim.Move(rc.x + rc.w - 3, CenterY(rc)); sim.Frames(3, ui); sim.Up(); sim.Frames(3, ui);
            CHECK(ra.x > rb.x && ra.x > rc.x);
            Check("imcompat_tabs_reordered", sim.Draw(28, 30, 36));
            UI::UseLauncherLook(false);
        }
        {
            Reset(); UI::UseLauncherLook(true);
            Sim sim(420, 190);
            UI::Rect hdr;
            auto ui = [&] {
                BeginWin("tbl", 6, 6, 408, 178);
                { const UI::Vec2 tp = UI::GetCursorScreenPos(); hdr = UI::Rect{tp.x, tp.y, 100, UI::GetFrameHeight()}; }
                if (UI::BeginTable("files", 4, UI::Table_Hideable | UI::Table_RowBg)) {
                    UI::TableSetupColumn("Name", UI::Col_NoHide, 2.0f); UI::TableSetupColumn("Kind", UI::Col_DefaultHide, 1.0f);
                    UI::TableSetupColumn("Size", 0, 1.0f); UI::TableSetupColumn("Modified", 0, 1.5f);
                    UI::TableHeadersRow();
                    const char* names[] = {"Player.okayscene", "hero.png", "theme.wav"}; const char* kinds[] = {"Scene", "Texture", "Audio"};
                    const char* sizes[] = {"12 KB", "480 KB", "2.1 MB"}; const char* when[] = {"today", "yesterday", "3 days ago"};
                    for (int r = 0; r < 3; ++r) { UI::TableNextRow(); UI::TableNextColumn(); UI::Text(names[r]); UI::TableNextColumn(); UI::Text(kinds[r]); UI::TableNextColumn(); UI::Text(sizes[r]); UI::TableNextColumn(); UI::Text(when[r]); }
                    UI::EndTable();
                }
                UI::End();
            };
            sim.Frames(3, ui);
            sim.Move(hdr.x + 40, hdr.y + 8); sim.Frame(ui); sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frames(2, ui);
            CHECK(UI::IsPopupOpen(nullptr, UI::Popup_AnyPopupId));
            Check("imcompat_table_hide_menu", sim.Draw(28, 30, 36));
            UI::UseLauncherLook(false);
        }
        {
            Reset(); UI::UseLauncherLook(true);
            Sim sim(520, 340);
            UI::Rect rb;
            auto ui = [&] {
                BeginWin("kids", 6, 6, 508, 328);
                UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);   // the Dear ImGui form of BeginChild(id, size, flags)
                UI::PushStyleColor(UI::Col_ChildBg, UI::RGBA(44, 50, 64));
                if (UI::BeginChild("with borders", UI::Vec2{160, 60}, UI::Child_Borders)) { UI::Text("Borders"); UI::EndChild(); }
                UI::SameLine();
                if (UI::BeginChild("without", UI::Vec2{160, 60}, 0)) { UI::Text("No borders"); UI::EndChild(); }
                UI::SameLine();
                if (UI::BeginChild("auto", UI::Vec2{150, 0}, UI::Child_Borders | UI::Child_AutoResizeY)) { UI::Text("Auto height"); UI::Text("two rows"); UI::EndChild(); }
                UI::PopStyleColor();
                UI::PopStyleVar();
                const UI::DrawList& dl = UI::GetWindowDrawList();
                const UI::Vec2 p = UI::GetCursorScreenPos();
                dl.AddRectFilled({p.x, p.y + 6}, {p.x + 50, p.y + 46}, UI::RGBA(110, 160, 255), 12.0f, UI::DrawFlags_RoundCornersTop);
                dl.AddRectFilled({p.x + 60, p.y + 6}, {p.x + 110, p.y + 46}, UI::RGBA(110, 160, 255), 12.0f, UI::DrawFlags_RoundCornersBottomLeft | UI::DrawFlags_RoundCornersTopRight);
                dl.AddRect({p.x + 120, p.y + 6}, {p.x + 170, p.y + 46}, UI::RGBA(255, 200, 90), 12.0f, UI::DrawFlags_RoundCornersLeft, 2.0f);
                dl.AddNgonFilled({p.x + 215, p.y + 26}, 20, UI::RGBA(120, 220, 150), 6);
                dl.AddNgon({p.x + 265, p.y + 26}, 20, UI::RGBA(240, 120, 120), 5, 2.0f);
                UI::Dummy(300, 52);
                UI::Button("Hover me for the tooltip"); rb = UI::GetItemRect();
                if (UI::IsItemHovered(UI::Hovered_DelayShort) && UI::BeginTooltip()) { UI::Text("A tooltip with widgets"); UI::ProgressBar(0.6f, UI::Vec2{160, 0}, "60%"); UI::Button("Inside"); UI::EndTooltip(); }
                UI::End();
            };
            sim.Frames(3, ui);
            sim.Move(CenterX(rb), CenterY(rb));
            sim.Frames(30, ui);
            Check("imcompat_children_tooltip_shapes", sim.Draw(28, 30, 36));
            UI::UseLauncherLook(false);
        }
    }
    TEST_MAIN_RESULT();
}
