#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <iostream>

using namespace okay;

// Follow-up to real-life digging: a terrain that is moved, turned and scaled, the world-edge catch, the dug material's dust
// colour and sound (docs/terrain_water.md, "Real-life digging").
namespace {
struct World {
    Scene s{"MORE"};
    GameObject* tg = nullptr;
    VoxelTerrain* v = nullptr;
    VoxelDigger* d = nullptr;
    GameObject* cam = nullptr;
    World(Vec3 pos, float yawDeg, float scale) {
        s.physicsEnabled = true;
        cam = s.CreateGameObject("Cam");
        cam->AddComponent<Camera>()->main = true;
        tg = s.CreateGameObject("Terrain");
        tg->transform->localPosition = pos;
        tg->transform->localRotation = Quat::AngleAxis(yawDeg, Vec3{0, 1, 0});
        tg->transform->localScale = {scale, scale, scale};
        v = tg->AddComponent<VoxelTerrain>();
        v->Resize(36, 30, 36); v->voxelSize = 1.0f; v->FillSlab(0.5f); v->GenerateMaterials(5u); v->Apply();
        d = tg->AddComponent<VoxelDigger>();
        d->radius = 2.5f; d->strength = 14.0f; d->removeFloaters = 0; d->showBrush = false;
        s.Update(0.0f);
        Input::FeedMouse({0, 0}, 0u); Input::FeedKeys({});
    }
    void AimAtLocal(Vec3 local) {            // camera 25 m straight above that point (world up), looking down
        const Vec3 w = tg->transform->TransformPoint(local);
        cam->transform->localPosition = {w.x, w.y + 25.0f, w.z};
        cam->transform->localRotation = Quat::LookRotation(Vec3{0, 1, 0}, Vec3{0, 0, -1});
    }
};
}

