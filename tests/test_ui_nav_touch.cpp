// 2-D gamepad / keyboard navigation (nearest widget in the pressed direction, wrap,
// Left/Right kept by value widgets, focus scrolls into view) and touch input (Input touch list, tap fires on
// lift, drags don't click, finger drag-scroll and fling in scroll views and lists).
#include "test_framework.hpp"
#include "okay/Components/UINavigation.hpp"
#include <Okay.hpp>
#include <vector>

using namespace okay;

namespace {

struct Finger { long long id; int state; Vec2 pos; };

void InstallHitTests() {   // what the player installs each frame (800x600 screen)
    UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 800, 600, o, sz); };
    UIWorld().clipContains = [](GameObject* go, const Vec2& p) { return UIClipHitTest(go, p, 800, 600); };
    UIWorld().screenW = 800; UIWorld().screenH = 600;
}

// One game frame the way the player runs it: drop last frame's lifted touches, take
// this frame's finger events, then the keys / mouse (the OS turns the first finger
// into the left button), layout and update.
void Frame(Scene& s, std::vector<Finger> fingers = {}, Vec2 mouse = {-100, -100},
           unsigned buttons = 0, std::vector<char> keys = {}) {
    Input::ClearTypedText();
    for (const auto& f : fingers) Input::FeedTouch(f.id, f.state, f.pos);
    Input::FeedKeys(keys);
    Input::FeedMouse(mouse, buttons);
    UpdateUILayouts(s);
    s.Update(0.016f);
}

// Key press edge for navigation: release everything, then press `k`, then navigate.
void Press(Scene& s, char k) {
    Input::FeedKeys({});
    Input::FeedKeys({k});
    NavigateUI(s);
}

UIButton* Button(Scene& s, GameObject* parent, const char* name, Vec2 pos, Vec2 size = {100, 40}) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    auto* b = g->AddComponent<UIButton>();
    b->position = pos; b->size = size;
    return b;
}

} // namespace

