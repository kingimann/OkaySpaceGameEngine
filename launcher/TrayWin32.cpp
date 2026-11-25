// The launcher's tray icon on Windows (launcher/TrayWin32.hpp). Written from Microsoft's documentation of
// Shell_NotifyIconW / NOTIFYICONDATAW (NIM_ADD, NIM_SETVERSION, NIM_MODIFY with NIF_INFO, NIM_DELETE), TrackPopupMenu
// (with SetForegroundWindow first and a WM_NULL after, as the TrackPopupMenu remarks ask), ShowWindow,
// RegisterWindowMessage("TaskbarCreated") and SetUnhandledExceptionFilter. No outside code. Only the decisions live in
// TrayLogic.hpp; this file just makes the calls. Unverified: compiled with MinGW here, never run on Windows.
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
HWND g_iconHwnd = nullptr;   // the window that owns a live icon (for the crash clean-up); null when none
LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;
std::terminate_handler g_prevTerminate = nullptr;

std::wstring Wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
template <size_t N> void CopyW(wchar_t (&dst)[N], const std::wstring& src) {
    size_t i = 0;
    for (; i + 1 < N && i < src.size(); ++i) dst[i] = src[i];
    dst[i] = L'\0';
}
void DeleteIcon(HWND h) {
    if (!h) return;
    NOTIFYICONDATAW nid; ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid); nid.hWnd = h; nid.uID = kUid;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

class Win32Shell final : public TrayShell {
public:
    explicit Win32Shell(HWND h) : m_hwnd(h) {}
    ~Win32Shell() override { RemoveIcon(); if (m_icon) DestroyIcon(m_icon); }

    bool AddIcon(const std::string& tip) override {
        if (!m_hwnd) return false;
        if (!m_icon)   // the exe's icon (resource 1, launcher/okayspace.rc) at the small-icon size
            m_icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                       GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        NOTIFYICONDATAW nid; ZeroMemory(&nid, sizeof(nid));
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = kUid;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = kCallback;
        nid.hIcon = m_icon ? m_icon : LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
        CopyW(nid.szTip, Wide(tip));
        if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
            // An icon with our id may still be there (a crash, an earlier add): replace it instead.
            if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) return false;
        }
        nid.uVersion = NOTIFYICON_VERSION;   // version 3: lParam keeps the mouse message; keyboard sends NIN_KEYSELECT
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        g_iconHwnd = m_hwnd;
        return true;
    }
    void RemoveIcon() override {
        DeleteIcon(m_hwnd);
        if (g_iconHwnd == m_hwnd) g_iconHwnd = nullptr;
    }
    void HideWindow() override { if (m_hwnd) ::ShowWindow(m_hwnd, SW_HIDE); }
    void ShowWindow() override {
        if (!m_hwnd) return;
        ::ShowWindow(m_hwnd, SW_SHOW);
        if (IsIconic(m_hwnd)) ::ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
    }
    MenuCmd RunMenu(const std::vector<MenuItem>& items) override {
        if (!m_hwnd) return MenuCmd::None;
        HMENU m = CreatePopupMenu();
        if (!m) return MenuCmd::Open;   // no menu: at least bring the launcher back
        UINT def = 0;
        for (const MenuItem& it : items) {
            if (it.separatorBefore && GetMenuItemCount(m) > 0) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, (UINT_PTR)(int)it.cmd, Wide(it.label).c_str());
            if (it.isDefault) def = (UINT)(int)it.cmd;
        }
        if (def) SetMenuDefaultItem(m, def, FALSE);
        POINT pt; GetCursorPos(&pt);
        SetForegroundWindow(m_hwnd);   // so the menu closes when the person clicks elsewhere
        const int cmd = (int)TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN,
                                            pt.x, pt.y, 0, m_hwnd, nullptr);
        PostMessageW(m_hwnd, WM_NULL, 0, 0);
        DestroyMenu(m);
        for (const MenuItem& it : items) if ((int)it.cmd == cmd) return it.cmd;
        return MenuCmd::None;
    }
    void Balloon(const std::string& title, const std::string& text) override {
        if (!m_hwnd || g_iconHwnd != m_hwnd) return;
        NOTIFYICONDATAW nid; ZeroMemory(&nid, sizeof(nid));
        nid.cbSize = sizeof(nid); nid.hWnd = m_hwnd; nid.uID = kUid;
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_INFO;
        CopyW(nid.szInfoTitle, Wide(title));
        CopyW(nid.szInfo, Wide(text));
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

private:
    HWND m_hwnd;
    HICON m_icon = nullptr;
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

std::unique_ptr<TrayShell> MakeWin32Shell(void* hwnd) {
    if (!hwnd) return nullptr;
    return std::unique_ptr<TrayShell>(new Win32Shell((HWND)hwnd));
}
unsigned CallbackMessage() { return kCallback; }
unsigned TaskbarCreatedMessage() {
    static const UINT msg = RegisterWindowMessageW(L"TaskbarCreated");
    return msg;
}
bool ShiftHeld() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }
void InstallCrashCleanup(void* hwnd) {
    (void)hwnd;
    static bool done = false;
    if (done) return;
    done = true;
    g_prevFilter = SetUnhandledExceptionFilter(CrashFilter);
    g_prevTerminate = std::set_terminate(CrashTerminate);
}

} // namespace okaytray
#endif
