// Tests for okay::testing::PlayTest: stepping, determinism, time scale, loading,
// input injection, log capture with expected errors, and the event helpers.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Game.hpp"
#include "okay/Testing/PlayTest.hpp"
#include "okay/Testing/Regression.hpp"
#include "okay/Testing/TestRegistry.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace okay;
using namespace okay::testing;

namespace {

// A component that remembers what Input said on every frame it ran.
struct InputProbe : Component {
    struct Snap {
        bool d = false, dDown = false, dUp = false, f = false, fDown = false, fUp = false, space = false;
        Vec2 wasd, stick, look, mouse;
        float trigL = 0, trigR = 0, wheel = 0;
        bool padA = false, padADown = false, connected = false;
        bool lmb = false, lmbDown = false, lmbUp = false, rmb = false;
        std::string typed;
        int touches = 0;
    };
    std::vector<Snap> frames;
    void Update(float) override {
        Snap s;
        s.d = Input::GetKey('d'); s.dDown = Input::GetKeyDown('d'); s.dUp = Input::GetKeyUp('d');
        s.f = Input::GetKey('f'); s.fDown = Input::GetKeyDown('f'); s.fUp = Input::GetKeyUp('f');
        s.space = Input::GetKey(' ');
        s.wasd = Input::AxisWASD(); s.stick = Input::GamepadAxis(); s.look = Input::GamepadRightAxis();
        s.mouse = Input::MousePosition();
        s.trigL = Input::GamepadTrigger(0); s.trigR = Input::GamepadTrigger(1); s.wheel = Input::MouseWheel();
        s.padA = Input::GetGamepadButton(Input::PadA); s.padADown = Input::GetGamepadButtonDown(Input::PadA);
        s.connected = Input::GamepadConnected();
        s.lmb = Input::GetMouseButton(0); s.lmbDown = Input::GetMouseButtonDown(0); s.lmbUp = Input::GetMouseButtonUp(0);
        s.rmb = Input::GetMouseButton(1);
        s.typed = Input::TypedText();
        s.touches = Input::TouchCount();
        frames.push_back(s);
    }
};

// Logs a line of the chosen level on its first Update.
struct Logger : Component {
    Log::Level level = Log::Level::Error;
    std::string text = "boom";
    int times = 1;
    void Update(float) override { for (int i = 0; i < times; ++i) { if (level == Log::Level::Error) Log::Error(text); else Log::Warning(text); } times = 0; }
};

// Asks the game to quit on a chosen frame.
struct Quitter : Component {
    int frame = 0, at = 3;
    void Update(float) override { if (++frame == at) Game::RequestQuit(); }
};

// Draws from the shared random generator every frame.
struct Dice : Component {
    std::vector<float> rolls;
    void Update(float) override { rolls.push_back(Random::Shared().Value()); }
};

struct Scored { int points; };

GameObject* AddMover(Scene& s, const char* name = "Mover", float speed = 2.0f) {
    GameObject* go = s.CreateGameObject(name);
    auto* sc = go->AddComponent<ScriptComponent>();
    CHECK(sc->LoadSource("function update(dt) { move(" + std::to_string(speed) + " * dt, 0); }"));
    return go;
}

void AddFallingBall(Scene& s) {
    GameObject* ground = s.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -1, 0};
    ground->AddComponent<BoxCollider2D>()->size = {20, 1};
    ground->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Static;
    GameObject* ball = s.CreateGameObject("Ball");
    ball->transform->localPosition = {0, 4, 0};
    ball->AddComponent<CircleCollider2D>()->radius = 0.5f;
    ball->AddComponent<Rigidbody2D>();
}

std::string Temp(const std::string& name) {
    const std::string dir = std::string(OKAY_TEST_OUTPUT_DIR) + "/playtest";
    std::filesystem::create_directories(dir);
    return dir + "/" + name;
}

} // namespace

