// UITouchScroll: a finger inside the view drags the content the other way, a
// flick keeps moving after the lift, and a tap or a locked axis does not.
#include "test_framework.hpp"
#include "okay/Components/UITouchScroll.hpp"
#include <cmath>

using namespace okay;

namespace {

bool Inside(const Vec2& p) { return p.x >= 0.0f && p.x < 200.0f && p.y >= 0.0f && p.y < 100.0f; }
Vec2 Ident(const Vec2& p) { return p; }
Vec2 Half(const Vec2& p) { return p * 0.5f; }

void Frame(long long id, int state, const Vec2& pos) {
    Input::ClearTypedText();
    if (id >= 0) Input::FeedTouch(id, state, pos);
}

} // namespace

int main() {
    RUN_SUITE("ui_touch_scroll");
    Input::ClearTypedText();

    // A finger outside never starts a drag. A short move inside is still a tap.
    {
        UITouchScroll sc;
        Frame(1, 0, {400, 400});
        Vec2 d = sc.Step(0.05f, Inside, Ident);
        CHECK(!sc.tracking && !sc.dragging);
        CHECK_NEAR(d.x, 0.0f, 1e-4f);
        CHECK_NEAR(d.y, 0.0f, 1e-4f);

        Frame(1, 2, {400, 400});
        sc.Step(0.05f, Inside, Ident);
        Frame(-1, 0, {});

        Frame(2, 0, {20, 20});
        d = sc.Step(0.05f, Inside, Ident);
        CHECK(sc.tracking && !sc.dragging);
        CHECK_NEAR(d.x, 0.0f, 1e-4f);
        Frame(2, 1, {24, 20});                                 // 4 px, under the 10 px threshold
        d = sc.Step(0.05f, Inside, Ident);
        CHECK(sc.tracking && !sc.dragging);
        CHECK_NEAR(d.x, 0.0f, 1e-4f);
        Frame(2, 2, {24, 20});
        d = sc.Step(0.05f, Inside, Ident);
        CHECK(!sc.tracking);
        CHECK_NEAR(d.x, 0.0f, 1e-4f);
        Frame(-1, 0, {});
        d = sc.Step(0.05f, Inside, Ident);                     // a tap does not fling
        CHECK_NEAR(d.x, 0.0f, 1e-4f);
        CHECK_NEAR(d.y, 0.0f, 1e-4f);
    }

    // Crossing the threshold scrolls by the whole movement, opposite the finger.
    // Later frames scroll by that frame only. A second finger does not take over.
    {
        UITouchScroll sc;
        Frame(3, 0, {40, 40});
        sc.Step(0.05f, Inside, Ident);
        Frame(3, 1, {40, 80});                                 // finger moved down 40
        Vec2 d = sc.Step(0.05f, Inside, Ident);
        CHECK(sc.dragging);
        CHECK_NEAR(d.x, 0.0f, 1e-3f);
        CHECK_NEAR(d.y, -40.0f, 1e-3f);
        Frame(9, 0, {10, 10});                                 // a new finger begins; the first one still owns the drag
        Input::FeedTouch(3, 1, {70, 80});                      // and the first finger moves right 30 this frame
        d = sc.Step(0.05f, Inside, Ident);
        CHECK(sc.id == 3);
        CHECK_NEAR(d.x, -30.0f, 1e-3f);
        CHECK_NEAR(d.y, 0.0f, 1e-3f);
        Frame(3, 2, {70, 80});
        Input::FeedTouch(9, 2, {10, 10});
        sc.Step(0.05f, Inside, Ident);
        Frame(-1, 0, {});
    }

    // A scaled view: local movement is what toLocal reports, not the raw pixels.
    {
        UITouchScroll sc;
        Frame(4, 0, {10, 10});
        sc.Step(0.05f, Inside, Half);
        Frame(4, 1, {10, 50});                                 // 40 screen px down, 20 local
        Vec2 d = sc.Step(0.05f, Inside, Half);
        CHECK_NEAR(d.y, -20.0f, 1e-3f);
        Frame(4, 2, {10, 50});
        sc.Step(0.05f, Inside, Half);
        Frame(-1, 0, {});
    }

    // A vertical-only scroller ignores a horizontal drag, including the fling.
    {
        UITouchScroll sc;
        sc.scrollX = false;
        Frame(5, 0, {20, 40});
        sc.Step(0.05f, Inside, Ident);
        Frame(5, 1, {80, 40});
        Vec2 d = sc.Step(0.05f, Inside, Ident);
        CHECK(sc.dragging);
        CHECK_NEAR(d.x, 0.0f, 1e-4f);
        CHECK_NEAR(d.y, 0.0f, 1e-4f);
        Frame(5, 2, {80, 40});
        d = sc.Step(0.05f, Inside, Ident);
        CHECK_NEAR(d.x, 0.0f, 1e-4f);
        Frame(-1, 0, {});
        d = sc.Step(0.05f, Inside, Ident);
        CHECK_NEAR(d.x, 0.0f, 1e-4f);
        CHECK_NEAR(d.y, 0.0f, 1e-4f);
    }

    // A flick keeps going in the drag direction, slows down, and Stop() ends it.
    {
        UITouchScroll sc;
        Frame(6, 0, {30, 80});
        sc.Step(0.016f, Inside, Ident);
        Frame(6, 1, {30, 20});                                 // finger moved up
        Vec2 dragged = sc.Step(0.016f, Inside, Ident);
        CHECK(dragged.y > 20.0f);
        Frame(6, 2, {30, 5});                                  // the lift carries a bit more movement
        sc.Step(0.016f, Inside, Ident);
        CHECK(!sc.tracking);
        Frame(-1, 0, {});
        Vec2 coast = sc.Step(0.016f, Inside, Ident);
        CHECK(coast.y > 0.0f);                                 // still moving the way the drag scrolled
        float prev = coast.y;
        bool slowed = false;
        for (int i = 0; i < 8; ++i) {
            coast = sc.Step(0.016f, Inside, Ident);
            if (coast.y > 0.0f && coast.y < prev) slowed = true;
            prev = coast.y;
        }
        CHECK(slowed);
        sc.Stop();
        coast = sc.Step(0.016f, Inside, Ident);
        CHECK_NEAR(coast.x, 0.0f, 1e-4f);
        CHECK_NEAR(coast.y, 0.0f, 1e-4f);
        Frame(-1, 0, {});
        coast = sc.Step(0.05f, Inside, Ident);
        CHECK_NEAR(coast.y, 0.0f, 1e-4f);                      // it stays stopped
    }

    Input::ClearTypedText();
    TEST_MAIN_RESULT();
}
