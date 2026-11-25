#pragma once
// The launcher's system-tray behaviour (Settings > Behavior "Minimize to tray" / "Close to tray"), with no window system
// in it: what the X button, minimize and the tray icon do, the tray menu and what each item does, re-adding the icon
// after Explorer restarts, and how slowly the loop runs while the launcher is hidden. The Win32 calls sit behind
// TrayShell (launcher/TrayWin32.cpp on Windows; a fake in tests/test_launcher_tray.cpp), so all of this is tested on
// Linux. Written from the owner's request (2026-11-25) and Microsoft's Shell_NotifyIcon documentation for the message
// values; no outside code.
//
// Safety rule kept from when hide-to-tray was removed (it could trap the window): the launcher never hides unless its
// tray icon is really there, so there is always a way back (the icon, or starting the launcher again).
#include <cstdint>
#include <string>
#include <vector>

namespace okaytray {

/// The two options on Settings > Behavior (both on by default).
struct Settings {
    bool minimizeToTray = true;
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

/// The tray menu's commands. The numbers are the menu item ids (never 0: TrackPopupMenu returns 0 for "nothing").
enum class MenuCmd : int { None = 0, Open = 1, CheckUpdates = 2, Settings = 3, ChatIn = 4, Exit = 5 };
struct MenuItem {
    MenuCmd cmd;
    std::string label;
    bool separatorBefore;
    bool isDefault;   // drawn bold: what a double-click does
};
/// The menu, top to bottom. "Put chat back in the launcher" only while the chat is popped out in its own window.
inline std::vector<MenuItem> Menu(bool chatPoppedOut) {
    std::vector<MenuItem> m;
    m.push_back({MenuCmd::Open, "Open Launcher", false, true});
    if (chatPoppedOut) m.push_back({MenuCmd::ChatIn, "Put chat back in the launcher", false, false});
    m.push_back({MenuCmd::CheckUpdates, "Check for Updates", true, false});
    m.push_back({MenuCmd::Settings, "Settings", false, false});
    m.push_back({MenuCmd::Exit, "Exit", true, false});
    return m;
}
/// What a menu command asks the launcher to do (the main loop carries it out).
struct Effect {
    bool show = false;           // bring the launcher window back
    bool quit = false;           // end the launcher
    bool checkUpdates = false;   // run the update check (the same one as the sidebar's button)
    bool openSettings = false;   // open the Settings page (Behavior)
    bool chatIn = false;         // put a popped-out chat back inside the launcher
};
inline Effect EffectOf(MenuCmd c) {
    Effect e;
    switch (c) {
        case MenuCmd::Open:         e.show = true; break;
        case MenuCmd::CheckUpdates: e.show = true; e.checkUpdates = true; break;
        case MenuCmd::Settings:     e.show = true; e.openSettings = true; break;
        case MenuCmd::ChatIn:       e.show = true; e.chatIn = true; break;
        case MenuCmd::Exit:         e.quit = true; break;
        case MenuCmd::None:         break;
    }
    return e;
}

/// Messages the tray icon sends (the low word of lParam of the callback message, NOTIFYICON_VERSION 3), from
/// Microsoft's documentation: WM_CONTEXTMENU 0x007B, WM_LBUTTONUP 0x0202, WM_LBUTTONDBLCLK 0x0203, WM_RBUTTONUP 0x0205,
/// NIN_SELECT = WM_USER 0x0400, NIN_KEYSELECT = WM_USER + 1.
constexpr unsigned kWmContextMenu = 0x007B, kWmLButtonUp = 0x0202, kWmLButtonDblClk = 0x0203, kWmRButtonUp = 0x0205;
constexpr unsigned kNinSelect = 0x0400, kNinKeySelect = 0x0401;
enum class IconEvent { None, Restore, ShowMenu };
/// Double-click (or Enter / Space on the icon with the keyboard) restores; right-click (or the menu key) shows the menu.
/// A single left click does nothing, so it cannot be mistaken for the first half of a double-click.
inline IconEvent ClassifyIconMessage(unsigned ev) {
    switch (ev) {
        case kWmLButtonDblClk: case kNinKeySelect: return IconEvent::Restore;
        case kWmRButtonUp: case kWmContextMenu:    return IconEvent::ShowMenu;
        default:                                   return IconEvent::None;
    }
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

/// The Win32 side (launcher/TrayWin32.cpp), or a fake.
class TrayShell {
public:
    virtual ~TrayShell() = default;
    virtual bool AddIcon(const std::string& tip) = 0;   // Shell_NotifyIconW(NIM_ADD); false = refused
    virtual void RemoveIcon() = 0;                      // Shell_NotifyIconW(NIM_DELETE)
    virtual void HideWindow() = 0;                      // ShowWindow(SW_HIDE): off the task bar and Alt+Tab
    virtual void ShowWindow() = 0;                      // SW_SHOW + SW_RESTORE + SetForegroundWindow
    virtual MenuCmd RunMenu(const std::vector<MenuItem>& items) = 0;   // TrackPopupMenu; MenuCmd::None if dismissed
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
    /// The window was just minimized. True when it went to the tray.
    bool OnMinimized() { return HideOnMinimize(settings, m_supported) && HideToTray(); }
    /// Hide the window to the tray. False (and nothing hidden) when the icon cannot be shown.
    bool HideToTray() {
        if (!m_supported) return false;
        if (m_hidden) return true;
        if (!AddIcon()) return false;
        m_shell->HideWindow();
        m_hidden = true;
        if (!hintShown) {
            m_shell->Balloon("OkaySpace", "OkaySpace is still running in the tray. Double-click the icon to open it, "
                                          "or right-click it to exit.");
            hintShown = true; m_hintJustShown = true;
        }
        return true;
    }
    /// Bring the window back (double-click, Open Launcher, a second launch of the program).
    void Restore() {
        if (!m_supported) return;
        m_shell->ShowWindow();
        m_hidden = false;
        if (!WantIcon(settings, m_supported, m_hidden)) RemoveIcon();
    }
    /// A message from the tray icon (the low word of lParam). Returns what the launcher must do next.
    Effect OnIconMessage(unsigned ev, bool chatPoppedOut) {
        Effect e;
        switch (ClassifyIconMessage(ev)) {
            case IconEvent::Restore:  e.show = true; break;
            case IconEvent::ShowMenu: e = EffectOf(m_shell->RunMenu(Menu(chatPoppedOut))); break;
            case IconEvent::None:     break;
        }
        if (e.show) Restore();
        return e;
    }
    /// Explorer restarted (the "TaskbarCreated" message): every icon was lost, so add ours again.
    void OnTaskbarCreated() {
        if (!m_icon) return;
        m_icon = false;
        AddIcon();
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
};

} // namespace okaytray
