// Code that Game UI (the in-game UI) and OkayUI (the editor and tools toolkit) share instead of each keeping a copy:
// word wrap (okay/UI/TextWrap.hpp), tooltip placement (okay/UI/TooltipPlace.hpp) and HSV colours
// (okay/Math/ColorSpace.hpp). The expected values were printed by the old copies before they were merged, so a
// change here means one of the two toolkits would draw differently.
#include "test_framework.hpp"
#include "okay/Math/ColorSpace.hpp"
#include "okay/UI/TextWrap.hpp"
#include "okay/UI/TooltipPlace.hpp"
#include <string>
#include <vector>

using namespace okay;

namespace {
// A stand-in for a font: 'm' is 3 wide, a space 1, anything else 2.
float Width(const std::string& s) {
    float w = 0.0f;
    for (char c : s) w += c == 'm' ? 3.0f : (c == ' ' ? 1.0f : 2.0f);
    return w;
}
// TextRenderer::WrapTo's use: characters, tabs split words, lines joined with '\n'.
std::string ByChars(const std::string& s, int maxChars) {
    textwrap::Options o;
    o.tabsSeparate = true;
    std::vector<std::string> lines;
    const bool nl = textwrap::Greedy(s, (float)maxChars, [](const std::string& t) { return (float)t.size(); }, lines, o);
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) { if (i) out += '\n'; out += lines[i]; }
    if (nl) out += '\n';
    return out;
}
// OkayUI's wrapped text: spaces kept while the line fits.
std::vector<std::string> Keep(const std::string& s, float maxW) {
    textwrap::Options o;
    o.collapseSpaces = false;
    std::vector<std::string> lines;
    textwrap::Greedy(s, maxW, Width, lines, o);
    return lines;
}
} // namespace

int main() {
    RUN_SUITE("ui_shared_code");

    // --- Word wrap, the Game UI way (TextRenderer) ---
    CHECK(ByChars("the quick brown fox", 7) == "the\nquick\nbrown\nfox");
    CHECK(ByChars("a  b   c", 7) == "a b c");
    CHECK(ByChars("one\n\ntwo three", 7) == "one\n\ntwo\nthree");
    CHECK(ByChars(" lead and trail ", 7) == "lead\nand\ntrail");
    CHECK(ByChars("tab\tsep word", 7) == "tab sep\nword");
    CHECK(ByChars("mmmmmmmm a", 7) == "mmmmmmmm\na");   // a word longer than the line stays whole
    CHECK(ByChars("x\n", 7) == "x\n");
    CHECK(ByChars("", 7).empty());

    // --- Word wrap, the OkayUI way ---
    CHECK((Keep("the quick brown fox", 12) == std::vector<std::string>{"the", "quick", "brown", "fox"}));
    CHECK((Keep("a  b   c", 12) == std::vector<std::string>{"a  b   c"}));
    CHECK((Keep("one\n\ntwo three", 12) == std::vector<std::string>{"one", "", "two", "three"}));
    CHECK((Keep(" lead and trail ", 12) == std::vector<std::string>{"lead", "and", "trail"}));
    CHECK((Keep("tab\tsep word", 12) == std::vector<std::string>{"tab\tsep", "word"}));
    CHECK((Keep("x\n", 12) == std::vector<std::string>{"x"}));

    // --- Tooltip placement: Game UI's defaults and OkayUI's style ---
    {
        const TooltipBox g = PlaceTooltip(100, 100, 50, 20, 0, 0, 800, 600);
        CHECK(g.x == 114 && g.y == 114 && g.w == 50 && g.h == 20);
        const TooltipBox flip = PlaceTooltip(780, 590, 50, 20, 0, 0, 800, 600);
        CHECK(flip.x == 780 - 50 - 8 && flip.y == 590 - 20 - 8);
        TooltipPlaceStyle ui;
        ui.offset = 14; ui.flipGap = 10; ui.pad = 2; ui.shrink = false;
        const TooltipBox o = PlaceTooltip(780, 590, 50, 20, 0, 0, 800, 600, ui);
        CHECK(o.x == 780 - 10 - 50 && o.y == 590 - 10 - 20);
        const TooltipBox wide = PlaceTooltip(10, 10, 2000, 20, 0, 0, 800, 600, ui);   // OkayUI keeps a box's size
        CHECK(wide.w == 2000 && wide.x == 2);
        const TooltipBox cut = PlaceTooltip(10, 10, 2000, 20, 0, 0, 800, 600);         // Game UI cuts it to the screen
        CHECK(cut.w == 792 && cut.x == 4);
    }

    // --- HSV: one conversion for the Game UI colour picker and OkayUI's ColorConvert* ---
    {
        const Hsv red = ColorSpace::ToHsv(Color(1, 0, 0));
        CHECK_NEAR(red.h, 0.0, 1e-4); CHECK_NEAR(red.s, 1.0, 1e-4); CHECK_NEAR(red.v, 1.0, 1e-4);
        CHECK_NEAR(ColorSpace::ToHsv(Color(0, 0, 1)).h, 240.0, 1e-3);
        const Color c = ColorSpace::FromHsv(480.0f, 1.0f, 1.0f);   // 480 degrees is 120: green
        CHECK_NEAR(c.r, 0.0, 1e-4); CHECK_NEAR(c.g, 1.0, 1e-4); CHECK_NEAR(c.b, 0.0, 1e-4);
        CHECK(ColorSpace::ToHex(Color(0, 1, 0, 0.5f), true) == "#00FF0080");
    }

    TEST_MAIN_RESULT();
}
