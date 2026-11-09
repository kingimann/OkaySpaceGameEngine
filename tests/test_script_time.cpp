// 1.1 fifth pass: what a game script can read and do about the loop and time. Both script languages:
// OkayScript (the default VM: dotted properties and builtins) and the typed one (NextScriptVM, whose
// binding table also generates the visual-script nodes and docs/engine_api.md).
//
//   Time.sessionTime / sessionUnscaledTime / sessionFrameCount    since this Play began (the process clocks never rewind)
//   Time.unscaledDeltaTime / unscaledTime                         real time (the OkayScript one used to answer the scaled delta)
//   Time.fixedDeltaTime                                           the fixed step (OkayScript used to answer the frame's delta)
//   Time.fps / hitchCount / paused                                real frame statistics and the pause
//   Time.HitStop(seconds, scale) / SlowMotion(scale, seconds) / RampTimeScale(target, seconds)
//
// Every frame time is made up; nothing sleeps.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/GameLoop.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

static std::vector<std::string> g_errs;

static void Fresh() {
    Game::SetPauseOnFocusLoss(false);
    Game::NotifyFocus(true);
    Game::Reset();
    Time::ResetForTesting();
    Time::SetFixedDeltaTime(0.02f);
}

// One OkayScript object with `body` as its script.
struct Okay {
    Scene scene{"S"};
    ScriptComponent* sc = nullptr;
    bool ok = false;
    explicit Okay(const std::string& body) {
        sc = scene.CreateGameObject("Hero")->AddComponent<ScriptComponent>();
        std::string e;
        ok = sc->LoadSource(body, &e);
        if (!ok) std::printf("  parse error: %s\n", e.c_str());
        scene.Start();
    }
    // One frame: Time steps by `real`, then the scene updates by the scaled step (what a loop host does).
    void Frame(float real) { Time::Step(real); scene.Update(Time::DeltaTime()); }
    float F(const char* n) { return sc->VM()->GetGlobal(n).AsFloat(); }
    bool B(const char* n) { return sc->VM()->GetGlobal(n).AsBool(); }
};

// One typed-language object.
struct Typed {
    Scene scene{"S"};
    ScriptComponent* sc = nullptr;
    bool ok = false;
    std::string error;
    explicit Typed(const std::string& body) {
        sc = scene.CreateGameObject("Hero")->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        ok = sc->LoadSource("class T : Behaviour {\n" + body + "\n}\n", &error);
        if (!ok) std::printf("  parse error: %s\n", error.c_str());
        scene.Start();
    }
    void Frame(float real) { Time::Step(real); scene.Update(Time::DeltaTime()); }
    vs::VsValue Get(const char* n) { return sc->VM()->GetGlobal(n); }
};

