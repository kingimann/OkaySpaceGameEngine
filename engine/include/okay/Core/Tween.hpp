#pragma once
#include "okay/Core/ClockCursor.hpp"
#include "okay/Math/Easing.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Render/Color.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace okay {

class ClockSet;
class GameObject;
class PropertyValue;
class Scene;
class TweenManager;

/// What a tween does when it reaches the end of a loop.
enum class LoopType {
    Restart,      ///< jump back to the start and play again
    Yoyo,         ///< play backwards, then forwards again
    Incremental,  ///< keep going: each loop continues from where the last ended
};

/// A running animation, DOTween-style: configure it with the chainable Set... and
/// On... calls right after creating it (it starts on the next frame), then control
/// it with Play / Pause / Restart / Complete / Kill. Tweens are owned by their
/// scene's TweenManager and are killed automatically when their target object is
/// destroyed, so a tween never touches a deleted object.
class Tweener : public std::enable_shared_from_this<Tweener> {
public:
    using Handle   = std::uint64_t;
    using Callback = std::function<void()>;
    virtual ~Tweener() = default;

    // ---- Settings (chainable) ----
    Tweener& SetEase(Ease e)            { ease = e; easeCurve = nullptr; return *this; }
    /// Your own easing: `fn` maps time 0..1 to progress (0 at 0, 1 at 1 for a clean start and
    /// end). Replaces SetEase until SetEase is called again.
    Tweener& SetEaseCurve(std::function<float(float)> fn) { easeCurve = std::move(fn); return *this; }
    /// A CSS-style cubic bezier (control points x1,y1 and x2,y2) or a staircase of `steps` jumps.
    Tweener& SetEaseBezier(float x1, float y1, float x2, float y2);
    Tweener& SetEaseSteps(int steps, bool jumpStart = false);
    /// An ease named as text: "OutQuad", "cubic-bezier(0.4,0,0.2,1)", "steps(4)", "ease-in-out"
    /// (see Easing.hpp, EaseCurve::Parse). Text it does not know leaves the ease unchanged.
    Tweener& SetEaseSpec(const std::string& text);
    Tweener& SetDelay(float seconds)    { delay = seconds > 0 ? (seconds < kMaxSeconds ? seconds : kMaxSeconds) : 0; return *this; }
    /// `count` < 0 loops forever; 1 (the default) plays once.
    Tweener& SetLoops(int count, LoopType type = LoopType::Restart) { loops = count == 0 ? 1 : count; loopType = type; return *this; }
    Tweener& SetId(std::string value)   { id = std::move(value); return *this; }
    Tweener& SetTarget(GameObject* go)  { target = go; return *this; }
    /// Ignore Time.timeScale (menus and pause screens keep animating).
    Tweener& SetUpdate(bool unscaled)   { unscaledTime = unscaled; return *this; }
    /// Follow a named Clock channel ("game", "ui", "cinematic", or your own) instead of the
    /// scene's time: the tween advances by what that channel advanced, so the channel's own
    /// scale and pause apply (a pause menu freezes "game" while a "ui" tween keeps running).
    /// Wins over SetUpdate; "" goes back to scene time. Needs the ClockSet ticked each frame
    /// (GameLoop does). Only the outermost tween of a sequence reads it.
    Tweener& SetClock(std::string channel) { clock = std::move(channel); return *this; }
    /// Play faster (> 1) or slower (< 1) than normal.
    Tweener& SetSpeed(float scale)      { timeScale = scale; return *this; }
    /// Keep the tween after it completes, so it can be restarted (default: removed).
    Tweener& SetAutoKill(bool on)       { autoKill = on; return *this; }
    /// The end value is an offset from the start value instead of an absolute one.
    Tweener& SetRelative(bool on = true) { relative = on; return *this; }
    /// Play from the end value to the current one (DOTween's From()).
    Tweener& From(bool on = true)       { fromMode = on; return *this; }
    Tweener& OnStart(Callback f)        { onStart = std::move(f); return *this; }
    Tweener& OnUpdate(Callback f)       { onUpdate = std::move(f); return *this; }
    Tweener& OnStepComplete(Callback f) { onStepComplete = std::move(f); return *this; }
    Tweener& OnComplete(Callback f)     { onComplete = std::move(f); return *this; }
    Tweener& OnKill(Callback f)         { onKill = std::move(f); return *this; }

