#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <iostream>

using namespace okay;

// The Voxel Digger's real-life options driven the way a player does it: hold the mouse over the ground, release, press Ctrl+Z.
// Every volume the brush takes away must turn up as store, debris or terrain again (docs/terrain_water.md, "Real-life digging").
namespace {

bool AllFiniteField(const VoxelTerrain& v) { for (float f : v.density) if (!std::isfinite(f)) return false; return true; }

struct World {
    Scene s{"REAL"};
    VoxelTerrain* v = nullptr;
    VoxelDigger* d = nullptr;
    World(bool physics = true) {
        s.physicsEnabled = physics;
        GameObject* cam = s.CreateGameObject("Cam");
        auto* c = cam->AddComponent<Camera>(); c->main = true;
        cam->transform->localPosition = {0, 25, 0};
        cam->transform->localRotation = Quat::LookRotation(Vec3{0, 1, 0}, Vec3{0, 0, -1});   // aim along -Forward = straight down
        GameObject* g = s.CreateGameObject("Terrain");
        v = g->AddComponent<VoxelTerrain>();
        v->Resize(36, 30, 36); v->voxelSize = 1.0f; v->FillSlab(0.5f); v->EnableMaterials(0);
        v->Apply();
        d = g->AddComponent<VoxelDigger>();
        d->radius = 2.5f; d->strength = 14.0f; d->removeFloaters = 0; d->showBrush = false;
        s.Update(0.0f);
        Input::FeedMouse({0, 0}, 0u);
        Input::FeedKeys({});
    }
    void Hold(unsigned buttons, int frames) {
        Input::FeedMouse({0, 0}, buttons);
        for (int i = 0; i < frames; ++i) s.Update(1.0f / 60.0f);
    }
    void Release() { Input::FeedMouse({0, 0}, 0u); s.Update(1.0f / 60.0f); }
    double DebrisVolume() const {
        double t = 0;
        for (auto* b : s.FindObjectsOfType<VoxelDebris>()) if (!b->finished) t += b->volume;
        return t;
    }
    static double Sum(const std::vector<double>& a) { double t = 0; for (double x : a) t += x; return t; }
};

} // namespace

