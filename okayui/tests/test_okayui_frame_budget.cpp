// Per-frame work of the OkayUI editor panels stays flat (the 2026-11-23 report: "with experimental on, the editor freezes a lot").
// An Inspector-like window of framed widgets (twelve components) and the Console with a full
// log are drawn for hundreds of frames, with the pointer moving, clicks, typing and the wheel in between, and the test counts what one
// steady frame costs: heap allocations, vertices, indices and draw spans. Nothing may grow from the early frames to the late ones (a list
// rebuilt and never cleared, a cache that never hits), and the counts must stay under fixed budgets. No clock is read: counts, not
// milliseconds, so the test cannot be flaky on a slow machine. The host's mouse areas (shared/OkayUIHost.hpp Region) are checked for an
// Inspector with many sections: every section must keep the mouse (with 8 slots the 9th section and later ignored clicks).
// Written from what the editor's host does each frame (editor/OkayPanels.cpp HostSection, editor/ConsolePanel.cpp).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIScreens.hpp"
#include "okay/UI/OkayUICompat.hpp"   // the Dear ImGui-named widgets the editor's bodies use
#include "OkayUIHost.hpp"
#include "okay/Debug/ConsoleModel.hpp"

#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "AllocHook.hpp"   // counts every operator new (allochook::count)

using namespace okayuitest;
namespace UI = OkayUI;
namespace S = okayuiscreens;

namespace {

struct Cost { long long allocs = 0; int vertices = 0, indices = 0, spans = 0, dropped = 0; };

Cost Measure(Sim& sim, const std::function<void()>& ui) {
    const long long a0 = allochook::count;
    sim.Frame(ui);
    Cost c;
    c.allocs = allochook::count - a0;
    const UI::DrawData d = UI::GetDrawData();
    c.vertices = d.vertexCount; c.indices = d.indexCount; c.spans = d.cmdCount; c.dropped = d.dropped;
    return c;
}

} // namespace

