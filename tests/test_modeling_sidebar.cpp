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
    // The page area always keeps its minimum: a narrow open list shrinks, never below the strip; folding frees width.
    for (float w : {400.0f, 500.0f, 640.0f, 700.0f, 900.0f, 1600.0f}) {
        SidebarState o; const float sw = SidebarWidth(o, w);
        CHECK(sw >= kSidebarStripW && sw <= kSidebarOpenW);
        if (w >= kMinPageAreaW + kSidebarStripW) CHECK(PageAreaWidth(o, w) >= kMinPageAreaW);
        SidebarState f{true, true}; CHECK(PageAreaWidth(f, w) >= PageAreaWidth(o, w));
        CHECK(PageAreaWidth(f, w) == w - kSidebarStripW);
    }
    CHECK(SidebarWidth(SidebarState{}, 900) == kSidebarOpenW && SidebarWidth(SidebarState{}, 560) == 140);
    // UI scale: every size grows with it, the page area keeps its (scaled) minimum, folding thresholds scale too.
    for (float sc : {1.0f, 1.25f, 1.5f, 2.0f}) {
        SidebarState o; CHECK(SidebarWidth(o, 3000, sc) == kSidebarOpenW * sc);
        CHECK(SidebarWidth(SidebarState{true, true}, 3000, sc) == kSidebarStripW * sc);
        for (float w : {500.0f, 900.0f, 1400.0f}) { const float sw = SidebarWidth(o, w, sc); CHECK(sw >= kSidebarStripW * sc && sw <= kSidebarOpenW * sc); }
        SidebarState a; SidebarAuto(a, kFoldBelowW * sc - 1, sc); CHECK(a.folded);
        SidebarAuto(a, kUnfoldAboveW * sc + 1, sc); CHECK(!a.folded);
        CHECK(RowHeight(sc, 15.0f * sc) >= 2 * 15.0f * sc);
        CHECK(StripRowHeight(sc, 15.0f * sc) >= 15.0f * sc);
    }
    // Text is cut with "..." so it never runs out of its row.
    const auto mw = [](const std::string& t) { return (float)t.size() * 7.0f; };
    CHECK(FitText("Easy", 100, mw) == "Easy");
    { const std::string f = FitText("Fences, buildings, cars", 100, mw); CHECK(f.size() * 7 <= 100 && f.size() > 3 && f.substr(f.size() - 3) == "..."); }
    CHECK(FitText("Hello", 10, mw).empty());
    for (float w : {30.0f, 60.0f, 90.0f, 140.0f}) CHECK(mw(FitText("Make something big", w, mw)) <= w);
    // Greyed pages say why; usable ones say nothing.
    CHECK(std::string(PageDisabledReason(false, false)).empty() && std::string(PageDisabledReason(true, true)).empty());
    CHECK(std::string(PageDisabledReason(true, false)).find("model") != std::string::npos);
    // Toggle is a manual choice, both ways.
    SidebarToggle(s); CHECK(s.folded && s.manual); SidebarToggle(s); CHECK(!s.folded && s.manual);
    // Automatic rule with hysteresis: fold below 760, unfold above 840, nothing in between.
    SidebarState a;
    SidebarAuto(a, 1000); CHECK(!a.folded);
    SidebarAuto(a, 770); CHECK(!a.folded);               // still open just above the fold line
    SidebarAuto(a, 759); CHECK(a.folded && !a.manual);
    SidebarAuto(a, 790); CHECK(a.folded);                // small growth: stays folded
    SidebarAuto(a, 770); SidebarAuto(a, 800); SidebarAuto(a, 750); CHECK(a.folded);   // wobbling inside the band never flips it
    SidebarAuto(a, 841); CHECK(!a.folded && !a.manual);
    SidebarAuto(a, 800); CHECK(!a.folded);               // and stays open
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
    // The folded strip: every page has a short unique label (2 letters) so clicking one opens that page.
    std::set<std::string> ab; for (const Page& p : Pages()) { CHECK(std::string(p.abbr).size() >= 1 && std::string(p.abbr).size() <= 2); CHECK(ab.insert(p.abbr).second); }
    // The folded strip has the Easy page first, and Ctrl+\ still folds the list (the key is wired in the window).
    CHECK(std::string(Pages().front().abbr) == "Ea");
    for (const Page& p : Pages()) CHECK(std::string(p.abbr).size() == 2 && std::isupper((unsigned char)p.abbr[0]) && std::islower((unsigned char)p.abbr[1]));   // "Ad", not the old "EM"
    for (const Page& p : Pages()) CHECK(std::string(p.abbr)[0] == p.name[0]);   // the letters come from the new name (Fix the surface: Fs)
    {
        std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/editor/main.cpp"); std::stringstream ss; ss << in.rdbuf();
        CHECK(ss.str().find("ImGuiKey_Backslash") != std::string::npos);
        CHECK(ss.str().find("Back to the simple view") == std::string::npos);
    }
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("modeling_sidebar: all passed\n");
    return 0;
}