int main() {
    RUN_SUITE("voxel_more");
    VoxelTerrain::ForceOldRemesh(0);

    // --- a terrain that is moved, turned and scaled: aim, debris and landing all work in its own space ------------------------
    for (int variant = 0; variant < 3; ++variant) {
        const Vec3 pos = variant == 0 ? Vec3{100, 5, -40} : Vec3{-60, 0, 30};
        const float yaw = variant == 0 ? 0.0f : 35.0f, scale = variant == 2 ? 1.5f : 1.0f;
        World w(pos, yaw, scale);
        w.d->collect = VoxelDigger::Collect::Debris; w.d->debrisLife = 3.0f; w.d->debrisMax = 60;
        w.AimAtLocal({0, 15.0f, 0});
        const double before = w.v->SolidVolume();
        Input::FeedMouse({0, 0}, 1u);
        for (int i = 0; i < 30; ++i) w.s.Update(1.0f / 60.0f);
        Input::FeedMouse({0, 0}, 0u); w.s.Update(1.0f / 60.0f);
        const double dug = before - w.v->SolidVolume();
        CHECK(dug > 3.0);                                                   // it dug (aim used the terrain's own space)
        bool near = true; int n = 0;
        const Vec3 centre = w.tg->transform->TransformPoint({0, 17.0f, 0});
        for (auto* b : w.s.FindObjectsOfType<VoxelDebris>()) {
            ++n;
            if ((b->gameObject->transform->Position() - centre).Magnitude() > 12.0f * scale) near = false;
        }
        CHECK(n > 0 && near);                                               // the debris starts at the dig, not at the world origin
        int frames = 0;
        while (VoxelDebris::Live() > 0 && frames < 60 * 10) { w.s.Update(1.0f / 60.0f); ++frames; }
        const double after = w.v->SolidVolume();
        std::cout << "  moved terrain #" << variant << " (yaw " << yaw << ", scale " << scale << "): dug " << dug << " m3, back after "
                  << frames << " frames: " << after << " of " << before << " m3\n";
        CHECK(VoxelDebris::Live() == 0);
        CHECK_NEAR(after, before, before * 0.01);
    }

    // --- world-edge catch ---------------------------------------------------------------------------------------------------------
    for (int on = 0; on < 2; ++on) {
        World w({0, 0, 0}, 0.0f, 1.0f);
        w.v->catchFalling = on == 1; w.v->catchMargin = 4.0f;
        GameObject* b = w.s.CreateGameObject("Ball");
        b->transform->localPosition = {w.v->HalfX() + 15.0f, 20.0f, 0.0f};      // past the side of the world
        b->AddComponent<SphereCollider3D>();
        auto* rb = b->AddComponent<Rigidbody3D>();
        (void)rb;
        float lowest = 1e9f;
        for (int i = 0; i < 60 * 8; ++i) { w.s.Update(1.0f / 60.0f); lowest = std::min(lowest, b->transform->Position().y); }
        std::cout << "  edge catch " << (on ? "on" : "off") << ": lowest y " << lowest << ", caught " << w.v->caughtCount << "\n";
        if (on) { CHECK(lowest > -8.0f); CHECK(w.v->caughtCount >= 1); }
        else    { CHECK(lowest < -20.0f); }
        if (on) {                                                          // the respawn hook replaces the default
            int hooked = 0;
            w.v->onFall = [&](GameObject&) { ++hooked; };
            b->transform->SetPosition({0, -30, 0});
            for (int i = 0; i < 12; ++i) w.s.Update(1.0f / 60.0f);
            std::cout << "  hook calls " << hooked << ", ball y " << b->transform->Position().y << ", caught " << w.v->caughtCount << "\n";
            CHECK(hooked >= 1);
        }
    }

    // --- dust colour and sound follow the material dug ----------------------------------------------------------------------------
    {
        World w({0, 0, 0}, 0.0f, 1.0f);
        const int stone = w.v->MaterialIndex("Stone");
        for (auto& m : w.v->material) m = (unsigned char)stone;
        w.v->materials[(std::size_t)stone].particle = Color::FromBytes(10, 200, 30);
        w.v->materials[(std::size_t)stone].digSound = "sounds/pick_stone.wav";
        auto* au = w.tg->AddComponent<AudioSource>();
        w.d->ApplyPreset(VoxelDigger::Preset::Arcade); w.d->power = 9.0f; w.d->shape = VoxelTerrain::EditOptions::Shape::Sphere;
        w.d->collect = VoxelDigger::Collect::Store;
        w.AimAtLocal({0, 15.0f, 0});
        Input::FeedMouse({0, 0}, 1u);
        for (int i = 0; i < 40; ++i) w.s.Update(1.0f / 60.0f);
        Input::FeedMouse({0, 0}, 0u); w.s.Update(1.0f / 60.0f);
        CHECK(w.d->lastDigMaterial == stone);
        GameObject* dust = w.s.Find("VoxelDebris");
        auto* ps = dust ? dust->GetComponent<ParticleSystem>() : nullptr;
        CHECK(ps != nullptr);
        if (ps) CHECK(ps->startColor.g > 0.7f && ps->startColor.r < 0.1f);
        CHECK(au->clipPath == "sounds/pick_stone.wav");
    }

    // --- the catch is saved --------------------------------------------------------------------------------------------------------
    {
        World w({0, 0, 0}, 0.0f, 1.0f);
        w.v->catchFalling = true; w.v->catchMargin = 7.0f;
        const std::string text = SceneSerializer::Serialize(w.s);
        Scene s2("L"); s2.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* v2 = s2.Find("Terrain")->GetComponent<VoxelTerrain>();
        CHECK(v2->catchFalling && v2->catchMargin == 7.0f);
    }

    // --- avalanche: more slump passes per frame carry loose ground farther, each pass stays bounded; the option is saved ----------------
    {
        auto run = [](int passes) {
            VoxelTerrain v; v.Resize(40, 30, 40); v.voxelSize = 1.0f; v.FillSlab(0.4f); v.EnableMaterials(2);   // sand
            for (int y = 12; y < 24; ++y) for (int x = 18; x < 22; ++x) for (int z = 18; z < 22; ++z) v.Set(x, y, z, 3.0f);   // a sand tower
            int moved = 0, worst = 0;
            for (int p = 0; p < passes; ++p) { const int n = v.Slump(40); worst = std::max(worst, n); moved += n; if (!n) break; }
            return std::make_pair(moved, worst);
        };
        const auto one = run(1), many = run(10);
        std::cout << "  slump: 1 pass moved " << one.first << " voxels, 10 passes " << many.first << " (worst pass " << many.second << ")\n";
        CHECK(many.first > one.first && many.second <= 40);
        Scene s("SV"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Terrain");
        auto* v = g->AddComponent<VoxelTerrain>(); v->Resize(12, 12, 12); v->FillSlab(0.5f);
        auto* d = g->AddComponent<VoxelDigger>(); d->slump = true; d->slumpPasses = 9;
        Scene s2("SV2"); s2.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s)));
        CHECK(s2.Find("Terrain")->GetComponent<VoxelDigger>()->slumpPasses == 9);
    }

    TEST_MAIN_RESULT();
}
