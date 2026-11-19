#pragma once
// Independent game clocks ("channels"), each with its own scale and pause.
//
// Time has one game clock: pause the game and everything on it stops, a slow-motion
// hit stop slows it all. A pause MENU still wants to animate, a cutscene should
// not slow down when gameplay does, and a "stunned" status wants its own speed. So
// give each its own channel:
//
//     float dt = Clock::Get("ui").Delta();              // keeps running while the game is paused
//     menu.Animate(dt);
//     Clock::Get("game").SetScale(0.5f);                 // only things on "game" slow down
//     Clock::Get("cinematic").Pause();                   // a cutscene holds without touching the game
//
// Three channels exist from the start; their sources and behaviour:
//   "game"       follows Time::DeltaTime(): scaled, frozen by Game::Paused(), slowed by hit stops.
//   "ui"         follows the real frame time: never frozen by the game pause or time scale.
//   "cinematic"  follows the real frame time, but holds while the game is paused.
// Any other name creates a channel on first use, on real time (see Clock::Get).
//
// A clock can also follow ANOTHER clock (SetParent): it then advances by what its parent advanced, so the
// parent's scale, pause and ramp apply first and the child adds its own. "Slow motion" on a parent slows
// every child, a child can be slower still, and a clock that is not under that parent (the "ui" one)
// runs on. A chain may be any depth; a loop in the chain is cut (the clock then reads real time).
//
//     Clock::Get("game").SetScale(0.5f);                          // everything under "game" is half speed
//     Clock::Get("enemies").SetParent("game"); Clock::Get("enemies").SetScale(0.5f);   // quarter speed
//
// Time to nanoseconds: every clock also counts whole nanoseconds (ElapsedTicks) with the leftover
// carried, like Time::ElapsedTicks, so a day of 60 fps frames is exact to the nanosecond.
//
// A host calls ClockSet::Global().Tick(...) once a frame, after Time::Step; GameLoop
// does it for you. Plain C++17, no threads, no allocation after a channel exists.
#include "okay/Core/Game.hpp"
#include "okay/Core/Stopwatch.hpp"
#include "okay/Core/Time.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace okay {

class Clock {
public:
    /// What a clock reads each frame before its own scale and pause.
    enum class Source {
        Scaled,     ///< Time::DeltaTime(): the game clock (time scale, game pause, hit stop)
        Unscaled,   ///< Time::UnscaledDeltaTime(): real frame time
        Parent,     ///< what the parent clock (SetParent) advanced this frame; real time while it has none
    };

    Clock(std::string name, Source source, bool holdsWhileGamePaused = false)
        : m_name(std::move(name)), m_source(source), m_holds(holdsWhileGamePaused) {}

    const std::string& Name() const { return m_name; }
    Source GetSource() const { return m_source; }
    void   SetSource(Source s) { m_source = s; }
    /// True: the clock stands still while Game::Paused() even on an unscaled source.
    bool   HoldsWhileGamePaused() const { return m_holds; }
    void   SetHoldsWhileGamePaused(bool h) { m_holds = h; }

    /// The clock this one follows ("" = none). Setting a parent makes the source Parent; clearing it puts
    /// the source back to what it was (Scaled or Unscaled). A name that is this clock's own is ignored.
    const std::string& Parent() const { return m_parent; }
    void SetParent(const std::string& parentName) {
        if (parentName == m_name) return;
        if (!parentName.empty() && m_source != Source::Parent) m_sourceBeforeParent = m_source;
        if (parentName.empty() && m_source == Source::Parent) m_source = m_sourceBeforeParent;
        m_parent = parentName;
        if (!parentName.empty()) m_source = Source::Parent;
    }

