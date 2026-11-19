#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace okay {

/// Global frame timing, mirroring static `Time` class.
/// Values are updated once per frame by the Application loop.
class Time {
public:
    /// Seconds elapsed since the last frame (scaled by timeScale).
    static float DeltaTime()   { return s_deltaTime; }
    /// Unscaled seconds elapsed since the last frame.
    static float UnscaledDeltaTime() { return s_unscaledDeltaTime; }
    /// Seconds since the application started (scaled). A float, so it loses
    /// resolution on long runs; ElapsedTimeD() keeps full precision.
    static float ElapsedTime() { return (float)s_time; }
    /// Same as ElapsedTime(), accumulated and returned as a double.
    static double ElapsedTimeD() { return s_time; }
    /// Seconds since the application started, ignoring timeScale (double).
    static double UnscaledTime() { return s_unscaledTime; }
    /// Number of frames rendered since startup.
    static std::uint64_t FrameCount() { return s_frameCount; }

    // ---- Whole nanoseconds: the exact clock ---------------------------------------------------
    // A frame time reaches the game as a float (1/60 s is 0.016666667 as a float, 9e-10 s too long), and
    // a float second counter cannot tell 144 fps frames apart after a day (its step is 8 ms at 24 h). So the
    // game keeps its time as a whole number of nanoseconds (int64: 292 years) and adds each frame's time
    // to it with the leftover fraction carried to the next frame, so after any number of frames the clock
    // is within one nanosecond of the exact sum. ElapsedTimeD() and UnscaledTime() are that count seen as
    // seconds; ElapsedTime() is the float view. Feed it exact doubles with StepD (GameLoop does).
    /// Scaled game time since startup, whole nanoseconds.
    static std::int64_t ElapsedTicks() { return s_ticks; }
    /// Real (unscaled) time since startup, whole nanoseconds.
    static std::int64_t UnscaledTicks() { return s_unscaledTicks; }
    static constexpr double kTicksPerSecond = 1.0e9;
    /// The last frame's scaled / unscaled time as an exact double (DeltaTime() is the float view).
    static double DeltaTimeD() { return s_deltaD; }
    static double UnscaledDeltaTimeD() { return s_unscaledDeltaD; }

    // ---- This session (since the last Game::BeginSession) -----------------------------------
    // ElapsedTime(), UnscaledTime() and FrameCount() are the PROCESS's clocks: they only move
    // forward and a new Play session (the editor's Stop and Play, a scene load) does not rewind
    // them, because much code keeps a stamp taken from them (a cooldown's "ready at", a save
    // game's play-time base, a scheduler's frame number) and a clock that jumped back would make
    // every such stamp read as the future. A script that wants "since this Play began" (an intro
    // that shows for the first five seconds of each Play) reads these instead. Game::BeginSession()
    // (and so GameLoop::BeginSession(), which the editor calls in Play and Stop) marks the start;
    // before the first mark they equal the process clocks.
    /// Scaled game seconds since the session began.
    static double SessionTime() { return s_time > s_sessionStartTime ? s_time - s_sessionStartTime : 0.0; }
    /// Real (unscaled) seconds since the session began.
    static double SessionUnscaledTime() { return s_unscaledTime > s_sessionStartUnscaled ? s_unscaledTime - s_sessionStartUnscaled : 0.0; }
    /// Frames stepped since the session began.
    static std::uint64_t SessionFrameCount() { return s_frameCount > s_sessionStartFrame ? s_frameCount - s_sessionStartFrame : 0; }
    /// Start a session now (Game::BeginSession calls it). Does not touch the process clocks.
    static void MarkSessionStart() {
        s_sessionStartTime = s_time; s_sessionStartUnscaled = s_unscaledTime; s_sessionStartFrame = s_frameCount;
    }
    /// Multiplier applied to DeltaTime (1 = normal, 0 = paused).
    /// Reads 0 while Game has the time frozen (paused), whatever scale is remembered.
    static float TimeScale()   { return s_frozen ? 0.0f : s_timeScale; }
    /// Negative (and NaN) scales are clamped to 0 so DeltaTime never runs backwards, and
    /// the scale is capped at kMaxTimeScale (an infinite scale made 0 * inf = NaN
    /// poison ElapsedTime for the rest of the run). Setting it cancels a running ramp.
    static void  SetTimeScale(float scale) {
        s_timeScale = scale > 0.0f ? (scale < kMaxTimeScale ? scale : kMaxTimeScale) : 0.0f;
        if (s_fxActive) CancelRamp();
    }
    static constexpr float kMaxTimeScale = 100.0f;
    /// The scale as last set (a ramp moves it), ignoring the freeze and any hit stop.
    static float BaseTimeScale() { return s_timeScale; }
    /// The multiplier DeltaTime actually used: the scale, 0 while frozen, times the
    /// strongest running hit stop. TimeScale() stays the scale you set, so code that
    /// reads it and writes it back (a cutscene restoring the speed) never captures a
    /// hit stop; divide by this one to get back to the unscaled frame time.
    static float EffectiveTimeScale() { return s_frozen ? 0.0f : s_timeScale * s_hitMul; }
    /// The real frame time a scaled step `dt` stands for (what "unscaled" timers and
    /// tweens run on). Given this frame's DeltaTime() it returns the frame's exact
    /// UnscaledDeltaTime(): dividing by EffectiveTimeScale() is wrong in a frame where a
    /// hit stop ends or a ramp moves the scale, because DeltaTime integrated the scale
    /// over the frame but the scale read afterwards is the end value. For any other `dt`
    /// it undoes the effective scale, and at scale 0 falls back to UnscaledDeltaTime().
    static float UnscaledFor(float dt) {
        if (dt == s_deltaTime) return s_unscaledDeltaTime;
        const float ts = EffectiveTimeScale();
        return ts > 1e-6f ? dt / ts : s_unscaledDeltaTime;
    }

