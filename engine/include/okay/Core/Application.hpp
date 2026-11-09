#pragma once
#include "okay/Core/GameLoop.hpp"
#include "okay/Render/Renderer.hpp"
#include <atomic>
#include <memory>
#include <string>

namespace okay {

class Scene;

/// The engine entry point: owns the renderer and runs the game loop, pumping
/// Time, Input, Update, and Render each frame. Comparable to player
/// loop, distilled to its essentials.
class Application {
public:
    struct Config {
        std::string title  = "OkaySpace";
        int   width        = 80;     // renderer width (chars for the console backend)
        int   height       = 30;     // renderer height
        float targetFps    = 30.0f;  // frame pacing; <= 0 means run uncapped
        int   maxFrames    = 0;      // 0 = run until quit; >0 = stop after N frames
        bool  smoothDt     = true;   // step by an evened-out frame time (Time::SmoothDelta)
        bool  clearScreen  = true;   // console backend clears the terminal each frame
        char  quitKey      = 'q';    // pressing it ends the loop; 0 = no quit key
        float maxDelta     = 0.25f;  // a frame longer than this (a breakpoint, a stall) steps one target frame instead; <= 0 = no clamp
        float fixedStep    = 0.0f;   // seconds per fixed step for loop().OnFixedUpdate; 0 = Time::FixedDeltaTime()
        int   maxFixedSteps = 5;     // fixed steps one frame may run (the rest is dropped)
        bool  catchExceptions = false; // true: an exception from the scene is logged, the loop ends and
                                       // ExitCode() is 1 (Run returns); false: it is rethrown after cleanup
        /// How targetFps is held. Sleep (the default, as it always was): sleep for what is left of each
        /// frame, so every frame that wakes up late runs long and the rate lands a little under the
        /// target (about 3% at 30 fps when a sleep overshoots by a millisecond). Limiter: Core/FramePacing's
        /// FrameLimiter, which schedules each frame from when the last one was due, sleeps most of the wait
        /// and yields through the rest, so an overshoot is paid back by the next frame and the average rate
        /// is the target. It costs a short busy wait (about 1.5 ms) at the end of each frame. Ignored when
        /// targetFps <= 0.
        enum class Pacing { Sleep, Limiter };
        Pacing pacing = Pacing::Sleep;
    };

    explicit Application(Config config);
    ~Application();

    /// Provide a custom renderer backend (defaults to the console renderer).
    void SetRenderer(std::unique_ptr<IRenderer> renderer);

    /// Run the main loop against the given scene until it quits.
    ///
    /// The terminal (raw input) is given back however Run ends, including when a script
    /// or component throws. With Config::catchExceptions false (the default) the exception
    /// then continues up to the caller; with it true Run logs it, records LastError() and
    /// ExitCode() == 1 and returns normally.
    void Run(Scene& scene);

    /// Seconds one paced frame lasts for a `targetFps`: 1 / targetFps, 0 (uncapped) for 0, a negative, NaN
    /// or infinite rate, and never more than 1 (a rate under 1 fps paces at 1: 1e-30 once asked the sleep for
    /// 1e30 seconds, which overflows the sleep's clock type). Run paces with this.
    static float TargetFrameSeconds(float targetFps) {
        if (!(targetFps > 0.0f)) return 0.0f;
        const float t = 1.0f / targetFps;
        return t > 1.0f ? 1.0f : t;
    }

    /// Whether the loop should end after this frame: the game asked to quit
    /// (Game::RequestQuit), or the quit key (`quitKey`, default 'q'; 0 = none) was pressed.
    static bool ShouldQuit(const Config& config, bool keyPressed, bool quitRequested) {
        return quitRequested || (config.quitKey != 0 && keyPressed);
    }

    /// Request a graceful shutdown of the loop (safe from another thread). It ends the Run that is
    /// going now, after the current frame; one called before Run starts has no effect (Run begins by
    /// marking itself running), so a thread that stops the app waits until a frame has run.
    void Quit() { m_running = false; }

    /// The per-frame driver Run uses: set OnFixedUpdate / OnPause / OnResume on it before
    /// Run, and read its hitch counters afterwards. OnUpdate is taken by Run (it updates
    /// the scene); use a Component for per-frame work, as before.
    GameLoop& loop() { return m_loop; }
    /// What ended the last Run on an error: the exception's message ("" when it ended normally).
    const std::string& LastError() const { return m_lastError; }
    /// The process exit code: what Game::RequestQuit(reason, code) asked for, or 1 when
    /// Run ended on an exception (with catchExceptions).
    int ExitCode() const { return m_exitCode; }

    IRenderer& renderer() { return *m_renderer; }
    const Config& config() const { return m_config; }

private:
    Config m_config;
    std::unique_ptr<IRenderer> m_renderer;
    std::atomic<bool> m_running{false};
    GameLoop m_loop;
    std::string m_lastError;
    int m_exitCode = 0;
};

} // namespace okay