    // ---- Control ----
    void Play()        { paused = false; backwards = false; }
    void Pause()       { paused = true; }
    void TogglePause() { paused = !paused; }
    /// Play back towards the start (it stops there).
    void PlayBackwards() { paused = false; backwards = true; }
    /// Back to the start (including the delay) and play again.
    void Restart(bool includeDelay = true);
    /// Back to the start values and pause.
    void Rewind();
    /// Jump to the end (fires OnComplete). An endless loop goes to the end of its current loop.
    void Complete();
    /// Stop and remove it. `complete` jumps to the end first.
    void Kill(bool complete = false);
    /// Jump to `seconds` into the tween (after its delay), then pause or play.
    void Goto(float seconds, bool andPlay = false);

    // ---- State ----
    Handle handle() const        { return m_handle; }
    bool  IsActive() const       { return !m_killed; }
    bool  IsPlaying() const      { return !m_killed && !paused && !m_complete; }
    bool  IsComplete() const     { return m_complete; }
    bool  IsBackwards() const    { return backwards; }
    /// The loop length used (at least 0.1 ms and at most 1e7 s; NaN counts as the minimum).
    float Duration() const       { return duration > 1e-4f ? (duration < kMaxSeconds ? duration : kMaxSeconds) : 1e-4f; }
    /// The ease at time `u` (0..1): the custom curve when one is set, else the Ease preset.
    float Eased(float u) const   { return easeCurve ? easeCurve(u) : Easing::Evaluate(ease, u); }
    /// Longest duration or delay taken as it is; beyond it the value is clamped (keeps every sum finite).
    static constexpr float kMaxSeconds = 1.0e7f;
    /// Delay + every loop (infinity for endless loops).
    float FullDuration() const;
    /// Seconds played (after the delay, across loops).
    float Elapsed() const        { return std::max(0.0f, m_time - delay); }
    /// 0..1 through the current loop.
    float ElapsedPercentage() const;
    int   CompletedLoops() const { return m_loop; }

    // Settings, readable directly too.
    float duration = 1.0f;
    float delay = 0.0f;
    Ease  ease = Ease::QuadOut;          // the default
    std::function<float(float)> easeCurve;   ///< set: used instead of `ease`
    std::string clock;                   ///< a Clock channel name, or "" for scene time
    int   loops = 1;
    LoopType loopType = LoopType::Restart;
    std::string id;
    GameObject* target = nullptr;
    bool  unscaledTime = false;
    float timeScale = 1.0f;
    bool  autoKill = true;
    bool  relative = false;
    bool  fromMode = false;
    bool  paused = false;
    bool  backwards = false;
    Callback onStart, onUpdate, onStepComplete, onComplete, onKill;

    /// Move the tween to `time` on its own timeline (delay included) and apply it,
    /// firing callbacks for anything crossed. Used by the manager and by sequences.
    void RenderAt(float time);
    /// The object `go` is being destroyed: drop anything that uses it (silently).
    virtual void DropDying(const std::unordered_set<const GameObject*>& dying);
    bool  Killed() const { return m_killed; }

protected:
    /// First time the tween starts playing (after its delay): read start values.
    virtual void DoInit() {}
    /// Apply progress: `k` is the eased 0..1 through this loop (Yoyo already
    /// reversed), `u` the raw 0..1, `loop` the loop number (for Incremental).
    virtual void DoApply(float k, float u, int loop) = 0;
    /// A sequence also rewinds its children when a loop restarts.
    virtual void DoLoopRestart() {}
    bool  m_initialized = false;

private:
    friend class TweenManager;
    friend class TweenSequence;
    TweenManager* m_owner = nullptr;
    Handle m_handle = 0;
    unsigned m_epoch = 0;       // bumped by every RenderAt: lets a render notice a callback re-rendered it
    int    m_forceLoopEnd = -1; // Complete() on an endless tween: render exactly the end of this loop
    ClockCursor m_clockCursor;  // what this tween has already taken from its channel
    float  m_time = 0.0f;       // seconds on the timeline, delay included
    int    m_loop = 0;          // completed loops
    bool   m_started = false, m_complete = false, m_killed = false, m_nested = false;
    void   Advance(float dt);
};

