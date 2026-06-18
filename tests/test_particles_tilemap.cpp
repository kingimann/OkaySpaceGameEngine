#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("particles_tilemap");

    // --- ParticleSystem ---
    {
        Scene scene("PS");
        scene.physicsEnabled = false;
        GameObject* go = scene.CreateGameObject("Emitter");
        auto* ps = go->AddComponent<ParticleSystem>();
        ps->emissionRate = 100.0f;
        ps->startLifetime = 1.0f;
        ps->maxParticles = 500;
        scene.Start();

        CHECK(ps->AliveCount() == 0);
        for (int i = 0; i < 30; ++i) scene.Update(1.0f / 60.0f); // 0.5s -> ~50 emitted
        int mid = ps->AliveCount();
        CHECK(mid > 30);

        // Stop emitting; after the lifetime passes, all particles die.
        ps->playing = false;
        for (int i = 0; i < 120; ++i) scene.Update(1.0f / 60.0f); // 2s
        CHECK(ps->AliveCount() == 0);

        // Burst emission.
        ps->Emit(10);
        CHECK(ps->AliveCount() == 10);
    }

    // --- Tilemap ---
    {
        Scene scene("TM");
        scene.physicsEnabled = false;
        GameObject* go = scene.CreateGameObject("Map");
        go->transform->localPosition = {0, 0, 0};
        auto* tm = go->AddComponent<Tilemap>();
        tm->tileSize = 2.0f;
        tm->Resize(4, 3);
        CHECK(tm->Width() == 4 && tm->Height() == 3);
        CHECK(tm->GetTile(0, 0) == 0);

        tm->SetTile(1, 2, 7);
        CHECK(tm->GetTile(1, 2) == 7);
        CHECK(tm->GetTile(99, 99) == 0); // out of bounds
        CHECK(tm->Count(7) == 1);

        tm->Fill(3);
        CHECK(tm->Count(3) == 12);

        // Cell <-> world round trip (tileSize 2 -> cell (1,2) center at (3,5)).
        Vec3 w = tm->CellToWorld(1, 2);
        CHECK_NEAR(w.x, 3.0f, 1e-4);
        CHECK_NEAR(w.y, 5.0f, 1e-4);
        int cx, cy; tm->WorldToCell(w, cx, cy);
        CHECK(cx == 1 && cy == 2);
    }

    TEST_MAIN_RESULT();
}