    /// Seconds this clock advanced in the last Tick (0 while paused or held).
    float  Delta() const { return (float)m_deltaD; }
    /// The same as an exact double (what children and the nanosecond count read).
    double DeltaD() const { return m_deltaD; }
    /// Seconds this clock has run since it was created or Reset.
    double Elapsed() const { return m_elapsed; }
    /// The same as whole nanoseconds, within 1 ns of the exact sum however long the clock has run.
    std::int64_t ElapsedTicks() const { return m_ticks64; }
    /// The frame time the clock read before its scale and pause (what a ui clock
    /// that is "paused" would have advanced by).
    float  SourceDelta() const { return (float)m_sourceDelta; }
    std::uint64_t Ticks() const { return m_ticks; }

    /// 1 = normal. Negative and NaN become 0, and it is capped at Time::kMaxTimeScale. A ramp in
    /// progress is cancelled (the scale is where you put it).
    float  Scale() const { return m_scale; }
    void   SetScale(float s) { m_scale = CleanScale(s); m_ramp = false; }

    /// Move the scale to `target` over `seconds` of this clock's source time, in a straight line
    /// (a stunned enemy slowing to a crawl, a menu clock easing back to speed). Exact however the
    /// frames fall: the step a frame takes is the integral of the scale over it. A clock that is
    /// paused (its own pause, or held by the game pause) stands still and so does its ramp.
    /// SetScale or another ramp takes over from wherever the scale is; `seconds` <= 0 or NaN sets
    /// it at once.
    void   RampScale(float target, float seconds) {
        target = CleanScale(target);
        if (!(seconds > 0.0f) || !std::isfinite(seconds)) { m_scale = target; m_ramp = false; return; }
        m_ramp = true; m_rampFrom = m_scale; m_rampTo = target; m_rampDur = (double)seconds; m_rampT = 0.0;
    }
    bool   Ramping() const { return m_ramp; }
    /// Where the running ramp ends (the current scale when there is none).
    float  RampTarget() const { return m_ramp ? m_rampTo : m_scale; }
    /// Source seconds the running ramp still has to go (0 when there is none).
    float  RampSecondsLeft() const { return m_ramp ? (float)(m_rampDur - m_rampT) : 0.0f; }
    void   CancelRamp() { m_ramp = false; }
    /// The clock's own pause (the game pause is separate: see HoldsWhileGamePaused).
    bool   Paused() const { return m_paused; }
    void   SetPaused(bool p) { m_paused = p; }
    void   Pause() { m_paused = true; }
    void   Resume() { m_paused = false; }

    /// Advance by one frame. `gamePaused`: Game::Paused() this frame. `parentDelta`: what the parent clock
    /// advanced (ClockSet::Tick passes it; only a clock whose source is Parent reads it).
    void Tick(double scaledDt, double unscaledDt, bool gamePaused, double parentDelta = 0.0) {
        double src = m_source == Source::Scaled ? scaledDt : (m_source == Source::Parent ? parentDelta : unscaledDt);
        if (!(src > 0.0) || !std::isfinite(src)) src = 0.0;
        m_sourceDelta = src;
        const bool held = m_paused || (m_holds && gamePaused);
        // Held in float range: a huge step (a clamp turned off) times a scale of 100 overflowed to infinity
        // and made Elapsed() infinite for good.
        double d = held ? 0.0 : (m_ramp ? StepRamp(src) : src * (double)m_scale);
        if (!(d < (double)std::numeric_limits<float>::max())) d = (double)std::numeric_limits<float>::max();
        m_deltaD = d;
        m_elapsed += d;
        // Whole nanoseconds with the fraction carried (see Time::ElapsedTicks).
        double dd = d > 1.0e9 ? 1.0e9 : d;
        const double t = dd * 1.0e9 + m_tickCarry, w = std::floor(t);
        constexpr std::int64_t kCap = 9000000000000000000LL;
        if (m_ticks64 >= kCap - (std::int64_t)w) { m_ticks64 = kCap; m_tickCarry = 0.0; }
        else { m_ticks64 += (std::int64_t)w; m_tickCarry = t - w; }
        ++m_ticks;
    }

    /// Back to the start: no time elapsed, scale 1, not paused, no ramp. The source stays.
    void Reset() { m_deltaD = m_sourceDelta = 0.0; m_elapsed = 0.0; m_ticks = 0; m_ticks64 = 0; m_tickCarry = 0.0; m_scale = 1.0f; m_paused = false; m_ramp = false; }

