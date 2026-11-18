#pragma once
// UWP app lifecycle as a pure state machine (no Windows headers): activation, visibility, suspend with a save deadline, resume,
// back request (B on the pad) and window close. The CoreWindow / CoreApplication glue calls the On* functions from the event
// handlers; the game loop calls Take() once per frame and obeys what it says.
// Written from Microsoft's documentation of the UWP app lifecycle (Windows.ApplicationModel.Core.CoreApplication Suspending / Resuming,
// SuspendingOperation.Deadline and GetDeferral, CoreWindow VisibilityChanged / Activated / Closed, SystemNavigationManager.BackRequested)
// and of DXGI Trim (an app must call IDXGIDevice3::Trim when it suspends). UNVERIFIED on Windows or an Xbox.
#include "XboxDisplay.hpp"
#include <cstdint>

namespace okay::xbox {

enum class AppPhase { Created, Active, Hidden, Suspended, Closing };

struct LifecyclePending {
    bool saveState = false;   // suspend started: write the game's state NOW, then call SaveDone()
    bool trimGpu = false;     // call IDXGIDevice3::Trim and release what can be rebuilt
    bool resumed = false;     // coming back: re-check the device, reload what was released
    bool exitRequested = false;   // back / close: leave the game loop (the backend turns this into a Quit event once)
    bool resized = false;     // Size() changed
    bool Any() const { return saveState || trimGpu || resumed || exitRequested || resized; }
};

class Lifecycle {
public:
    AppPhase Phase() const { return phase_; }
    bool Visible() const { return visible_; }
    bool Focused() const { return focused_; }
    /// Draw this frame? Not while hidden, suspended or closing.
    bool ShouldRender() const { return phase_ == AppPhase::Active; }
    /// Run game logic? Not while suspended or closing. (Hidden: the game pauses itself; a hidden UWP app may be suspended soon.)
    bool ShouldRunLogic() const { return phase_ == AppPhase::Active || phase_ == AppPhase::Hidden || phase_ == AppPhase::Created; }
    SizePx Size() const { return sizePx_; }
    float Dpi() const { return dpi_; }

    void Launched() { if (phase_ == AppPhase::Created) { phase_ = visible_ ? AppPhase::Active : AppPhase::Hidden; } }
    void Activated(bool windowActive) { if (phase_ == AppPhase::Suspended || phase_ == AppPhase::Closing) return; focused_ = windowActive; }
    void VisibilityChanged(bool visible) {
        visible_ = visible;
        if (phase_ == AppPhase::Suspended || phase_ == AppPhase::Closing) return;   // applied on resume
        phase_ = visible ? AppPhase::Active : AppPhase::Hidden;
    }
    /// CoreWindow.SizeChanged / DisplayInformation.DpiChanged. `dipsW/H` are the window size in DIPs.
    void Resized(float dipsW, float dipsH, float dpi) {
        dpi_ = dpi > 0 ? dpi : 96.0f;
        const SizePx n{DipsToPixels(dipsW, dpi_), DipsToPixels(dipsH, dpi_)};
        if (!(n == sizePx_)) { sizePx_ = n; p_.resized = true; }
    }
    /// CoreApplication.Suspending. `nowMs` and `deadlineMs` (SuspendingOperation.Deadline minus now) start the save timer.
    void Suspending(std::uint64_t nowMs, std::uint64_t deadlineMs = 5000) {
        if (phase_ == AppPhase::Suspended || phase_ == AppPhase::Closing) return;   // a second Suspending changes nothing
        phase_ = AppPhase::Suspended;
        p_.saveState = true; p_.trimGpu = true;
        saveDone_ = false; suspendAt_ = nowMs; deadline_ = deadlineMs;
    }
    void SaveDone() { saveDone_ = true; }
    /// True once the save finished (or there was no suspend): the glue may complete the SuspendingOperation deferral.
    bool SuspendReady() const { return phase_ != AppPhase::Suspended || saveDone_; }
    /// True when the save is taking longer than the deadline allows (the app would be terminated): complete the deferral anyway.
    bool SuspendOverdue(std::uint64_t nowMs) const { return phase_ == AppPhase::Suspended && !saveDone_ && nowMs >= suspendAt_ + (deadline_ > 500 ? deadline_ - 500 : 0); }
    void Resuming() {
        if (phase_ != AppPhase::Suspended) return;   // Resuming without Suspending: ignore
        phase_ = visible_ ? AppPhase::Active : AppPhase::Hidden;
        p_.resumed = true;
    }
    /// SystemNavigationManager.BackRequested (the B button on a console pad). Marks the event handled so the system does not close us.
    void BackRequested() { if (phase_ != AppPhase::Closing) p_.exitRequested = true; }
    void WindowClosed() { phase_ = AppPhase::Closing; p_.exitRequested = true; }
    /// What changed since the last call (cleared).
    LifecyclePending Take() { LifecyclePending t = p_; p_ = LifecyclePending(); return t; }
    bool Peek() const { return p_.Any(); }

private:
    AppPhase phase_ = AppPhase::Created;
    bool visible_ = true, focused_ = true, saveDone_ = true;
    SizePx sizePx_{};
    float dpi_ = 96.0f;
    std::uint64_t suspendAt_ = 0, deadline_ = 5000;
    LifecyclePending p_;
};

} // namespace okay::xbox
