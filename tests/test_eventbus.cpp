#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>

using namespace okay;

struct Ping { int n; };
struct Pong { int n; };

int main() {
    RUN_SUITE("eventbus");

    // --- an earlier handler unsubscribes a later one: the later one is skipped ---
    {
        EventBus bus;
        int ranB = 0, tokB = 0;
        bus.Subscribe<Ping>([&](const Ping&) { bus.Unsubscribe<Ping>(tokB); });
        tokB = bus.Subscribe<Ping>([&](const Ping&) { ++ranB; });
        bus.Publish(Ping{1});
        CHECK(ranB == 0);
        bus.Publish(Ping{2});
        CHECK(ranB == 0);
        CHECK(bus.SubscriberCount<Ping>() == 1);
    }

    // --- unsubscribe self ---
    {
        EventBus bus;
        int runs = 0, tok = 0, other = 0;
        tok = bus.Subscribe<Ping>([&](const Ping&) { ++runs; bus.Unsubscribe(tok); });
        bus.Subscribe<Ping>([&](const Ping&) { ++other; });
        bus.Publish(Ping{1});
        bus.Publish(Ping{2});
        CHECK(runs == 1);
        CHECK(other == 2);
        CHECK(bus.SubscriberCount<Ping>() == 1);
    }

    // --- subscribe during publish: the new handler gets the next event only ---
    {
        EventBus bus;
        int late = 0; bool added = false;
        bus.Subscribe<Ping>([&](const Ping&) {
            if (!added) { added = true; bus.Subscribe<Ping>([&](const Ping&) { ++late; }); }
        });
        bus.Publish(Ping{1});
        CHECK(late == 0);
        bus.Publish(Ping{2});
        CHECK(late == 1);
    }

    // --- re-entrant publish (same and other type) ---
    {
        EventBus bus;
        std::string log;
        int tokB = 0;
        bus.Subscribe<Ping>([&](const Ping& p) {
            log += "a" + std::to_string(p.n);
            if (p.n < 2) bus.Publish(Ping{p.n + 1});
            bus.Publish(Pong{p.n});
        });
        tokB = bus.Subscribe<Ping>([&](const Ping& p) { log += "b" + std::to_string(p.n); });
        bus.Subscribe<Pong>([&](const Pong& p) { log += "P" + std::to_string(p.n); });
        bus.Publish(Ping{1});
        CHECK(log == "a1" "a2" "P2" "b2" "P1" "b1");
        // unsubscribing from a nested publish, then the outer loop must skip it
        log.clear();
        EventBus bus2;
        int ranLast = 0, tokLast = 0;
        bus2.Subscribe<Ping>([&](const Ping& p) { if (p.n == 0) bus2.Publish(Ping{1}); else bus2.Unsubscribe(tokLast); });
        tokLast = bus2.Subscribe<Ping>([&](const Ping& p) { if (p.n == 0) ++ranLast; });
        bus2.Publish(Ping{0});
        CHECK(ranLast == 0);
        CHECK(bus2.SubscriberCount<Ping>() == 1);
        (void)tokB;
    }

    // --- token-only Unsubscribe, typed Unsubscribe with the wrong type ---
    {
        EventBus bus;
        int a = 0, b = 0;
        int ta = bus.Subscribe<Ping>([&](const Ping&) { ++a; });
        int tb = bus.Subscribe<Pong>([&](const Pong&) { ++b; });
        bus.Unsubscribe<Ping>(tb);                  // wrong type: ignored
        CHECK(bus.SubscriberCount<Pong>() == 1);
        bus.Unsubscribe(ta);
        bus.Unsubscribe(tb);
        bus.Unsubscribe(tb);                        // twice is harmless
        bus.Unsubscribe(9999);
        bus.Publish(Ping{1}); bus.Publish(Pong{1});
        CHECK(a == 0 && b == 0);
    }

    // --- RAII handle ---
    {
        EventBus bus;
        int n = 0;
        {
            auto sub = bus.Subscribe<Ping>([&](const Ping&) { ++n; }, EventBus::Scoped{});
            CHECK(sub.Active());
            bus.Publish(Ping{1});
            auto moved = std::move(sub);
            CHECK(!sub.Active() && moved.Active());
            bus.Publish(Ping{2});
            CHECK(n == 2);
        }
        bus.Publish(Ping{3});
        CHECK(n == 2);
        CHECK(bus.SubscriberCount<Ping>() == 0);
        EventBus::Subscription s2 = bus.Subscribe<Ping>([&](const Ping&) { ++n; }, EventBus::Scoped{});
        s2.Reset(); s2.Reset();
        bus.Publish(Ping{4});
        CHECK(n == 2);
    }

    // --- Clear during publish ---
    {
        EventBus bus;
        int n = 0;
        bus.Subscribe<Ping>([&](const Ping&) { bus.Clear(); });
        bus.Subscribe<Ping>([&](const Ping&) { ++n; });
        bus.Publish(Ping{1});
        CHECK(n == 0);
        CHECK(bus.SubscriberCount<Ping>() == 0);
    }

    TEST_MAIN_RESULT();
}
