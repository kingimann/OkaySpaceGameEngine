// The launcher's system tray (launcher/TrayLogic.hpp) and where its chat is drawn (launcher/ChatPlacement.hpp), with a
// fake shell in place of Shell_NotifyIconW / TrackPopupMenu / ShowWindow, so it runs headless on Linux.
#include "TrayLogic.hpp"
#include "ChatPlacement.hpp"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace okaytray;

struct FakeShell : TrayShell {
    bool icon = false, hidden = false, refuseAdd = false, iconLost = false;
    int adds = 0, removes = 0, hides = 0, shows = 0, balloons = 0, menus = 0;
    int next = kIdNone;
    std::vector<MenuItem> lastMenu;
    std::string lastBalloon;
    bool AddIcon(const std::string&) override { ++adds; if (refuseAdd) return false; icon = true; iconLost = false; return true; }
    void RemoveIcon() override { ++removes; icon = false; }
    bool IconAlive() override { return icon && !iconLost; }
    void HideWindow() override { ++hides; hidden = true; }
    void ShowWindow() override { ++shows; hidden = false; }
    int RunMenu(const std::vector<MenuItem>& items) override { ++menus; lastMenu = items; return next; }
    void Balloon(const std::string&, const std::string& text) override { ++balloons; lastBalloon = text; }
};

// The window is drawn at full speed when it is not hidden in the tray (main.cpp skips drawing and idles while
// TrayController::Hidden() or the window is hidden / minimized).
static bool Drawing(const TrayController& t, const FakeShell& sh) { return !t.Hidden() && !sh.hidden; }

static void TestDecisions() {
    Settings def, off; off.minimizeToTray = false; off.closeToTray = false;
    CHECK(!def.minimizeToTray && def.closeToTray);   // owner bug 2026-11-25: minimize is a normal minimize by default
    Settings on; on.minimizeToTray = true;
    // The X: to the tray when on, the confirm dialog when off or unsupported; Shift + X and tray Exit always quit.
    CHECK(DecideClose(def, true, CloseSource::WindowX) == CloseAction::HideToTray);
    CHECK(DecideClose(off, true, CloseSource::WindowX) == CloseAction::AskConfirm);
    CHECK(DecideClose(def, false, CloseSource::WindowX) == CloseAction::AskConfirm);   // Linux
    CHECK(DecideClose(def, true, CloseSource::WindowXShift) == CloseAction::Quit);
    CHECK(DecideClose(def, true, CloseSource::TrayExit) == CloseAction::Quit);
    CHECK(DecideClose(off, false, CloseSource::TrayExit) == CloseAction::Quit);
    CHECK(!HideOnMinimize(def, true));   // the default: minimize stays on the task bar
    CHECK(HideOnMinimize(on, true));
    CHECK(!HideOnMinimize(on, false));
    CHECK(WantIcon(def, true, false));
    CHECK(!WantIcon(off, true, false));
    CHECK(WantIcon(off, true, true));
    CHECK(!WantIcon(on, false, true));
}

static const MenuItem* TrayItemById(const std::vector<MenuItem>& m, int id) {
    for (const MenuItem& it : m) {
        if (it.id == id && it.sub.empty()) return &it;
        if (const MenuItem* s = TrayItemById(it.sub, id)) return s;
    }
    return nullptr;
}
static const MenuItem* TrayItemByLabel(const std::vector<MenuItem>& m, const std::string& p) {
    for (const MenuItem& it : m) if (it.label.rfind(p, 0) == 0) return &it;
    return nullptr;
}
static void CheckIds(const std::vector<MenuItem>& m) {
    for (const MenuItem& it : m) {
        if (it.sub.empty() && it.enabled) CHECK(it.id != kIdNone);   // 0 is TrackPopupMenu's "nothing chosen"
        CheckIds(it.sub);
    }
}

