#pragma once
// Where the launcher's chat is drawn: inside the launcher window or popped out in its own OS window (the "Pop out" /
// "Put chat back" button in the chat's header, closing the popped-out window, the tray menu). One chat view
// (DrawChatWindow in launcher/main.cpp) is drawn in either place; this file only holds the decisions, with no window
// system, so tests/test_launcher_tray.cpp checks them on Linux. Written from the owner's request (2026-11-25).
namespace okaychat {

/// The chat's state: is it open, and is it in its own window (the saved "chat_window" preference).
struct Place {
    bool open = false;
    bool ownWindow = false;
};
/// The header's pop-out button while the chat is inside the launcher.
inline Place PopOut(Place p) { p.open = true; p.ownWindow = true; return p; }
/// The header's "Put chat back inside the launcher" button, or "Put chat back in the launcher" in the tray menu:
/// the chat stays open, now inside the launcher.
inline Place PopIn(Place p) { p.open = true; p.ownWindow = false; return p; }
/// The popped-out window's own close button: the chat closes and goes back in, so it opens inside next time.
inline Place CloseOwnWindow(Place p) { p.open = false; p.ownWindow = false; return p; }
/// The header button toggles between the two.
inline Place TogglePop(Place p) { return p.ownWindow ? PopIn(p) : PopOut(p); }
/// Whether the separate window should be on screen.
inline bool WantOwnWindow(Place p) { return p.open && p.ownWindow; }
/// Whether the chat is drawn inside the launcher this frame.
inline bool DrawInside(Place p) { return p.open && !p.ownWindow; }

/// The popped-out window's remembered position and size (chat_win_x / _y / _w / _h in launcher.cfg). x or y of -1 =
/// never placed: centre it.
struct Geometry {
    int x = -1, y = -1, w = 760, h = 560;
};
constexpr int kMinW = 520, kMinH = 380;   // the chat's layout needs this much (also the window's minimum size)
/// A saved size below the minimum (an old file, a hand edit) grows to it.
inline Geometry Sanitize(Geometry g) {
    if (g.w < kMinW) g.w = kMinW;
    if (g.h < kMinH) g.h = kMinH;
    return g;
}
/// The geometry to remember when the window is hidden or the launcher closes: a minimized window's position means
/// nothing, so the last good one is kept.
inline Geometry Remember(Geometry last, bool shown, bool minimized, int x, int y, int w, int h) {
    if (!shown || minimized || w <= 0 || h <= 0) return last;
    return Sanitize(Geometry{x, y, w, h});
}

/// The conversation list folds to its avatar strip when the person folded it, or when the chat is too narrow to show
/// the list and a readable conversation side by side.
constexpr float kNarrowW = 600.0f;
inline bool ListFolded(bool userFolded, float chatWidth) { return userFolded || chatWidth < kNarrowW; }

} // namespace okaychat
