// The panels ported from Dear ImGui screens (okayui/screens/OkayUIPanels): the launcher's Tips page, the editor's Crash Log and
// Audio Mixer. Driven headless the way the apps drive them: every button, field and toggle is found by its rectangle in the
// result and clicked with synthetic input; golden images (launcher look) pin what is drawn.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIPanels.hpp"

#include <cstring>
#include <cstdio>

using namespace okayuitest;
namespace UI = OkayUI;
namespace S = okayuiscreens;

static void Check(const char* name, const Bitmap& b) {
    std::string why;
    const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << " (actual written as " << name << ".actual.png)\n";
    CHECK(ok);
}
static const int kHostBg[3] = {28, 30, 36};
template <class R> static float CX(const R& r) { return r.x + r.w * 0.5f; }
template <class R> static float CY(const R& r) { return r.y + r.h * 0.5f; }

// ---- Tips ----
static S::TipsModel Tips() {
    S::TipsModel m;
    m.cats = {"All", "Editor", "Scripting", "Building", "Launcher"};
    m.tips = {
        {1, "Test instantly", "Press Play in the editor toolbar to try the scene right away."},
        {1, "Save everything", "Ctrl+S saves the scene, Ctrl+Shift+S saves it under a new name and Ctrl+Alt+S saves the whole project."},
        {2, "Jump between problems", "Errors and warnings appear while you type. Press F2 to jump to the next one."},
        {2, "One-line timers", "if (timer(\"spawn\", 2)) runs every 2 seconds."},
        {3, "Choose the scenes you ship", "File > Build Settings > Scenes In Build: tick the scenes to include."},
        {4, "Collapse the sidebar", "The panel button next to the logo shrinks the sidebar to icons."},
    };
    m.dayNumber = 20000;   // 20000 % 6 == 2
    return m;
}
struct TipsRig {
    Sim sim{800, 600};
    S::TipsModel model = Tips();
    S::TipsState st;
    S::TipsResult res;
    TipsRig() { Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
    ~TipsRig() { UI::UseLauncherLook(false); }
    void Frame() { sim.Frame([&] { res = S::DrawTips(model, st, 16, 12, 800 - 32, 600 - 24); }); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Click(const UI::Rect& r) { sim.Move(CX(r), CY(r)); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
};

// ---- Crash Log ----
struct CrashRig {
    Sim sim{800, 480};
    S::CrashLogModel model;
    S::CrashLogState st;
    S::CrashLogHost host;
    S::CrashLogResult res;
    int refreshed = 0, opened = 0, cleared = 0;
    std::vector<std::string> copied;
    CrashRig() {
        Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false;
        host.refresh = [this] { ++refreshed; };
        host.openFolder = [this] { ++opened; };
        host.copyAll = [this](const std::string& s) { copied.push_back(s); };
        host.clear = [this] { ++cleared; model.text.clear(); ++model.revision; };
        model.path = "C:/OkaySpace/okay_crashlog.txt";
    }
    ~CrashRig() { UI::UseLauncherLook(false); }
    void Frame() { sim.Frame([&] { res = S::DrawCrashLog(model, st, host, 12, 10, 776, 460); }); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Click(const UI::Rect& r) { sim.Move(CX(r), CY(r)); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
};

// ---- Audio Mixer ----
struct MixRig {
    Sim sim{520, 620};
    float master = 0.5f; bool masterMuted = true;
    float gv[3] = {0.2f, 0.4f, 0.6f}; bool gm[3] = {true, false, true};
    bool duck = true; float duckLevel = 0.3f, attack = 0.1f, release = 0.5f;
    S::AudioMixerModel model;
    S::AudioMixerResult res;
    float h = 600;
    MixRig() {
        Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false;
        model.peakLeft = 0.5f; model.peakRight = 1.2f;
        model.masterVolume = &master; model.masterMuted = &masterMuted;
        model.groupNames = {"Music", "Effects", "Voice"};
        for (int i = 0; i < 3; ++i) { model.groupVolume.push_back(&gv[i]); model.groupMuted.push_back(&gm[i]); }
        model.duckMusic = &duck; model.duckLevel = &duckLevel; model.duckAttack = &attack; model.duckRelease = &release; model.duckGain = 0.43f;
    }
    ~MixRig() { UI::UseLauncherLook(false); }
    void Frame() { sim.Frame([&] { res = S::DrawAudioMixer(model, 10, 10, 500, h); }); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Click(const UI::Rect& r) { sim.Move(CX(r), CY(r)); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
};

int main() {
    RUN_SUITE("okayui panels");
    // ================= Tips =================
    {   // filter logic, headless
        const S::TipsModel m = Tips();
        CHECK(S::TipMatches(m.tips[0], 0, ""));
        CHECK(S::TipMatches(m.tips[0], 1, "") && !S::TipMatches(m.tips[0], 2, ""));
        CHECK(S::TipMatches(m.tips[2], 0, "F2") && S::TipMatches(m.tips[2], 0, "JUMP"));   // case-insensitive, title or body
        CHECK(!S::TipMatches(m.tips[2], 1, "F2"));                                           // category and query both apply
        CHECK(!S::TipMatches(m.tips[2], 0, "zzz"));
    }
    {   // the page: all tips, the tip of the day, no overflow of geometry
        TipsRig t; t.Frames(4);
        CHECK(t.res.shown.size() == 6 && t.res.cards.size() == 6 && t.res.columns == 1 && !t.res.noMatch);
        CHECK(t.st.daily == 2);                                     // from dayNumber
        CHECK(t.res.categoryButtons.size() == 5 && t.res.dailyCard.h > 40);
        CHECK(UI::DroppedGeometry() == 0);
        // the cards stack top to bottom without overlapping
        for (std::size_t i = 1; i < t.res.cards.size(); ++i) CHECK(t.res.cards[i].y >= t.res.cards[i - 1].y + t.res.cards[i - 1].h - 1.0f);
    }
    {   // wide window: two columns
        TipsRig t; t.sim = Sim(1100, 600);
        t.sim.Frames(4, [&] { t.res = S::DrawTips(t.model, t.st, 16, 12, 1100 - 32, 600 - 24); });
        CHECK(t.res.columns == 2 && t.res.cards.size() == 6);
        CHECK(t.res.cards[1].x > t.res.cards[0].x + 100);           // the second tip sits beside the first
    }
    {   // category pills filter; "All" restores
        TipsRig t; t.Frames(3);
        t.Click(t.res.categoryButtons[2]);                           // Scripting
        t.Frames(2);
        CHECK(t.st.cat == 2 && t.res.shown.size() == 2 && t.res.shown[0] == 2 && t.res.shown[1] == 3);
        t.Click(t.res.categoryButtons[0]);
        t.Frames(2);
        CHECK(t.st.cat == 0 && t.res.shown.size() == 6);
    }
    {   // search: typing filters, no match says so, clearing restores
        TipsRig t; t.Frames(3);
        t.Click(t.res.searchField);
        t.sim.Type("scene"); t.Frames(2);
        CHECK(std::strcmp(t.st.query, "scene") == 0);
        CHECK(t.res.shown.size() == 3);                              // "Test instantly", "Save everything" (scene) and the build one
        t.sim.Type("qqq"); t.Frames(2);
        CHECK(t.res.noMatch && t.res.shown.empty() && t.res.cards.empty());
        Check("tips_nomatch", t.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        for (int i = 0; i < 8; ++i) { t.sim.Press(Key_Backspace); t.Frame(); }
        CHECK(std::strcmp(t.st.query, "") == 0 && t.res.shown.size() == 6);
    }
    {   // Next tip cycles through all and wraps
        TipsRig t; t.Frames(3);
        for (int i = 0; i < 6; ++i) { t.Click(t.res.nextTipButton); t.Frames(1); }
        CHECK(t.st.daily == 2);                                      // six clicks on six tips: back where it began
        t.Click(t.res.nextTipButton);
        CHECK(t.st.daily == 3);
    }
    {   // host asks for the search field (Ctrl+F): it takes the keyboard and the request is consumed
        TipsRig t; t.Frames(2);
        t.st.focusSearch = true; t.Frames(2);
        CHECK(!t.st.focusSearch);
        t.sim.Type("play"); t.Frames(2);
        CHECK(std::strcmp(t.st.query, "play") == 0 && t.res.shown.size() == 1);
    }
    {   // an empty model and a bad state do not crash
        TipsRig t; t.model.tips.clear(); t.st.cat = 77; t.st.daily = 9; t.Frames(3);
        CHECK(t.res.shown.empty() && t.st.cat == 0);
        TipsRig u; u.model.cats.clear(); u.Frames(3);
        CHECK(u.res.categoryButtons.empty());
    }
    {   // golden
        TipsRig t; t.Frames(4);
        Check("tips_all", t.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
    }

    // ================= Crash Log =================
    {   // empty: the friendly message, copy disabled
        CrashRig c; c.Frames(3);
        CHECK(c.res.empty && c.res.lineCount == 0 && c.res.body.w == 0);
        c.Click(c.res.copyAllButton);
        CHECK(c.copied.empty());                                     // disabled with nothing to copy
        Check("crashlog_empty", c.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
    }
    {   // with reports: buttons call the host, the body shows the end first
        CrashRig c;
        for (int i = 0; i < 400; ++i) c.model.text += "frame " + std::to_string(i) + "\tat Foo::Bar\r\n";
        c.model.text += "LAST LINE";
        c.model.revision = 1;
        c.Frames(4);
        CHECK(!c.res.empty && c.res.lineCount == 401);
        CHECK(c.res.linesDrawn > 5 && c.res.linesDrawn < 60);       // only the visible rows are laid out
        c.Frames(2);
        CHECK(c.res.linesDrawn > 5 && c.res.linesDrawn < 60);
        CHECK(c.res.lastDrawn == 400 && c.res.firstDrawn > 340);          // it opened at the newest line, the last one is on screen
        c.Click(c.res.refreshButton); CHECK(c.refreshed == 1);
        c.Click(c.res.openFolderButton); CHECK(c.opened == 1);
        c.Click(c.res.copyAllButton); CHECK(c.copied.size() == 1 && c.copied[0] == c.model.text);
        CHECK(UI::DroppedGeometry() == 0);
        Check("crashlog_reports", c.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
    }
    {   // the body starts at the newest line and scrolls with the wheel; a reload goes back to the end
        CrashRig c;
        for (int i = 0; i < 300; ++i) c.model.text += "line " + std::to_string(i) + "\n";
        c.model.revision = 1; c.Frames(5);
        c.sim.Move(400, 300); c.sim.wheel = 3.0f; c.Frames(2);   // wheel up: moves toward the oldest
        CHECK(c.res.linesDrawn > 0);
        c.model.text += "newer\n"; ++c.model.revision; c.Frames(4);
        CHECK(c.res.lineCount == 301);
    }
    {   // Clear asks first; Cancel keeps everything; Delete calls the host and the page shows the empty state
        CrashRig c; c.model.text = "boom\nbang\n"; c.model.revision = 1; c.Frames(3);
        CHECK(c.res.lineCount == 2);
        c.Click(c.res.clearButton); c.Frames(2);
        CHECK(c.st.confirming && c.cleared == 0 && c.res.confirmDelete.w > 0 && c.res.confirmCancel.w > 0);
        Check("crashlog_confirm", c.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
        c.Click(c.res.confirmCancel); c.Frames(2);
        CHECK(!c.st.confirming && c.cleared == 0 && !c.model.text.empty());
        c.Click(c.res.clearButton); c.Frames(2);
        c.Click(c.res.confirmDelete); c.Frames(3);
        CHECK(c.cleared == 1 && c.res.empty && !c.st.confirming);
    }
    {   // odd text: blank lines, no trailing newline, a lone CR, a very long line, invalid bytes
        CrashRig c; c.model.text = std::string("a\n\n\nb\r\nc") + std::string(5000, 'x') + "\n\xff\xfe\n"; c.model.revision = 1;
        c.Frames(4);
        CHECK(c.res.lineCount == 6 && UI::DroppedGeometry() == 0);
    }

    // ================= Audio Mixer =================
    {
        CHECK_NEAR(S::MeterDb(1.0f), 0.0f, 1e-5); CHECK_NEAR(S::MeterDb(0.1f), -20.0f, 1e-3); CHECK(S::MeterDb(0.0f) == -60.0f);
        CHECK(S::MeterFill(0.0f) == 0.0f && S::MeterFill(1.0f) == 1.0f && S::MeterFill(5.0f) == 1.0f);
        CHECK_NEAR(S::MeterFill(0.0316228f), 0.5f, 1e-3);                  // -30 dB is half
        CHECK(S::MeterFill(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    }
    {   // layout: one row per group, sliders and mutes found
        MixRig m; m.Frames(4);
        CHECK(m.res.groupSliders.size() == 3 && m.res.groupMutes.size() == 3);
        CHECK(m.res.masterSlider.w > 100 && m.res.masterMute.w > 0 && m.res.resetButton.w > 0 && m.res.duckToggle.w > 0);
        CHECK(m.res.groupSliders[1].y > m.res.groupSliders[0].y);          // rows stack
        CHECK(UI::DroppedGeometry() == 0);
        Check("audiomixer_default", m.sim.Draw(kHostBg[0], kHostBg[1], kHostBg[2]));
    }
    {   // a click on a slider's track sets the value; the model changes in place
        MixRig m; m.Frames(3);
        const UI::Rect s = m.res.groupSliders[0];
        m.sim.Move(s.x + s.w * 0.9f, CY(s)); m.Frame(); m.sim.Down(); m.Frame(); m.sim.Up(); m.Frame();
        CHECK(m.gv[0] > 0.7f && m.gv[1] == 0.4f && m.gv[2] == 0.6f);
        m.sim.Move(m.res.masterSlider.x + 2, CY(m.res.masterSlider)); m.Frame(); m.sim.Down(); m.Frames(2); m.sim.Up(); m.Frame();
        CHECK(m.master < 0.15f);
    }
    {   // mute checkboxes toggle only their own group
        MixRig m; m.Frames(3);
        m.Click(m.res.groupMutes[1]);
        CHECK(m.gm[0] && m.gm[1] && m.gm[2]);
        m.Click(m.res.masterMute);
        CHECK(!m.masterMuted);
    }
    {   // duck options show only while the toggle is on; Reset restores the defaults
        MixRig m; m.Frames(3);
        const float yOn = m.res.resetButton.y;
        m.Click(m.res.duckToggle);
        CHECK(!m.duck);
        m.Frames(2);
        CHECK(m.res.resetButton.y < yOn - 20);                              // the three duck sliders and the line are gone
        m.Click(m.res.duckToggle); m.Frames(2);
        CHECK(m.duck && m.res.resetButton.y == yOn);
        m.Click(m.res.resetButton);
        CHECK(m.master == 1.0f && !m.masterMuted && m.gv[0] == 1.0f && m.gv[2] == 1.0f && !m.gm[0] && !m.gm[2]);
        CHECK(m.duckLevel == 0.3f);                                         // Reset leaves the duck settings alone, as the ImGui window does
    }
    {   // a short window scrolls: Reset starts below the fold, the wheel brings it into view and it works there
        MixRig m; m.sim = Sim(520, 300); m.h = 280; m.Frames(3);
        CHECK(m.res.resetButton.y > 300);                                   // not visible yet
        m.sim.Move(250, 150);
        for (int i = 0; i < 40; ++i) { m.sim.wheel = -3.0f; m.Frame(); }
        m.Frames(2);
        CHECK(m.res.resetButton.y < 300 && m.res.resetButton.y > 100);
        m.Click(m.res.resetButton);
        CHECK(m.master == 1.0f && !m.gm[0]);
    }
    {   // missing pointers are skipped, not dereferenced
        MixRig m; m.model.masterVolume = nullptr; m.model.masterMuted = nullptr; m.model.duckMusic = nullptr; m.model.groupVolume[1] = nullptr; m.model.groupMuted.pop_back();
        m.Frames(3);
        CHECK(m.res.duckToggle.w == 0);
        m.Click(m.res.resetButton);
    }
    TEST_MAIN_RESULT();
}