    // ---- Time effects (Time.cpp) ------------------------------------------------
    // Both are measured in REAL seconds, keep running while the game is paused, and
    // compose with the scale instead of overwriting it, so a script's own SetTimeScale
    // is never lost. DeltaTime integrates the scale over the frame, so a 0.1 s stop
    // takes exactly 0.1 s * scale of game time out, whatever the frame length.

    /// A brief slow-down for impacts: for `seconds` of real time the game runs at
    /// `scale` (0 = a full freeze frame) times the current scale, then goes back by
    /// itself. Several at once: the strongest (lowest scale) applies; each ends on its
    /// own clock. Ignored for a duration that is not a positive finite number.
    static void HitStop(float seconds, float scale = 0.0f);
    static bool  HitStopActive() { return s_hitCount > 0; }
    /// Real seconds until the last running hit stop ends (0 when none).
    static float HitStopRemaining();
    static void  CancelHitStop();

    /// Move the scale to `target` over `seconds` of real time, in a straight line
    /// (slow-motion ease in/out, a pause menu fading the world to a stop). SetTimeScale
    /// (or another ramp) takes over from wherever it was. `seconds` <= 0 sets it at once.
    static void RampTimeScale(float target, float seconds);
    static bool IsRamping();
    static float RampTarget();
    /// Real seconds the running ramp still has to go (0 when there is none).
    static float RampSecondsLeft();
    static void CancelRamp();

    /// Hold DeltaTime at 0 without touching the scale: SetTimeScale calls made while
    /// frozen are remembered and apply when it is lifted. Game::SetPaused drives this.
    static void SetFrozen(bool frozen) { s_frozen = frozen; }
    static bool Frozen() { return s_frozen; }

    /// The length of one fixed gameplay step (default 0.02 s = 50 steps a second, the
    /// rate scripts' FixedUpdate and physics run at). FixedTimestep reads it when it
    /// is built without a step. A step that is not a finite positive number is ignored.
    static float FixedDeltaTime() { return s_fixedDelta; }
    static void  SetFixedDeltaTime(float step) { if (step > 0.0f && std::isfinite(step)) s_fixedDelta = step; }
    /// The simulated clock of the fixed steps: the start of the step being run inside
    /// a fixed update, else the total fixed time run so far. Only a driver that runs
    /// fixed steps (GameLoop) advances it; InFixedStep() is true while one runs.
    static double FixedTime() { return s_fixedTime; }
    static bool   InFixedStep() { return s_inFixed; }
    /// How far the leftover time is into the next fixed step, in [0, 1): what to blend the previous and
    /// current simulated state by when drawing. GameLoop::Alpha() is the same number for code that holds the
    /// loop; this one is for code that does not (a camera, a renderer, a component that interpolates). A
    /// driver publishes it once a frame with SetFixedAlpha (GameLoop does); 0 before one has, and a value that
    /// is not in [0, 1) (NaN included) is cut to that range.
    static float FixedAlpha() { return s_fixedAlpha; }
    static void  SetFixedAlpha(float a) { s_fixedAlpha = (a > 0.0f && a < 1.0f) ? a : (a >= 1.0f ? std::nextafter(1.0f, 0.0f) : 0.0f); }
    /// For the driver: bracket each fixed update with these.
    static void BeginFixedStep() { s_inFixed = true; }
    static void EndFixedStep(float step) { s_inFixed = false; if (step > 0.0f && std::isfinite(step)) s_fixedTime += (double)step; }
    static void ResetFixedTime() { s_fixedTime = 0.0; s_inFixed = false; s_fixedAlpha = 0.0f; }

