// The editor's game fiber: the visual-script debugger runs the game tick on it so a
// breakpoint can freeze the tick mid-way and the editor can carry on drawing.
#include "test_framework.hpp"
#include "GameFiber.hpp"
#include <stdexcept>
#include <string>

using okay::editor::GameFiber;

static GameFiber g_fiber;
static std::string g_trace;

static int Deep(int n) {   // a few frames deep, then freeze in the middle
    if (n == 0) { g_trace += "y"; g_fiber.Yield(); g_trace += "r"; return 1; }
    return Deep(n - 1) + 1;
}

int main() {
    RUN_SUITE("game_fiber");

    // A tick that yields twice: the caller gets control back each time, the tick's
    // locals and call stack survive, and it finishes on the third resume.
    {
        int local = 0;
        g_fiber.Run([&] {
            local = 1; g_trace += "a";
            g_fiber.Yield();
            local = 2; g_trace += "b";
            local += Deep(50);
            g_trace += "c";
        });
        CHECK(g_trace == "a" && g_fiber.Suspended() && !g_fiber.InFiber() && local == 1);
        g_trace += "|";
        g_fiber.Resume();
        CHECK(g_trace == "a|by" && g_fiber.Suspended() && local == 2);
        g_trace += "|";
        g_fiber.Resume();
        CHECK(g_trace == "a|by|rc" && !g_fiber.Suspended() && local == 53);
        g_fiber.Resume();                       // nothing suspended: no-op
        CHECK(g_trace == "a|by|rc");
    }
    // Reused for the next tick; a tick that doesn't yield simply runs to the end.
    {
        int ran = 0;
        g_fiber.Run([&] { ++ran; CHECK(g_fiber.InFiber()); });
        CHECK(ran == 1 && !g_fiber.Suspended());
        for (int i = 0; i < 1000; ++i) g_fiber.Run([&] { ++ran; });
        CHECK(ran == 1001);
    }
    // An exception inside the tick stays inside the fiber (nothing to unwind into).
    {
        g_fiber.Run([] { throw std::runtime_error("boom"); });
        CHECK(!g_fiber.Suspended());
        bool after = false;
        g_fiber.Run([&] { after = true; });
        CHECK(after);
    }
    // Yield outside the fiber does nothing.
    g_fiber.Yield();
    CHECK(!g_fiber.Suspended());

    TEST_MAIN_RESULT();
}
