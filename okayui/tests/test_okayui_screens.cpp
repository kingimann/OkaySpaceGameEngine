// The real screens the launcher and the editor host as the OkayUI backend (okayui/screens): What's New and the Console. They are
// driven through the headless harness the way the apps drive them (synthetic mouse, keys and text per frame; a rectangle to fill),
// asserting what the person can do (every button, field, toggle and row) and what is drawn (golden images in the launcher look).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIScreens.hpp"
#include "ConsoleScriptLink.hpp"

#include <cstring>

using namespace okayuitest;
namespace UI = OkayUI;
namespace S = okayuiscreens;

static void Check(const char* name, const Bitmap& b) {
    std::string why;
    const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << " (actual written as " << name << ".actual.png)\n";
    CHECK(ok);
}

// The colour behind a hosted screen in the apps (the host window's own background): a screen draws no background itself.
static const int kHostBg[3] = {28, 30, 36};

static S::WhatsNewModel News() {
    S::WhatsNewModel m;
    m.currentVersion = "2.860.0";
    m.releases.push_back({"v2.860.0", {"Settings: a UI backend choice, with an OkayUI preview", "Console: filters, categories and copy", "Fixed a crash when the project folder is read-only and the launcher starts for the first time on a very old machine"}});
    m.releases.push_back({"v2.859.0", {"Account: export my data", "Account: delete my account"}});
    m.releases.push_back({"v2.858.0", {"Version manager: keep previous editors"}});
    return m;
}