    // ---- Real and wall-clock time (Time.cpp) -----------------------------------
    // The game clocks above only move when a frame is stepped. These read the machine.
    using ClockFn = double (*)();
    /// Seconds on a monotonic clock since the process started: never goes backwards,
    /// ignores the time scale, pause and any clamped hitch.
    static double RealtimeSinceStartup();
    /// Wall-clock seconds since 1970-01-01 UTC (can jump when the user sets the clock).
    static double UnixSeconds();
    /// Test hook: replace the monotonic clock (seconds) and/or the wall clock (Unix
    /// seconds) with your own so output is deterministic. nullptr puts the real one back.
    static void SetClockSources(ClockFn monotonic, ClockFn unixSeconds);
    /// "1h 02m 03s", "2m 05s", "7s", "2d 03h 04m 05s". Whole seconds, rounded down so
    /// it never shows a moment that has not happened yet. Negative gets a leading "-".
    static std::string FormatDuration(double seconds);
    /// A clock face: "MM:SS" under an hour, "H:MM:SS" from one hour; `millis` adds
    /// ".mmm" ("01:02.500"). Rounded down; negative gets a leading "-".
    static std::string FormatClock(double seconds, bool millis = false);

    /// Frames per second, smoothed, from the measured frame times. A single frame is read as at most kMaxFps.
    static float SmoothedFPS() { return s_fps; }
    static constexpr float kMaxFps = 1.0e6f;
    /// The last frame's time as measured (before SmoothDelta), for FPS readouts
    /// and profiling. The game itself steps by DeltaTime.
    static float RawDeltaTime() { return s_rawDeltaTime; }

    /// Even frame steps for movement and cameras. A measured frame time jitters
    /// around the display's refresh period even at a steady frame rate; moving by
    /// it while the screen shows frames at a fixed rate judders. With vsync on
    /// and the refresh rate known (`refreshHz` > 0), a frame within 15% of a
    /// whole number of refresh periods steps exactly that much (the difference
    /// is carried, so time never drifts by more than a period); otherwise the
    /// step is the average of the last 4 frames, kept within [raw/2, raw*2].
    /// 0 (paused) stays 0. Call it once a frame on the measured time, then pass
    /// the result to Step.
    static float SmoothDelta(float raw, float refreshHz = 0.0f) {
        if (!(raw > 0.0f) || !std::isfinite(raw)) { s_rawPending = 0.0f; return 0.0f; }   // paused, NaN or infinite: no step
        s_rawPending = raw;
        s_hist[s_histAt] = raw;
        s_histAt = (s_histAt + 1) % 4;
        if (s_histN < 4) ++s_histN;
        if (refreshHz > 0.0f) {
            const float period = 1.0f / refreshHz;
            const float n = std::round(raw / period);
            if (n >= 1.0f && std::fabs(raw - n * period) <= 0.15f * period) {
                float step = n * period;
                s_carry += raw - step;
                if (s_carry > period) { step += period; s_carry -= period; }
                else if (s_carry < -period && step > period) { step -= period; s_carry += period; }
                return step;
            }
        }
        s_carry = 0.0f;
        float sum = 0.0f;
        for (int i = 0; i < s_histN; ++i) sum += s_hist[i];
        const float avg = sum / (float)s_histN;
        const float v = avg < raw * 0.5f ? raw * 0.5f : (avg > raw * 2.0f ? raw * 2.0f : avg);
        // For a frame time near the top of the float range the sum, or twice the frame, overflows to infinity.
        return std::isfinite(v) ? v : std::numeric_limits<float>::max();
    }

    /// Advance global time by one frame of `unscaledDt` seconds. The Application
    /// loop calls this automatically; standalone runtimes (the player) call it so
    /// `ElapsedTime()`, `DeltaTime()`, and `timeScale` work there too. A time that is
    /// not a finite number >= 0 (NaN, infinity, negative) counts as a 0 s frame: it
    /// used to turn ElapsedTime into NaN for good, or run it backwards.
    static void Step(float unscaledDt) { Advance((double)unscaledDt); }
    /// Step() with the frame time as a double: no float rounding of 1/60 s, so a long run stays exact
    /// (see "Whole nanoseconds" above). A time that is not a finite number >= 0 counts as 0, as in Step.
    static void StepD(double unscaledDt) { Advance(unscaledDt); }

