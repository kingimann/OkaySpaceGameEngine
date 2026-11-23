// Core wave 6, 1.9 Events: audit fixes and additions. (The suite keeps its old name; the ECS half was removed with
// the unused okay::ecs headers, see docs/engine/core_runtime.md.)
//   - Subscription::Active() reports the truth after a once-only handler ran, after Clear and after Unsubscribe by token.
//   - A handler that throws leaves the bus usable; Flush keeps the events behind the one that threw.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace okay;

struct Ping { int n; };
struct Pong { int n; };

int main() {
    RUN_SUITE("core_events_ecs");

    // --- Subscription::Active tells the truth ---
    {
        EventBus bus; int ran = 0;
        auto once = bus.Listen<Ping>([&](const Ping&) { ++ran; }, 0, /*once=*/true);
        CHECK(once.Active());
        bus.Publish(Ping{1});
        CHECK(ran == 1);
        CHECK(!once.Active());                    // the handler removed itself after its one run

        auto a = bus.Listen<Ping>([&](const Ping&) { ++ran; });
        auto b = bus.Subscribe<Pong>([&](const Pong&) { ++ran; }, EventBus::Scoped{});
        CHECK(a.Active() && b.Active());
        bus.Unsubscribe(a.Token());               // removed by token, not through the handle
        CHECK(!a.Active());
        CHECK(b.Active());
        bus.Clear();
        CHECK(!b.Active());
        bus.Publish(Pong{1});
        CHECK(ran == 1);

        EventBus* heap = new EventBus;
        auto c = heap->Listen<Ping>([&](const Ping&) {});
        CHECK(c.Active());
        delete heap;
        CHECK(!c.Active());                       // the bus is gone
        c.Unsubscribe();                          // and that is harmless
    }

    // --- a handler that throws leaves the bus usable ---
    {
        EventBus bus; int a = 0, b = 0;
        int tok = bus.Subscribe<Ping>([&](const Ping& p) { ++a; if (p.n == 1) throw std::runtime_error("boom"); });
        bus.Subscribe<Ping>([&](const Ping&) { ++b; });
        bool threw = false;
        try { bus.Publish(Ping{1}); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
        CHECK(a == 1 && b == 0);                  // the exception stopped that Publish
        bus.Unsubscribe(tok);
        bus.Publish(Ping{2});
        CHECK(a == 1 && b == 1);
        CHECK(bus.SubscriberCount<Ping>() == 1);
        // Unsubscribing inside a later Publish is deferred as usual, and the bus is not stuck "dispatching".
        int tokB = 0, c = 0;
        tokB = bus.Subscribe<Ping>([&](const Ping&) { ++c; bus.Unsubscribe(tokB); });
        bus.Publish(Ping{3});
        bus.Publish(Ping{4});
        CHECK(c == 1);
    }

    // --- Flush keeps the events behind one whose handler threw ---
    {
        EventBus bus; std::vector<int> seen;
        bus.Subscribe<Ping>([&](const Ping& p) { seen.push_back(p.n); if (p.n == 1) throw std::runtime_error("boom"); });
        bus.Queue(Ping{1}); bus.Queue(Ping{2}); bus.Queue(Ping{3});
        bool threw = false;
        try { bus.Flush(); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
        CHECK(bus.QueuedCount() == 2);            // 2 and 3 wait for the next Flush
        CHECK(bus.Flush() == 2);
        CHECK(seen.size() == 3 && seen[0] == 1 && seen[1] == 2 && seen[2] == 3);

        // Events a handler queued during the failed flush go behind the ones that were already waiting.
        EventBus b2; std::vector<int> order;
        b2.Subscribe<Ping>([&](const Ping& p) {
            order.push_back(p.n);
            if (p.n == 1) { b2.Queue(Ping{9}); throw std::runtime_error("boom"); }
        });
        b2.Queue(Ping{1}); b2.Queue(Ping{2});
        try { b2.Flush(); } catch (...) {}
        b2.Flush();
        CHECK(order.size() == 3 && order[1] == 2 && order[2] == 9);
    }

    TEST_MAIN_RESULT();
}
