#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/FrameRecord.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Log.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

namespace okay {

namespace {
// Marks Time as inside a fixed step for the callback's duration, even if it throws.
struct FixedScope {
    float step;
    explicit FixedScope(float s) : step(s) { Time::BeginFixedStep(); }
    ~FixedScope() { Time::EndFixedStep(step); }
};

// A debug step runs on a paused game: lift the freeze for the frame, put it back after (even
// if a callback throws), to whatever the pause state is by then.
struct StepFreeze {
    bool active;
    explicit StepFreeze(bool a) : active(a) { if (active) Time::SetFrozen(false); }
    ~StepFreeze() { if (active) Time::SetFrozen(Game::Paused()); }
};

// Sets a flag for a scope when `on`, and clears it however the scope ends.
struct ScopedFlag {
    bool& flag;
    bool on;
    ScopedFlag(bool& f, bool o) : flag(f), on(o) { if (on) flag = true; }
    ~ScopedFlag() { if (on) flag = false; }
};

constexpr float kFallbackStep = 1.0f / 60.0f;
}  // namespace

void GameLoop::Apply() {
    if (m_opt.maxFixedSteps < 1) m_opt.maxFixedSteps = 1;
    m_fixed.SetMaxSteps(EffectiveMaxSteps());
    m_stepSeconds = m_opt.fixedStep > 0.0f && std::isfinite(m_opt.fixedStep) ? m_opt.fixedStep : Time::FixedDeltaTime();
    m_fixed.SetStep(m_stepSeconds);
    m_stats.SetWindow(m_opt.statsWindow > 0 ? (std::size_t)m_opt.statsWindow : 0);
    m_stats.SetHitchMs(m_opt.hitchThreshold > 0.0f ? (double)m_opt.hitchThreshold * 1000.0 : 0.0);
}

void GameLoop::SetOptions(const Options& options) {
    const bool budgetChanged = options.frameBudget != m_opt.frameBudget;
    m_opt = options;
    if (budgetChanged) m_budgetBase = m_frames;   // a new budget counts from now
    Apply();
}

int GameLoop::EffectiveMaxSteps() const {
    // A fast forward owes proportionally more steps a frame; the cap grows with it.
    const double mult = m_debugScale > 1.0f ? std::ceil((double)m_debugScale) : 1.0;
    const double cap = (double)std::max(1, m_opt.maxFixedSteps) * mult;
    return cap >= (double)kMaxFixedStepsPerFrame ? kMaxFixedStepsPerFrame : (int)cap;
}

bool GameLoop::BudgetSpent() const {
    return m_opt.frameBudget > 0 && (std::int64_t)(m_frames - std::min(m_frames, m_budgetBase)) >= m_opt.frameBudget;
}

bool GameLoop::PlaybackFinished() const {
    return m_player && m_opt.stopAtPlaybackEnd && !m_player->HasNext();
}

bool GameLoop::QuitRequested() const { return m_stop || Game::QuitRequested() || BudgetSpent() || PlaybackFinished(); }
int  GameLoop::ExitCode() const { return Game::ExitCode(); }

bool GameLoop::CaptureActive() const {
    return m_opt.captureFps > 0.0f && std::isfinite(m_opt.captureFps) && m_opt.captureFps <= kMaxCaptureFps;
}

bool GameLoop::SetCapture(float fps, std::int64_t frames) {
    if (!(fps > 0.0f) || !std::isfinite(fps) || fps > kMaxCaptureFps) return false;
    m_opt.captureFps = fps;
    if (frames > 0) SetFrameBudget(frames);
    return true;
}

void GameLoop::SetFrameBudget(std::int64_t frames) {
    m_opt.frameBudget = frames > 0 ? frames : 0;
    m_budgetBase = m_frames;
}

std::int64_t GameLoop::FramesRemaining() const {
    if (m_opt.frameBudget <= 0) return -1;
    const std::int64_t used = (std::int64_t)(m_frames - std::min(m_frames, m_budgetBase));
    return used >= m_opt.frameBudget ? 0 : m_opt.frameBudget - used;
}

bool GameLoop::SetDebugTimeScale(float scale) {
    if (!(scale >= 0.0f) || !std::isfinite(scale)) return false;
    m_debugScale = scale < kMaxDebugTimeScale ? scale : kMaxDebugTimeScale;
    return true;
}

bool GameLoop::RequestStep(int frames, float stepDelta) {
    if (frames < 1 || !Game::Paused()) return false;
    const long long total = (long long)m_stepsLeft + (long long)frames;
    m_stepsLeft = total > kMaxStepRequest ? kMaxStepRequest : (int)total;
    if (stepDelta > 0.0f && std::isfinite(stepDelta)) m_stepDeltaReq = std::min(stepDelta, kMaxStepDelta);
    return true;
}

bool GameLoop::RunFrames(int frames, float frameSeconds, const std::string& pauseReason) {
    if (frames < 1) return false;
    if (Game::Paused()) return RequestStep(frames, frameSeconds);   // already paused: the run is a step request
    m_runLeft = frames > kMaxStepRequest ? kMaxStepRequest : frames;
    m_runDelta = (frameSeconds > 0.0f && std::isfinite(frameSeconds)) ? std::min(frameSeconds, kMaxStepDelta) : 0.0f;
    m_runReason = pauseReason.empty() ? std::string("manual") : pauseReason;
    return true;
}

float GameLoop::NextDebugTimeScale(float current, int direction) {
    if (!(current >= 0.0f) || !std::isfinite(current)) current = 1.0f;
    if (direction == 0) return current;
    if (direction > 0) {
        for (int i = 0; i < kDebugScalePresetCount; ++i)
            if (kDebugScalePresets[i] > current * 1.0001f) return kDebugScalePresets[i];
        return kDebugScalePresets[kDebugScalePresetCount - 1];
    }
    for (int i = kDebugScalePresetCount - 1; i >= 0; --i)
        if (kDebugScalePresets[i] < current * 0.9999f) return kDebugScalePresets[i];
    return kDebugScalePresets[0];
}

float GameLoop::StepDelta() const {
    if (m_stepDeltaReq > 0.0f) return m_stepDeltaReq;
    if (m_opt.stepDelta > 0.0f && std::isfinite(m_opt.stepDelta)) return std::min(m_opt.stepDelta, kMaxStepDelta);
    if (CaptureActive()) return 1.0f / m_opt.captureFps;
    return m_stepSeconds > 0.0f && std::isfinite(m_stepSeconds) ? m_stepSeconds : kFallbackStep;
}

void GameLoop::Reset() {
    m_fixed.Reset();
    m_steps = 0; m_alpha = 0.0f;
    m_real = m_clamped = m_stepped = 0.0f;
    m_frames = m_totalSteps = 0;
    m_budgetBase = 0;
    m_dropped = m_lostReal = 0.0;
    m_hitches = m_clampedFrames = 0;
    m_lastHitch = m_worstHitch = 0.0f;
    m_wasPaused = false;
    m_stop = false;
    m_stepsLeft = 0; m_stepDeltaReq = 0.0f; m_wasStep = false; m_inStepFrame = false;
    m_runLeft = 0; m_runDelta = 0.0f;
    m_replaying = false;
    m_marker = m_frameMarker = 0;
    m_stats.Clear();
}

void GameLoop::BeginSession() {
    const bool wasPaused = m_wasPaused;
    Game::BeginSession();
    ClockSet::Global().Reset();
    Time::ResetFixedTime();
    Time::ResetSmoothing();
    Reset();
    Apply();
    // Balance the OnPause the loop announced: the session starts un-paused (unless a focus
    // loss holds it), and a game that stays paused is not announced a second time.
    m_wasPaused = wasPaused && Game::Paused();
    if (wasPaused && !m_wasPaused && OnResume) OnResume();
}

bool GameLoop::BeginFrame(double realDeltaSeconds) {
    // A callback that pumps the loop (a modal dialog) must not start a second frame inside this one.
    if (m_inFrame) return !QuitRequested();
    // A used-up budget or a finished replay runs no more frames.
    if (BudgetSpent() || PlaybackFinished()) return false;
    ScopedFlag inFrame(m_inFrame, true);

    // 1. Where this frame's time comes from.
    //    A measurement that is not a finite number >= 0 (a clock that went backwards, NaN from
    //    a divide) is a 0 s frame, never a negative or poisoned step.
    double real = (realDeltaSeconds > 0.0 && std::isfinite(realDeltaSeconds)) ? realDeltaSeconds : 0.0;
    bool exact = false;        // set rather than measured: not clamped, smoothed or counted as a hitch
    bool stepFrame = false;
    bool replaying = false;
    int marker = m_marker;
    m_marker = 0;
    FrameSample played;
    if (m_player && m_player->Next(played)) {
        replaying = true;
        real = FrameRecording::ToSeconds(played.micros);
        marker = played.marker;
        if (played.flags & kFrameSynthetic) exact = true;
        if ((played.flags & kFrameStepped) && Game::Paused()) { stepFrame = true; exact = true; }
    } else if (m_stepsLeft > 0) {
        if (Game::Paused()) {
            stepFrame = true; exact = true;
            real = (double)StepDelta();
            if (--m_stepsLeft == 0) m_stepDeltaReq = 0.0f;   // the request is used up
        } else {
            m_stepsLeft = 0; m_stepDeltaReq = 0.0f;           // resumed: pending debug steps are moot
        }
    }
    // A frame of RunFrames' run: the game is running and nothing replaces this frame's time.
    const bool runFrame = m_runLeft > 0 && !replaying && !stepFrame && !Game::Paused();
    if (!replaying && !stepFrame) {
        if (runFrame && m_runDelta > 0.0f) { real = (double)m_runDelta; exact = true; }
        else if (CaptureActive()) { real = 1.0 / (double)m_opt.captureFps; exact = true; }
        else if (m_frames == 0 && m_opt.firstFrameDelta >= 0.0f) real = (double)m_opt.firstFrameDelta;
    }
    // The frame time is kept as a float: a measurement beyond that range (a broken timer) is
    // the largest float, not an infinity (converting it would be undefined, and OnHitch and
    // FrameDeltaReal() would read infinity).
    if (real > (double)std::numeric_limits<float>::max()) real = (double)std::numeric_limits<float>::max();
    // While recording (or on request) the time is a whole number of microseconds, so a
    // replay of the recording steps Time by the very same numbers.
    if (!replaying && (m_recorder || m_opt.quantizeMicros)) real = FrameRecording::Quantize(real);
    m_real = (float)real;
    m_replaying = replaying;

    // 2. Hitch accounting on what was really measured, then the record and the statistics.
    const bool pausedAtStart = Game::Paused();
    bool hitch = false;
    if (!exact && m_opt.hitchThreshold > 0.0f && real > (double)m_opt.hitchThreshold) {
        hitch = true;
        ++m_hitches;
        m_lastHitch = (float)real;
        if (m_lastHitch > m_worstHitch) m_worstHitch = m_lastHitch;
        if (m_opt.logHitches && (m_hitches <= 5 || m_hitches % 50 == 0))
            Log::Warning("Frame hitch: ", (int)(real * 1000.0), " ms (", m_hitches, " so far)");
    }
    {
        std::uint8_t flags = 0;
        if (pausedAtStart) flags |= kFramePaused;
        if (hitch) flags |= kFrameHitch;
        if (stepFrame) flags |= kFrameStepped;
        else if (exact) flags |= kFrameSynthetic;
        if (m_recorder) m_recorder->Add(real, flags, marker);
        m_frameMarker = marker;
        m_stats.Push(real * 1000.0);
    }

    // 3. Clamp, then maybe even out. An exact time is used as it is.
    double clamped = real;
    if (!exact && m_opt.maxDelta > 0.0f && std::isfinite(m_opt.maxDelta) && real > (double)m_opt.maxDelta) {
        // The replacement is what a clamped frame steps instead of maxDelta. It is only used when it is a
        // real number, and a clamp never lengthens a frame (a replacement above the real time used to make
        // the game step more than it had measured, count a negative lost time, and an infinite one made
        // both of them infinite).
        const double replacement = (m_opt.hitchReplacement > 0.0f && std::isfinite(m_opt.hitchReplacement))
                                       ? (double)m_opt.hitchReplacement : (double)m_opt.maxDelta;
        clamped = std::min(replacement, real);
        if (clamped < real) {
            ++m_clampedFrames;
            m_lostReal += real - clamped;
        }
    }
    m_clamped = (float)clamped;
    m_stepped = (m_opt.smoothDelta && !exact) ? Time::SmoothDelta(m_clamped, m_opt.refreshHz) : m_clamped;
    // 4. The debug scale: slow motion or fast forward for the whole game (not for a debug step).
    if (!stepFrame && m_debugScale != 1.0f) {
        const double scaled = (double)m_stepped * (double)m_debugScale;   // held in float range (a huge frame at 32x)
        m_stepped = scaled < (double)std::numeric_limits<float>::max() ? (float)scaled : std::numeric_limits<float>::max();
    }
    if (OnHitch && hitch) OnHitch(m_real, m_stepped);

    // 5. Pause transitions (Game already froze Time when the flag flipped).
    const bool paused = Game::Paused();
    if (paused != m_wasPaused) {
        m_wasPaused = paused;
        if (paused) { if (OnPause) OnPause(); }
        else        { if (OnResume) OnResume(); }
    }
    m_wasStep = stepFrame;
    ScopedFlag stepping(m_inStepFrame, stepFrame);
    if (stepFrame && OnStep) OnStep(m_stepped, m_stepsLeft);

    // 6. Time, then the clocks that follow it. A debug step lifts the freeze for this frame, and
    //    the "cinematic" clock (which holds while the game is paused) runs on it too.
    StepFreeze freeze(stepFrame);
    Time::Step(m_stepped);
    if (m_opt.tickClocks)
        ClockSet::Global().Tick(Time::DeltaTime(), Time::UnscaledDeltaTime(), stepFrame ? false : Game::Paused());
    ++m_frames;

    // 7. Fixed steps on the scaled time (frozen while paused, slower in slow motion).
    m_stepSeconds = m_opt.fixedStep > 0.0f && std::isfinite(m_opt.fixedStep) ? m_opt.fixedStep : Time::FixedDeltaTime();
    if (m_fixed.Step() != m_stepSeconds) m_fixed.SetStep(m_stepSeconds);
    const int cap = EffectiveMaxSteps();
    if (m_fixed.MaxSteps() != cap) m_fixed.SetMaxSteps(cap);
    const double droppedBefore = m_fixed.DroppedTime();
    m_steps = m_fixed.Advance(Time::DeltaTime());
    m_dropped += m_fixed.DroppedTime() - droppedBefore;
    {
        const float a = m_fixed.Alpha();
        m_alpha = a < 0.0f ? 0.0f : (a >= 1.0f ? std::nextafter(1.0f, 0.0f) : a);
    }
    // Locals: a callback may Reset() the loop or change its options; this frame's steps keep
    // the count and the length they were due with. A step that throws counts as run.
    const int dueSteps = m_steps;
    const float stepSeconds = m_stepSeconds;
    for (int i = 0; i < dueSteps; ++i) {
        ++m_totalSteps;
        FixedScope scope(stepSeconds);
        if (OnFixedUpdate) OnFixedUpdate(stepSeconds);
    }

    // 8. Per-frame updates on the scaled step.
    if (OnUpdate)     OnUpdate(Time::DeltaTime());
    if (OnLateUpdate) OnLateUpdate(Time::DeltaTime());

    m_inStepFrame = false;
    if (stepFrame && !replaying && m_stepsLeft == 0 && OnStepsFinished) OnStepsFinished();

    // The last frame of a RunFrames run: pause the game (once per reason), then tell the host. The count
    // may have been cleared by a callback (Reset, CancelRunFrames): then there is nothing to finish.
    if (runFrame && m_runLeft > 0 && --m_runLeft == 0) {
        m_runDelta = 0.0f;
        if (!Game::IsPausedBy(m_runReason)) Game::PushPause(m_runReason);
        if (OnRunFramesDone) OnRunFramesDone();
    }

    return !QuitRequested();
}

}  // namespace okay