    /// Put every clock back to its start-up state: zero elapsed time and frame
    /// count, time scale 1, not frozen, no smoothing history. The automated test
    /// runner (okay::testing::PlayTest) calls it so each test starts from the
    /// same clock; a game has no reason to.
    static void ResetForTesting() {
        s_deltaTime = s_unscaledDeltaTime = 0.0f;
        s_deltaD = s_unscaledDeltaD = 0.0;
        s_time = s_unscaledTime = 0.0;
        s_ticks = s_unscaledTicks = 0; s_tickCarry = s_unscaledTickCarry = 0.0;
        s_timeScale = 1.0f; s_frozen = false;
        s_fps = 0.0f; s_fpsSamples = 0; s_frameCount = 0;
        s_rawDeltaTime = 0.0f; s_rawPending = -1.0f;
        for (float& h : s_hist) h = 0.0f;
        s_histAt = 0; s_histN = 0; s_carry = 0.0f;
        s_fixedTime = 0.0; s_inFixed = false; s_fixedAlpha = 0.0f;
        s_sessionStartTime = s_sessionStartUnscaled = 0.0; s_sessionStartFrame = 0;
        ClearEffects();
    }
    /// Forget SmoothDelta's history and vsync carry: a new session or scene starts its
    /// first frames from its own measurements, not the previous scene's. (GameLoop's
    /// BeginSession calls it.)
    static void ResetSmoothing() {
        s_rawPending = -1.0f;
        for (float& h : s_hist) h = 0.0f;
        s_histAt = 0; s_histN = 0; s_carry = 0.0f;
    }
    /// Drop every hit stop and ramp and restore a plain scale path. The scale itself stays.
    /// Game::Reset calls it so one Play session's effects never leak into the next.
    static void ClearEffects();

private:
    friend class Application;
    static void AddTicks(std::int64_t& ticks, double& carry, double seconds) {
        // Whole nanoseconds, the fraction carried: the sum stays within 1 ns of exact however many frames pass.
        // One frame counts at most 1e9 s (31 years) and the count stops at 9e18 ns (285 years): a broken
        // frame time can never overflow the integer.
        if (seconds > 1.0e9) seconds = 1.0e9;
        const double t = seconds * kTicksPerSecond + carry;
        const double w = std::floor(t);
        constexpr std::int64_t kCap = 9000000000000000000LL;
        if (ticks >= kCap - (std::int64_t)w) { ticks = kCap; carry = 0.0; return; }
        ticks += (std::int64_t)w;
        carry = t - w;
    }
    static void Advance(double unscaledDt) {
        if (!(unscaledDt > 0.0) || !std::isfinite(unscaledDt)) unscaledDt = 0.0;
        // A frame longer than a float can hold is the largest float: the clocks stay finite.
        if (unscaledDt > (double)std::numeric_limits<float>::max()) unscaledDt = (double)std::numeric_limits<float>::max();
        s_unscaledDeltaTime = (float)unscaledDt;
        s_unscaledDeltaD = unscaledDt;
        // With a hit stop or ramp running the scale changes inside the frame: integrate it. The product
        // is formed in double and held in float range: a huge frame at a scale above 1 (the clamp turned
        // off, a broken timer) used to overflow to infinity and poison ElapsedTime for good.
        double scaled = s_fxActive ? StepEffects(unscaledDt) : unscaledDt * (double)TimeScale();
        if (!(scaled < (double)std::numeric_limits<float>::max())) scaled = (double)std::numeric_limits<float>::max();
        s_deltaTime = (float)scaled;
        s_deltaD = scaled;
        AddTicks(s_ticks, s_tickCarry, scaled);
        AddTicks(s_unscaledTicks, s_unscaledTickCarry, unscaledDt);
        s_time = (double)s_ticks / kTicksPerSecond;
        s_unscaledTime = (double)s_unscaledTicks / kTicksPerSecond;
        ++s_frameCount;
        // The FPS readout follows the measured time (SmoothDelta's input), when there is one.
        s_rawDeltaTime = s_rawPending >= 0.0f ? s_rawPending : (float)unscaledDt;
        s_rawPending = -1.0f;
        if (s_rawDeltaTime > 0.0f) {
            // The very first measured frame is not a frame time worth showing (it holds the
            // start-up: 1e-5 s read as 100000 FPS for the next hundred frames), so it is
            // skipped; after that a running average (1/n while warming up, then 0.9/0.1).
            if (s_fpsSamples++ > 0) {
                // A denormal frame time (1e-40 s) would make 1 / dt infinity, and the average then turned NaN
                // (inf - inf) for the rest of the run: an instant reading is held at kMaxFps.
                const float instant = std::fmin(1.0f / s_rawDeltaTime, kMaxFps);
                const float a = s_fpsSamples <= 10 ? 1.0f / (float)(s_fpsSamples - 1) : 0.1f;
                s_fps = s_fps <= 0.0f ? instant : s_fps + (instant - s_fps) * a;
            }
        }
    }
    /// Time.cpp: integrate the scale over `unscaledDt` seconds while hit stops / a ramp
    /// run, and advance them. Returns the scaled delta.
    static double StepEffects(double unscaledDt);
    static void RefreshFx();

