// Scheduler edge cases: NaN / infinite / negative times, huge steps, callbacks that
// throw, very long timers. Each block has an index so `test_scheduler_hardening N`
// runs just that block (a hang then names its culprit).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace okay;

static int g_only = 0;
#define BLOCK(n) if (g_only == 0 || g_only == (n))

int main(int argc, char** argv) {
    if (argc > 1) g_only = std::atoi(argv[1]);
    RUN_SUITE("scheduler_hardening");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    BLOCK(1) {   // A NaN delay, interval or duration must not leave a task that can never finish.
        Scheduler s;
        int a = 0, b = 0, c = 0, lastBad = 0;
        s.Invoke(nan, [&] { ++a; });
        s.InvokeRepeating(nan, [&] { ++b; }, 2);
        s.Tween(nan, [&](float t) { ++c; if (!std::isfinite(t)) ++lastBad; });
        s.Update(0.5f);
        CHECK(a == 1);
        CHECK(b == 2);
        CHECK(c >= 1 && lastBad == 0);
        CHECK(s.ActiveCount() == 0);
        // SetInterval with NaN is just as harmless.
        int n = 0;
        auto h = s.Invoke(5.0f, [&] { ++n; });
        CHECK(s.SetInterval(h, nan));
        s.Update(0.1f);
        CHECK(n == 1);
    }

    BLOCK(2) {   // A NaN, infinite or negative frame time must not poison every task.
        Scheduler s;
        int n = 0;
        s.InvokeRepeating(1.0f, [&] { ++n; });
        float tweenT = -1.0f;
        s.Tween(10.0f, [&](float t) { tweenT = t; });
        s.Update(nan);
        s.Update(-5.0f);
        s.Update(1.0f);
        CHECK(n == 1);
        CHECK(std::isfinite(tweenT) && tweenT >= 0.0f && tweenT <= 0.11f);
        // Two-argument form too.
        s.Update(nan, nan);
        s.Update(inf, inf);
        s.Update(0.5f, 0.5f);
        s.Update(0.5f, 0.5f);
        CHECK(n == 2);
    }

    BLOCK(3) {   // An infinite step is skipped rather than firing everything.
        Scheduler s;
        int n = 0;
        s.InvokeRepeating(1.0f, [&] { ++n; });
        s.Update(inf);
        CHECK(n == 0);
        s.Update(1.0f);
        CHECK(n == 1);
    }

    BLOCK(4) {   // A hitch (or a suspended laptop) must not run a fast timer millions of times.
        Scheduler s;
        long n = 0;
        s.InvokeRepeating(0.001f, [&] { ++n; });
        s.Update(1.0e30f);
        CHECK(n > 0 && n <= 4096);              // Scheduler::kMaxCatchUp
        const long before = n;
        s.Update(0.0105f);                            // the timer keeps its rhythm afterwards
        CHECK(n - before >= 9 && n - before <= 11);

        // A normal catch-up still delivers every missed run.
        Scheduler s2;
        int m = 0;
        s2.InvokeRepeating(0.1f, [&] { ++m; });
        s2.Update(0.55f);
        CHECK(m == 5);
        // A finite-count timer ends after its runs, whatever the step.
        Scheduler s3;
        int k = 0;
        s3.InvokeRepeating(0.001f, [&] { ++k; }, 3);
        s3.Update(3.0e38f);
        CHECK(k == 3 && s3.ActiveCount() == 0);
    }

    BLOCK(5) {   // A callback that throws leaves the scheduler consistent.
        Scheduler s;
        int ran = 0, rep = 0, after = 0;
        auto tracker = std::make_shared<int>(0);
        s.Invoke(0.1f, [] { throw std::runtime_error("boom"); });
        s.Invoke(0.1f, [&] { ++ran; });
        s.InvokeRepeating(0.1f, [&] { ++rep; });
        bool threw = false;
        try { s.Update(0.1f); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
        CHECK(ran == 1);                              // the tasks after it still ran this update
        CHECK(rep == 1);
        CHECK(s.ActiveCount() == 1);                  // only the repeating timer is left
        // Cancel releases a task at once again (the update bracket was closed).
        auto h = s.Invoke(5.0f, [tracker] { (void)tracker; });
        CHECK(tracker.use_count() == 2);
        CHECK(s.Cancel(h));
        CHECK(tracker.use_count() == 1);
        // ... and the rest keeps working.
        s.Update(0.1f);
        CHECK(rep == 2);

        // A repeating task that throws is cancelled (it would throw every frame).
        Scheduler r;
        int calls = 0;
        r.InvokeRepeating(0.1f, [&] { ++calls; throw std::logic_error("again"); });
        try { r.Update(0.1f); } catch (const std::logic_error&) {}
        CHECK(calls == 1 && r.ActiveCount() == 0);
        r.Update(1.0f);
        CHECK(calls == 1);

        // Tween update and completion callbacks too.
        Scheduler t;
        int ups = 0;
        t.Tween(1.0f, [&](float) { ++ups; throw std::runtime_error("tw"); });
        try { t.Update(0.1f); } catch (const std::runtime_error&) {}
        CHECK(ups == 1 && t.ActiveCount() == 0);
        Scheduler u;
        u.Tween(0.1f, [](float) {}, [] { throw std::runtime_error("done"); });
        bool threwDone = false;
        try { u.Update(1.0f); } catch (const std::runtime_error&) { threwDone = true; }
        CHECK(threwDone && u.ActiveCount() == 0);
        (void)after;
    }

    BLOCK(6) {   // A long timer fires on time (the elapsed time must not lose precision).
        Scheduler s;
        bool fired = false;
        s.Invoke(36000.0f, [&] { fired = true; });    // ten hours
        const float dt = 1.0f / 60.0f;
        long frames = 0;
        while (!fired && frames < 3000000) { s.Update(dt, dt); ++frames; }
        CHECK(fired);
        CHECK(std::labs(frames - 2160000L) <= 2);
    }

    BLOCK(7) {   // Remaining reports the long wait in full too.
        Scheduler s;
        auto h = s.Invoke(100000.0f, [] {});
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f, 1.0f / 60.0f);
        CHECK_NEAR(s.Remaining(h), 99999.0f, 1.0f);
    }

    BLOCK(8) {   // Random schedule / cancel / clear / throw from inside callbacks: no task ever runs after it was cancelled.
        unsigned rng = 20240607u;
        auto next = [&] { rng = rng * 1664525u + 1013904223u; return (int)(rng >> 8); };
        long totalFired = 0, totalCancelled = 0;
        for (int round = 0; round < 300; ++round) {
            struct Slot { Scheduler::Handle h = 0; int tag = 0; int kind = 0; int fired = 0; int maxFires = 1; bool cancelled = false; bool done = false; };
            std::vector<Slot> slots;
            Scheduler s;
            int violation = 0;
            std::function<void()> spawn;
            auto cancelSlot = [&](int i) { if (!slots[(std::size_t)i].cancelled && !slots[(std::size_t)i].done) { slots[(std::size_t)i].cancelled = true; ++totalCancelled; } };
            auto act = [&](int self) {
                Slot& me = slots[(std::size_t)self];
                if (me.cancelled) violation = 1;                       // ran after a successful cancel
                ++me.fired; ++totalFired;
                if (me.fired >= me.maxFires) me.done = true;
                const int op = next() % 9;
                const int other = slots.empty() ? 0 : next() % (int)slots.size();
                if (op == 0 && !slots.empty()) { const bool existed = s.Cancel(slots[(std::size_t)other].h); if (other != self && existed != (!slots[(std::size_t)other].cancelled && !slots[(std::size_t)other].done)) violation = 2; cancelSlot(other); }
                else if (op == 1) {
                    const int tag = next() % 4;
                    s.CancelByTag("t" + std::to_string(tag));
                    for (int i = 0; i < (int)slots.size(); ++i) if (slots[(std::size_t)i].tag == tag) cancelSlot(i);
                }
                else if (op == 2) { for (int i = 0; i < (int)slots.size(); ++i) cancelSlot(i); s.Clear(); }
                else if (op == 3 || op == 4) spawn();
                else if (op == 5) { me.done = true; throw std::runtime_error("fuzz"); }
            };
            spawn = [&] {
                if (slots.size() > 400) return;
                const int id = (int)slots.size();
                Slot sl; sl.tag = next() % 4; sl.kind = next() % 3;
                slots.push_back(sl);
                Scheduler::Options o; o.tag = "t" + std::to_string(slots[(std::size_t)id].tag);
                const float delay = (float)(next() % 50) * 0.01f;
                if (slots[(std::size_t)id].kind == 0) slots[(std::size_t)id].h = s.Invoke(delay, [&, id] { act(id); }, o);
                else if (slots[(std::size_t)id].kind == 1) { slots[(std::size_t)id].maxFires = 3; slots[(std::size_t)id].h = s.InvokeRepeating(0.02f + delay, [&, id] { act(id); }, 3, o); }
                else slots[(std::size_t)id].h = s.Tween(0.05f + delay, [](float) {}, [&, id] { act(id); }, o);
            };
            for (int i = 0; i < 20; ++i) spawn();
            for (int step = 0; step < 60 && !violation; ++step) {
                try { s.Update(0.02f + (float)(next() % 4) * 0.01f); } catch (const std::runtime_error&) {}
            }
            for (int tries = 0; tries < 400 && s.ActiveCount() > 0; ++tries) {
                try { s.Update(1.0f); } catch (const std::runtime_error&) {}
            }
            if (violation) std::cerr << "  round " << round << " violation " << violation << "\n";
            CHECK(violation == 0);
            CHECK(s.ActiveCount() == 0);
            for (const Slot& sl : slots) if (sl.fired > sl.maxFires) violation = 3;
            CHECK(violation == 0);
        }
        CHECK(totalFired > 1000 && totalCancelled > 100);   // the run really exercised both paths
    }

    TEST_MAIN_RESULT();
}