static void TestMenu() {
    // No projects, signed out, nothing waiting.
    MenuState st;
    std::vector<MenuItem> m = BuildMenu(st);
    CHECK(m[0].id == kIdOpen && m[0].label == "Open Launcher" && m[0].isDefault && m[0].enabled);
    const MenuItem* ed = TrayItemById(m, kIdOpenEditor);
    CHECK(ed && !ed->enabled && ed->label == "Open Editor");                 // no project: greyed out
    const MenuItem* rec = TrayItemByLabel(m, "Recent projects");
    CHECK(rec && !rec->enabled && rec->sub.size() == 1 && !rec->sub[0].enabled);
    const MenuItem* stat = TrayItemByLabel(m, "Status");
    CHECK(stat && !stat->enabled && stat->separatorBefore && stat->sub.size() == (size_t)kStatusCount);   // signed out: greyed out
    CHECK(TrayItemById(m, kIdNotifications)->label == "Notifications");
    CHECK(TrayItemById(m, kIdDownloads)->label == "Downloads");
    CHECK(TrayItemById(m, kIdCheckUpdates) && TrayItemById(m, kIdSettings)->separatorBefore);
    CHECK(m.back().id == kIdExit && m.back().label == "Exit OkaySpace");
    CHECK(!TrayItemById(m, kIdChatIn));
    CheckIds(m);
    // Projects, signed in and Away, unread notifications and downloads running, chat popped out.
    st.editorAvailable = true;
    st.recentProjects = {"Space Race", "A&B", "Three", "Four", "Five", "Six"};
    st.signedIn = true; st.onlineStatus = 1; st.unreadNotifications = 3; st.activeDownloads = 2; st.chatPoppedOut = true;
    m = BuildMenu(st);
    ed = TrayItemById(m, kIdOpenEditor);
    CHECK(ed && ed->enabled && ed->label == "Open Editor (Space Race)");
    rec = TrayItemByLabel(m, "Recent projects");
    CHECK(rec && rec->enabled && rec->sub.size() == (size_t)kMaxRecent);    // at most 5
    CHECK(rec->sub[1].label == "A&B" && rec->sub[1].id == kRecentBase + 1);
    stat = TrayItemByLabel(m, "Status");
    CHECK(stat && stat->enabled && stat->label == "Status: Away");
    CHECK(!stat->sub[0].checked && stat->sub[1].checked && !stat->sub[2].checked);
    CHECK(stat->sub[0].label == "Online" && stat->sub[2].label == "Invisible");
    CHECK(TrayItemById(m, kIdNotifications)->label == "Notifications (3 unread)");
    CHECK(TrayItemById(m, kIdDownloads)->label == "Downloads (2 active)");
    CHECK(TrayItemById(m, kIdChatIn) != nullptr);
    CheckIds(m);
    // The editor program is missing: projects listed, but nothing can open them.
    st.editorAvailable = false;
    m = BuildMenu(st);
    CHECK(!TrayItemById(m, kIdOpenEditor)->enabled && !TrayItemByLabel(m, "Recent projects")->enabled);
    CHECK(!HasEnabledId(m, kRecentBase));
    CHECK(HasEnabledId(m, kIdOpen) && !HasEnabledId(m, kIdNone) && !HasEnabledId(m, 9999));
    // Each item calls the right action.
    Effect e = EffectOf(kIdOpen);           CHECK(e.show && !e.quit && !e.checkUpdates && !e.openSettings && e.openProject < 0);
    e = EffectOf(kIdOpenEditor);            CHECK(e.show && e.openProject == 0);
    e = EffectOf(kRecentBase + 3);          CHECK(e.show && e.openProject == 3);
    e = EffectOf(kRecentBase + kMaxRecent); CHECK(!e.show && e.openProject < 0);   // out of range: nothing
    e = EffectOf(kStatusBase + 0);          CHECK(e.setStatus == 0 && !e.show);    // status changes leave the window alone
    e = EffectOf(kStatusBase + 2);          CHECK(e.setStatus == 2);
    e = EffectOf(kIdNotifications);         CHECK(e.show && e.openNotifications && !e.openDownloads);
    e = EffectOf(kIdDownloads);             CHECK(e.show && e.openDownloads && !e.openNotifications);
    e = EffectOf(kIdCheckUpdates);          CHECK(e.show && e.checkUpdates && !e.quit);
    e = EffectOf(kIdSettings);              CHECK(e.show && e.openSettings && !e.quit);
    e = EffectOf(kIdChatIn);                CHECK(e.show && e.chatIn);
    e = EffectOf(kIdExit);                  CHECK(e.quit && !e.show);
    e = EffectOf(kIdNone);                  CHECK(!e.show && !e.quit && !e.checkUpdates && !e.openSettings && !e.chatIn && e.setStatus < 0);
    // Recent projects: newest first, missing ones left out, at most 5.
    std::vector<ProjectEntry> all = {{"old", "a.okayscene", 10}, {"gone", "b.okayscene", 0}, {"new", "c.okayscene", 50},
                                     {"mid", "d.okayscene", 30}, {"e", "e", 1}, {"f", "f", 2}, {"g", "g", 3}};
    std::vector<ProjectEntry> r = MostRecentProjects(all);
    CHECK(r.size() == 5 && r[0].name == "new" && r[1].name == "mid" && r[2].name == "old" && r[3].name == "g" && r[4].name == "f");
    CHECK(MostRecentProjects({}).empty());
}