    static inline float        s_deltaTime         = 0.0f;
    static inline float        s_unscaledDeltaTime = 0.0f;
    static inline double       s_deltaD = 0.0, s_unscaledDeltaD = 0.0;
    static inline std::int64_t s_ticks = 0, s_unscaledTicks = 0;
    static inline double       s_tickCarry = 0.0, s_unscaledTickCarry = 0.0;
    static inline double       s_time              = 0.0;
    static inline double       s_unscaledTime      = 0.0;
    static inline float        s_timeScale         = 1.0f;
    static inline float        s_fixedDelta        = 1.0f / 50.0f;
    static inline bool         s_frozen            = false;
    static inline float        s_fps               = 0.0f;
    static inline int          s_fpsSamples        = 0;       // measured frames seen by the FPS average
    static inline std::uint64_t s_frameCount       = 0;
    static inline float        s_rawDeltaTime      = 0.0f;
    static inline float        s_rawPending        = -1.0f;   // SmoothDelta's input this frame (-1 = none)
    static inline float        s_hist[4]           = {0.0f, 0.0f, 0.0f, 0.0f};
    static inline int          s_histAt = 0, s_histN = 0;
    static inline float        s_carry             = 0.0f;    // vsync snapping: measured minus stepped
    // Effects (state in Time.cpp; these are the parts the inline accessors read).
    static inline bool         s_fxActive          = false;   // a hit stop or ramp is running
    static inline float        s_hitMul            = 1.0f;    // strongest running hit stop (1 = none)
    static inline int          s_hitCount          = 0;
    static inline double       s_fixedTime         = 0.0;
    static inline bool         s_inFixed           = false;
    static inline float        s_fixedAlpha        = 0.0f;
    static inline double       s_sessionStartTime  = 0.0, s_sessionStartUnscaled = 0.0;
    static inline std::uint64_t s_sessionStartFrame = 0;
};

/// A fixed-timestep accumulator for gameplay code outside physics: feed it each
/// frame's time and run `Advance`'s count of equal steps, then draw `Alpha()` of
/// the way to the next one (to interpolate). Scene physics uses the same scheme
/// (Scene::fixedTimeStep, maxPhysicsCatchUp); this is for anything else that needs
/// a steady rate (AI ticks, rhythm timing, replays, custom simulations).
///
///   FixedTimestep fixed;                       // Time::FixedDeltaTime(), 5 steps at most
///   ...each frame:
///   fixed.Run(Time::DeltaTime(), [&](float dt) { Simulate(dt); });
///   Draw(Lerp(prev, cur, fixed.Alpha()));
///
/// Feed it Time::DeltaTime() (scaled: it stands still while paused and speeds up or
/// slows with the time scale) or Time::UnscaledDeltaTime() (UI, menus). A long frame
/// runs at most `maxSteps` steps; the time beyond that is dropped (not owed), so a
/// hitch never spirals into ever more steps.
class FixedTimestep {
public:
    /// `step` <= 0 (or not finite) reads Time::FixedDeltaTime().
    explicit FixedTimestep(float step = 0.0f, int maxStepsPerFrame = 5) {
        SetStep(step);
        SetMaxSteps(maxStepsPerFrame);
    }

