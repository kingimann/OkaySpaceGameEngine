#pragma once
// The launcher's system-tray behaviour (Settings > Behavior "Minimize to tray" / "Close to tray"), with no window system
// in it: what the X button, minimize and the tray icon do, the tray menu and what each item does, re-adding the icon
// after Explorer restarts, the watchdog that brings the window back when the icon is lost, and how slowly the loop runs
// while the launcher is hidden. The Win32 calls sit behind TrayShell (launcher/TrayWin32.cpp on Windows; a fake in
// tests/test_launcher_tray.cpp), so all of this is tested on Linux. Written from the owner's requests (2026-11-25) and
// Microsoft's Shell_NotifyIcon / NOTIFYICONDATA documentation for the message values and the NOTIFYICON_VERSION_4
// message layout; no outside code.
//
// Safety rules:
//  - the launcher never hides unless its tray icon is really there, so there is always a way back (the icon, or
//    starting the launcher again);
//  - "Minimize to tray" is OFF by default (owner bug 2026-11-25: "when i minimize it i cant open it back up"): the "_"
//    button is a normal task-bar minimize unless the person turns the option on;
//  - while hidden, the watchdog checks the icon; when it is gone and cannot be added again, the window comes back.
#include "FriendsList.hpp"   // your own status (Online / Away / Invisible / Do not disturb)
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace okaytray {

/// The two options on Settings > Behavior. Close to tray is on by default; minimize to tray is off.
struct Settings {
    bool minimizeToTray = false;
    bool closeToTray = true;
};

/// Where a request to close came from.
enum class CloseSource {
    WindowX,        // the title bar's X, Alt+F4, or the app being asked to quit by its last window closing
    WindowXShift,   // the same with Shift held: really quit
    TrayExit,       // Exit in the tray menu
};
enum class CloseAction {
    HideToTray,     // hide the window and keep running
    AskConfirm,     // the launcher's "Exit or minimize?" dialog (the behaviour without the tray)
    Quit,           // end the launcher (the editor-safe quit path)
};
/// What a close request does. `traySupported` is false off Windows (and when the shell refused the icon).
inline CloseAction DecideClose(const Settings& s, bool traySupported, CloseSource src) {
    if (src == CloseSource::TrayExit || src == CloseSource::WindowXShift) return CloseAction::Quit;
    if (traySupported && s.closeToTray) return CloseAction::HideToTray;
    return CloseAction::AskConfirm;
}
/// Whether a minimize goes to the tray (else it stays a normal task-bar minimize).
inline bool HideOnMinimize(const Settings& s, bool traySupported) { return traySupported && s.minimizeToTray; }
/// The tray icon is there while either option is on (so the menu can always reach the launcher), and while hidden.
inline bool WantIcon(const Settings& s, bool traySupported, bool hidden) {
    return traySupported && (hidden || s.minimizeToTray || s.closeToTray);
}

// ---- The tray menu ------------------------------------------------------------------------------------------------
/// Menu item ids (never 0: TrackPopupMenu returns 0 for "nothing chosen"). Recent projects are kRecentBase + index,
/// the online statuses kStatusBase + status (0 Online, 1 Away, 2 Invisible, 3 Do not disturb: the launcher's
/// g_onlineStatus, okayfriends::MyStatusLabel).
enum MenuId : int {
    kIdNone = 0, kIdOpen = 1, kIdCheckUpdates = 2, kIdSettings = 3, kIdChatIn = 4, kIdExit = 5,
    kIdOpenEditor = 6, kIdNotifications = 7, kIdDownloads = 8,
    kIdFriends = 9,          // Friends: the friends list inside the launcher (the launcher comes back)
    kIdFriendsWindow = 10,   // Friends: the friends list in its own window (the launcher stays where it is)
    kRecentBase = 100, kStatusBase = 200,
};
constexpr int kMaxRecent = 5;
constexpr int kStatusCount = okayfriends::kMyStatusCount;
inline const char* StatusLabel(int s) { return okayfriends::MyStatusLabel(s); }

struct MenuItem {
    int id = kIdNone;             // 0 for a submenu's title
    std::string label;
    bool separatorBefore = false;
    bool isDefault = false;       // drawn bold: what a click on the icon does
    bool enabled = true;
    bool checked = false;
    std::vector<MenuItem> sub;    // non-empty: this item opens a submenu
    bool radio = false;           // one of a group of choices (drawn with a round bullet when checked)
};
/// What the menu shows, read from the launcher when the menu opens.
struct MenuState {
    bool chatPoppedOut = false;
    bool editorAvailable = false;            // the editor program was found
    std::vector<std::string> recentProjects; // names, most recent first (MostRecentProjects)
    bool signedIn = false;
    int onlineStatus = 0;                    // 0 Online, 1 Away, 2 Invisible, 3 Do not disturb
    int unreadNotifications = 0;
    int activeDownloads = 0;
    // The Friends item (FriendsMenuItem).
    bool friendsInOwnWindow = false;         // the friends window was last used popped out (the chat_window preference)
    int friendsOnline = 0;                   // friends online now
    int unreadChats = 0;                     // conversations with unread messages
};
inline MenuItem Item(int id, const std::string& label, bool sepBefore = false, bool enabled = true) {
    MenuItem m; m.id = id; m.label = label; m.separatorBefore = sepBefore; m.enabled = enabled; return m;
}
// ---- The Friends item (owner, 2026-11-25: "i dont see it in right click icon tray") ------------------------------
/// "Friends (2 unread)" / "Friends (3 online)": opens the friends window where it was last used, popped out or inside
/// the launcher (a popped-out window is shown and raised without bringing the launcher back). Greyed out signed out.
inline MenuItem FriendsMenuItem(const MenuState& st) {
    std::string label = "Friends";
    if (st.signedIn && st.unreadChats > 0) label += " (" + std::to_string(st.unreadChats) + " unread)";
    else if (st.signedIn && st.friendsOnline > 0) label += " (" + std::to_string(st.friendsOnline) + " online)";
    MenuItem m = Item(st.friendsInOwnWindow ? kIdFriendsWindow : kIdFriends, label, true, st.signedIn);
    return m;
}

/// The menu, top to bottom (owner, 2026-11-25: "add right click options for the tray icon").
inline std::vector<MenuItem> BuildMenu(const MenuState& st) {
    std::vector<MenuItem> m;
    MenuItem open = Item(kIdOpen, "Open Launcher"); open.isDefault = true;
    m.push_back(open);
    const bool haveProject = !st.recentProjects.empty();
    m.push_back(Item(kIdOpenEditor, haveProject ? "Open Editor (" + st.recentProjects[0] + ")" : std::string("Open Editor"),
                     false, haveProject && st.editorAvailable));
    MenuItem recent = Item(kIdNone, "Recent projects", false, haveProject && st.editorAvailable);
    for (int i = 0; i < (int)st.recentProjects.size() && i < kMaxRecent; ++i)
        recent.sub.push_back(Item(kRecentBase + i, st.recentProjects[(size_t)i]));
    if (recent.sub.empty()) recent.sub.push_back(Item(kIdNone, "No projects yet", false, false));
    m.push_back(recent);
    m.push_back(FriendsMenuItem(st));
    if (st.chatPoppedOut) m.push_back(Item(kIdChatIn, "Put chat back in the launcher"));
    MenuItem status = Item(kIdNone, st.signedIn ? std::string("Status: ") + StatusLabel(st.onlineStatus) : std::string("Status"),
                           true, st.signedIn);
    for (int s = 0; s < kStatusCount; ++s) {
        MenuItem it = Item(kStatusBase + s, StatusLabel(s));
        it.checked = st.signedIn && st.onlineStatus == s;
        it.radio = true;
        status.sub.push_back(it);
    }
    m.push_back(status);
    m.push_back(Item(kIdNotifications, st.unreadNotifications > 0
                     ? "Notifications (" + std::to_string(st.unreadNotifications) + " unread)" : std::string("Notifications")));
    m.push_back(Item(kIdDownloads, st.activeDownloads > 0
                     ? "Downloads (" + std::to_string(st.activeDownloads) + " active)" : std::string("Downloads")));
    m.push_back(Item(kIdCheckUpdates, "Check for Updates"));
    m.push_back(Item(kIdSettings, "Settings", true));
    m.push_back(Item(kIdExit, "Exit OkaySpace"));
    return m;
}
/// Whether `id` is an enabled command somewhere in `items` (only those ids are acted on).
inline bool HasEnabledId(const std::vector<MenuItem>& items, int id) {
    if (id == kIdNone) return false;
    for (const MenuItem& it : items) {
        if (!it.enabled) continue;
        if (it.id == id && it.sub.empty()) return true;
        if (HasEnabledId(it.sub, id)) return true;
    }
    return false;
}

/// What a menu command asks the launcher to do (the main loop carries it out with the launcher's existing actions).
struct Effect {
    bool show = false;              // bring the launcher window back
    bool quit = false;              // end the launcher
    bool checkUpdates = false;      // run the update check (the same one as the sidebar's button)
    bool openSettings = false;      // open the Settings page (Behavior)
    bool chatIn = false;            // put a popped-out chat back inside the launcher
    bool openNotifications = false; // the Notifications page
    bool openDownloads = false;     // the Downloads page
    int openProject = -1;           // open this recent project (index into MenuState::recentProjects) in the editor
    int setStatus = -1;             // set the online status (0 Online, 1 Away, 2 Invisible, 3 Do not disturb)
    bool openFriends = false;       // open the friends window (the chat window's Friends list)
    bool friendsOwnWindow = false;  // ...in its own window (else inside the launcher)
};
/// The Friends item's effect (kept apart from EffectOf's switch).
inline bool FriendsEffect(int id, Effect& e) {
    if (id == kIdFriends) { e.show = true; e.openFriends = true; return true; }
    if (id == kIdFriendsWindow) { e.openFriends = true; e.friendsOwnWindow = true; return true; }   // the launcher stays hidden
    return false;
}
inline Effect EffectOf(int id) {
    Effect e;
    if (id >= kRecentBase && id < kRecentBase + kMaxRecent) { e.show = true; e.openProject = id - kRecentBase; return e; }
    if (id >= kStatusBase && id < kStatusBase + kStatusCount) { e.setStatus = id - kStatusBase; return e; }   // stays where it is
    if (FriendsEffect(id, e)) return e;
    switch (id) {
        case kIdOpen:          e.show = true; break;
        case kIdOpenEditor:    e.show = true; e.openProject = 0; break;
        case kIdCheckUpdates:  e.show = true; e.checkUpdates = true; break;
        case kIdSettings:      e.show = true; e.openSettings = true; break;
        case kIdChatIn:        e.show = true; e.chatIn = true; break;
        case kIdNotifications: e.show = true; e.openNotifications = true; break;
        case kIdDownloads:     e.show = true; e.openDownloads = true; break;
        case kIdExit:          e.quit = true; break;
        default:               break;
    }
    return e;
}

/// One project for the "Recent projects" list.
struct ProjectEntry {
    std::string name, scene;
    long long mtime = 0;   // the scene file's last write time (0 = missing: left out)
};
/// The `n` most recently changed projects, newest first (ties keep the given order).
inline std::vector<ProjectEntry> MostRecentProjects(std::vector<ProjectEntry> all, size_t n = kMaxRecent) {
    all.erase(std::remove_if(all.begin(), all.end(), [](const ProjectEntry& p) { return p.mtime == 0 || p.scene.empty(); }), all.end());
    std::stable_sort(all.begin(), all.end(), [](const ProjectEntry& a, const ProjectEntry& b) { return a.mtime > b.mtime; });
    if (all.size() > n) all.resize(n);
    return all;
}

// ---- Tray icon messages ---------------------------------------------------------------------------------------------
/// Messages the tray icon sends, from Microsoft's documentation: WM_CONTEXTMENU 0x007B, WM_MOUSEMOVE 0x0200,
/// WM_LBUTTONDOWN 0x0201, WM_LBUTTONUP 0x0202, WM_LBUTTONDBLCLK 0x0203, WM_RBUTTONDOWN 0x0204, WM_RBUTTONUP 0x0205,
/// NIN_SELECT = WM_USER 0x0400, NIN_KEYSELECT = WM_USER + 1.
constexpr unsigned kWmContextMenu = 0x007B, kWmMouseMove = 0x0200, kWmLButtonDown = 0x0201, kWmLButtonUp = 0x0202,
                   kWmLButtonDblClk = 0x0203, kWmRButtonDown = 0x0204, kWmRButtonUp = 0x0205;
constexpr unsigned kNinSelect = 0x0400, kNinKeySelect = 0x0401;
/// NIM_SETVERSION values: 0 = the old layout, 3 = NOTIFYICON_VERSION, 4 = NOTIFYICON_VERSION_4.
constexpr unsigned kIconVersionOld = 0, kIconVersion3 = 3, kIconVersion4 = 4;

/// The callback message's two parameters, decoded.
struct IconMsg {
    unsigned event = 0;    // the mouse / NIN_ message
    unsigned iconId = 0;   // our uID
    int x = 0, y = 0;      // the anchor point (version 4 only; else 0)
    bool hasPoint = false;
};
/// Version 4: LOWORD(lParam) is the event, HIWORD(lParam) the icon id, and wParam holds the anchor point (signed x in
/// the low word, signed y in the high word). Older versions: lParam is the event and wParam the icon id.
inline IconMsg DecodeIconMessage(unsigned version, uint64_t wParam, int64_t lParam) {
    IconMsg m;
    const uint32_t lp = (uint32_t)(uint64_t)lParam;
    if (version >= kIconVersion4) {
        m.event = lp & 0xFFFFu;
        m.iconId = (lp >> 16) & 0xFFFFu;
        m.x = (int)(int16_t)(uint16_t)(wParam & 0xFFFFu);
        m.y = (int)(int16_t)(uint16_t)((wParam >> 16) & 0xFFFFu);
        m.hasPoint = true;
    } else {
        m.event = lp & 0xFFFFu;   // the whole lParam is the message; it always fits a word
        m.iconId = (unsigned)(wParam & 0xFFFFFFFFu);
    }
    return m;
}
enum class IconEvent { None, Restore, ShowMenu };
/// Any left click (single or double) or Enter / Space on the icon restores; right-click (or the menu key) shows the
/// menu. Version 4 sends NIN_SELECT for a left click and WM_CONTEXTMENU for a right click, each after the raw button
/// messages, so only those count there (one action per click); older versions send the raw button messages.
inline IconEvent ClassifyIconMessage(unsigned ev, unsigned version) {
    if (version >= kIconVersion4) {
        switch (ev) {
            case kNinSelect: case kNinKeySelect: case kWmLButtonDblClk: return IconEvent::Restore;
            case kWmContextMenu:                                       return IconEvent::ShowMenu;
            default:                                                   return IconEvent::None;
        }
    }
    switch (ev) {
        case kWmLButtonUp: case kWmLButtonDblClk: case kNinSelect: case kNinKeySelect: return IconEvent::Restore;
        case kWmRButtonUp: case kWmContextMenu:                                       return IconEvent::ShowMenu;
        default:                                                                      return IconEvent::None;
    }
}

/// A launcher dialog has just started waiting ("Close the editor first" after Exit OkaySpace in the tray menu, or an
/// update that waits for the editor). A launcher hidden in the tray or minimized draws nothing, so the dialog waited
/// where nobody could see it and the launcher stayed running with the editor still open (owner bug 2026-11-25: "if i
/// quit the launcher from the tray, the editor doesnt close"). True = bring the window back once, so the dialog shows.
/// Only on the edge (`pendingBefore` false), so a person who minimizes it again while it waits is left alone.
inline bool ShowForPendingDialog(bool pendingNow, bool pendingBefore, bool windowBackgrounded) {
    return pendingNow && !pendingBefore && windowBackgrounded;
}

/// How long the main loop sleeps per pass while the launcher window is not drawn. Hidden in the tray is the slowest,
/// unless a voice call is live (its audio is pumped by the loop) or the popped-out chat window is drawing.
inline int IdleDelayMs(bool hiddenInTray, bool chatWindowShown, bool chatFocused, bool callActive) {
    if (chatWindowShown) return chatFocused ? 15 : 45;   // the chat window keeps its own frame rate
    if (callActive) return 20;
    return hiddenInTray ? 250 : 120;                     // (120 ms = a normal minimize, as before)
}
/// The frame rate a hidden launcher runs its pumps at (the frame-rate drop while hidden, for tests and the docs).
inline int IdleFramesPerSecond(int delayMs) { return delayMs > 0 ? 1000 / delayMs : 0; }
/// How often (ms) the watchdog checks that the icon is still there while the launcher is hidden.
constexpr int kWatchdogMs = 1000;

/// The Win32 side (launcher/TrayWin32.cpp), or a fake.
class TrayShell {
public:
    virtual ~TrayShell() = default;
    virtual bool AddIcon(const std::string& tip) = 0;   // Shell_NotifyIconW(NIM_ADD); false = refused
    virtual void RemoveIcon() = 0;                      // Shell_NotifyIconW(NIM_DELETE)
    virtual bool IconAlive() = 0;                       // the icon is still in the tray (NIM_MODIFY succeeds)
    virtual void HideWindow() = 0;                      // ShowWindow(SW_HIDE): off the task bar and Alt+Tab
    virtual void ShowWindow() = 0;                      // SW_SHOW, SW_RESTORE if minimized, SetForegroundWindow
    virtual int RunMenu(const std::vector<MenuItem>& items) = 0;   // TrackPopupMenu; the chosen id, kIdNone if dismissed
    virtual void Balloon(const std::string& title, const std::string& text) = 0;   // NIF_INFO notification
};

/// The tray state machine. One per launcher; every call is made on the main thread.
class TrayController {
public:
    TrayController(TrayShell* shell, bool supported) : m_shell(shell), m_supported(supported && shell) {}

    Settings settings;
    bool hintShown = false;   // the "still running in the tray" note was shown once (saved with the preferences)

    bool Supported() const { return m_supported; }
    bool Hidden() const { return m_hidden; }
    bool IconShown() const { return m_icon; }
    int Restores() const { return m_restores; }   // how many times Restore ran (tests)

    /// Add or remove the icon to match the settings (call after loading or changing them).
    void Sync() {
        if (WantIcon(settings, m_supported, m_hidden)) AddIcon();
        else RemoveIcon();
    }
    /// The X button (or Alt+F4). Returns what the launcher must do; HideToTray has already been done here (or, when
    /// the icon could not be added, AskConfirm is returned so the window never disappears with no way back).
    CloseAction OnClose(CloseSource src) {
        CloseAction a = DecideClose(settings, m_supported, src);
        if (a == CloseAction::HideToTray && !HideToTray()) a = CloseAction::AskConfirm;
        return a;
    }
    /// The window was just minimized. True when it went to the tray (only with "Minimize to tray" on).
    bool OnMinimized() { return HideOnMinimize(settings, m_supported) && HideToTray(); }
    /// Hide the window to the tray. False (and nothing hidden) when the icon cannot be shown.
    bool HideToTray() {
        if (!m_supported) return false;
        if (m_hidden) return true;
        if (!AddIcon()) return false;
        m_shell->HideWindow();
        m_hidden = true;
        if (!hintShown) {
            m_shell->Balloon("OkaySpace", "OkaySpace is still running. Click its icon in the notification area by the "
                                          "clock (it may be under the ^ arrow) to open it, or start OkaySpace again.");
            hintShown = true; m_hintJustShown = true;
        }
        return true;
    }
    /// THE way back, used by every trigger: a click on the icon, Open Launcher (and every menu item that shows the
    /// launcher), a second launch of the program, a game asking us to come forward, and the watchdog. It shows the
    /// window even when it is only minimized, and clears the hidden state, so the main loop draws at full speed again.
    void Restore() {
        ++m_restores;
        m_hidden = false;
        if (!m_supported) return;
        m_shell->ShowWindow();
        if (!WantIcon(settings, m_supported, m_hidden)) RemoveIcon();
    }
    /// A second launch of the launcher asked this one to come forward.
    void OnSecondLaunch() { Restore(); }
    /// A decoded tray-icon event. Returns what the launcher must do next (the window is already back when `show`).
    Effect OnIconEvent(IconEvent ev, const MenuState& st) {
        Effect e;
        switch (ev) {
            case IconEvent::Restore:  e.show = true; break;
            case IconEvent::ShowMenu: {
                const std::vector<MenuItem> items = BuildMenu(st);
                const int id = m_supported ? m_shell->RunMenu(items) : (int)kIdNone;
                if (HasEnabledId(items, id)) e = EffectOf(id);
                break;
            }
            case IconEvent::None:     break;
        }
        if (e.show) Restore();
        return e;
    }
    /// A raw callback message (decoded with the icon's version).
    Effect OnIconMessage(unsigned version, uint64_t wParam, int64_t lParam, const MenuState& st) {
        return OnIconEvent(ClassifyIconMessage(DecodeIconMessage(version, wParam, lParam).event, version), st);
    }
    /// Explorer restarted (the "TaskbarCreated" message): every icon was lost, so add ours again (and if that fails
    /// while hidden, come back).
    void OnTaskbarCreated() {
        if (!m_icon && !m_hidden) return;
        m_icon = false;
        if (!AddIcon() && m_hidden) Restore();
    }
    /// Call about once a second while hidden (kWatchdogMs). When the icon is gone (Explorer crashed, the shell dropped
    /// it) and cannot be added again, the window comes back at once, so it is never hidden with no way back. True when
    /// it restored the window.
    bool Watchdog() {
        if (!m_hidden) return false;
        if (!m_supported) { Restore(); return true; }
        if (m_icon && m_shell->IconAlive()) return false;
        m_icon = false;
        if (AddIcon()) return false;
        Restore();
        return true;
    }
    /// The launcher is ending (normally or from a crash handler): take the icon away.
    void Shutdown() { RemoveIcon(); }
    /// True once, right after the first-hide note was shown (so the launcher saves its preferences).
    bool TakeHintJustShown() { const bool v = m_hintJustShown; m_hintJustShown = false; return v; }

private:
    bool AddIcon() {
        if (m_icon) return true;
        if (!m_supported) return false;
        m_icon = m_shell->AddIcon("OkaySpace Launcher");
        return m_icon;
    }
    void RemoveIcon() {
        if (!m_icon) return;
        m_shell->RemoveIcon();
        m_icon = false;
    }
    TrayShell* m_shell;
    bool m_supported;
    bool m_hidden = false, m_icon = false, m_hintJustShown = false;
    int m_restores = 0;
};

} // namespace okaytray
