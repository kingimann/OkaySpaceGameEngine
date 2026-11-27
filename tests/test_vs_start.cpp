// The start screen: the cards (one plain sentence each), when it shows, how it is laid out, and what is remembered.
#include "test_framework.hpp"
#include "VisualScriptModel.hpp"
#include "VisualScriptStart.hpp"
#include "VisualScriptUsagePrefs.hpp"
#include "VisualScriptGlossary.hpp"
#include <cstdio>
#include <string>

using namespace okay::editor::vsed;

int main() {
    RUN_SUITE("vs_start");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};

    // Cards: 6 to 8, one per recipe, a short plain sentence each, in words a 10 year old knows.
    const auto cards = VsStartCards();
    CHECK(cards.size() >= 6 && cards.size() <= 8);
    for (const VsStartCard& c : cards) {
        CHECK(c.recipe >= 0 && c.recipe < VsRecipeCount());
        CHECK(!c.title.empty() && !c.line.empty());
        CHECK(c.line.size() < 110);
        CHECK(c.line.find("Example") == std::string::npos);
        CHECK(c.line.back() == '.');
        CHECK(c.line.find('.') == c.line.size() - 1);                        // exactly one sentence
        CHECK(VsGlossaryViolation(c.title).empty() && VsGlossaryViolation(c.line).empty());
    }
    CHECK(VsStartCards(3).size() == 3);
    CHECK(VsStartSentence("Slides. Example: go.") == "Slides.");

    // Every card inserts into an empty graph already wired (a wire for every recipe), and a second recipe
    // lands under the first without covering it.
    for (const VsStartCard& c : cards) {
        VsEditGraph g;
        const auto made = VsInsertRecipe(g, VsRecipes()[(std::size_t)c.recipe].graph);
        CHECK((int)made.size() == VsGraphNodeCount(VsRecipes()[(std::size_t)c.recipe].graph));
        CHECK(made.size() < 2 || !g.execLinks.empty());   // a one-block Quick recipe has nothing to wire
    }
    {
        VsEditGraph g;
        VsInsertRecipe(g, VsRecipes()[0].graph);
        VsInsertRecipe(g, VsRecipes()[1].graph);
        for (std::size_t i = 0; i < g.nodes.size(); ++i)
            for (std::size_t j = i + 1; j < g.nodes.size(); ++j)
                CHECK(!VsBoxesOverlap(VsEstBox(g, g.nodes[i]), VsEstBox(g, g.nodes[j]), 0.0f));
    }

    // When it shows.
    CHECK(VsStartVisible(0, false, false, false));
    CHECK(!VsStartVisible(1, false, false, false));     // something is on the canvas
    CHECK(!VsStartVisible(0, true, false, false));      // "do not show again"
    CHECK(!VsStartVisible(0, false, true, false));      // "Start empty"
    CHECK(!VsStartVisible(0, false, false, true));      // the file could not be opened

    // Layout: every card inside the canvas, none touching another, in two rows of four.
    for (float w : {700.0f, 1000.0f, 1400.0f}) {
        const float h = 700.0f;
        const auto r = VsStartLayout((int)cards.size(), w, h);
        CHECK(r.size() == cards.size());
        for (std::size_t i = 0; i < r.size(); ++i) {
            CHECK(r[i].x >= 0 && r[i].x + r[i].w <= w && r[i].y >= 100 && r[i].y + r[i].h <= h);
            for (std::size_t j = i + 1; j < r.size(); ++j) {
                const bool apart = r[i].x + r[i].w <= r[j].x || r[j].x + r[j].w <= r[i].x ||
                                   r[i].y + r[i].h <= r[j].y || r[j].y + r[j].h <= r[i].y;
                CHECK(apart);
            }
        }
    }
    CHECK(VsStartLayout(0, 800, 600).empty());

    // What is remembered: text round trip, bad lines ignored, a file round trip.
    {
        VsUsagePrefs p; p.hideStart = true; p.hintsDone = 5;
        CHECK(VsUsagePrefsFromText(VsUsagePrefsToText(p)) == p);
        CHECK(VsUsagePrefsFromText("").hideStart == false);
        CHECK(VsUsagePrefsFromText("nonsense\nhidestart x\nhintsdone 3\n").hintsDone == 3);
        const std::string path = "vs_usage_test_prefs.txt";
        CHECK(VsUsagePrefsSave(p, path));
        CHECK(VsUsagePrefsLoad(path) == p);
        std::remove(path.c_str());
        CHECK(VsUsagePrefsLoad("no_such_vs_usage_file.txt") == VsUsagePrefs{});
    }
    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