    float Step() const { return (float)m_step; }
    void  SetStep(float step) {
        m_step = (double)((step > 0.0f && std::isfinite(step)) ? step : Time::FixedDeltaTime());
        Clamp();
    }
    int   MaxSteps() const { return m_maxSteps; }
    /// At least 1, at most kMaxStepsCap: a limit of INT_MAX with a long frame kept one Advance busy for
    /// billions of steps.
    void  SetMaxSteps(int n) { m_maxSteps = n < 1 ? 1 : (n > kMaxStepsCap ? kMaxStepsCap : n); Clamp(); }
    static constexpr int kMaxStepsCap = 100000;

    /// What a frame that owes more than MaxSteps() steps does with the surplus.
    ///   Drop   the surplus is thrown away (DroppedTime()): the game falls behind real time for good, and
    ///          a hitch can never cost more than MaxSteps() steps. The default, and what it always did.
    ///   Spread the surplus is kept (up to SetMaxOwed() seconds, one second unless set) and paid back at
    ///          MaxSteps() steps a frame in the frames after: the game catches up, a little fast, and
    ///          only what is owed beyond that is dropped. Still never more than MaxSteps() in one frame.
    enum class Overflow { Drop, Spread };
    Overflow GetOverflow() const { return m_overflow; }
    void SetOverflow(Overflow o) { m_overflow = o; Clamp(); }
    /// With Spread: the most time that may be owed, seconds (0 to a day; NaN and negatives mean 0).
    double MaxOwed() const { return m_maxOwed; }
    void SetMaxOwed(double seconds) { m_maxOwed = (seconds > 0.0 && std::isfinite(seconds)) ? (seconds < 86400.0 ? seconds : 86400.0) : 0.0; Clamp(); }
    /// Time owed beyond what this frame's steps could run (Spread), seconds.
    double Owed() const { const double cap = m_step * (double)m_maxSteps; return m_acc > cap ? m_acc - cap : 0.0; }

    /// Add `dt` seconds (negative, NaN and infinite times add nothing) and return how
    /// many fixed steps are now due, at most MaxSteps(); they are taken off the
    /// accumulator. A frame that would owe more has the surplus dropped (Dropped()),
    /// or, with Overflow::Spread, kept up to MaxOwed().
    int Advance(float dt) { return AdvanceD((double)dt); }
    /// Advance with an exact double (Time::DeltaTimeD()): no float rounding in a long run.
    int AdvanceD(double dt) {
        if (dt > 0.0 && std::isfinite(dt)) m_acc += dt;
        const double cap = Cap();
        if (m_acc > cap + kEps * m_step) { m_dropped += m_acc - cap; m_acc = cap; }
        int n = 0;
        while (m_acc >= m_step * (1.0 - kEps) && n < m_maxSteps) { m_acc -= m_step; ++n; }
        if (m_acc < 0.0) m_acc = 0.0;
        m_steps = n;
        m_total += (std::uint64_t)n;
        return n;
    }

    /// Advance(dt), then call fn(stepSeconds) once per due step. Returns the count.
    template <class Fn> int Run(float dt, Fn&& fn) {
        const int n = Advance(dt);
        const float s = Step();
        for (int i = 0; i < n; ++i) fn(s);
        return n;
    }

    /// How far into the next step the leftover time is: 0 (just stepped) up to
    /// just under 1. Blend the previous and current state with it.
    float Alpha() const {
        const double a = m_acc / m_step;   // can pass 1 after SetStep shrank the step, until the next Advance
        return a <= 0.0 ? 0.0f : (a >= 1.0 ? std::nextafter(1.0f, 0.0f) : (float)a);
    }
    /// Time carried toward the next step, seconds.
    float Accumulated() const { return (float)m_acc; }
    /// Steps the last Advance returned, and every step returned so far.
    int   LastSteps() const { return m_steps; }
    std::uint64_t TotalSteps() const { return m_total; }
    /// Seconds dropped for exceeding MaxSteps() since the last Reset.
    double DroppedTime() const { return m_dropped; }
    /// Forget the carried time and the counters.
    void Reset() { m_acc = 0.0; m_steps = 0; m_total = 0; m_dropped = 0.0; }

private:
    double Cap() const { return m_step * (double)m_maxSteps + (m_overflow == Overflow::Spread ? m_maxOwed : 0.0); }
    void Clamp() { const double cap = Cap(); if (m_acc > cap) m_acc = cap; }
    static constexpr double kEps = 1e-6;   // a step short by float rounding still counts
    double m_step = 0.02;
    double m_acc = 0.0;
    double m_dropped = 0.0;
    std::uint64_t m_total = 0;
    int m_maxSteps = 5;
    int m_steps = 0;
    Overflow m_overflow = Overflow::Drop;
    double m_maxOwed = 1.0;
};

} // namespace okay
