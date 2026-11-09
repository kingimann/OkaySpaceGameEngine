// Scheduler at scale: every lookup by handle, owner, tag and object is O(1) or O(group), so a
// hundred thousand timers can be scheduled, paused and cancelled one by one without the
// quadratic cost a linear scan (and a vector erase per cancel) used to have. The ordering and
// re-entrancy rules must survive the change: tasks run in the order they were scheduled, a
// callback may cancel / schedule / clear, and what a task captured is freed on time.
// Block N runs alone with `test_scheduler_scale N`.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace okay;

static int g_only = 0;
#define BLOCK(n) if (g_only == 0 || g_only == (n))

namespace {
// A value that runs a function when it dies (a captured RAII handle that cancels in its destructor).
struct OnDeath {
    std::shared_ptr<std::function<void()>> fn;
    explicit OnDeath(std::function<void()> f) : fn(std::make_shared<std::function<void()>>(std::move(f))) {}
    OnDeath(const OnDeath&) = default;
    ~OnDeath() { if (fn && fn.use_count() == 1 && *fn) (*fn)(); }
};
}

int main(int argc, char** argv) {
    if (argc > 1) g_only = std::atoi(argv[1]);
    RUN_SUITE("scheduler_scale");
    constexpr int N = 100000;

    BLOCK(1) {   // 100k timers cancelled one by one by handle (front to back, the worst case for an erase)
        Scheduler s;
        std::vector<Scheduler::Handle> h;
        h.reserve(N);
        int fired = 0;
        for (int i = 0; i < N; ++i) h.push_back(s.Invoke(10.0f + (float)(i % 7), [&] { ++fired; }));
        CHECK(s.ActiveCount() == (std::size_t)N);
        for (int i = 0; i < N; ++i) {
            if (!s.Cancel(h[(std::size_t)i])) { CHECK(false); break; }
        }
        CHECK(s.ActiveCount() == 0);
        CHECK(!s.Cancel(h[0]) && !s.IsScheduled(h[N - 1]));
        s.Update(100.0f);
        CHECK(fired == 0);
    }

    BLOCK(2) {   // 100k handle lookups of every kind
        Scheduler s;
        std::vector<Scheduler::Handle> h;
        for (int i = 0; i < N; ++i) h.push_back(s.Invoke(5.0f, [] {}));
        long sum = 0;
        for (int i = 0; i < N; ++i) {
            const Scheduler::Handle x = h[(std::size_t)((i * 7919) % N)];     // scattered, not in order
            if (s.IsScheduled(x)) ++sum;
            if (s.Remaining(x) > 4.0f) ++sum;
            s.SetPaused(x, true);
            if (s.IsPaused(x)) ++sum;
            s.SetPaused(x, false);
            s.SetInterval(x, 6.0f);
        }
        CHECK(sum == 3L * N);
        s.Update(5.5f);
        CHECK(s.ActiveCount() == (std::size_t)N);       // all were stretched to 6 s
        s.Update(1.0f);
        CHECK(s.ActiveCount() == 0);
    }

    BLOCK(3) {   // a different owner for every task, cancelled owner by owner (an object's death does this)
        Scheduler s;
        std::vector<int> owners((std::size_t)N);
        for (int i = 0; i < N; ++i) {
            Scheduler::Options o; o.owner = &owners[(std::size_t)i];
            s.Invoke(3.0f, [] {}, o);
            s.InvokeRepeating(1.0f, [] {}, -1, o);       // two tasks per owner
        }
        CHECK(s.ActiveCount() == 2u * N);
        int removed = 0;
        for (int i = 0; i < N; ++i) removed += s.CancelByOwner(&owners[(std::size_t)i]);
        CHECK(removed == 2 * N);
        CHECK(s.ActiveCount() == 0);
        CHECK(s.CancelByOwner(&owners[0]) == 0);
    }

    BLOCK(4) {   // a different tag for every task, cancelled and counted tag by tag
        Scheduler s;
        for (int i = 0; i < N / 2; ++i) {
            Scheduler::Options o; o.tag = "t" + std::to_string(i);
            s.Invoke(3.0f, [] {}, o);
        }
        CHECK(s.CountByTag("t0") == 1 && s.CountByTag("t49999") == 1 && s.CountByTag("t50000") == 0);
        int removed = 0;
        for (int i = 0; i < N / 2; ++i) removed += s.CancelByTag("t" + std::to_string(i));
        CHECK(removed == N / 2 && s.ActiveCount() == 0);
    }

    BLOCK(5) {   // one big group: cancel it by tag, by owner, by object, and pause it, in one go
        Scene scene;
        GameObject* go = scene.CreateGameObject("Boss");
        Scheduler s;
        const int owner = 0;
        for (int i = 0; i < N; ++i) {
            Scheduler::Options o;
            o.tag = (i % 2) ? "odd" : "even";
            o.owner = (i % 3 == 0) ? &owner : nullptr;
            if (i % 5 == 0) o.object = go;
            s.Invoke(2.0f, [] {}, o);
        }
        CHECK(s.CountByTag("odd") == (std::size_t)N / 2);
        CHECK(s.SetPausedByTag("odd", true) == N / 2);
        s.Update(3.0f);
        CHECK(s.ActiveCount() == (std::size_t)N / 2);   // the odd ones were paused and are still waiting
        CHECK(s.SetPausedByTag("odd", false) == N / 2);
        CHECK(s.CancelByObject(go) == N / 5 - N / 10);  // multiples of 5 that are still alive: the odd ones (even ones fired)
        CHECK(s.CancelByOwner(&owner) > 0);
        CHECK(s.CancelByTag("odd") > 0);
        CHECK(s.ActiveCount() == 0);
    }

    BLOCK(6) {   // an owner that keeps scheduling short timers does not make a group that grows for ever, and counts stay right
        Scheduler s;
        const int owner = 0;
        int fired = 0;
        for (int round = 0; round < 2000; ++round) {
            Scheduler::Options o; o.owner = &owner; o.tag = "spam";
            for (int k = 0; k < 20; ++k) s.Invoke(0.1f, [&] { ++fired; }, o);
            s.Update(0.1f);
        }
        CHECK(fired == 2000 * 20);
        CHECK(s.ActiveCount() == 0 && s.CountByTag("spam") == 0);
        Scheduler::Options o; o.owner = &owner; o.tag = "spam";
        s.Invoke(5.0f, [] {}, o); s.Invoke(5.0f, [] {}, o);
        CHECK(s.CountByTag("spam") == 2 && s.CancelByOwner(&owner) == 2 && s.CountByTag("spam") == 0);
    }

    BLOCK(7) {   // order: tasks run in the order they were scheduled, also after cancels and a compaction
        Scheduler s;
        std::vector<int> ran;
        std::vector<Scheduler::Handle> h;
        for (int i = 0; i < 300; ++i) h.push_back(s.Invoke(1.0f, [&ran, i] { ran.push_back(i); }));
        for (int i = 0; i < 300; ++i) if (i % 3 != 1) s.Cancel(h[(std::size_t)i]);   // two thirds gone: enough tombstones to compact
        for (int i = 300; i < 330; ++i) h.push_back(s.Invoke(1.0f, [&ran, i] { ran.push_back(i); }));
        CHECK(s.ActiveCount() == 100u + 30u);
        for (int i = 1; i < 300; i += 3) CHECK(s.IsScheduled(h[(std::size_t)i]) && s.Remaining(h[(std::size_t)i]) > 0.99f);
        CHECK(!s.IsScheduled(h[0]) && !s.IsScheduled(h[2]));
        s.Update(1.0f);
        std::vector<int> want;
        for (int i = 1; i < 300; i += 3) want.push_back(i);
        for (int i = 300; i < 330; ++i) want.push_back(i);
        CHECK(ran == want);
        CHECK(s.ActiveCount() == 0);
        const auto tasks = s.Tasks();
        CHECK(tasks.empty());
    }

    BLOCK(8) {   // Tasks() and Dump() list live tasks in scheduling order even with tombstones in between
        Scheduler s;
        std::vector<Scheduler::Handle> h;
        for (int i = 0; i < 10; ++i) h.push_back(s.Invoke(5.0f, [] {}));
        s.Cancel(h[2]); s.Cancel(h[3]); s.Cancel(h[7]);
        const auto list = s.Tasks();
        CHECK(list.size() == 7);
        Scheduler::Handle last = 0;
        bool ascending = true;
        for (const auto& t : list) { ascending = ascending && t.handle > last; last = t.handle; }
        CHECK(ascending);
        CHECK(s.Dump().find("Scheduler: 7 tasks") == 0);
    }

    BLOCK(9) {   // a callback cancels later tasks (by handle, tag, owner) and schedules more: nothing cancelled runs, new ones run in the same update
        Scheduler s;
        std::vector<int> ran;
        const int owner = 0;
        Scheduler::Options sib; sib.tag = "sib"; sib.owner = &owner;
        std::vector<Scheduler::Handle> h;
        s.Invoke(1.0f, [&] {                                   // scheduled first, so it runs first
            ran.push_back(1);
            CHECK(s.Cancel(h[1]));
            CHECK(!s.IsScheduled(h[1]) && !s.Cancel(h[1]));
            CHECK(s.CountByTag("sib") == 5);
            CHECK(s.CancelByTag("sib") == 5);                  // h[1] is already gone
            CHECK(s.CountByTag("sib") == 0);
            CHECK(s.ActiveCount() == 1);                       // only this running task
            s.Invoke(0.0f, [&ran] { ran.push_back(2); });
            Scheduler::Options again; again.tag = "sib";
            s.Invoke(0.0f, [&ran] { ran.push_back(3); }, again);
            CHECK(s.CountByTag("sib") == 1);
        });
        for (int i = 0; i < 6; ++i) h.push_back(s.Invoke(1.0f, [&ran, i] { ran.push_back(100 + i); }, sib));
        s.Update(1.0f);
        CHECK(ran == (std::vector<int>{1, 2, 3}));
        CHECK(s.ActiveCount() == 0 && s.CountByTag("sib") == 0);
        CHECK(!s.IsScheduled(h[0]) && !s.IsScheduled(h[5]));
    }

    BLOCK(10) {   // the same, with the canceller first: the siblings never run
        Scheduler s;
        std::vector<int> ran;
        const int owner = 0;
        Scheduler::Options sib; sib.tag = "sib"; sib.owner = &owner;
        s.Invoke(1.0f, [&] { ran.push_back(1); CHECK(s.CancelByOwner(&owner) == 6); });
        for (int i = 0; i < 6; ++i) s.Invoke(1.0f, [&ran, i] { ran.push_back(100 + i); }, sib);
        s.Update(1.0f);
        CHECK(ran == std::vector<int>{1});
        CHECK(s.ActiveCount() == 0);
    }

    BLOCK(11) {  // Clear() inside a callback: the rest of the update runs nothing, the scheduler is usable at once
        Scheduler s;
        int a = 0, b = 0, after = 0;
        s.Invoke(1.0f, [&] { ++a; s.Clear(); s.Invoke(0.5f, [&] { ++after; }); });
        for (int i = 0; i < 50; ++i) s.Invoke(1.0f, [&] { ++b; });
        s.Update(1.0f);
        CHECK(a == 1 && b == 0);
        CHECK(after == 1);                                    // scheduled by the callback after Clear, due in the same update
        CHECK(s.ActiveCount() == 0);
        s.Invoke(0.25f, [&] { ++after; });
        s.Update(0.25f);
        CHECK(after == 2);
    }

    BLOCK(12) {  // what a task captured is freed when the task goes: cancelled outside an update, cancelled inside one, finished, cleared
        Scheduler s;
        auto token = std::make_shared<int>(1);
        std::weak_ptr<int> w = token;
        auto h1 = s.Invoke(5.0f, [token] {});
        token.reset();
        CHECK(!w.expired());
        s.Cancel(h1);
        CHECK(w.expired());                                   // freed by Cancel itself, as the old erase did

        token = std::make_shared<int>(2); w = token;
        s.Invoke(1.0f, [token] {});
        token.reset();
        s.Update(1.0f);
        CHECK(w.expired());                                   // a one-shot that ran lets go of its captures

        token = std::make_shared<int>(3); w = token;
        auto h3 = s.InvokeRepeating(1.0f, [token] {}, -1);
        token.reset();
        s.Invoke(1.0f, [&] { s.Cancel(h3); });                // cancelled from inside the next update
        s.Update(0.5f);
        CHECK(!w.expired());
        s.Update(0.5f);
        CHECK(w.expired());

        token = std::make_shared<int>(4); w = token;
        s.Invoke(5.0f, [token] {}); s.InvokeRepeating(1.0f, [token] {}, -1);
        token.reset();
        s.Clear();
        CHECK(w.expired());
    }

    BLOCK(13) {  // a captured value whose destructor calls back into the scheduler (cancel / schedule) while the task is released
        Scheduler s;
        int late = 0;
        Scheduler::Handle victim = s.Invoke(9.0f, [] {});
        {
            auto g = std::make_shared<OnDeath>([&] { s.Cancel(victim); s.Invoke(0.5f, [&] { ++late; }); });
            auto h = s.Invoke(5.0f, [g] {});
            g.reset();
            CHECK(s.IsScheduled(victim));
            s.Cancel(h);                                      // its capture dies inside Cancel and cancels `victim`, schedules `late`
        }
        CHECK(!s.IsScheduled(victim));
        CHECK(s.ActiveCount() == 1);
        s.Update(0.5f);
        CHECK(late == 1);

        // The same from Clear() and from the end of an update.
        Scheduler::Handle v2 = s.Invoke(9.0f, [] {});
        auto g2 = std::make_shared<OnDeath>([&] { s.Cancel(v2); s.Invoke(0.25f, [&] { ++late; }); });
        s.Invoke(5.0f, [g2] {});
        g2.reset();
        s.Clear();
        CHECK(!s.IsScheduled(v2) && s.ActiveCount() == 1);
        s.Update(0.25f);
        CHECK(late == 2);

        Scheduler::Handle v3 = s.Invoke(9.0f, [] {});
        auto g3 = std::make_shared<OnDeath>([&] { s.Cancel(v3); s.Invoke(0.25f, [&] { ++late; }); });
        s.Invoke(1.0f, [g3] {});
        g3.reset();
        s.Update(1.0f);                                       // the one-shot finishes; its capture dies at the end of the update
        CHECK(!s.IsScheduled(v3));
        s.Update(0.25f);
        CHECK(late == 3);
    }

    BLOCK(14) {  // a scheduler that is destroyed with tasks that call back from their captures
        int touched = 0;
        {
            Scheduler s;
            auto g = std::make_shared<OnDeath>([&] { s.Cancel(1); s.Invoke(1.0f, [] {}); ++touched; });
            s.Invoke(5.0f, [g] {});
            g.reset();
        }                                                     // ~Scheduler runs the capture's destructor against a consistent, empty scheduler
        CHECK(touched == 1);
    }

    BLOCK(15) {  // an object destroyed from inside a callback: its tasks finish this frame (it lives until the end of it), then every one is dropped without running, also across a compaction
        Scene scene;
        GameObject* a = scene.CreateGameObject("A");
        Scheduler& s = scene.scheduler();
        int ranA = 0, ranOther = 0;
        Scheduler::Options oa; oa.object = a;
        s.Invoke(1.0f, [&] { scene.Destroy(a); });
        for (int i = 0; i < 40; ++i) s.InvokeRepeating(1.0f, [&] { ++ranA; }, -1, oa);
        s.Invoke(1.0f, [&] { ++ranOther; });
        scene.Update(1.0f);
        CHECK(ranA == 40 && ranOther == 1);
        scene.Update(1.0f);                                    // `a` is gone: all 40 go without running
        CHECK(ranA == 40);
        CHECK(s.ActiveCount() == 0);
    }

    BLOCK(16) {  // PurgeDeadOwners and alive predicates on a big list, and a predicate that schedules from inside the check
        Scheduler s;
        auto token = std::make_shared<int>(1);
        for (int i = 0; i < 5000; ++i) {
            Scheduler::Options o; o.alive = [w = std::weak_ptr<int>(token)] { return !w.expired(); };
            s.Invoke(5.0f, [] {}, o);
        }
        for (int i = 0; i < 100; ++i) s.Invoke(5.0f, [] {});
        token.reset();
        CHECK(s.ActiveCount() == 5100);
        CHECK(s.PurgeDeadOwners() == 5000);
        CHECK(s.ActiveCount() == 100);

        int spawned = 0;
        Scheduler::Options o;
        o.alive = [&] { s.Invoke(9.0f, [] {}); ++spawned; return true; };   // grows the list while it is being asked
        s.Invoke(0.1f, [] {}, o);
        s.Update(0.1f);
        CHECK(spawned >= 1);
        CHECK(s.ActiveCount() == 100 + (std::size_t)spawned);
    }

    BLOCK(17) {  // frames of churn: one-shots finish every frame next to a big steady list; counts and timing stay exact
        Scheduler s;
        int steady = 0, burst = 0;
        for (int i = 0; i < 5000; ++i) s.InvokeRepeating(1.0f, [&] { ++steady; });
        for (int frame = 1; frame <= 101; ++frame) {          // 101: a hundred steps of 0.01f are a hair short of a second
            for (int k = 0; k < 50; ++k) s.Invoke(0.01f, [&] { ++burst; });
            s.Update(0.01f);
            CHECK(s.ActiveCount() == 5000);
        }
        CHECK(burst == 101 * 50);
        CHECK(steady == 5000);                                // each repeater fired once in a second
    }

    TEST_MAIN_RESULT();
}
