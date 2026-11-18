#pragma once
// Pure policies for the Xbox app's two recovery paths (no Windows, no engine; tested on Linux): GPU device loss (DXGI_ERROR_DEVICE_REMOVED / RESET)
// and the suspend marker that tells the next launch to restore the state saved at suspend. UNVERIFIED on a console.
#include <algorithm>
#include <cstdint>

namespace okay::xbox {

/// Retry policy for a lost graphics device. Waits `baseDelayMs` * 2^n between rebuild attempts (100, 200, 400, 800, 1600 ms), gives up after
/// `maxAttempts` failed attempts in a row, and ALSO when the device is lost `maxLossesPerWindow` times within `windowMs` even if every rebuild
/// "worked" (a device that dies again at once must not loop forever). A rebuild that stays healthy for `healthyMs` clears the failure count.
class DeviceRecovery {
public:
    struct Config { int maxAttempts = 5; std::uint64_t baseDelayMs = 100, maxDelayMs = 3000; int maxLossesPerWindow = 4; std::uint64_t windowMs = 20000, healthyMs = 5000; };
    enum class State { Healthy, Waiting, Rebuilding, GaveUp };
    DeviceRecovery() = default;
    explicit DeviceRecovery(const Config& c) : c_(c) {}

    State GetState() const { return state_; }
    int Attempts() const { return attempts_; }
    bool GaveUp() const { return state_ == State::GaveUp; }
    const char* Reason() const { return reason_; }   // why it gave up (a plain sentence for the log)

    /// The device was reported lost (Present returned DEVICE_REMOVED / DEVICE_RESET).
    void OnLost(std::uint64_t nowMs) {
        if (state_ == State::GaveUp) return;
        // forget losses older than the window, then count this one
        int keep = 0; for (int i = 0; i < nLoss_; ++i) if (nowMs - loss_[i] <= c_.windowMs) loss_[keep++] = loss_[i];
        nLoss_ = keep; if (nLoss_ < kMaxLoss) loss_[nLoss_++] = nowMs;
        if (nLoss_ > c_.maxLossesPerWindow) { Give("the graphics device was lost again and again in a short time"); return; }
        if (state_ == State::Healthy) { attempts_ = 0; }
        state_ = State::Waiting; due_ = nowMs + Delay();
    }
    /// True when it is time to try the rebuild now (call each frame while Waiting). Moves to Rebuilding.
    bool ShouldRebuild(std::uint64_t nowMs) {
        if (state_ != State::Waiting || nowMs < due_) return false;
        state_ = State::Rebuilding; ++attempts_; return true;
    }
    /// The rebuild finished: ok = device, swap chain, renderer and shaders are back.
    void RebuildDone(bool ok, std::uint64_t nowMs) {
        if (state_ != State::Rebuilding) return;
        if (ok) { state_ = State::Healthy; healthySince_ = nowMs; pendingClear_ = true; return; }
        if (attempts_ >= c_.maxAttempts) { Give("the graphics device could not be rebuilt"); return; }
        state_ = State::Waiting; due_ = nowMs + Delay();
    }
    /// Call every frame while Healthy: after `healthyMs` without a new loss the failed-attempt count is cleared.
    void Tick(std::uint64_t nowMs) { if (state_ == State::Healthy && pendingClear_ && nowMs - healthySince_ >= c_.healthyMs) { attempts_ = 0; pendingClear_ = false; } }
    /// Should the loop draw this frame? Only while healthy.
    bool CanDraw() const { return state_ == State::Healthy; }

private:
    static constexpr int kMaxLoss = 16;
    std::uint64_t Delay() const { std::uint64_t d = c_.baseDelayMs; for (int i = 0; i < attempts_ && d < c_.maxDelayMs; ++i) d *= 2; return std::min(d, c_.maxDelayMs); }
    void Give(const char* why) { state_ = State::GaveUp; reason_ = why; }
    Config c_; State state_ = State::Healthy; int attempts_ = 0; std::uint64_t due_ = 0, healthySince_ = 0, loss_[kMaxLoss] = {}; int nLoss_ = 0; bool pendingClear_ = false; const char* reason_ = "";
};

/// What the next launch does about a state saved at suspend. The marker file is written when the save is made and removed when the app resumes
/// or exits cleanly; if it is still there at the next start the app was ended while suspended (the system may terminate a suspended app).
enum class StartAction { Fresh, RestoreSuspended, DropStaleMarker };
inline StartAction DecideStart(bool markerExists, bool slotExists) {
    if (markerExists && slotExists) return StartAction::RestoreSuspended;
    if (markerExists) return StartAction::DropStaleMarker;   // a marker with no save: remove it, start fresh
    return StartAction::Fresh;
}

} // namespace okay::xbox
