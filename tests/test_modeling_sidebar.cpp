// Fold state and width maths of the Modeling window's page list (editor/ModelingSidebar.hpp).
#include "ModelingSidebar.hpp"
#include "ModelingToolIndex.hpp"
#include "EditorLayoutMath.hpp"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
using namespace oked::modeltools;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main() {
    // Default: open, no manual choice.
    SidebarState s; CHECK(!s.folded && !s.manual && SidebarToPref(s) == 0);
    CHECK(SidebarWidth(s, 1200) == kSidebarOpenW && SidebarWidth(SidebarState{true, true}, 1200) == kSidebarStripW);
    // Never cut: the open list is as wide as its text needs and never shrinks below that, at any window width.
    const float longText = 230.0f;   // a long name or hint, measured in the current font
    const float need = SidebarOpenWidth(longText, 1.0f, 30.0f);
    CHECK(need >= kIconColW + longText + 30.0f && need > kSidebarOpenW);
    CHECK(SidebarOpenWidth(10.0f) == kSidebarOpenW);   // short text: the 196 px minimum
    for (float w : {300.0f, 400.0f, 500.0f, 640.0f, 700.0f, 900.0f, 1600.0f}) {
        SidebarState o; CHECK(SidebarWidth(o, w, 1.0f, need) == need);                // open: always the full width, text fits
        SidebarState f{true, true}; CHECK(SidebarWidth(f, w, 1.0f, need) == kSidebarStripW);
        CHECK(PageAreaWidth(f, w, 1.0f, need) >= PageAreaWidth(o, w, 1.0f, need));
        // Automatic rule: either the list is folded to icons, or the open list AND a usable page area fit.
        SidebarState a; SidebarAuto(a, w, 1.0f, need);
        CHECK(a.folded || PageAreaWidth(a, w, 1.0f, need) >= kMinPageAreaW);
    }
    // The fold line moves out when the text needs a wider list.
    CHECK(FoldBelowWidth(1.0f, 0.0f) == kFoldBelowW && FoldBelowWidth(1.0f, need) >= need + kMinPageAreaW);
    // UI scale: every size grows with it, the folding thresholds too.
    for (float sc : {1.0f, 1.25f, 1.5f, 2.0f}) {
        SidebarState o; CHECK(SidebarWidth(o, 3000, sc) == kSidebarOpenW * sc);
        CHECK(SidebarWidth(SidebarState{true, true}, 3000, sc) == kSidebarStripW * sc);
        SidebarState a; SidebarAuto(a, kFoldBelowW * sc - 1, sc); CHECK(a.folded);
        SidebarAuto(a, kUnfoldAboveW * sc + 1, sc); CHECK(!a.folded);
        CHECK(RowHeight(sc, 15.0f * sc) >= 2 * 15.0f * sc);
        CHECK(StripRowHeight(sc, 15.0f * sc) >= 15.0f * sc);
    }
    // The owner's case (2.903.8.27): a docked Modeling window about 1000 px wide at 125 % shows the full names.
    { SidebarState a; SidebarAuto(a, 1000.0f, 1.25f, SidebarOpenWidth(170.0f * 1.25f, 1.25f, 30.0f)); CHECK(!a.folded); }
    // Greyed pages say why; usable ones say nothing.
    CHECK(std::string(PageDisabledReason(false, false)).empty() && std::string(PageDisabledReason(true, true)).empty());
    CHECK(std::string(PageDisabledReason(true, false)).find("model") != std::string::npos);
    // Toggle is a manual choice, both ways.
    SidebarToggle(s); CHECK(s.folded && s.manual); SidebarToggle(s); CHECK(!s.folded && s.manual);
    // Automatic rule with hysteresis: fold below 660, unfold above 740, nothing in between.
    SidebarState a;
    SidebarAuto(a, 1000); CHECK(!a.folded);
    SidebarAuto(a, 670); CHECK(!a.folded);               // still open just above the fold line
    SidebarAuto(a, 659); CHECK(a.folded && !a.manual);
    SidebarAuto(a, 690); CHECK(a.folded);                // small growth: stays folded
    SidebarAuto(a, 670); SidebarAuto(a, 700); SidebarAuto(a, 650); CHECK(a.folded);   // wobbling inside the band never flips it
    SidebarAuto(a, 741); CHECK(!a.folded && !a.manual);
    SidebarAuto(a, 700); CHECK(!a.folded);               // and stays open
    // A manual choice wins over the automatic rule, in both directions.
    SidebarState m; SidebarToggle(m);                    // folded by choice
    SidebarAuto(m, 2000); CHECK(m.folded);
    SidebarState n; SidebarToggle(n); SidebarToggle(n);  // open by choice
    SidebarAuto(n, 300); CHECK(!n.folded);
    // Persistence: the number round-trips; junk reads as the default.
    for (int v : {0, 1, 2}) CHECK(SidebarToPref(SidebarFromPref(v)) == v);
    CHECK(SidebarToPref(SidebarFromPref(7)) == 0 && SidebarToPref(SidebarFromPref(-3)) == 0);
    CHECK(SidebarFromPref(1).folded && SidebarFromPref(1).manual && !SidebarFromPref(2).folded && SidebarFromPref(2).manual && !SidebarFromPref(0).manual);
    // Through the settings file (ViewPrefs, key modelingsidebarfolded).
    {
        okay::edlayout::ViewPrefs d; CHECK(d.modelingSidebar == 0);
        okay::edlayout::ViewPrefs p; p.modelingSidebar = 1;
        CHECK(okay::edlayout::ViewPrefs::Parse(p.Write()).modelingSidebar == 1);
        CHECK(okay::edlayout::ViewPrefs::Parse("modelingsidebarfolded 2\n").modelingSidebar == 2);
        CHECK(okay::edlayout::ViewPrefs::Parse("modelingsidebarfolded 9\n").modelingSidebar == 0);
    }
    // The folded strip shows one picture per page (names as tooltips): every page has its own icon, the Easy page first.
    std::set<int> icons; for (const Page& p : Pages()) CHECK(icons.insert((int)p.icon).second);
    CHECK(Pages().front().icon == PageIcon::Star && std::string(Pages().front().name) == "Easy");
    {
        std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/editor/main.cpp"); std::stringstream ss; ss << in.rdbuf();
        CHECK(ss.str().find("ImGuiKey_Backslash") != std::string::npos);
        CHECK(ss.str().find("Back to the simple view") == std::string::npos);
        CHECK(ss.str().find("DrawPageList(") != std::string::npos);   // the window draws the shared page list
    }
    {   // The page list never shortens text with "..." and never draws the old two-letter strip.
        std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/editor/ModelingSidebarUi.cpp"); std::stringstream ss; ss << in.rdbuf();
        CHECK(ss.str().find("FitText") == std::string::npos && ss.str().find("abbr") == std::string::npos);
        CHECK(ss.str().find("IconFor(pi.icon)") != std::string::npos && ss.str().find("SetTooltip") != std::string::npos);
    }
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("modeling_sidebar: all passed\n");
    return 0;
}
