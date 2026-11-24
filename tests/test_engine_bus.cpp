// Game publishes pause and quit on the engine event bus.
#include "test_framework.hpp"
#include "okay/Core/EventBus.hpp"
#include "okay/Core/Game.hpp"
#include <stdexcept>

using namespace okay;

int main() {
    RUN_SUITE("engine_bus");
    Game::Reset();
    Game::SetPaused(false);
    int pauses = 0;
    bool last = false;
    auto pauseSub = EngineBus().Listen<GamePaused>([&](const GamePaused& e) { ++pauses; last = e.paused; });
    Game::SetPaused(true);
    CHECK(pauses == 1 && last);
    Game::SetPaused(false);
    CHECK(pauses == 2 && !last);
    Game::SetPaused(false);   // already running: no second resume
    CHECK(pauses == 2);

    int quits = 0;
    bool accepted = false;
    auto quitSub = EngineBus().Listen<GameQuit>([&](const GameQuit& e) { ++quits; accepted = e.accepted; });
    Game::SetQuitGuard([] { return false; });
    CHECK(!Game::RequestQuit("nope", 0));
    CHECK(quits == 1 && !accepted);
    Game::SetQuitGuard({});
    CHECK(Game::RequestQuit("bye", 3));
    CHECK(quits == 2 && accepted);
    CHECK(Game::QuitRequested());
    Game::Reset();

    // A handler that throws does not escape RequestQuit: the quit still happens.
    {
        auto bad = EngineBus().Listen<GameQuit>([](const GameQuit&) { throw std::runtime_error("handler"); });
        bool threw = false;
        bool ok = false;
        try { ok = Game::RequestQuit("throwing handler", 1); } catch (...) { threw = true; }
        CHECK(!threw);
        CHECK(ok);
        CHECK(Game::QuitRequested());
        (void)bad;
        Game::Reset();
    }
    (void)pauseSub;
    (void)quitSub;
    TEST_MAIN_RESULT();
}
