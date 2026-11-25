// The launcher sidebar's rows fit a short window (launcher/NavSidebarLayout.hpp).
#include "test_framework.hpp"
#include "NavSidebarLayout.hpp"

using okay::launchernav::FitNavRows;

int main() {
    RUN_SUITE("launcher_nav_layout");
    const float rowH = 34.0f, lineH = 17.0f;
    // 13 rows and 3 group labels (MENU / SOCIAL / LEARN). Full need: 13*36 + 3*(22+13.26) = about 573.8.
    {   // A tall window: the full layout, no scrollbar.
        auto f = FitNavRows(13, 3, 700.0f, rowH, lineH, false);
        CHECK(!f.compact); CHECK(!f.scroll); CHECK_NEAR(f.rowH, rowH, 1e-4);
    }
    {   // About 750 high: the area is too short for the full layout (What's New was cut off). Compact fits:
        // 13*32 + 3*17 = 467.
        auto f = FitNavRows(13, 3, 520.0f, rowH, lineH, false);
        CHECK(f.compact); CHECK(!f.scroll); CHECK_NEAR(f.rowH, rowH - 4.0f, 1e-4);
    }
    {   // Even shorter: compact and the scrollbar.
        auto f = FitNavRows(13, 3, 400.0f, rowH, lineH, false);
        CHECK(f.compact); CHECK(f.scroll);
    }
    {   // The icons-only sidebar never goes compact, but scrolls when it does not fit.
        auto f = FitNavRows(13, 3, 400.0f, rowH, lineH, true);
        CHECK(!f.compact); CHECK(f.scroll); CHECK_NEAR(f.rowH, rowH, 1e-4);
        CHECK(!FitNavRows(13, 3, 700.0f, rowH, lineH, true).scroll);
    }
    {   // First frame (nothing counted yet): full layout, no scrollbar.
        auto f = FitNavRows(0, 0, 10.0f, rowH, lineH, false);
        CHECK(!f.compact); CHECK(!f.scroll);
    }
    {   // The rows area ends above the pinned footer at short windows (960 x 600, 720 x 480): it never reaches under it.
        using okay::launchernav::NavItemsHeight;
        for (float winH : {600.0f, 480.0f}) {
            const float headerH = 70.0f, avail = winH - headerH;
            const float footEstimate = 2 * (rowH + 2.0f) + 15.0f + 52.0f;   // Notifications, Settings, the profile card
            const float footReal = footEstimate + 22.0f;                      // what was really drawn (item spacing, the divider)
            const float itemsH = NavItemsHeight(avail, footEstimate, footReal);
            const float footTop = headerH + itemsH, footBottom = footTop + footReal;
            CHECK(footBottom <= winH + 0.5f);                 // the footer fits in the window
            CHECK(headerH + itemsH <= winH - footReal + 0.5f); // the rows end where the footer starts, not under it
            const auto f = FitNavRows(13, 3, itemsH, rowH, lineH, false);
            CHECK(f.compact);                                 // too short for the full layout
            CHECK(f.scroll || 13 * (f.rowH + 2.0f) + 3 * 17.0f <= itemsH);   // and what does not fit scrolls inside its area
        }
        CHECK(NavItemsHeight(100.0f, 50.0f, 0.0f) == 50.0f);   // first frame: the estimate
        CHECK(NavItemsHeight(100.0f, 50.0f, 70.0f) == 30.0f);  // the measured footer wins when it is taller
        CHECK(NavItemsHeight(40.0f, 50.0f, 0.0f) == 0.0f);
        // The area ends after the last whole row: rows ending at 32, 64, 96, 128 in 110 px -> 96 (+2 spacing), no half row.
        using okay::launchernav::WholeRowsHeight;
        CHECK(WholeRowsHeight({32.0f, 64.0f, 96.0f, 128.0f}, 110.0f) == 98.0f);
        CHECK(WholeRowsHeight({}, 110.0f) == 110.0f);              // first frame: nothing known yet
        CHECK(WholeRowsHeight({32.0f, 64.0f}, 200.0f) == 66.0f);
    }
    TEST_MAIN_RESULT();
}
