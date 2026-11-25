// The launcher's system tray (launcher/TrayLogic.hpp) and where its chat is drawn (launcher/ChatPlacement.hpp), with a
// fake shell in place of Shell_NotifyIconW / TrackPopupMenu / ShowWindow, so it runs headless on Linux.
#include "TrayLogic.hpp"
#include "ChatPlacement.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace okaytray;

struct FakeShell : TrayShell {
    bool icon = false, hidden = false, refuseAdd = false;
    int adds = 0, removes = 0, hides = 0, shows = 0, balloons = 0, menus = 0;
    MenuCmd next = MenuCmd::None;
    std::vector<MenuItem> lastMenu;
    std::string lastBalloon;
    bool AddIcon(const std::string&) override { ++adds; if (refuseAdd) return false; icon = true; return true; }
    void RemoveIcon() override { ++removes; icon = false; }
    void HideWindow() override { ++hides; hidden = true; }
    void ShowWindow() override { ++shows; hidden = false; }
    MenuCmd RunMenu(const std::vector<MenuItem>& items) override { ++menus; lastMenu = items; return next; }
    void Balloon(const std::string&, const std::string& text) override { ++balloons; lastBalloon = text; }
};

static void TestDecisions() {
    Settings on, off; off.minimizeToTray = false; off.closeToTray = false;
    CHECK(on.minimizeToTray && on.closeToTray);   // both ticked by default
    // The X: to the tray when on, the confirm dialog when off or unsupported; Shift + X and tray Exit always quit.
    CHECK(DecideClose(on, true, CloseSource::WindowX) == CloseAction::HideToTray);
    CHECK(DecideClose(off, true, CloseSource::WindowX) == CloseAction::AskConfirm);
    CHECK(DecideClose(on, false, CloseSource::WindowX) == CloseAction::AskConfirm);   // Linux
    CHECK(DecideClose(on, true, CloseSource::WindowXShift) == CloseAction::Quit);
    CHECK(DecideClose(on, true, CloseSource::TrayExit) == CloseAction::Quit);
    CHECK(DecideClose(off, false, CloseSource::TrayExit) == CloseAction::Quit);
    Settings closeOnly; closeOnly.minimizeToTray = false;
    CHECK(DecideClose(closeOnly, true, CloseSource::WindowX) == CloseAction::HideToTray);
    CHECK(!HideOnMinimize(closeOnly, true));
    CHECK(HideOnMinimize(on, true));
    CHECK(!HideOnMinimize(on, false));
    CHECK(WantIcon(closeOnly, true, false));
    CHECK(!WantIcon(off, true, false));
    CHECK(WantIcon(off, true, true));
    CHECK(!WantIcon(on, false, true));
}

static void TestMenu() {
    std::vector<MenuItem> m = Menu(false);
    CHECK(m.size() == 4);
    CHECK(m[0].cmd == MenuCmd::Open && m[0].label == "Open Launcher" && m[0].isDefault);
    CHECK(m[1].cmd == MenuCmd::CheckUpdates && m[1].label == "Check for Updates" && m[1].separatorBefore);
    CHECK(m[2].cmd == MenuCmd::Settings && m[2].label == "Settings");
    CHECK(m[3].cmd == MenuCmd::Exit && m[3].label == "Exit" && m[3].separatorBefore);
    std::vector<MenuItem> mc = Menu(true);   // popped-out chat: one more item to bring it back in
    CHECK(mc.size() == 5 && mc[1].cmd == MenuCmd::ChatIn);
    for (const MenuItem& it : mc) CHECK((int)it.cmd != 0);   // 0 is TrackPopupMenu's "nothing chosen"
    Effect e = EffectOf(MenuCmd::Open);         CHECK(e.show && !e.quit && !e.checkUpdates && !e.openSettings);
    e = EffectOf(MenuCmd::CheckUpdates);        CHECK(e.show && e.checkUpdates && !e.quit);
    e = EffectOf(MenuCmd::Settings);            CHECK(e.show && e.openSettings && !e.quit);
    e = EffectOf(MenuCmd::ChatIn);              CHECK(e.show && e.chatIn);
    e = EffectOf(MenuCmd::Exit);                CHECK(e.quit && !e.show);
    e = EffectOf(MenuCmd::None);                CHECK(!e.show && !e.quit && !e.checkUpdates && !e.openSettings && !e.chatIn);
}

