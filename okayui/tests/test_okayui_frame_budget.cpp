// Per-frame work of the OkayUI editor panels stays flat (the 2026-11-23 report: "with experimental on, the editor freezes a lot").
// The Inspector stack (header card, Transform, a header bar per component, the Mesh Renderer, Add Component) and the Console with a full
// log are drawn for hundreds of frames, with the pointer moving, clicks, typing and the wheel in between, and the test counts what one
// steady frame costs: heap allocations, vertices, indices and draw spans. Nothing may grow from the early frames to the late ones (a list
// rebuilt and never cleared, a cache that never hits), and the counts must stay under fixed budgets. No clock is read: counts, not
// milliseconds, so the test cannot be flaky on a slow machine. The host's mouse areas (shared/OkayUIHost.hpp Region) are checked for an
// Inspector with many sections: every section must keep the mouse (with 8 slots the 9th section and later ignored clicks).
// Written from what the editor's host does each frame (editor/OkayPanels.cpp HostSection, editor/ConsolePanel.cpp).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "InspectorStackOkayUI.hpp"
#include "OkayUIScreens.hpp"
#include "OkayUIHost.hpp"
#include "okay/Debug/ConsoleModel.hpp"
#include "okay/UI/OkayUICompat.hpp"   // CalcTextSize with Dear ImGui's arguments

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

/// An object with `n` components (the first a Mesh Renderer, open), like a First Person player with its controller, camera, scripts.
inspfix::Scene Busy(int n) {
    inspfix::Scene s = inspfix::Sample();
    inspfix::Obj& o = s.objs[0];
    while ((int)o.comps.size() < n) {
        inspfix::Comp c;
        c.label = "Script (extra_" + std::to_string(o.comps.size()) + ".okay)";
        c.open = (o.comps.size() % 2) == 0;
        o.comps.push_back(c);
    }
    return s;
}

} // namespace

int main() {
    RUN_SUITE("okayui frame budget");
    Reset();
    UI::UseLauncherLook(true);
    UI::Style().caretBlink = false;

    // ---- the Inspector stack: steady frames cost the same after 600 frames of use as after 20 --------------------------------------------
    {
        inspfix::Scene scene = Busy(12);
        Sim sim(460, 1400);
        inspfix::StackResult res;
        auto ui = [&] {
            UI::PushTextScale(S::kEditorTextScale);
            res = inspfix::DrawStack(scene, 16.0f, 12.0f, sim.W - 32.0f, sim.H - 24.0f);
            UI::PopTextScale();
        };
        auto frame = [&] { sim.Frame(ui); inspfix::ApplyStack(scene, res); };
        for (int i = 0; i < 20; ++i) frame();
        sim.Move(5, 5);   // a spot with nothing under it
        frame(); frame();
        const Cost early = Measure(sim, ui);
        inspfix::ApplyStack(scene, res);
        // use it: hover every part, click the name field and type, press Escape, scroll, click each component bar twice (fold, unfold)
        for (int round = 0; round < 6; ++round) {
            for (float y = 20; y < 1300; y += 37) { sim.Move(200, y); frame(); }
            sim.Move(CenterX(res.header.nameField), CenterY(res.header.nameField)); frame(); sim.Down(); frame(); sim.Up(); frame();
            sim.Type("x"); frame(); sim.Press(Key_Backspace); frame(); sim.Press(Key_Escape); frame();
            sim.wheel = -3; frame(); sim.wheel = 3; frame();
            for (std::size_t c = 0; c < res.comps.size(); ++c)
                for (int t = 0; t < 2; ++t) {
                    const UI::Rect r = res.comps[c].bar;
                    sim.Move(r.x + 60.0f, CenterY(r)); frame(); sim.Down(); frame(); sim.Up(); frame();
                }
            for (int i = 0; i < 40; ++i) frame();
        }
        sim.Move(5, 5);
        frame(); frame();
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

    // ---- text measurement allocates nothing for a whole label (the compatibility CalcTextSize copied every label first) ------------
    {
        const char* label = "Directional Light (main sun) - Shadows";   // longer than a short-string buffer
        const UI::Vec2 a = UI::CalcTextSize(label);
        const long long a0 = allochook::count;
        UI::Vec2 b{};
        for (int i = 0; i < 1000; ++i) b = UI::CalcTextSize(label, nullptr, true, -1.0f);
        CHECK(allochook::count - a0 == 0);
        CHECK(b.x == a.x && b.y == a.y);   // the same size as before
        const UI::Vec2 hidden = UI::CalcTextSize("Name##inspector-row", nullptr, true, -1.0f);   // "##" still hides the id
        CHECK(hidden.x == UI::CalcTextSize("Name").x);
        const char text[] = "Shadows and more";
        CHECK(UI::CalcTextSize(text, text + 7, false, -1.0f).x == UI::CalcTextSize("Shadows").x);   // an end before the zero still counts
    }

    UI::UseLauncherLook(false);
    TEST_MAIN_RESULT();
}
