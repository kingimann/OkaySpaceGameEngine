#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <fstream>
#include <chrono>
#include <iostream>

using namespace okay;

// Follow-up to real-life digging: a terrain that is moved, turned and scaled, the world-edge catch, the dug material's dust
// colour and sound (docs/rendering/terrain_water.md, "Real-life digging").
namespace {
bool AllFiniteField(const VoxelTerrain& v) { for (float f : v.density) if (!std::isfinite(f)) return false; return true; }
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
            b->transform->SetPosition({w.v->HalfX() + 15.0f, -30.0f, 0.0f});   // outside the grid (inside it the ground pushes a body up)
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
        const std::string wav = std::string(OKAY_TEST_OUTPUT_DIR) + "/voxel_pick_stone.wav";
        {   // a 0.1 s, 16-bit mono, 8 kHz WAV written by hand
            const int n = 800; std::string d((std::size_t)n * 2, '\0');
            for (int i = 0; i < n; ++i) { const short v = (short)(8000.0 * std::sin(i * 0.3)); d[(std::size_t)i * 2] = (char)(v & 255); d[(std::size_t)i * 2 + 1] = (char)((v >> 8) & 255); }
            auto le = [](std::string& o, unsigned x, int bytes) { for (int b = 0; b < bytes; ++b) o.push_back((char)((x >> (8 * b)) & 255)); };
            std::string f = "RIFF"; le(f, 36 + (unsigned)d.size(), 4); f += "WAVEfmt "; le(f, 16, 4); le(f, 1, 2); le(f, 1, 2); le(f, 8000, 4); le(f, 16000, 4); le(f, 2, 2); le(f, 16, 2);
            f += "data"; le(f, (unsigned)d.size(), 4); f += d;
            std::ofstream(wav, std::ios::binary) << f;
        }
        w.v->materials[(std::size_t)stone].digSound = wav;
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
        CHECK(au->clipPath == wav);
        CHECK(!au->Clip().samples.empty());                                  // loaded the first time the material was dug
    }

    // --- tools and building items come from the inventory ----------------------------------------------------------------------------------
    {
        World w({0, 0, 0}, 0.0f, 1.0f);
        GameObject* pl = w.s.CreateGameObject("Player");
        auto* inv = pl->AddComponent<Inventory>();
        const int stone = w.v->MaterialIndex("Stone"), dirt = w.v->MaterialIndex("Dirt");
        for (auto& m : w.v->material) m = (unsigned char)stone;
        w.d->collect = VoxelDigger::Collect::Store; w.d->giveItems = true; w.d->toolsFromInventory = true;
        w.d->shape = VoxelTerrain::EditOptions::Shape::Sphere;
        w.AimAtLocal({0, 15.0f, 0});
        const std::vector<float> d0 = w.v->density;
        Input::FeedMouse({0, 0}, 1u);
        for (int i = 0; i < 30; ++i) w.s.Update(1.0f / 60.0f);
        Input::FeedMouse({0, 0}, 0u); w.s.Update(1.0f / 60.0f);
        CHECK(w.d->CurrentPower() < 1.0f);
        CHECK(w.v->density == d0);                                          // bare hands cannot dig stone
        inv->AddPartial("pickaxe", 1);
        CHECK(w.d->CurrentPower() >= 5.0f);
        Input::FeedMouse({0, 0}, 1u);
        for (int i = 0; i < 60; ++i) w.s.Update(1.0f / 60.0f);
        Input::FeedMouse({0, 0}, 0u); w.s.Update(1.0f / 60.0f);
        CHECK(w.v->density != d0);                                          // a pickaxe can
        const int gotStone = inv->Count("stone");
        std::cout << "  pickaxe dug stone: " << gotStone << " stone items in the bag\n";
        CHECK(gotStone > 0);
        // building spends the items; with none it does nothing
        w.d->buildFromItems = true; w.d->addMaterial = dirt; w.d->toolsFromInventory = false;
        const std::vector<float> d1 = w.v->density;
        Input::FeedMouse({0, 0}, 2u);
        for (int i = 0; i < 20; ++i) w.s.Update(1.0f / 60.0f);
        Input::FeedMouse({0, 0}, 0u); w.s.Update(1.0f / 60.0f);
        CHECK(w.v->density == d1);                                          // no dirt items: nothing to build with
        inv->AddPartial("dirt", 40);
        Input::FeedMouse({0, 0}, 2u);
        for (int i = 0; i < 40; ++i) w.s.Update(1.0f / 60.0f);
        Input::FeedMouse({0, 0}, 0u); w.s.Update(1.0f / 60.0f);
        std::cout << "  built from items: 40 dirt -> " << inv->Count("dirt") << "\n";
        CHECK(w.v->density != d1 && inv->Count("dirt") < 40);
        // saved
        const std::string text = SceneSerializer::Serialize(w.s);
        Scene s2("L"); s2.physicsEnabled = false;
        w.d->toolsFromInventory = true; w.d->tools = "spade:3"; w.d->bareHandsPower = 1.5f;
        CHECK(SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(w.s)));
        auto* d2 = s2.Find("Terrain")->GetComponent<VoxelDigger>();
        CHECK(d2->toolsFromInventory && d2->buildFromItems && d2->tools == "spade:3" && d2->bareHandsPower == 1.5f);
        (void)text;
    }

    // --- ore veins in the generator --------------------------------------------------------------------------------------------------------
    {
        auto ore = [](int veins, unsigned seed) {
            VoxelTerrain v; v.Resize(50, 40, 50); v.voxelSize = 1.0f; v.Generate(0.6f, 5.0f, 0.0f, 3u);
            VoxelTerrain::LayerRules r; r.oreAmount = 0.0f; r.oreVeins = veins;
            v.GenerateMaterials(seed, r);
            std::vector<unsigned char> m = v.material; int n = 0; const int id = v.MaterialIndex("Ore");
            for (std::size_t i = 0; i < m.size(); ++i) if (m[i] == id && v.density[i] > v.iso) ++n;
            return std::make_pair(n, m);
        };
        const auto none = ore(0, 5u), some = ore(25, 5u), again = ore(25, 5u), other = ore(25, 6u);
        std::cout << "  ore voxels: no veins " << none.first << ", 25 veins " << some.first << "\n";
        CHECK(none.first == 0 && some.first > 150);
        CHECK(some.second == again.second);                                  // same seed, same veins
        CHECK(some.second != other.second);
    }

    // --- cave-in rule ---------------------------------------------------------------------------------------------------------------------
    {
        auto tunnel = [](int halfWidth) {            // a roofed tunnel through a solid block, the roof 4 voxels thick
            VoxelTerrain v; v.Resize(48, 36, 48); v.voxelSize = 1.0f; v.FillSlab(0.7f); v.EnableMaterials(4);
            for (int z = 0; z < v.nz; ++z) for (int y = 6; y < 12; ++y) for (int x = 24 - halfWidth; x < 24 + halfWidth; ++x) v.Set(x, y, z, -3.0f);   // the cut
            return v;
        };
        // a narrow tunnel holds, a wide one lets the roof go
        VoxelTerrain narrow = tunnel(3), wide = tunnel(10);
        const double vn = narrow.SolidVolume(), vw = wide.SolidVolume();
        const auto pn = narrow.CaveIn(4, 6), pw = wide.CaveIn(4, 6);
        double fallen = 0; for (const auto& p : pw) fallen += p.volume;
        std::cout << "  cave-in span 4: narrow tunnel " << pn.size() << " pieces, wide tunnel " << pw.size() << " pieces carrying " << fallen << " m3\n";
        CHECK(pn.empty());
        CHECK(!pw.empty() && fallen > 50.0);
        CHECK_NEAR(vw - wide.SolidVolume(), fallen, 1e-3);                      // what left the field is what the pieces carry
        CHECK(vn == narrow.SolidVolume());
        // off, and bounded: span 0 does nothing; a huge box is skipped
        VoxelTerrain z = tunnel(10); CHECK(z.CaveIn(0, 6).empty());
        // the digger: Cave-in Span saved
        Scene s("CV"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Terrain");
        auto* v = g->AddComponent<VoxelTerrain>(); v->Resize(12, 12, 12); v->FillSlab(0.5f);
        auto* d = g->AddComponent<VoxelDigger>(); d->caveInSpan = 6;
        Scene s2("CV2"); s2.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s)));
        CHECK(s2.Find("Terrain")->GetComponent<VoxelDigger>()->caveInSpan == 6);
    }

    // --- budgeted (chunk by chunk) generation gives the very same world --------------------------------------------------------------------
    {
        VoxelTerrain::LayerRules rules; rules.oreVeins = 10;
        VoxelTerrain a; a.Resize(60, 40, 60); a.voxelSize = 1.0f; a.Generate(0.55f, 6.0f, 0.4f, 31u); a.GenerateMaterials(31u, rules);
        VoxelTerrain b; b.Resize(60, 40, 60); b.voxelSize = 1.0f;
        b.BeginGenerate(0.55f, 6.0f, 0.4f, 31u, true, rules);
        CHECK(b.Generating());
        int steps = 0; float last = -1.0f; bool mono = true;
        while (!b.StepGenerate(0.0f)) { ++steps; const float p = b.GenerateProgress(); if (p < last) mono = false; last = p; if (steps > 1000) break; }
        std::cout << "  budgeted generation: " << steps + 1 << " steps of at least one slice each\n";
        CHECK(!b.Generating() && mono && steps >= 100);                      // 60 + 60 slices
        CHECK(a.density == b.density && a.material == b.material);
        VoxelTerrain c; c.Resize(60, 40, 60); c.voxelSize = 1.0f;
        c.BeginGenerate(0.55f, 6.0f, 0.4f, 31u, true, rules);
        CHECK(c.StepGenerate(1.0e6f) && c.density == a.density);              // a huge budget finishes in one call
        // the running game steps it by itself and skins the terrain when it is done
        Scene s("GEN"); s.physicsEnabled = false; s.runScripts = true;
        GameObject* g = s.CreateGameObject("Terrain");
        auto* v = g->AddComponent<VoxelTerrain>(); v->Resize(60, 40, 60); v->voxelSize = 1.0f; v->generateBudgetMs = 0.2f;
        v->BeginGenerate(0.55f, 6.0f, 0.0f, 5u);
        s.Update(1.0f / 60.0f);
        const bool partial = v->Generating();
        int frames = 1; while (v->Generating() && frames < 5000) { s.Update(1.0f / 60.0f); ++frames; }
        std::cout << "  running game: generated over " << frames << " frames\n";
        CHECK(partial || frames == 1);
        CHECK(!v->Generating());
        const Mesh drawn = [&] { GameObject* gg = g; std::size_t n = 0; for (auto* mr : gg->GetComponents<MeshRenderer>()) n += mr->mesh.vertices.size(); Mesh m; m.vertices.resize(n); return m; }();
        CHECK(drawn.vertices.size() > 1000);
    }

    // --- water and lava in dug holes -------------------------------------------------------------------------------------------------------
    {
        VoxelTerrain v; v.Resize(36, 30, 36); v.voxelSize = 1.0f; v.FillSlab(0.5f);
        v.Dig({0, 15.0f, 0}, 4.0f, 30.0f);                                    // a pit
        v.Dig({0, 12.0f, 0}, 3.0f, 30.0f);
        CHECK(v.PourFluid({0, 20.0f, 0}, 50) == 0);                            // off by default: nothing happens
        v.fluidOn = true; v.fluidBudget = 40;
        CHECK(v.PourFluid({0, 18.0f, 0}, 150) == 150);
        int worstStep = 0, steps = 0;
        while (steps < 4000 && v.FluidActiveCells() > 0) { const int m = v.FluidStep(); worstStep = std::max(worstStep, m); ++steps; }
        std::cout << "  fluid: 150 cells came to rest after " << steps << " steps; worst step moved " << worstStep << " cells\n";
        CHECK(worstStep <= 40);                                                // the per-step budget holds
        CHECK(v.FluidCells() == 150);                                          // nothing is lost
        bool rests = true, inGround = false, inPit = false;
        for (int z = 0; z < v.nz; ++z) for (int y = 0; y < v.ny; ++y) for (int x = 0; x < v.nx; ++x) {
            if (!v.FluidAt(x, y, z)) continue;
            if (v.Get(x, y, z) > v.iso) inGround = true;
            if (y > 0 && v.Get(x, y - 1, z) <= v.iso && !v.FluidAt(x, y - 1, z)) rests = false;   // every cell stands on ground or fluid
            if (std::abs(x - 18) <= 5 && std::abs(z - 18) <= 5 && y < 15) inPit = true;
        }
        CHECK(rests && !inGround && inPit);
        // digging beside it wakes it; building ground over it removes that fluid
        v.Add({0, 11.0f, 0}, 3.5f, 30.0f);
        for (int i = 0; i < 400 && v.FluidActiveCells() > 0; ++i) v.FluidStep();
        std::cout << "  fluid after building over it: " << v.FluidCells() << " cells\n";
        CHECK(v.FluidCells() < 150);
        v.ClearFluid(); CHECK(v.FluidCells() == 0);
        // in the running game: Update flows it and draws it as one child object that goes when the fluid does
        Scene s("FL"); s.physicsEnabled = false; s.runScripts = true;
        GameObject* g = s.CreateGameObject("Terrain");
        auto* t = g->AddComponent<VoxelTerrain>(); t->Resize(36, 30, 36); t->voxelSize = 1.0f; t->FillSlab(0.5f); t->Dig({0, 15.0f, 0}, 4.0f, 30.0f);
        t->fluidOn = true; t->fluidRate = 60.0f;
        t->Apply(); s.Update(0.0f);
        t->PourFluid({0, 18.0f, 0}, 80);
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        GameObject* fo = s.Find("Voxel Fluid");
        CHECK(fo != nullptr && fo->GetComponent<MeshRenderer>() && fo->GetComponent<MeshRenderer>()->mesh.TriangleCount() > 20);
        CHECK(t->FluidCells() == 80);
        t->ClearFluid(); for (int i = 0; i < 20; ++i) s.Update(1.0f / 60.0f);
        CHECK(s.Find("Voxel Fluid") == nullptr);
        // fluid settings are saved; the fluid itself is not
        t->fluidRate = 7.0f; t->fluidGlow = true;
        Scene s2("FL2"); s2.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s)));
        auto* t2 = s2.Find("Terrain")->GetComponent<VoxelTerrain>();
        CHECK(t2->fluidOn && t2->fluidRate == 7.0f && t2->fluidGlow && t2->FluidCells() == 0);
    }

    // --- footprints and tracks --------------------------------------------------------------------------------------------------------------
    {
        Scene s("FT"); s.physicsEnabled = false; s.runScripts = true;
        GameObject* g = s.CreateGameObject("Terrain");
        auto* t = g->AddComponent<VoxelTerrain>(); t->Resize(40, 30, 40); t->voxelSize = 1.0f; t->FillSlab(0.5f); t->GenerateMaterials(2u); t->Apply();
        GameObject* pl = s.CreateGameObject("Player"); pl->tag = "Player";
        float sy = 0; t->SurfaceY(0, 0, sy);
        pl->transform->localPosition = {-10, sy + 0.1f, 0};
        s.Update(1.0f / 60.0f);
        const std::vector<float> d0 = t->density;
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(t->density == d0 && t->footprintCount == 0);                    // off by default
        t->footprints = true;
        for (int i = 0; i < 6; ++i) s.Update(1.0f / 60.0f);
        for (int i = 0; i < 100; ++i) {                                        // walk 12 m along the ground
            pl->transform->localPosition = {-10.0f + i * 0.12f, 0.0f, 0.0f};
            float y = 0; t->SurfaceY(pl->transform->localPosition.x, 0, y); pl->transform->localPosition = {-10.0f + i * 0.12f, y + 0.05f, 0.0f};
            s.Update(1.0f / 60.0f);
        }
        std::cout << "  footprints: " << t->footprintCount << " dents along 12 m\n";
        CHECK(t->footprintCount >= 10 && t->footprintCount <= 40);
        CHECK(t->density != d0 && AllFiniteField(*t));
        CHECK(!t->CanUndo());                                                   // not part of a stroke
        // stone does not take a print
        VoxelTerrain st; st.Resize(40, 30, 40); st.voxelSize = 1.0f; st.FillSlab(0.5f); st.EnableMaterials(4);
        VoxelTerrain::EditOptions o; o.power = 2.0f;
        CHECK(!st.Edit({0, 15.2f, 0}, 0.45f, 0.5f, o).changed);
        Scene s2("FT2"); s2.physicsEnabled = false;
        t->footprintTag = "Walker"; t->footprintStep = 1.5f;
        CHECK(SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s)));
        auto* t2 = s2.Find("Terrain")->GetComponent<VoxelTerrain>();
        CHECK(t2->footprints && t2->footprintTag == "Walker" && t2->footprintStep == 1.5f);
    }

    // --- foliage on a voxel terrain asks the density field instead of ray casting every triangle -------------------------------------------------
    {
        auto build = [](Scene& s, float yawDeg) {
            GameObject* g = s.CreateGameObject("Terrain");
            g->transform->localRotation = Quat::AngleAxis(yawDeg, Vec3{0, 1, 0});
            auto* v = g->AddComponent<VoxelTerrain>(); v->Resize(72, 44, 72); v->voxelSize = 1.5f; v->Generate(0.55f, 9.0f, 0.0f, 2024u); v->Apply();
            GameObject* f = s.CreateGameObject("Foliage"); f->AddComponent<Foliage>();
            return std::make_pair(v, f->GetComponent<Foliage>());
        };
        Scene sa("FA"), sb("FB"); sa.physicsEnabled = false; sb.physicsEnabled = false;
        auto [va, fa] = build(sa, 0.0f);          // the fast path
        auto [vb, fb] = build(sb, 0.05f);         // turned a hair: the ray cast path
        auto fast = fa->SceneSurface(sa, 500.0f), slow = fb->SceneSurface(sb, 500.0f);
        int ok = 0, n = 0; float worst = 0; float nyMin = 1;
        using Clk = std::chrono::steady_clock;
        double tf = 0, ts = 0;
        for (int i = 0; i < 60; ++i) {
            const float x = -40.0f + (i % 10) * 8.0f, z = -35.0f + (i / 10) * 12.0f;
            Vec3 p1, n1, p2, n2;
            auto t0 = Clk::now(); const bool a = fast(x, z, p1, n1); tf += std::chrono::duration<double, std::milli>(Clk::now() - t0).count();
            t0 = Clk::now(); const bool b = slow(x, z, p2, n2); ts += std::chrono::duration<double, std::milli>(Clk::now() - t0).count();
            if (a != b) continue;
            ++n; if (!a) continue;
            ++ok; worst = std::max(worst, std::fabs(p1.y - p2.y)); nyMin = std::min(nyMin, n1.y);
            CHECK(std::fabs(n1.Magnitude() - 1.0f) < 1e-3f);
        }
        // Timing: 60 density queries take only ~0.1 ms in all, so one scheduler stall on a busy CI runner can swamp that single sum
        // (seen on CI: 0.07 ms per density query against 0.0015 ms locally, ray cast unchanged at ~0.18 ms). Measure each path in
        // 7 separate rounds and compare the medians. Locally the density path is ~100x faster, so 5x keeps wide headroom.
        auto median = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        std::vector<double> rf, rs;
        for (int round = 0; round < 7; ++round) {
            double a = 0, b = 0;
            for (int i = 0; i < 60; ++i) {
                const float x = -40.0f + (i % 10) * 8.0f, z = -35.0f + (i / 10) * 12.0f;
                Vec3 p1, n1;
                auto t0 = Clk::now(); (void)fast(x, z, p1, n1); a += std::chrono::duration<double, std::milli>(Clk::now() - t0).count();
                t0 = Clk::now(); (void)slow(x, z, p1, n1); b += std::chrono::duration<double, std::milli>(Clk::now() - t0).count();
            }
            rf.push_back(a); rs.push_back(b);
        }
        tf = median(rf); ts = median(rs);
        std::cout << "  foliage ground query: " << tf / 60 << " ms (density) against " << ts / 60 << " ms (ray cast), worst height difference " << worst << " m\n";
        CHECK(ok > 40 && n >= ok);
        CHECK(worst < 1.2f);                                           // within a voxel and a half (the mesh and the density sample differ a little)
        CHECK(tf * 5.0 < ts);                                          // medians of 7 rounds (see above)
        CHECK(nyMin > -0.01f);
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