static void TestIconMessages() {
    CHECK(ClassifyIconMessage(kWmLButtonDblClk) == IconEvent::Restore);
    CHECK(ClassifyIconMessage(kNinKeySelect) == IconEvent::Restore);
    CHECK(ClassifyIconMessage(kWmRButtonUp) == IconEvent::ShowMenu);
    CHECK(ClassifyIconMessage(kWmContextMenu) == IconEvent::ShowMenu);
    CHECK(ClassifyIconMessage(kWmLButtonUp) == IconEvent::None);   // a single click is the first half of a double-click
    CHECK(ClassifyIconMessage(0x0200) == IconEvent::None);         // mouse move over the icon
}

static void TestController() {
    FakeShell sh;
    TrayController t(&sh, true);
    t.Sync();
    CHECK(sh.icon && t.IconShown());   // on by default: the icon is there from the start
    // X hides to the tray, first time with the note.
    CHECK(t.OnClose(CloseSource::WindowX) == CloseAction::HideToTray);
    CHECK(sh.hidden && t.Hidden() && sh.balloons == 1);
    CHECK(sh.lastBalloon.find("still running in the tray") != std::string::npos);
    CHECK(t.TakeHintJustShown() && !t.TakeHintJustShown());
    // Double-click restores.
    Effect e = t.OnIconMessage(kWmLButtonDblClk, false);
    CHECK(e.show && !sh.hidden && !t.Hidden() && sh.shows == 1);
    // A second hide: no second note.
    CHECK(t.OnMinimized() && sh.hidden && sh.balloons == 1);
    // Right-click > Check for Updates: shows the launcher and asks for the check.
    sh.next = MenuCmd::CheckUpdates;
    e = t.OnIconMessage(kWmRButtonUp, false);
    CHECK(sh.menus == 1 && sh.lastMenu.size() == 4 && e.checkUpdates && e.show && !sh.hidden);
    // Right-click > Settings.
    t.HideToTray(); sh.next = MenuCmd::Settings;
    e = t.OnIconMessage(kWmContextMenu, false);
    CHECK(e.openSettings && e.show && !t.Hidden());
    // Right-click > Exit: quits, the window stays where it was (hidden).
    t.HideToTray(); sh.next = MenuCmd::Exit;
    e = t.OnIconMessage(kWmRButtonUp, true);
    CHECK(e.quit && !e.show && t.Hidden() && sh.lastMenu.size() == 5);
    // Menu dismissed: nothing.
    sh.next = MenuCmd::None;
    e = t.OnIconMessage(kWmRButtonUp, false);
    CHECK(!e.quit && !e.show && t.Hidden());
    // A single click does nothing.
    e = t.OnIconMessage(kWmLButtonUp, false);
    CHECK(!e.show && t.Hidden());
    // Explorer restarted: the icon is added again.
    const int addsBefore = sh.adds;
    t.OnTaskbarCreated();
    CHECK(sh.adds == addsBefore + 1 && sh.icon);
    // Shutdown removes it.
    t.Shutdown();
    CHECK(!sh.icon && !t.IconShown());
    t.OnTaskbarCreated();   // no icon: nothing re-added
    CHECK(!sh.icon);
}

static void TestSafetyAndOff() {
    // The shell refuses the icon: the window must NOT hide (it would be trapped); the X falls back to the dialog.
    FakeShell sh; sh.refuseAdd = true;
    TrayController t(&sh, true);
    t.Sync();
    CHECK(!t.IconShown());
    CHECK(t.OnClose(CloseSource::WindowX) == CloseAction::AskConfirm);
    CHECK(!sh.hidden && sh.hides == 0);
    CHECK(!t.OnMinimized() && sh.hides == 0);
    // Settings off: X asks, minimize stays a normal minimize, no icon.
    FakeShell s2;
    TrayController t2(&s2, true);
    t2.settings.minimizeToTray = false; t2.settings.closeToTray = false;
    t2.Sync();
    CHECK(!s2.icon);
    CHECK(t2.OnClose(CloseSource::WindowX) == CloseAction::AskConfirm);
    CHECK(!t2.OnMinimized() && s2.hides == 0);
    CHECK(t2.OnClose(CloseSource::WindowXShift) == CloseAction::Quit);
    // Turning one back on adds the icon; turning both off again removes it.
    t2.settings.closeToTray = true; t2.Sync(); CHECK(s2.icon);
    t2.settings.closeToTray = false; t2.Sync(); CHECK(!s2.icon);
    // Unsupported (Linux, no shell): nothing ever hides.
    TrayController t3(nullptr, true);
    CHECK(!t3.Supported());
    t3.Sync();
    CHECK(t3.OnClose(CloseSource::WindowX) == CloseAction::AskConfirm);
    CHECK(!t3.OnMinimized() && !t3.Hidden());
    t3.Restore(); t3.Shutdown();   // harmless
    CHECK(t3.OnClose(CloseSource::TrayExit) == CloseAction::Quit);
    // The note is never shown again once it was (it is saved).
    FakeShell s4; TrayController t4(&s4, true); t4.hintShown = true;
    CHECK(t4.HideToTray() && s4.balloons == 0 && !t4.TakeHintJustShown());
}

