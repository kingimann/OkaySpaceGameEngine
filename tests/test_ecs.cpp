#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;
using namespace okay::ecs;

struct Position { float x, y, z; };
struct Velocity { float x, y, z; };
struct Health   { int hp; };

int main() {
    RUN_SUITE("ecs");

    // ---- World basics: create / add / get / query / remove / destroy ----
    {
        World w;
        Entity a = w.create();
        Entity b = w.create();
        CHECK(a != b && w.alive(a) && w.alive(b));
        CHECK(w.count() == 2);

        w.add<Position>(a, {1, 2, 3});
        w.add<Velocity>(a, {1, 0, 0});
        w.add<Position>(b, {0, 0, 0});            // b has Position but no Velocity

        CHECK(w.has<Position>(a) && w.has<Velocity>(a));
        CHECK(w.has<Position>(b) && !w.has<Velocity>(b));
        CHECK(w.get<Position>(a)->y == 2);

        // A movement system: only entities with BOTH Position and Velocity move.
        for (int step = 0; step < 3; ++step)
            w.each<Position, Velocity>([](Entity, Position& p, Velocity& v) { p.x += v.x; });
        CHECK(std::fabs(w.get<Position>(a)->x - 4.0f) < 1e-5f);   // 1 + 3*1
        CHECK(std::fabs(w.get<Position>(b)->x - 0.0f) < 1e-5f);   // b never moved

        // Single-component iteration counts.
        int posCount = 0; w.each<Position>([&](Entity, Position&) { ++posCount; });
        CHECK(posCount == 2);

        w.remove<Velocity>(a);
        CHECK(!w.has<Velocity>(a) && w.has<Position>(a));

        w.destroy(b);
        CHECK(!w.alive(b) && w.count() == 1);
        CHECK(!w.has<Position>(b));               // destroy pruned its components
    }

    // ---- Sparse-set remove is O(1) swap and stays correct over churn ----
    {
        World w;
        std::vector<Entity> es;
        for (int i = 0; i < 50; ++i) { Entity e = w.create(); w.add<Health>(e, {i}); es.push_back(e); }
        // Remove every even-indexed entity's Health.
        for (int i = 0; i < 50; i += 2) w.remove<Health>(es[i]);
        int seen = 0;
        w.each<Health>([&](Entity e, Health& h) {
            ++seen;
            // The surviving ones keep their right value.
            CHECK(w.get<Health>(e)->hp == h.hp);
        });
        CHECK(seen == 25);
    }

    // ---- Networking built in: snapshot on the server, apply on the client ----
    {
        NetWorld server;
        server.replicate<Position>(1);
        server.replicate<Health>(2);

        Entity p1 = server.world.create();
        server.world.add<Position>(p1, {10, 0, 5});
        server.world.add<Health>(p1, {100});
        Entity p2 = server.world.create();
        server.world.add<Position>(p2, {-3, 1, 0});   // p2 has Position only

        NetWorld client;
        client.replicate<Position>(1);
        client.replicate<Health>(2);

        client.apply(server.snapshot());

        // Entities mirrored with the same ids and data.
        CHECK(client.world.alive(p1) && client.world.alive(p2));
        CHECK(client.world.count() == 2);
        Position* cp = client.world.get<Position>(p1);
        CHECK(cp && std::fabs(cp->x - 10.0f) < 1e-5f && std::fabs(cp->z - 5.0f) < 1e-5f);
        CHECK(client.world.get<Health>(p1)->hp == 100);
        CHECK(client.world.has<Position>(p2) && !client.world.has<Health>(p2));

        // Server changes: move p1, hurt it, spawn p3, despawn p2. Re-sync.
        server.world.get<Position>(p1)->x = 11.0f;
        server.world.get<Health>(p1)->hp = 70;
        Entity p3 = server.world.create();
        server.world.add<Position>(p3, {7, 7, 7});
        server.world.destroy(p2);

        client.apply(server.snapshot());

        CHECK(client.world.count() == 2);                 // p2 gone, p3 added
        CHECK(!client.world.alive(p2));
        CHECK(client.world.alive(p3) && client.world.has<Position>(p3));
        CHECK(std::fabs(client.world.get<Position>(p1)->x - 11.0f) < 1e-5f);  // moved
        CHECK(client.world.get<Health>(p1)->hp == 70);                        // hurt
        CHECK(std::fabs(client.world.get<Position>(p3)->z - 7.0f) < 1e-5f);   // new entity replicated
    }

    // ---- A component removed on the server is pruned on the client ----
    {
        NetWorld server; server.replicate<Health>(2);
        NetWorld client; client.replicate<Health>(2);
        Entity e = server.world.create();
        server.world.add<Health>(e, {5});
        client.apply(server.snapshot());
        CHECK(client.world.has<Health>(e));
        server.world.remove<Health>(e);                   // drop the component (entity stays)
        client.apply(server.snapshot());
        CHECK(client.world.alive(e) && !client.world.has<Health>(e));
    }

    TEST_MAIN_RESULT();
}
