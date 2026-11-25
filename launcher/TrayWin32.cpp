// The launcher's tray icon on Windows (launcher/TrayWin32.hpp). Written from Microsoft's documentation of
// Shell_NotifyIconW / NOTIFYICONDATAW (NIM_ADD, NIM_SETVERSION with NOTIFYICON_VERSION_4, NIM_MODIFY with NIF_INFO,
// NIM_DELETE), RegisterClassExW / CreateWindowExW, TrackPopupMenu (with SetForegroundWindow first and a WM_NULL after,
// as the TrackPopupMenu remarks ask), ShowWindow, RegisterWindowMessage("TaskbarCreated") and
// SetUnhandledExceptionFilter. No outside code. Only the decisions live in TrayLogic.hpp; this file just makes the calls.
//
// Why the icon has its own window (owner bug 2026-11-25, "when i minimize it i cant open it back up"): the icon used to
// belong to SDL's window, with SDL_SetWindowsMessageHook to see its clicks. That hook is only called for messages SDL's
// PeekMessage loop takes off the queue (posted messages); a message another process SENDS (the shell's notify-icon
// callback is delivered that way) goes straight to SDL's window procedure, which drops unknown messages. So the clicks
// never reached the launcher. Our own hidden top-level window gets every message in its own window procedure: sent ones
// while any PeekMessage on this thread runs (SDL's or the own backend's pump), posted ones through their
// DispatchMessage, and broadcasts (TaskbarCreated, the second-launch message) because it is top-level, not message-only.
// Unverified: compiled with MinGW here, never run on Windows.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include "TrayWin32.hpp"
#include <cstdlib>
#include <exception>
#include <string>

namespace okaytray {
namespace {

constexpr UINT kUid = 1;
constexpr UINT kCallback = WM_USER + 0x51;
const wchar_t kClassName[] = L"OkaySpaceLauncherTray";
HWND g_iconHwnd = nullptr;   // the window that owns a live icon (for the crash clean-up); null when none
UINT g_version = 0;          // the NIM_SETVERSION the shell accepted
MessageSink g_sink = nullptr;
bool g_inMenu = false;       // icon messages that arrive while the menu is open are dropped (no second menu)
LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;
std::terminate_handler g_prevTerminate = nullptr;

std::wstring Wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
/// A menu label: '&' would underline the next letter, so a project called "A&B" is written "A&&B".
std::wstring MenuLabel(const std::string& s) {
    std::wstring w = Wide(s), out;
    for (wchar_t c : w) { out += c; if (c == L'&') out += L'&'; }
    return out;
}
template <size_t N> void CopyW(wchar_t (&dst)[N], const std::wstring& src) {
    size_t i = 0;
    for (; i + 1 < N && i < src.size(); ++i) dst[i] = src[i];
    dst[i] = L'\0';
}
void BaseData(NOTIFYICONDATAW& nid, HWND h) {
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid); nid.hWnd = h; nid.uID = kUid;
}
void DeleteIcon(HWND h) {
    if (!h) return;
    NOTIFYICONDATAW nid; BaseData(nid, h);
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

LRESULT CALLBACK TrayWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == kCallback) {
        if (g_sink && !g_inMenu) g_sink((unsigned)m, (uint64_t)wp, (int64_t)lp);
        return 0;
    }
    if (m >= 0xC000 && m <= 0xFFFF) {   // registered messages: TaskbarCreated, the second-launch message
        if (g_sink) g_sink((unsigned)m, (uint64_t)wp, (int64_t)lp);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

HWND MakeTrayWindow() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    // Top-level (so broadcasts reach it), never shown, a tool window (never on the task bar or in Alt+Tab).
    return CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"OkaySpace Launcher tray", WS_POPUP,
                           0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
}

HMENU BuildHMenu(const std::vector<MenuItem>& items) {
    HMENU m = CreatePopupMenu();
    if (!m) return nullptr;
    for (const MenuItem& it : items) {
        if (it.separatorBefore && GetMenuItemCount(m) > 0) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        UINT flags = MF_STRING | (it.enabled ? MF_ENABLED : MF_GRAYED) | (it.checked ? MF_CHECKED : MF_UNCHECKED);
        if (!it.sub.empty()) {
            HMENU sub = BuildHMenu(it.sub);
            if (!sub) continue;
            AppendMenuW(m, flags | MF_POPUP, (UINT_PTR)sub, MenuLabel(it.label).c_str());   // destroyed with its parent
        } else {
            AppendMenuW(m, flags, (UINT_PTR)(UINT)it.id, MenuLabel(it.label).c_str());
        }
        if (it.isDefault && it.id != kIdNone) SetMenuDefaultItem(m, (UINT)it.id, FALSE);
    }
    return m;
}

class Win32Shell final : public TrayShell {
public:
    Win32Shell(HWND mainWnd, HWND trayWnd) : m_main(mainWnd), m_tray(trayWnd) {}
    ~Win32Shell() override {
        RemoveIcon();
        if (m_icon) DestroyIcon(m_icon);
        if (m_bigIcon) DestroyIcon(m_bigIcon);
        if (m_tray) DestroyWindow(m_tray);
    }

