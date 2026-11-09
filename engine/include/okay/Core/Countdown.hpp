#pragma once
// Small pull-based timers: you hand them time each frame, they tell you what happened. Nothing
// here owns a callback, a clock or a thread, so they are deterministic (the same dt sequence
// gives the same answers on every machine), cost nothing when idle, and can be saved and put back.
//
//     Cooldown fire(0.25f);                       // a weapon that may fire four times a second
//     void Update(float dt) {
//         fire.Tick(dt);                          // or fire.Tick(Clock::Get("game"))
//         if (Input::Fire() && fire.TryUse()) Shoot();
//     }
//
//     Countdown jumpBuffer;                       // "jump pressed up to 0.12 s before landing still jumps"
//     if (JumpPressed()) jumpBuffer.Start(0.12f);
//     jumpBuffer.Tick(dt);
//     if (grounded && jumpBuffer.Consume()) Jump();
//
//     Countdown fuse(3.0f);                       // fires once, three seconds from now
//     if (fuse.Tick(dt)) Explode();
//
// Which dt: the one that suits the thing. A gameplay timer takes Time::DeltaTime() (it slows with
// slow motion and stands still in a pause), a UI timer Time::UnscaledDeltaTime(), or give the timer
// a Clock (`timer.Tick(Clock::Get("ui"))`, which reads Clock::Delta(): its own scale and pause).
// Scheduler (Core/Scheduler.hpp) is the other kind of timer: it calls you back. These are the ones
// you poll. For "every N seconds, however long the frame was" use FixedTimestep (Core/Time.hpp).
//
// A bad number is never an error: a duration that is not a finite number >= 0 counts as 0, a dt that
// is not a finite number > 0 adds no time, and a duration is capped at kMaxSeconds.
//
// Saving: Save(writer) / Load(reader) take a SaveWriter / SaveReader (Core/SaveFramework.hpp; they
// are templates, so this header does not include it). Put each timer in its own scope:
//     fire.Save(w.Scope("fire"));     fire.Load(r.Scope("fire"));
// A field an older save never wrote leaves the timer as it was built. Header only, plain C++17.
#include "okay/Core/Clock.hpp"
#include <cmath>
#include <cstdint>

namespace okay {

/// Counts down from a duration to 0 and reports once when it gets there.
class Countdown {
public:
    /// The longest duration (about 31 years); longer ones are cut to it.
    static constexpr float kMaxSeconds = 1.0e9f;
    enum class State { Idle, Running, Done };

    Countdown() = default;
    /// Starts at once.
    explicit Countdown(float seconds) { Start(seconds); }

    /// Begin counting down `seconds` (also from Done, or while running: it starts over). A duration of
    /// 0 is allowed: the next Tick reports it, whatever dt it is given. Un-pauses.
    void Start(float seconds) {
        m_duration = Clean(seconds);
        m_remaining = m_duration;
        m_overshoot = 0.0f;
        m_state = State::Running;
        m_paused = false;
    }
    /// Start again with the same duration.
    void Restart() { Start(m_duration); }
    /// Back to idle: not running, not done, remaining 0. The duration is remembered (Restart).
    void Stop() { m_state = State::Idle; m_remaining = 0.0f; m_overshoot = 0.0f; m_paused = false; }
    void Pause() { m_paused = true; }
    void Resume() { m_paused = false; }
    /// Add (or, negative, take away) time while running: an effect that lengthens a stun. Cannot take
    /// it below 0 or past kMaxSeconds. Does nothing unless running.
    void Extend(float seconds) {
        if (m_state != State::Running || !std::isfinite(seconds)) return;
        m_remaining = Clamp(m_remaining + seconds);
        if (m_remaining > m_duration) m_duration = m_remaining;
    }

    /// Advance by `dt` seconds. Returns true on the one tick that takes it to 0. `Overshoot()` is then
    /// how much of that dt was left over, so a chain of timers can pass the remainder on. Idle,
    /// finished and paused timers ignore the tick.
    bool Tick(float dt) {
        if (m_state != State::Running || m_paused) return false;
        const float step = (dt > 0.0f && std::isfinite(dt)) ? dt : 0.0f;
        if (step < m_remaining) { m_remaining -= step; return false; }
        m_overshoot = step - m_remaining;
        m_remaining = 0.0f;
        m_state = State::Done;
        return true;
    }
    /// Advance by what `clock` moved in its last Tick (its own scale, pause and source).
    bool Tick(const Clock& clock) { return Tick(clock.Delta()); }

    /// True once it has counted down (until the next Start / Stop).
    bool Done() const { return m_state == State::Done; }
    /// Started and not finished (a paused timer is still active).
    bool Active() const { return m_state == State::Running; }
    /// Active and not paused: the timer is counting.
    bool Running() const { return m_state == State::Running && !m_paused; }
    bool Paused() const { return m_paused; }
    State GetState() const { return m_state; }

    float Duration() const { return m_duration; }
    float Remaining() const { return m_remaining; }
    float Elapsed() const { return m_state == State::Idle ? 0.0f : m_duration - m_remaining; }
    /// 0 when it starts, 1 when it is done; 0 while idle. A zero-length timer reads 1 once started.
    float Progress() const {
        if (m_state == State::Idle) return 0.0f;
        if (m_state == State::Done || m_duration <= 0.0f) return 1.0f;
        const float p = 1.0f - m_remaining / m_duration;
        return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
    }
    /// The part of the tick that finished it which was not needed (0 before then).
    float Overshoot() const { return m_overshoot; }

