// okay-ms own Win32 header, part of <windows.h>: windows, messages, keyboard and mouse state, cursors, icons, monitors,
// display modes and DPI, the user32.dll functions of the platform layer (OwnWin32.cpp). Written in our own words from
// Microsoft's public Win32 API reference: the pages of each function and structure named here (their documented
// prototypes and members, in the order the pages list them, "Header winuser.h", "DLL User32.dll"), and the documented
// values of the message numbers (the "Window Messages" and "Keyboard Input" lists), window styles, virtual-key codes
// and system metrics. No text from the Windows SDK, MinGW or Wine headers. Round 37.
#pragma once
#include "base.h"

typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT, *PPOINT, *LPPOINT;
typedef struct _POINTL {
    LONG x;
    LONG y;
} POINTL, *PPOINTL;
typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT, *LPRECT;
typedef const RECT* LPCRECT;
typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE, *PSIZE, *LPSIZE;

typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagMSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
    DWORD lPrivate;
} MSG, *PMSG, *LPMSG;

typedef struct tagWNDCLASSEXW {
    UINT cbSize;
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCWSTR lpszMenuName;
    LPCWSTR lpszClassName;
    HICON hIconSm;
} WNDCLASSEXW, *PWNDCLASSEXW, *LPWNDCLASSEXW;

// What WM_NCCREATE and WM_CREATE carry in lParam.
typedef struct tagCREATESTRUCTW {
    LPVOID lpCreateParams;
    HINSTANCE hInstance;
    HMENU hMenu;
    HWND hwndParent;
    int cy;
    int cx;
    int y;
    int x;
    LONG style;
    LPCWSTR lpszName;
    LPCWSTR lpszClass;
    DWORD dwExStyle;
} CREATESTRUCTW, *LPCREATESTRUCTW;

typedef struct tagMINMAXINFO {
    POINT ptReserved;
    POINT ptMaxSize;
    POINT ptMaxPosition;
    POINT ptMinTrackSize;
    POINT ptMaxTrackSize;
} MINMAXINFO, *PMINMAXINFO, *LPMINMAXINFO;

