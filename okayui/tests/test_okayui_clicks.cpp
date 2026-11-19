// Mouse clicks fed the way a Dear ImGui host feeds them (IO::AddMousePosEvent / AddMouseButtonEvent, then NewFrame): a press and a release that arrive in the
// SAME frame must still be one click on the button under the pointer, and a press and release in separate frames one click as well. Window mode and the
// launcher on OkayUI (docs/own_ui_toolkit.md 6.26) get their input this way (shared/ImGuiPlatformFeed.hpp).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICompat.hpp"

namespace UI = OkayUI;

struct Rig {
    int widget = 0;   // 0 Button, 1 InvisibleButton, 2 Selectable, 3 SmallButton
    UI::Rect r; int clicks = 0; int frames = 0;
    int mClicked = 0, mReleased = 0, mDownFrames = 0, itemClicked = 0, activated = 0, deactivated = 0, dbl = 0;
    void Frame() {
        UI::IO& io = UI::GetIO(); io.DisplaySize = UI::Vec2{400, 300}; io.DeltaTime = 1.0f / 60.0f;
        UI::NewFrame();
        UI::SetNextWindowPos(UI::Vec2{0, 0}); UI::SetNextWindowSize(UI::Vec2{400, 300});
        if (UI::Begin("w", nullptr, UI::Window_NoTitleBar | UI::Window_NoResize | UI::Window_NoMove)) {
            bool hit = false;
            if (widget == 0) hit = UI::Button("Go", UI::Vec2{120, 32});
            else if (widget == 1) hit = UI::InvisibleButton("##go", 120, 32);
            else if (widget == 2) hit = UI::Selectable("Go", false, 0, UI::Vec2{120, 32});
            else hit = UI::SmallButton("Go");
            if (hit) ++clicks;
            r = UI::GetItemRect();
            if (UI::IsItemClicked(0)) ++itemClicked;
            if (UI::IsItemActivated()) ++activated;
            if (UI::IsItemDeactivated()) ++deactivated;
        }
        UI::End();
        if (UI::IsMouseClicked(0)) ++mClicked;
        if (UI::IsMouseReleased(0)) ++mReleased;
        if (UI::IsMouseDown(0)) ++mDownFrames;
        if (UI::IsMouseDoubleClicked(0)) ++dbl;
        UI::Render();
        ++frames;
    }
};

int main() {
    RUN_SUITE("okayui clicks fed as IO events");
    {   // press and release in the same frame
        okayuitest::Reset(); Rig g;
        for (int i = 0; i < 3; ++i) g.Frame();
        UI::GetIO().AddMousePosEvent(g.r.x + g.r.w / 2, g.r.y + g.r.h / 2);
        for (int i = 0; i < 3; ++i) g.Frame();           // hover first, as a person does
        CHECK(g.clicks == 0);
        UI::GetIO().AddMouseButtonEvent(0, true); UI::GetIO().AddMouseButtonEvent(0, false);
        for (int i = 0; i < 6; ++i) g.Frame();
        CHECK(g.clicks == 1);
        for (int i = 0; i < 6; ++i) g.Frame();
        CHECK(g.clicks == 1);                              // not stuck down: no second click
    }
    {   // the pointer moves in the same frame as the one-frame click
        okayuitest::Reset(); Rig g;
        for (int i = 0; i < 3; ++i) g.Frame();
        UI::GetIO().AddMousePosEvent(g.r.x + 10, g.r.y + 10);
        UI::GetIO().AddMouseButtonEvent(0, true); UI::GetIO().AddMouseButtonEvent(0, false);
        for (int i = 0; i < 6; ++i) g.Frame();
        CHECK(g.clicks == 1);
    }
    {   // press and release in separate frames
        okayuitest::Reset(); Rig g;
        for (int i = 0; i < 3; ++i) g.Frame();
        UI::GetIO().AddMousePosEvent(g.r.x + 10, g.r.y + 10);
        for (int i = 0; i < 2; ++i) g.Frame();
        UI::GetIO().AddMouseButtonEvent(0, true);
        g.Frame(); g.Frame();
        UI::GetIO().AddMouseButtonEvent(0, false);
        for (int i = 0; i < 4; ++i) g.Frame();
        CHECK(g.clicks == 1);
    }
    {   // a press that never releases inside the button is not a click; releasing outside is not one either
        okayuitest::Reset(); Rig g;
        for (int i = 0; i < 3; ++i) g.Frame();
        UI::GetIO().AddMousePosEvent(g.r.x + 10, g.r.y + 10);
        for (int i = 0; i < 2; ++i) g.Frame();
        UI::GetIO().AddMouseButtonEvent(0, true);
        for (int i = 0; i < 3; ++i) g.Frame();
        UI::GetIO().AddMousePosEvent(300, 250);
        for (int i = 0; i < 2; ++i) g.Frame();
        UI::GetIO().AddMouseButtonEvent(0, false);
        for (int i = 0; i < 4; ++i) g.Frame();
        CHECK(g.clicks == 0);
    }
    // every widget and every query is true for a one-frame click, exactly once (a real Dear ImGui host sees the press in one frame and the release in the next)
    for (int w = 0; w < 4; ++w) {
        okayuitest::Reset(); Rig g; g.widget = w;
        for (int i = 0; i < 3; ++i) g.Frame();
        UI::GetIO().AddMousePosEvent(g.r.x + g.r.w / 2, g.r.y + g.r.h / 2);
        for (int i = 0; i < 3; ++i) g.Frame();
        UI::GetIO().AddMouseButtonEvent(0, true); UI::GetIO().AddMouseButtonEvent(0, false);
        for (int i = 0; i < 8; ++i) g.Frame();
        CHECK(g.clicks == 1);
        CHECK(g.mClicked == 1 && g.mReleased == 1);
        CHECK(g.mDownFrames >= 1 && g.mDownFrames <= 2);
        CHECK(g.itemClicked == 1);
        CHECK(g.activated == 1 && g.deactivated == 1);
        CHECK(g.dbl == 0);
    }
    {   // two one-frame clicks in a row are a double click on the second
        okayuitest::Reset(); Rig g;
        for (int i = 0; i < 3; ++i) g.Frame();
        UI::GetIO().AddMousePosEvent(g.r.x + 10, g.r.y + 10);
        for (int i = 0; i < 3; ++i) g.Frame();
        for (int k = 0; k < 2; ++k) {
            UI::GetIO().AddMouseButtonEvent(0, true); UI::GetIO().AddMouseButtonEvent(0, false);
            for (int i = 0; i < 3; ++i) g.Frame();
        }
        CHECK(g.clicks == 2 && g.dbl >= 1);
    }
    TEST_MAIN_RESULT();
}
