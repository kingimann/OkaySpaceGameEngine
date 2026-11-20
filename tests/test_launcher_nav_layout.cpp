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
    TEST_MAIN_RESULT();
}
