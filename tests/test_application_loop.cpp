// 1.1 Application::Run on the shared GameLoop: exception safety (the terminal is handed back),
// exit codes, the new Config knobs, and the quit key while a text field has the keyboard.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/GameLoop.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#  include <fcntl.h>
#  include <termios.h>
#  include <unistd.h>
#  define OKAY_TEST_PTY 1
#endif

using namespace okay;

namespace {

class NullRenderer : public IRenderer {
public:
    void BeginFrame(const Color&) override {}
    void EndFrame() override {}
    void SetCamera(const Vec3&, float, float) override {}
    void DrawQuad(const Vec3&, const Vec2&, const Color&, char) override {}
    Viewport GetViewport() const override { return {10, 10}; }
};

// Calls `fn(frameNumber)` every Update.
class Hook : public Component {
public:
    int frame = 0;
    std::function<void(int)> fn;
    void Update(float) override { ++frame; if (fn) fn(frame); }
};

Application::Config Quiet() {
    Application::Config cfg;
    cfg.targetFps = 0.0f; cfg.maxFrames = 30; cfg.clearScreen = false;
    return cfg;
}

// Runs a scene whose only object runs `fn` each frame. Returns the frames it ran.
int Run(Application& app, std::function<void(int)> fn) {
    app.SetRenderer(std::make_unique<NullRenderer>());
    Scene scene("t");
    auto* h = scene.CreateGameObject("hook")->AddComponent<Hook>();
    h->fn = std::move(fn);
    app.Run(scene);
    return h->frame;
}

}  // namespace