static void TestIconMessages() {
    // NOTIFYICON_VERSION_4: LOWORD(lParam) = the event, HIWORD(lParam) = the icon id, wParam = the anchor point.
    IconMsg d = DecodeIconMessage(kIconVersion4, ((uint64_t)(uint16_t)-20 << 16) | 300u, ((int64_t)1 << 16) | kNinSelect);
    CHECK(d.event == kNinSelect && d.iconId == 1 && d.x == 300 && d.y == -20 && d.hasPoint);
    d = DecodeIconMessage(kIconVersion4, 0, ((int64_t)1 << 16) | kWmContextMenu);
    CHECK(d.event == kWmContextMenu);
    // The old layout: lParam is the mouse message, wParam the id.
    d = DecodeIconMessage(kIconVersionOld, 1, kWmRButtonUp);
    CHECK(d.event == kWmRButtonUp && d.iconId == 1 && !d.hasPoint);
    // The bug: reading the whole lParam as the event under version 4 would see 0x1007B, not WM_CONTEXTMENU.
    CHECK(DecodeIconMessage(kIconVersion4, 0, 0x1007B).event == kWmContextMenu);
    // Version 4: a single left click (NIN_SELECT), a double-click and the keyboard restore; WM_CONTEXTMENU shows the
    // menu; the raw button messages that come with them do nothing (one action per click).
    CHECK(ClassifyIconMessage(kNinSelect, kIconVersion4) == IconEvent::Restore);
    CHECK(ClassifyIconMessage(kNinKeySelect, kIconVersion4) == IconEvent::Restore);
    CHECK(ClassifyIconMessage(kWmLButtonDblClk, kIconVersion4) == IconEvent::Restore);
    CHECK(ClassifyIconMessage(kWmContextMenu, kIconVersion4) == IconEvent::ShowMenu);
    CHECK(ClassifyIconMessage(kWmRButtonUp, kIconVersion4) == IconEvent::None);
    CHECK(ClassifyIconMessage(kWmLButtonUp, kIconVersion4) == IconEvent::None);
    CHECK(ClassifyIconMessage(kWmMouseMove, kIconVersion4) == IconEvent::None);
    // The old layout (the shell refused version 4): the raw messages.
    CHECK(ClassifyIconMessage(kWmLButtonUp, kIconVersionOld) == IconEvent::Restore);   // a single left click restores
    CHECK(ClassifyIconMessage(kWmLButtonDblClk, kIconVersionOld) == IconEvent::Restore);
    CHECK(ClassifyIconMessage(kWmRButtonUp, kIconVersionOld) == IconEvent::ShowMenu);
    CHECK(ClassifyIconMessage(kWmLButtonDown, kIconVersionOld) == IconEvent::None);
    CHECK(ClassifyIconMessage(kWmRButtonDown, kIconVersionOld) == IconEvent::None);
    CHECK(ClassifyIconMessage(kWmMouseMove, kIconVersionOld) == IconEvent::None);
}

