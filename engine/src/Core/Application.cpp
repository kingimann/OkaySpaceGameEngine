#include "okay/Core/Application.hpp"
#include "okay/Core/FramePacing.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Render/ConsoleRenderer.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>

namespace okay {

using clock = std::chrono::steady_clock;

Application::Application(Config config) : m_config(std::move(config)) {
    m_renderer = std::make_unique<ConsoleRenderer>(
        m_config.width, m_config.height, m_config.clearScreen);
    OKAY_INFO("Application '", m_config.title, "' created (",
              m_config.width, "x", m_config.height, ")");
}

Application::~Application() = default;

void Application::SetRenderer(std::unique_ptr<IRenderer> renderer) {
    if (renderer) m_renderer = std::move(renderer);
}

void Application::Run(Scene& scene) {
    m_running = true;
    m_lastError.clear();
    m_exitCode = 0;
    Game::Reset();   // a quit/pause left over from an earlier run must not leak in
    // This run is a session: a script's Time.sessionTime and Time.sessionFrameCount start from 0 in it (a
    // second Run in one process, a test, used to read on from the first). No process clock moves, and the
    // time scale is left alone (a host may have set one before Run).
    Time::MarkSessionStart();

    // The terminal goes raw for the run and is handed back however we leave, an
    // exception from a script included (it used to stay raw, and the shell misbehaved).
    // A local class has the access of the member function around it, so it may call the
    // private session hooks.
    struct InputSession {
        InputSession()  { Input::BeginSession(); }
        ~InputSession() { Input::EndSession(); }
    };
    InputSession session;

    const float targetFrameTime = TargetFrameSeconds(m_config.targetFps);   // at most one second (see there)

    // The shared driver: clamp a stall, step Time (evened out when smoothDt), run the
    // fixed steps and OnUpdate. Config defaults reproduce the loop this used to be. The Config
    // decides only the options it has a field for; anything else the host set on loop() before
    // Run (capture mode, a frame budget, the stats window, hitch logging...) stays as it set it.
    GameLoop::Options opt = m_loop.options();
    opt.maxDelta = m_config.maxDelta;
    // A frame beyond the clamp steps one target frame (0.016 s uncapped), not the limit.
    opt.hitchReplacement = targetFrameTime > 0.0f ? targetFrameTime : 0.016f;
    opt.smoothDelta = m_config.smoothDt;
    opt.fixedStep = m_config.fixedStep;
    opt.maxFixedSteps = m_config.maxFixedSteps;
    m_loop.SetOptions(opt);
    m_loop.Reset();
    m_loop.OnUpdate = [&scene](float dt) { scene.Update(dt); };
    struct DropHook {   // the callback points at `scene`: never leave it behind
        GameLoop& loop;
        ~DropHook() { loop.OnUpdate = nullptr; }
    } dropHook{m_loop};

    FrameLimiter limiter(m_config.pacing == Config::Pacing::Limiter && targetFrameTime > 0.0f ? std::max(1.0, (double)m_config.targetFps) : 0.0);

    int frame = 0;
    try {
        OKAY_INFO("Entering main loop for scene '", scene.Name(), "'");
        scene.Start();

        auto previous = clock::now();

        while (m_running) {
            auto now = clock::now();
            const float dt = std::chrono::duration<float>(now - previous).count();
            previous = now;

            Input::Poll();
            m_loop.BeginFrame(dt);   // Time, fixed steps, scene.Update(Time::DeltaTime())
            scene.Render(*m_renderer);

            // The quit key is typed text while a UI text field has the keyboard: leave it alone.
            const bool keyQuit = m_config.quitKey != 0 && !Input::UICaptured() && Input::GetKeyDown(m_config.quitKey);
            if (ShouldQuit(m_config, keyQuit, m_loop.QuitRequested())) m_running = false;

            ++frame;
            if (m_config.maxFrames > 0 && frame >= m_config.maxFrames) m_running = false;

            if (limiter.Enabled()) {
                limiter.EndFrame();   // sleeps to the frame's deadline (the first call only starts its clock)
            } else if (targetFrameTime > 0.0f) {
                auto frameEnd = clock::now();
                float elapsed = std::chrono::duration<float>(frameEnd - now).count();
                float remaining = targetFrameTime - elapsed;
                if (remaining > 0.0f)
                    std::this_thread::sleep_for(std::chrono::duration<float>(remaining));
            }
        }
    } catch (const std::exception& e) {
        m_running = false;
        m_lastError = e.what();
        OKAY_ERROR("Main loop ended by an exception after ", frame, " frames: ", e.what());
        if (!m_config.catchExceptions) throw;   // `session` gives the terminal back first
        Game::ForceQuit(std::string("exception: ") + e.what(), 1);
    } catch (...) {
        m_running = false;
        m_lastError = "unknown exception";
        OKAY_ERROR("Main loop ended by an unknown exception after ", frame, " frames");
        if (!m_config.catchExceptions) throw;
        Game::ForceQuit("exception", 1);
    }

    m_running = false;
    m_exitCode = Game::ExitCode();
    OKAY_INFO("Main loop exited after ", frame, " frames");
}

} // namespace okay
