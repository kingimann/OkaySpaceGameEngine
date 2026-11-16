// The launcher sidebar's height plan (launcher/SidebarLayout.hpp): normal windows
// keep the full layout with no scrollbar; short windows first shrink the update
// card to one row, then scroll the rows with a visible scrollbar; the list never
// gets a zero or negative height (issue #888 finding 55).
#include "test_framework.hpp"
#include "SidebarLayout.hpp"

int main() {
    RUN_SUITE("launcher_sidebar_layout");
    using okaysidebar::Plan;
    const float rows = 600.0f;   // MENU + SOCIAL + LEARN, roughly

    {   // Tall window: everything fits, nothing changes.
        auto L = Plan(900.0f, 227.0f, 181.0f, rows, 76.0f);
        CHECK(!L.compactUpdate);
        CHECK(!L.scrolls);
        CHECK_NEAR(L.itemsH, 900.0f - 227.0f, 0.01);
    }
    {   // Exactly fits: no compact card, no scrollbar.
        auto L = Plan(827.0f, 227.0f, 181.0f, rows, 76.0f);
        CHECK(!L.compactUpdate && !L.scrolls);
    }
    {   // A bit short: the one-row update card makes it fit.
        auto L = Plan(800.0f, 227.0f, 181.0f, rows, 76.0f);
        CHECK(L.compactUpdate);
        CHECK(!L.scrolls);
        CHECK_NEAR(L.itemsH, 800.0f - 181.0f, 0.01);
    }
    {   // Short window (the finding's case): compact card and a scrollbar.
        auto L = Plan(370.0f, 227.0f, 181.0f, rows, 76.0f);
        CHECK(L.compactUpdate);
        CHECK(L.scrolls);
        CHECK_NEAR(L.itemsH, 370.0f - 181.0f, 0.01);
    }
    {   // No update card (footFull == footCompact): never "compact", just scrolls.
        auto L = Plan(370.0f, 143.0f, 143.0f, rows, 76.0f);
        CHECK(!L.compactUpdate);
        CHECK(L.scrolls);
        CHECK_NEAR(L.itemsH, 370.0f - 143.0f, 0.01);
    }
    {   // Footer bigger than the space: the list keeps its minimum, never <= 0.
        auto L = Plan(150.0f, 227.0f, 181.0f, rows, 76.0f);
        CHECK_NEAR(L.itemsH, 76.0f, 0.01);
        CHECK(L.scrolls);
        auto Z = Plan(-50.0f, 300.0f, 300.0f, rows, 0.0f);
        CHECK(Z.itemsH >= 1.0f);
    }
    {   // First frame (rows not measured yet): full layout, no scrollbar.
        auto L = Plan(370.0f, 227.0f, 181.0f, 0.0f, 76.0f);
        CHECK(!L.compactUpdate && !L.scrolls);
        CHECK_NEAR(L.itemsH, 370.0f - 227.0f, 0.01);
    }
    TEST_MAIN_RESULT();
}