static void TestController() {
    FakeShell sh;
    TrayController t(&sh, true);
    t.Sync();
    CHECK(sh.icon && t.IconShown());   // close to tray is on by default: the icon is there from the start
    // Minimize with the default settings never hides: it stays a normal task-bar minimize.
    CHECK(!t.OnMinimized() && sh.hides == 0 && !t.Hidden());
    // X hides to the tray, first time with the note saying where the icon is.
    CHECK(t.OnClose(CloseSource::WindowX) == CloseAction::HideToTray);
    CHECK(sh.hidden && t.Hidden() && sh.balloons == 1 && !Drawing(t, sh));
    CHECK(sh.lastBalloon.find("still running") != std::string::npos && sh.lastBalloon.find("^") != std::string::npos);
    CHECK(t.TakeHintJustShown() && !t.TakeHintJustShown());
    MenuState st;
    // Every restore trigger goes through Restore and leaves the window shown and drawing.
    int restores = t.Restores();
    Effect e = t.OnIconMessage(kIconVersion4, 0, ((int64_t)1 << 16) | kNinSelect, st);   // single left click (version 4)
    CHECK(e.show && t.Restores() == restores + 1 && Drawing(t, sh) && sh.shows == 1);
    t.HideToTray(); restores = t.Restores();
    e = t.OnIconMessage(kIconVersion4, 0, ((int64_t)1 << 16) | kWmLButtonDblClk, st);    // double-click
    CHECK(e.show && t.Restores() == restores + 1 && Drawing(t, sh));
    t.HideToTray(); restores = t.Restores();
    e = t.OnIconMessage(kIconVersionOld, 1, kWmLButtonUp, st);                           // single click, old layout
    CHECK(e.show && t.Restores() == restores + 1 && Drawing(t, sh));
    t.HideToTray(); restores = t.Restores(); sh.next = kIdOpen;
    e = t.OnIconMessage(kIconVersion4, 0, ((int64_t)1 << 16) | kWmContextMenu, st);      // menu > Open Launcher
    CHECK(e.show && sh.menus == 1 && t.Restores() == restores + 1 && Drawing(t, sh));
    t.HideToTray(); restores = t.Restores();
    t.OnSecondLaunch();                                                                  // a second launch
    CHECK(t.Restores() == restores + 1 && Drawing(t, sh));
    restores = t.Restores(); const int shows = sh.shows;
    t.Restore();                                                                         // not hidden (only minimized): still shows it
    CHECK(t.Restores() == restores + 1 && sh.shows == shows + 1 && Drawing(t, sh));
    CHECK(IdleDelayMs(t.Hidden(), false, false, false) == 120);   // and the loop is out of its 250 ms tray idle
    // Right-click > Check for Updates: shows the launcher and asks for the check.
    t.HideToTray(); sh.next = kIdCheckUpdates;
    e = t.OnIconEvent(IconEvent::ShowMenu, st);
    CHECK(e.checkUpdates && e.show && !sh.hidden);
    CHECK(sh.lastMenu.size() == BuildMenu(st).size());
    // Right-click > Status > Invisible: the status changes, the window stays hidden.
    t.HideToTray(); st.signedIn = true; sh.next = kStatusBase + 2;
    e = t.OnIconEvent(IconEvent::ShowMenu, st);
    CHECK(e.setStatus == 2 && !e.show && t.Hidden());
    // A greyed-out item (signed out, so Status is disabled) is never acted on, even if the shell reports it.
    st.signedIn = false; sh.next = kStatusBase + 1;
    e = t.OnIconEvent(IconEvent::ShowMenu, st);
    CHECK(e.setStatus < 0 && t.Hidden());
    sh.next = kRecentBase;   // no projects
    e = t.OnIconEvent(IconEvent::ShowMenu, st);
    CHECK(e.openProject < 0 && t.Hidden());
    // Right-click > Exit: quits, the window stays where it was (hidden).
    sh.next = kIdExit;
    e = t.OnIconEvent(IconEvent::ShowMenu, st);
    CHECK(e.quit && !e.show && t.Hidden());
    // Menu dismissed: nothing.
    sh.next = kIdNone;
    e = t.OnIconEvent(IconEvent::ShowMenu, st);
    CHECK(!e.quit && !e.show && t.Hidden());
    // A mouse move over the icon does nothing.
    e = t.OnIconMessage(kIconVersion4, 0, kWmMouseMove, st);
    CHECK(!e.show && t.Hidden());
    // Explorer restarted: the icon is added again.
    const int addsBefore = sh.adds;
    t.OnTaskbarCreated();
    CHECK(sh.adds == addsBefore + 1 && sh.icon && t.Hidden());
    // Explorer restarted and refuses the icon while hidden: come back.
    sh.refuseAdd = true; restores = t.Restores();
    t.OnTaskbarCreated();
    CHECK(t.Restores() == restores + 1 && Drawing(t, sh));
    sh.refuseAdd = false; t.Sync();
    // Shutdown removes it.
    t.Shutdown();
    CHECK(!sh.icon && !t.IconShown());
    t.OnTaskbarCreated();   // no icon, not hidden: nothing re-added
    CHECK(!sh.icon);
    // "Minimize to tray" turned on by the person: minimize hides, a click brings it back.
    FakeShell s2; TrayController t2(&s2, true); t2.settings.minimizeToTray = true; t2.hintShown = true; t2.Sync();
    CHECK(t2.OnMinimized() && s2.hidden && t2.Hidden());
    e = t2.OnIconEvent(IconEvent::Restore, MenuState());
    CHECK(e.show && Drawing(t2, s2));
}

