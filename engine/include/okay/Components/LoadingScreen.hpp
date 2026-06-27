#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mathf.hpp"
#include <string>
#include <vector>

namespace okay {

/// A drop-in loading screen: a full-screen overlay with a background (colour or
/// image), a title, a rotating tip line, and a progress bar — drawn on top of
/// everything by the player and the editor's Play view, so adding one is just
/// dropping this component on an object and (optionally) setting a target scene.
///
/// Two common setups:
///   • Level intro — `showOnStart` on, `targetScene` empty: covers the scene for
///     `duration` seconds while it spins up, then fades to reveal it.
///   • Transition scene — a tiny scene that holds only a LoadingScreen with
///     `targetScene` set: load THIS scene to show the loading screen, then it
///     loads the real level when the bar fills.
/// You can also trigger one mid-game from code/script via Begin("NextLevel").
class LoadingScreen : public Behaviour {
public:
    Color       backgroundColor = Color::FromBytes(12, 14, 20);
    std::string backgroundImage;                 ///< optional full-screen image (else a flat colour)
    std::string title           = "Loading...";
    std::vector<std::string> tips;               ///< a different one is shown each time
    Color       textColor       = Color::White;
    Color       barBackground   = Color::FromBytes(40, 40, 50);
    Color       barFill         = Color::FromBytes(90, 200, 110);
    bool        showBar         = true;
    bool        showTitle       = true;
    float       duration        = 2.5f;          ///< minimum seconds the screen stays up
    std::string targetScene;                     ///< scene to load when done (empty = reveal this one)
    bool        showOnStart     = true;          ///< show automatically when the scene starts
    float       fadeTime        = 0.35f;         ///< fade-in / fade-out length (seconds)

    // ---- Runtime state (read by the renderer) ----
    bool  Active()    const { return active_; }
    float Progress()  const { return duration > 1e-4f ? Mathf::Clamp01(timer_ / duration) : 1.0f; }
    const std::string& CurrentTip() const { return tip_; }
    /// Overlay opacity 0..1 — eases in at the start and (for a reveal) out at the end.
    float Alpha() const {
        if (!active_) return 0.0f;
        float a = 1.0f;
        if (fadeTime > 1e-4f) {
            if (timer_ < fadeTime) a = Mathf::Clamp01(timer_ / fadeTime);
            if (targetScene.empty()) {              // a reveal fades back out at the end
                float remain = duration - timer_;
                if (remain < fadeTime) a = Mathf::Min(a, Mathf::Clamp01(remain / fadeTime));
            }
        }
        return a;
    }

    /// Show the loading screen now; optionally override the scene to load when done.
    void Begin(const std::string& target = "") {
        if (!target.empty()) targetScene = target;
        active_ = true; loaded_ = false; timer_ = 0.0f;
        PickTip();
    }

    void Start() override { if (showOnStart) Begin(); else PickTip(); }

    void Update(float dt) override {
        if (!active_) return;
        timer_ += dt;
        if (timer_ >= duration && !loaded_) {
            loaded_ = true;
            if (!targetScene.empty()) {
                if (Scene* s = GetScene()) SceneManager::LoadSceneByName(*s, targetScene);
                // stay visible until the scene actually swaps (deferred to frame end)
            } else {
                active_ = false;   // nothing to load — just reveal the current scene
            }
        }
    }

private:
    bool  active_ = false, loaded_ = false;
    float timer_  = 0.0f;
    int   tipIndex_ = -1;
    std::string tip_;

    void PickTip() {
        if (tips.empty()) { tip_.clear(); return; }
        tipIndex_ = (tipIndex_ + 1) % (int)tips.size();   // rotate (deterministic, testable)
        tip_ = tips[tipIndex_];
    }
};

} // namespace okay