static void TestFrameRate() {
    const int visibleMin = IdleDelayMs(false, false, false, false);
    const int tray = IdleDelayMs(true, false, false, false);
    CHECK(visibleMin == 120);            // a normal minimize, as before
    CHECK(tray == 250);                  // hidden in the tray: slower still
    CHECK(IdleFramesPerSecond(tray) == 4);
    CHECK(IdleFramesPerSecond(tray) < IdleFramesPerSecond(visibleMin));
    CHECK(IdleDelayMs(true, false, false, true) == 20);    // a live voice call keeps its audio pumped
    CHECK(IdleDelayMs(true, true, true, false) == 15);     // the popped-out chat keeps drawing while the launcher is hidden
    CHECK(IdleDelayMs(true, true, false, false) == 45);
    CHECK(IdleFramesPerSecond(0) == 0);
}

static void TestChatPlacement() {
    using namespace okaychat;
    Place p;                                   // closed, inside
    CHECK(!WantOwnWindow(p) && !DrawInside(p));
    p = PopOut(p);                             // pop out
    CHECK(p.open && p.ownWindow && WantOwnWindow(p) && !DrawInside(p));
    p = PopIn(p);                              // pop in: still open, inside
    CHECK(p.open && !p.ownWindow && DrawInside(p) && !WantOwnWindow(p));
    p = TogglePop(p); CHECK(p.ownWindow && p.open);
    p = TogglePop(p); CHECK(!p.ownWindow && p.open);
    p = PopOut(p);
    p = CloseOwnWindow(p);                     // closing the popped window: closed AND back in
    CHECK(!p.open && !p.ownWindow && !WantOwnWindow(p));
    p.open = true; CHECK(DrawInside(p));       // opening again shows it inside
    // Remembered geometry.
    Geometry g{100, 200, 800, 600};
    CHECK(Remember(g, true, true, 5, 5, 300, 300).x == 100);   // minimized: keep the last good one
    CHECK(Remember(g, false, false, 5, 5, 900, 700).w == 800); // not shown: keep it
    Geometry r = Remember(g, true, false, 40, 50, 1000, 700);
    CHECK(r.x == 40 && r.y == 50 && r.w == 1000 && r.h == 700);
    r = Remember(g, true, false, 40, 50, 100, 100);             // too small: grown to the minimum
    CHECK(r.w == kMinW && r.h == kMinH);
    CHECK(Sanitize(Geometry{-1, -1, 10, 9999}).w == kMinW && Sanitize(Geometry{-1, -1, 10, 9999}).h == 9999);
    // Narrow: the list folds to the avatar strip; wide: the person's choice.
    CHECK(ListFolded(false, 500.0f) && !ListFolded(false, 900.0f) && ListFolded(true, 900.0f));
}

// The same chat view in both places: launcher/main.cpp has ONE DrawChatWindow, drawn inside the launcher and in the
// popped-out window's frame (a structural check of the source, since the view needs a live window to draw).
static void TestOneChatView() {
    std::ifstream f(OKAY_LAUNCHER_MAIN);
    CHECK(f.good());
    if (!f.good()) return;
    std::stringstream ss; ss << f.rdbuf();
    const std::string src = ss.str();
    int defs = 0, calls = 0;
    for (size_t i = src.find("DrawChatWindow("); i != std::string::npos; i = src.find("DrawChatWindow(", i + 1)) {
        const size_t ls = src.rfind('\n', i) + 1;
        const std::string line = src.substr(ls, src.find('\n', i) - ls);
        if (line.find("//") != std::string::npos && line.find("//") < i - ls) continue;   // a comment
        if (line.rfind("void DrawChatWindow(", 0) == 0) { if (line.find(';') == std::string::npos) ++defs; }
        else if (line.find("DrawChatWindow(account)") != std::string::npos) ++calls;
    }
    CHECK(defs == 1);    // one view
    CHECK(calls == 2);   // drawn in two places: inside the launcher and in ChatHostFrame
    CHECK(src.find("if (!g_chatSeparate) DrawChatWindow(account);") != std::string::npos);
}

int main() {
    TestDecisions();
    TestMenu();
    TestIconMessages();
    TestController();
    TestSafetyAndOff();
    TestFrameRate();
    TestChatPlacement();
    TestOneChatView();
    std::printf("launcher_tray: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