    /// For a buffer or a grace period ("coyote time"): true, once, while the timer is active; it
    /// then goes idle so the same press cannot be used twice. False when idle or already done.
    bool Consume() {
        if (m_state != State::Running) return false;
        Stop();
        return true;
    }

    template <class W> void Save(W&& w) const {
        w.Float("duration", m_duration);
        w.Float("remaining", m_remaining);
        w.Int("state", (int)m_state);
        w.Bool("paused", m_paused);
        w.Float("overshoot", m_overshoot);
    }
    template <class R> void Load(const R& r) {
        const float duration = Clean(r.Float("duration", m_duration));
        const float remaining = Clamp(r.Float("remaining", m_remaining));
        const int state = r.Int("state", (int)m_state);
        m_duration = duration;
        m_remaining = remaining > duration ? duration : remaining;
        m_state = state == 1 ? State::Running : (state == 2 ? State::Done : State::Idle);
        if (m_state == State::Done) m_remaining = 0.0f;
        if (m_state == State::Idle) m_remaining = 0.0f;
        m_paused = r.Bool("paused", m_paused) && m_state == State::Running;
        m_overshoot = Clamp(r.Float("overshoot", 0.0f));
    }

private:
    static float Clamp(float v) { return v > 0.0f ? (v < kMaxSeconds ? v : kMaxSeconds) : 0.0f; }   // NaN -> 0
    static float Clean(float v) { return std::isfinite(v) ? Clamp(v) : (v > 0.0f ? kMaxSeconds : 0.0f); }   // +inf -> the cap, -inf and NaN -> 0
    float m_duration = 0.0f, m_remaining = 0.0f, m_overshoot = 0.0f;
    State m_state = State::Idle;
    bool m_paused = false;
};

/// "May I use this again yet?": an ability, a weapon, a dodge. Ready at the start; Trigger (or TryUse)
/// starts the wait. It never reports an event: you ask Ready().
///
/// Held fire is frame-rate independent: when the tick that made the cooldown ready had time left over,
/// a use in that same frame starts the next wait that much shorter, so a 0.25 s cooldown fires four times
/// a second at 144 fps and at 30 fps alike (without it a poll-style cooldown rounds up to a whole number
/// of frames: 3.75 shots a second at 30 fps). The credit is for that one frame only; a weapon that sat
/// ready and is fired later gets the full wait.
class Cooldown {
public:
    static constexpr float kMaxSeconds = Countdown::kMaxSeconds;

    Cooldown() = default;
    explicit Cooldown(float seconds) : m_duration(Clean(seconds)) {}

    /// How long the wait is. Changing it does not touch a wait that is running (a stronger
    /// "haste" effect calls Reduce for that).
    void SetDuration(float seconds) { m_duration = Clean(seconds); }
    float Duration() const { return m_duration; }

    /// Ready (not waiting)?
    bool Ready() const { return m_remaining <= 0.0f; }
    /// Use it if it is ready: true and the wait starts; false when it is still waiting.
    bool TryUse() {
        if (!Ready()) return false;
        Trigger();
        return true;
    }
    /// Start the wait now, even if it was already running.
    void Trigger() {
        m_remaining = m_duration > m_credit ? m_duration - m_credit : 0.0f;
        m_credit = 0.0f;
    }
    /// Ready again at once.
    void Reset() { m_remaining = 0.0f; m_credit = 0.0f; }
    /// Take `seconds` off the wait (haste, a reset-on-kill perk); never below 0.
    void Reduce(float seconds) {
        if (!(seconds > 0.0f) || !std::isfinite(seconds)) return;
        m_remaining = m_remaining > seconds ? m_remaining - seconds : 0.0f;
    }
    /// A paused cooldown does not count down (a stunned player).
    void SetPaused(bool paused) { m_paused = paused; }
    bool Paused() const { return m_paused; }

    void Tick(float dt) {
        if (m_paused) return;
        const float step = (dt > 0.0f && std::isfinite(dt)) ? dt : 0.0f;
        if (m_remaining <= 0.0f) { m_credit = 0.0f; return; }      // it was ready already: an earlier frame's leftover is gone
        if (m_remaining > step) { m_remaining -= step; return; }
        m_credit = step - m_remaining;                              // became ready inside this tick
        m_remaining = 0.0f;
    }
    void Tick(const Clock& clock) { Tick(clock.Delta()); }

    /// Seconds still to wait (0 when ready).
    float Remaining() const { return m_remaining; }
    /// 0 right after use, 1 when ready: the fill of an ability icon's sweep. A cooldown with no
    /// duration is always 1.
    float Progress() const {
        if (m_remaining <= 0.0f || m_duration <= 0.0f) return 1.0f;
        const float p = 1.0f - m_remaining / m_duration;
        return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
    }

    template <class W> void Save(W&& w) const {
        w.Float("duration", m_duration);
        w.Float("remaining", m_remaining);
        w.Bool("paused", m_paused);
    }
    template <class R> void Load(const R& r) {
        m_duration = Clean(r.Float("duration", m_duration));
        const float rem = r.Float("remaining", m_remaining);
        m_remaining = rem > 0.0f ? (rem < kMaxSeconds ? rem : kMaxSeconds) : 0.0f;
        m_credit = 0.0f;
        m_paused = r.Bool("paused", m_paused);
    }

private:
    static float Clean(float v) { return std::isfinite(v) ? (v > 0.0f ? (v < kMaxSeconds ? v : kMaxSeconds) : 0.0f) : (v > 0.0f ? kMaxSeconds : 0.0f); }
    float m_duration = 0.0f, m_remaining = 0.0f, m_credit = 0.0f;
    bool m_paused = false;
};

}  // namespace okay
