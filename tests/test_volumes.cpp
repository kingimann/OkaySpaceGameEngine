#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Volume.hpp"
#include "okay/Testing/SceneValidator.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace okay;
using namespace okay::testing;
namespace fs = std::filesystem;

// Volumes: the shared shape (box, sphere, cylinder), the five zone kinds that use it (checkpoint, kill, force,
// atmosphere, ambience), saving them in a scene and a prefab, and the validator rules for them.

namespace {

void Run(Scene& s, int frames, float dt = 1.0f / 60.0f) { for (int i = 0; i < frames; ++i) s.Update(dt); }

Volume* MakeVolume(Scene& s, const char* name, Volume::Kind kind, Vec3 pos, Vec3 size) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->SetPosition(pos);
    auto* v = g->AddComponent<Volume>();
    v->kind = kind;
    v->size = size;
    return v;
}

GameObject* MakeBody(Scene& s, const char* name, Vec3 pos, bool rigid = false) {
    GameObject* g = s.CreateGameObject(name);
    g->tag = "Player";
    g->transform->SetPosition(pos);
    if (rigid) {
        auto* rb = g->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->allowSleep = false;
    }
    return g;
}

GameObject* AddCamera(Scene& s, Vec3 pos) {
    GameObject* c = s.CreateGameObject("Cam");
    c->transform->SetPosition(pos);
    c->AddComponent<Camera>();
    return c;
}

} // namespace

