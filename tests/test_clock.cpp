// 1.1 Clock channels: "game" / "ui" / "cinematic" with their own scale and pause, so a
// pause menu keeps animating while gameplay is frozen.
#include "test_framework.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Time.hpp"
#include <cmath>
#include <limits>
#include <string>

using namespace okay;

namespace {
void Fresh() {
    Game::Reset();
    Time::ResetForTesting();
    ClockSet::Global().Reset();
}
// One host frame: step Time, tick the set.
void Frame(float dt) { Time::Step(dt); ClockSet::Global().TickFromTime(); }
}  // namespace

int main() {
    RUN_SUITE("clock");
    Fresh();

    // ---- the three built-in channels exist ----------------------------------------------------------
    {
        ClockSet& set = ClockSet::Global();
        CHECK(set.Count() == 3);
        CHECK(Clock::Exists("game") && Clock::Exists("ui") && Clock::Exists("cinematic"));
        CHECK(!Clock::Exists("dialogue"));
        CHECK(Clock::Get("game").GetSource() == Clock::Source::Scaled);
        CHECK(Clock::Get("ui").GetSource() == Clock::Source::Unscaled);
        CHECK(Clock::Get("cinematic").HoldsWhileGamePaused());
        CHECK(!Clock::Get("ui").HoldsWhileGamePaused());
        CHECK(Clock::Get("ui").Name() == "ui");
    }

    // ---- default channels follow Time's scaled and unscaled clocks ------------------------------------------
    {
        Fresh();
        Frame(0.016f);
        CHECK_NEAR(Clock::Get("game").Delta(), Time::DeltaTime(), 1e-9);
        CHECK_NEAR(Clock::Get("ui").Delta(), Time::UnscaledDeltaTime(), 1e-9);
        Time::SetTimeScale(0.5f);
        Frame(0.02f);
        CHECK_NEAR(Clock::Get("game").Delta(), 0.01, 1e-7);   // slow motion slows the game clock...
        CHECK_NEAR(Clock::Get("ui").Delta(), 0.02, 1e-7);     // ...not the ui, nor
        CHECK_NEAR(Clock::Get("cinematic").Delta(), 0.02, 1e-7);   // a cutscene
        CHECK_NEAR(Clock::Get("game").Elapsed(), 0.016 + 0.01, 1e-6);
        CHECK_NEAR(Clock::Get("ui").Elapsed(), Time::UnscaledTime(), 1e-6);
        CHECK(Clock::Get("game").Ticks() == 2);
        Time::SetTimeScale(1.0f);
    }

    // ---- the feature map's question: pause freezes the timers, the menu animation must go on -------------------
    {
        Fresh();
        Game::SetPaused(true);                                // the pause menu is up
        for (int i = 0; i < 10; ++i) Frame(0.016f);
        CHECK(Clock::Get("game").Delta() == 0.0f);
        CHECK(Clock::Get("game").Elapsed() == 0.0);
        CHECK_NEAR(Clock::Get("ui").Elapsed(), 0.16, 1e-6);   // the menu's animation clock ran
        CHECK(Clock::Get("ui").Delta() > 0.0f);
        CHECK(Clock::Get("cinematic").Delta() == 0.0f);       // a cutscene holds with the game
        CHECK(Clock::Get("cinematic").Elapsed() == 0.0);
        Game::SetPaused(false);
        Frame(0.016f);
        CHECK(Clock::Get("game").Delta() > 0.0f && Clock::Get("cinematic").Delta() > 0.0f);
        CHECK_NEAR(Clock::Get("ui").Elapsed(), 0.176, 1e-6);
    }

    // ---- a hit stop freezes the game clock only -----------------------------------------------------------------------
    {
        Fresh();
        Time::HitStop(0.1f, 0.0f);
        Frame(0.05f);
        CHECK(Clock::Get("game").Delta() == 0.0f);
        CHECK_NEAR(Clock::Get("ui").Delta(), 0.05, 1e-7);
        CHECK_NEAR(Clock::Get("cinematic").Delta(), 0.05, 1e-7);
        Fresh();
    }

    // ---- each clock's own scale and pause ----------------------------------------------------------------------------
    {
        Fresh();
        Clock& ui = Clock::Get("ui");
        ui.SetScale(2.0f);
        Frame(0.01f);
        CHECK_NEAR(ui.Delta(), 0.02, 1e-7);
        CHECK_NEAR(ui.SourceDelta(), 0.01, 1e-7);
        CHECK_NEAR(Clock::Get("game").Delta(), 0.01, 1e-7);   // untouched
        ui.Pause();
        Frame(0.01f);
        CHECK(ui.Paused() && ui.Delta() == 0.0f && ui.SourceDelta() > 0.0f);
        CHECK_NEAR(ui.Elapsed(), 0.02, 1e-7);
        CHECK(Clock::Get("game").Delta() > 0.0f);             // the game did not stop
        ui.Resume();
        Frame(0.01f);
        CHECK_NEAR(ui.Elapsed(), 0.04, 1e-7);
        ui.SetPaused(true); CHECK(ui.Paused()); ui.SetPaused(false);
        // a game-clock scale on top of the Time scale
        Clock& g = Clock::Get("game");
        g.SetScale(0.5f);
        Time::SetTimeScale(0.5f);
        Frame(0.04f);
        CHECK_NEAR(g.Delta(), 0.04 * 0.5 * 0.5, 1e-7);
        // bad scales
        g.SetScale(-3.0f);       CHECK(g.Scale() == 0.0f);
        g.SetScale(std::numeric_limits<float>::quiet_NaN());  CHECK(g.Scale() == 0.0f);
        g.SetScale(std::numeric_limits<float>::infinity());   CHECK(g.Scale() <= Time::kMaxTimeScale && g.Scale() > 0.0f);
        Time::SetTimeScale(1.0f);
        Fresh();
    }

    // ---- custom channels: created on first use, on real time ------------------------------------------------------------
    {
        Fresh();
        CHECK(!Clock::Exists("dialogue"));
        Clock& d = Clock::Get("dialogue");
        CHECK(Clock::Exists("dialogue") && ClockSet::Global().Count() == 4);
        CHECK(&d == &Clock::Get("dialogue"));                 // the same clock, and a stable reference
        Frame(0.1f);
        CHECK_NEAR(d.Delta(), 0.1, 1e-7);
        Game::SetPaused(true);
        Frame(0.1f);
        CHECK_NEAR(d.Delta(), 0.1, 1e-7);                     // a custom channel does not hold for the game pause...
        d.SetHoldsWhileGamePaused(true);                      // ...unless asked to
        Frame(0.1f);
        CHECK(d.Delta() == 0.0f);
        d.SetSource(Clock::Source::Scaled);
        Game::SetPaused(false);
        Time::SetTimeScale(0.5f);
        Frame(0.1f);
        CHECK_NEAR(d.Delta(), 0.05, 1e-7);
        Time::SetTimeScale(1.0f);
        // many channels added later do not move the ones already handed out
        Clock* first = &Clock::Get("c0");
        for (int i = 1; i < 200; ++i) Clock::Get("c" + std::to_string(i));
        CHECK(first == &Clock::Get("c0"));
        CHECK(ClockSet::Global().Count() == 4 + 200);
        // Remove: custom yes, built-ins never
        CHECK(ClockSet::Global().Remove("c5") && !Clock::Exists("c5"));
        CHECK(!ClockSet::Global().Remove("c5"));
        CHECK(!ClockSet::Global().Remove("game") && !ClockSet::Global().Remove("ui") && !ClockSet::Global().Remove("cinematic"));
        CHECK(Clock::Exists("game"));
        for (int i = 0; i < 200; ++i) ClockSet::Global().Remove("c" + std::to_string(i));
        ClockSet::Global().Remove("dialogue");
        CHECK(ClockSet::Global().Count() == 3);
        const auto names = ClockSet::Global().Names();
        CHECK(names.size() == 3 && names[0] == "game" && names[1] == "ui" && names[2] == "cinematic");
    }

    // ---- a private set (tests, a second world), explicit ticking, bad times ----------------------------------------------
    {
        ClockSet set;
        Clock& a = set.Add("a", Clock::Source::Scaled);
        Clock& again = set.Add("a", Clock::Source::Unscaled);   // existing: returned as is
        CHECK(&a == &again && a.GetSource() == Clock::Source::Scaled);
        set.Tick(0.5f, 1.0f, false);
        CHECK_NEAR(a.Delta(), 0.5, 1e-9);
        CHECK_NEAR(set.Get("game").Delta(), 0.5, 1e-9);
        CHECK_NEAR(set.Get("ui").Delta(), 1.0, 1e-9);
        set.Tick(std::numeric_limits<float>::quiet_NaN(), -1.0f, false);   // never negative, never NaN
        CHECK(a.Delta() == 0.0f && set.Get("ui").Delta() == 0.0f);
        CHECK(std::isfinite(a.Elapsed()) && a.Elapsed() == 0.5);
        set.Tick(std::numeric_limits<float>::infinity(), 0.25f, true);
        CHECK(a.Delta() == 0.0f && std::isfinite(a.Elapsed()));
        CHECK(set.Get("ui").Delta() > 0.0f && set.Get("cinematic").Delta() == 0.0f);
        set.Reset();
        CHECK(a.Elapsed() == 0.0 && a.Ticks() == 0 && a.Scale() == 1.0f && !a.Paused());
        a.SetScale(3.0f); a.Pause();
        set.Reset();                                           // Reset restores scale and pause as well
        CHECK(a.Scale() == 1.0f && !a.Paused());
        // a long session: elapsed is a double and keeps counting
        for (int i = 0; i < 1000000; ++i) set.Tick(0.016f, 0.016f, false);
        CHECK_NEAR(a.Elapsed(), 1000000 * (double)0.016f, 1e-3);
    }

    // ---- TickOncePerFrame ticks once per Time frame ----------------------------------------------------------------------------
    {
        Fresh();
        ClockSet& set = ClockSet::Global();
        Time::Step(0.1f);
        set.TickOncePerFrame();
        set.TickOncePerFrame();                                // the same frame: ignored
        CHECK(Clock::Get("ui").Ticks() == 1);
        Time::Step(0.1f);
        set.TickOncePerFrame();
        CHECK(Clock::Get("ui").Ticks() == 2);
        CHECK_NEAR(Clock::Get("ui").Elapsed(), 0.2, 1e-6);
        Fresh();
        set.TickOncePerFrame();                                // Time never stepped (frame 0): every call ticks
        set.TickOncePerFrame();
        CHECK(Clock::Get("ui").Ticks() == 2);
        Fresh();
    }

    Fresh();
    TEST_MAIN_RESULT();
}
