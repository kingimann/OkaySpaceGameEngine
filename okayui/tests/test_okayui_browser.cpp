// Browser: the OkayUI view of a tab (okayui/screens/BrowserView.hpp), driven headless like the other screens: a page is
// drawn and compared with a golden image (headings, a paragraph with bold and a link, both kinds of list, a rule, a table,
// a bordered box, the placeholder of an embedded game), a link is hovered and clicked, the toolbar buttons report, and the
// loading and error states draw. Step 2: the browser program's toolbar (an address field, Go, no "Open in browser" or
// "Close"), and an embedded game's box with the host's Play button and state line (golden), a click on Play, and a box the
// host says cannot play (no button). Round 1 of the browser program: a form's field clicked and typed into through the view, a
// checkbox ticked, and Enter sending the form (the POST body checked). Round 2 of the look: text selected by dragging (golden),
// Ctrl+C and Ctrl+A, and a press on a link that follows it and selects nothing.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "BrowserView.hpp"
#include "okay/Graphics/TtfFont.hpp"   // EmbeddedUIFontData: the launcher look's font

#include <cmath>
#include <string>
#include <vector>

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

    // ---- the launcher's Browser page: an "Open in its own window" button just before the menu button (owner, 2026-11-28);
    // the browser program's own toolbar does not have it ----
    for (bool withButton : {false, true}) {
        br::BrowserTab tabW;
        tabW.SetAnyOrigin(true);
        CHECK(tabW.Open("https://own.test/"));
        tabW.TakeFetches();
        tabW.DeliverPage("https://own.test/", true, 200, "<body><p>page</p></body>");
        S::AddressField field;
        S::BrowserViewOptions opt;
        opt.address = &field;
        opt.hostButtons = false;
        opt.programButtons = true;
        opt.ownWindowButton = withButton;
        Sim simW(480, 320);
        S::BrowserViewResult rW;
        bool asked = false, menu = false;
        auto uiW = [&] { rW = S::DrawBrowserView(tabW, 0, 0, 480, 320, S::BrowserImageFn(), opt); asked |= rW.ownWindow; menu |= rW.menu; };
        simW.Frames(3, uiW);
        const OkayUI::Rect mb = rW.menuButton;
        simW.Click(mb.x - 2.0f - mb.w * 0.5f, mb.y + mb.h * 0.5f, uiW);   // where the button goes: just left of the menu
        simW.Frames(2, uiW);
        CHECK(asked == withButton);
        CHECK(!menu);   // the menu button is still its own
    }

    // ---- round 2 of the look: text selection with the mouse in the browser program's view; Ctrl+C copies it, Ctrl+A selects
    // all of the page's text; a press on a link still follows the link and selects nothing ----
    {
        br::BrowserTab tab4;
        tab4.SetAnyOrigin(true);
        CHECK(tab4.Open("https://text.test/"));
        tab4.TakeFetches();
        tab4.DeliverPage("https://text.test/", true, 200,
            "<body style='margin:10px'><p style='margin:0'>Hello brave new world</p><p style='margin:0'>Second line here</p>"
            "<p style='margin:0'><a href='/x'>a link</a></p></body>");
        S::AddressField field;
        S::ControlEdit edit;
        S::PageKeys keys;
        S::BrowserViewOptions opt;
        opt.address = &field;
        opt.hostButtons = false;
        opt.programButtons = true;
        opt.edit = &edit;
        opt.keys = &keys;
        Sim sim4(480, 320);
        S::BrowserViewResult r4;
        int clicked = -1;
        auto ui4 = [&] { r4 = S::DrawBrowserView(tab4, 0, 0, 480, 320, S::BrowserImageFn(), opt); if (r4.linkClicked >= 0) clicked = r4.linkClicked; };
        sim4.Frames(3, ui4);
        const br::LayoutResult& pg = tab4.Layout(r4.page.w, S::OkayUIMeasure());
        const br::DrawCmd* hello = nullptr;
        const br::DrawCmd* second = nullptr;
        for (const br::DrawCmd& c : pg.cmds) {
            if (c.kind == br::DrawCmd::Text && c.text.rfind("Hello", 0) == 0) hello = &c;
            if (c.kind == br::DrawCmd::Text && c.text.rfind("Second", 0) == 0) second = &c;
        }
        CHECK(hello && second);
        if (hello && second) {
            const float hx = hello->x, hy = hello->y + hello->h * 0.5f;
            const float upTo = S::OkayUIMeasure().Width("Hello brave", hello->font);
            const float sx = second->x + S::OkayUIMeasure().Width("Second", second->font);
            const float sy = second->y + second->h * 0.5f;
            // drag from the start of "Hello" to just after "Second"
            sim4.Move(r4.page.x + hx + 1.0f, r4.pageTop + hy); sim4.Frame(ui4);
            sim4.Down(); sim4.Frame(ui4);
            sim4.Move(r4.page.x + hx + upTo, r4.pageTop + hy); sim4.Frame(ui4);
            sim4.Move(r4.page.x + sx + 1.0f, r4.pageTop + sy); sim4.Frame(ui4);
            sim4.Up(); sim4.Frame(ui4);
            CHECK(keys.HasSelection());
            CHECK(S::SelectedPageText(pg, keys) == "Hello brave new world\nSecond");
            UI::SetClipboardText("");
            sim4.ctrl = true; sim4.Hold(UI::KeyId_C, true); sim4.Frame(ui4); sim4.Hold(UI::KeyId_C, false); sim4.Frame(ui4); sim4.ctrl = false;
            CHECK(std::string(UI::GetClipboardText()) == "Hello brave new world\nSecond");
            {
                std::string why;
                const bool same = MatchesGolden("browser_select", sim4.Draw(28, 30, 36), &why);
                if (!same) std::cerr << "  golden browser_select: " << why << "\n";
                CHECK(same);
            }
            // a plain click on the text clears it
            sim4.Click(r4.page.x + hx + 3.0f, r4.pageTop + hy, ui4);
            CHECK(!keys.HasSelection());
            // Ctrl+A: all of it
            sim4.ctrl = true; sim4.Hold(UI::KeyId_A, true); sim4.Frame(ui4); sim4.Hold(UI::KeyId_A, false); sim4.Frame(ui4); sim4.ctrl = false;
            CHECK(S::SelectedPageText(pg, keys) == "Hello brave new world\nSecond line here\na link");
        }
        // a press on the link follows it and starts no selection
        for (const br::LinkArea& a : pg.areas) {
            sim4.Click(r4.page.x + a.x + a.w * 0.5f, r4.pageTop + a.y + a.h * 0.5f, ui4);
            break;
        }
        CHECK(clicked == 0 && !keys.HasSelection());
    }

    // ---- round 4: text is sized by its em (CSS font-size), not its line; the page's cursor property; user-select: none
    {
        std::vector<unsigned char> font;
        S::FontEm em;
        CHECK(okay::EmbeddedUIFontData(font) && S::FontEmOf(font.data(), font.size(), em));
        CHECK(em.linePerEm > 1.1f && em.linePerEm < 1.3f && em.ascentPerEm > 0.85f && em.ascentPerEm < 1.0f);   // Roboto: 2400 / 2048, 1900 / 2048
        S::FontEm none;
        CHECK(!S::FontEmOf(font.data(), 12, none) && none.linePerEm == 1.0f);   // not a font: false, the default kept
        // a 16 px run is as wide as the font's 16 px em makes it: linePerEm times the width at a 16 px line (what was drawn before)
        br::FontSpec fs;
        fs.size = 16.0f;
        const float now = S::OkayUIMeasure().Width("Patch notes", fs);
        const float saved = UI::Style().textScale;
        UI::Style().textScale = 2.0f;   // OkayUI's 8 px cell twice: a 16 px line
        const float lineSized = UI::CalcTextWidth("Patch notes");
        UI::Style().textScale = saved;
        CHECK(lineSized > 0 && std::fabs(now / lineSized - em.linePerEm) < 0.02f);

        br::BrowserTab tab5;
        CHECK(tab5.Open("https://pages.test/ui.html"));
        tab5.TakeFetches();
        tab5.DeliverPage("https://pages.test/ui.html", true, 200,
            "<body style='margin:0'><div id=na style='cursor:not-allowed;height:60px'>no</div><p style='margin:0'><a href='/w' style='cursor:wait'>busy link</a></p>"
            "<p style='margin:0;height:40px'>plain text</p><p style='margin:0'>Keep</p><p style='margin:0;user-select:none'>Skip</p><p style='margin:0'>End</p></body>");
        S::PageKeys keys5;
        S::BrowserViewOptions opt5;
        opt5.keys = &keys5;
        Sim sim5(480, 320);
        S::BrowserViewResult r5;
        auto ui5 = [&] { r5 = S::DrawBrowserView(tab5, 0, 0, 480, 320, S::BrowserImageFn(), opt5); };
        sim5.Frames(3, ui5);
        const br::LayoutResult& p5 = tab5.Layout(r5.page.w, S::OkayUIMeasure());
        auto cursorAt = [&](const char* text) {
            for (const br::DrawCmd& c : p5.cmds)
                if (c.kind == br::DrawCmd::Text && c.text == text) {
                    sim5.Move(r5.page.x + c.x + 2.0f, r5.pageTop + c.y + c.h * 0.5f);
                    sim5.Frame(ui5);
                    return UI::GetMouseCursor();
                }
            return UI::MouseCursor_COUNT;
        };
        CHECK(cursorAt("no") == UI::MouseCursor_NotAllowed);
        CHECK(cursorAt("busy link") == UI::MouseCursor_Wait);
        CHECK(cursorAt("plain text") == UI::MouseCursor_Arrow);
        // Ctrl+A leaves the user-select: none text out
        sim5.ctrl = true; sim5.Hold(UI::KeyId_A, true); sim5.Frame(ui5); sim5.Hold(UI::KeyId_A, false); sim5.Frame(ui5); sim5.ctrl = false;
        CHECK(S::SelectedPageText(p5, keys5) == "no\nbusy link\nplain text\nKeep\nEnd");
    }

    UI::UseLauncherLook(false);
    TEST_MAIN_RESULT();
}
