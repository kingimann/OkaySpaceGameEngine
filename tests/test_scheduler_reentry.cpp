// Scheduler::Update must stay valid when a callback schedules (Invoke) or
// cancels tasks: the old loop held a Task& across the callback, so a push_back
// that reallocated the task list (or a Cancel/Clear that erased it) left the
// running callback and the loop pointing at freed memory.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("scheduler_reentry");

    // --- Invoke from a callback reallocates the list; the running callback's
    //     captures (heap string, beyond SBO) must still be alive afterwards. ---
    {
        Scheduler s;
        int spawned = 0;
        std::string seen;
        const std::string tag(64, 'x');
        s.Invoke(0.0f, [&s, &spawned, &seen, tag]() {
            for (int i = 0; i < 64; ++i) s.Invoke(1.0f, [&spawned] { ++spawned; });
            seen = tag;                                   // touches the closure after the adds
        });
        s.Update(0.1f);
        CHECK(seen == tag);
        CHECK(s.ActiveCount() == 64);
        s.Update(1.0f);
        CHECK(spawned == 64);
        CHECK(s.ActiveCount() == 0);
    }

    // --- A task added from a callback with no delay still runs in the same Update. ---
    {
        Scheduler s;
        int inner = 0;
        s.Invoke(0.0f, [&] { s.Invoke(0.0f, [&] { ++inner; }); });
        s.Update(0.016f);
        CHECK(inner == 1);
    }

    // --- A repeating task that cancels itself fires exactly once, and the
    //     task behind it in the list is unaffected. ---
    {
        Scheduler s;
        int a = 0, b = 0;
        const std::string tag(64, 'y');
        std::string seen;
        Scheduler::Handle ha = 0;
        ha = s.InvokeRepeating(0.1f, [&, tag] { ++a; s.Cancel(ha); seen = tag; }, 5);
        s.InvokeRepeating(0.1f, [&] { ++b; }, 3);
        s.Update(0.35f);                                   // would fire 3x each
        CHECK(a == 1);
        CHECK(b == 3);
        CHECK(seen == tag);
        CHECK(s.ActiveCount() == 0);
    }

    // --- Cancelling another (earlier) task from a callback doesn't skip or
    //     double-run the rest. ---
    {
        Scheduler s;
        int a = 0, b = 0, c = 0;
        Scheduler::Handle ha = s.Invoke(5.0f, [&] { ++a; });
        s.Invoke(0.0f, [&] { ++b; s.Cancel(ha); });
        s.Invoke(0.0f, [&] { ++c; });
        CHECK(!s.Cancel(Scheduler::InvalidHandle));
        s.Update(0.1f);
        CHECK(a == 0);
        CHECK(b == 1);
        CHECK(c == 1);
        CHECK(s.ActiveCount() == 0);
        CHECK(!s.Cancel(ha));
    }

    // --- Clear from inside a callback. ---
    {
        Scheduler s;
        int after = 0;
        const std::string tag(64, 'z');
        std::string seen;
        s.Invoke(0.0f, [&, tag] { s.Clear(); seen = tag; });
        s.Invoke(0.0f, [&] { ++after; });
        s.Update(0.1f);
        CHECK(seen == tag);
        CHECK(after == 0);
        CHECK(s.ActiveCount() == 0);
    }

    // --- Tween that schedules work in its update and completion. ---
    {
        Scheduler s;
        int fired = 0;
        s.Tween(0.1f, [&](float) { for (int i = 0; i < 16; ++i) s.Invoke(10.0f, [&] { ++fired; }); },
                [&] { for (int i = 0; i < 16; ++i) s.Invoke(10.0f, [&] { ++fired; }); });
        s.Update(0.2f);
        CHECK(s.ActiveCount() == 32);
        s.Update(10.0f);
        CHECK(fired == 32);
    }

    TEST_MAIN_RESULT();
}