    /// Put the elapsed time and tick count back to what a save game stored (a world clock that
    /// drives a day and night cycle). Negative and non-finite times count as 0. The last frame's
    /// Delta() reads 0 until the next Tick. See Core/TimeSnapshot.hpp.
    void Restore(double elapsed, std::uint64_t ticks) {
        m_elapsed = (elapsed > 0.0 && std::isfinite(elapsed)) ? elapsed : 0.0;
        m_ticks = ticks;
        m_ticks64 = (std::int64_t)(m_elapsed < 9.0e9 ? m_elapsed * 1.0e9 : 9.0e18); m_tickCarry = 0.0;
        m_deltaD = m_sourceDelta = 0.0;
    }

    /// The channel called `name` in the process-wide set (ClockSet::Global()); an
    /// unknown name creates a new channel on real time with its own scale and pause.
    static Clock& Get(const std::string& name);
    static bool   Exists(const std::string& name);

private:
    static float CleanScale(float s) { return s > 0.0f ? (s < Time::kMaxTimeScale ? s : Time::kMaxTimeScale) : 0.0f; }
    // Integrate the ramping scale over `src` seconds of source time and advance the ramp.
    double StepRamp(double src) {
        const double dt = src;
        const double seg = dt < m_rampDur - m_rampT ? dt : m_rampDur - m_rampT;   // the part of the frame still inside the ramp
        const double k0 = m_rampT / m_rampDur, k1 = (m_rampT + seg) / m_rampDur;
        const double s0 = (double)m_rampFrom + ((double)m_rampTo - (double)m_rampFrom) * k0;
        const double s1 = (double)m_rampFrom + ((double)m_rampTo - (double)m_rampFrom) * k1;
        double acc = seg * 0.5 * (s0 + s1);
        m_rampT += seg;
        m_scale = CleanScale((float)s1);
        if (m_rampT >= m_rampDur - 1e-9) {            // arrived: the rest of the frame runs at the target
            m_scale = m_rampTo;
            m_ramp = false;
            acc += (dt - seg) * (double)m_rampTo;
        }
        return acc;
    }
    std::string m_name;
    Source m_source;
    bool   m_holds = false;
    bool   m_paused = false;
    bool   m_ramp = false;
    float  m_rampFrom = 1.0f, m_rampTo = 1.0f;
    double m_rampDur = 0.0, m_rampT = 0.0;
    float  m_scale = 1.0f;
    std::string m_parent;
    Source m_sourceBeforeParent = Source::Unscaled;
    double m_deltaD = 0.0, m_sourceDelta = 0.0;
    double m_elapsed = 0.0;
    std::int64_t m_ticks64 = 0;
    double m_tickCarry = 0.0;
    std::uint64_t m_ticks = 0;
};

/// A group of clocks that is ticked together once per frame.
class ClockSet {
public:
    /// Starts with "game", "ui" and "cinematic" (see the top of the file).
    ClockSet() {
        Add("game", Clock::Source::Scaled, false);
        Add("ui", Clock::Source::Unscaled, false);
        Add("cinematic", Clock::Source::Unscaled, true);
    }
    ClockSet(const ClockSet&) = delete;
    ClockSet& operator=(const ClockSet&) = delete;

