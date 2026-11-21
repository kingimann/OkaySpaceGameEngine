#pragma once
// Model of the Windows notification-area (tray) icon callback protocol, as plain C++ so it is tested on every OS.
// Written from Microsoft's public documentation of Shell_NotifyIcon / NOTIFYICONDATA (uCallbackMessage, uVersion,
// NOTIFYICON_VERSION_4, NIN_* notifications), WinUser.h's documented WM_* numbers, and the "TaskbarCreated" registered
// message. Nothing else. The numbers below are those public constants:
//   WM_MOUSEMOVE 0x200, WM_LBUTTONDOWN/UP/DBLCLK 0x201/0x202/0x203, WM_RBUTTONDOWN/UP/DBLCLK 0x204/0x205/0x206,
//   WM_MBUTTONDOWN/UP 0x207/0x208 (WinUser.h); WM_CONTEXTMENU 0x7B (WinUser.h); WM_USER 0x400;
//   NIN_SELECT = WM_USER, NINF_KEY = 1, NIN_KEYSELECT = NIN_SELECT | NINF_KEY, NIN_BALLOONSHOW .. NIN_POPUPCLOSE =
//   WM_USER+2 .. WM_USER+7 (ShellAPI.h, Shell_NotifyIcon documentation).
#include <cstdint>

namespace okay::platform::tray {

constexpr unsigned kWmMouseMove = 0x200, kWmLButtonDown = 0x201, kWmLButtonUp = 0x202, kWmLButtonDblClk = 0x203;
constexpr unsigned kWmRButtonDown = 0x204, kWmRButtonUp = 0x205, kWmRButtonDblClk = 0x206;
constexpr unsigned kWmMButtonDown = 0x207, kWmMButtonUp = 0x208, kWmContextMenu = 0x7B, kWmUser = 0x400;
constexpr unsigned kNinSelect = kWmUser, kNinKeySelect = kNinSelect | 1u;
constexpr unsigned kNinBalloonShow = kWmUser + 2, kNinBalloonHide = kWmUser + 3, kNinBalloonTimeout = kWmUser + 4,
                   kNinBalloonUserClick = kWmUser + 5, kNinPopupOpen = kWmUser + 6, kNinPopupClose = kWmUser + 7;

enum class TrayEvent {
    Unknown, Hover, LeftDown, LeftClick, LeftDoubleClick, RightDown, RightClick, MiddleClick, ContextMenu,
    Select, KeySelect, BalloonShown, BalloonHidden, BalloonTimeout, BalloonClicked, PopupOpen, PopupClose
};

struct TrayDecoded {
    TrayEvent event = TrayEvent::Unknown;
    unsigned iconId = 0;       ///< version 4 only: the high word of lParam (the icon's uID); 0 for version 0
    bool hasPoint = false;     ///< version 4 only, for events that carry a position (wParam)
    int x = 0, y = 0;          ///< screen coordinates from wParam (two signed 16-bit halves), when hasPoint
};

inline TrayEvent TrayEventFromCode(unsigned code) {
    switch (code) {
        case kWmMouseMove: return TrayEvent::Hover;
        case kWmLButtonDown: return TrayEvent::LeftDown;
        case kWmLButtonUp: return TrayEvent::LeftClick;
        case kWmLButtonDblClk: return TrayEvent::LeftDoubleClick;
        case kWmRButtonDown: return TrayEvent::RightDown;
        case kWmRButtonUp: return TrayEvent::RightClick;
        case kWmMButtonUp: return TrayEvent::MiddleClick;
        case kWmContextMenu: return TrayEvent::ContextMenu;
        case kNinSelect: return TrayEvent::Select;
        case kNinKeySelect: return TrayEvent::KeySelect;
        case kNinBalloonShow: return TrayEvent::BalloonShown;
        case kNinBalloonHide: return TrayEvent::BalloonHidden;
        case kNinBalloonTimeout: return TrayEvent::BalloonTimeout;
        case kNinBalloonUserClick: return TrayEvent::BalloonClicked;
        case kNinPopupOpen: return TrayEvent::PopupOpen;
        case kNinPopupClose: return TrayEvent::PopupClose;
        default: return TrayEvent::Unknown;
    }
}

/// Decode the callback message's lParam (and wParam). `version` is the icon's NOTIFYICON_VERSION (0 = default, 4 = modern).
/// Version 0: the low word of lParam is the event (the high word is not defined, so it is ignored), wParam is the icon id.
/// Version 4: low word = event, high word = icon id, wParam = x | (y << 16) for events that have a position.
inline TrayDecoded DecodeCallback(uint64_t lParam, int version, uint64_t wParam = 0) {
    TrayDecoded d;
    d.event = TrayEventFromCode((unsigned)(lParam & 0xFFFFu));
    if (version >= 4) {
        d.iconId = (unsigned)((lParam >> 16) & 0xFFFFu);
        const bool pos = d.event != TrayEvent::Unknown && d.event != TrayEvent::BalloonShown && d.event != TrayEvent::BalloonHidden &&
                         d.event != TrayEvent::BalloonTimeout && d.event != TrayEvent::BalloonClicked;
        if (pos && wParam != 0) {
            d.hasPoint = true;
            d.x = (int)(int16_t)(uint16_t)(wParam & 0xFFFFu);
            d.y = (int)(int16_t)(uint16_t)((wParam >> 16) & 0xFFFFu);
        }
    }
    return d;
}

/// True when `msg` is the registered "TaskbarCreated" message (explorer restarted), so the icon has to be added again.
/// `taskbarCreatedMsg` is what RegisterWindowMessage("TaskbarCreated") returned; 0 means it was never registered.
inline bool IsTaskbarCreated(unsigned msg, unsigned taskbarCreatedMsg) { return taskbarCreatedMsg != 0 && msg == taskbarCreatedMsg; }
inline bool NeedsReAdd(unsigned msg, unsigned taskbarCreatedMsg) { return IsTaskbarCreated(msg, taskbarCreatedMsg); }

struct TrayAction { bool restore = false; bool menu = false; };

/// What the launcher does for an event: left click / double click open the app, right click / context menu raise the menu.
/// (Select / KeySelect, sent by version 4 icons for a click or Enter, open the app too.)
inline TrayAction ActionFor(TrayEvent e) {
    TrayAction a;
    if (e == TrayEvent::LeftClick || e == TrayEvent::LeftDoubleClick || e == TrayEvent::Select || e == TrayEvent::KeySelect) a.restore = true;
    else if (e == TrayEvent::RightClick || e == TrayEvent::ContextMenu) a.menu = true;
    return a;
}

}  // namespace okay::platform::tray