int main() {
    RUN_SUITE("script_time");
    Log::sink = [](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) g_errs.push_back(m); };

    // ---- OkayScript: the session clocks, real time and the fixed step ---------------------------------------------
    {
        Fresh();
        Time::Step(2.0f);                       // the process has run for two seconds before this Play
        Game::BeginSession();                   // what the editor's Play, the player's scene start and GameLoop::BeginSession call
        Okay o("float st; float sut; float sfc; float tm; float udt; float ut; float fd; float dl;\n"
               "void Update() { st = Time.sessionTime; sut = Time.sessionUnscaledTime; sfc = Time.sessionFrameCount; tm = Time.time;\n"
               "                udt = Time.unscaledDeltaTime; ut = Time.unscaledTime; fd = Time.fixedDeltaTime; dl = Time.deltaTime; }");
        CHECK(o.ok);
        Time::SetTimeScale(0.5f);
        o.Frame(0.25f);
        o.Frame(0.25f);
        CHECK_NEAR(o.F("st"), 0.25, 1e-6);          // two frames of 0.25 at half speed: a quarter second of game time
        CHECK_NEAR(o.F("sut"), 0.5, 1e-6);          // half a second of real time
        CHECK(o.F("sfc") == 2.0f);
        CHECK_NEAR(o.F("tm"), 2.25, 1e-5);          // Time.time is the process's: it did not start over
        CHECK_NEAR(o.F("udt"), 0.25, 1e-6);         // real: the old answer was the scaled 0.125
        CHECK_NEAR(o.F("ut"), 2.5, 1e-5);
        CHECK_NEAR(o.F("fd"), 0.02, 1e-6);          // the fixed step, not this frame's delta
        CHECK_NEAR(o.F("dl"), 0.125, 1e-6);
        // a pause: game time stands still, real time does not
        Game::PushPause("menu");
        o.Frame(0.25f);
        CHECK_NEAR(o.F("st"), 0.25, 1e-6);
        CHECK_NEAR(o.F("sut"), 0.75, 1e-6);
        CHECK(o.F("sfc") == 3.0f);
        CHECK_NEAR(o.F("udt"), 0.25, 1e-6);         // a pause no longer reads as 0
        CHECK(o.F("dl") == 0.0f);
        Game::PopPause("menu");
        // a new Play starts them again
        Game::BeginSession();
        o.Frame(0.1f);
        CHECK(o.F("sfc") == 1.0f);
        CHECK_NEAR(o.F("sut"), 0.1, 1e-6);
        CHECK(o.F("tm") > 2.3f);                    // still the process's clock (2.25 s before this Play's frame)
    }

    // ---- OkayScript: builtins and aliases ------------------------------------------------------------------------------
    {
        Fresh();
        Game::BeginSession();
        Okay o("float r1; float r2; float r3; float r4; float r5; bool r6; float r7;\n"
               "void Update() { r1 = session_time(); r2 = session_unscaled_time(); r3 = session_frame_count(); r4 = unscaled_dt(); r5 = fps(); r6 = is_paused(); r7 = hitch_count(); }");
        CHECK(o.ok);
        Time::SetTimeScale(0.25f);
        o.Frame(0.1f);
        CHECK_NEAR(o.F("r1"), 0.025, 1e-6);
        CHECK_NEAR(o.F("r2"), 0.1, 1e-6);
        CHECK(o.F("r3") == 1.0f);
        CHECK_NEAR(o.F("r4"), 0.1, 1e-6);
        CHECK(!o.B("r6"));
        CHECK(o.F("r7") == 0.0f);
        CHECK_NEAR(o.F("r5"), 10.0, 1e-3);          // no loop: the last real frame (the old formula read 1 / 0.025 = 40)
        Game::SetPaused(true);
        o.Frame(0.1f);
        CHECK(o.B("r6"));
        CHECK_NEAR(o.F("r5"), 10.0, 1e-3);          // fps() read 0 in a pause
        Game::SetPaused(false);
    }

    // ---- OkayScript: Time.fps and hitchCount from the loop's REAL frame times ---------------------------------------------
    {
        Fresh();
        GameLoop::Options opt; opt.smoothDelta = false;
        GameLoop loop(opt);
        loop.BeginSession();
        CHECK(GameLoop::Current() != &loop);        // nothing has run on this one yet
        Okay o("float fp; float hc;\nvoid Update() { fp = Time.fps; hc = Time.hitchCount; }");
        CHECK(o.ok);
        loop.OnUpdate = [&](float) { o.scene.Update(Time::DeltaTime()); };
        for (int i = 0; i < 40; ++i) loop.BeginFrame(0.02);
        CHECK(GameLoop::Current() == &loop);
        CHECK_NEAR(o.F("fp"), 50.0, 0.01);
        CHECK(o.F("hc") == 0.0f);
        Time::SetTimeScale(0.1f);                    // slow motion does not change the real rate
        for (int i = 0; i < 40; ++i) loop.BeginFrame(0.02);
        CHECK_NEAR(o.F("fp"), 50.0, 0.01);
        loop.BeginFrame(5.0);                        // a 5 s stall: the game steps 0.25 s, the readout sees the stall
        CHECK(o.F("hc") == 1.0f);
        CHECK(o.F("fp") < 10.0f);                    // 30 frames: 29 x 20 ms + 5 s
        Time::SetTimeScale(1.0f);
        loop.OnUpdate = nullptr;
    }
    CHECK(GameLoop::Current() == nullptr);           // the loop is gone: no dangling pointer

    // ---- OkayScript and typed: pause reasons of a script's own ----------------------------------------------------------------------------
    {
        Fresh();
        Game::BeginSession();
        Okay o("bool a; bool b; bool c;\n"
               "void Dialogue() { push_pause(\"dialogue\"); }\n"
               "void Shop() { Time.PushPause(\"shop\"); }\n"
               "void CloseDialogue() { a = pop_pause(\"dialogue\"); b = is_paused_by(\"shop\"); c = Time.PopPause(\"nothing\"); }\n"
               "void CloseShop() { Time.PopPause(\"shop\"); }");
        CHECK(o.ok);
        o.sc->VM()->CallEvent("Dialogue");
        o.sc->VM()->CallEvent("Shop");
        CHECK(Game::Paused() && Game::IsPausedBy("dialogue") && Game::IsPausedBy("shop"));
        o.sc->VM()->CallEvent("CloseDialogue");
        CHECK(o.B("a") && o.B("b") && !o.B("c"));         // the dialogue closed, the shop still holds the pause, "nothing" was never held
        CHECK(Game::Paused());                            // closing one never resumed the other
        o.sc->VM()->CallEvent("CloseShop");
        CHECK(!Game::Paused());
        // a pause the script left held does not outlive the Play
        o.sc->VM()->CallEvent("Dialogue");
        Game::BeginSession();
        CHECK(!Game::Paused());

        Typed t("    public bool held; public bool popped;\n"
                "    public void Open() { Time.PushPause(\"menu\"); held = Time.IsPausedBy(\"menu\"); }\n"
                "    public void Close() { popped = Time.PopPause(\"menu\"); held = Time.IsPausedBy(\"menu\"); }\n");
        CHECK(t.ok);
        Game::PushPause("cutscene");
        t.sc->VM()->CallEvent("Open");
        CHECK(t.Get("held").AsBool() && Game::IsPausedBy("menu"));
        t.sc->VM()->CallEvent("Close");
        CHECK(t.Get("popped").AsBool() && !t.Get("held").AsBool());
        CHECK(Game::Paused() && Game::IsPausedBy("cutscene"));             // the cutscene's own pause is still there
        Game::PopPause("cutscene");
    }

    // ---- OkayScript: hit stop, slow motion, ramps (builtins and the C#-style dotted calls) -------------------------------
    {
        Fresh();
        Game::BeginSession();
        Okay o("void Start() { hit_stop(0.2); }\n"
               "void Update() { }\n"
               "void Slow() { slow_motion(0.25, 3); }\n"
               "void Ramp() { ramp_timescale(0.5, 2); }\n"
               "void Hit2() { hit_stop(0.1, 0.5); }\n"
               "void Dotted() { Time.HitStop(0.3, 0.1); Time.SlowMotion(0.5, 4); Time.RampTimeScale(2, 1); }");
        CHECK(o.ok);
        CHECK(Time::HitStopActive());                      // hit_stop(0.2): a freeze frame
        CHECK_NEAR(Time::HitStopRemaining(), 0.2, 1e-6);
        o.Frame(0.1f);
        CHECK(Time::DeltaTime() == 0.0f);                  // frozen by the stop
        o.Frame(0.1f);
        o.Frame(0.1f);
        CHECK(!Time::HitStopActive());                     // and over: it restores itself
        CHECK(Time::DeltaTime() > 0.0f);
        o.sc->VM()->CallEvent("Slow");
        CHECK(Time::HitStopActive() && std::fabs(Time::EffectiveTimeScale() - 0.25f) < 1e-6f);
        CHECK_NEAR(Time::HitStopRemaining(), 3.0, 1e-6);
        Time::CancelHitStop();
        o.sc->VM()->CallEvent("Ramp");
        CHECK(Time::IsRamping() && Time::RampTarget() == 0.5f);
        Time::CancelRamp();
        o.sc->VM()->CallEvent("Hit2");
        CHECK(std::fabs(Time::EffectiveTimeScale() - 0.5f) < 1e-6f);
        Time::CancelHitStop();
        o.sc->VM()->CallEvent("Dotted");               // the three C#-style forms
        CHECK_NEAR(Time::HitStopRemaining(), 4.0, 1e-6);
        CHECK(std::fabs(Time::EffectiveTimeScale() - 0.1f) < 1e-6f);   // the strongest running stop wins
        CHECK(Time::IsRamping() && Time::RampTarget() == 2.0f);
        Game::BeginSession();                              // a new Play inherits none of it
        CHECK(!Time::HitStopActive() && !Time::IsRamping());
    }

    // ---- typed language: the same properties and methods ------------------------------------------------------------------------
    {
        Fresh();
        Time::Step(3.0f);
        Game::BeginSession();
        const std::string src =
            "    public float st; public float sut; public int sfc; public float udt; public float ut; public float fps;\n"
            "    public bool paused; public int hitches; public float tm;\n"
            "    void Update() { st = Time.sessionTime; sut = Time.sessionUnscaledTime; sfc = Time.sessionFrameCount;\n"
            "                    udt = Time.unscaledDeltaTime; ut = Time.unscaledTime; fps = Time.fps; paused = Time.paused;\n"
            "                    hitches = Time.hitchCount; tm = Time.time; }\n"
            "    public void Hit() { Time.HitStop(0.2, 0.0); }\n"
            "    public void Slow() { Time.SlowMotion(0.25, 3.0); }\n"
            "    public void Ramp() { Time.RampTimeScale(0.5, 2.0); }\n";
        NextScriptVM vm;
        const auto diags = vm.ValidateAll("class T : Behaviour {\n" + src + "\n}\n");
        int errors = 0;
        for (const auto& d : diags) if (d.severity == 0) { ++errors; std::printf("  diagnostic: line %d: %s\n", d.line, d.message.c_str()); }
        CHECK(errors == 0);
        Typed t(src);
        CHECK(t.ok);
        Time::SetTimeScale(0.5f);
        t.Frame(0.25f);
        t.Frame(0.25f);
        CHECK_NEAR(t.Get("st").AsFloat(), 0.25, 1e-6);
        CHECK_NEAR(t.Get("sut").AsFloat(), 0.5, 1e-6);
        CHECK(t.Get("sfc").AsFloat() == 2);
        CHECK_NEAR(t.Get("udt").AsFloat(), 0.25, 1e-6);
        CHECK_NEAR(t.Get("ut").AsFloat(), 3.5, 1e-5);
        CHECK_NEAR(t.Get("tm").AsFloat(), 3.25, 1e-5);
        CHECK(!t.Get("paused").AsBool());
        CHECK(t.Get("hitches").AsFloat() == 0);
        CHECK(t.Get("fps").AsFloat() == 0.0f);             // no loop has run: nothing to measure
        Game::PushPause("cutscene");
        t.Frame(0.25f);
        CHECK(t.Get("paused").AsBool());
        CHECK_NEAR(t.Get("udt").AsFloat(), 0.25, 1e-6);
        Game::PopPause("cutscene");

        // with a loop running
        GameLoop::Options opt; opt.smoothDelta = false;
        GameLoop loop(opt);
        loop.BeginSession();
        loop.OnUpdate = [&](float) { t.scene.Update(Time::DeltaTime()); };
        for (int i = 0; i < 30; ++i) loop.BeginFrame(0.016);
        CHECK_NEAR(t.Get("fps").AsFloat(), 62.5, 0.01);
        loop.BeginFrame(0.4);
        CHECK(t.Get("hitches").AsFloat() == 1);
        loop.OnUpdate = nullptr;

        t.sc->VM()->CallEvent("Hit");
        CHECK(Time::HitStopActive() && Time::HitStopRemaining() > 0.19f);
        Time::CancelHitStop();
        t.sc->VM()->CallEvent("Slow");
        CHECK(std::fabs(Time::EffectiveTimeScale() - 0.25f) < 1e-6f);    // the loop's BeginSession put the scale back to 1
        Time::CancelHitStop();
        t.sc->VM()->CallEvent("Ramp");
        CHECK(Time::IsRamping() && Time::RampTarget() == 0.5f);
        Time::CancelRamp();
    }

    // ---- the binding table lists them (this is what the visual-script nodes and docs/engine_api.md are generated from) --------
    {
        for (const char* n : {"Time.sessionTime", "Time.sessionUnscaledTime", "Time.sessionFrameCount", "Time.unscaledDeltaTime", "Time.unscaledTime",
                              "Time.fps", "Time.hitchCount", "Time.paused", "Time.time", "Time.HitStop/2", "Time.SlowMotion/2", "Time.RampTimeScale/2", "Time.PushPause/1", "Time.PopPause/1", "Time.IsPausedBy/1"}) {
            const vs::VsApiEntry* e = vs::VsApiFind(n);
            CHECK(e != nullptr);
            if (!e) std::printf("  no node %s\n", n);
        }
        const vs::VsApiEntry* hs = vs::VsApiFind("Time.HitStop/2");
        CHECK(hs && hs->exec && hs->ins.size() == 2 && !hs->hasTarget);   // a step with two inputs, no target (Time is static)
        const vs::VsApiEntry* st = vs::VsApiFind("Time.sessionTime");
        CHECK(st && !st->exec && st->outs.size() == 1);                   // a value node
    }

    CHECK(g_errs.empty());
    for (const auto& e : g_errs) std::printf("  logged error: %s\n", e.c_str());
    TEST_MAIN_RESULT();
}