    bool AddIcon(const std::string& tip) override {
        if (!m_tray) return false;
        if (!m_icon)   // the exe's icon (resource 1, launcher/okayspace.rc) at the small-icon size
            m_icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                       GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        m_tip = Wide(tip);
        NOTIFYICONDATAW nid; BaseData(nid, m_tray);
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;   // NIF_SHOWTIP: version 4 hides the tip without it
        nid.uCallbackMessage = kCallback;
        nid.hIcon = m_icon ? m_icon : LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
        CopyW(nid.szTip, m_tip);
        if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
            // An icon with our id may still be there (a crash, an earlier add): replace it instead.
            if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) return false;
        }
        nid.uVersion = NOTIFYICON_VERSION_4;   // LOWORD(lParam) = the event; NIN_SELECT / WM_CONTEXTMENU for clicks
        g_version = Shell_NotifyIconW(NIM_SETVERSION, &nid) ? 4u : 0u;
        g_iconHwnd = m_tray;
        return true;
    }
    void RemoveIcon() override {
        DeleteIcon(m_tray);
        if (g_iconHwnd == m_tray) g_iconHwnd = nullptr;
    }
    bool IconAlive() override {
        if (!m_tray || g_iconHwnd != m_tray) return false;
        NOTIFYICONDATAW nid; BaseData(nid, m_tray);
        nid.uFlags = NIF_TIP | NIF_SHOWTIP;   // set the same tip again: fails only when the icon is gone
        CopyW(nid.szTip, m_tip);
        return Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
    }
    void HideWindow() override { if (m_main) ::ShowWindow(m_main, SW_HIDE); }
    void ShowWindow() override {
        if (!m_main) return;
        if (!IsWindowVisible(m_main)) ::ShowWindow(m_main, SW_SHOW);
        if (IsIconic(m_main)) ::ShowWindow(m_main, SW_RESTORE);
        BringWindowToTop(m_main);
        SetForegroundWindow(m_main);
    }
    int RunMenu(const std::vector<MenuItem>& items) override {
        if (!m_tray) return kIdNone;
        HMENU m = BuildHMenu(items);
        if (!m) return kIdOpen;   // no menu: at least bring the launcher back
        POINT pt; GetCursorPos(&pt);
        SetForegroundWindow(m_tray);   // so the menu closes when the person clicks elsewhere
        g_inMenu = true;
        const int cmd = (int)TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN,
                                            pt.x, pt.y, 0, m_tray, nullptr);
        g_inMenu = false;
        PostMessageW(m_tray, WM_NULL, 0, 0);
        DestroyMenu(m);
        return cmd;   // TrayController only acts on an id that is an enabled item of `items`
    }
    void Balloon(const std::string& title, const std::string& text) override {
        if (!m_tray || g_iconHwnd != m_tray) return;
        NOTIFYICONDATAW nid; BaseData(nid, m_tray);
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_INFO;
        CopyW(nid.szInfoTitle, Wide(title));
        CopyW(nid.szInfo, Wide(text));
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    // A desktop notification (launcher/DesktopNotify.hpp decides; main.cpp queues them, one at a time): the balloon
    // with the app's own icon (NIIF_USER with hBalloonIcon, the large size when the SDK has it), quiet when the sound
    // setting is off (NIIF_NOSOUND), and held back by Windows during quiet time / focus assist (NIIF_RESPECT_QUIET_TIME).
    // Windows 10 and 11 show it as a notification in the bottom-right corner and keep it in the Action Center; a click
    // comes back as NIN_BALLOONUSERCLICK in the icon's callback message.
    bool Notify(const std::string& title, const std::string& text, bool sound) override {
        if (!m_tray || g_iconHwnd != m_tray) return false;
        NOTIFYICONDATAW nid; BaseData(nid, m_tray);
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_USER | (sound ? 0u : (DWORD)NIIF_NOSOUND);
#if defined(NIIF_RESPECT_QUIET_TIME)
        nid.dwInfoFlags |= NIIF_RESPECT_QUIET_TIME;
#endif
#if defined(NIIF_LARGE_ICON)
        if (!m_bigIcon)
            m_bigIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
        nid.hBalloonIcon = m_bigIcon ? m_bigIcon : m_icon;
        if (m_bigIcon) nid.dwInfoFlags |= NIIF_LARGE_ICON;
        if (!nid.hBalloonIcon) nid.dwInfoFlags = (nid.dwInfoFlags & ~(DWORD)(NIIF_USER | NIIF_LARGE_ICON)) | NIIF_INFO;
#else
        nid.dwInfoFlags = (nid.dwInfoFlags & ~(DWORD)NIIF_USER) | NIIF_INFO;   // no hBalloonIcon in this SDK: the info icon
#endif
        CopyW(nid.szInfoTitle, Wide(title));
        CopyW(nid.szInfo, Wide(text.empty() ? std::string(" ") : text));   // an empty szInfo removes the balloon
        return Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
    }

private:
    HWND m_main;   // the launcher's window (shown and hidden)
    HWND m_tray;   // our hidden window: owns the icon and the menu, receives the messages
    HICON m_icon = nullptr;
    HICON m_bigIcon = nullptr;   // the balloon's icon (desktop notifications)
    std::wstring m_tip;
};

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    DeleteIcon(g_iconHwnd);   // no ghost icon left in the tray after a crash
    g_iconHwnd = nullptr;
    return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}
void CrashTerminate() {
    DeleteIcon(g_iconHwnd);
    g_iconHwnd = nullptr;
    if (g_prevTerminate) g_prevTerminate();
    std::abort();
}

} // namespace

std::unique_ptr<TrayShell> MakeWin32Shell(void* mainHwnd, MessageSink sink) {
    if (!mainHwnd) return nullptr;
    HWND tray = MakeTrayWindow();
    if (!tray) return nullptr;
    g_sink = sink;
    return std::unique_ptr<TrayShell>(new Win32Shell((HWND)mainHwnd, tray));
}
unsigned CallbackMessage() { return kCallback; }
unsigned IconVersion() { return g_version; }
unsigned TaskbarCreatedMessage() {
    static const UINT msg = RegisterWindowMessageW(L"TaskbarCreated");
    return msg;
}
bool ShiftHeld() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }
void InstallCrashCleanup() {
    static bool done = false;
    if (done) return;
    done = true;
    g_prevFilter = SetUnhandledExceptionFilter(CrashFilter);
    g_prevTerminate = std::set_terminate(CrashTerminate);
}

} // namespace okaytray
#endif