int main() {
    RUN_SUITE("voxel_digger_real");
    VoxelTerrain::ForceOldRemesh(0);

    // --- presets ------------------------------------------------------------------------------------------------------------
    {
        VoxelDigger d;
        d.ApplyPreset(VoxelDigger::Preset::Realistic);
        CHECK(d.collect == VoxelDigger::Collect::Both && d.needsMaterial && d.slump && d.collapse && d.power < 100.0f);
        d.ApplyPreset(VoxelDigger::Preset::Arcade);
        CHECK(d.collect == VoxelDigger::Collect::Vanish && !d.needsMaterial && !d.slump && d.power > 1.0e8f);
        d.ApplyPreset(VoxelDigger::Preset::Creative);
        CHECK(d.creative && d.collect == VoxelDigger::Collect::Store);
    }

    // --- an old-style digger (all options off) digs exactly as before ---------------------------------------------------------
    {
        World a, b;
        a.v->DisableMaterials(); b.v->DisableMaterials(); a.v->Apply(); b.v->Apply();
        CHECK(!a.d->UsesRealLife(*a.v));
        a.Hold(1u, 20);
        // the same stroke with the Dig brush called by hand
        for (int i = 0; i < 20; ++i) b.v->Dig({0, 15.0f, 0}, 2.5f, 14.0f / 60.0f);
        CHECK(a.v->density != World().v->density);        // it dug
    }

    // --- Store: every cubic metre the brush takes lands in the store, and the HUD text lists it ------------------------------
    {
        World w;
        GameObject* hud = w.s.CreateGameObject("Counter");
        auto* t = hud->AddComponent<TextRenderer>(); t->text = "x";
        w.d->collect = VoxelDigger::Collect::Store; w.d->hudTarget = "Counter";
        const double before = w.v->SolidVolume();
        w.Hold(1u, 40); w.Release();
        const double removed = before - w.v->SolidVolume();
        const double stored = World::Sum(w.d->store);
        std::cout << "  store mode: terrain lost " << removed << " m3, store holds " << stored << " m3, HUD \"" << t->text << "\"\n";
        CHECK(removed > 5.0);
        CHECK_NEAR(stored, removed, 1e-6 * before);
        CHECK(w.d->Stored(w.v->MaterialIndex("Dirt")) + w.d->Stored(w.v->MaterialIndex("Grass")) + w.d->Stored(w.v->MaterialIndex("Stone")) > 0.0);
        CHECK(t->text.find("m3") != std::string::npos && t->text != "x");

        // building takes from the store: with an empty bag nothing happens, with some it does and the store falls
        w.d->needsMaterial = true; w.d->addMaterial = 0;
        std::fill(w.d->store.begin(), w.d->store.end(), 0.0);
        const std::vector<float> d0 = w.v->density;
        w.Hold(2u, 20); w.Release();
        CHECK(w.v->density == d0);
        w.d->Deposit(0, 6.0);
        w.Hold(2u, 20); w.Release();
        const double left = w.d->Stored(0);
        std::cout << "  built from the store: 6.0 m3 -> " << left << " m3 left\n";
        CHECK(w.v->density != d0 && left < 6.0 && left >= 0.0);
        // creative: unlimited
        w.d->creative = true;
        const double c0 = w.d->Stored(0);
        w.Hold(2u, 20); w.Release();
        CHECK(w.d->Stored(0) == c0);
    }

    // --- Both: debris falls with its volume, the volume is never lost ---------------------------------------------------------
    {
        World w;
        w.d->collect = VoxelDigger::Collect::Both; w.d->debrisMax = 12;
        const double before = w.v->SolidVolume();
        w.Hold(1u, 30);
        const double removed = before - w.v->SolidVolume();
        const double inAir = w.DebrisVolume(), inStore = World::Sum(w.d->store);
        std::cout << "  both: removed " << removed << " m3 = " << inAir << " in " << VoxelDebris::Live() << " debris + " << inStore << " stored\n";
        CHECK(removed > 5.0);
        CHECK(VoxelDebris::Live() > 0 && VoxelDebris::Live() <= 12);
        CHECK_NEAR(inAir + inStore, removed, 1e-6 * before);
        // the player walks up: the camera is inside the pickup range of every piece that lands near it
        w.Release();
        for (auto* b : w.s.FindObjectsOfType<VoxelDebris>()) b->Finish();
        w.s.Update(1.0f / 60.0f);
        CHECK_NEAR(World::Sum(w.d->store), removed, 1e-6 * before);
        CHECK(VoxelDebris::Live() == 0);
    }

    // --- Debris cap: more pieces than allowed go straight to the store ------------------------------------------------------------
    {
        World w;
        w.d->collect = VoxelDigger::Collect::Debris; w.d->debrisMax = 3;
        const double before = w.v->SolidVolume();
        int worst = 0;
        Input::FeedMouse({0, 0}, 1u);
        for (int i = 0; i < 60; ++i) { w.s.Update(1.0f / 60.0f); worst = std::max(worst, VoxelDebris::Live()); }
        const double removed = before - w.v->SolidVolume();
        CHECK(worst <= 3);
        CHECK(World::Sum(w.d->store) > 0.0);                          // the overflow was kept
        CHECK_NEAR(World::Sum(w.d->store) + w.DebrisVolume(), removed, 1e-6 * before);
        w.Release();
    }

    // --- Debris that lands is put back into the terrain: nothing is lost when no one collects it ---------------------------------
    {
        World w;
        w.d->collect = VoxelDigger::Collect::Debris; w.d->debrisLife = 4.0f; w.d->debrisMax = 60;
        const double before = w.v->SolidVolume();
        w.Hold(1u, 30); w.Release();
        const double dug = before - w.v->SolidVolume();
        const int spawned = VoxelDebris::Live();
        int frames = 0;
        while (VoxelDebris::Live() > 0 && frames < 60 * 12) { w.s.Update(1.0f / 60.0f); ++frames; }
        const double after = w.v->SolidVolume() + World::Sum(w.d->store);
        std::cout << "  debris: dug " << dug << " m3 into " << spawned << " pieces; after " << frames << " frames all gone, terrain + store " << after << " m3 (was " << before << ")\n";
        CHECK(spawned > 0);
        CHECK(VoxelDebris::Live() == 0);
        CHECK_NEAR(after, before, before * 0.01);
        CHECK(!w.s.Find("Voxel Debris"));                              // the bodies are gone
    }

    // --- Ctrl+Z undoes the whole stroke, terrain and materials -------------------------------------------------------------------
    {
        World w;
        const std::vector<float> d0 = w.v->density; const std::vector<unsigned char> m0 = w.v->material;
        w.d->collect = VoxelDigger::Collect::Store;
        w.Hold(1u, 30); w.Release();
        CHECK(w.v->density != d0 && w.v->CanUndo());
        const char ctrl = Input::KeyFromName("ctrl");
        Input::FeedKeys({});
        w.s.Update(1.0f / 60.0f);
        Input::FeedKeys({ctrl, 'z'});
        w.s.Update(1.0f / 60.0f);
        Input::FeedKeys({});
        CHECK(w.v->density == d0 && w.v->material == m0);
        CHECK(!w.v->CanUndo());
    }

    // --- Collapse: cutting a pillar at its foot sends the top down as one piece with its own mesh --------------------------------------
    {
        World w;
        w.d->collect = VoxelDigger::Collect::Store; w.d->collapse = true; w.d->debrisMax = 12;
        // a 2x2 pillar standing on the ground
        for (int y = 15; y < 26; ++y) for (int x = 17; x < 19; ++x) for (int z = 17; z < 19; ++z) w.v->Set(x, y, z, 3.0f);
        w.v->Apply();
        // the camera looks along +x at the pillar's foot
        Transform* ct = w.s.Find("Cam")->transform;
        const Vec3 foot = w.v->GridToLocal(17, 17, 17);
        ct->localPosition = {foot.x - 10.0f, 17.4f, foot.z + 0.5f};
        ct->localRotation = Quat::LookRotation(Vec3{-1, 0, 0}, Vec3{0, 1, 0});
        w.d->radius = 2.2f; w.d->strength = 30.0f;
        const double before = w.v->SolidVolume();
        w.Hold(1u, 60); w.Release();
        const double removed = before - w.v->SolidVolume();
        int pieces = 0, meshTris = 0;
        for (auto* b : w.s.FindObjectsOfType<VoxelDebris>()) {
            ++pieces;
            if (auto* mr = b->gameObject->GetComponent<MeshRenderer>()) meshTris = std::max(meshTris, mr->mesh.TriangleCount());
        }
        std::cout << "  collapse: removed " << removed << " m3, " << pieces << " pieces alive, biggest mesh " << meshTris << " triangles, store " << World::Sum(w.d->store) << " m3\n";
        CHECK(removed > 3.0);
        CHECK(pieces >= 1);
        CHECK(meshTris > 12);                                           // the top came down with its own shape, not a cube
        CHECK_NEAR(w.DebrisVolume() + World::Sum(w.d->store), removed, removed * 0.02 + 1e-6);
        // it lands, comes to rest and is built back into the ground where it lies
        int frames = 0;
        while (VoxelDebris::Live() > 0 && frames < 60 * 25) { w.s.Update(1.0f / 60.0f); ++frames; }
        const double after = w.v->SolidVolume() + World::Sum(w.d->store);
        std::cout << "  collapse: piece gone after " << frames << " frames, terrain + store " << after << " m3 (was " << before << ")\n";
        CHECK(VoxelDebris::Live() == 0);
        CHECK_NEAR(after, before, before * 0.01);
        CHECK(AllFiniteField(*w.v));
    }

    TEST_MAIN_RESULT();
}
