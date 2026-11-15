// Which Inspector right-click menu a click opens (editor/InspectorClickRules.hpp, no ImGui, no engine).
//
// The bug this guards: a field's menu is asked for while the cards are drawn, the empty-space menu at the end of the window, and Dear
// ImGui lets the popup asked for last replace the one asked for first. Text has no widget id, so over a field's label or in the gap
// between rows "no widget is hovered" is true and the empty-space menu used to replace the field menu: only a click on the number
// box itself reached the field's menu. The router makes the field claim the click, so exactly one menu opens.
//
// The tests walk the mouse over every pixel of a small Inspector (rows with menus, rows without, widgets, empty space) and run the
// same calls, in the same order, as the panel does (InspectorPanel.cpp: FieldMark for each row while drawing, then the empty-space menu).
#include "test_framework.hpp"
#include "InspectorClickRules.hpp"
#include <vector>

using namespace okay::inspectorctx;

namespace {

// One row of the layout the tests draw: a label on the left, a widget (an item with an id) from `widgetX` to the right edge.
struct Row {
    float top, bottom;      // the row's widgets
    bool hasMenu;           // a field menu exists for it
    bool wholeRow;          // its hot area is the full row (else only the widget)
    float widgetX;          // where the widget starts
};

constexpr float kLeft = 10.0f, kRight = 410.0f, kPad = 3.0f;

// What the panel sees at one mouse position.
struct Opened {
    int fieldMenus = 0;          // how many field menus asked to open
    int fieldIndex = -1;         // which row (the first)
    int backgroundMenus = 0;
};

Opened Click(ClickRouter& router, int frame, const std::vector<Row>& rows, float x, float y, bool released = true, bool windowHovered = true) {
    Opened o;
    const RightClick click{released, x, y};
    bool anyItemHovered = false;
    float cursor = 20.0f;   // where the cursor stood before each row was drawn (rows are 4 px apart)
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        const bool overWidget = x >= r.widgetX && x < kRight && y >= r.top && y < r.bottom;
        anyItemHovered = anyItemHovered || (overWidget && windowHovered);
        bool opens;
        if (r.wholeRow) {
            const Rect row = FieldRowRect(kLeft, kRight, cursor, r.top, r.bottom, kPad);
            opens = router.FieldOpensInRow(frame, click, windowHovered, row, r.hasMenu);
        } else {
            opens = router.FieldOpens(frame, click, windowHovered && overWidget, r.hasMenu);
        }
        if (opens) { ++o.fieldMenus; if (o.fieldIndex < 0) o.fieldIndex = (int)i; }
        cursor = r.bottom + 4.0f;
    }
    if (router.BackgroundOpens(frame, click, windowHovered, anyItemHovered)) ++o.backgroundMenus;
    return o;
}

std::vector<Row> Layout() {
    return {
        {20, 40, true, true, 150},     // row 0: a generic card row with a menu, the whole row answers (label, gap, box)
        {44, 64, true, true, 150},     // row 1: the same
        {68, 88, false, true, 150},    // row 2: a row that has no field menu
        {92, 112, true, false, 150},   // row 3: a hand-drawn row: only its widget answers
    };
}

void TestRowRect() {
    RUN_SUITE("a field's row is the whole width of the card");
    const Rect r = FieldRowRect(10, 410, 20, 22, 40, 3);
    CHECK(r.x0 == 10 && r.x1 == 410);                       // full width, label to the right edge
    CHECK(r.y0 == 19 && r.y1 == 43);                        // the top of the last widget minus the pad is higher than the row top
    CHECK(Contains(r, 10, 30) && Contains(r, 409.9f, 30));  // the label end and the box end
    CHECK(!Contains(r, 410, 30) && !Contains(r, 9.9f, 30));  // half open: the edge belongs to the outside
    CHECK(!Contains(r, 100, 43) && Contains(r, 100, 42.9f));
    // The row top wins when it is higher than the last widget's top (a group that began above it).
    const Rect tall = FieldRowRect(10, 410, 5, 22, 40, 3);
    CHECK(tall.y0 == 5 && tall.y1 == 43);
    // Two stacked rows 4 px apart leave no pixel of the gap to nobody.
    const Rect a = FieldRowRect(kLeft, kRight, 20, 20, 40, kPad), b = FieldRowRect(kLeft, kRight, 44, 44, 64, kPad);
    for (float y = 20.0f; y < 64.0f; y += 0.5f) CHECK(Contains(a, 100, y) || Contains(b, 100, y));
}

