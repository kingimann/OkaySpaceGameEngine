#pragma once
// The decisions of the Xbox app glue, as pure logic (no Windows headers, no threads): CoreWindow pointer events -> mouse / touch calls on the
// backend, UTF-16 key characters -> UTF-8 text, the gamepad list -> PadHub connects and disconnects, window and app life-cycle events -> the
// lifecycle machine (and "cancel every finger when focus or visibility is lost"). app/engine_bridge.cpp and app/engine_shell.hpp only copy the
// fields out of the WinRT event arguments into these structs and call these functions. Written from our own design and Microsoft's documentation
// of CoreWindow pointer events (PointerDevice type, pointer id, properties of the pressed buttons, wheel delta of 120 per notch),
// CharacterReceived (one UTF-16 code unit per event) and Windows.Gaming.Input.Gamepad.Gamepads (NonRoamableId as the stable key).
// UNVERIFIED on Windows and on any console: tested on Linux over the fake host only.
#include "XboxUwpBackend.hpp"
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace okay::xbox {

enum class PointerKind { Mouse, Touch, Pen };
enum class PointerAction { Pressed, Moved, Released, Canceled, Wheel };

/// One CoreWindow pointer event, copied out of PointerEventArgs by the shell. Positions are in DIPs (device independent pixels).
struct PointerInput {
    long long id = 0;                 // PointerId
    PointerKind kind = PointerKind::Mouse;
    PointerAction action = PointerAction::Moved;
    float dipX = 0, dipY = 0;
    bool left = false, middle = false, right = false;   // PointerPointProperties: the buttons held right now (mouse only)
    int wheelDelta = 0;               // MouseWheelDelta: 120 per notch, positive = away from the user
};

/// One connected pad as Windows.Gaming.Input.Gamepad.Gamepads() lists it.
struct PadListing { std::string key; PadDevice* device = nullptr; };

inline void AppendUtf8(std::string& s, std::uint32_t cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

class AppGlue {
public:
    explicit AppGlue(UwpBackend* b) : b_(b) {}

    // ---- pointers ----
    /// DIPs to pixels with the window's current DPI (96 DIPs per inch is 1:1), rounded to the nearest pixel.
    int ToPixels(float dips) const { return (int)std::lround((double)dips * (double)b_->Life().Dpi() / 96.0); }

    void OnPointer(const PointerInput& p) {
        const int x = ToPixels(p.dipX), y = ToPixels(p.dipY);
        if (p.kind != PointerKind::Mouse) {   // touch and pen are fingers
            switch (p.action) {
                case PointerAction::Pressed: b_->PushTouch(p.id, PointerPhase::Down, x, y); break;
                case PointerAction::Moved: b_->PushTouch(p.id, PointerPhase::Move, x, y); break;
                case PointerAction::Released: b_->PushTouch(p.id, PointerPhase::Up, x, y); break;
                case PointerAction::Canceled: b_->PushTouch(p.id, PointerPhase::Cancel, x, y); break;
                case PointerAction::Wheel: break;
            }
            return;
        }
        if (p.action == PointerAction::Wheel) { if (p.wheelDelta != 0) b_->PushWheel((float)p.wheelDelta / 120.0f); return; }
        if (p.action == PointerAction::Canceled) { ReleaseMouse(x, y); return; }
        b_->PushPointerMove(x, y);
        // The pressed-button set is compared with the last one, so a second button, or a release while another is held, is not lost.
        SetMouseButtons((p.left ? 1 : 0) | (p.middle ? 2 : 0) | (p.right ? 4 : 0), x, y);
    }

    // ---- text ----
    /// CharacterReceived: one UTF-16 code unit. Control characters are dropped; a surrogate pair is joined and sent once.
    void OnCharacter(std::uint32_t unit) {
        if (unit >= 0xD800 && unit < 0xDC00) { high_ = unit; return; }
        std::uint32_t cp = unit;
        if (unit >= 0xDC00 && unit < 0xE000) { if (!high_) return; cp = 0x10000 + ((high_ - 0xD800) << 10) + (unit - 0xDC00); }
        high_ = 0;   // a high surrogate that is not followed by a low one is dropped
        if (cp < 32 || cp == 127 || (cp >= 0x80 && cp < 0xA0)) return;
        std::string s; AppendUtf8(s, cp); b_->PushText(s);
    }
    void OnComposition(const std::string& utf8, int caret) { b_->PushComposition(utf8, caret); }
    void OnCompositionCommit(const std::string& text = std::string()) { b_->PushCompositionCommit(text); }

    // ---- pads ----
    /// Call once a frame from the UI thread with the current Gamepads() list (polling is thread-safe; the GamepadAdded / Removed events fire on
    /// other threads, so the shell does not use them). New keys connect, keys that vanished disconnect (the hub keeps the slot for the grace time).
    void SyncPads(const std::vector<PadListing>& now) {
        std::set<std::string> seen;
        for (const PadListing& p : now) {
            seen.insert(p.key);
            if (!known_.count(p.key)) { b_->PadConnected(p.key, p.device); known_.insert(p.key); }
        }
        for (auto it = known_.begin(); it != known_.end();) {
            if (!seen.count(*it)) { b_->PadDisconnected(*it); it = known_.erase(it); } else ++it;
        }
        if (!b_->Hub()) b_->EnableHub();
        b_->ServiceLifecycle();   // runs the hub's grace timer and turns its events into GamepadAdded / GamepadRemoved
    }

    // ---- window and app life cycle ----
    void OnResized(float dipsW, float dipsH, float dpi) { b_->Life().Resized(dipsW, dipsH, dpi); }
    void OnVisibility(bool visible) { b_->Life().VisibilityChanged(visible); if (!visible) LetGo(); }
    void OnActivated(bool active) { b_->Life().Activated(active); if (!active) LetGo(); }
    void OnClosed() { LetGo(); b_->Life().WindowClosed(); }
    void OnBackRequested() { b_->Life().BackRequested(); }
    /// CoreApplication.Suspending: fingers and buttons end, the lifecycle starts the save timer, and the hooks run now (the save must be done before
    /// the shell completes its deferral). Returns whether the save finished.
    bool OnSuspending(std::uint64_t deadlineMs) {
        LetGo();
        b_->Life().Suspending(b_->TicksMs(), deadlineMs);
        b_->ServiceLifecycle();
        return b_->Life().SuspendReady();
    }
    void OnResuming() { b_->Life().Resuming(); }

    int MouseButtonsHeld() const { return held_; }

private:
    void SetMouseButtons(int want, int x, int y) {
        static const int kBits[3] = {1, 2, 4};
        static const int kButtons[3] = {platform::MouseLeft, platform::MouseMiddle, platform::MouseRight};
        for (int i = 0; i < 3; ++i) {
            const bool was = (held_ & kBits[i]) != 0, is = (want & kBits[i]) != 0;
            if (was != is) b_->PushPointerButton(kButtons[i], is, x, y);
        }
        held_ = want;
    }
    void ReleaseMouse(int x, int y) { SetMouseButtons(0, x, y); }
    /// Focus or visibility lost, or the app is closing or suspending: no finger and no mouse button stays held.
    void LetGo() { b_->CancelTouches(); int x = 0, y = 0; b_->MouseState(&x, &y); ReleaseMouse(x, y); }

    UwpBackend* b_; int held_ = 0; std::uint32_t high_ = 0; std::set<std::string> known_;
};

} // namespace okay::xbox