/// Tweens up to four floats read and written through functions (a position, a
/// colour, a slider value...). The engine's DO... shortcuts build these.
class ValueTween : public Tweener {
public:
    using Values = std::array<float, 4>;
    ValueTween(int count, std::function<Values()> get, std::function<void(const Values&)> set, Values end)
        : m_count(count), m_get(std::move(get)), m_set(std::move(set)), m_end(end) {}
    const Values& StartValues() const { return m_from; }
    const Values& EndValues() const   { return m_to; }
protected:
    void DoInit() override;
    void DoApply(float k, float u, int loop) override;
private:
    int m_count;
    std::function<Values()> m_get;
    std::function<void(const Values&)> m_set;
    Values m_end{}, m_from{}, m_to{};
};

/// A tween with its own start / apply functions (punch, shake, jump, path, text...).
class CustomTween : public Tweener {
public:
    CustomTween(std::function<void()> init, std::function<void(float k, float u, int loop)> apply)
        : m_init(std::move(init)), m_apply(std::move(apply)) {}
    /// Set the functions after construction (when they need the tween itself).
    void SetFunctions(std::function<void()> init, std::function<void(float, float, int)> apply) {
        m_init = std::move(init); m_apply = std::move(apply);
    }
protected:
    void DoInit() override { if (m_init) m_init(); }
    void DoApply(float k, float u, int loop) override { if (m_apply) m_apply(k, u, loop); }
private:
    std::function<void()> m_init;
    std::function<void(float, float, int)> m_apply;
};

/// Tweens laid out on one timeline: Append plays after what's there, Join plays
/// alongside the last appended, Insert at a time; intervals and callbacks too.
/// Tweens added to a sequence are controlled by it (not by the manager).
class TweenSequence : public Tweener {
public:
    TweenSequence() { ease = Ease::Linear; duration = 0.0f; }
    TweenSequence& Append(std::shared_ptr<Tweener> t);
    TweenSequence& Join(std::shared_ptr<Tweener> t);
    TweenSequence& Insert(float at, std::shared_ptr<Tweener> t);
    TweenSequence& AppendInterval(float seconds);
    TweenSequence& PrependInterval(float seconds);
    TweenSequence& AppendCallback(Callback fn);
    TweenSequence& InsertCallback(float at, Callback fn);
    std::size_t Count() const { return m_items.size(); }
    void DropDying(const std::unordered_set<const GameObject*>& dying) override;
protected:
    void DoApply(float k, float u, int loop) override;
    void DoLoopRestart() override;
private:
    struct Item { std::shared_ptr<Tweener> tween; float at; };
    struct Call { float at; Callback fn; };
    std::vector<Item> m_items;
    std::vector<Call> m_calls;
    float m_lastStart = 0.0f, m_end = 0.0f, m_prevT = -1.0f;
    void Adopt(const std::shared_ptr<Tweener>& t);
    static float ChildLength(const Tweener& t);
    /// True if `t` is this sequence's child, grandchild, ... (or this itself).
    bool Contains(const Tweener* t) const;
};

/// Every running tween of a scene (Scene::tweens()), ticked once per frame.
class TweenManager {
public:
    using Handle = Tweener::Handle;
    ~TweenManager();

