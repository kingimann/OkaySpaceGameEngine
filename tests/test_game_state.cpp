// 1.1 Game state: pause reasons (nesting), focus-loss auto-pause, quit with a reason and
// exit code, the quit guard (veto), Reset / BeginSession, and cross-thread quit.
#include "test_framework.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Time.hpp"
#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

int main() {
    RUN_SUITE("game_state");
    Game::Reset();
    Game::SetPauseOnFocusLoss(false);
    Game::NotifyFocus(true);
    Time::ResetForTesting();

    // ---- the plain SetPaused still works, as one more reason ---------------------------------------
    {
        CHECK(!Game::Paused() && !Time::Frozen());
        Game::SetPaused(true);
        CHECK(Game::Paused() && Time::Frozen() && Time::TimeScale() == 0.0f);
        CHECK(Game::IsPausedBy(Game::kManualPause));
        Game::SetPaused(true);                                // idempotent: one release is enough
        Game::SetPaused(false);
        CHECK(!Game::Paused() && !Time::Frozen());
        Game::TogglePause();
        CHECK(Game::Paused());
        Game::TogglePause();
        CHECK(!Game::Paused());
        CHECK(Game::PauseReasons().empty());
    }

    // ---- nesting: two systems pause, one resumes, the game stays paused -------------------------------
    {
        Game::PushPause("menu");
        Game::PushPause("cutscene");
        CHECK(Game::Paused() && Time::Frozen());
        CHECK(Game::IsPausedBy("menu") && Game::IsPausedBy("cutscene") && !Game::IsPausedBy("dialogue"));
        const std::vector<std::string> r = Game::PauseReasons();
        CHECK(r.size() == 2 && r[0] == "menu" && r[1] == "cutscene");      // oldest first
        CHECK(Game::PopPause("menu"));
        CHECK(Game::Paused());                                // the cutscene still holds it
        CHECK(!Game::IsPausedBy("menu"));
        CHECK(Game::PopPause("cutscene"));
        CHECK(!Game::Paused() && !Time::Frozen());
        CHECK(!Game::PopPause("cutscene"));                   // not held: false, nothing changes
        CHECK(!Game::Paused());
    }
    // the old API composes with reasons: SetPaused(false) does not release somebody else's pause
    {
        Game::SetPaused(true);                                // a pause menu, the old way
        Game::PushPause("cutscene");
        Game::SetPaused(false);                               // menu closes...
        CHECK(Game::Paused());                                // ...but the cutscene still holds (it used to un-pause)
        Game::PopPause("cutscene");
        CHECK(!Game::Paused());
        Game::PushPause("a");
        Game::SetPaused(true);
        Game::PopPause("a");
        CHECK(Game::Paused());                                // manual still holds
        Game::SetPaused(false);
        CHECK(!Game::Paused());
    }
    // pushes of one reason count; ClearPause drops all of them; empty names are stored
    {
        Game::PushPause("menu"); Game::PushPause("menu");
        Game::PopPause("menu");
        CHECK(Game::Paused() && Game::IsPausedBy("menu"));
        Game::PopPause("menu");
        CHECK(!Game::Paused());
        Game::PushPause("menu"); Game::PushPause("menu"); Game::PushPause("menu");
        Game::ClearPause("menu");
        CHECK(!Game::Paused());
        Game::ClearPause("menu");                             // clearing an absent reason is fine
        Game::PushPause("");
        CHECK(Game::IsPausedBy("unnamed") && Game::IsPausedBy(""));
        CHECK(Game::PopPause(""));
        CHECK(!Game::Paused());
    }
    // the time scale is left alone by every kind of pause
    {
        Time::SetTimeScale(0.5f);
        Game::PushPause("x");
        Time::SetTimeScale(0.25f);                            // set while paused: remembered
        Time::Step(0.1f);
        CHECK(Time::DeltaTime() == 0.0f);
        Game::PopPause("x");
        CHECK_NEAR(Time::TimeScale(), 0.25, 1e-6);
        Time::SetTimeScale(1.0f);
    }

    // ---- focus loss ---------------------------------------------------------------------------------------
    {
        CHECK(Game::HasFocus() && !Game::PauseOnFocusLoss());
        Game::NotifyFocus(false);                             // option off: nothing happens
        CHECK(!Game::Paused() && !Game::HasFocus());
        Game::NotifyFocus(true);
        Game::SetPauseOnFocusLoss(true);
        CHECK(Game::PauseOnFocusLoss() && !Game::Paused());
        Game::NotifyFocus(false);
        CHECK(Game::Paused() && Game::IsPausedBy(Game::kFocusPause));
        Game::NotifyFocus(false);                             // a repeated event does not stack
        Game::NotifyFocus(true);
        CHECK(!Game::Paused());
        // a menu opened before the alt-tab stays paused after it
        Game::PushPause("menu");
        Game::NotifyFocus(false);
        Game::NotifyFocus(true);
        CHECK(Game::Paused() && Game::IsPausedBy("menu") && !Game::IsPausedBy("focus"));
        Game::PopPause("menu");
        CHECK(!Game::Paused());
        // already unfocused when the option is switched on: pauses now; off: resumes
        Game::SetPauseOnFocusLoss(false);
        Game::NotifyFocus(false);
        CHECK(!Game::Paused());
        Game::SetPauseOnFocusLoss(true);
        CHECK(Game::Paused());
        Game::SetPauseOnFocusLoss(false);
        CHECK(!Game::Paused());
        // Reset while still unfocused with the option on starts paused (and refocus resumes)
        Game::SetPauseOnFocusLoss(true);
        Game::Reset();
        CHECK(Game::Paused() && Game::IsPausedBy("focus"));
        Game::NotifyFocus(true);
        CHECK(!Game::Paused());
        Game::SetPauseOnFocusLoss(false);
    }

    // ---- quit with a reason and an exit code ----------------------------------------------------------------
    {
        Game::Reset();
        CHECK(!Game::QuitRequested() && Game::ExitCode() == 0 && Game::QuitReason().empty());
        CHECK(Game::RequestQuit());                           // the old call still works
        CHECK(Game::QuitRequested() && Game::ExitCode() == 0 && Game::QuitReason().empty());
        Game::Reset();
        CHECK(!Game::QuitRequested());
        CHECK(Game::RequestQuit("menu: quit", 0));
        CHECK(Game::QuitReason() == "menu: quit" && Game::ExitCode() == 0);
        Game::RequestQuit("later", 0);                        // asking again keeps the first reason
        CHECK(Game::QuitReason() == "menu: quit");
        Game::RequestQuit("fatal: out of memory", 3);         // but an error code is not hidden
        CHECK(Game::ExitCode() == 3 && Game::QuitReason() == "fatal: out of memory");
        Game::RequestQuit("whatever", 0);
        CHECK(Game::ExitCode() == 3);
        Game::RequestQuit("another", 5);                      // the first error code stands
        CHECK(Game::ExitCode() == 3);
        Game::Reset();                                        // a new session forgets all of it
        CHECK(!Game::QuitRequested() && Game::ExitCode() == 0 && Game::QuitReason().empty());
        Game::RequestQuit("x", 2);
        Game::CancelQuit();                                   // the player chose "keep playing"
        CHECK(!Game::QuitRequested() && Game::ExitCode() == 0 && Game::QuitReason().empty());
    }

    // ---- the quit guard ----------------------------------------------------------------------------------------
    {
        Game::Reset();
        bool unsaved = true;
        int asked = 0;
        Game::SetQuitGuard([&] { ++asked; return !unsaved; });
        CHECK(!Game::RequestQuit("window closed"));           // vetoed: "you have unsaved changes"
        CHECK(!Game::QuitRequested() && asked == 1 && Game::QuitVetoCount() == 1);
        CHECK(Game::QuitReason().empty());
        unsaved = false;
        CHECK(Game::RequestQuit("window closed", 0));
        CHECK(Game::QuitRequested() && Game::QuitReason() == "window closed" && asked == 2);
        Game::RequestQuit("again");                           // already pending: the guard is not asked again
        CHECK(asked == 2);
        // ForceQuit skips the guard (the dialog's Quit button, an OS shutdown)
        Game::CancelQuit();
        unsaved = true;
        Game::ForceQuit("os shutdown", 0);
        CHECK(Game::QuitRequested() && Game::QuitReason() == "os shutdown" && asked == 2);
        // a guard that asks for a quit itself does not recurse
        Game::Reset();
        int depth = 0;
        Game::SetQuitGuard([&] { ++depth; const bool r = Game::RequestQuit("nested"); CHECK(!r); return true; });
        CHECK(Game::RequestQuit("outer"));
        CHECK(depth == 1 && Game::QuitReason() == "outer");
        // a guard that throws allows the quit
        Game::Reset();
        Game::SetQuitGuard([]() -> bool { throw std::runtime_error("bug in the guard"); });
        CHECK(Game::RequestQuit("x"));
        CHECK(Game::QuitRequested());
        // removing the guard, and Reset removes it (a stale lambda must not outlive its scene)
        Game::Reset();
        Game::SetQuitGuard([] { return false; });
        Game::SetQuitGuard({});
        CHECK(Game::RequestQuit());
        Game::Reset();
        Game::SetQuitGuard([] { return false; });
        Game::Reset();
        CHECK(Game::RequestQuit());
        CHECK(Game::QuitVetoCount() == 0);                    // counted per session
        Game::Reset();
    }

    // ---- Reset keeps the scale (documented); BeginSession gives a clean slate -------------------------------------
    {
        Time::ResetForTesting();
        Time::SetTimeScale(0.1f);                             // a script slowed the game down and Play stopped
        Game::SetPaused(true);
        Time::HitStop(5.0f, 0.0f);
        Game::RequestQuit("x", 1);
        Game::Reset();
        CHECK(!Game::Paused() && !Game::QuitRequested() && !Time::HitStopActive());
        CHECK_NEAR(Time::TimeScale(), 0.1, 1e-6);             // the next Play would start in slow motion...
        Game::BeginSession();                                 // ...unless it begins a session
        CHECK_NEAR(Time::TimeScale(), 1.0, 1e-9);
        CHECK(!Game::Paused() && !Time::Frozen() && Time::EffectiveTimeScale() == 1.0f);
    }

    // ---- ForceQuit / QuitRequested / QuitReason / ExitCode from other threads ------------------------------------------
    {
        Game::Reset();
        std::atomic<bool> go{false};
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t)
            workers.emplace_back([&, t] {
                while (!go.load()) std::this_thread::yield();
                for (int i = 0; i < 500; ++i) {
                    Game::ForceQuit("worker " + std::to_string(t), t);
                    (void)Game::QuitRequested(); (void)Game::QuitReason(); (void)Game::ExitCode();
                }
            });
        go = true;
        for (auto& w : workers) w.join();
        CHECK(Game::QuitRequested());
        const std::string reason = Game::QuitReason();
        CHECK(reason.rfind("worker ", 0) == 0);               // one intact reason, not a torn string
        CHECK(Game::ExitCode() >= 0 && Game::ExitCode() <= 3);
        Game::Reset();
    }

    Game::Reset();
    TEST_MAIN_RESULT();
}