typedef struct tagPAINTSTRUCT {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT, *PPAINTSTRUCT, *LPPAINTSTRUCT;

typedef struct {
    UINT cbSize;
    HWND hwnd;
    DWORD dwFlags;
    UINT uCount;
    DWORD dwTimeout;
} FLASHWINFO, *PFLASHWINFO;
#define FLASHW_STOP 0
#define FLASHW_CAPTION 0x00000001
#define FLASHW_TRAY 0x00000002
#define FLASHW_ALL (FLASHW_CAPTION | FLASHW_TRAY)
#define FLASHW_TIMER 0x00000004
#define FLASHW_TIMERNOFG 0x0000000C

typedef struct _ICONINFO {
    BOOL fIcon;
    DWORD xHotspot;
    DWORD yHotspot;
    HBITMAP hbmMask;
    HBITMAP hbmColor;
} ICONINFO, *PICONINFO;

// Monitors. MONITORINFOEXW is MONITORINFO followed by the device name, so it is written as a derived structure.
#define CCHDEVICENAME 32
typedef struct tagMONITORINFO {
    DWORD cbSize;
    RECT rcMonitor;
    RECT rcWork;
    DWORD dwFlags;
} MONITORINFO, *LPMONITORINFO;
typedef struct tagMONITORINFOEXW : public tagMONITORINFO {
    WCHAR szDevice[CCHDEVICENAME];
} MONITORINFOEXW, *LPMONITORINFOEXW;
#define MONITORINFOF_PRIMARY 0x00000001
#define MONITOR_DEFAULTTONULL 0x00000000
#define MONITOR_DEFAULTTOPRIMARY 0x00000001
#define MONITOR_DEFAULTTONEAREST 0x00000002
typedef BOOL(CALLBACK* MONITORENUMPROC)(HMONITOR, HDC, LPRECT, LPARAM);

// A display mode (EnumDisplaySettingsW). 220 bytes; the printer and display members share their places as the page
// shows (two unions).
#define CCHFORMNAME 32
typedef struct _devicemodeW {
    WCHAR dmDeviceName[CCHDEVICENAME];
    WORD dmSpecVersion;
    WORD dmDriverVersion;
    WORD dmSize;
    WORD dmDriverExtra;
    DWORD dmFields;
    union {
        struct {
            short dmOrientation;
            short dmPaperSize;
            short dmPaperLength;
            short dmPaperWidth;
            short dmScale;
            short dmCopies;
            short dmDefaultSource;
            short dmPrintQuality;
        };
        struct {
            POINTL dmPosition;
            DWORD dmDisplayOrientation;
            DWORD dmDisplayFixedOutput;
        };
    };
    short dmColor;
    short dmDuplex;
    short dmYResolution;
    short dmTTOption;
    short dmCollate;
    WCHAR dmFormName[CCHFORMNAME];
    WORD dmLogPixels;
    DWORD dmBitsPerPel;
    DWORD dmPelsWidth;
    DWORD dmPelsHeight;
    union {
        DWORD dmDisplayFlags;
        DWORD dmNup;
    };
    DWORD dmDisplayFrequency;
    DWORD dmICMMethod;
    DWORD dmICMIntent;
    DWORD dmMediaType;
    DWORD dmDitherType;
    DWORD dmReserved1;
    DWORD dmReserved2;
    DWORD dmPanningWidth;
    DWORD dmPanningHeight;
} DEVMODEW, *PDEVMODEW, *LPDEVMODEW;
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#define ENUM_REGISTRY_SETTINGS ((DWORD)-2)
#define DM_POSITION 0x00000020L
#define DM_BITSPERPEL 0x00040000L
#define DM_PELSWIDTH 0x00080000L
#define DM_PELSHEIGHT 0x00100000L
#define DM_DISPLAYFLAGS 0x00200000L
#define DM_DISPLAYFREQUENCY 0x00400000L

// DPI awareness contexts are handles with fixed negative values.
typedef struct okay_win32_dpi_awareness_context_* DPI_AWARENESS_CONTEXT;
#define DPI_AWARENESS_CONTEXT_UNAWARE ((DPI_AWARENESS_CONTEXT)-1)
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE ((DPI_AWARENESS_CONTEXT)-2)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((DPI_AWARENESS_CONTEXT)-3)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#define USER_DEFAULT_SCREEN_DPI 96

// Resource names: a small number in place of a string.
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define IS_INTRESOURCE(r) ((((ULONG_PTR)(r)) >> 16) == 0)
// The stock cursors, as names for LoadCursorA (no UNICODE macros here; see windows.h).
#define IDC_ARROW MAKEINTRESOURCEA(32512)
#define IDC_IBEAM MAKEINTRESOURCEA(32513)
#define IDC_WAIT MAKEINTRESOURCEA(32514)
#define IDC_CROSS MAKEINTRESOURCEA(32515)
#define IDC_UPARROW MAKEINTRESOURCEA(32516)
#define IDC_SIZENWSE MAKEINTRESOURCEA(32642)
#define IDC_SIZENESW MAKEINTRESOURCEA(32643)
#define IDC_SIZEWE MAKEINTRESOURCEA(32644)
#define IDC_SIZENS MAKEINTRESOURCEA(32645)
#define IDC_SIZEALL MAKEINTRESOURCEA(32646)
#define IDC_NO MAKEINTRESOURCEA(32648)
#define IDC_HAND MAKEINTRESOURCEA(32649)
#define IDC_APPSTARTING MAKEINTRESOURCEA(32650)

// Class styles.
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_DBLCLKS 0x0008
#define CS_OWNDC 0x0020
#define CS_CLASSDC 0x0040

// Window styles and extended styles.
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_CHILD 0x40000000L
#define WS_MINIMIZE 0x20000000L
#define WS_VISIBLE 0x10000000L
#define WS_DISABLED 0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE 0x01000000L
#define WS_CAPTION 0x00C00000L
#define WS_BORDER 0x00800000L
#define WS_DLGFRAME 0x00400000L
#define WS_VSCROLL 0x00200000L
#define WS_HSCROLL 0x00100000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW \
    (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_POPUPWINDOW (WS_POPUP | WS_BORDER | WS_SYSMENU)
#define WS_EX_TOPMOST 0x00000008L
#define WS_EX_ACCEPTFILES 0x00000010L
#define WS_EX_TOOLWINDOW 0x00000080L
#define WS_EX_WINDOWEDGE 0x00000100L
#define WS_EX_APPWINDOW 0x00040000L
#define CW_USEDEFAULT ((int)0x80000000)

// GetWindowLong / SetWindowLong indexes.
#define GWL_WNDPROC (-4)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWLP_WNDPROC (-4)
#define GWLP_HINSTANCE (-6)
#define GWLP_USERDATA (-21)

// ShowWindow commands.
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA 8
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10

// SetWindowPos flags and the special "insert after" windows.
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOREDRAW 0x0008
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
#define SWP_NOOWNERZORDER 0x0200
#define HWND_TOP ((HWND)0)
#define HWND_BOTTOM ((HWND)1)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
#define HWND_MESSAGE ((HWND)-3)

#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001

// Window messages.
#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETTEXT 0x000C
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_GETMINMAXINFO 0x0024
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_DISPLAYCHANGE 0x007E
#define WM_SETICON 0x0080
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCHITTEST 0x0084
#define WM_INPUT_DEVICE_CHANGE 0x00FE
#define WM_INPUT 0x00FF
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_UNICHAR 0x0109
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION 0x010E
#define WM_IME_COMPOSITION 0x010F
#define WM_SYSCOMMAND 0x0112
#define WM_MENUCHAR 0x0120
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP 0x020C
#define WM_XBUTTONDBLCLK 0x020D
#define WM_MOUSEHWHEEL 0x020E
#define WM_SIZING 0x0214
#define WM_CAPTURECHANGED 0x0215
#define WM_DEVICECHANGE 0x0219
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#ifndef WM_DROPFILES
#define WM_DROPFILES 0x0233
#endif
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY 0x0282
#define WM_MOUSELEAVE 0x02A3
#define WM_DPICHANGED 0x02E0
#define WM_USER 0x0400

// Message parameters.
#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2
#define SC_SIZE 0xF000
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_CLOSE 0xF060
#define SC_KEYMENU 0xF100
#define SC_RESTORE 0xF120
#define SC_SCREENSAVE 0xF140
#define SC_MONITORPOWER 0xF170
#define MNC_IGNORE 0
#define MNC_CLOSE 1
#define HTCLIENT 1
#define WHEEL_DELTA 120
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010
#define MK_XBUTTON1 0x0020
#define MK_XBUTTON2 0x0040
#define XBUTTON1 0x0001
#define XBUTTON2 0x0002
#define ICON_SMALL 0
#define ICON_BIG 1
#define GA_PARENT 1
#define GA_ROOT 2
#define GA_ROOTOWNER 3
#define MAKELRESULT(lo, hi) ((LRESULT)(DWORD)MAKELONG(lo, hi))
#define MAKEWPARAM(lo, hi) ((WPARAM)(DWORD)MAKELONG(lo, hi))
#define MAKELPARAM(lo, hi) ((LPARAM)(DWORD)MAKELONG(lo, hi))
#define GET_WHEEL_DELTA_WPARAM(w) ((short)HIWORD(w))
#define GET_KEYSTATE_WPARAM(w) (LOWORD(w))
#define GET_XBUTTON_WPARAM(w) (HIWORD(w))

// System metrics.
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXICON 11
#define SM_CYICON 12
#define SM_CXDOUBLECLK 36
#define SM_CYDOUBLECLK 37
#define SM_CXSMICON 49
#define SM_CYSMICON 50
#define SM_XVIRTUALSCREEN 76
#define SM_YVIRTUALSCREEN 77
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
#define SM_CMONITORS 80

// Virtual-key codes. The letters and digits are their ASCII codes ('A' to 'Z', '0' to '9') and have no names.
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE

// Windows and messages.
OKAY_WIN32_IMPORT ATOM RegisterClassExW(const WNDCLASSEXW* wc);
OKAY_WIN32_IMPORT BOOL UnregisterClassW(LPCWSTR class_name, HINSTANCE instance);
OKAY_WIN32_IMPORT HWND CreateWindowExW(DWORD ex_style, LPCWSTR class_name, LPCWSTR window_name, DWORD style, int x, int y,
                                       int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param);
OKAY_WIN32_IMPORT BOOL DestroyWindow(HWND hwnd);
OKAY_WIN32_IMPORT LRESULT DefWindowProcW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
OKAY_WIN32_IMPORT BOOL PeekMessageW(LPMSG msg, HWND hwnd, UINT first, UINT last, UINT remove);
OKAY_WIN32_IMPORT BOOL GetMessageW(LPMSG msg, HWND hwnd, UINT first, UINT last);
OKAY_WIN32_IMPORT BOOL TranslateMessage(const MSG* msg);
OKAY_WIN32_IMPORT LRESULT DispatchMessageW(const MSG* msg);
OKAY_WIN32_IMPORT LRESULT SendMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
OKAY_WIN32_IMPORT BOOL PostMessageW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
OKAY_WIN32_IMPORT void PostQuitMessage(int exit_code);
OKAY_WIN32_IMPORT LONG GetMessageTime(void);
OKAY_WIN32_IMPORT BOOL ShowWindow(HWND hwnd, int command);
OKAY_WIN32_IMPORT BOOL UpdateWindow(HWND hwnd);
OKAY_WIN32_IMPORT BOOL SetWindowTextW(HWND hwnd, LPCWSTR text);
OKAY_WIN32_IMPORT BOOL GetClientRect(HWND hwnd, LPRECT rect);
OKAY_WIN32_IMPORT BOOL GetWindowRect(HWND hwnd, LPRECT rect);
OKAY_WIN32_IMPORT BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD ex_style);
OKAY_WIN32_IMPORT BOOL AdjustWindowRectExForDpi(LPRECT rect, DWORD style, BOOL menu, DWORD ex_style, UINT dpi);
OKAY_WIN32_IMPORT BOOL SetWindowPos(HWND hwnd, HWND insert_after, int x, int y, int cx, int cy, UINT flags);
OKAY_WIN32_IMPORT BOOL MoveWindow(HWND hwnd, int x, int y, int width, int height, BOOL repaint);
OKAY_WIN32_IMPORT LONG GetWindowLongW(HWND hwnd, int index);
OKAY_WIN32_IMPORT LONG SetWindowLongW(HWND hwnd, int index, LONG value);
OKAY_WIN32_IMPORT LONG_PTR GetWindowLongPtrW(HWND hwnd, int index);
OKAY_WIN32_IMPORT LONG_PTR SetWindowLongPtrW(HWND hwnd, int index, LONG_PTR value);
OKAY_WIN32_IMPORT BOOL ClientToScreen(HWND hwnd, LPPOINT point);
OKAY_WIN32_IMPORT BOOL ScreenToClient(HWND hwnd, LPPOINT point);
OKAY_WIN32_IMPORT BOOL IsIconic(HWND hwnd);
OKAY_WIN32_IMPORT BOOL IsZoomed(HWND hwnd);
OKAY_WIN32_IMPORT BOOL IsWindowVisible(HWND hwnd);
OKAY_WIN32_IMPORT HWND GetAncestor(HWND hwnd, UINT flags);
OKAY_WIN32_IMPORT HWND WindowFromPoint(POINT point);
OKAY_WIN32_IMPORT BOOL FlashWindowEx(PFLASHWINFO info);
OKAY_WIN32_IMPORT BOOL InvalidateRect(HWND hwnd, const RECT* rect, BOOL erase);
OKAY_WIN32_IMPORT HDC BeginPaint(HWND hwnd, LPPAINTSTRUCT paint);
OKAY_WIN32_IMPORT BOOL EndPaint(HWND hwnd, const PAINTSTRUCT* paint);
OKAY_WIN32_IMPORT HDC GetDC(HWND hwnd);
OKAY_WIN32_IMPORT int ReleaseDC(HWND hwnd, HDC dc);

// Focus, capture and keyboard state.
OKAY_WIN32_IMPORT HWND GetFocus(void);
OKAY_WIN32_IMPORT HWND SetFocus(HWND hwnd);
OKAY_WIN32_IMPORT HWND GetForegroundWindow(void);
OKAY_WIN32_IMPORT BOOL SetForegroundWindow(HWND hwnd);
OKAY_WIN32_IMPORT HWND GetActiveWindow(void);
OKAY_WIN32_IMPORT HWND SetCapture(HWND hwnd);
OKAY_WIN32_IMPORT BOOL ReleaseCapture(void);
OKAY_WIN32_IMPORT SHORT GetAsyncKeyState(int key);
OKAY_WIN32_IMPORT SHORT GetKeyState(int key);
OKAY_WIN32_IMPORT UINT MapVirtualKeyW(UINT code, UINT map_type);
OKAY_WIN32_IMPORT UINT GetDoubleClickTime(void);
OKAY_WIN32_IMPORT int GetSystemMetrics(int index);

// The mouse cursor and icons.
OKAY_WIN32_IMPORT BOOL GetCursorPos(LPPOINT point);
OKAY_WIN32_IMPORT BOOL SetCursorPos(int x, int y);
OKAY_WIN32_IMPORT BOOL ClipCursor(const RECT* rect);
OKAY_WIN32_IMPORT HCURSOR SetCursor(HCURSOR cursor);
OKAY_WIN32_IMPORT int ShowCursor(BOOL show);
OKAY_WIN32_IMPORT HCURSOR LoadCursorA(HINSTANCE instance, LPCSTR name);
OKAY_WIN32_IMPORT HCURSOR LoadCursorW(HINSTANCE instance, LPCWSTR name);
OKAY_WIN32_IMPORT HICON CreateIconIndirect(PICONINFO info);
OKAY_WIN32_IMPORT BOOL DestroyIcon(HICON icon);
OKAY_WIN32_IMPORT BOOL DestroyCursor(HCURSOR cursor);

// Monitors, display modes and DPI.
OKAY_WIN32_IMPORT HMONITOR MonitorFromPoint(POINT point, DWORD flags);
OKAY_WIN32_IMPORT HMONITOR MonitorFromWindow(HWND hwnd, DWORD flags);
OKAY_WIN32_IMPORT BOOL GetMonitorInfoW(HMONITOR monitor, LPMONITORINFO info);
OKAY_WIN32_IMPORT BOOL GetMonitorInfoA(HMONITOR monitor, LPMONITORINFO info);
// The name without A or W is the A function when UNICODE is not defined (as here, see windows.h). A function, not a
// macro, so that it does not rename anything else.
inline BOOL GetMonitorInfo(HMONITOR monitor, LPMONITORINFO info) { return GetMonitorInfoA(monitor, info); }
OKAY_WIN32_IMPORT BOOL EnumDisplayMonitors(HDC dc, LPCRECT clip, MONITORENUMPROC callback, LPARAM data);
OKAY_WIN32_IMPORT BOOL EnumDisplaySettingsW(LPCWSTR device, DWORD mode, DEVMODEW* devmode);
OKAY_WIN32_IMPORT UINT GetDpiForWindow(HWND hwnd);
OKAY_WIN32_IMPORT BOOL SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT context);
