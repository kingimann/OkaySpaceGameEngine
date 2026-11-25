// Browser: the OkayUI view of a tab (okayui/screens/BrowserView.hpp), driven headless like the other screens: a page is
// drawn and compared with a golden image (headings, a paragraph with bold and a link, both kinds of list, a rule, a table,
// a bordered box, the placeholder of an embedded game), a link is hovered and clicked, the toolbar buttons report, and the
// loading and error states draw.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "BrowserView.hpp"

#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace S = okayuiscreens;
namespace br = okay::browser;

static const char* kPage =
    "<!DOCTYPE html><html><head><title>Patch notes</title><style>"
    "body { margin: 12px; background: #f4f6fa; color: #1c2230 }"
    ".note { border: 2px solid #3a7bd5; padding: 6px; background: #ffffff }"
    "h1 { color: #3a7bd5 }"
    "</style></head><body>"
    "<h1>Patch notes</h1>"
    "<p>This page is drawn by <b>our own</b> page engine. <a href='/next.html'>Read more</a> about it.</p>"
    "<ul><li>Lists with markers</li><li>Nested<ul><li>circle</li></ul></li></ul>"
    "<ol start=3><li>three</li><li>four</li></ol>"
    "<hr>"
    "<div class=note>A box with a border.</div>"
    "<table><tr><td>cell 1</td><td>cell 2</td></tr></table>"
    "<okay-game src='game.okaypack' width=120 height=40></okay-game>"
    "</body></html>";

int main() {
    RUN_SUITE("okayui browser view");
    Reset();
    UI::UseLauncherLook(true);
    UI::Style().caretBlink = false;

    br::BrowserTab tab;
    CHECK(tab.Open("https://pages.test/notes.html"));
    CHECK(tab.TakeFetches().size() == 1);
    Sim sim(480, 760);
    S::BrowserViewResult res;
    bool sawBack = false, sawClick = false;
    auto ui = [&] {
        res = S::DrawBrowserView(tab, 0, 0, 480, 760);
        if (res.back) sawBack = true;
        if (res.linkClicked == 0) sawClick = true;
    };

    // Loading, then the page.
    sim.Frames(2, ui);
    CHECK(res.linkClicked < 0 && res.hoverHref.empty());
    tab.DeliverPage("https://pages.test/notes.html", true, 200, kPage);
    CHECK(tab.GetState() == br::BrowserTab::State::Ready && tab.Title() == "Patch notes");
    sim.Frames(3, ui);
    {
        std::string why;
        const bool same = MatchesGolden("browser_page", sim.Draw(28, 30, 36), &why);
        if (!same) std::cerr << "  golden browser_page: " << why << "\n";
        CHECK(same);
    }

    // The link: hover shows its address, a click reports it.
    const br::LayoutResult& page = tab.Layout(res.page.w, S::OkayUIMeasure());
    CHECK(page.links.size() == 1 && !page.areas.empty());
    if (!page.areas.empty()) {
        const br::LinkArea& a = page.areas[0];
        const float lx = res.page.x + a.x + a.w * 0.5f, ly = res.pageTop + a.y + a.h * 0.5f;
        sim.Move(lx, ly);
        sim.Frame(ui);
        CHECK(res.hoverHref == "/next.html");
        sim.Down();
        sim.Frame(ui);
        sim.Up();
        sim.Frame(ui);
        CHECK(sawClick);
        const br::BrowserTab::Action act = tab.Activate(page.links[0]);
        CHECK(act.kind == br::BrowserTab::Action::Navigated && act.url == "https://pages.test/next.html");
    }
    // Loading the next page; then an error.
    sim.Move(5, 400);
    sim.Frames(2, ui);
    CHECK(tab.GetState() == br::BrowserTab::State::Loading);
    tab.TakeFetches();
    tab.DeliverPage("https://pages.test/next.html", false, 404, "");
    sim.Frames(2, ui);
    CHECK(tab.GetState() == br::BrowserTab::State::Error);

    // Back is enabled now and reports a click.
    CHECK(!sawBack);
    for (float x = 4; x < 120 && !sawBack; x += 6) sim.Click(x, 12, ui);
    CHECK(sawBack);

    UI::UseLauncherLook(false);
    TEST_MAIN_RESULT();
}