void TestOneMenuEverywhere() {
    RUN_SUITE("a right click opens exactly one menu, or none");
    const std::vector<Row> rows = Layout();
    ClickRouter router;
    int frame = 0;
    int labelClicks = 0, gapClicks = 0, boxClicks = 0, emptyClicks = 0;
    for (float y = 0.0f; y < 140.0f; y += 1.0f) {
        for (float x = 0.0f; x < 440.0f; x += 5.0f) {
            ++frame;
            const Opened o = Click(router, frame, rows, x, y);
            CHECK(o.fieldMenus + o.backgroundMenus <= 1);   // never two menus: the second would replace the first
            // Which row's hot area holds this point (the whole-row ones are padded by kPad).
            int row = -1;
            for (std::size_t i = 0; i < rows.size() && row < 0; ++i) {
                const Row& r = rows[i];
                const bool box = x >= r.widgetX && x < kRight && y >= r.top && y < r.bottom;
                const float top = (i == 0 ? 20.0f : rows[i - 1].bottom + 4.0f);
                const Rect area = r.wholeRow ? FieldRowRect(kLeft, kRight, top, r.top, r.bottom, kPad) : Rect{};
                if ((r.wholeRow && Contains(area, x, y)) || (!r.wholeRow && box)) row = (int)i;
            }
            if (row >= 0 && rows[(std::size_t)row].hasMenu) {
                CHECK(o.fieldMenus == 1 && o.backgroundMenus == 0);    // the row's own menu, wherever on the row the click lands
                CHECK(o.fieldIndex == row);
                const Row& r = rows[(std::size_t)row];
                if (x < r.widgetX) ++labelClicks; else if (y >= r.top && y < r.bottom) ++boxClicks; else ++gapClicks;
            } else if (row >= 0) {
                CHECK(o.fieldMenus == 0);                              // a row without a menu opens none of its own
                const Row& r = rows[(std::size_t)row];
                const bool onWidget = x >= r.widgetX && y >= r.top && y < r.bottom;
                CHECK(o.backgroundMenus == (onWidget ? 0 : 1));        // a widget keeps its own say; the label and gaps fall through to the empty space
            } else {
                CHECK(o.fieldMenus == 0 && o.backgroundMenus == 1);    // the empty space (and a hand-drawn row's label): only the empty-space menu
                ++emptyClicks;
            }
        }
    }
    CHECK(labelClicks > 100 && gapClicks > 30 && boxClicks > 100 && emptyClicks > 100);   // the walk covered every kind of place
}

void TestLabelAndGapReachTheFieldMenu() {
    RUN_SUITE("the label and the gap of a row reach its field menu (the reported bug)");
    const std::vector<Row> rows = Layout();
    ClickRouter router;
    // The label of row 1 (text: no widget under the mouse), the gap right of it, and between rows 0 and 1.
    for (const float x : {20.0f, 60.0f, 120.0f}) {
        const Opened o = Click(router, 10 + (int)x, rows, x, 54.0f);
        CHECK(o.fieldMenus == 1 && o.fieldIndex == 1 && o.backgroundMenus == 0);
    }
    const Opened gap = Click(router, 500, rows, 100.0f, 42.0f);   // y 42 lies between row 0 (to 40) and row 1 (from 44)
    CHECK(gap.fieldMenus == 1 && gap.backgroundMenus == 0);
    // The number box itself still works.
    const Opened box = Click(router, 501, rows, 300.0f, 54.0f);
    CHECK(box.fieldMenus == 1 && box.fieldIndex == 1 && box.backgroundMenus == 0);
    // Empty space below the last row: the empty-space menu, nothing else.
    const Opened below = Click(router, 502, rows, 100.0f, 130.0f);
    CHECK(below.fieldMenus == 0 && below.backgroundMenus == 1);
}