int main() {
    RUN_SUITE("okayui frame budget");
    Reset();
    UI::UseLauncherLook(true);
    UI::Style().caretBlink = false;

    // ---- an Inspector-like window: steady frames cost the same after 600 frames of use as after 20 ---------------------------------------
    // (Round 6: the library's own Inspector views this measured were deleted; the editor's Inspector is its bodies written in Dear ImGui's
    // names, drawn with the framed widgets: twelve components, each a collapsing header over drag cells, a slider, a check box, a combo
    // and a text field.)
    {
        struct Comp { float pos[3] = {0, 1, 2}; float amount = 0.5f; bool on = true; int kind = 0; char name[32] = "Thing"; };
        std::vector<Comp> comps(12);
        static const char* kinds[] = {"Box", "Sphere", "Capsule", "Mesh"};
        Sim sim(460, 1400);
        auto ui = [&] {
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
            UI::SetNextWindowPos(UI::Vec2{16, 12}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{428, 1376}, UI::Cond_Always);
            if (UI::Begin("Inspector", nullptr, 0)) {
                for (std::size_t i = 0; i < comps.size(); ++i) {
                    Comp& c = comps[i];
                    UI::PushID((int)i);
                    if (UI::CollapsingHeader("Component", i % 2 == 0)) {
                        UI::DragFloat3("Position", c.pos, 0.1f, 0.0f, 0.0f, "%.2f");
                        UI::SliderFloat("Amount", &c.amount, 0.0f, 1.0f, "%.2f");
                        UI::Checkbox("Enabled", &c.on);
                        UI::Combo("Kind", &c.kind, kinds, 4);
                        UI::InputText("Name", c.name, (int)sizeof c.name);
                    }
                    UI::PopID();
                }
            }
            UI::End();
            UI::PopStyleVar();
        };
        for (int i = 0; i < 20; ++i) sim.Frame(ui);
        sim.Move(5, 5);   // a spot with nothing under it
        sim.Frames(2, ui);
        const Cost early = Measure(sim, ui);
        // use it: hover every row, click and type in a field, press Escape, scroll
        for (int round = 0; round < 6; ++round) {
            for (float y = 20; y < 1300; y += 37) { sim.Move(200, y); sim.Frame(ui); }
            sim.Move(200, 60); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
            sim.Type("x"); sim.Frame(ui); sim.Press(Key_Backspace); sim.Frame(ui); sim.Press(Key_Escape); sim.Frame(ui);
            sim.wheel = -3; sim.Frame(ui); sim.wheel = 3; sim.Frame(ui);
            for (int i = 0; i < 60; ++i) sim.Frame(ui);
        }
        sim.Move(5, 5);
        sim.Frames(2, ui);
        const Cost late = Measure(sim, ui);
        std::printf("  inspector steady frame: early %lld allocs %d vertices %d spans, late %lld allocs %d vertices %d spans (after %d frames)\n",
                    early.allocs, early.vertices, early.spans, late.allocs, late.vertices, late.spans, sim.frames);
        CHECK(sim.frames > 600);
        CHECK(late.allocs <= early.allocs);         // nothing piles up per frame
        CHECK(late.vertices == early.vertices && late.indices == early.indices && late.spans == early.spans);
        CHECK(early.dropped == 0 && late.dropped == 0);
        CHECK(late.spans < 200);                    // well inside the 256 draw spans a frame holds
        CHECK(late.allocs < 4000);                  // a budget for one Inspector frame of 12 components
    }

    // ---- the Console with a full log: a steady frame costs the same with 2,000 lines after 300 frames, and adding lines does not grow it --
    {
        okay::ConsoleModel model(2000);
        model.SetClock([] { return 3723.0; });
        for (int i = 0; i < 2000; ++i) model.Add(i % 7 == 0 ? okay::Log::Level::Warning : okay::Log::Level::Info, "line " + std::to_string(i), i % 3 ? "Script" : "");
        S::ConsoleViewState st;
        S::ConsoleHost host;
        Sim sim(900, 520);
        auto ui = [&] { S::DrawConsole(model, st, host, 16, 12, 900 - 32, 520 - 24); };
        sim.Frames(10, ui);
        sim.Move(5, 5);
        sim.Frames(2, ui);
        const Cost early = Measure(sim, ui);
        for (int i = 0; i < 300; ++i) {
            if (i % 3 == 0) model.Add(okay::Log::Level::Info, "more " + std::to_string(i));   // the oldest line drops out: still 2,000
            sim.Move(400, 100 + (float)(i % 300)); sim.wheel = (i % 20 < 10) ? -1.0f : 1.0f;
            sim.Frame(ui);
        }
        sim.Move(5, 5);
        sim.Frames(2, ui);
        const Cost late = Measure(sim, ui);
        std::printf("  console steady frame (2000 lines): early %lld allocs %d vertices, late %lld allocs %d vertices\n",
                    early.allocs, early.vertices, late.allocs, late.vertices);
        CHECK(model.Size() == 2000);
        CHECK(late.allocs <= early.allocs + early.allocs / 10);   // the same work (row texts differ by a digit or two)
        CHECK(late.vertices <= early.vertices + early.vertices / 10);
        CHECK(late.dropped == 0);
    }

    // ---- the host's mouse areas: an Inspector of 12 components offers 2 + 2 * 12 + 2 sections, and every one keeps the mouse -------------
    {
        okayuihost::Region region;
        const int sections = 2 + 2 * 12 + 2;   // card, Transform, a bar and a body per component, Add Component, the Console panel
        for (int frame = 0; frame < 3; ++frame) {
            region.NextFrame();
            for (int i = 0; i < sections; ++i) region.Offer(0, (float)i * 30, 400, 28, true);
        }
        region.NextFrame();
        CHECK(region.Overflowed() == 0);
        int owned = 0;
        for (int i = 0; i < sections; ++i) owned += region.Owns(100, (float)i * 30 + 10) ? 1 : 0;
        CHECK(owned == sections);
    }

    UI::UseLauncherLook(false);
    TEST_MAIN_RESULT();
}
