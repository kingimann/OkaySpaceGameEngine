#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Core/Localization.hpp"
#include <algorithm>
#include <memory>
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
///
/// Background loading (`asyncLoad`, on for a screen made now): when the screen is on an object of its own (just a
/// Transform and this component, the transition scene above) it loads `targetScene` over several frames
/// (`objectsPerFrame` objects each) as soon as it begins, so the frame loop and the bar keep running and nothing
/// hitches. The bar shows the slower of the time and the real progress; the screen stays up for at least `duration`,
/// then fades out over the new scene, and its own object goes away (it was kept alive across the swap with
/// DontDestroyOnLoad). If the scene cannot be loaded the current scene is revealed after the fade and
/// LoadFailed() says so. With other components on the object, or `asyncLoad` off (what a scene saved before this
/// existed does), it loads in one go after `duration` as it always did.
class LoadingScreen : public Behaviour {
public:
    /// How the pieces are arranged (see LoadingScreenLayout.hpp for each one).
    enum class Layout {
        Classic,    ///< title, tip and bar centred at the positions set below
        Minimal,    ///< a spinner and a small title in the middle, thin bar
        Cinematic,  ///< letterbox bars, title bottom-left, bar along the lower bar
        Splash,     ///< a big centred logo (or title) with a thin bar under it
        Card,       ///< a raised card in the middle holding everything
        Corner,     ///< title bottom-left, spinner bottom-right, bar along the bottom
        Ring,       ///< a round progress ring with the percent inside
        Count
    };
    enum class BarStyle { Solid, Segmented, Shimmer, Indeterminate, Count };
    enum class SpinnerStyle { Dots, Arc, Bars, Pulse, Count };

    int         layout          = (int)Layout::Classic;
    int         barStyle        = (int)BarStyle::Solid;
    int         spinnerStyle    = (int)SpinnerStyle::Dots;
    std::string subtitle;                        ///< smaller line with the title (level name, chapter...)
    std::string logoImage;                       ///< optional logo (Splash shows it big)
    float       logoSize        = 0.25f;         ///< logo size as a fraction of screen height
    bool        logoPulse       = true;          ///< logo gently breathes
    bool        kenBurns        = false;         ///< background image slowly zooms in
    Color       cardColor       = Color::FromBytes(28, 32, 44);   ///< Card layout's panel
    float       tipInterval     = 0.0f;          ///< > 0: show the next tip every N seconds
    Color       backgroundColor = Color::FromBytes(12, 14, 20);
    Color       backgroundColor2 = Color::FromBytes(20, 24, 34); ///< gradient bottom colour
    bool        gradientBackground = false;      ///< vertical gradient backgroundColor -> backgroundColor2
    std::string backgroundImage;                 ///< optional full-screen image (else a flat colour)
    std::string title           = "Loading...";
    std::vector<std::string> tips;               ///< a different one is shown each time
    Color       textColor       = Color::White;
    /// Localized: `title`, `subtitle` and each tip are string-table keys, shown in
    /// the player's language.
    bool        localized       = false;
    /// A text field as players read it: its string-table text when localized.
    std::string Shown(const std::string& field) const {
        return localized && !field.empty() ? Localization::Get(field) : field;
    }

    // ---- Title ----
    bool        showTitle       = true;
    Color       titleColor      = Color::White;
    float       titleScale      = 1.0f;          ///< multiplier on the auto title size
    float       titleY          = 0.40f;         ///< vertical position (0=top, 1=bottom)

    // ---- Tip line ----
    Color       tipColor        = Color::FromBytes(180, 186, 200);
    float       tipScale        = 1.0f;
    float       tipY            = 0.78f;

    // ---- Progress bar ----
    Color       barBackground   = Color::FromBytes(40, 40, 50);
    Color       barFill         = Color::FromBytes(90, 200, 110);
    bool        showBar         = true;
    float       barWidth        = 0.5f;          ///< fraction of screen width
    float       barHeight       = 0.022f;        ///< fraction of screen height
    float       barY            = 0.86f;         ///< vertical position (0..1)
    float       barRadius       = 0.0f;          ///< rounded corners (px)
    bool        barBorder       = false;
    Color       barBorderColor  = Color::FromBytes(90, 96, 110);
    bool        showPercent     = false;         ///< draw "NN%" next to the bar

    // ---- Spinner (a chasing ring of dots) ----
    bool        showSpinner     = false;
    Color       spinnerColor    = Color::White;
    float       spinnerRadius   = 0.05f;         ///< fraction of screen height
    float       spinnerDotSize  = 5.0f;          ///< dot size (px, scaled by height)
    float       spinnerSpeed    = 6.0f;          ///< revolutions-ish per second
    float       spinnerY        = 0.62f;

    bool        asyncLoad       = true;          ///< load `targetScene` over several frames (see the class comment)
    int         objectsPerFrame = 64;            ///< objects made per frame by a background load
    float       duration        = 2.5f;          ///< minimum seconds the screen stays up
    std::string targetScene;                     ///< scene to load when done (empty = reveal this one)
    bool        showOnStart     = true;          ///< show automatically when the scene starts
    float       fadeTime        = 0.35f;         ///< fade-in / fade-out length (seconds)