    /// Start managing a tween (the DO... shortcuts do this for you).
    template <class T> std::shared_ptr<T> Add(std::shared_ptr<T> t) { Adopt(t); return t; }
    std::shared_ptr<TweenSequence> Sequence();
    /// Tween any float: `get` reads it, `set` writes it (DOTween's DOTween.To).
    std::shared_ptr<ValueTween> To(std::function<float()> get, std::function<void(float)> set, float end, float duration);
    /// Tween a value that has no getter, from `from` to `to`, calling `set` each frame.
    std::shared_ptr<ValueTween> Virtual(float from, float to, float duration, std::function<void(float)> set);
    /// Call `fn` after `seconds` (a tween with nothing to animate).
    std::shared_ptr<Tweener> DelayedCall(float seconds, std::function<void()> fn);

    /// Advance every tween. `unscaledDt` drives tweens set to ignore the time scale; a
    /// tween with a clock channel takes its time from that channel instead.
    void Update(float dt, float unscaledDt);
    /// The clocks channel tweens read (default: ClockSet::Global()). A test passes its own set
    /// and ticks it by hand. The set must outlive the tweens that use it.
    void SetClockSet(ClockSet* clocks) { m_clocks = clocks; }

    std::shared_ptr<Tweener> Find(Handle h) const;
    /// Tweens with this id / on this object (not nested ones inside sequences).
    std::vector<std::shared_ptr<Tweener>> WithId(const std::string& id) const;
    std::vector<std::shared_ptr<Tweener>> OfTarget(const GameObject* go) const;
    int  Kill(const std::string& id, bool complete = false);
    int  KillTarget(const GameObject* go, bool complete = false);
    void KillAll(bool complete = false);
    int  Pause(const std::string& id);
    int  Play(const std::string& id);
    int  Restart(const std::string& id);
    int  Complete(const std::string& id);
    int  PauseTarget(const GameObject* go);
    int  PlayTarget(const GameObject* go);
    /// Pause / resume everything (a pause menu): unscaled tweens keep playing when `keepUnscaled`.
    void PauseAll(bool keepUnscaled = false);
    void PlayAll();

    std::size_t Count() const;
    std::size_t PlayingCount() const;
    bool IsTweening(const GameObject* go) const;
    /// A short multi-line text of what is running, one line per tween, for a debug overlay:
    /// `Tweens: 2 (1 playing)` then `#3 value "fade" 0.25/1s loop 1/1 [paused] [ui]`.
    std::string Dump() const;

    /// Objects are going away: kill (silently) every tween that uses one of them.
    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying);
    void Clear();

private:
    friend class Tweener;
    friend class TweenSequence;
    void Adopt(const std::shared_ptr<Tweener>& t);
    void Detach(Tweener* t);
    std::vector<std::shared_ptr<Tweener>> m_tweens;
    Handle m_next = 1;
    ClockSet* m_clocks = nullptr;
    // While Update runs: the index it is at, so a tween that leaves the list under it (moved
    // into a sequence by a callback) cannot make the next one skip its turn.
    std::ptrdiff_t m_cursor = 0;
    bool m_updating = false;
};

/// DOTween-style shortcuts for objects. Each returns the tween so you can chain
/// settings: `DOTween::Move(go, {0, 3, 0}, 0.5f)->SetEase(Ease::BackOut).SetLoops(-1, LoopType::Yoyo);`
/// They tween the object's own components and are killed with the object.
namespace DOTween {
    enum class PathType { Linear, CatmullRom };