static void TestWatchdog() {
    FakeShell sh; TrayController t(&sh, true); t.hintShown = true; t.Sync();
    CHECK(!t.Watchdog());                       // not hidden: nothing to do
    CHECK(t.HideToTray() && t.Hidden());
    CHECK(!t.Watchdog() && t.Hidden());         // icon there: stay hidden
    sh.iconLost = true;                         // the shell dropped the icon; adding it again works
    CHECK(!t.Watchdog() && t.Hidden() && sh.icon && !sh.iconLost);
    sh.iconLost = true; sh.refuseAdd = true;    // gone and cannot be added: the window comes back at once
    const int restores = t.Restores();
    CHECK(t.Watchdog() && !t.Hidden() && t.Restores() == restores + 1 && Drawing(t, sh));
}

static void TestSafetyAndOff() {
    // The shell refuses the icon: the window must NOT hide (it would be trapped); the X falls back to the dialog.
    FakeShell sh; sh.refuseAdd = true;
    TrayController t(&sh, true);
    t.settings.minimizeToTray = true;
    t.Sync();
    CHECK(!t.IconShown());
    CHECK(t.OnClose(CloseSource::WindowX) == CloseAction::AskConfirm);
    CHECK(!sh.hidden && sh.hides == 0);
    CHECK(!t.OnMinimized() && sh.hides == 0);
    CHECK(!t.HideToTray() && !t.Hidden() && sh.hides == 0);
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
    CHECK(!t3.Watchdog());
    CHECK(!t3.OnIconEvent(IconEvent::ShowMenu, MenuState()).show);   // no shell, no menu
    CHECK(t3.OnClose(CloseSource::TrayExit) == CloseAction::Quit);
    // The note is never shown again once it was (it is saved).
    FakeShell s4; TrayController t4(&s4, true); t4.hintShown = true;
    CHECK(t4.HideToTray() && s4.balloons == 0 && !t4.TakeHintJustShown());
}