    // ---- Runtime state (read by the renderer) ----
    bool  Active()    const { return active_; }
    float Elapsed()   const { return timer_; }   ///< seconds shown (drives the spinner)
    /// 0..1: how far the screen is. With a background load the slower of the time (`duration`) and the real progress.
    float Progress()  const {
        const float t = duration > 1e-4f ? Mathf::Clamp01(timer_ / duration) : 1.0f;
        if (!load_) return t;
        const float l = load_->phase == Phase::Loaded ? 1.0f : Mathf::Clamp01(load_->progress);
        return std::min(t, l);
    }
    /// True while a background load of `targetScene` is running.
    bool Loading() const { return load_ && load_->phase == Phase::Loading; }
    /// True when the background load of `targetScene` failed (the scene that was showing stays).
    bool LoadFailed() const { return load_ && load_->phase == Phase::Failed; }
    const std::string& CurrentTip() const { return tip_; }
    /// Overlay opacity 0..1 — eases in at the start and (for a reveal) out at the end.
    float Alpha() const {
        if (!active_) return 0.0f;
        float a = 1.0f;
        if (fadeTime > 1e-4f) {
            if (timer_ < fadeTime) a = Mathf::Clamp01(timer_ / fadeTime);
            if (targetScene.empty() || revealing_) {   // a reveal fades back out at the end
                float remain = (revealing_ ? endTime_ : duration) - timer_;
                if (remain < fadeTime) a = Mathf::Min(a, Mathf::Clamp01(remain / fadeTime));
            }
        }
        return a;
    }

    /// Show the loading screen now; optionally override the scene to load when done.
    void Begin(const std::string& target = "") {
        if (!target.empty()) targetScene = target;
        active_ = true; loaded_ = false; timer_ = 0.0f; tipTimer_ = 0.0f;
        revealing_ = false; endTime_ = duration; load_.reset();
        PickTip();
        StartBackgroundLoad();
    }

    void Start() override { if (showOnStart) Begin(); else PickTip(); }

    void Update(float dt) override {
        if (!active_) return;
        timer_ += dt;
        if (tipInterval > 0.0f && tips.size() > 1) {
            tipTimer_ += dt;
            if (tipTimer_ >= tipInterval) { tipTimer_ = 0.0f; PickTip(); }
        }
        if (load_) {   // a background load was started by Begin: wait for it, hold the screen for `duration`, then reveal
            if (load_->phase == Phase::Loading) return;
            if (!revealing_) {
                revealing_ = true;
                const float fade = fadeTime > 0.0f ? fadeTime : 0.0f;
                endTime_ = load_->phase == Phase::Failed ? timer_ + fade : std::max(duration, timer_ + fade);
            }
            if (timer_ >= endTime_) {
                active_ = false;
                Retire();
            }
            return;
        }
        if (timer_ >= duration && !loaded_) {
            loaded_ = true;
            if (!targetScene.empty()) {
                Scene* s = GetScene();
                if (!s || !SceneManager::LoadSceneByName(*s, targetScene))
                    active_ = false;   // no such scene: reveal the current one instead of covering it for ever
                // else stay visible until the scene actually swaps (deferred to frame end)
            } else {
                active_ = false;   // nothing to load — just reveal the current scene
            }
        }
    }

private:
    enum class Phase { Loading, Loaded, Failed };
    /// What the load callbacks write to. Shared, so a callback that runs after this component is gone (the scene was
    /// cleared by another load) touches nothing that was freed.
    struct LoadState { Phase phase = Phase::Loading; float progress = 0.0f; };
    std::shared_ptr<LoadState> load_;
    bool  revealing_ = false;    // the background load is over: fading out
    float endTime_ = 0.0f;       // `timer_` at which the reveal ends

    /// Start the background load of `targetScene` when it can run: on, a target, and an object of its own.
    void StartBackgroundLoad() {
        Scene* s = GetScene();
        if (!asyncLoad || targetScene.empty() || !s || !gameObject || !gameObject->transform) return;
        if (gameObject->Components().size() != 2 || !gameObject->transform->Children().empty()) return;   // Transform + this only
        auto state = std::make_shared<LoadState>();
        Scene::AsyncLoadOptions o;
        o.objectsPerFrame = std::max(1, objectsPerFrame);
        o.onProgress = [state](float p) { state->progress = p; };
        o.onDone = [state](bool ok) { state->phase = ok ? Phase::Loaded : Phase::Failed; };
        s->DontDestroyOnLoad(gameObject);   // the screen outlives the swap: it is what the player sees while the scene builds
        const int h = SceneManager::LoadSceneAsyncByName(*s, targetScene, o);
        if (h == 0) {   // unknown scene or unreadable file: nothing to wait for
            s->CancelDontDestroyOnLoad(gameObject);
            if (state->phase == Phase::Loading) state->phase = Phase::Failed;
        }
        load_ = state;
    }
    /// The load is over and the screen has faded out. Its object was only here for the load and goes now; when it
    /// did not load (or the object is not alone any more) it only gives up its place in the next scene.
    void Retire() {
        Scene* s = GetScene();
        if (!s || !gameObject) return;
        if (load_ && load_->phase == Phase::Loaded) {
            if (gameObject->Components().size() == 2 && gameObject->transform->Children().empty()) { s->Destroy(gameObject); return; }
            targetScene.clear(); showOnStart = false;   // a saved game must not start the load again
        }
        s->CancelDontDestroyOnLoad(gameObject);
    }

    bool  active_ = false, loaded_ = false;
    float timer_  = 0.0f;
    float tipTimer_ = 0.0f;
    int   tipIndex_ = -1;
    std::string tip_;

    void PickTip() {
        if (tips.empty()) { tip_.clear(); return; }
        tipIndex_ = (tipIndex_ + 1) % (int)tips.size();   // rotate (deterministic, testable)
        tip_ = tips[tipIndex_];
    }
};

} // namespace okay