int main() {
    RUN_SUITE("playtest");

    // --- Stepping basics: fixed frames, a clean clock ---
    {
        PlayTest t;
        CHECK(t.Frame() == 0 && !t.Started());
        CHECK_NEAR(t.FrameTime(), 1.0f / 60.0f, 1e-9);
        CHECK(t.RunFrames(30) == 30);
        CHECK(t.Started() && t.Frame() == 30);
        CHECK_NEAR(t.SimulatedSeconds(), 0.5, 1e-4);
        CHECK_NEAR(Time::ElapsedTimeD(), 0.5, 1e-4);          // global Time advanced by the fixed frames
        CHECK(Time::FrameCount() == 30);
        CHECK(t.RunSeconds(0.1f) == 6);
        CHECK(t.RunSeconds(0.0f) == 0 && t.RunSeconds(-1.0f) == 0);
        t.Step();
        CHECK(t.Frame() == 37);
    }
    CHECK(Time::FrameCount() == 0 && Time::ElapsedTimeD() == 0.0);   // the destructor put the clock back

    // --- A scripted object moves; the position after N frames is exact enough to assert ---
    {
        PlayTest t;
        AddMover(t.scene());
        t.RunFrames(60);
        CHECK_NEAR(t.PositionOf("Mover").x, 2.0f, 1e-3);          // 2 units/s for one second
        CHECK_NEAR(t.PositionOf("Mover").y, 0.0f, 1e-6);
        CHECK(t.FindComponent<ScriptComponent>("Mover") != nullptr);
        CHECK(t.FindComponent<ScriptComponent>() == t.FindComponent<ScriptComponent>("Mover"));
        CHECK(t.FindComponent<Camera>("Mover") == nullptr);
    }

    // --- RunUntil: true when it comes true, false after the time limit, no stepping when already true ---
    {
        PlayTest t;
        AddMover(t.scene());
        GameObject* mover = t.FindObject("Mover");
        CHECK(t.RunUntil([&] { return mover->transform->Position().x >= 1.0f; }, 5.0f));
        CHECK(t.Frame() >= 29 && t.Frame() <= 31);                  // 1 unit at 2/s is half a second
        const int f = t.Frame();
        CHECK(t.RunUntil([] { return true; }, 5.0f));
        CHECK(t.Frame() == f);                                       // already true: no frame
        CHECK(!t.RunUntil([] { return false; }, 0.5f));
        CHECK(t.Frame() == f + 30);                                  // ran the whole half second
        CHECK(!t.RunUntil([] { return false; }, 0.0f));
        CHECK(t.Frame() == f + 30);                                  // no time allowed: no frame
        CHECK(!t.RunUntil([] { return false; }, std::nanf("")));
        CHECK(t.Frame() == f + 30);                                  // NaN is no time either
        int calls = 0;
        CHECK(t.RunUntil([&] { return ++calls >= 4; }, 1.0f));
        CHECK(t.Frame() == f + 30 + 3);                              // checked before and after each frame
    }

    // --- Time scale and pause ---
    {
        PlayTest t;
        AddMover(t.scene());
        t.SetTimeScale(0.5f);
        CHECK_NEAR(t.TimeScale(), 0.5f, 1e-6);
        t.RunFrames(60);
        CHECK_NEAR(t.PositionOf("Mover").x, 1.0f, 1e-3);            // half speed
        CHECK_NEAR(t.SimulatedSeconds(), 1.0, 1e-4);                // frames stay the same length
        t.SetTimeScale(0.0f);
        t.RunFrames(30);
        CHECK_NEAR(t.PositionOf("Mover").x, 1.0f, 1e-3);            // frozen
        t.SetTimeScale(2.0f);
        t.RunFrames(30);
        CHECK_NEAR(t.PositionOf("Mover").x, 3.0f, 1e-3);            // double speed
        t.SetPaused(true);
        t.RunFrames(30);
        CHECK_NEAR(t.PositionOf("Mover").x, 3.0f, 1e-3);
        CHECK(t.TimeScale() == 0.0f);                                // paused reads as zero
        t.SetPaused(false);
        t.RunFrames(30);
        CHECK_NEAR(t.PositionOf("Mover").x, 5.0f, 1e-3);

    }
    {
        PlayTestOptions slow; slow.timeScale = 0.25f;
        PlayTest u(slow);
        AddMover(u.scene());
        u.RunFrames(60);
        CHECK_NEAR(u.PositionOf("Mover").x, 0.5f, 1e-3);
    }

    // --- A rigidbody falls and comes to rest on the ground ---
    {
        PlayTest t;
        AddFallingBall(t.scene());
        t.RunFrames(10);
        CHECK(t.PositionOf("Ball").y < 4.0f);                       // already falling
        CHECK(t.RunUntil([&] { return t.FindComponent<Rigidbody2D>("Ball")->sleeping ||
                                      std::fabs(t.FindComponent<Rigidbody2D>("Ball")->velocity.y) < 0.01f; }, 5.0f));
        t.RunFrames(30);
        // ground top is at y = -0.5 and the ball's radius is 0.5, so it rests at about y = 0
        CHECK_NEAR(t.PositionOf("Ball").y, 0.0f, 0.1f);
        CHECK(t.PositionOf("Ball").y > -0.2f);
    }

    // --- Determinism: two runs of the same scene give the same fingerprint, frame for frame ---
    {
        auto run = [](int frames) {
            PlayTest t;
            AddFallingBall(t.scene());
            AddMover(t.scene());
            t.RunFrames(frames);
            return DigestScene(t.scene());
        };
        SceneDigest a = run(75), b = run(75), c = run(76);
        CHECK(a.hash == b.hash);
        CHECK(a.lines == b.lines);
        CHECK(a.hash != c.hash);                                     // one more frame is a different world
        CHECK(a.HashHex().size() == 16);
    }

    // --- Loading: text, file, errors, warnings ---
    {
        Scene source("Built");
        AddMover(source, "Mover", 3.0f);
        source.CreateGameObject("Marker")->transform->localPosition = {1, 2, 3};
        const std::string text = SceneSerializer::Serialize(source);

        {
        PlayTest t;
        CHECK(t.LoadSceneText(text));
        CHECK(t.LastError().empty());
        CHECK(t.FindObject("Marker") != nullptr);
        t.RunFrames(60);
        CHECK_NEAR(t.PositionOf("Mover").x, 3.0f, 1e-3);            // the saved script runs again
        CHECK_NEAR(t.PositionOf("Marker").z, 3.0f, 1e-6);

        // a bad document is refused and the scene stays as it was
        CHECK(!t.LoadSceneText("this is not a scene"));
        CHECK(!t.LastError().empty());
        CHECK(t.FindObject("Marker") != nullptr);
        }

        const std::string path = Temp("level.okayscene");
        CHECK(SceneSerializer::SaveToFile(source, path));
        PlayTest f;
        CHECK(f.LoadSceneFile(path));
        CHECK(f.FindObject("Mover") != nullptr);
        f.RunFrames(30);
        CHECK_NEAR(f.PositionOf("Mover").x, 1.5f, 1e-3);
        CHECK(!f.LoadSceneFile(Temp("does_not_exist.okayscene")));
        CHECK(f.LastError().find("cannot open") != std::string::npos);

        // reloading restarts: the new scene's objects get their Start
        f.LoadSceneFile(path);
        CHECK(!f.Started());
        f.RunFrames(60);
        CHECK_NEAR(f.PositionOf("Mover").x, 3.0f, 1e-3);

        // adopt / new scene
        auto fresh = std::make_unique<Scene>("Adopted");
        fresh->CreateGameObject("Only");
        f.AdoptScene(std::move(fresh));
        CHECK(f.FindObject("Only") != nullptr && f.FindObject("Mover") == nullptr);
        CHECK(f.scene().Name() == "Adopted");
        f.NewScene("Blank");
        CHECK(f.scene().Name() == "Blank" && f.scene().Objects().empty());
        f.AdoptScene(nullptr);                                       // ignored
        CHECK(f.scene().Name() == "Blank");
    }

    // --- Options: frame time, physics mode ---
    {
        PlayTestOptions o; o.dt = 1.0f / 30.0f;
        PlayTest t(o);
        AddMover(t.scene());
        t.RunFrames(30);
        CHECK_NEAR(t.SimulatedSeconds(), 1.0, 1e-4);
        CHECK_NEAR(t.PositionOf("Mover").x, 2.0f, 1e-3);
        CHECK(t.scene().fixedTimeStep == 0.0f);
    }
    {
        PlayTestOptions bad; bad.dt = -1.0f;
        PlayTest b(bad);
        CHECK_NEAR(b.FrameTime(), 1.0f / 60.0f, 1e-9);              // nonsense falls back to 60 fps
    }
    {
        PlayTestOptions fx; fx.fixedStepPhysics = true;
        PlayTest p(fx);
        CHECK(p.scene().fixedTimeStep > 0.0f && p.scene().interpolatePhysics);
        p.NewScene();
        CHECK(p.scene().fixedTimeStep > 0.0f);                       // kept across scene swaps
    }

    // --- Finding objects ---
    {
        PlayTest t;
        GameObject* arm = t.scene().CreateGameObject("Arm");
        GameObject* hand = t.scene().CreateGameObject("Hand");
        hand->transform->SetParent(arm->transform, false);
        hand->tag = "Grip";
        t.scene().CreateGameObject("Hand");                          // a second, unparented Hand
        CHECK(t.FindObject("Arm") == arm);
        CHECK(t.FindObject("Arm/Hand") == hand);
        CHECK(t.FindObject("Hand") != nullptr);
        CHECK(t.FindObject("Nope/Hand") == nullptr && t.FindObject("Arm/Nope") == nullptr);
        CHECK(t.FindObjects("Hand").size() == 2 && t.FindObjects("Nope").empty());
        CHECK(t.FindObjectWithTag("Grip") == hand);
        CHECK(t.Require("Arm") == arm);
        bool threw = false;
        try { t.Require("Missing"); } catch (const std::runtime_error& e) { threw = std::string(e.what()).find("Missing") != std::string::npos; }
        CHECK(threw);                                                // outside a run it throws
        // inside a run it fails the test and aborts it
        TestContext ctx;
        bool aborted = false;
        {
            TestContext::Scope scope(ctx);
            try { t.PositionOf("Missing"); } catch (const AbortTest&) { aborted = true; }
        }
        CHECK(aborted && ctx.Failed());
        CHECK(ctx.Failures()[0].message.find("Missing") != std::string::npos);
    }

    // --- Input injection: keys ---
    {
        PlayTest t;
        auto* probe = t.scene().CreateGameObject("P")->AddComponent<InputProbe>();
        t.KeyDown('d');
        t.Step();                                    // frame 0: pressed
        t.Step();                                    // frame 1: held
        t.KeyUp('d');
        t.Step();                                    // frame 2: released
        t.Step();                                    // frame 3: idle
        const auto& f = probe->frames;
        CHECK(f.size() == 4);
        if (f.size() == 4) {
            CHECK(f[0].d && f[0].dDown && !f[0].dUp);
            CHECK(f[1].d && !f[1].dDown && !f[1].dUp);
            CHECK(!f[2].d && !f[2].dDown && f[2].dUp);
            CHECK(!f[3].d && !f[3].dUp);
        }
        // upper case and names map to the engine's key codes
        probe->frames.clear();
        t.KeyDown('D');
        t.KeyDown("space");
        t.Step();
        CHECK(probe->frames.back().d && probe->frames.back().space);
        t.KeyUp("space");
        t.KeyUp('d');
        t.Step();
        CHECK(!probe->frames.back().d && !probe->frames.back().space);

        // a tap is one frame down, then up
        probe->frames.clear();
        t.TapKey('f');
        t.Step(); t.Step(); t.Step();
        CHECK(probe->frames.size() == 3);
        if (probe->frames.size() == 3) {
            CHECK(probe->frames[0].f && probe->frames[0].fDown);
            CHECK(!probe->frames[1].f && probe->frames[1].fUp);
            CHECK(!probe->frames[2].f && !probe->frames[2].fUp);
        }
        t.TapKey("f");
        t.Step();
        CHECK(probe->frames.back().fDown);

        // WASD as an axis, and release
        probe->frames.clear();
        t.SetMoveKeys({1, 1});
        t.Step();
        CHECK_NEAR(probe->frames.back().wasd.x, 1.0f, 1e-6);
        CHECK_NEAR(probe->frames.back().wasd.y, 1.0f, 1e-6);
        t.SetMoveKeys({-1, 0});
        t.Step();
        CHECK_NEAR(probe->frames.back().wasd.x, -1.0f, 1e-6);
        CHECK_NEAR(probe->frames.back().wasd.y, 0.0f, 1e-6);
        t.ReleaseAll();
        t.Step();
        CHECK_NEAR(probe->frames.back().wasd.x, 0.0f, 1e-6);
    }

    // --- Input injection: controller, mouse, text, touch ---
    {
        PlayTest t;
        auto* probe = t.scene().CreateGameObject("P")->AddComponent<InputProbe>();
        t.Step();
        CHECK(!probe->frames.back().connected);
        t.SetAxis({0.5f, -0.25f});
        t.SetLookAxis({-1.0f, 1.0f});
        t.SetTrigger(0, 0.3f);
        t.SetTrigger(1, 2.0f);                       // clamped
        t.SetTrigger(5, 1.0f);                       // not a trigger: ignored
        t.SetGamepadButton(Input::PadA, true);
        t.Step();
        {
            const auto& s = probe->frames.back();
            CHECK(s.connected);
            CHECK_NEAR(s.stick.x, 0.5f, 1e-6); CHECK_NEAR(s.stick.y, -0.25f, 1e-6);
            CHECK_NEAR(s.look.x, -1.0f, 1e-6); CHECK_NEAR(s.look.y, 1.0f, 1e-6);
            CHECK_NEAR(s.trigL, 0.3f, 1e-6); CHECK_NEAR(s.trigR, 1.0f, 1e-6);
            CHECK(s.padA && s.padADown);
        }
        t.Step();
        CHECK(probe->frames.back().padA && !probe->frames.back().padADown);
        t.SetGamepadButton(Input::PadA, false);
        t.SetGamepadButton(99, true);                // out of range: ignored
        t.Step();
        CHECK(!probe->frames.back().padA);

        // mouse
        t.MoveMouse({120, 45});
        t.Click(0);
        t.Step();
        CHECK_NEAR(probe->frames.back().mouse.x, 120.0f, 1e-6);
        CHECK_NEAR(probe->frames.back().mouse.y, 45.0f, 1e-6);
        CHECK(probe->frames.back().lmb && probe->frames.back().lmbDown);
        t.Step();
        CHECK(!probe->frames.back().lmb && probe->frames.back().lmbUp);
        t.MouseDown(1);
        t.Step(); t.Step();
        CHECK(probe->frames.back().rmb);
        t.MouseUp(1);
        t.MouseDown(7);                              // out of range: ignored
        t.Step();
        CHECK(!probe->frames.back().rmb);

        // wheel and typed text last one frame
        t.Scroll(2.0f);
        t.Scroll(1.0f);
        t.TypeText("hi");
        t.TypeText(" there");
        t.Step();
        CHECK_NEAR(probe->frames.back().wheel, 3.0f, 1e-6);
        CHECK(probe->frames.back().typed == "hi there");
        t.Step();
        CHECK(probe->frames.back().wheel == 0.0f && probe->frames.back().typed.empty());

        // touch
        t.Touch(1, 0, {10, 20});
        t.Step();
        CHECK(probe->frames.back().touches == 1);
        t.Touch(1, 2, {10, 20});
        t.Step();
        CHECK(probe->frames.back().touches == 1);    // a lifted finger stays one frame
        t.Step();
        CHECK(probe->frames.back().touches == 0);
    }
    {   // the next PlayTest starts with nothing held
        PlayTest t;
        auto* probe = t.scene().CreateGameObject("P")->AddComponent<InputProbe>();
        t.Step();
        const auto& s = probe->frames.back();
        CHECK(!s.d && !s.padA && !s.connected && !s.lmb && s.touches == 0 && s.mouse.x == 0.0f);
    }

    // --- The shared random generator is reseeded: random gameplay repeats ---
    {
        auto roll = [](std::uint64_t seed) {
            PlayTestOptions o; o.randomSeed = seed;
            PlayTest t(o);
            auto* d = t.scene().CreateGameObject("D")->AddComponent<Dice>();
            Random::Shared().Value();                 // stray draws before the run do not matter...
            t.RunFrames(5);
            return d->rolls;
        };
        auto a = roll(7), b = roll(7), c = roll(8);
        CHECK(a.size() == 5 && a == b);                 // ...the seed was applied when the PlayTest was made
        CHECK(a != c);
        auto defaults = [] {
            PlayTest t;
            auto* d = t.scene().CreateGameObject("D")->AddComponent<Dice>();
            t.RunFrames(3);
            return d->rolls;
        };
        CHECK(defaults() == defaults());                // default seed: also repeatable
    }

    // --- Quit stops the run early ---
    {
        PlayTest t;
        t.scene().CreateGameObject("Q")->AddComponent<Quitter>();
        CHECK(t.RunFrames(100) == 3);                // the script asked to quit on frame 3
        CHECK(t.QuitRequested() && t.Frame() == 3);
        CHECK(!t.RunUntil([] { return false; }, 5.0f));
        CHECK(t.Frame() == 3);
    }
    CHECK(!Game::QuitRequested());                    // and the destructor cleared it

    // --- Log capture and expected errors ---
    {
        PlayTest t;
        auto* lg = t.scene().CreateGameObject("L")->AddComponent<Logger>();
        lg->text = "boom: disk on fire";
        t.RunFrames(2);
        CHECK(t.LogLines().size() == 1);
        CHECK(t.LogContains("disk on fire"));
        CHECK(t.LogContains("boom", Log::Level::Error));
        CHECK(t.CountLog("disk") == 1 && t.CountLog("water") == 0);
        CHECK(t.LogLines()[0].level == Log::Level::Error && t.LogLines()[0].frame == 0);
        auto problems = t.Problems();
        CHECK(problems.size() == 1 && problems[0].find("unexpected error") != std::string::npos);
        CHECK(problems[0].find("disk on fire") != std::string::npos);

        t.ExpectError("disk on");
        CHECK(t.Problems().empty());                 // declared: fine
        t.ExpectError("never happens");
        problems = t.Problems();
        CHECK(problems.size() == 1 && problems[0].find("never happens") != std::string::npos);
        CHECK(problems[0].find("0 times") != std::string::npos);

        // a failing Finish() reports to the running test; Finish() on a clean log does not
        TestContext ctx;
        {
            TestContext::Scope scope(ctx);
            CHECK(!t.Finish());
        }
        CHECK(ctx.Failures().size() == 1);
        t.ClearLog();
        CHECK(t.LogLines().empty());
    }
    {   // counts: exactly N, any number (0), extras are unexpected
        PlayTest t;
        auto* lg = t.scene().CreateGameObject("L")->AddComponent<Logger>();
        lg->times = 3; lg->text = "repeat";
        t.Step();
        t.ExpectError("repeat", 2);
        CHECK(t.Problems().size() == 1);             // the third one is not expected
        t.ExpectError("repeat", 1);
        CHECK(t.Problems().empty());                 // now all three are claimed
    }
    {
        PlayTest u;
        auto* l2 = u.scene().CreateGameObject("L")->AddComponent<Logger>();
        l2->times = 5; l2->text = "noise";
        u.Step();
        u.ExpectError("noise", 0);
        CHECK(u.Problems().empty());                 // 0 = any number
    }
    {
        PlayTest v;
        v.ExpectError("optional", 0);
        CHECK(v.Problems().empty());                 // and not required either
    }
    {   // warnings are fine unless the option says otherwise
        PlayTest t;
        auto* lg = t.scene().CreateGameObject("L")->AddComponent<Logger>();
        lg->level = Log::Level::Warning; lg->text = "careful";
        t.Step();
        CHECK(t.LogContains("careful") && t.Problems().empty());
    }
    {
        PlayTestOptions o; o.treatWarningsAsErrors = true;
        PlayTest strict(o);
        auto* l2 = strict.scene().CreateGameObject("L")->AddComponent<Logger>();
        l2->level = Log::Level::Warning; l2->text = "careful";
        strict.Step();
        auto p = strict.Problems();
        CHECK(p.size() == 1 && p[0].find("unexpected warning") != std::string::npos);
        strict.ExpectError("careful");
        CHECK(strict.Problems().empty());
    }
    {   // the previous sink keeps getting lines while a PlayTest runs, and is back afterwards
        std::vector<std::string> seen;
        Log::Sink before = Log::sink;
        Log::sink = [&](Log::Level, const std::string& m) { seen.push_back(m); };
        {
            PlayTest t;
            Log::Info("inside");
            CHECK(t.LogContains("inside"));
        }
        Log::console = false;
        Log::Info("after");
        Log::console = true;
        auto at = [&](const char* m) { for (std::size_t i = 0; i < seen.size(); ++i) if (seen[i] == m) return (int)i; return -1; };
        CHECK(at("inside") >= 0 && at("after") > at("inside"));
        Log::sink = before;
        CHECK(Log::console);                         // the console flag came back too
    }
    {   // an unexpected error fails the running test when the PlayTest goes away
        TestContext ctx;
        {
            TestContext::Scope scope(ctx);
            PlayTest t;
            t.scene().CreateGameObject("L")->AddComponent<Logger>();
            t.Step();
        }
        CHECK(ctx.Failed() && ctx.Failures()[0].message.find("unexpected error") != std::string::npos);
        TestContext clean;
        {
            TestContext::Scope scope(clean);
            PlayTest t;
            auto* lg = t.scene().CreateGameObject("L")->AddComponent<Logger>();
            lg->text = "expected one";
            t.Step();
            t.ExpectError("expected one");
            CHECK(t.Finish());
        }                                            // Finish ran: the destructor does not report twice
        CHECK(!clean.Failed());
    }

    // --- EventProbe: collisions, triggers, lifecycle ---
    {
        PlayTest t;
        AddFallingBall(t.scene());
        auto* ballProbe = t.FindObject("Ball")->AddComponent<EventProbe>();
        auto* groundProbe = t.FindObject("Ground")->AddComponent<EventProbe>();
        CHECK(t.RunUntil([&] { return ballProbe->Saw("collision_enter:Ground"); }, 5.0f));
        CHECK(groundProbe->Saw("collision_enter:Ball"));
        CHECK(ballProbe->Count("collision_enter") >= 1);
        CHECK(ballProbe->Saw("start") && ballProbe->updates == t.Frame());
        ballProbe->Clear();
        CHECK(ballProbe->events.empty() && ballProbe->updates == 0);

        PlayTest u;
        GameObject* zone = u.scene().CreateGameObject("Zone");
        auto* zc = zone->AddComponent<BoxCollider2D>(); zc->size = {4, 4}; zc->isTrigger = true;
        zone->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Static;
        auto* zoneProbe = zone->AddComponent<EventProbe>();
        GameObject* mover = AddMover(u.scene(), "Walker", 4.0f);
        mover->transform->localPosition = {-6, 0, 0};
        mover->AddComponent<CircleCollider2D>()->radius = 0.25f;
        auto* rb = mover->AddComponent<Rigidbody2D>();
        rb->bodyType = Rigidbody2D::BodyType::Kinematic;
        CHECK(u.RunUntil([&] { return zoneProbe->Saw("trigger_enter:Walker"); }, 5.0f));
        CHECK(u.RunUntil([&] { return zoneProbe->Saw("trigger_exit:Walker"); }, 5.0f));
        CHECK(zoneProbe->Count("trigger_") >= 2);
    }

    // --- EventRecorder ---
    {
        EventBus bus;
        EventRecorder<Scored> rec(bus);
        CHECK(!rec.Any() && rec.Count() == 0);
        bus.Publish(Scored{5});
        bus.Publish(Scored{7});
        CHECK(rec.Count() == 2 && rec.events[1].points == 7);
        rec.Clear();
        CHECK(!rec.Any());
    }

    TEST_MAIN_RESULT();
}
