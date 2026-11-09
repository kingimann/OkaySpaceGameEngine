#pragma once
// One per-frame driver for the console Application, the player and the editor's Play
// mode, so a hitch is handled the same way everywhere:
//
//     GameLoop loop;                                   // defaults: clamp frames to 0.25 s, 5 fixed steps
//     loop.OnFixedUpdate = [&](float dt) { Simulate(dt); };   // equal steps, 50 a second
//     loop.OnUpdate      = [&](float dt) { scene.Update(dt); };
//     loop.OnPause  = [&] { audio.Duck(true); };
//     loop.OnResume = [&] { audio.Duck(false); };
//
//     while (running) {
//         float real = MeasureRealFrameTime();         // whatever your window / timer gives
//         if (!loop.BeginFrame(real)) running = false; // false: Game quit was requested
//         Draw(loop.Alpha());                          // blend the last two fixed states by Alpha()
//     }
//
// BeginFrame does, in this order:
//   1. cleans the measured time (NaN, infinity and negatives count as 0),
//   2. counts a hitch when it is longer than hitchThreshold (HitchCount, LastHitch...),
//   3. clamps it to maxDelta (a breakpoint, a window drag or an alt-tab no longer
//      becomes one giant step), and optionally evens it out (Time::SmoothDelta),
//   4. fires OnPause / OnResume when Game::Paused() changed since the last frame,
//   5. steps Time (so DeltaTime is the scaled step, 0 while paused), then ticks the
//      ClockSet (Clock::Get("ui").Delta() ...),
//   6. runs OnFixedUpdate once per fixed step that is due (at most maxFixedSteps, the
//      surplus is dropped, never owed: no spiral of death),
//   7. runs OnUpdate(Time::DeltaTime()) then OnLateUpdate(Time::DeltaTime()), once each.
// Every callback is optional. A host that runs its own update just calls BeginFrame and
// reads FixedStepsThisFrame() / Alpha() / Time::DeltaTime().
//
// Callbacks are plain std::function; do not replace a callback from inside it. The
// loop reads and writes the global Time and Game, so there is one per process.
#include "okay/Core/Time.hpp"
#include <cstdint>
#include <functional>

namespace okay {

class GameLoop {
public:
    struct Options {
        /// Real frame times longer than this are cut down (seconds; <= 0 turns the clamp off).
        float maxDelta = 0.25f;
        /// > 0: a clamped frame steps this long instead of maxDelta (Application uses its
        /// target frame time, as it always has).
        float hitchReplacement = 0.0f;
        /// A real frame longer than this is counted as a hitch (<= 0 turns counting off).
        float hitchThreshold = 0.1f;
        /// The fixed step in seconds; 0 follows Time::FixedDeltaTime().
        float fixedStep = 0.0f;
        /// Fixed steps one frame may run (>= 1). Time past that is dropped.
        int maxFixedSteps = 5;
        /// Step by an evened-out time (Time::SmoothDelta) instead of the clamped one.
        bool smoothDelta = false;
        /// With smoothDelta: the display's refresh rate for vsync snapping (0 = unknown).
        float refreshHz = 0.0f;
        /// >= 0: the first frame steps this long whatever was measured (the first
        /// measurement usually holds loading time). < 0 uses what the host passes.
        float firstFrameDelta = -1.0f;
        /// Warn in the log about hitches (the first few, then every 50th).
        bool logHitches = false;
        /// Tick ClockSet::Global() each frame.
        bool tickClocks = true;
    };

    GameLoop() = default;
    explicit GameLoop(const Options& options) : m_opt(options) { Apply(); }

    // ---- callbacks -----------------------------------------------------------
    std::function<void(float)> OnFixedUpdate;   ///< (fixedStepSeconds), 0..maxFixedSteps times a frame
    std::function<void(float)> OnUpdate;        ///< (Time::DeltaTime())
    std::function<void(float)> OnLateUpdate;    ///< (Time::DeltaTime()), after OnUpdate
    std::function<void()>      OnPause;         ///< Game became paused (before this frame's step)
    std::function<void()>      OnResume;        ///< Game became un-paused
    /// (real seconds measured, seconds stepped) when a frame is a hitch.
    std::function<void(float, float)> OnHitch;

    /// Run one frame for `realDeltaSeconds` of real time. Returns false when the loop
    /// should stop: Game::QuitRequested() (a PauseMenu, a script, a guarded RequestQuit
    /// that was accepted) or Stop(). The frame itself always runs to the end first.
    bool BeginFrame(double realDeltaSeconds);

    // ---- this frame --------------------------------------------------------------
    /// Fixed steps the last BeginFrame ran.
    int   FixedStepsThisFrame() const { return m_steps; }
    /// How far the leftover time is into the next fixed step, in [0, 1): blend the
    /// previous and current simulated state by it when drawing.
    float Alpha() const { return m_alpha; }
    /// The measured real time this frame (after the first-frame rule), as the host passed it.
    float FrameDeltaReal() const { return m_real; }
    /// The real time after the max-delta clamp: what one frame is allowed to count as.
    float FrameDeltaClamped() const { return m_clamped; }
    /// What Time was stepped by (the clamped time, or its smoothed form).
    float FrameDeltaStepped() const { return m_stepped; }
    float FixedStep() const { return m_stepSeconds; }

    // ---- bookkeeping ----------------------------------------------------------
    std::uint64_t FrameCount() const { return m_frames; }
    std::uint64_t TotalFixedSteps() const { return m_totalSteps; }
    /// Seconds of scaled time dropped because a frame owed more than maxFixedSteps.
    double DroppedFixedTime() const { return m_dropped; }
    /// Frames longer than hitchThreshold, the length (real seconds) of the last and the
    /// longest, and how many frames the max-delta clamp shortened.
    int    HitchCount() const { return m_hitches; }
    float  LastHitch() const { return m_lastHitch; }
    float  WorstHitch() const { return m_worstHitch; }
    int    ClampedFrames() const { return m_clampedFrames; }
    /// Real seconds the clamp threw away in total.
    double LostRealTime() const { return m_lostReal; }

    // ---- control -----------------------------------------------------------------
    /// Ask the loop to stop after the current frame (BeginFrame then returns false).
    void Stop() { m_stop = true; }
    bool QuitRequested() const;
    /// The process exit code the game asked for (Game::RequestQuit(reason, code)).
    int  ExitCode() const;

    const Options& options() const { return m_opt; }
    void SetOptions(const Options& options) { m_opt = options; Apply(); }

    /// Forget this loop's counters, accumulator and pause baseline (not Time or Game).
    void Reset();
    /// Start a clean Play session or scene: Game::BeginSession() (no pause reasons, no
    /// quit, time scale 1, no hit stops), every clock back to zero, the fixed clock at 0,
    /// and Reset(). The editor's Play button and the player's scene load call this.
    void BeginSession();

private:
    void Apply();
    Options m_opt;
    FixedTimestep m_fixed;
    int    m_steps = 0;
    float  m_alpha = 0.0f;
    float  m_real = 0.0f, m_clamped = 0.0f, m_stepped = 0.0f, m_stepSeconds = 0.02f;
    std::uint64_t m_frames = 0, m_totalSteps = 0;
    double m_dropped = 0.0, m_lostReal = 0.0;
    int    m_hitches = 0, m_clampedFrames = 0;
    float  m_lastHitch = 0.0f, m_worstHitch = 0.0f;
    bool   m_wasPaused = false;
    bool   m_stop = false;
};

} // namespace okay