void TestNotAClick() {
    RUN_SUITE("nothing opens without a release over the window");
    const std::vector<Row> rows = Layout();
    ClickRouter router;
    CHECK(Click(router, 1, rows, 60.0f, 54.0f, /*released=*/false).fieldMenus == 0);
    CHECK(Click(router, 2, rows, 60.0f, 130.0f, /*released=*/false).backgroundMenus == 0);
    // Another window on top of the Inspector, or a child window, is under the mouse: neither menu opens.
    const Opened covered = Click(router, 3, rows, 60.0f, 54.0f, true, /*windowHovered=*/false);
    CHECK(covered.fieldMenus == 0 && covered.backgroundMenus == 0);
    const Opened coveredSpace = Click(router, 4, rows, 60.0f, 130.0f, true, false);
    CHECK(coveredSpace.fieldMenus == 0 && coveredSpace.backgroundMenus == 0);
}

void TestClaimLastsOneFrame() {
    RUN_SUITE("a claim lasts one frame");
    const std::vector<Row> rows = Layout();
    ClickRouter router;
    CHECK(Click(router, 7, rows, 60.0f, 54.0f).fieldMenus == 1);
    CHECK(router.Claimed(7) && !router.Claimed(8));
    // The next click, in the next frame, on empty space, is not blocked by the earlier claim.
    const Opened next = Click(router, 8, rows, 60.0f, 130.0f);
    CHECK(next.backgroundMenus == 1 && next.fieldMenus == 0);
    // Two clicks in one frame cannot both take it (a field asked twice, or two rows on one pixel): the first wins.
    ClickRouter two;
    const RightClick click{true, 50, 50};
    CHECK(two.FieldOpens(1, click, true, true));
    CHECK(!two.FieldOpens(1, click, true, true));
    CHECK(two.FieldOpens(2, click, true, true));
}

void TestOtherMenuClaims() {
    RUN_SUITE("a menu opened elsewhere (the object header) keeps the empty-space menu closed");
    ClickRouter router;
    const RightClick click{true, 50, 50};
    CHECK(router.BackgroundOpens(3, click, true, false));
    router.Claim(3);
    CHECK(!router.BackgroundOpens(3, click, true, false));
    CHECK(router.BackgroundOpens(4, click, true, false));
    // A widget under the mouse always keeps the empty-space menu closed (the widget has its own say).
    CHECK(!router.BackgroundOpens(5, click, true, true));
}

void TestHasMenuIsLazy() {
    RUN_SUITE("the field is only looked up when a click could open its menu");
    ClickRouter router;
    int looked = 0;
    const auto has = [&] { ++looked; return true; };
    const RightClick idle{false, 10, 10}, click{true, 10, 10};
    CHECK(!router.FieldOpens(1, idle, true, has) && looked == 0);     // no release this frame
    CHECK(!router.FieldOpens(1, click, false, has) && looked == 0);   // the mouse is elsewhere
    CHECK(router.FieldOpens(1, click, true, has) && looked == 1);     // asked now
    CHECK(!router.FieldOpens(1, click, true, has) && looked == 1);    // claimed already: no need to look
    ClickRouter none;
    int calls = 0;
    CHECK(!none.FieldOpens(1, click, true, [&] { ++calls; return false; }) && calls == 1);
    CHECK(!none.Claimed(1));                                            // a field with no menu takes nothing
    CHECK(none.BackgroundOpens(1, click, true, false));
}

}   // namespace

int main() {
    TestRowRect();
    TestOneMenuEverywhere();
    TestLabelAndGapReachTheFieldMenu();
    TestNotAClick();
    TestClaimLastsOneFrame();
    TestOtherMenuClaims();
    TestHasMenuIsLazy();
    TEST_MAIN_RESULT();
}