int main() {
    RUN_SUITE("ui_nav_touch");
    InstallHitTests();

    // ---- Input touch list: phases, deltas, lift lasts one frame, drag threshold ----
    {
        Scene s("touch");
        Frame(s, {{7, 0, {100, 100}}});
        CHECK(Input::TouchCount() == 1);
        CHECK(Input::GetTouch(0).id == 7 && Input::GetTouch(0).phase == Input::Touch::Phase::Began);
        CHECK(Input::PointerIsTouch());
        Frame(s);
        CHECK(Input::GetTouch(0).phase == Input::Touch::Phase::Stationary);
        Frame(s, {{7, 1, {104, 103}}});
        CHECK(Input::GetTouch(0).phase == Input::Touch::Phase::Moved);
        CHECK_NEAR(Input::GetTouch(0).delta.x, 4.0f, 1e-4f);
        CHECK_NEAR(Input::GetTouch(0).delta.y, 3.0f, 1e-4f);
        CHECK(!Input::TouchDragging());                       // 5 px: still a tap
        Frame(s, {{8, 0, {300, 300}}, {7, 1, {120, 100}}});
        CHECK(Input::TouchCount() == 2);
        CHECK(Input::TouchDragging());                        // 20 px: a drag
        Frame(s, {{7, 2, {120, 100}}, {8, 2, {300, 300}}});
        CHECK(Input::TouchCount() == 2);                      // lifted, still visible this frame
        CHECK(Input::GetTouch(0).phase == Input::Touch::Phase::Ended);
        Frame(s);
        CHECK(Input::TouchCount() == 0);
        CHECK(!Input::PointerIsTouch());
        Frame(s, {{9, 1, {1, 1}}, {9, 2, {1, 1}}});           // events for an unknown finger: ignored
        CHECK(Input::TouchCount() == 0);
    }

    // ---- Tap: a button fires when the finger lifts; a drag never fires it ----
    {
        Scene s("tap");
        UIButton* b = Button(s, nullptr, "b", {100, 100});
        UIButton* rep = Button(s, nullptr, "rep", {300, 100});
        rep->holdRepeat = true;
        auto* t = s.CreateGameObject("t")->AddComponent<UIToggle>();
        t->position = {100, 300};
        s.Start();
        Frame(s);

        Frame(s, {{1, 0, {120, 110}}}, {120, 110}, 1);        // finger down
        CHECK(!b->WasClicked());
        Frame(s, {}, {120, 110}, 1);
        CHECK(!b->WasClicked());
        Frame(s, {{1, 2, {120, 110}}}, {120, 110}, 0);        // lifted on it
        CHECK(b->WasClicked());
        Frame(s);
        CHECK(!b->WasClicked());

        Frame(s, {{2, 0, {120, 110}}}, {120, 110}, 1);        // down, then drags off 30 px and back
        Frame(s, {{2, 1, {150, 110}}}, {150, 110}, 1);
        Frame(s, {{2, 1, {120, 110}}}, {120, 110}, 1);
        Frame(s, {{2, 2, {120, 110}}}, {120, 110}, 0);
        CHECK(!b->WasClicked());

        Frame(s, {{3, 0, {120, 110}}}, {120, 110}, 1);        // down on it, lifted elsewhere
        Frame(s, {{3, 1, {125, 112}}}, {500, 500}, 1);
        Frame(s, {{3, 2, {125, 112}}}, {500, 500}, 0);
        CHECK(!b->WasClicked());

        Frame(s, {}, {120, 110}, 1);                          // the mouse still clicks on press
        CHECK(b->WasClicked());
        Frame(s);

        Frame(s, {{4, 0, {320, 110}}}, {320, 110}, 1);        // hold-repeat buttons fire on touch-down
        CHECK(rep->WasClicked());
        Frame(s, {{4, 2, {320, 110}}}, {320, 110}, 0);

        const bool was = t->on;                               // toggles flip on lift too
        Frame(s, {{5, 0, {110, 310}}}, {110, 310}, 1);
        CHECK(t->on == was);
        Frame(s, {{5, 2, {110, 310}}}, {110, 310}, 0);
        CHECK(t->on != was);
        Frame(s);
    }

    // ---- Scroll view: a finger drags the content, a flick glides, bars are excluded ----
    {
        Scene s("scroll");
        auto* sv = s.CreateGameObject("sv")->AddComponent<UIScrollView>();
        sv->position = {0, 0}; sv->size = {300, 200};
        sv->autoContent = false; sv->contentHeight = 2000.0f;
        UIButton* row = Button(s, sv->gameObject, "row", {10, 130});
        s.Start();
        Frame(s);

        Frame(s, {{1, 0, {50, 150}}}, {50, 150}, 1);          // lands on the row's button
        Frame(s, {{1, 1, {50, 145}}}, {50, 145}, 1);
        CHECK_NEAR(sv->scroll, 0.0f, 1e-4f);                  // under the drag threshold
        Frame(s, {{1, 1, {50, 100}}}, {50, 100}, 1);
        CHECK_NEAR(sv->scroll, 50.0f, 1e-3f);                 // whole distance once it's a drag
        CHECK(sv->IsTouchDragging());
        Frame(s, {{1, 1, {50, 60}}}, {50, 60}, 1);
        CHECK_NEAR(sv->scroll, 90.0f, 1e-3f);
        Frame(s, {{1, 2, {50, 40}}}, {50, 40}, 0);            // flick up and lift
        CHECK_NEAR(sv->scroll, 110.0f, 1e-3f);
        CHECK(!row->WasClicked());                            // the drag didn't press the row
        Frame(s);
        const float glide = sv->scroll;
        CHECK(glide > 110.0f);                                // keeps going after the lift
        for (int i = 0; i < 300; ++i) Frame(s);
        const float rest = sv->scroll;
        Frame(s);
        CHECK_NEAR(sv->scroll, rest, 1e-4f);                  // and comes to rest
        CHECK(rest < sv->ScrollMax());

        Frame(s, {{2, 0, {50, 100}}}, {50, 100}, 1);          // drag down past the top: clamps
        Frame(s, {{2, 1, {50, 1500}}}, {50, 1500}, 1);
        CHECK_NEAR(sv->scroll, 0.0f, 1e-4f);
        Frame(s, {{2, 2, {50, 1500}}}, {50, 1500}, 0);
        for (int i = 0; i < 5; ++i) Frame(s);
        CHECK_NEAR(sv->scroll, 0.0f, 1e-4f);

        sv->SetScroll(100.0f);
        Frame(s, {{3, 0, {296, 100}}}, {296, 100}, 1);        // on the scrollbar: the bar handles it
        Frame(s, {{3, 1, {296, 40}}}, {296, 40}, 1);
        CHECK(!sv->IsTouchDragging());
        Frame(s, {{3, 2, {296, 40}}}, {296, 40}, 0);
        Frame(s);

        sv->SetScroll(100.0f);
        Frame(s, {{4, 0, {200, 100}}}, {200, 100}, 1);        // sideways drag on a vertical view
        Frame(s, {{4, 1, {100, 100}}}, {100, 100}, 1);
        CHECK_NEAR(sv->scroll, 100.0f, 1e-4f);
        CHECK_NEAR(sv->scrollX, 0.0f, 1e-4f);
        Frame(s, {{4, 2, {100, 100}}}, {100, 100}, 0);

        sv->axis = UIScrollView::Axis::Horizontal;            // a horizontal view takes x
        sv->contentWidth = 1200.0f;
        Frame(s);
        Frame(s, {{5, 0, {200, 100}}}, {200, 100}, 1);
        Frame(s, {{5, 1, {120, 100}}}, {120, 100}, 1);
        CHECK_NEAR(sv->scrollX, 80.0f, 1e-3f);
        Frame(s, {{5, 2, {120, 100}}}, {120, 100}, 0);

        Frame(s, {{6, 0, {500, 500}}}, {500, 500}, 1);        // a finger outside the view: nothing
        Frame(s, {{6, 1, {400, 500}}}, {400, 500}, 1);
        CHECK(!sv->IsTouchDragging());
        Frame(s, {{6, 2, {400, 500}}}, {400, 500}, 0);
    }

    // ---- List view: drag-scroll through a virtualized list ----
    {
        Scene s("list");
        auto* lv = s.CreateGameObject("list")->AddComponent<UIListView>();
        lv->position = {0, 0}; lv->size = {300, 240};
        lv->itemHeight = 40.0f; lv->spacing = 0.0f;
        for (int i = 0; i < 100; ++i) lv->items.push_back("row");
        Button(s, lv->gameObject, "template", {0, 0}, {300, 40});
        s.Start();
        Frame(s);
        Frame(s, {{1, 0, {100, 200}}}, {100, 200}, 1);
        Frame(s, {{1, 1, {100, 120}}}, {100, 120}, 1);
        CHECK_NEAR(lv->scroll, 80.0f, 1e-3f);
        Frame(s, {{1, 2, {100, 120}}}, {100, 120}, 0);
        CHECK(lv->selected < 0);                              // dragging didn't select a row
        Frame(s);
        Frame(s, {{2, 0, {100, 100}}}, {100, 100}, 1);        // a tap selects the row under it
        Frame(s, {{2, 2, {100, 100}}}, {100, 100}, 0);
        Frame(s);
        CHECK(lv->selected >= 0);
    }

    // ---- 2-D navigation over a grid ----
    {
        Scene s("grid");
        UIButton* g[2][3];
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c)
                g[r][c] = Button(s, nullptr, "cell", {100.0f + c * 150.0f, 100.0f + r * 100.0f});
        s.Start();
        Frame(s);
        Input::FeedKeys({}); NavigateUI(s);
        CHECK(g[0][0]->IsFocused());
        Press(s, 'd'); CHECK(g[0][1]->IsFocused());
        Press(s, 's'); CHECK(g[1][1]->IsFocused());
        Press(s, 'a'); CHECK(g[1][0]->IsFocused());
        Press(s, 'w'); CHECK(g[0][0]->IsFocused());
        Press(s, 'd'); Press(s, 'd'); CHECK(g[0][2]->IsFocused());
        Press(s, 'd'); CHECK(g[0][2]->IsFocused());           // nothing further right: stays
        Press(s, 's'); CHECK(g[1][2]->IsFocused());
        Press(s, 's'); CHECK(g[0][0]->IsFocused());           // down off the bottom wraps to the first
        Press(s, 'w'); CHECK(g[1][2]->IsFocused());           // up off the top wraps to the last
        int focusedCount = 0;
        for (auto& row : g) for (auto* b : row) focusedCount += b->IsFocused() ? 1 : 0;
        CHECK(focusedCount == 1);

        // D-pad works the same (gamepad buttons 8..11 = up/down/left/right).
        Input::FeedKeys({});
        Input::FeedGamepad({0, 0}, 0); Input::FeedGamepad({0, 0}, 1u << 10); NavigateUI(s);
        CHECK(g[1][1]->IsFocused());
        Input::FeedGamepad({0, 0}, 0); Input::FeedGamepad({0, 0}, 1u << 8); NavigateUI(s);
        CHECK(g[0][1]->IsFocused());
        Input::FeedGamepad({0, 0}, 0);
    }

    // ---- Straight ahead beats a closer widget off to the side ----
    {
        Scene s("ahead");
        UIButton* from = Button(s, nullptr, "from", {100, 100});
        UIButton* ahead = Button(s, nullptr, "ahead", {100, 400});
        UIButton* side = Button(s, nullptr, "side", {400, 200});
        s.Start();
        Frame(s);
        Input::FeedKeys({}); NavigateUI(s);
        CHECK(from->IsFocused());
        Press(s, 's');
        CHECK(ahead->IsFocused());
        CHECK(!side->IsFocused());
    }

    // ---- Left/Right stay with a slider; moving onto it doesn't nudge its value ----
    {
        Scene s("slider");
        UIButton* btn = Button(s, nullptr, "btn", {100, 100});
        auto* sl = s.CreateGameObject("sl")->AddComponent<UISlider>();
        sl->position = {300, 110}; sl->size = {200, 20};
        sl->minValue = 0.0f; sl->maxValue = 1.0f; sl->value = 0.5f;
        s.Start();
        Frame(s);
        Input::FeedKeys({}); NavigateUI(s);
        CHECK(btn->IsFocused());
        Press(s, 'd');
        CHECK(sl->IsFocused());
        CHECK_NEAR(sl->value, 0.5f, 1e-5f);
        Press(s, 'd');
        CHECK(sl->IsFocused());
        CHECK_NEAR(sl->value, 0.55f, 1e-4f);
        Press(s, 'a');
        CHECK(sl->IsFocused());                               // Left adjusts, never leaves the slider
        CHECK_NEAR(sl->value, 0.5f, 1e-4f);
        Press(s, 's');                                        // Up/Down still move away from it
        CHECK(btn->IsFocused());
    }

    // ---- Focus moving onto a hidden widget scrolls its Scroll View to show it ----
    {
        Scene s("navscroll");
        auto* sv = s.CreateGameObject("sv")->AddComponent<UIScrollView>();
        sv->position = {100, 100}; sv->size = {300, 200};
        std::vector<UIButton*> b;
        for (int i = 0; i < 10; ++i) b.push_back(Button(s, sv->gameObject, "item", {110, 110.0f + i * 50.0f}));
        s.Start();
        Frame(s);
        Input::FeedKeys({}); NavigateUI(s);
        CHECK(b[0]->IsFocused());
        for (int i = 0; i < 3; ++i) Press(s, 's');
        CHECK(b[3]->IsFocused());
        CHECK_NEAR(sv->scroll, 0.0f, 1e-4f);                  // still fully visible: no scroll
        Press(s, 's');
        UpdateUILayouts(s);
        CHECK(b[4]->IsFocused());
        CHECK_NEAR(sv->scroll, 50.0f, 1e-3f);                 // bottom edge (250) brought to the viewport's (200)
        for (int i = 0; i < 5; ++i) { Press(s, 's'); UpdateUILayouts(s); }
        CHECK(b[9]->IsFocused());
        CHECK_NEAR(sv->scroll, 300.0f, 1e-3f);                // its bottom (600) lands on the viewport's (300)
        Press(s, 's'); UpdateUILayouts(s);                    // wraps to the first: scrolls back up
        CHECK(b[0]->IsFocused());
        CHECK_NEAR(sv->scroll, 10.0f, 1e-3f);                 // just enough: its top (110) at the viewport's (100)
    }

    // ---- ...and a List View likewise ----
    {
        Scene s("navlist");
        auto* lv = s.CreateGameObject("list")->AddComponent<UIListView>();
        lv->position = {0, 0}; lv->size = {300, 240};
        lv->itemHeight = 40.0f; lv->spacing = 0.0f;
        for (int i = 0; i < 100; ++i) lv->items.push_back("row");
        Button(s, lv->gameObject, "template", {0, 0}, {300, 40});
        s.Start();
        Frame(s);
        Input::FeedKeys({}); NavigateUI(s);
        for (int i = 0; i < 6; ++i) { Press(s, 's'); UpdateUILayouts(s); }
        CHECK_NEAR(lv->scroll, 40.0f, 1e-3f);                 // the 7th row (240..280) scrolled into view
    }

    TEST_MAIN_RESULT();
}
