#pragma once
// Which system pointer each CursorShape stands for on the own backends. Plain numbers so the tables are tested on any machine:
// X11 core font cursor glyph numbers (the standard "cursor" font, X11/cursorfont.h) and the Win32 stock cursor ids (IDC_*, winuser.h).
#include "okay/Platform/Platform.hpp"

namespace okay::platform {

/// The glyph number in the X11 "cursor" font. The font has no diagonal resize arrows, so those use the window corner cursors, and no
/// "busy but usable" arrow, so Progress is the watch like Wait.
inline int X11FontCursorFor(CursorShape s) {
    switch (s) {
    case CursorShape::Arrow:     return 68;    // XC_left_ptr
    case CursorShape::TextInput: return 152;   // XC_xterm
    case CursorShape::ResizeAll: return 52;    // XC_fleur
    case CursorShape::ResizeNS:  return 116;   // XC_sb_v_double_arrow
    case CursorShape::ResizeEW:  return 108;   // XC_sb_h_double_arrow
    case CursorShape::ResizeNESW:return 136;   // XC_top_right_corner
    case CursorShape::ResizeNWSE:return 134;   // XC_top_left_corner
    case CursorShape::Hand:      return 60;    // XC_hand2
    case CursorShape::Wait:      return 150;   // XC_watch
    case CursorShape::Progress:  return 150;   // XC_watch
    case CursorShape::NotAllowed:return 0;     // XC_X_cursor
    }
    return -1;
}

/// The Win32 stock cursor id (the number MAKEINTRESOURCE(IDC_*) holds); -1 for a shape out of range.
inline int Win32CursorIdFor(CursorShape s) {
    switch (s) {
    case CursorShape::Arrow:     return 32512;   // IDC_ARROW
    case CursorShape::TextInput: return 32513;   // IDC_IBEAM
    case CursorShape::ResizeAll: return 32646;   // IDC_SIZEALL
    case CursorShape::ResizeNS:  return 32645;   // IDC_SIZENS
    case CursorShape::ResizeEW:  return 32644;   // IDC_SIZEWE
    case CursorShape::ResizeNESW:return 32643;   // IDC_SIZENESW
    case CursorShape::ResizeNWSE:return 32642;   // IDC_SIZENWSE
    case CursorShape::Hand:      return 32649;   // IDC_HAND
    case CursorShape::Wait:      return 32514;   // IDC_WAIT
    case CursorShape::Progress:  return 32650;   // IDC_APPSTARTING
    case CursorShape::NotAllowed:return 32648;   // IDC_NO
    }
    return -1;
}

} // namespace okay::platform