int main() {
    RUN_SUITE("volumes");
    Log::console = false;

    // ---- Shapes: contains, depth, rotation, scale ----------------------------------------------
    {
        Scene s("shapes");
        GameObject* g = s.CreateGameObject("V");
        g->transform->SetPosition({10, 0, 0});
        VolumeGeometry box{VolumeShape::Box, {4, 2, 6}, {0, 0, 0}};
        CHECK(box.Contains(*g->transform, {10, 0, 0}));
        CHECK(box.Contains(*g->transform, {11.9f, 0.9f, 2.9f}));
        CHECK(!box.Contains(*g->transform, {12.1f, 0, 0}));
        CHECK(!box.Contains(*g->transform, {10, 1.1f, 0}));
        CHECK_NEAR(box.DepthAt(*g->transform, {10, 0, 0}), 1.0f, 1e-5f);
        CHECK_NEAR(box.DepthAt(*g->transform, {12, 0, 0}), 0.0f, 1e-5f);
        CHECK(box.DepthAt(*g->transform, {14, 0, 0}) < 0.0f);

        VolumeGeometry sph{VolumeShape::Sphere, {4, 4, 4}, {0, 0, 0}};
        CHECK(sph.Contains(*g->transform, {11.9f, 0, 0}));
        CHECK(!sph.Contains(*g->transform, {11.7f, 1.7f, 0}));   // inside the box, outside the ball
        VolumeGeometry cyl{VolumeShape::Cylinder, {4, 4, 4}, {0, 0, 0}};
        CHECK(cyl.Contains(*g->transform, {11.9f, 1.9f, 0}));
        CHECK(!cyl.Contains(*g->transform, {11.7f, 0, 1.7f}));   // outside the round side
        CHECK(!cyl.Contains(*g->transform, {10, 2.1f, 0}));

        // Offset moves the centre; a turned and scaled object turns and scales the region.
        VolumeGeometry off{VolumeShape::Box, {2, 2, 2}, {0, 3, 0}};
        CHECK(off.Contains(*g->transform, {10, 3, 0}));
        CHECK(!off.Contains(*g->transform, {10, 0, 0}));
        g->transform->SetRotation(Quat::Euler(0, 90, 0));
        g->transform->SetScale({2, 1, 1});
        VolumeGeometry thin{VolumeShape::Box, {4, 2, 1}, {0, 0, 0}};   // local x length 4 * scale 2 = 8 wide, turned onto world z
        CHECK(thin.Contains(*g->transform, {10, 0, 3.5f}));
        CHECK(!thin.Contains(*g->transform, {13.5f, 0, 0}));

        // A size of zero holds nothing and is not Valid.
        VolumeGeometry flat{VolumeShape::Box, {4, 0, 4}, {0, 0, 0}};
        CHECK(!flat.Valid());
        CHECK(!flat.Contains(*g->transform, g->transform->Position()));
        CHECK(box.Valid());
    }

    // ---- Handles: dragging a face leaves the opposite one where it was --------------------------
    {
        VolumeGeometry g{VolumeShape::Box, {4, 2, 4}, {0, 0, 0}};
        float applied = g.DragFace(0, +1, 2.0f);              // pull the +X face out by 2
        CHECK_NEAR(applied, 2.0f, 1e-5f);
        CHECK_NEAR(g.size.x, 6.0f, 1e-5f);
        CHECK_NEAR(g.offset.x, 1.0f, 1e-5f);                  // -X face still at -2: centre moved by 1
        g.DragFace(0, -1, -3.0f);                             // push the -X face out by 3 (moves along -X)
        CHECK_NEAR(g.size.x, 9.0f, 1e-5f);
        CHECK_NEAR(g.offset.x, -0.5f, 1e-5f);                 // +X face still at +4
        CHECK_NEAR(g.offset.x + g.size.x * 0.5f, 4.0f, 1e-5f);
        applied = g.DragFace(1, +1, -50.0f);                  // cannot collapse
        CHECK_NEAR(g.size.y, VolumeGeometry::kMinSize, 1e-5f);
        CHECK(applied > -50.0f);
        CHECK(g.Valid());
        CHECK_NEAR(g.DragFace(5, 1, 1.0f), 0.0f, 1e-6f);      // not an axis

        Vec3 pos, size;
        VolumeGeometry::FromGroundDrag({1, 2, 3}, {5, 2, -1}, 3.0f, pos, size);
        CHECK_NEAR(pos.x, 3.0f, 1e-5f); CHECK_NEAR(pos.z, 1.0f, 1e-5f); CHECK_NEAR(pos.y, 3.5f, 1e-5f);
        CHECK_NEAR(size.x, 4.0f, 1e-5f); CHECK_NEAR(size.y, 3.0f, 1e-5f); CHECK_NEAR(size.z, 4.0f, 1e-5f);
        VolumeGeometry::FromGroundDrag({0, 0, 0}, {0, 0, 0}, 3.0f, pos, size);   // a click without a drag still has a size
        CHECK(size.x >= VolumeGeometry::kMinSize && size.z >= VolumeGeometry::kMinSize);
    }

    // ---- Checkpoint + Kill ----------------------------------------------------------------------
    {
        Scene s("checkpoint");
        Volume* cp = MakeVolume(s, "CP", Volume::Kind::Checkpoint, {20, 1.5f, 0}, {4, 3, 4});   // floor at y = 0
        Volume* kill = MakeVolume(s, "Lava", Volume::Kind::Kill, {40, 0, 0}, {6, 2, 6});
        GameObject* hero = MakeBody(s, "Hero", {0, 1, 0}, /*rigid*/ true);
        GameObject* npc = s.CreateGameObject("Rock");   // not a body of ours
        npc->transform->SetPosition({40, 0, 0});
        s.Start();
        Run(s, 2);
        CHECK(!cp->Occupants());

        // Straight into the lava: no checkpoint yet, so it goes back to where it began.
        hero->transform->SetPosition({40, 0, 0});
        hero->GetComponent<Rigidbody3D>()->velocity = {3, -4, 0};
        Run(s, 2);
        CHECK_NEAR(hero->transform->Position().x, 0.0f, 1e-4f);
        CHECK_NEAR(hero->transform->Position().y, 1.0f, 1e-4f);
        CHECK_NEAR(hero->GetComponent<Rigidbody3D>()->velocity.x, 0.0f, 1e-4f);
        CHECK_NEAR(npc->transform->Position().x, 40.0f, 1e-4f);            // untagged, no rigidbody: ignored

        // Touch the checkpoint, wander off, fall in the lava: back to the checkpoint's floor.
        hero->transform->SetPosition({21, 2, 1});
        Run(s, 2);
        CHECK(cp->Occupants() == 1);
        Vec3 rp;
        CHECK(Volume::RespawnPoint(hero, rp));
        CHECK_NEAR(rp.x, 20.0f, 1e-4f); CHECK_NEAR(rp.y, 0.1f, 1e-4f); CHECK_NEAR(rp.z, 0.0f, 1e-4f);
        hero->transform->SetPosition({60, 1, 0});
        Run(s, 2);
        CHECK(cp->Occupants() == 0);
        hero->transform->SetPosition({41, 0, 0});
        Run(s, 2);
        CHECK_NEAR(hero->transform->Position().x, 20.0f, 1e-4f);
        CHECK_NEAR(hero->transform->Position().y, 0.1f, 1e-4f);
        CHECK(kill->Occupants() == 0);                                      // sent away already

        // Kill damage hurts first; Destroy removes the body.
        auto* hp = hero->AddComponent<HealthStat>();
        s.Start();
        const float before = hp->health;
        kill->killDamage = 30.0f;
        hero->transform->SetPosition({41, 0, 0});
        Run(s, 2);
        CHECK(hp->health < before);
        kill->killAction = (int)Volume::KillAction::Destroy;
        hero->transform->SetPosition({41, 0, 0});
        Run(s, 3);
        CHECK(s.Find("Hero") == nullptr);
    }

    // ---- Kill and Checkpoint respect the activator tag and a child follows its parent -------------
    {
        Scene s("tags");
        Volume* kill = MakeVolume(s, "Pit", Volume::Kind::Kill, {0, 0, 0}, {10, 10, 10});
        kill->activatorTag = "Hero";
        GameObject* a = s.CreateGameObject("A"); a->tag = "Hero"; a->transform->SetPosition({30, 0, 0});
        GameObject* b = s.CreateGameObject("B"); b->tag = "Enemy"; b->transform->SetPosition({30, 0, 5});
        GameObject* child = s.CreateGameObject("Hat"); child->tag = "Hero"; child->transform->SetPosition({30, 0, 0});
        child->transform->SetParent(a->transform);
        s.Start();
        Run(s, 2);
        a->transform->SetPosition({1, 0, 0});
        b->transform->SetPosition({1, 0, 5});
        Run(s, 2);
        CHECK_NEAR(a->transform->Position().x, 30.0f, 1e-4f);   // the Hero went home, the child with it
        CHECK_NEAR(child->transform->Position().x, 30.0f, 1e-4f);
        CHECK(b->transform->Position().x == 1.0f);              // the Enemy is not ours to send
    }

    // ---- Kill / Checkpoint tell scripts ---------------------------------------------------------
    {
        Scene s("signal");
        Volume* cp = MakeVolume(s, "CP", Volume::Kind::Checkpoint, {0, 0, 0}, {4, 4, 4});
        cp->eventName = "Saved";
        GameObject* hero = MakeBody(s, "Hero", {20, 0, 0});
        GameObject* listener = s.CreateGameObject("Listener");
        auto* sc = listener->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("var got = \"\";\nfunction on_logic_signal(name) { got = name; }\n"));
        s.Start();
        Run(s, 2);
        CHECK(sc->VM()->GetGlobal("got").AsString() == "");
        hero->transform->SetPosition({0, 0, 0});
        Run(s, 2);
        CHECK(sc->VM()->GetGlobal("got").AsString() == "Saved");
    }

    // ---- Force ---------------------------------------------------------------------------------
    {
        Scene s("force");
        Volume* f = MakeVolume(s, "Wind", Volume::Kind::Force, {0, 0, 0}, {10, 10, 10});
        f->activatorTag.clear();
        f->forceDirection = {0, 0, 1};
        f->forceStrength = 6.0f;
        GameObject* inside = MakeBody(s, "In", {0, 0, 0}, true);
        GameObject* outside = MakeBody(s, "Out", {50, 0, 0}, true);
        GameObject* fixed = MakeBody(s, "Fixed", {1, 0, 1}, true);
        fixed->GetComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        s.Start();
        Run(s, 60);
        const float vz = inside->GetComponent<Rigidbody3D>()->velocity.z;
        CHECK(vz > 4.0f && vz < 7.5f);                                      // ~6 m/s after one second
        CHECK_NEAR(outside->GetComponent<Rigidbody3D>()->velocity.z, 0.0f, 1e-4f);
        CHECK_NEAR(fixed->transform->Position().z, 1.0f, 1e-4f);
        CHECK(f->Occupants() == 1);

        // The direction turns with the object when it is local, and stays put when it is not.
        f->transform->SetRotation(Quat::Euler(0, 90, 0));
        inside->GetComponent<Rigidbody3D>()->velocity = Vec3::Zero;
        inside->transform->SetPosition({0, 0, 0});
        Run(s, 30);
        CHECK(inside->GetComponent<Rigidbody3D>()->velocity.x > 2.0f);       // +Z turned a quarter circle onto +X
        f->forceLocal = false;
        inside->GetComponent<Rigidbody3D>()->velocity = Vec3::Zero;
        inside->transform->SetPosition({0, 0, 0});
        Run(s, 30);
        CHECK(inside->GetComponent<Rigidbody3D>()->velocity.z > 2.0f);

        // Radial: away from the centre, and pulled in when negative.
        f->forceShape = (int)Volume::ForceShape::Radial;
        f->forceStrength = 5.0f;
        inside->GetComponent<Rigidbody3D>()->velocity = Vec3::Zero;
        inside->transform->SetPosition({2, 0, 0});
        Run(s, 5);
        CHECK(inside->GetComponent<Rigidbody3D>()->velocity.x > 0.1f);
        f->forceStrength = -5.0f;
        inside->GetComponent<Rigidbody3D>()->velocity = Vec3::Zero;
        inside->transform->SetPosition({2, 0, 0});
        Run(s, 5);
        CHECK(inside->GetComponent<Rigidbody3D>()->velocity.x < -0.1f);

        // A tag filter leaves other bodies alone.
        f->forceShape = (int)Volume::ForceShape::Direction;
        f->forceStrength = 6.0f; f->forceLocal = false;
        f->activatorTag = "Crate";
        inside->GetComponent<Rigidbody3D>()->velocity = Vec3::Zero;
        inside->transform->SetPosition({0, 0, 0});
        Run(s, 30);
        CHECK_NEAR(inside->GetComponent<Rigidbody3D>()->velocity.z, 0.0f, 1e-4f);
    }

    // ---- Atmosphere ----------------------------------------------------------------------------
    {
        Scene s("atmo");
        GameObject* cam = AddCamera(s, {100, 0, 0});
        Volume* v = MakeVolume(s, "Mist", Volume::Kind::Atmosphere, {0, 0, 0}, {20, 20, 20});
        v->fogColor = Color{0.2f, 0.4f, 0.6f, 1.0f};
        v->fogStart = 1.0f; v->fogEnd = 10.0f; v->vignette = 0.5f; v->blend = 0.5f;
        s.renderSettings.fog = false;
        s.renderSettings.vignette = 0.0f;
        s.Start();
        Run(s, 3);
        CHECK(!s.renderSettings.fog);                                       // the camera is outside: untouched
        CHECK_NEAR(v->ViewerWeight(), 0.0f, 1e-6f);

        cam->transform->SetPosition({0, 0, 0});                              // the middle: full strength
        Run(s, 2);
        CHECK(s.renderSettings.fog);
        CHECK_NEAR(s.renderSettings.fogColor.g, 0.4f, 1e-4f);
        CHECK_NEAR(s.renderSettings.fogStart, 1.0f, 1e-3f);
        CHECK_NEAR(s.renderSettings.fogEnd, 10.0f, 1e-3f);
        CHECK_NEAR(s.renderSettings.vignette, 0.5f, 1e-4f);
        CHECK_NEAR(v->ViewerWeight(), 1.0f, 1e-4f);

        cam->transform->SetPosition({9.0f, 0, 0});                           // near the edge: part way
        Run(s, 2);
        CHECK(v->ViewerWeight() > 0.0f && v->ViewerWeight() < 1.0f);
        CHECK(s.renderSettings.vignette > 0.0f && s.renderSettings.vignette < 0.5f);

        cam->transform->SetPosition({100, 0, 0});                            // out again: the scene's own look is back
        Run(s, 2);
        CHECK(!s.renderSettings.fog);
        CHECK_NEAR(s.renderSettings.vignette, 0.0f, 1e-6f);

        // A scene that already has fog goes back to its own fog, not to none.
        s.renderSettings.fog = true;
        s.renderSettings.fogColor = Color{1, 1, 1, 1};
        s.renderSettings.fogStart = 20.0f; s.renderSettings.fogEnd = 90.0f;
        cam->transform->SetPosition({0, 0, 0});
        Run(s, 2);
        CHECK_NEAR(s.renderSettings.fogEnd, 10.0f, 1e-3f);
        cam->transform->SetPosition({100, 0, 0});
        Run(s, 2);
        CHECK(s.renderSettings.fog);
        CHECK_NEAR(s.renderSettings.fogStart, 20.0f, 1e-4f);
        CHECK_NEAR(s.renderSettings.fogEnd, 90.0f, 1e-4f);
        CHECK_NEAR(s.renderSettings.fogColor.r, 1.0f, 1e-4f);

        // Switching the volume off while the camera is inside gives the scene its settings back.
        cam->transform->SetPosition({0, 0, 0});
        Run(s, 2);
        CHECK_NEAR(s.renderSettings.fogEnd, 10.0f, 1e-3f);
        v->enabled = false;
        Run(s, 2);
        CHECK_NEAR(s.renderSettings.fogEnd, 90.0f, 1e-4f);
    }

    // ---- Ambience ------------------------------------------------------------------------------
    {
        Scene s("ambience");
        GameObject* cam = AddCamera(s, {100, 0, 0});
        Volume* v = MakeVolume(s, "Cave", Volume::Kind::Ambience, {0, 0, 0}, {10, 10, 10});
        v->fadeTime = 2.0f;
        auto* a = v->gameObject->AddComponent<AudioSource>();
        a->clip.sampleRate = 8000; a->clip.channels = 1;
        a->clip.samples.assign(8000, 0.1f);
        a->loop = true;
        s.Start();
        Run(s, 3);
        CHECK(!a->IsPlaying());
        cam->transform->SetPosition({0, 0, 0});
        Run(s, 2);
        CHECK(a->IsPlaying());
        CHECK(a->IsFading());                                               // rising toward full volume
        CHECK_NEAR(v->ViewerWeight(), 1.0f, 1e-6f);
        a->FadeTo(1.0f, 0.0f);                                              // (the mixer would have finished the fade-in by now)
        cam->transform->SetPosition({100, 0, 0});
        Run(s, 2);
        CHECK(a->IsFading());                                               // falling, then it stops
        CHECK_NEAR(v->ViewerWeight(), 0.0f, 1e-6f);
        // No Audio Source: nothing to play, and it does not crash.
        Volume* bare = MakeVolume(s, "Bare", Volume::Kind::Ambience, {0, 0, 0}, {4, 4, 4});
        (void)bare;
        cam->transform->SetPosition({0, 0, 0});
        Run(s, 2);
        CHECK(true);
    }

    // ---- Scene and prefab round trip -----------------------------------------------------------
    {
        Scene a("save");
        GameObject* g = a.CreateGameObject("Everything");
        auto* v = g->AddComponent<Volume>();
        v->kind = Volume::Kind::Force;
        v->shape = VolumeShape::Cylinder;
        v->size = {3.5f, 7.25f, 2.0f};
        v->offset = {0.5f, 1.0f, -2.0f};
        v->activatorTag = "Crate";
        v->eventName = "Whoosh";
        v->killAction = (int)Volume::KillAction::Destroy;
        v->killDamage = 12.5f;
        v->forceShape = (int)Volume::ForceShape::Radial;
        v->forceDirection = {0.25f, 0.5f, -1.0f};
        v->forceLocal = false;
        v->forceStrength = -7.5f;
        v->fogColor = Color{0.1f, 0.2f, 0.3f, 1.0f};
        v->fogStart = 3.0f; v->fogEnd = 44.0f; v->vignette = 0.6f; v->blend = 0.2f;
        v->fadeTime = 4.5f;
        v->enabled = false;
        GameObject* g2 = a.CreateGameObject("Second");
        g2->AddComponent<Volume>()->kind = Volume::Kind::Kill;
        g2->AddComponent<Volume>()->kind = Volume::Kind::Checkpoint;      // two on one object both survive

        auto check = [&](Scene& b, const char* what) {
            GameObject* r = b.Find("Everything");
            CHECK(r != nullptr);
            auto* w = r ? r->GetComponent<Volume>() : nullptr;
            CHECK(w != nullptr);
            if (!w) { std::fprintf(stderr, "    (%s)\n", what); return; }
            CHECK(w->kind == Volume::Kind::Force);
            CHECK(w->shape == VolumeShape::Cylinder);
            CHECK_NEAR(w->size.y, 7.25f, 1e-5f); CHECK_NEAR(w->size.x, 3.5f, 1e-5f);
            CHECK_NEAR(w->offset.z, -2.0f, 1e-5f);
            CHECK(w->activatorTag == "Crate");
            CHECK(w->eventName == "Whoosh");
            CHECK(w->killAction == (int)Volume::KillAction::Destroy);
            CHECK_NEAR(w->killDamage, 12.5f, 1e-5f);
            CHECK(w->forceShape == (int)Volume::ForceShape::Radial);
            CHECK_NEAR(w->forceDirection.z, -1.0f, 1e-5f);
            CHECK(!w->forceLocal);
            CHECK_NEAR(w->forceStrength, -7.5f, 1e-5f);
            CHECK_NEAR(w->fogColor.b, 0.3f, 1e-5f);
            CHECK_NEAR(w->fogEnd, 44.0f, 1e-5f);
            CHECK_NEAR(w->vignette, 0.6f, 1e-5f);
            CHECK_NEAR(w->blend, 0.2f, 1e-5f);
            CHECK_NEAR(w->fadeTime, 4.5f, 1e-5f);
            CHECK(!w->enabled);
        };

        const std::string text = SceneSerializer::Serialize(a);
        Scene b("loaded");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err, nullptr));
        check(b, "scene");
        CHECK(SceneSerializer::Serialize(b) == text);
        CHECK(b.Find("Second")->GetComponents<Volume>().size() == 2);

        const std::string path = (fs::temp_directory_path() / "okay_volume_test.okayprefab").string();
        CHECK(SceneSerializer::SaveObjectToFile(*g, path));
        Scene c("prefab");
        GameObject* inst = SceneSerializer::InstantiateFromFile(c, path, &err);
        CHECK(inst != nullptr);
        if (inst) inst->name = "Everything";
        check(c, "prefab");
        std::error_code ec; fs::remove(path, ec);
    }

    // ---- Validator ----------------------------------------------------------------------------
    {
        Scene s("lint");
        AddCamera(s, {0, 0, 0});
        MakeVolume(s, "Flat", Volume::Kind::Checkpoint, {0, 0, 0}, {4, 0, 4});
        Volume* nothing = MakeVolume(s, "Calm", Volume::Kind::Force, {0, 0, 0}, {4, 4, 4});
        nothing->forceStrength = 0.0f;
        MakeVolume(s, "Mute", Volume::Kind::Ambience, {0, 0, 0}, {4, 4, 4});
        Volume* bad = MakeVolume(s, "NoClip", Volume::Kind::Ambience, {0, 0, 0}, {4, 4, 4});
        bad->gameObject->AddComponent<AudioSource>();
        GameObject* plate = s.CreateGameObject("Plate");
        auto* ls = plate->AddComponent<LogicSource>();
        ls->kind = LogicSource::Kind::TriggerZone;
        ls->zoneSize = {2, 0, 2};
        MakeVolume(s, "Fine", Volume::Kind::Kill, {0, 0, 0}, {4, 4, 4});
        ValidatorOptions opt;
        opt.checkAssets = false;
        ValidationReport r = SceneValidator::Validate(s, opt);
        CHECK(r.CountCode("volume.zero_size") == 2);       // the flat volume and the flat logic zone
        CHECK(r.CountCode("volume.force_zero") == 1);
        CHECK(r.CountCode("volume.no_audio") == 2);        // no Audio Source, and one with no clip
        for (const auto& i : r.issues)
            if (i.object == "Fine") CHECK(false);
    }

    // ---- Names and colours: every kind has them ------------------------------------------------
    {
        for (int k = 0; k < Volume::kKindCount; ++k) {
            const Volume::Kind kind = (Volume::Kind)k;
            CHECK(std::string(Volume::KindName(kind)).size() > 2);
            CHECK(std::string(Volume::KindHint(kind)).size() > 10);
            const Color c = Volume::KindColor(kind);
            CHECK(c.r + c.g + c.b > 0.1f);
        }
        CHECK(Volume::KindColor(Volume::Kind::Kill).r != Volume::KindColor(Volume::Kind::Force).r);
    }

    TEST_MAIN_RESULT();
}
