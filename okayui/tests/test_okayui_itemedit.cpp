// Edit tracking on value widgets (IsItemEdited, IsItemDeactivatedAfterEdit), CalcItemWidth and BeginPopupContextWindow.
// Headless, through the same frame driver as the other OkayUI tests. Written from the behaviour the names describe: a
// value widget is "edited" on the frame it reports a change, and "deactivated after edit" on the frame it is let go
// after it changed at some point while held.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"

#include <cstring>
#include <cstdio>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {
struct Probe { bool edited = false, deact = false, ret = false; UI::Rect r; };
}

int main() {
    RUN_SUITE("okayui item edit");
    {   // checkbox: edited and deactivated-after-edit on the click frame only
        Reset(); Sim sim(300, 200);
        bool v = false; Probe p; int editedFrames = 0, daeFrames = 0;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 280, 180)) {
                p.ret = UI::Checkbox("cb", &v); p.edited = UI::IsItemEdited(); p.deact = UI::IsItemDeactivatedAfterEdit(); p.r = UI::GetItemRect();
                if (p.edited) ++editedFrames;
                if (p.deact) ++daeFrames;
            }
            UI::End();
        };
        sim.Frames(2, ui);
        CHECK(!p.edited && !p.deact);
        sim.Move(CenterX(p.r) - 20, CenterY(p.r)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        CHECK(!p.edited && !v);                       // held, not yet changed
        sim.Up(); sim.Frame(ui);
        CHECK(v && p.ret && p.edited && p.deact);
        CHECK(editedFrames == 1 && daeFrames == 1);
        sim.Frames(2, ui);
        CHECK(!p.edited && !p.deact && editedFrames == 1);
    }
    {   // drag field: edited on every changing frame, deactivated-after-edit only when let go
        Reset(); Sim sim(400, 200);
        float v = 5.0f; Probe p; int dae = 0, ed = 0; bool daeWhileHeld = false;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 380, 180)) {
                UI::SetNextItemWidth(240); p.ret = UI::DragFloat("", &v, 0.5f, 0.0f, 100.0f);
                p.edited = UI::IsItemEdited(); p.deact = UI::IsItemDeactivatedAfterEdit(); p.r = UI::GetItemRect();
                if (p.edited) ++ed;
                if (p.deact) { ++dae; if (sim.down) daeWhileHeld = true; }
            }
            UI::End();
        };
        sim.Frames(2, ui);
        sim.Move(CenterX(p.r), CenterY(p.r)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        for (int i = 1; i <= 5; ++i) { sim.Move(CenterX(p.r) + 6.0f * i, CenterY(p.r)); sim.Frame(ui); }
        CHECK(v > 5.0f && ed >= 3 && dae == 0 && !daeWhileHeld);
        sim.Up(); sim.Frame(ui);
        CHECK(dae == 1 && p.deact && !p.edited);       // let go: commit moment (no change on this very frame)
        sim.Frame(ui);
        CHECK(dae == 1 && !p.deact);
    }
    {   // a click on a slider that changes nothing is not an edit
        Reset(); Sim sim(400, 200);
        int v = 0; Probe p; int dae = 0, ed = 0;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 380, 180)) {
                UI::SetNextItemWidth(200); p.ret = UI::SliderInt("", &v, 0, 10);
                p.edited = UI::IsItemEdited(); p.deact = UI::IsItemDeactivatedAfterEdit(); p.r = UI::GetItemRect();
                if (p.edited) ++ed;
                if (p.deact) ++dae;
            }
            UI::End();
        };
        sim.Frames(2, ui);
        // press at the far left end: the value is already the minimum, so nothing changes
        sim.Move(p.r.x + 1.0f, CenterY(p.r)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        CHECK(v == 0 && ed == 0 && dae == 0);
        // now a real change
        sim.Move(p.r.x + p.r.w * 0.6f, CenterY(p.r)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        CHECK(v != 0 && ed >= 1);
        sim.Up(); sim.Frame(ui);
        CHECK(dae == 1);
    }
    {   // text field: typing is an edit; clicking away is the deactivation after it. Focus without typing is not
        Reset(); Sim sim(400, 200);
        char buf[32] = "ab"; Probe p; int dae = 0, ed = 0; UI::Rect other;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 380, 180)) {
                UI::SetNextItemWidth(200); p.ret = UI::InputText("", buf, sizeof buf);
                p.edited = UI::IsItemEdited(); p.deact = UI::IsItemDeactivatedAfterEdit(); p.r = UI::GetItemRect();
                if (p.edited) ++ed;
                if (p.deact) ++dae;
                UI::Button("other"); other = UI::GetItemRect();
            }
            UI::End();
        };
        sim.Frames(2, ui);
        sim.Click(CenterX(p.r), CenterY(p.r), ui);     // focus it
        CHECK(ed == 0 && dae == 0);
        sim.Click(CenterX(other), CenterY(other), ui);   // click away without typing
        CHECK(ed == 0 && dae == 0);
        sim.Click(CenterX(p.r), CenterY(p.r), ui);
        sim.Type("cd"); sim.Frame(ui);
        CHECK(ed >= 1 && std::strstr(buf, "cd") != nullptr);
        CHECK(dae == 0);                                 // still typing
        sim.Click(CenterX(other), CenterY(other), ui);
        CHECK(dae == 1);
    }
    {   // the queries answer false for items that have no value, and after a frame with no widgets
        Reset(); Sim sim(300, 200);
        sim.Frame([&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 280, 180)) { UI::Text("plain"); CHECK(!UI::IsItemEdited() && !UI::IsItemDeactivatedAfterEdit()); }
            UI::End();
        });
        CHECK(!UI::IsItemEdited() && !UI::IsItemDeactivatedAfterEdit());
    }
    {   // CalcItemWidth
        Reset(); Sim sim(400, 300);
        float full = 0, set = 0, pushed = 0, after = 0, big = 0, noFrame = UI::CalcItemWidth();
        CHECK(noFrame == 0.0f);
        sim.Frame([&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 380, 280)) {
                full = UI::CalcItemWidth();
                UI::SetNextItemWidth(100); set = UI::CalcItemWidth();
                UI::SetNextItemWidth(100); UI::Button("b");            // the one-shot is used up by the item
                after = UI::CalcItemWidth();
                UI::PushItemWidth(60); pushed = UI::CalcItemWidth(); UI::PopItemWidth();
                UI::SetNextItemWidth(100000); big = UI::CalcItemWidth();   // never more than the room left
            }
            UI::End();
        });
        CHECK(full > 200 && full <= 380);
        CHECK(set == 100.0f && pushed == 60.0f);
        CHECK(after == full && big == full);
        // after an item on the same line less room is left
        float shared = 0;
        sim.Frame([&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 380, 280)) { UI::Button("a button"); UI::SameLine(); shared = UI::CalcItemWidth(); }
            UI::End();
        });
        CHECK(shared > 0 && shared < full);
    }
    {   // BeginPopupContextWindow: right click on the background opens it; on an item or outside the window does not
        Reset(); Sim sim(400, 300);
        UI::Rect btn; bool open = false;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 300, 250)) {
                UI::Button("item"); btn = UI::GetItemRect();
                open = UI::BeginPopupContextWindow("ctx");
                if (open) { UI::Text("menu"); UI::EndPopup(); }
            }
            UI::End();
        };
        auto rclick = [&](float x, float y) { sim.Move(x, y); sim.Frame(ui); sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frame(ui); };
        sim.Frames(2, ui);
        rclick(CenterX(btn), CenterY(btn));
        CHECK(!open && !UI::IsPopupOpen("ctx"));        // over an item
        rclick(380, 280);
        CHECK(!open && !UI::IsPopupOpen("ctx"));        // outside the window
        rclick(150, 200);
        CHECK(open && UI::IsPopupOpen("ctx"));          // on the empty background
    }
    TEST_MAIN_RESULT();
}
