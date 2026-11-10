// Core wave 6, 1.9 Events and ECS: audit fixes and additions.
//   - Subscription::Active() reports the truth after a once-only handler ran, after Clear and after Unsubscribe by token.
//   - A handler that throws leaves the bus usable; Flush keeps the events behind the one that threw.
//   - SystemScheduler: add / remove / clear from inside a running system is safe; remove, has, names.
//   - ecs::World entity ids survive the 32-bit counter wrapping around.
//   - SceneBridge: an object freed behind its back is made again, a non-finite pose or a bad rate cannot throw the
//     object around, and the bridge never reads a freed GameObject.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::ecs;

struct Ping { int n; };
struct Pong { int n; };
struct Pos { float x = 0, y = 0; };

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

    // --- SystemScheduler: changes while it runs ---
    {
        SystemScheduler sch; World w;
        std::string log;
        SystemScheduler* p = &sch;   // a one-pointer capture lives inside the std::function: moving it while it runs used to free it
        sch.add("grow", [p, &log](World&, float) {
            log += "g";
            for (int i = 0; i < 50; ++i) p->add("late" + std::to_string(i), [&log](World&, float) { log += "L"; });
        });
        sch.add("tail", [&log](World&, float) { log += "t"; });
        sch.run(w, 0.0f);
        CHECK(log == "gt");                       // the 50 new systems wait for the next tick
        CHECK(sch.count() == 52);
        log.clear();
        sch.setEnabled("grow", false);
        sch.run(w, 0.0f);
        CHECK(log.size() == 51 && log[0] == 't' && log[1] == 'L' && log.back() == 'L');   // in the order they were added: tail, then the 50
    }
    {
        SystemScheduler sch; World w; std::string log;
        SystemScheduler* p = &sch;
        sch.add("a", [&log](World&, float) { log += "a"; });
        sch.add("killer", [p, &log](World&, float) { log += "k"; p->remove("victim"); p->remove("killer"); });
        sch.add("victim", [&log](World&, float) { log += "v"; });
        sch.add("z", [&log](World&, float) { log += "z"; });
        sch.run(w, 0.0f);
        CHECK(log == "akz");                      // the victim was removed before its turn; the killer removed itself and still finished
        CHECK(!sch.has("killer") && !sch.has("victim"));
        CHECK(sch.count() == 2);
        log.clear();
        sch.run(w, 0.0f);
        CHECK(log == "az");
    }
    {
        SystemScheduler sch; World w; int ran = 0;
        SystemScheduler* p = &sch;
        sch.add("reset", [p, &ran](World&, float) { ++ran; p->clear(); p->add("fresh", [&ran](World&, float) { ran += 100; }); });
        sch.add("never", [&ran](World&, float) { ran += 1000; });
        sch.run(w, 0.0f);
        CHECK(ran == 1);                          // clear() dropped "never" before it ran
        CHECK(sch.count() == 1 && sch.has("fresh"));
        sch.run(w, 0.0f);
        CHECK(ran == 101);
    }
    {
        SystemScheduler sch; World w; int n = 0;
        sch.add("a", [&](World&, float) { ++n; });
        sch.add("b", [&](World&, float) { ++n; });
        sch.add("a", [&](World&, float) { ++n; });
        CHECK(sch.names().size() == 3 && sch.names()[0] == "a" && sch.names()[1] == "b" && sch.names()[2] == "a");
        CHECK(sch.has("a") && sch.has("b") && !sch.has("c"));
        CHECK(sch.remove("a") == 2);              // every system with that name
        CHECK(sch.remove("a") == 0);
        CHECK(sch.count() == 1 && !sch.enabled("a") && sch.enabled("b"));
        sch.run(w, 0.0f);
        CHECK(n == 1);
        sch.clear();
        CHECK(sch.count() == 0);
        // A system running another tick inside itself: nesting must not break the bookkeeping.
        SystemScheduler outer; int depth = 0;
        SystemScheduler* o = &outer;
        outer.add("nest", [&, o](World& ww, float dt) { if (++depth < 3) o->run(ww, dt); o->add("x", [](World&, float) {}); });
        outer.run(w, 0.0f);
        CHECK(depth == 3);
        CHECK(outer.count() == 4);                // "nest" plus the three "x" added at the ends of the ticks
    }

    // --- World: ids keep working across the 32-bit wrap ---
    {
        World w;
        Entity low = w.ensure(1);
        CHECK(low == 1 && w.alive(1));
        CHECK(w.ensure(0xFFFFFFFEu) == 0xFFFFFFFEu);
        CHECK(w.ensure(0xFFFFFFFFu) == Null);                 // still refused
        Entity top = w.create();
        CHECK(top == 0xFFFFFFFFu);
        Entity wrapped = w.create();                          // used to be Null, a dead id that every query mishandles
        CHECK(wrapped != Null);
        CHECK(wrapped != low);                                // skips the ids that are alive
        CHECK(w.alive(wrapped));
        Entity next = w.create();
        CHECK(next != Null && next != wrapped && next != low && w.alive(next));
        w.add<Pos>(wrapped, Pos{3, 4});
        int visited = 0;
        w.each<Pos>([&](Entity e, Pos& p) { ++visited; CHECK(e == wrapped && p.x == 3.0f); });
        CHECK(visited == 1);
        // Reserved ids from the command buffer are not handed out twice either.
        Entity r1 = w.commands().create();
        Entity r2 = w.commands().create();
        CHECK(r1 != r2 && r1 != Null && r2 != Null);
        w.flush();
        CHECK(w.alive(r1) && w.alive(r2));
    }

    // --- World::entities is in ascending order, so snapshots are the same bytes on every platform ---
    {
        World w;
        std::vector<Entity> made;
        for (int i = 0; i < 200; ++i) made.push_back(w.create());
        for (std::size_t i = 0; i < made.size(); i += 3) w.destroy(made[i]);          // holes
        for (int i = 0; i < 50; ++i) w.create();
        w.ensure(5000); w.ensure(17);
        const std::vector<Entity> ids = w.entities();
        CHECK(ids.size() == w.count());
        CHECK(std::is_sorted(ids.begin(), ids.end()));
        CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());                // no id twice

        // Two worlds built in different orders describe the same entities with the same snapshot bytes.
        struct Hp { int v; };
        NetWorld a, b;
        a.replicate<Hp>(1); b.replicate<Hp>(1);
        for (Entity e : {Entity(3), Entity(9), Entity(1), Entity(40), Entity(7)}) { a.world.ensure(e); a.world.add<Hp>(e, Hp{(int)e}); }
        for (Entity e : {Entity(40), Entity(7), Entity(1), Entity(9), Entity(3)}) { b.world.ensure(e); b.world.add<Hp>(e, Hp{(int)e}); }
        const std::vector<std::uint8_t> sa = a.snapshot(), sb = b.snapshot();
        // The entity list (the first block) is identical; the component blocks follow each pool's own order.
        CHECK(sa.size() == sb.size());
        CHECK(std::equal(sa.begin(), sa.begin() + 4 + 5 * 4, sb.begin()));
    }

    // --- SceneBridge ---
    {
        Scene scene("bridge"); scene.physicsEnabled = false;
        World w;
        int made = 0;
        SceneBridge bridge(w, scene, [&](Scene& s, Entity) { ++made; return s.CreateGameObject("Unit"); });
        Entity e = w.create();
        w.add<EcsTransform>(e, EcsTransform{Vec3(1, 2, 3)});
        bridge.sync();
        GameObject* go = bridge.gameObjectFor(e);
        CHECK(go && made == 1);
        CHECK(std::fabs(go->transform->localPosition.x - 1.0f) < 1e-5f);

        // Somebody else destroys the object: the bridge must not touch the freed memory, and (by default) makes it again.
        scene.Destroy(go);
        scene.Update(0.016f);                                 // the delayed destroy happens here
        CHECK(bridge.gameObjectFor(e) == nullptr);
        w.get<EcsTransform>(e)->position = Vec3(7, 8, 9);
        bridge.sync();
        GameObject* again = bridge.gameObjectFor(e);
        CHECK(again != nullptr && made == 2);
        CHECK(std::fabs(again->transform->localPosition.x - 7.0f) < 1e-5f);   // a new object snaps to the entity
        CHECK(bridge.mappedCount() == 1);

        // The same with the whole scene cleared (a scene reload).
        scene.Clear();
        bridge.sync();
        CHECK(bridge.gameObjectFor(e) != nullptr && made == 3);

        // Off: the entity stays without an object, and nothing is read from the freed one.
        bridge.respawnLost = false;
        scene.Destroy(bridge.gameObjectFor(e));
        scene.Update(0.016f);
        bridge.sync();
        bridge.sync();
        CHECK(bridge.gameObjectFor(e) == nullptr && made == 3);
        bridge.respawnLost = true;
        bridge.sync();
        CHECK(bridge.gameObjectFor(e) != nullptr && made == 4);

        // A damaged snapshot: the pose is not finite, the object keeps its last good one.
        GameObject* g = bridge.gameObjectFor(e);
        w.get<EcsTransform>(e)->position = Vec3(std::numeric_limits<float>::quiet_NaN(), 0, 0);
        bridge.sync();
        CHECK(std::isfinite(g->transform->localPosition.x) && std::fabs(g->transform->localPosition.x - 7.0f) < 1e-5f);
        w.get<EcsTransform>(e)->position = Vec3(1, 1, 1);
        w.get<EcsTransform>(e)->scale = Vec3(1, std::numeric_limits<float>::infinity(), 1);
        bridge.sync();
        CHECK(std::isfinite(g->transform->localScale.y) && g->transform->localScale.y == 1.0f);

        // A bad rate (negative, NaN) snaps and zero stands still; none of them overshoots.
        w.get<EcsTransform>(e)->scale = Vec3(1, 1, 1);
        w.get<EcsTransform>(e)->position = Vec3(10, 0, 0);
        bridge.interpolate = true;
        for (float rate : {-12.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()}) {
            g->transform->localPosition = Vec3(0, 0, 0);
            bridge.interpRate = rate;
            bridge.sync(1.0f / 60.0f);
            CHECK(std::isfinite(g->transform->localPosition.x));
            CHECK(g->transform->localPosition.x >= 0.0f && g->transform->localPosition.x <= 10.0f + 1e-4f);
        }
    }

    TEST_MAIN_RESULT();
}
