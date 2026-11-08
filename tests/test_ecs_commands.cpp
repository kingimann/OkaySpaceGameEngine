#include "test_framework.hpp"
#include <Okay.hpp>
#include <set>
#include <chrono>
#include <vector>

using namespace okay;
using namespace okay::ecs;

struct Hp { int v; };
struct Tag { int v; };

int main() {
    RUN_SUITE("ecs_commands");

    // ---- each(): removing an EARLIER-visited entity must not skip anyone ----
    {
        World w;
        std::vector<Entity> es;
        for (int i = 0; i < 6; ++i) { Entity e = w.create(); w.add<Hp>(e, {i}); es.push_back(e); }
        std::multiset<Entity> seen;
        int n = 0;
        w.each<Hp>([&](Entity e, Hp&) {
            seen.insert(e);
            if (++n == 4) w.destroy(es[0]);       // destroy something already visited
        });
        for (Entity e : es) CHECK(seen.count(e) == 1);   // each live one visited exactly once
    }
    // ---- each(): destroying a LATER entity: it is not visited, nobody visited twice ----
    {
        World w;
        std::vector<Entity> es;
        for (int i = 0; i < 6; ++i) { Entity e = w.create(); w.add<Hp>(e, {i}); es.push_back(e); }
        std::multiset<Entity> seen;
        bool first = true;
        w.each<Hp>([&](Entity e, Hp&) {
            seen.insert(e);
            if (first) { first = false; w.destroy(es[3]); }
        });
        for (Entity e : seen) CHECK(seen.count(e) == 1);
        CHECK(seen.count(es[3]) == 0);
        CHECK(seen.size() == 5);
    }
    // ---- each(): adding to the iterated pool is safe (reference stays valid, new ones wait) ----
    {
        World w;
        for (int i = 0; i < 4; ++i) w.add<Hp>(w.create(), {i});
        int visited = 0; bool ok = true;
        w.each<Hp>([&](Entity, Hp& h) {
            int before = h.v;
            for (int k = 0; k < 64; ++k) w.add<Hp>(w.create(), {100});   // forces reallocation
            h.v += 1;   // the reference must still be valid; (vector realloc would be UB)
            if (h.v != before + 1) ok = false;
            ++visited;
        });
        CHECK(ok);
        CHECK(visited == 4);                 // entities added mid-iteration are not visited this pass
        CHECK(w.pool<Hp>().size() == 4 + 4 * 64);
    }
    // ---- two-pool each: same guarantees ----
    {
        World w;
        std::vector<Entity> es;
        for (int i = 0; i < 5; ++i) { Entity e = w.create(); w.add<Hp>(e, {i}); w.add<Tag>(e, {i}); es.push_back(e); }
        std::multiset<Entity> seen; int n = 0;
        w.each<Hp, Tag>([&](Entity e, Hp&, Tag&) {
            seen.insert(e);
            if (++n == 3) w.remove<Tag>(es[0]);
        });
        CHECK(seen.size() == 5 || seen.size() == 4);
        for (Entity e : seen) CHECK(seen.count(e) == 1);
    }

    // ---- command buffer ----
    {
        World w;
        Entity a = w.create(), b = w.create(), c = w.create();
        w.add<Hp>(a, {1}); w.add<Hp>(b, {2}); w.add<Hp>(c, {3});
        Entity spawned = Null;
        w.each<Hp>([&](Entity e, Hp& h) {
            auto& cmd = w.commands();
            if (h.v == 1) {
                cmd.destroy(b);                       // a later entity
                spawned = cmd.create();               // id usable right away
                cmd.add<Hp>(spawned, {50});
                cmd.add<Tag>(spawned, {7});
            }
            if (h.v == 3) { cmd.remove<Hp>(a); cmd.set<Tag>(c, {9}); }
            CHECK(w.flush() == 0);                    // flushing mid-iteration is refused
            (void)e;
        });
        CHECK(w.alive(b) && !w.alive(spawned));       // nothing applied yet
        CHECK(!w.has<Tag>(c));
        CHECK(w.commands().size() > 0);
        std::size_t n = w.flush();
        CHECK(n == 6);
        CHECK(w.commands().empty());
        CHECK(!w.alive(b) && w.alive(spawned));
        CHECK(w.get<Hp>(spawned) && w.get<Hp>(spawned)->v == 50 && w.get<Tag>(spawned)->v == 7);
        CHECK(!w.has<Hp>(a) && w.alive(a));
        CHECK(w.get<Tag>(c) && w.get<Tag>(c)->v == 9);
        CHECK(w.flush() == 0);
        // New ids never collide with a reserved one.
        Entity next = w.create();
        CHECK(next != spawned && next != a && next != b && next != c);
    }
    // Commands run in order: add then destroy leaves nothing, destroy then add on a dead entity stores nothing.
    {
        World w;
        Entity e = w.create();
        w.commands().add<Hp>(e, {1});
        w.commands().destroy(e);
        w.commands().add<Hp>(e, {2});
        w.flush();
        CHECK(!w.alive(e) && !w.has<Hp>(e) && w.pool<Hp>().size() == 0);
    }

    // ---- delta snapshots ----
    {
        NetWorld srv, cli;
        srv.replicate<Hp>(1); srv.replicate<Tag>(2);
        cli.replicate<Hp>(1); cli.replicate<Tag>(2);
        std::vector<Entity> es;
        for (int i = 0; i < 200; ++i) { Entity e = srv.world.create(); srv.world.add<Hp>(e, {i}); srv.world.add<Tag>(e, {i}); es.push_back(e); }

        // First packet: no baseline -> full, and a legacy-format client path still works.
        auto p1 = srv.deltaSnapshot();
        CHECK(cli.applyPacket(p1) == NetWorld::ApplyResult::Applied);
        CHECK(cli.world.count() == 200 && cli.world.get<Hp>(es[17])->v == 17);
        CHECK(cli.appliedTick() == srv.tick());
        // The legacy plain snapshot is unchanged and still applies.
        { NetWorld c2; c2.replicate<Hp>(1); c2.replicate<Tag>(2); c2.apply(srv.snapshot());
          CHECK(c2.world.count() == 200 && c2.world.get<Tag>(es[199])->v == 199); }

        // Not acked yet -> still full.
        auto p2 = srv.deltaSnapshot();
        CHECK(p2.size() == p1.size());
        CHECK(cli.applyPacket(p2) == NetWorld::ApplyResult::Applied);
        CHECK(srv.receiveAck(cli.ackPacket()) && srv.ackedTick() == srv.tick());

        // One change -> tiny delta.
        srv.world.get<Hp>(es[5])->v = 999;
        auto d1 = srv.deltaSnapshot();
        CHECK(d1.size() < p1.size() / 10);
        CHECK(cli.applyPacket(d1) == NetWorld::ApplyResult::Applied);
        CHECK(cli.world.get<Hp>(es[5])->v == 999 && cli.world.get<Hp>(es[6])->v == 6);
        // Duplicate delivery is ignored.
        CHECK(cli.applyPacket(d1) == NetWorld::ApplyResult::Ignored);
        // No change at all -> an (almost) empty delta.
        CHECK(srv.receiveAck(cli.ackPacket()));
        auto d2 = srv.deltaSnapshot();
        CHECK(d2.size() < 64);
        CHECK(cli.applyPacket(d2) == NetWorld::ApplyResult::Applied);

        // Spawn, destroy, remove a component, in one delta (baseline = d2's predecessor ack).
        srv.receiveAck(cli.ackPacket());
        Entity ne = srv.world.create(); srv.world.add<Hp>(ne, {77});
        srv.world.destroy(es[0]);
        srv.world.remove<Tag>(es[1]);
        auto d3 = srv.deltaSnapshot();
        CHECK(cli.applyPacket(d3) == NetWorld::ApplyResult::Applied);
        CHECK(cli.world.alive(ne) && cli.world.get<Hp>(ne)->v == 77 && !cli.world.has<Tag>(ne));
        CHECK(!cli.world.alive(es[0]) && !cli.world.has<Hp>(es[0]));
        CHECK(cli.world.alive(es[1]) && !cli.world.has<Tag>(es[1]) && cli.world.has<Hp>(es[1]));
        CHECK(cli.world.count() == srv.world.count());

        // Lost packet: the server keeps diffing against the acked baseline, so the next delta repairs it.
        srv.receiveAck(cli.ackPacket());
        srv.world.get<Hp>(es[10])->v = 1;
        (void)srv.deltaSnapshot();                           // lost
        srv.world.get<Hp>(es[11])->v = 2;
        auto d5 = srv.deltaSnapshot();
        CHECK(cli.applyPacket(d5) == NetWorld::ApplyResult::Applied);
        CHECK(cli.world.get<Hp>(es[10])->v == 1 && cli.world.get<Hp>(es[11])->v == 2);

        // A delta for a baseline the client is not at is refused (and changes nothing).
        NetWorld fresh; fresh.replicate<Hp>(1); fresh.replicate<Tag>(2);
        CHECK(fresh.applyPacket(d5) == NetWorld::ApplyResult::NeedFull);
        CHECK(fresh.world.count() == 0);
        // The client reports tick 0; the server then falls back to a full snapshot.
        CHECK(srv.receiveAck(fresh.ackPacket()) && srv.ackedTick() == 0);
        auto full = srv.deltaSnapshot();
        CHECK(fresh.applyPacket(full) == NetWorld::ApplyResult::Applied);
        CHECK(fresh.world.count() == srv.world.count());

        // Value that changes and changes back between baseline and send still ends equal.
        srv.receiveAck(fresh.ackPacket());
        srv.world.get<Hp>(es[20])->v = 5;
        srv.world.get<Hp>(es[20])->v = 20;
        auto d6 = srv.deltaSnapshot();
        CHECK(fresh.applyPacket(d6) == NetWorld::ApplyResult::Applied);
        CHECK(fresh.world.get<Hp>(es[20])->v == 20);

        // Bad acks are rejected.
        CHECK(!srv.receiveAck({1, 2, 3}));
        auto ack = cli.ackPacket(); ack[4] = 9;
        CHECK(!srv.receiveAck(ack));
        auto future = cli.ackPacket(); future[5] = 0xFF; future[6] = 0xFF;
        CHECK(!srv.receiveAck(future));
    }

    // ---- hostile / damaged delta packets change nothing and never crash ----
    {
        NetWorld srv, cli;
        srv.replicate<Hp>(1); cli.replicate<Hp>(1);
        for (int i = 0; i < 20; ++i) srv.world.add<Hp>(srv.world.create(), {i});
        cli.applyPacket(srv.deltaSnapshot());
        srv.receiveAck(cli.ackPacket());
        srv.world.add<Hp>(srv.world.create(), {99});
        srv.world.get<Hp>(3)->v = 42;
        auto good = srv.deltaSnapshot();
        const std::size_t before = cli.world.count();

        // Every truncation is rejected without a partial apply.
        for (std::size_t n = 0; n < good.size(); ++n) {
            std::vector<std::uint8_t> cut(good.begin(), good.begin() + n);
            auto r = cli.applyPacket(cut);
            CHECK(r != NetWorld::ApplyResult::Applied);
            CHECK(cli.world.count() == before && cli.world.get<Hp>(3)->v == 2);
        }
        // Huge counts.
        auto bad = good;
        for (int i = 0; i < 4; ++i) bad[13 + i] = 0xFF;       // nAdded
        CHECK(cli.applyPacket(bad) == NetWorld::ApplyResult::Malformed);
        // Trailing garbage and unknown kind.
        auto trail = good; trail.push_back(0);
        CHECK(cli.applyPacket(trail) == NetWorld::ApplyResult::Malformed);
        auto kind = good; kind[4] = 77;
        CHECK(cli.applyPacket(kind) == NetWorld::ApplyResult::Malformed);
        CHECK(cli.world.count() == before);
        // Random bytes behind a valid header never crash.
        unsigned seed = 12345;
        for (int t = 0; t < 2000; ++t) {
            std::vector<std::uint8_t> junk = good;
            for (int k = 0; k < 3; ++k) { seed = seed * 1664525u + 1013904223u; junk[(seed >> 8) % junk.size()] = (std::uint8_t)(seed >> 24); }
            NetWorld c2; c2.replicate<Hp>(1);
            c2.applyPacket(srv.deltaSnapshot().size() ? junk : junk);
            CHECK(c2.world.count() < 100000);
        }
        // The real one still applies after all that.
        CHECK(cli.applyPacket(good) == NetWorld::ApplyResult::Applied);
        CHECK(cli.world.get<Hp>(3)->v == 42 && cli.world.count() == before + 1);
    }

    // ---- NetWorld::apply scales linearly ----
    {
        NetWorld srv, cli;
        srv.replicate<Hp>(1); cli.replicate<Hp>(1);
        for (int i = 0; i < 40000; ++i) srv.world.add<Hp>(srv.world.create(), {i});
        auto snap = srv.snapshot();
        cli.apply(snap);
        auto t0 = std::chrono::steady_clock::now();
        cli.apply(snap);                      // every entity already present
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(cli.world.count() == 40000);
        CHECK(ms < 1000.0);                   // was O(n^2): tens of seconds
    }

    TEST_MAIN_RESULT();
}
