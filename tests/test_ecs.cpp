#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <chrono>
#include <thread>

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

    // ---- System scheduler: systems run in order, with dt, and toggle ----
    {
        World w;
        Entity e = w.create();
        w.add<Position>(e, {0, 0, 0});
        w.add<Velocity>(e, {2, 0, 0});

        SystemScheduler sched;
        sched.add("gravity", [](World& wd, float dt) {
            wd.each<Velocity>([&](Entity, Velocity& v) { v.y -= 10.0f * dt; });
        });
        sched.add("move", [](World& wd, float dt) {
            wd.each<Position, Velocity>([&](Entity, Position& p, Velocity& v) {
                p.x += v.x * dt; p.y += v.y * dt;
            });
        });
        CHECK(sched.count() == 2);

        for (int i = 0; i < 60; ++i) sched.run(w, 1.0f / 60.0f);   // 1 second
        Position* p = w.get<Position>(e);
        CHECK(std::fabs(p->x - 2.0f) < 1e-3f);     // 2 u/s for 1s
        CHECK(p->y < -4.0f);                        // gravity pulled it down

        // Disable a system and it stops running.
        sched.setEnabled("gravity", false);
        CHECK(!sched.enabled("gravity"));
        float vyBefore = w.get<Velocity>(e)->y;
        for (int i = 0; i < 30; ++i) sched.run(w, 1.0f / 60.0f);
        CHECK(std::fabs(w.get<Velocity>(e)->y - vyBefore) < 1e-4f);   // gravity off: vy unchanged
    }

    // ---- Over the wire: server broadcasts its ECS world, client mirrors it ----
    {
        // Server NetworkManager.
        Scene srvScene("Srv");
        auto* server = srvScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(server->StartServer(0));
        srvScene.Start();
        std::uint16_t port = server->ServerPort();

        // Client NetworkManager.
        Scene cliScene("Cli");
        auto* client = cliScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(client->StartClient("127.0.0.1", port));
        cliScene.Start();

        // ECS worlds + replicators.
        NetWorld sw; sw.replicate<Position>(1); sw.replicate<Health>(2);
        NetWorld cw; cw.replicate<Position>(1); cw.replicate<Health>(2);
        WorldReplicator srvRep(sw, *server);
        WorldReplicator cliRep(cw, *client);
        cliRep.listen();

        Entity e = sw.world.create();
        sw.world.add<Position>(e, {3, 4, 5});
        sw.world.add<Health>(e, {88});

        // Pump until the client joins, broadcasting the world each tick.
        bool mirrored = false;
        for (int i = 0; i < 400; ++i) {
            if (server->PeerCount() >= 1) srvRep.broadcast();
            srvScene.Update(0.02f);
            cliScene.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (cw.world.alive(e) && cw.world.get<Position>(e)) { mirrored = true; break; }
        }
        CHECK(mirrored);
        if (mirrored) {
            Position* p = cw.world.get<Position>(e);
            CHECK(p && std::fabs(p->x - 3.0f) < 1e-4f && std::fabs(p->z - 5.0f) < 1e-4f);
            CHECK(cw.world.get<Health>(e) && cw.world.get<Health>(e)->hp == 88);
        }
        server->Stop(); client->Stop();
    }

    // ---- SceneBridge: ECS entities become GameObjects that render/update ----
    {
        Scene scene("bridged");
        NetWorld nw; nw.replicate<EcsTransform>(1);   // same struct drives net + render
        int created = 0;
        SceneBridge bridge(nw.world, scene, [&](Scene& s, Entity) {
            ++created;
            return s.CreateGameObject("Unit");        // a real game would add a MeshRenderer here
        });

        Entity a = nw.world.create();
        EcsTransform ta; ta.position = {2, 0, -3};
        nw.world.add<EcsTransform>(a, ta);
        Entity b = nw.world.create();
        nw.world.add<EcsTransform>(b, {});

        bridge.sync();
        CHECK(created == 2 && bridge.mappedCount() == 2);
        GameObject* goA = bridge.gameObjectFor(a);
        CHECK(goA && std::fabs(goA->transform->Position().x - 2.0f) < 1e-5f
                  && std::fabs(goA->transform->Position().z + 3.0f) < 1e-5f);

        // Move the entity; sync pushes the new transform onto the GameObject.
        nw.world.get<EcsTransform>(a)->position = {5, 1, 0};
        bridge.sync();
        CHECK(created == 2);                            // no new GameObject, just updated
        CHECK(std::fabs(bridge.gameObjectFor(a)->transform->Position().x - 5.0f) < 1e-5f);

        // Despawn the entity; sync drops its GameObject mapping.
        nw.world.destroy(b);
        bridge.sync();
        CHECK(bridge.mappedCount() == 1 && bridge.gameObjectFor(b) == nullptr);

        // The whole pipeline: a snapshot from elsewhere, applied + bridged, renders.
        NetWorld src; src.replicate<EcsTransform>(1);
        Entity s1 = src.world.create();
        EcsTransform st; st.position = {9, 9, 9};
        src.world.add<EcsTransform>(s1, st);
        nw.apply(src.snapshot());                      // replaces nw's world with src's
        bridge.sync();
        CHECK(bridge.gameObjectFor(s1) != nullptr);
        CHECK(std::fabs(bridge.gameObjectFor(s1)->transform->Position().y - 9.0f) < 1e-5f);
    }

    TEST_MAIN_RESULT();
}