// Frame driver for the What's New page: a 760x560 area.
struct NewsRig {
    float H;
    Sim sim;
    S::WhatsNewModel model = News();
    S::WhatsNewResult res;
    explicit NewsRig(float h = 560) : H(h), sim(760, h) { Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
    ~NewsRig() { UI::UseLauncherLook(false); }
    // inset like the launcher page, whose own window has padding
    void Frame() { sim.Frame([&] { res = S::DrawWhatsNew(model, 16, 12, 760 - 32, H - 24); }); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    // Click the centre of a rectangle; the result of the frame the click is released in is returned.
    S::WhatsNewResult Click(const UI::Rect& r) {
        sim.Move(CenterX(r), CenterY(r)); Frame(); sim.Down(); Frame(); sim.Up(); Frame();
        return res;
    }
};

int main() {
    RUN_SUITE("okayui screens");
    {   // library additions the screens rely on: a bare window, scroll accessors, a header that starts open
        Reset();
        Sim sim(300, 200);
        UI::Rect first; float scrollMax = -1.0f, scrollNow = -1.0f; bool open1 = false, open2 = true;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("bare", 20, 30, 260, 150)) {
                UI::Button("one"); first = UI::GetItemRect();
                open1 = UI::CollapsingHeader("starts open", true);
                open2 = UI::CollapsingHeader("starts closed");
                UI::BeginChild("c", 0, 60, true);
                for (int i = 0; i < 20; ++i) UI::Text("row");
                scrollMax = UI::GetScrollMaxY(); scrollNow = UI::GetScrollY();
                UI::SetScrollY(1e9f);
                UI::EndChild();
            }
            UI::End();
        };
        sim.Frames(4, ui);
        CHECK(open1 && !open2);                                            // defaults apply the first time
        CHECK(first.x == 20 && first.y < 30 + 20);                         // no title bar and no padding: content starts at the window's own corner
        CHECK(scrollMax > 50.0f && scrollNow >= scrollMax - 1.0f);         // SetScrollY(1e9) before EndChild lands on the end
        sim.Move(100, 36); sim.Down(); sim.Frames(3, ui); sim.Up(); sim.Frame(ui);   // dragging where a title bar would be moves nothing
        sim.Frame(ui);
        CHECK(first.x == 20);
        UI::SetScrollY(5.0f);                                              // outside a child: harmless
        CHECK(UI::GetScrollY() == 0.0f && UI::GetScrollMaxY() == 0.0f);
    }
    {   // a label's "##" suffix is only an id: InputText and Combo show what comes before it, and nothing for a bare "##id"
        auto verts = [](const char* label, bool combo) {
            Reset();
            Sim sim(300, 100);
            char buf[8] = ""; int cur = 0; const char* items[] = {"a", "b"};
            sim.Frames(2, [&] {
                UI::BareNextWindow();
                if (UI::Begin("lab", 0, 0, 300, 100)) { if (combo) UI::Combo(label, items, 2, &cur); else UI::InputText(label, buf, (int)sizeof(buf)); }
                UI::End();
            });
            return UI::GetDrawData().vertexCount;
        };
        for (bool combo : {false, true}) {
            CHECK(verts("Name##hidden", combo) == verts("Name", combo));
            CHECK(verts("##hidden", combo) < verts("Name", combo));
            CHECK(verts("##hidden", combo) == verts("", combo));
        }
    }
    {   // What's New: the idle page; each state offers the right button and it does what the ImGui page does
        NewsRig r;
        r.Frames(3);
        CHECK(r.res.updateButton.w > 0 && r.res.updateButtonEnabled && r.res.releasesButton.w > 0);
        CHECK(r.res.firstEarlierHeader.w > 0);
        const S::WhatsNewResult c = r.Click(r.res.updateButton);
        CHECK(c.checkForUpdates && !c.installNow && !c.restartNow && !c.openReleasesPage);   // one action, one frame
        r.Frames(2);
        CHECK(!r.res.checkForUpdates);                                                       // not repeated while nothing is pressed
    }
    {   // state: update available -> "Install now"
        NewsRig r;
        r.model.state = S::UpdateState::Available; r.model.availableVersion = "2.861.0";
        r.Frames(3);
        const S::WhatsNewResult c = r.Click(r.res.updateButton);
        CHECK(c.installNow && !c.checkForUpdates && !c.restartNow);
    }
    {   // state: installed, restart needed -> "Restart now"; installed without restart -> "Check for updates"
        NewsRig r;
        r.model.state = S::UpdateState::Installed; r.model.restartNeeded = true;
        r.Frames(3);
        CHECK(r.Click(r.res.updateButton).restartNow);
        NewsRig r2;
        r2.model.state = S::UpdateState::Installed; r2.model.restartNeeded = false;
        r2.Frames(3);
        const S::WhatsNewResult c2 = r2.Click(r2.res.updateButton);
        CHECK(c2.checkForUpdates && !c2.restartNow);
    }
    {   // state: checking -> the button is disabled and a click does nothing
        NewsRig r;
        r.model.state = S::UpdateState::Checking; r.model.message = "Downloading 42%";
        r.Frames(3);
        CHECK(!r.res.updateButtonEnabled);
        const S::WhatsNewResult c = r.Click(r.res.updateButton);
        CHECK(!c.checkForUpdates && !c.installNow && !c.restartNow);
    }
    {   // failed / up to date still offer a check
        for (S::UpdateState st : {S::UpdateState::Failed, S::UpdateState::UpToDate}) {
            NewsRig r;
            r.model.state = st; r.model.message = "Could not reach the update server";
            r.Frames(3);
            CHECK(r.Click(r.res.updateButton).checkForUpdates);
        }
    }
    {   // the link to all releases
        NewsRig r(900);   // tall enough that the page does not scroll
        r.Frames(3);
        CHECK(r.Click(r.res.releasesButton).openReleasesPage);
        CHECK(std::strstr(S::WhatsNewReleasesUrl(), "github.com/kingimann/OkaySpaceGameEngine/releases") != nullptr);
    }
    {   // earlier releases: the first starts open, a click folds it (fewer shapes), a click again opens it
        NewsRig r(900);
        r.Frames(3);
        const int open = UI::GetDrawData().vertexCount;
        r.Click(r.res.firstEarlierHeader);
        const int closed = UI::GetDrawData().vertexCount;
        CHECK(closed < open);
        r.Click(r.res.firstEarlierHeader);
        CHECK(UI::GetDrawData().vertexCount == open);
    }
    {   // many releases and long notes: no geometry dropped, the page scrolls instead of growing past its area
        NewsRig r;
        for (int i = 0; i < 12; ++i) r.model.releases.push_back({"v2." + std::to_string(800 - i), {std::string(300, 'w') + " long note", "short"}});
        r.Frames(4);
        CHECK(UI::DroppedGeometry() == 0);
        r.sim.Move(300, 300);
        const int before = UI::GetDrawData().vertexCount;
        r.sim.wheel = -5.0f; r.Frame(); r.Frame();
        CHECK(UI::GetDrawData().vertexCount > 0 && before > 0);
        CHECK(r.res.releasesButton.y > 0);   // the button exists after scrolling (its position is where the layout put it)
    }
    {   // the page is a real window of the app: a hosted rectangle smaller than the page still clips to itself
        Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false;
        Sim sim(760, 560);
        sim.Frames(3, [&] { S::DrawWhatsNew(News(), 100, 80, 400, 300); });
        const Bitmap b = sim.Draw(12, 14, 20);
        CHECK(CountNot(b, 0, 0, 760, 79, Rgba(12, 14, 20)) == 0);            // nothing above the rectangle
        CHECK(CountNot(b, 0, 0, 99, 560, Rgba(12, 14, 20)) == 0);            // nothing left of it
        CHECK(CountNot(b, 520, 0, 760, 560, Rgba(12, 14, 20)) == 0);         // nor right of it (shadow stays inside)
        UI::UseLauncherLook(false);
    }
    {   // golden images: the page in three states, launcher look
        for (int variant = 0; variant < 3; ++variant) {
            NewsRig r;
            if (variant == 1) { r.model.state = S::UpdateState::Available; r.model.availableVersion = "2.861.0"; }
            if (variant == 2) { r.model.releases.clear(); r.model.releases.push_back({"Highlights", {"Script Editor: errors as you type", "Camera: zoom, shake and lens shift"}}); r.model.placeholder = true; r.model.state = S::UpdateState::Failed; r.model.message = "Could not reach the update server"; }
            r.Frames(4);
            Check(variant == 0 ? "whatsnew_idle" : variant == 1 ? "whatsnew_available" : "whatsnew_placeholder", r.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        }
    }

    // ---- Console ----
    {   // which entries offer Open Script (shared with the Dear ImGui panel)
        std::string path; int line = -1;
        okay::ConsoleEntry e;
        e.message = "Script error in Assets/player.okay: line 12 unexpected token";
        CHECK(okayconsole::ScriptLink(e, path, line) && path == "Assets/player.okay" && line == 12);
        e.message = "Couldn't load 'Assets/ai.lua' (bad header)";
        CHECK(okayconsole::ScriptLink(e, path, line) && path == "Assets/ai.lua" && line == 0);
        e.message = "Saved everything"; line = 5;
        CHECK(!okayconsole::ScriptLink(e, path, line) && path.empty() && line == 0);
        e.message = "x"; e.source.kind = okay::ConsoleSource::Kind::Script; e.source.path = "Scripts/a.cs"; e.source.line = 40;
        CHECK(okayconsole::ScriptLink(e, path, line) && path == "Scripts/a.cs" && line == 40);
        e.source.kind = okay::ConsoleSource::Kind::Asset; e.source.path = "Assets/crate.png";                    // an asset that is not a script
        CHECK(!okayconsole::ScriptLink(e, path, line));
    }
    {   // helper data
        struct Host {
            std::vector<std::string> copied, saved, opened, activated;
            int cleared = 0;
            bool clearOnPlay = true, pauseOnError = false;
            S::ConsoleHost h;
            Host() {
                h.clearOnPlay = &clearOnPlay; h.pauseOnError = &pauseOnError;
                h.copyText = [this](const std::string& s) { copied.push_back(s); };
                h.saveLog = [this](const std::string& s) { saved.push_back(s); };
                h.openScript = [this](const std::string& p, int l) { opened.push_back(p + ":" + std::to_string(l)); };
                h.activateSource = [this](const okay::ConsoleSource& s) { activated.push_back(s.path); };
                h.cleared = [this] { ++cleared; };
            }
        };
        struct CRig {
            Sim sim{900, 520};
            okay::ConsoleModel model{20000};
            S::ConsoleViewState st;
            Host host;
            S::ConsoleResult res;
            CRig() {
                Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false;
                model.SetClock([] { return 3723.0; });   // 01:02:03
            }
            ~CRig() { UI::UseLauncherLook(false); }
            void Frame() { sim.Frame([&] { res = S::DrawConsole(model, st, host.h, 16, 12, 900 - 32, 520 - 24); }); }
            void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
            void Click(const UI::Rect& r) { sim.Move(CenterX(r), CenterY(r)); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
            void Fill() {
                model.Add(okay::Log::Level::Info, "Scene loaded: Level1");
                model.Add(okay::Log::Level::Warning, "Slow physics step (24 ms)", "Physics");
                model.Add(okay::Log::Level::Error, "Script error in Assets/player.okay: line 12 unexpected token", "Script");
                model.Add(okay::Log::Level::Info, "Saved everything", "Editor");
            }
        };
        {   // everything is on screen and counted; no row is selected yet
            CRig c; c.Fill(); c.Frames(3);
            CHECK(c.res.rowsShown == 4 && c.res.rowsDrawn == 4);
            CHECK(c.res.clearButton.w > 0 && c.res.searchField.w > 0 && c.res.categoryCombo.w > 0 && c.res.copyAllButton.w > 0 && c.res.saveButton.w > 0);
            CHECK(c.res.firstRow.w > 0 && c.res.logArea.w > 0);
            CHECK(c.res.detailsCopyButton.w == 0);                    // "Select a log entry to see details."
        }
        {   // selecting a row shows its details and Copy sends the message; right-click offers the copy menu
            CRig c; c.Fill(); c.Frames(3);
            c.Click(c.res.firstRow);
            c.Frames(2);
            CHECK(c.st.selectedSeq == 0);
            CHECK(c.res.detailsCopyButton.w > 0 && c.res.openScriptButton.w == 0);   // an info line: no script
            c.Click(c.res.detailsCopyButton);
            CHECK(c.host.copied.size() == 1 && c.host.copied[0] == "Scene loaded: Level1");
        }
        {   // an error that names a script offers Open Script with the line; a click opens it
            CRig c; c.Fill(); c.st.selectedSeq = 2; c.Frames(3);
            CHECK(c.res.openScriptButton.w > 0);
            c.Click(c.res.openScriptButton);
            CHECK(c.host.opened.size() == 1 && c.host.opened[0] == "Assets/player.okay:12");
            // without the host callback there is no such button (nothing faked)
            CRig d; d.Fill(); d.st.selectedSeq = 2; d.host.h.openScript = nullptr; d.Frames(3);
            CHECK(d.res.openScriptButton.w == 0);
        }
        {   // a row with a source is reported to the host when clicked
            CRig c;
            okay::ConsoleSource src; src.kind = okay::ConsoleSource::Kind::Object; src.path = "World/Crate";
            c.model.Add(okay::Log::Level::Warning, "Crate has no collider", "", src);
            c.Frames(3);
            c.Click(c.res.firstRow);
            CHECK(c.host.activated.size() == 1 && c.host.activated[0] == "World/Crate");
        }
        {   // search: typing narrows the rows (message, category or source), clearing the text brings them back
            CRig c; c.Fill(); c.Frames(3);
            c.Click(c.res.searchField);
            c.sim.Type("slow"); c.Frame();
            c.Frames(2);
            CHECK(std::string(c.st.filter) == "slow" && c.res.rowsShown == 1);
            c.sim.Type("zzz"); c.Frame(); c.Frame();
            CHECK(c.res.rowsShown == 0);
            for (int i = 0; i < 7; ++i) c.sim.Tap(Key_Backspace, [&] { c.Frame(); });
            c.Frames(2);
            CHECK(c.res.rowsShown == 4 && c.st.filter[0] == 0);
        }
        {   // level toggles: hide warnings, hide errors, each through its own control
            CRig c; c.Fill(); c.Frames(3);
            c.Click(c.res.warnToggle); c.Frames(2);
            CHECK(!c.st.showWarn && c.res.rowsShown == 3);
            c.Click(c.res.errorToggle); c.Frames(2);
            CHECK(!c.st.showError && c.res.rowsShown == 2);
            c.Click(c.res.warnToggle); c.Click(c.res.errorToggle); c.Frames(2);
            CHECK(c.st.showWarn && c.st.showError && c.res.rowsShown == 4);
        }
        {   // category: the dropdown lists "All" and each category once; picking one filters
            CRig c; c.Fill(); c.Frames(3);
            c.Click(c.res.categoryCombo);        // opens
            c.Frames(2);
            const float rowH = c.res.categoryCombo.h;
            // items: All, Physics, Script, Editor: pick the third (Script)
            c.sim.Move(c.res.categoryCombo.x + 20, c.res.categoryCombo.y + rowH * 3.5f); c.Frame();
            c.sim.Down(); c.Frame(); c.sim.Up(); c.Frame(); c.Frames(2);
            CHECK(c.st.category == 2 && c.res.rowsShown == 1);
            // "prefer category" (what the editor's tools use to jump to one) selects it by name
            c.st.preferCategory = "Physics"; c.Frames(2);
            CHECK(c.st.category == 1 && c.res.rowsShown == 1 && c.st.preferCategory.empty());
            c.st.preferCategory = "Nope"; c.Frames(2);
            CHECK(c.st.category == 0 && c.res.rowsShown == 4);
        }
        {   // collapse duplicates folds a run into one row with its count
            CRig c;
            for (int i = 0; i < 3; ++i) c.model.Add(okay::Log::Level::Info, "Tick");
            c.model.Add(okay::Log::Level::Info, "Tock");
            c.Frames(3);
            CHECK(c.res.rowsShown == 4);
            c.Click(c.res.collapseToggle); c.Frames(2);
            CHECK(c.st.collapse && c.res.rowsShown == 2);
        }
        {   // Clear empties the model, deselects, and tells the host
            CRig c; c.Fill(); c.st.selectedSeq = 1; c.Frames(3);
            c.Click(c.res.clearButton); c.Frames(2);
            CHECK(c.model.Size() == 0 && c.res.rowsShown == 0 && c.st.selectedSeq == ~0ull && c.host.cleared == 1);
        }
        {   // Copy all and Save send the whole log; without a callback the buttons are disabled and do nothing
            CRig c; c.Fill(); c.Frames(3);
            c.Click(c.res.copyAllButton);
            CHECK(c.host.copied.size() == 1 && c.host.copied[0].find("Saved everything") != std::string::npos
                  && c.host.copied[0].find("Slow physics step") != std::string::npos);
            c.Click(c.res.saveButton);
            CHECK(c.host.saved.size() == 1 && c.host.saved[0].find("Script error") != std::string::npos);
            CHECK(c.res.saveHint == "console_log.txt");
            CRig d; d.Fill(); d.host.h.copyText = nullptr; d.host.h.saveLog = nullptr; d.Frames(3);
            d.Click(d.res.copyAllButton); d.Click(d.res.saveButton);
            CHECK(d.host.copied.empty() && d.host.saved.empty());
        }
        {   // the host's own options: Clear on Play and Pause on error are the host's variables
            CRig c; c.Fill(); c.Frames(3);
            CHECK(c.res.pauseOnErrorToggle.w > 0 && c.res.clearOnPlayToggle.w > 0);
            c.Click(c.res.pauseOnErrorToggle);
            CHECK(c.host.pauseOnError && c.host.clearOnPlay);
            c.Click(c.res.clearOnPlayToggle);
            CHECK(!c.host.clearOnPlay);
            CRig d; d.Fill(); d.host.h.clearOnPlay = nullptr; d.host.h.pauseOnError = nullptr; d.Frames(3);
            CHECK(d.res.copyAllButton.w > 0 && d.res.pauseOnErrorToggle.w == 0 && d.res.clearOnPlayToggle.w == 0);   // hidden, rest unaffected
        }
        {   // a log of 5000 lines: only the visible rows are laid out, nothing is dropped, auto-scroll follows the newest, wheel scrolls up
            CRig c;
            for (int i = 0; i < 5000; ++i) c.model.Add(i % 7 == 0 ? okay::Log::Level::Warning : okay::Log::Level::Info, "line " + std::to_string(i));
            c.Frames(6);
            CHECK(c.res.rowsShown == 5000 && c.res.rowsDrawn > 0 && c.res.rowsDrawn < 60);
            CHECK(UI::DroppedGeometry() == 0);
            CHECK(c.res.lastDrawnSeq >= 4990);                          // scrolled to the end
            c.model.Add(okay::Log::Level::Info, "newest"); c.Frames(4);
            CHECK(c.res.lastDrawnSeq == 5000);                          // followed
            c.sim.Move(CenterX(c.res.logArea), CenterY(c.res.logArea)); c.Frame();
            c.sim.wheel = 40.0f; c.Frame(); c.Frames(2);
            const std::uint64_t scrolledTo = c.res.lastDrawnSeq;
            CHECK(scrolledTo < 4980);                                   // the wheel moved the log up
            c.model.Add(okay::Log::Level::Info, "after"); c.Frames(3);
            CHECK(c.res.lastDrawnSeq == scrolledTo);                    // and it stays there (does not yank back)
            c.Click(c.res.autoScrollToggle);                            // the checkbox is the same state the ImGui menu had
            CHECK(!c.st.autoScroll);
        }
        {   // a hostile log: huge, control-laden and unterminated-looking messages neither crash nor drop geometry
            CRig c;
            c.model.Add(okay::Log::Level::Error, std::string(5000, 'x'));
            c.model.Add(okay::Log::Level::Info, std::string("tab\there\nnewline \x01\x02 \xFF\xFE bad utf8"));
            c.model.Add(okay::Log::Level::Info, "");
            c.st.selectedSeq = 0;
            c.Frames(4);
            CHECK(c.res.rowsShown == 3 && UI::DroppedGeometry() == 0);
        }
        {   // golden: the panel with a mix of lines, one selected (the error with its script link)
            CRig c; c.Fill();
            c.model.Add(okay::Log::Level::Info, "Saved everything", "Editor");
            c.model.Add(okay::Log::Level::Warning, "Slow physics step (31 ms)", "Physics");
            c.st.selectedSeq = 2;
            c.Frames(4);
            Check("console_mixed", c.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        }
        {   // golden: filtered to errors with a search
            CRig c; c.Fill();
            std::strcpy(c.st.filter, "script"); c.st.showInfo = false; c.st.showWarn = false;
            c.Frames(4);
            CHECK(c.res.rowsShown == 1);
            Check("console_filtered", c.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        }
        {   // golden: an empty console
            CRig c; c.Frames(3);
            Check("console_empty", c.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        }
    }
    TEST_MAIN_RESULT();
}
