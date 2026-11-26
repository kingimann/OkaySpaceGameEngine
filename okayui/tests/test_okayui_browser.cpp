// Browser: the OkayUI view of a tab (okayui/screens/BrowserView.hpp), driven headless like the other screens: a page is
// drawn and compared with a golden image (headings, a paragraph with bold and a link, both kinds of list, a rule, a table,
// a bordered box, the placeholder of an embedded game), a link is hovered and clicked, the toolbar buttons report, and the
// loading and error states draw. Step 2: the browser program's toolbar (an address field, Go, no "Open in browser" or
// "Close"), and an embedded game's box with the host's Play button and state line (golden), a click on Play, and a box the
// host says cannot play (no button). Round 1 of the browser program: a form's field clicked and typed into through the view, a
// checkbox ticked, and Enter sending the form (the POST body checked).
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

    // ---- step 2: the browser program's view ----
    {
        br::BrowserTab tab2;
        CHECK(tab2.Open("https://games.test/index.html"));
        tab2.TakeFetches();
        tab2.DeliverPage("https://games.test/index.html", true, 200,
            "<html><head><title>Games</title><style>body { margin: 10px; background: #eef1f6 }</style></head><body>"
            "<h2>Space Run</h2><okay-game src='space.okaypack.gz' width=300 height=120></okay-game>"
            "<p>A video: <video src='a.webm' width=120 height=40></video></p></body></html>");
        S::AddressField field;
        S::BrowserViewOptions opt;
        opt.address = &field;
        opt.hostButtons = false;
        int asked = 0;
        bool running = false;
        opt.embedState = [&](const br::DrawCmd& c) {
            ++asked;
            S::EmbedState st;
            if (c.tag == "okay-game") { st.playable = true; st.busy = running; st.title = "Space Run"; st.status = running ? "Running in its own window" : "Runs natively with the OkaySpace player"; }
            return st;
        };
        Sim sim2(480, 320);
        S::BrowserViewResult r2;
        int plays = -1;
        std::string navigated;
        auto ui2 = [&] {
            r2 = S::DrawBrowserView(tab2, 0, 0, 480, 320, S::BrowserImageFn(), opt);
            if (r2.playEmbed >= 0) plays = r2.playEmbed;
            if (!r2.navigate.empty()) navigated = r2.navigate;
            CHECK(!r2.openInSystemBrowser && !r2.close);
        };
        sim2.Frames(3, ui2);
        CHECK(std::string(field.text) == "https://games.test/index.html");   // the field shows the tab's address
        CHECK(asked > 0);
        {
            std::string why;
            const bool same = MatchesGolden("browser_game", sim2.Draw(28, 30, 36), &why);
            if (!same) std::cerr << "  golden browser_game: " << why << "\n";
            CHECK(same);
        }
        // Play: the button is in the middle of the game's box.
        const br::LayoutResult& pg = tab2.Layout(r2.page.w, S::OkayUIMeasure());
        int gameCmd = -1, videoCmd = -1;
        for (int ei : pg.embeds) {
            if (pg.cmds[(std::size_t)ei].tag == "okay-game") gameCmd = ei;
            if (pg.cmds[(std::size_t)ei].tag == "video") videoCmd = ei;
        }
        CHECK(gameCmd >= 0 && videoCmd >= 0);
        if (gameCmd >= 0 && videoCmd >= 0) {
            const UI::Rect b = S::EmbedPlayButton(pg.cmds[(std::size_t)gameCmd]);
            sim2.Click(r2.page.x + b.x + b.w * 0.5f, r2.pageTop + b.y + b.h * 0.5f, ui2);
            CHECK(plays == gameCmd);
            // Busy: a click does nothing.
            plays = -1;
            running = true;
            sim2.Click(r2.page.x + b.x + b.w * 0.5f, r2.pageTop + b.y + b.h * 0.5f, ui2);
            CHECK(plays == -1);
            // The video's box has no button: a click there plays nothing.
            const br::DrawCmd& v = pg.cmds[(std::size_t)videoCmd];
            sim2.Click(r2.page.x + v.x + v.w * 0.5f, r2.pageTop + v.y + v.h * 0.5f, ui2);
            CHECK(plays == -1);
        }
        // The address field: type an address and press Enter.
        sim2.Click(330, 15, ui2);   // inside the address field
        sim2.Hold(UI::KeyId_Ctrl, false);
        sim2.Press(Key_SelectAll);
        sim2.Frame(ui2);
        sim2.Type("https://other.test/x.html");
        sim2.Frame(ui2);
        sim2.Press(Key_Enter);
        sim2.Frame(ui2);
        CHECK(navigated == "https://other.test/x.html");
    }

    // ---- round 1: a form in the browser program's view: click a field, type, tick a box, Enter sends it (POST) ----
    {
        br::BrowserTab tab3;
        tab3.SetAnyOrigin(true);
        CHECK(tab3.Open("https://forms.test/login.html"));
        tab3.TakeFetches();
        tab3.DeliverPage("https://forms.test/login.html", true, 200,
            "<body style='margin:10px'><form method=post action=/in><input id=u name=user style='width:200px'>"
            "<label><input type=checkbox name=keep value=1> keep</label><button>Go</button></form></body>");
        S::AddressField field;
        S::ControlEdit edit;
        S::BrowserViewOptions opt;
        opt.address = &field;
        opt.hostButtons = false;
        opt.programButtons = true;
        opt.edit = &edit;
        Sim sim3(480, 320);
        S::BrowserViewResult r3;
        bool sent = false;
        auto ui3 = [&] { r3 = S::DrawBrowserView(tab3, 0, 0, 480, 320, S::BrowserImageFn(), opt); if (r3.formSent) sent = true; };
        sim3.Frames(3, ui3);
        const br::LayoutResult& pg = tab3.Layout(r3.page.w, S::OkayUIMeasure());
        CHECK(pg.controls.size() == 3);   // the field, the checkbox and the button
        if (pg.controls.size() == 3) {
            const br::ControlBox u = pg.controls[0];   // copies: the layout is redone as the person types
            const br::ControlBox k = pg.controls[1];
            sim3.Click(r3.page.x + u.x + 10, r3.pageTop + u.y + u.h * 0.5f, ui3);
            sim3.Frames(2, ui3);
            CHECK(edit.node == u.node);
            sim3.Type("ann@test");
            sim3.Frames(2, ui3);
            CHECK(tab3.Forms().Value(*u.node) == "ann@test");
            sim3.Click(r3.page.x + k.x + 3, r3.pageTop + k.y + 3, ui3);   // the checkbox
            CHECK(tab3.Forms().Checked(*k.node));
            sim3.Click(r3.page.x + u.x + 10, r3.pageTop + u.y + u.h * 0.5f, ui3);
            sim3.Frames(2, ui3);
            sim3.Press(Key_Enter);   // implicit submission: the form's button
            sim3.Frame(ui3);
            CHECK(sent && tab3.GetState() == br::BrowserTab::State::Loading);
            const std::vector<std::string> f = tab3.TakeFetches();
            std::string body;
            CHECK(f.size() == 1 && f[0] == "https://forms.test/in" && tab3.PostBody(f[0], body) && body == "user=ann%40test&keep=1");
        }
    }

    UI::UseLauncherLook(false);
    TEST_MAIN_RESULT();
}