int main() {
    RUN_SUITE("application_loop");
    Log::console = false;

    // ---- defaults are unchanged ------------------------------------------------------------------------------------
    {
        Application::Config cfg;
        CHECK(cfg.targetFps == 30.0f && cfg.quitKey == 'q' && cfg.maxFrames == 0 && cfg.smoothDt);
        CHECK(cfg.maxDelta == 0.25f && cfg.fixedStep == 0.0f && cfg.maxFixedSteps == 5 && !cfg.catchExceptions);
    }

    // ---- a normal run drives the loop ----------------------------------------------------------------------------------
    {
        Game::Reset(); Time::ResetForTesting();
        Application app(Quiet());
        const int frames = Run(app, [](int) {});
        CHECK(frames == 30);
        CHECK(app.loop().FrameCount() == 30);
        CHECK(Time::FrameCount() == 30);
        CHECK(app.LastError().empty() && app.ExitCode() == 0);
        CHECK(!app.loop().OnUpdate);                          // Run does not leave a callback pointing at its scene
    }

    // ---- exit code and reason from Game::RequestQuit --------------------------------------------------------------------------
    {
        Application app(Quiet());
        const int frames = Run(app, [](int f) { if (f == 4) Game::RequestQuit("level cleared", 5); });
        CHECK(frames >= 4 && frames <= 5);
        CHECK(app.ExitCode() == 5);
        CHECK(Game::QuitReason() == "level cleared");
        // a guard that vetoes keeps the game running; the loop honours it
        Application app2(Quiet());
        int asked = 0;
        const int frames2 = Run(app2, [&](int f) {
            if (f == 1) Game::SetQuitGuard([&] { ++asked; return false; });
            if (f == 3) Game::RequestQuit("window closed");
        });
        CHECK(frames2 == 30 && asked == 1 && app2.ExitCode() == 0);
        Game::Reset();
    }

    // ---- the quit key is not a quit while a text field has the keyboard ---------------------------------------------------------------
    {
        Input::SetUICaptured(true);
        Application app(Quiet());
        const int frames = Run(app, [](int f) { if (f == 3) Input::FeedKeys({'q'}); });
        CHECK(frames == 30);                                  // 'q' was typed text
        Input::SetUICaptured(false);
        Application app2(Quiet());
        const int frames2 = Run(app2, [](int f) { if (f == 3) Input::FeedKeys({'q'}); });
        CHECK(frames2 == 3);                                  // with nothing capturing the keyboard it still quits
    }

    // ---- an exception from a script: rethrown by default, after the terminal session is closed ------------------------------------------
    {
        Application app(Quiet());
        bool threw = false;
        try {
            Run(app, [](int f) {
                if (f == 3) { Input::FeedKeys({'z'}); throw std::runtime_error("boom"); }
            });
        } catch (const std::runtime_error& e) { threw = std::string(e.what()) == "boom"; }
        CHECK(threw);
        CHECK(app.LastError() == "boom");
        CHECK(!Input::GetKeyDown('z'));                       // EndSession ran: the key state was cleared (it was not before)
        // the application can run again afterwards
        Application again(Quiet());
        CHECK(Run(again, [](int) {}) == 30);
        // a non-std exception is rethrown as well
        bool other = false;
        try { Run(again, [](int f) { if (f == 2) throw 42; }); } catch (int v) { other = v == 42; }
        CHECK(other && again.LastError() == "unknown exception");
    }

    // ---- ...or contained: logged, Run returns, exit code 1 ---------------------------------------------------------------------------------
    {
        Application::Config cfg = Quiet(); cfg.catchExceptions = true;
        Application app(cfg);
        int frames = 0;
        bool threw = false;
        try { frames = Run(app, [](int f) { if (f == 5) throw std::runtime_error("script failed"); }); }
        catch (...) { threw = true; }
        CHECK(!threw);
        CHECK(frames == 5);
        CHECK(app.LastError() == "script failed" && app.ExitCode() == 1);
        CHECK(Game::QuitRequested() && Game::QuitReason() == "exception: script failed");
        // the next Run starts clean
        Application next(Quiet());
        CHECK(Run(next, [](int) {}) == 30 && next.ExitCode() == 0 && next.LastError().empty());
        Game::Reset();
    }

#ifdef OKAY_TEST_PTY
    // ---- the real thing: stdin is a terminal; after a throw it is back to cooked, blocking mode ----------------------------------------------
    {
        int master = posix_openpt(O_RDWR | O_NOCTTY);
        CHECK(master >= 0);
        if (master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0) {
            int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
            CHECK(slave >= 0);
            const int savedStdin = dup(STDIN_FILENO);
            dup2(slave, STDIN_FILENO);
            termios before{}; tcgetattr(STDIN_FILENO, &before);
            CHECK((before.c_lflag & ICANON) != 0 && (before.c_lflag & ECHO) != 0);
            CHECK((fcntl(STDIN_FILENO, F_GETFL, 0) & O_NONBLOCK) == 0);

            bool sawRaw = false, threw = false;
            Application app(Quiet());
            try {
                Run(app, [&](int f) {
                    if (f == 2) {
                        termios t{}; tcgetattr(STDIN_FILENO, &t);
                        sawRaw = (t.c_lflag & ICANON) == 0 && (fcntl(STDIN_FILENO, F_GETFL, 0) & O_NONBLOCK) != 0;
                        throw std::runtime_error("script crashed");
                    }
                });
            } catch (const std::runtime_error&) { threw = true; }
            CHECK(threw);
            CHECK(sawRaw);                                    // the session really was raw during the run
            termios after{}; tcgetattr(STDIN_FILENO, &after);
            CHECK((after.c_lflag & ICANON) != 0 && (after.c_lflag & ECHO) != 0);   // used to stay raw: the shell misbehaved
            CHECK((fcntl(STDIN_FILENO, F_GETFL, 0) & O_NONBLOCK) == 0);

            // and a clean run restores it too
            Application ok(Quiet());
            Run(ok, [](int) {});
            tcgetattr(STDIN_FILENO, &after);
            CHECK((after.c_lflag & ICANON) != 0 && (fcntl(STDIN_FILENO, F_GETFL, 0) & O_NONBLOCK) == 0);

            dup2(savedStdin, STDIN_FILENO);
            close(savedStdin);
            close(slave);
        }
        if (master >= 0) close(master);
    }
#endif

    // ---- Config::maxDelta: a stall is cut down to one target frame --------------------------------------------------------------------------------------
    {
        Game::Reset(); Time::ResetForTesting();
        Application::Config cfg = Quiet();
        cfg.maxFrames = 8; cfg.smoothDt = false; cfg.maxDelta = 0.02f;
        Application app(cfg);
        std::vector<float> seen;
        Run(app, [&](int) { seen.push_back(Time::UnscaledDeltaTime()); std::this_thread::sleep_for(std::chrono::milliseconds(40)); });
        CHECK(seen.size() == 8);
        bool cut = true;
        for (std::size_t i = 1; i < seen.size(); ++i) if (seen[i] > 0.0161f) cut = false;   // every 40 ms frame stepped 0.016 (uncapped: the 0.016 s fallback)
        CHECK(cut);
        CHECK(app.loop().ClampedFrames() >= 6);
        CHECK(app.loop().HitchCount() == 0);                  // 40 ms is under the 0.1 s hitch threshold: clamped, not a hitch
    }
    // with the clamp off the real time comes through
    {
        Time::ResetForTesting();
        Application::Config cfg = Quiet();
        cfg.maxFrames = 5; cfg.smoothDt = false; cfg.maxDelta = 0.0f;
        Application app(cfg);
        float biggest = 0.0f;
        Run(app, [&](int) { biggest = std::max(biggest, Time::UnscaledDeltaTime()); std::this_thread::sleep_for(std::chrono::milliseconds(30)); });
        CHECK(biggest >= 0.029f);
        CHECK(app.loop().ClampedFrames() == 0);
    }

    // ---- Config::fixedStep: loop().OnFixedUpdate runs at that rate --------------------------------------------------------------------------------------------
    {
        Time::ResetForTesting();
        Application::Config cfg = Quiet();
        cfg.maxFrames = 20; cfg.smoothDt = false; cfg.fixedStep = 0.01f;
        Application app(cfg);
        int steps = 0; bool inStep = true; float stepSeen = 0.0f;
        app.loop().OnFixedUpdate = [&](float dt) { ++steps; stepSeen = dt; if (!Time::InFixedStep()) inStep = false; };
        Run(app, [](int) { std::this_thread::sleep_for(std::chrono::milliseconds(12)); });
        CHECK(steps >= 15);                                   // 19 frames of >= 12 ms at 10 ms a step
        CHECK(inStep && std::fabs(stepSeen - 0.01f) < 1e-9f);
        CHECK((int)app.loop().TotalFixedSteps() == steps);
    }

    Game::Reset();
    TEST_MAIN_RESULT();
}