    std::shared_ptr<Tweener> Move(GameObject* go, const Vec3& to, float duration);        ///< world position
    std::shared_ptr<Tweener> LocalMove(GameObject* go, const Vec3& to, float duration);   ///< local position
    std::shared_ptr<Tweener> MoveAxis(GameObject* go, int axis, float to, float duration);   ///< one axis (0 x, 1 y, 2 z), local
    std::shared_ptr<Tweener> Scale(GameObject* go, const Vec3& to, float duration);
    std::shared_ptr<Tweener> Scale(GameObject* go, float to, float duration);
    /// Rotate to Euler angles (degrees). With SetRelative() it spins by them instead,
    /// so 360 or 720 make full turns.
    std::shared_ptr<Tweener> Rotate(GameObject* go, const Vec3& eulerDeg, float duration);
    std::shared_ptr<Tweener> PunchPosition(GameObject* go, const Vec3& punch, float duration, int vibrato = 10, float elasticity = 1.0f);
    std::shared_ptr<Tweener> PunchScale(GameObject* go, const Vec3& punch, float duration, int vibrato = 10, float elasticity = 1.0f);
    std::shared_ptr<Tweener> PunchRotation(GameObject* go, const Vec3& punchDeg, float duration, int vibrato = 10, float elasticity = 1.0f);
    std::shared_ptr<Tweener> ShakePosition(GameObject* go, float duration, const Vec3& strength, int vibrato = 10, bool fadeOut = true);
    std::shared_ptr<Tweener> ShakeRotation(GameObject* go, float duration, const Vec3& strengthDeg, int vibrato = 10, bool fadeOut = true);
    std::shared_ptr<Tweener> ShakeScale(GameObject* go, float duration, const Vec3& strength, int vibrato = 10, bool fadeOut = true);
    /// Hop to `to` in `jumps` arcs, each `power` high (local position).
    std::shared_ptr<Tweener> Jump(GameObject* go, const Vec3& to, float power, int jumps, float duration);
    /// Follow waypoints (local position), straight or smoothed through every point.
    std::shared_ptr<Tweener> Path(GameObject* go, std::vector<Vec3> points, float duration,
                                  PathType type = PathType::Linear, bool closed = false);
    /// The object's colour: sprite, mesh, UI image / panel / button, or text.
    std::shared_ptr<Tweener> ColorTo(GameObject* go, const Color& to, float duration);
    /// The same colour's alpha only.
    std::shared_ptr<Tweener> Fade(GameObject* go, float alpha, float duration);
    /// A UI widget's anchored position / size.
    std::shared_ptr<Tweener> AnchorPos(GameObject* go, const Vec2& to, float duration);
    std::shared_ptr<Tweener> SizeDelta(GameObject* go, const Vec2& to, float duration);
    /// Type a TextRenderer's text out letter by letter (it starts empty).
    std::shared_ptr<Tweener> Text(GameObject* go, const std::string& text, float duration);
    /// Count a TextRenderer's number from `from` to `to`, with an optional prefix.
    std::shared_ptr<Tweener> Counter(GameObject* go, float from, float to, float duration, const std::string& prefix = "");

    /// Tween any registered property by path ("Light.intensity", "Transform.position",
    /// "SpriteRenderer.color", "Transform.position.y"; see PropertyRegistry) from its value
    /// when the tween starts to `to`. Float, Int (rounded), Vec2, Vec3, Vec4, Color and Quat
    /// (slerp) properties can be tweened; `to` must have the property's type (an Int value
    /// is accepted for a Float property). The path is resolved again every frame, so a
    /// removed component just stops the effect. If it cannot be tweened (unknown path,
    /// wrong type, a type that does not blend) the result is an inactive tween and `error`
    /// (when given) says why. The tween is killed with `go`.
    std::shared_ptr<Tweener> Property(GameObject* go, const std::string& path, const PropertyValue& to,
                                      float duration, std::string* error = nullptr);

    /// The colour DOTween::ColorTo / Fade animate on `go` (nullptr if it has none).
    Color* ColorOf(GameObject* go);
    /// Kill / pause / play / restart / complete every tween on `go`.
    int Kill(GameObject* go, bool complete = false);
    int Pause(GameObject* go);
    int Play(GameObject* go);
    int Restart(GameObject* go);
    int Complete(GameObject* go);
}

} // namespace okay
