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
//   0. refuses to run when a frame budget or a finished replay says the loop is done, and
//      returns at once when called from inside one of its own callbacks,
//   1. chooses the frame's real time: a replayed frame (AttachPlayer) first, else a debug
//      step (RequestStep, while paused), else the capture rate (Options::captureFps), else
//      the host's measurement cleaned up (NaN, infinity and negatives count as 0),
//   2. counts a hitch when that is longer than hitchThreshold (HitchCount, LastHitch...),
//      records the frame (AttachRecorder) and feeds Stats(),
//   3. clamps it to maxDelta (a breakpoint, a window drag or an alt-tab no longer
//      becomes one giant step), and optionally evens it out (Time::SmoothDelta). A time that
//      was set rather than measured (capture, debug step) is exact: never clamped or smoothed,
//   4. scales it by the debug time scale (SetDebugTimeScale),
//   5. fires OnPause / OnResume when Game::Paused() changed since the last frame,
//      OnStep for a debug step,
//   6. steps Time (so DeltaTime is the scaled step, 0 while paused except on a debug step),
//      then ticks the ClockSet (Clock::Get("ui").Delta() ...),
//   7. runs OnFixedUpdate once per fixed step that is due (at most maxFixedSteps, the
//      surplus is dropped, never owed: no spiral of death),
//   8. runs OnUpdate(Time::DeltaTime()) then OnLateUpdate(Time::DeltaTime()), once each,
//      then OnStepsFinished after the last requested debug step.
// Every callback is optional. A host that runs its own update just calls BeginFrame and
// reads FixedStepsThisFrame() / Alpha() / Time::DeltaTime().
//
// Callbacks are plain std::function; do not replace a callback from inside itself. The
// loop reads and writes the global Time and Game, so there is one per process.
#include "okay/Core/FrameStats.hpp"
#include "okay/Core/Time.hpp"
#include <cstdint>
#include <functional>

namespace okay {

class FrameRecorder;   // Core/FrameRecord.hpp
class FramePlayer;

class GameLoop {
public:
    struct Options {
        /// Real frame times longer than this are cut down (seconds; <= 0 turns the clamp off).
        float maxDelta = 0.25f;
        /// > 0: a clamped frame steps this long instead of maxDelta (Application uses its
        /// target frame time, as it always has). Never longer than the frame was (a clamp only
        /// shortens), and a value that is not a finite number > 0 means maxDelta.
        float hitchReplacement = 0.0f;
        /// A real frame longer than this is counted as a hitch (<= 0 turns counting off).
        float hitchThreshold = 0.1f;
        /// The fixed step in seconds; 0 follows Time::FixedDeltaTime().
        float fixedStep = 0.0f;
        /// Fixed steps one frame may run (>= 1, at most kMaxFixedStepsPerFrame). Time past that is dropped.
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

        // ---- capture, replay, stepping, diagnostics --------------------------------------
        /// > 0: capture mode. Every BeginFrame takes exactly 1 / captureFps seconds whatever the
        /// host passes (screenshots, video, golden images). Invalid values (NaN, <= 0, > 1000) are
        /// off. See SetCapture.
        float captureFps = 0.0f;
        /// > 0: the loop runs this many frames (counted from the last Reset / BeginSession,
        /// or from the SetOptions / SetFrameBudget that changed it), then BeginFrame returns
        /// false and runs no more. FramesRemaining() counts down.
        std::int64_t frameBudget = 0;
        /// Seconds one debug step (RequestStep) advances: 0 = one fixed step (or one capture
        /// frame in capture mode). RequestStep can override it per request.
        float stepDelta = 0.0f;
        /// Round the real time to whole microseconds even with no recorder attached (a
        /// recorder always does), so a run can be compared with a recording of it.
        bool quantizeMicros = false;
        /// With a FramePlayer attached: stop after its last frame (BeginFrame returns false).
        /// false: the host's own time is used again once the recording ends.
        bool stopAtPlaybackEnd = true;
        /// Frames of real time kept by Stats(); 0 turns the statistics off.
        int statsWindow = 240;
    };

