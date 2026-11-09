#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Log.hpp"

#include <cmath>

namespace okay {

namespace {
// Marks Time as inside a fixed step for the callback's duration, even if it throws.
struct FixedScope {
    float step;
    explicit FixedScope(float s) : step(s) { Time::BeginFixedStep(); }
    ~FixedScope() { Time::EndFixedStep(step); }
};
}  // namespace

void GameLoop::Apply() {
    if (m_opt.maxFixedSteps < 1) m_opt.maxFixedSteps = 1;
    m_fixed.SetMaxSteps(m_opt.maxFixedSteps);
    m_stepSeconds = m_opt.fixedStep > 0.0f && std::isfinite(m_opt.fixedStep) ? m_opt.fixedStep : Time::FixedDeltaTime();
    m_fixed.SetStep(m_stepSeconds);
}

bool GameLoop::QuitRequested() const { return m_stop || Game::QuitRequested(); }
int  GameLoop::ExitCode() const { return Game::ExitCode(); }

void GameLoop::Reset() {
    m_fixed.Reset();
    m_steps = 0; m_alpha = 0.0f;
    m_real = m_clamped = m_stepped = 0.0f;
    m_frames = m_totalSteps = 0;
    m_dropped = m_lostReal = 0.0;
    m_hitches = m_clampedFrames = 0;
    m_lastHitch = m_worstHitch = 0.0f;
    m_wasPaused = false;
    m_stop = false;
}

void GameLoop::BeginSession() {
    Game::BeginSession();
    ClockSet::Global().Reset();
    Time::ResetFixedTime();
    Reset();
    Apply();
}

bool GameLoop::BeginFrame(double realDeltaSeconds) {
    // 1. A measurement that is not a finite number >= 0 (a clock that went backwards,
    //    NaN from a divide) is a 0 s frame, never a negative or poisoned step.
    double real = (realDeltaSeconds > 0.0 && std::isfinite(realDeltaSeconds)) ? realDeltaSeconds : 0.0;
    if (m_frames == 0 && m_opt.firstFrameDelta >= 0.0f) real = (double)m_opt.firstFrameDelta;
    m_real = (float)real;

    // 2. Hitch accounting on what was really measured.
    if (m_opt.hitchThreshold > 0.0f && real > (double)m_opt.hitchThreshold) {
        ++m_hitches;
        m_lastHitch = (float)real;
        if (m_lastHitch > m_worstHitch) m_worstHitch = m_lastHitch;
        if (m_opt.logHitches && (m_hitches <= 5 || m_hitches % 50 == 0))
            Log::Warning("Frame hitch: ", (int)(real * 1000.0), " ms (", m_hitches, " so far)");
    }

    // 3. Clamp, then maybe even out.
    double clamped = real;
    if (m_opt.maxDelta > 0.0f && std::isfinite(m_opt.maxDelta) && real > (double)m_opt.maxDelta) {
        clamped = m_opt.hitchReplacement > 0.0f ? (double)m_opt.hitchReplacement : (double)m_opt.maxDelta;
        ++m_clampedFrames;
        m_lostReal += real - clamped;
    }
    m_clamped = (float)clamped;
    m_stepped = m_opt.smoothDelta ? Time::SmoothDelta(m_clamped, m_opt.refreshHz) : m_clamped;
    if (OnHitch && m_opt.hitchThreshold > 0.0f && real > (double)m_opt.hitchThreshold) OnHitch(m_real, m_stepped);

    // 4. Pause transitions (Game already froze Time when the flag flipped).
    const bool paused = Game::Paused();
    if (paused != m_wasPaused) {
        m_wasPaused = paused;
        if (paused) { if (OnPause) OnPause(); }
        else        { if (OnResume) OnResume(); }
    }

    // 5. Time, then the clocks that follow it.
    Time::Step(m_stepped);
    if (m_opt.tickClocks) ClockSet::Global().TickFromTime();
    ++m_frames;

    // 6. Fixed steps on the scaled time (frozen while paused, slower in slow motion).
    m_stepSeconds = m_opt.fixedStep > 0.0f && std::isfinite(m_opt.fixedStep) ? m_opt.fixedStep : Time::FixedDeltaTime();
    if (m_fixed.Step() != m_stepSeconds) m_fixed.SetStep(m_stepSeconds);
    const double droppedBefore = m_fixed.DroppedTime();
    m_steps = m_fixed.Advance(Time::DeltaTime());
    m_dropped += m_fixed.DroppedTime() - droppedBefore;
    m_totalSteps += (std::uint64_t)m_steps;
    {
        const float a = m_fixed.Alpha();
        m_alpha = a < 0.0f ? 0.0f : (a >= 1.0f ? std::nextafter(1.0f, 0.0f) : a);
    }
    if (OnFixedUpdate) {
        for (int i = 0; i < m_steps; ++i) {
            FixedScope scope(m_stepSeconds);
            OnFixedUpdate(m_stepSeconds);
        }
    } else {
        for (int i = 0; i < m_steps; ++i) { Time::BeginFixedStep(); Time::EndFixedStep(m_stepSeconds); }
    }

    // 7. Per-frame updates on the scaled step.
    if (OnUpdate)     OnUpdate(Time::DeltaTime());
    if (OnLateUpdate) OnLateUpdate(Time::DeltaTime());

    return !QuitRequested();
}

}  // namespace okay
