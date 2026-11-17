// The node search popup's pure logic (editor/VisualScriptSearchNav.hpp, no ImGui): panel size and clamping,
// selection movement, scroll-into-view maths, category stepping.
#include "test_framework.hpp"
#include "VisualScriptSearchNav.hpp"
#include <string>
#include <vector>

using namespace okay::editor::vsed::searchnav;

int main() {
    RUN_SUITE("vs_search_nav");

    {   // Default size: about 560x520 at Comfortable, growing with the density, never bigger than the screen.
        const PanelSize c = DefaultPanelSize(1, 1920, 1080);
        CHECK(c.w == 560 && c.h == 520);
        CHECK(DefaultPanelSize(0, 1920, 1080).w < c.w);
        CHECK(DefaultPanelSize(2, 1920, 1080).w > c.w);
        const PanelSize small = DefaultPanelSize(1, 400, 300);
        CHECK(small.w <= 400 - 24 && small.h <= 300 - 24);
        CHECK(small.w > 0 && small.h > 0);
    }
    {   // A remembered size is kept when it fits, pulled in when the screen is smaller, and never tiny.
        PanelSize r = ClampPanelSize({700, 600}, 1920, 1080);
        CHECK(r.w == 700 && r.h == 600);
        r = ClampPanelSize({3000, 3000}, 1000, 800);
        CHECK(r.w <= 1000 - 24 && r.h <= 800 - 24);
        r = ClampPanelSize({10, 10}, 1920, 1080);
        CHECK(r.w >= 420 && r.h >= 320);
        r = ClampPanelSize({0, -5}, 1920, 1080);
        CHECK(r.w >= 420 && r.h >= 320);
        CHECK(!ParsePanelSize("700x600").bad && ParsePanelSize("700x600").w == 700 && ParsePanelSize("700x600").h == 600);
        CHECK(ParsePanelSize("junk").bad && ParsePanelSize("5x").bad && ParsePanelSize("").bad);
        CHECK(FormatPanelSize({701.4f, 600.6f}) == "701x601");
    }
    {   // Categories get about a third of the width, within sane limits.
        CHECK(CategoryPaneWidth(560) >= 160 && CategoryPaneWidth(560) <= 240);
        CHECK(CategoryPaneWidth(300) == 160 && CategoryPaneWidth(2000) == 240);
    }
    {   // Row height: the density's row height, or two lines of text when that is taller.
        CHECK(RowHeight(44.0f, 16.0f, 8.0f) == 48.0f);   // 2 lines (32) + 2*8 beats 44
        CHECK(RowHeight(60.0f, 16.0f, 8.0f) == 60.0f);
    }
    {   // Selection movement.
        const int n = 20, page = 6;
        CHECK(MoveSelection(0, n, Move::Down, page) == 1);
        CHECK(MoveSelection(19, n, Move::Down, page) == 19);   // stops at the end
        CHECK(MoveSelection(0, n, Move::Up, page) == 0);       // stops at the start
        CHECK(MoveSelection(5, n, Move::Up, page) == 4);
        CHECK(MoveSelection(7, n, Move::Home, page) == 0);
        CHECK(MoveSelection(7, n, Move::End, page) == 19);
        CHECK(MoveSelection(2, n, Move::PageDown, page) == 8);
        CHECK(MoveSelection(17, n, Move::PageDown, page) == 19);
        CHECK(MoveSelection(8, n, Move::PageUp, page) == 2);
        CHECK(MoveSelection(3, n, Move::PageUp, page) == 0);
        CHECK(MoveSelection(5, 0, Move::Down, page) == 0);     // an empty list has no selection to move
        CHECK(MoveSelection(99, n, Move::None, page) == 19);   // out of range is pulled back
        CHECK(MoveSelection(-4, n, Move::None, page) == 0);
        CHECK(MoveSelection(2, n, Move::PageDown, 0) == 3);    // a zero page still moves
    }
    {   // Scroll into view: nothing moves while the row is visible; otherwise just enough to show it with a margin.
        // rows 40 tall, view 200 tall, margin 8.
        CHECK(RevealScroll(0, 200, 40, 40, 8) == 0);           // row 1 fully inside
        CHECK(RevealScroll(0, 200, 200, 40, 8) == 48.0f);      // row at 200..240 is below: bottom lands at 240+8-200
        CHECK(RevealScroll(120, 200, 40, 40, 8) == 32.0f);     // row at 40..80 is above: top lands at 40-8
        CHECK(RevealScroll(0, 200, 0, 40, 8) == 0);            // first row never scrolls negative
        CHECK(RevealScroll(100, 200, 120, 40, 8) == 100);      // inside, unchanged
        CHECK(RevealScroll(0, 30, 100, 40, 8) == 100 - 8);     // view smaller than a row: show the top
        CHECK(MaxScroll(1000, 200) == 800 && MaxScroll(100, 200) == 0);
    }
    {   // Left / Right step through the visible categories and stop at the ends.
        const std::vector<int> ids = {-4, -2, -1, -3, 0, 1, 2};
        CHECK(StepCategory(ids, -4, +1) == -2);
        CHECK(StepCategory(ids, 2, +1) == 2);
        CHECK(StepCategory(ids, -4, -1) == -4);
        CHECK(StepCategory(ids, 0, -1) == -3);
        CHECK(StepCategory(ids, 77, +1) == -4);                // unknown current: start at the first
        CHECK(StepCategory({}, 5, +1) == 5);
    }
    TEST_MAIN_RESULT();
}