// main.cpp: every way back calls the one RestoreLauncher, the old preference key is not read back (it was saved as 1 for
// everyone), and the tray icon's clicks come through the tray's own window (a structural check of the source).
static void TestOneRestorePath() {
    std::ifstream f(OKAY_LAUNCHER_MAIN);
    CHECK(f.good());
    if (!f.good()) return;
    std::stringstream ss; ss << f.rdbuf();
    const std::string src = ss.str();
    int calls = 0;
    for (size_t i = src.find("RestoreLauncher(window)"); i != std::string::npos; i = src.find("RestoreLauncher(window)", i + 1)) ++calls;
    CHECK(calls >= 5);   // menu / click, second launch, --versions hand-off, a game's raise request, the watchdog
    CHECK(src.find("if (g_tray) g_tray->Restore();") != std::string::npos);
    int direct = 0;   // no trigger calls the controller's Restore by itself any more
    for (size_t i = src.find("g_tray->Restore()"); i != std::string::npos; i = src.find("g_tray->Restore()", i + 1)) ++direct;
    CHECK(direct == 1);
    CHECK(src.find("k == \"minimize_to_tray\"") == std::string::npos);
    CHECK(src.find("bool g_minimizeToTray = false;") != std::string::npos);
    CHECK(src.find("MakeWin32Shell(lw::Win32Handle(window), TrayOnMessage)") != std::string::npos);
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

// The Friends item and the status choices (owner, 2026-11-25: "i dont see it in right click icon tray").
static void TestTrayFriends() {
    MenuState st;   // signed out
    std::vector<MenuItem> m = BuildMenu(st);
    const MenuItem* fr = TrayItemById(m, kIdFriends);
    CHECK(fr && !fr->enabled && fr->label == "Friends" && fr->separatorBefore);   // greyed out signed out
    CHECK(!TrayItemById(m, kIdFriendsWindow));
    CHECK(!HasEnabledId(m, kIdFriends));
    // Signed in, last used inside the launcher, 3 friends online.
    st.signedIn = true; st.friendsOnline = 3;
    m = BuildMenu(st);
    fr = TrayItemById(m, kIdFriends);
    CHECK(fr && fr->enabled && fr->label == "Friends (3 online)" && HasEnabledId(m, kIdFriends));
    // Unread chats win over the online count; last used popped out: the window item.
    st.unreadChats = 2; st.friendsInOwnWindow = true;
    m = BuildMenu(st);
    CHECK(!TrayItemById(m, kIdFriends));
    fr = TrayItemById(m, kIdFriendsWindow);
    CHECK(fr && fr->enabled && fr->label == "Friends (2 unread)");
    // It sits after Recent projects and before Status.
    int iRecent = -1, iFriends = -1, iStatus = -1;
    for (int i = 0; i < (int)m.size(); ++i) {
        if (m[i].label == "Recent projects") iRecent = i;
        if (m[i].id == kIdFriendsWindow) iFriends = i;
        if (m[i].label.rfind("Status", 0) == 0) iStatus = i;
    }
    CHECK(iRecent >= 0 && iRecent < iFriends && iFriends < iStatus);
    CheckIds(m);
    // What it does: inside = the launcher comes back on the Friends list; popped out = only the friends window.
    Effect e = EffectOf(kIdFriends);        CHECK(e.show && e.openFriends && !e.friendsOwnWindow && !e.quit);
    e = EffectOf(kIdFriendsWindow);         CHECK(!e.show && e.openFriends && e.friendsOwnWindow && !e.quit);
    e = EffectOf(kIdOpen);                  CHECK(!e.openFriends);
    // The status choices: four radio items, the current one checked, Do not disturb included.
    st.onlineStatus = 3;
    m = BuildMenu(st);
    const MenuItem* stat = TrayItemByLabel(m, "Status: Do not disturb");
    CHECK(stat && stat->enabled && stat->sub.size() == 4);
    int checked = 0;
    for (const MenuItem& it : stat->sub) { CHECK(it.radio && it.enabled); if (it.checked) ++checked; }
    CHECK(checked == 1 && stat->sub[3].checked && stat->sub[3].label == "Do not disturb" && stat->sub[3].id == kStatusBase + 3);
    e = EffectOf(kStatusBase + 3);          CHECK(e.setStatus == 3 && !e.show);
    e = EffectOf(kStatusBase + 4);          CHECK(e.setStatus < 0);   // out of range: nothing
    // Picking Friends through the controller, while hidden in the tray, does not bring the launcher back when popped out.
    FakeShell sh; TrayController t(&sh, true);
    CHECK(t.HideToTray() && t.Hidden());
    sh.next = kIdFriendsWindow;
    e = t.OnIconEvent(IconEvent::ShowMenu, st);
    CHECK(e.openFriends && e.friendsOwnWindow && t.Hidden() && sh.hidden);
    sh.next = kIdFriends;
    st.friendsInOwnWindow = false;
    e = t.OnIconEvent(IconEvent::ShowMenu, st);
    CHECK(e.openFriends && !t.Hidden() && !sh.hidden);   // inside the launcher: the launcher comes back
    // The Win32 menu draws radio bullets for them (a structural check: TrayWin32.cpp only compiles on Windows).
    std::ifstream f(OKAY_LAUNCHER_MAIN);
    std::stringstream ss; ss << f.rdbuf();
    const std::string src = ss.str();
    CHECK(src.find("st.friendsInOwnWindow = g_chatSeparate;") != std::string::npos);
    CHECK(src.find("if (fx.openFriends)") != std::string::npos);
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

// Owner bug 2026-11-25 ("if i quit the launcher from the tray, the editor doesnt close"): Exit OkaySpace in the tray
// menu with the editor open raised "Close the editor first" in a launcher hidden in the tray, which draws nothing, so
// the dialog never showed and the launcher kept running. Now the window comes back once when such a dialog starts.
static void TestTrayExitWithEditor() {
    Settings on;
    FakeShell sh; TrayController t(&sh, true); t.settings = on; t.Sync();
    CHECK(t.OnClose(CloseSource::WindowX) == CloseAction::HideToTray && t.Hidden());
    sh.next = kIdExit;
    MenuState st;
    const Effect fx = t.OnIconEvent(IconEvent::ShowMenu, st);
    CHECK(fx.quit && !fx.show);   // Exit itself does not show the window...
    // ...so the launcher's quit (editor open: g_afterEditor = 1) must bring it back for the dialog.
    CHECK(ShowForPendingDialog(true, false, t.Hidden()));
    t.Restore();
    CHECK(Drawing(t, sh));
    CHECK(!ShowForPendingDialog(true, true, true));     // only on the edge: minimizing it again while it waits is allowed
    CHECK(!ShowForPendingDialog(true, false, false));   // already on screen: nothing to do
    CHECK(!ShowForPendingDialog(false, false, true));   // nothing waiting: the tray stays as it is
    CHECK(!ShowForPendingDialog(false, true, true));
    const std::string src = [] { std::ifstream f(OKAY_LAUNCHER_MAIN); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }();
    CHECK(src.find("auto quitLauncher = [&]() { if (EditorRunning()) g_afterEditor = 1; else running = false; };") != std::string::npos);
    CHECK(src.find("okaytray::ShowForPendingDialog(") != std::string::npos);
}

int main() {
    TestDecisions();
    TestTrayExitWithEditor();
    TestMenu();
    TestIconMessages();
    TestController();
    TestWatchdog();
    TestSafetyAndOff();
    TestOneRestorePath();
    TestFrameRate();
    TestChatPlacement();
    TestTrayFriends();
    TestOneChatView();
    std::printf("launcher_tray: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