    /// Add a channel, or return the one with that name (its settings are left alone).
    Clock& Add(const std::string& name, Clock::Source source = Clock::Source::Unscaled, bool holdsWhileGamePaused = false) {
        if (Clock* c = Find(name)) return *c;
        m_clocks.push_back(std::make_unique<Clock>(name, source, holdsWhileGamePaused));
        return *m_clocks.back();
    }
    /// The channel with that name, created on real time when it does not exist yet.
    Clock& Get(const std::string& name) { return Add(name); }
    Clock* Find(const std::string& name) {
        for (auto& c : m_clocks) if (c->Name() == name) return c.get();
        return nullptr;
    }
    const Clock* Find(const std::string& name) const {
        for (auto& c : m_clocks) if (c->Name() == name) return c.get();
        return nullptr;
    }
    /// Remove a channel; the three built-in ones cannot be removed. References to a
    /// removed clock dangle. Returns whether it was removed.
    bool Remove(const std::string& name) {
        if (name == "game" || name == "ui" || name == "cinematic") return false;
        for (std::size_t i = 0; i < m_clocks.size(); ++i)
            if (m_clocks[i]->Name() == name) { m_clocks.erase(m_clocks.begin() + (std::ptrdiff_t)i); return true; }
        return false;
    }
    std::size_t Count() const { return m_clocks.size(); }
    std::vector<std::string> Names() const {
        std::vector<std::string> out;
        for (auto& c : m_clocks) out.push_back(c->Name());
        return out;
    }

    /// Advance every clock by this frame's times.
    /// Remembers Time::FrameCount() so TickOncePerFrame() will not tick the same frame again.
    /// A clock under a parent is ticked after it, so it reads what the parent advanced this very frame.
    void Tick(double scaledDt, double unscaledDt, bool gamePaused) {
        const std::size_t n = m_clocks.size();
        m_state.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i) TickOne(i, scaledDt, unscaledDt, gamePaused);
        m_lastFrame = Time::FrameCount();
    }
    /// Tick(Time::DeltaTimeD(), Time::UnscaledDeltaTimeD(), Game::Paused()).
    void TickFromTime() { Tick(Time::DeltaTimeD(), Time::UnscaledDeltaTimeD(), Game::Paused()); }
    /// TickFromTime, but only once for each Time::FrameCount(): a frame that GameLoop or an
    /// earlier Tick / TickFromTime already ticked is skipped (a host that cannot tell
    /// whether the loop already ticked the set calls this). Where Time is never stepped
    /// (frame count stuck at 0) it ticks every call.
    void TickOncePerFrame() {
        const std::uint64_t f = Time::FrameCount();
        if (f != 0 && f == m_lastFrame) return;
        TickFromTime();
    }
    /// Every clock back to its start (a new Play session).
    void Reset() { for (auto& c : m_clocks) c->Reset(); m_lastFrame = ~std::uint64_t(0); }

    /// The process-wide set Clock::Get uses and GameLoop ticks.
    static ClockSet& Global() { static ClockSet s; return s; }

private:
    // Tick clock `i` after its parent. A parent that does not exist, or a loop in the chain (a clock
    // met again while it is still being ticked), is cut: that clock reads real time.
    void TickOne(std::size_t i, double scaledDt, double unscaledDt, bool gamePaused) {
        if (m_state[i] == 2) return;
        m_state[i] = 1;
        Clock& c = *m_clocks[i];
        double parentDelta = unscaledDt;
        if (c.GetSource() == Clock::Source::Parent) {
            for (std::size_t j = 0; j < m_clocks.size(); ++j) {
                if (m_clocks[j]->Name() != c.Parent()) continue;
                if (m_state[j] == 0) TickOne(j, scaledDt, unscaledDt, gamePaused);
                if (m_state[j] == 2) parentDelta = m_clocks[j]->DeltaD();
                break;
            }
        }
        c.Tick(scaledDt, unscaledDt, gamePaused, parentDelta);
        m_state[i] = 2;
    }
    std::vector<std::unique_ptr<Clock>> m_clocks;
    std::vector<char> m_state;
    std::uint64_t m_lastFrame = ~std::uint64_t(0);
};

inline Clock& Clock::Get(const std::string& name) { return ClockSet::Global().Get(name); }
inline bool   Clock::Exists(const std::string& name) { return ClockSet::Global().Find(name) != nullptr; }

inline Stopwatch Stopwatch::OnClock(const std::string& name) {
    return Stopwatch([name] { const Clock* c = ClockSet::Global().Find(name); return c ? c->Elapsed() : 0.0; });
}

} // namespace okay