    /// Largest debug time scale; a debug step may not be longer than kMaxStepDelta seconds;
    /// pending debug steps are capped at kMaxStepRequest.
    static constexpr float kMaxDebugTimeScale = 32.0f;
    /// No frame runs more fixed steps than this, whatever maxFixedSteps and a fast-forward say: a tiny
    /// fixedStep (a typo, 1e-9) with a huge cap used to keep one BeginFrame busy for billions of callbacks.
    static constexpr int   kMaxFixedStepsPerFrame = 10000;
    static constexpr float kMaxCaptureFps = 1000.0f;
    static constexpr float kMaxStepDelta = 1.0f;
    static constexpr int   kMaxStepRequest = 1000000;

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
    /// A debug step is about to run: (seconds it advances, debug steps still to come after it).
    std::function<void(float, int)> OnStep;
    /// The frame that ran the last requested debug step has finished (after OnLateUpdate).
    std::function<void()> OnStepsFinished;

    /// Run one frame for `realDeltaSeconds` of real time. Returns false when the loop
    /// should stop: Game::QuitRequested() (a PauseMenu, a script, a guarded RequestQuit
    /// that was accepted) or Stop(). The frame itself always runs to the end first.
    bool BeginFrame(double realDeltaSeconds);

    // ---- this frame --------------------------------------------------------------
    /// Fixed steps due in the last BeginFrame (all of them ran unless a callback threw).
    int   FixedStepsThisFrame() const { return m_steps; }
    /// How far the leftover time is into the next fixed step, in [0, 1): blend the
    /// previous and current simulated state by it when drawing.
    float Alpha() const { return m_alpha; }
    /// The real time this frame used: the host's measurement (after the first-frame rule),
    /// or the replayed, captured or stepped time that replaced it.
    float FrameDeltaReal() const { return m_real; }
    /// The real time after the max-delta clamp: what one frame is allowed to count as.
    float FrameDeltaClamped() const { return m_clamped; }
    /// What Time was stepped by (the clamped time, or its smoothed form, times the debug scale).
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

    // ---- frame stepping (while paused) ---------------------------------------------------
    // The pause stays (Game::Paused() reads true, no OnResume): each stepped frame runs one
    // frame of the simulation with a fixed time, then the game is back to standing still.
    //
    //     Game::SetPaused(true);
    //     loop.RequestStep();            // the next BeginFrame advances one fixed step
    //     loop.RequestStep(3, 1.0f / 60);// then three frames of 1/60 s
    //
    /// Advance `frames` frames of simulation, each `stepDelta` seconds of frame time (<= 0:
    /// Options::stepDelta, else one fixed step, else one capture frame), at the next BeginFrame
    /// calls. A step frame is exact (not clamped, smoothed or debug scaled), still goes through
    /// the gameplay time scale and hit stops, and Time::DeltaTime(), the fixed steps, the
    /// clocks (the "cinematic" one too) and OnUpdate all see it. Returns false, and does
    /// nothing, when the game is not paused or `frames` < 1. Steps already pending add up.
    /// Pending steps are dropped when the game resumes.
    bool RequestStep(int frames = 1, float stepDelta = 0.0f);
    void CancelStep() { m_stepsLeft = 0; m_stepDeltaReq = 0.0f; }
    /// A debug step is pending or running right now.
    bool IsStepping() const { return m_stepsLeft > 0 || m_inStepFrame; }
    /// Debug steps still to run (not counting one that is running).
    int  StepsRemaining() const { return m_stepsLeft; }
    /// Was the last BeginFrame a debug step?
    bool WasStepFrame() const { return m_wasStep; }
    /// What the next step advances, in seconds.
    float StepDelta() const;

    // ---- debug time scale -----------------------------------------------------------------
    /// Slow motion (< 1) or fast forward (> 1) for debugging, applied to the frame time before
    /// Time sees it: the whole game runs as if the machine were that much slower or faster. It
    /// composes with the gameplay time scale and hit stops (they are inside Time), it is not a
    /// game setting (never saved, not touched by Game::Reset, Time::SetTimeScale, a script, Reset
    /// or BeginSession) and debug steps ignore it. A fast forward raises the fixed step cap in
    /// proportion. False, and unchanged, for NaN, infinity or a negative; capped at
    /// kMaxDebugTimeScale. 0 stops the clock without pausing the game.
    bool  SetDebugTimeScale(float scale);
    float DebugTimeScale() const { return m_debugScale; }

