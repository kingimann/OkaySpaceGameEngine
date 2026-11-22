// Edge cases in the world-building components (feature map area 2: particles, terrain, foliage, water):
// absurd or broken numbers must not hang, crash or give a wrong answer, and a setting changed while the
// game runs must take effect.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Terrain.hpp"
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#ifndef _WIN32
#include <csignal>
#include <cstdio>
#include <unistd.h>
#endif

using namespace okay;

namespace {

#ifndef _WIN32
// A hang is a failure, not a stuck test run: give up loudly after a minute.
void OnAlarm(int) {
    const char msg[] = "  FAIL: timed out (an Update or a load never returned)\n";
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(1);
}
#endif

double SecondsOf(const std::chrono::steady_clock::time_point& t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

ParticleSystem* MakeEmitter(Scene& s) {
    GameObject* g = s.CreateGameObject("Fx");
    auto* ps = g->AddComponent<ParticleSystem>();
    s.Update(0.0f);   // runs Awake: the pool is made from maxParticles
    return ps;
}

Terrain* MakeTerrain(Scene& s, float size, int res, float height) {
    GameObject* g = s.CreateGameObject("Terrain");
    auto* t = g->AddComponent<Terrain>();
    t->Resize(res); t->size = size;
    t->Flatten(height);
    t->Apply();
    return t;
}

} // namespace

int main() {
    RUN_SUITE("world_edges");
    Log::console = false;
#ifndef _WIN32
    std::signal(SIGALRM, OnAlarm);
    alarm(60);
#endif

    // ---- particles: Max Particles changed while it runs takes effect --------------------------------------
    {
        Scene s("P1"); s.physicsEnabled = false;
        ParticleSystem* ps = MakeEmitter(s);
        ps->startLifetime = 100.0f; ps->emissionRate = 0.0f;
        ps->maxParticles = 1000;           // raised from 300 after the pool was made (the Inspector does this)
        ps->Emit(1000);
        ps->Update(0.01f);
        CHECK(ps->AliveCount() == 1000);
        ps->maxParticles = 10;             // lowered: the extra particles go
        ps->Update(0.01f);
        CHECK(ps->AliveCount() <= 10);
        CHECK((int)ps->Particles().size() == 10);
    }

    // ---- particles: a huge burst costs no more than filling the pool once ------------------------------
    {
        Scene s("P2"); s.physicsEnabled = false;
        ParticleSystem* ps = MakeEmitter(s);
        ps->maxParticles = 100000; ps->emissionRate = 0.0f; ps->startLifetime = 50.0f;
        ps->burstCount = 100000000;        // far more than the pool holds
        ps->burstTime = 0.0f;
        const auto t0 = std::chrono::steady_clock::now();
        ps->Update(0.016f);
        const double secs = SecondsOf(t0);
        CHECK(ps->AliveCount() == 100000);
        CHECK(secs < 2.0);
        if (secs >= 2.0) std::cerr << "    burst took " << secs << " s\n";
        // Emit() called by a script with the same number is just as quick.
        ps->Restart();
        const auto t1 = std::chrono::steady_clock::now();
        ps->Emit(100000000);
        CHECK(SecondsOf(t1) < 2.0);
        CHECK(ps->AliveCount() == 100000);
    }

    // ---- particles: an infinite or enormous emission rate fills the pool and returns ---------------------
    {
        Scene s("P3"); s.physicsEnabled = false;
        ParticleSystem* ps = MakeEmitter(s);
        ps->emissionRate = std::numeric_limits<float>::infinity();
        ps->Update(0.016f);                // used to loop forever (inf - 1 == inf)
        CHECK(ps->AliveCount() == ps->maxParticles);
        ps->Restart();
        ps->emissionRate = 1.0e30f;
        const auto t0 = std::chrono::steady_clock::now();
        ps->Update(0.016f);
        CHECK(SecondsOf(t0) < 2.0);
        CHECK(ps->AliveCount() == ps->maxParticles);
        // A full pool does not bank the missed particles: once rate is normal again it emits normally.
        ps->Restart();
        ps->emissionRate = 10.0f;
        ps->Update(0.1f);
        CHECK(ps->AliveCount() == 1);
    }

    // ---- particles: a NaN lifetime does not make particles live forever ---------------------------------
    {
        Scene s("P4"); s.physicsEnabled = false;
        ParticleSystem* ps = MakeEmitter(s);
        ps->emissionRate = 0.0f;
        ps->startLifetime = std::numeric_limits<float>::quiet_NaN();
        ps->Emit(5);
        for (int i = 0; i < 10; ++i) ps->Update(0.1f);
        CHECK(ps->AliveCount() == 0);
        // A NaN size or start colour is not passed to the renderer either.
        ps->startLifetime = 5.0f;
        ps->startSize = std::numeric_limits<float>::quiet_NaN();
        ps->Emit(1);
        ps->Update(0.01f);
        bool finite = true;
        for (const auto& p : ps->Particles()) if (p.alive && !std::isfinite(p.size)) finite = false;
        CHECK(finite);
    }

    // ---- terrain: the ground is level right up to the edge of a raised flat terrain ----------------------
    {
        Scene s("T1"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s, 40.0f, 32, 5.0f);
        // One cell (1.25 m) from the edge the samples used to read height 0 beyond the grid.
        for (float x : {19.9f, 20.0f, -19.9f, -20.0f, 19.0f}) {
            const Vec3 n = t->NormalAt(x, 0.0f);
            CHECK_NEAR(n.y, 1.0f, 1e-4f);
            CHECK(t->SlopeAt(x, 3.0f) < 0.5f);
            const Vec3 nz = t->NormalAt(3.0f, x);
            CHECK_NEAR(nz.y, 1.0f, 1e-4f);
        }
        CHECK_NEAR(t->SampleHeight(20.0f, 20.0f), 5.0f, 1e-4f);
        CHECK_NEAR(t->SampleHeight(19.99f, -19.99f), 5.0f, 1e-4f);
        // A real slope still reads as a slope next to the edge (one-sided difference there).
        for (int z = 0; z < t->Dim(); ++z)
            for (int x = 0; x < t->Dim(); ++x) t->SetHeight(x, z, x * t->CellSize());   // 45 degrees along +X
        CHECK_NEAR(t->SlopeAt(19.9f, 0.0f), 45.0f, 0.5f);
        CHECK_NEAR(t->SlopeAt(0.0f, 0.0f), 45.0f, 0.5f);
    }

    // ---- terrain: size 0 does not give NaN heights or normals ---------------------------------------------
    {
        Scene s("T2"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s, 40.0f, 16, 2.0f);
        t->size = 0.0f;
        CHECK(std::isfinite(t->SampleHeight(0.0f, 0.0f)));
        CHECK(std::isfinite(t->SampleHeight(3.0f, -1.0f)));
        const Vec3 n = t->NormalAt(0.0f, 0.0f);
        CHECK(std::isfinite(n.x) && std::isfinite(n.y) && std::isfinite(n.z));
    }

    // ---- foliage: plants grow right up to the edge of a raised terrain -----------------------------------
    {
        Scene s("F1"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s, 40.0f, 32, 5.0f);
        GameObject* g = s.CreateGameObject("Grass");
        auto* f = g->AddComponent<Foliage>();
        f->mesh = "Bush"; f->variants = 1;
        f->radius = 0.5f; f->density = 8.0f; f->spacing = 0.0f; f->maxSlope = 10.0f;
        const int n = f->Paint(19.45f, 0.0f, Foliage::TerrainSurface(*t));   // a brush inside the last cell
        CHECK(n >= 3);
    }

    // ---- foliage: Scatter leans the plants with the ground like painting does -----------------------------
    {
        Scene s("F2"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s, 40.0f, 32, 0.0f);
        for (int z = 0; z < t->Dim(); ++z)
            for (int x = 0; x < t->Dim(); ++x) t->SetHeight(x, z, x * t->CellSize() * 0.4f);   // a steady 22 degree slope
        GameObject* g = s.CreateGameObject("Trees");
        auto* f = g->AddComponent<Foliage>();
        f->mesh = "Bush"; f->variants = 1; f->maxSlope = 40.0f;
        f->alignToSurface = 1.0f;
        CHECK(f->ScatterOnTerrain(*t, 20, 3u) == 20);
        int leaning = 0;
        for (const auto& in : f->instances) if (in.leanX < -0.2f) ++leaning;   // the slope rises to +X: up leans to -X
        CHECK(leaning == 20);
        // With alignment off they stand straight, as before.
        f->Clear(); f->alignToSurface = 0.0f;
        CHECK(f->ScatterOnTerrain(*t, 20, 3u) == 20);
        bool straight = true;
        for (const auto& in : f->instances) if (in.leanX != 0.0f || in.leanZ != 0.0f) straight = false;
        CHECK(straight);
    }

    // ---- water: a damaged Detail in a scene file does not crash the load ----------------------------------
    {
        Scene a("W1"); a.physicsEnabled = false;
        GameObject* g = a.CreateGameObject("Lake");
        auto* w = g->AddComponent<Water>();
        w->Apply();
        std::string text = SceneSerializer::Serialize(a);
        const std::string key = "  water 100 48 ";
        const std::size_t at = text.find(key);
        CHECK(at != std::string::npos);
        if (at != std::string::npos) {
            for (const char* bad : {"  water 100 1000000 ", "  water 100 -5 ", "  water 100 0 "}) {
                std::string t2 = text;
                t2.replace(at, key.size(), bad);
                Scene b("W1b"); b.physicsEnabled = false;
                std::string err;
                const bool ok = SceneSerializer::Deserialize(b, t2, &err);
                CHECK(ok);
                Water* lw = nullptr;
                for (const auto& o : b.Objects()) if (auto* c = o->GetComponent<Water>()) lw = c;
                CHECK(lw != nullptr);
                if (lw) {
                    CHECK(lw->resolution >= 1 && lw->resolution <= 1024);
                    CHECK(lw->LastVertexCount() >= 0);
                    auto* mr = lw->gameObject->GetComponent<MeshRenderer>();
                    bool finite = mr != nullptr;
                    if (mr) for (const Vec3& v : mr->mesh.vertices) if (!std::isfinite(v.x) || !std::isfinite(v.z)) finite = false;
                    CHECK(finite);
                }
            }
        }
    }

    TEST_MAIN_RESULT();
}
