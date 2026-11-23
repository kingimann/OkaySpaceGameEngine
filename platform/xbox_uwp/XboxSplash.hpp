#pragma once
// Splash / loading screen model (pure, no Windows headers; tested on Linux by tests/test_xbox_uwp_pieces.cpp).
// The UWP app shows a splash (the manifest's splash image, then our own frame) while the engine starts. This is the state machine that says
// when it may go away and how opaque it is:
//   Showing  - from Start(), until the game has PRESENTED its first frame AND the minimum time has passed (so it never just flashes).
//   Fading   - alpha goes from 1 to 0 over fadeMs (linear; the shell may apply its own easing).
//   Done     - alpha 0, the splash is removed.
// Stalled() turns true when no first frame has been presented after `stallMs`: the shell then shows "still loading" text (and the log
// gets a line) instead of a splash that looks frozen. Suspending while the splash shows keeps the clock: the shell passes real time
// differences and the model clamps a backwards clock to no change. Times are milliseconds from any steady clock.
// Written from Microsoft's documentation of the UWP splash screen and of ApplicationView; the model is our own. UNVERIFIED on Windows.
#include <cstdint>

namespace okay::xbox {

enum class SplashState { NotStarted, Showing, Fading, Done };

struct SplashConfig {
    std::uint64_t minShowMs = 1500;   // shortest time on screen
    std::uint64_t fadeMs = 300;
    std::uint64_t stallMs = 20000;    // no first frame after this long: report a stall
};

class SplashModel {
public:
    explicit SplashModel(SplashConfig c = {}) : cfg_(c) {}

    void Start(std::uint64_t nowMs) { if (state_ == SplashState::NotStarted) { state_ = SplashState::Showing; start_ = last_ = nowMs; } }
    /// The game presented its first real frame (call once; further calls do nothing).
    void OnFramePresented() { framePresented_ = true; }
    /// Advance. A clock that goes backwards counts as no time passing.
    void Tick(std::uint64_t nowMs) {
        if (state_ == SplashState::NotStarted || state_ == SplashState::Done) return;
        if (nowMs < last_) nowMs = last_;
        last_ = nowMs;
        if (state_ == SplashState::Showing && framePresented_ && last_ - start_ >= cfg_.minShowMs) { state_ = SplashState::Fading; fadeStart_ = last_; }
        if (state_ == SplashState::Fading && last_ - fadeStart_ >= cfg_.fadeMs) state_ = SplashState::Done;
    }
    SplashState State() const { return state_; }
    bool Visible() const { return state_ == SplashState::Showing || state_ == SplashState::Fading; }
    bool Done() const { return state_ == SplashState::Done; }
    /// 1 while showing, falling to 0 while fading (a zero fade time goes straight to Done on the next Tick).
    float Alpha() const {
        switch (state_) {
            case SplashState::NotStarted: return 0.f;
            case SplashState::Showing: return 1.f;
            case SplashState::Done: return 0.f;
            case SplashState::Fading: {
                if (cfg_.fadeMs == 0) return 0.f;
                const float t = (float)(last_ - fadeStart_) / (float)cfg_.fadeMs;
                return t >= 1.f ? 0.f : 1.f - t;
            }
        }
        return 0.f;
    }
    bool Stalled() const { return state_ == SplashState::Showing && !framePresented_ && last_ - start_ >= cfg_.stallMs; }

private:
    SplashConfig cfg_;
    SplashState state_ = SplashState::NotStarted;
    bool framePresented_ = false;
    std::uint64_t start_ = 0, last_ = 0, fadeStart_ = 0;
};

} // namespace okay::xbox
