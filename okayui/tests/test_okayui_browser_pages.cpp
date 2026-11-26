// Browser: the page corpus (tests/data/browser_pages/: twelve pages written for these tests, shaped like common sites - a news
// article, a blog with a sidebar, a docs page, a product grid, a sign-in form, a pricing page, a forum thread, a landing page,
// settings, a data table, a recipe, search results) drawn through the real browser view (okayui/screens/BrowserView) with the
// software rasterizer and compared with a golden picture each (okayui/tests/golden/page_<name>.png). A golden that changes is
// looked at before it is accepted (OKAYUI_UPDATE_GOLDEN=1 rewrites them).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "BrowserView.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace S = okayuiscreens;
namespace br = okay::browser;

static const char* const kPages[] = {"news_article", "blog_sidebar", "docs_code", "product_grid", "sign_in", "pricing", "forum_thread",
                                     "landing_hero", "settings", "data_table", "recipe_list", "search_results"};

int main() {
    RUN_SUITE("okayui browser page corpus");
    Reset();
    UI::UseLauncherLook(true);
    UI::Style().caretBlink = false;
    for (const char* name : kPages) {
        std::ifstream f(std::string(OKAY_BROWSER_PAGES_DIR) + "/" + name + ".html", std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        CHECK(!ss.str().empty());
        br::BrowserTab tab;
        CHECK(tab.Open("https://pages.test/" + std::string(name) + ".html"));
        tab.TakeFetches();
        Sim sim(640, 800);
        auto ui = [&] { S::DrawBrowserView(tab, 0, 0, 640, 800); };
        sim.Frames(2, ui);
        tab.DeliverPage("https://pages.test/" + std::string(name) + ".html", true, 200, ss.str());
        CHECK(tab.GetState() == br::BrowserTab::State::Ready);
        sim.Frames(3, ui);
        std::string why;
        const bool same = MatchesGolden(std::string("page_") + name, sim.Draw(28, 30, 36), &why);
        if (!same) std::cerr << "  golden page_" << name << ": " << why << "\n";
        CHECK(same);
    }
    TEST_MAIN_RESULT();
}