    // ---- capture mode -----------------------------------------------------------------------
    /// Run at a fixed frame rate whatever the real time is: each BeginFrame takes exactly
    /// 1/fps seconds (exact: not clamped or smoothed), FrameCount advances by one, hit stops, pause
    /// reasons, clocks and the recorder all follow. `frames` > 0 also sets a frame budget.
    /// False, and unchanged, unless 0 < fps <= kMaxCaptureFps.
    bool SetCapture(float fps, std::int64_t frames = 0);
    void ClearCapture() { m_opt.captureFps = 0.0f; }
    bool  CaptureActive() const;
    float CaptureFps() const { return CaptureActive() ? m_opt.captureFps : 0.0f; }
    /// Stop after `frames` more frames (<= 0 removes the budget).
    void SetFrameBudget(std::int64_t frames);
    /// Frames left in the budget, or -1 with none.
    std::int64_t FramesRemaining() const;

    // ---- recording and replay ----------------------------------------------------------------
    /// Record every frame's time and flags into `recorder` (not owned; nullptr detaches). While
    /// one is attached the real time is rounded to whole microseconds so a replay is exact.
    void AttachRecorder(FrameRecorder* recorder) { m_recorder = recorder; }
    /// Take the frame times from `player` instead of the host (not owned; nullptr detaches).
    void AttachPlayer(FramePlayer* player) { m_player = player; }
    FrameRecorder* Recorder() const { return m_recorder; }
    FramePlayer*   Player() const { return m_player; }
    /// Did the last BeginFrame replay a recorded frame? / Is the recording used up (and the
    /// loop set to stop)?
    bool Replaying() const { return m_replaying; }
    bool PlaybackFinished() const;
    /// A number for the next frame, recorded with it (an input batch, a scene id); it applies to
    /// one frame and then goes back to 0. FrameMarker() is the last frame's (from the recording
    /// while replaying).
    void SetFrameMarker(int marker) { m_marker = marker; }
    int  FrameMarker() const { return m_frameMarker; }

    // ---- diagnostics ---------------------------------------------------------------------------
    /// The last Options::statsWindow frames' real times: average, 1% / 0.1% low, worst, hitches.
    const FrameStats& Stats() const { return m_stats; }
    FrameStats& Stats() { return m_stats; }

    // ---- control -----------------------------------------------------------------
    /// Ask the loop to stop after the current frame (BeginFrame then returns false).
    void Stop() { m_stop = true; }
    /// Stop() was called, Game asked to quit, the frame budget is used up or the replay ended.
    bool QuitRequested() const;
    /// The process exit code the game asked for (Game::RequestQuit(reason, code)).
    int  ExitCode() const;

    const Options& options() const { return m_opt; }
    void SetOptions(const Options& options);

    /// Forget this loop's counters, accumulator, pause baseline, pending debug steps and
    /// statistics (not Time, Game, the options, the debug time scale or what is attached).
    void Reset();
    /// Start a clean Play session or scene: Game::BeginSession() (no pause reasons, no
    /// quit, time scale 1, no hit stops), every clock back to zero, the fixed clock at 0,
    /// SmoothDelta's history forgotten, and Reset(). If the loop had told OnPause and the
    /// session starts un-paused, OnResume fires so the pair stays balanced. The editor's Play
    /// button and the player's scene load call this.
    void BeginSession();

private:
    void Apply();
    bool BudgetSpent() const;
    int  EffectiveMaxSteps() const;
    Options m_opt;
    FixedTimestep m_fixed;
    FrameStats m_stats;
    FrameRecorder* m_recorder = nullptr;
    FramePlayer*   m_player = nullptr;
    float  m_debugScale = 1.0f;
    int    m_stepsLeft = 0;
    float  m_stepDeltaReq = 0.0f;
    int    m_marker = 0, m_frameMarker = 0;
    std::uint64_t m_budgetBase = 0;
    bool   m_inFrame = false, m_inStepFrame = false, m_wasStep = false, m_replaying = false;
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
