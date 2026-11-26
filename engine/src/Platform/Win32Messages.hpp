#pragma once
// Pure decisions of the own Windows backend's window procedure (no Windows headers; tested on Linux by
// tests/test_platform_win32_pure.cpp):
//   * ClickCounter: the `clicks` number of MouseButtonDown / MouseButtonUp (1 single, 2 double, 3 triple...). The window
//     class has no CS_DBLCLKS, so Windows sends plain button messages and we count ourselves with the system's rules:
//     the same button again within GetDoubleClickTime() milliseconds and inside an SM_CXDOUBLECLK x SM_CYDOUBLECLK box
//     centred on the previous click.
//   * SwallowSysCommand: WM_SYSCOMMAND with SC_KEYMENU is what DefWindowProc does when Alt or F10 is pressed and released
//     alone, or Alt+letter: it enters menu mode, so the next key (Space, an arrow) drives the system menu and the game
//     stops reading keys. A game window has no menu bar; only Alt+Space (the window menu) is kept.
//   * MenuCharReply: the answer to WM_MENUCHAR (Alt+key that matches no menu): MNC_CLOSE, so Windows does not beep.
// Written from Microsoft's documentation of WM_SYSCOMMAND (SC_KEYMENU = 0xF100, "the four low-order bits are used
// internally by the system"; lParam is the character for a keyboard mnemonic), WM_MENUCHAR (MNC_CLOSE = 1 in the high
// word), GetDoubleClickTime, GetMessageTime and SM_CXDOUBLECLK / SM_CYDOUBLECLK. Our own code.
#include <cstdint>

namespace okay::platform::win32msg {

class ClickCounter {
public:
    /// A button went down at (x, y) at message time `timeMs` (GetMessageTime, wraps at 2^32). `maxGapMs` = GetDoubleClickTime()
    /// (0 or more than 5 s: 500), `boxW` x `boxH` = SM_CXDOUBLECLK x SM_CYDOUBLECLK. Returns the click count.
    int Down(int button, int x, int y, uint32_t timeMs, uint32_t maxGapMs, int boxW, int boxH) {
        if (maxGapMs == 0 || maxGapMs > 5000) maxGapMs = 500;
        const uint32_t gap = timeMs - lastTime_;   // unsigned: correct across the 32-bit wrap
        const int dx = x - lastX_, dy = y - lastY_;
        const bool closeBy = (dx < 0 ? -dx : dx) * 2 <= boxW && (dy < 0 ? -dy : dy) * 2 <= boxH;
        count_ = (count_ > 0 && button == lastButton_ && gap <= maxGapMs && closeBy) ? count_ + 1 : 1;
        lastButton_ = button; lastX_ = x; lastY_ = y; lastTime_ = timeMs;
        return count_;
    }
    /// The count to put on the matching MouseButtonUp (that of the last press of this button; 1 for any other).
    int Up(int button) const { return button == lastButton_ && count_ > 0 ? count_ : 1; }

private:
    int lastButton_ = -1, lastX_ = 0, lastY_ = 0, count_ = 0;
    uint32_t lastTime_ = 0;
};

inline bool SwallowSysCommand(uint64_t wParam, int64_t lParam) {
    return (wParam & 0xFFF0u) == 0xF100u && lParam != ' ';
}

inline int64_t MenuCharReply() { return (int64_t)1 << 16; }

} // namespace okay::platform::win32msg
