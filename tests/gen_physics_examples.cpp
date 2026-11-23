// Writes the physics example scenes (not part of the normal test run; build it by hand):
//   examples/physics_toys/physics_toys.okayscene        every physics toy in a small playground
//   examples/physics_materials/physics_materials.okayscene   a ramp and a drop pad for every Physics Material preset
//   examples/physics/physics_upgrade.okayscene          domino chain, tower of 10, wrecking ball, ramps, moving platform with a character
//   examples/physics_engine/physics_engine.okayscene    Engine quality (our own engine core): tower, pyramid, ragdoll-like chains
//                                                       with cone-twist joints and drives, a powered arm, fast shots at a thin wall
//   examples/physics_engine/physics_engine_5k.okayscene Engine quality: 5,000 bodies poured into a pit
// usage: gen_physics_examples <examples folder>
#include <Okay.hpp>
#include "okay/Components/PhysicsToyPresets.hpp"
#include "okay/Scene/SceneTemplates.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace okay;
using namespace okay::toypresets;
namespace okay { void RegisterPhysicsToyProperties(); namespace detail { void RegisterPhysicsToySerializers(); } }

static void Label(Scene& s, const std::string& text, Vec3 at) {
    GameObject* g = s.CreateGameObject(UniqueName(s, "Label"));
    g->transform->localPosition = at;
    auto* t = g->AddComponent<TextRenderer>(); t->text = text; t->screenSpace = true; t->color = Color::White; t->pixelSize = 2.0f;
    g->AddComponent<WorldSpaceUI>();
}
static void Basics(Scene& s, const char* name, Vec3 playerAt, bool player = true) {
    s.SetName(name);
    s.physics3D().quality = PhysicsQuality::Improved;
    GameObject* sun = s.CreateGameObject("Sun"); sun->transform->localRotation = Quat::Euler(50.0f, -30.0f, 0.0f); sun->AddComponent<Light>();
    Ground(s, {160, 1, 80});
    if (player) Templates::AddThirdPersonPlayer(s, playerAt);
}
static void WriteScene(Scene& s, const std::filesystem::path& file) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << SceneSerializer::Serialize(s);
    std::printf("wrote %s\n", file.string().c_str());
}
static Vec3 V(float x, float y, float z) { return {x, y, z}; }

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: gen_physics_examples <examples folder>\n"); return 1; }
    RegisterPhysicsToyProperties(); detail::RegisterPhysicsToySerializers();
    const std::filesystem::path root = argv[1];

    {   // ---- physics_toys
        Scene s; Basics(s, "Physics toys", V(0, 1, 14));
        Label(s, "1  CONVEYOR BELT: crates ride the belt (speed in the Inspector)", V(-34, 4, 0));
        ConveyorBeltPreset(s, V(-34, 0, 0));
        for (int i = 0; i < 3; ++i) Piece(s, "Belt crate", V(-34, 0.8f + i * 1.1f, -3.0f), Vec3::One * 0.8f, Color::FromBytes(200, 140, 80), false, true, 1.0f);
        Label(s, "2  JUMP PAD: the ball bounces on it for ever", V(-26, 9, 0));
        JumpPadPreset(s, V(-26, 0, 0));
        Piece(s, "Pad ball", V(-26, 4, 0), Vec3::One * 0.8f, Color::FromBytes(240, 80, 80), true, true, 1.0f);
        Label(s, "3  ATTRACTOR and REPELLER: balls fall toward one, flee the other", V(-16, 8, 0));
        ForceFieldPreset(s, V(-20, 2.5f, 0), ForceField::Attract);
        ForceFieldPreset(s, V(-12, 2.5f, 0), ForceField::Repel);
        for (int i = 0; i < 4; ++i) { Piece(s, "Field ball", V(-20 + (i % 2 ? 3.5f : -3.5f), 0.5f, (i / 2 ? 1.5f : -1.5f)), Vec3::One * 0.8f, Color::FromBytes(90, 140, 255), true, true, 1.0f);
                                      Piece(s, "Field ball", V(-12 + (i % 2 ? 2.0f : -2.0f), 0.5f, (i / 2 ? 1.5f : -1.5f)), Vec3::One * 0.8f, Color::FromBytes(255, 140, 90), true, true, 1.0f); }
        Label(s, "4  GRAVITY WELL and WHIRLPOOL", V(-2, 8, 0));
        ForceFieldPreset(s, V(-4, 1.0f, 0), ForceField::GravityWell);
        ForceFieldPreset(s, V(4, 1.0f, 0), ForceField::Swirl);
        for (int i = 0; i < 3; ++i) { Piece(s, "Well ball", V(-4 + (i - 1) * 2.0f, 5.0f + i, 0), Vec3::One * 0.7f, Color::FromBytes(180, 90, 255), true, true, 1.0f);
                                      Piece(s, "Swirl ball", V(4 + (i - 1) * 2.5f, 0.5f, 2.0f), Vec3::One * 0.7f, Color::FromBytes(80, 220, 220), true, true, 1.0f); }
        Label(s, "5  GRAVITY ZONES: floating, moon gravity, normal", V(18, 9, 0));
        GravityZonePreset(s, V(12, 4, 0), Vec3::Zero);
        GravityZonePreset(s, V(20, 4, 0), V(0, -1.6f, 0));
        Piece(s, "Float ball", V(12, 5, 0), Vec3::One * 0.8f, Color::FromBytes(255, 230, 90), true, true, 1.0f);
        Piece(s, "Moon ball", V(20, 7.5f, 0), Vec3::One * 0.8f, Color::FromBytes(220, 220, 230), true, true, 1.0f);
        Piece(s, "Normal ball", V(26, 7.5f, 0), Vec3::One * 0.8f, Color::FromBytes(255, 90, 90), true, true, 1.0f);
        Label(s, "6  TURNTABLE carries the crates round", V(36, 6, 0));
        TurntablePreset(s, V(36, 0, 0));
        for (int i = 0; i < 4; ++i) Piece(s, "Turn crate", V(36 + (i % 2 ? 1.6f : -1.6f), 0.9f, (i / 2 ? 1.2f : -1.2f)), Vec3::One * 0.8f, Color::FromBytes(200, 170, 90), false, true, 1.0f);
        Label(s, "7  MOVING PLATFORM: stand on it and ride", V(46, 6, 0));
        { GameObject* p = MovingPlatformPreset(s, V(44, 0.5f, 0)); p->GetComponent<MovingPlatform>()->offset2 = V(8, 0, 0); }
        Piece(s, "Rider crate", V(44, 1.3f, 0), Vec3::One * 0.8f, Color::FromBytes(230, 120, 120), false, true, 1.0f);
        Label(s, "8  BOMB (3 s) and BREAKABLE CRATES", V(58, 8, 0));
        for (int y = 0; y < 3; ++y) for (int x = 0; x < 3 - y; ++x) BreakableCratePreset(s, V(56 + x * 1.05f + y * 0.52f, 0.5f + y * 1.02f, 0));
        BombPreset(s, V(57.04f, 6, 0));   // drops onto the top crate (about 7.8 m/s: it breaks), then goes off at 3 s
        Label(s, "9  PENDULUM, ROPE and WRECKING BALL", V(70, 10, 0));
        PendulumPreset(s, V(66, 9, 0), 4.0f);
        RopePreset(s, V(72, 9, 0), 8, 0.6f);
        Label(s, "10  SURFACES: trampoline, ice, sticky", V(84, 7, 0));
        SurfacePatchPreset(s, V(80, 0, 0), "Trampoline");
        Piece(s, "Tramp ball", V(80, 5, 0), Vec3::One * 0.8f, Color::FromBytes(255, 120, 150), true, true, 1.0f);
        SurfacePatchPreset(s, V(87, 0, 0), "Ice");
        { GameObject* c = Piece(s, "Ice crate", V(85.0f, 1.0f, 0), Vec3::One * 0.8f, Color::FromBytes(255, 255, 255), false, true, 1.0f); c->GetComponent<Rigidbody3D>()->velocity = V(3, 0, 0); }
        SurfacePatchPreset(s, V(94, 0, 0), "Sticky");
        { GameObject* c = Piece(s, "Sticky crate", V(92.0f, 1.0f, 0), Vec3::One * 0.8f, Color::FromBytes(255, 255, 255), false, true, 1.0f); c->GetComponent<Rigidbody3D>()->velocity = V(3, 0, 0); }
        WriteScene(s, root / "physics_toys" / "physics_toys.okayscene");
    }
    {   // ---- physics_materials: a ramp (with a crate) and a drop pad (with a ball) for every preset
        Scene s; Basics(s, "Physics materials", V(0, 1, 16));
        const std::vector<std::string> names = PhysicsMaterials::PresetNames();
        for (std::size_t i = 0; i < names.size(); ++i) {
            const float z = ((float)i - (float)names.size() * 0.5f) * 5.0f;
            Label(s, names[i], V(0, 5.2f, z));
            // ramp: 20 degrees up toward +x, 6 m long; the crate starts on its top surface 2.4 m up the slope
            GameObject* ramp = s.CreateGameObject(UniqueName(s, names[i] + " Ramp"));
            ramp->transform->localPosition = V(-4, 1.0f, z); ramp->transform->localRotation = Quat::Euler(0, 0, 20);
            ramp->transform->localScale = V(6, 0.4f, 3);
            auto* mr = ramp->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(1.0f); mr->color = Color::FromBytes(150, 150, 160);
            ramp->AddComponent<BoxCollider3D>()->size = Vec3::One;
            ramp->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
            AssignPhysicsMaterial(*ramp, names[i]);
            const float a = 20.0f * 3.14159265f / 180.0f;
            const Vec3 up{-std::sin(a), std::cos(a), 0}, dir{std::cos(a), std::sin(a), 0};
            GameObject* crate = Piece(s, names[i] + " crate", V(-4, 1.0f, z) + dir * 1.5f + up * 0.7f, Vec3::One * 0.8f, Color::FromBytes(235, 190, 90), false, true, 1.0f);
            crate->transform->localRotation = Quat::Euler(0, 0, 20);
            AssignPhysicsMaterial(*crate, "Default");
            // pad: a flat slab of this material and a ball (bounciness 0 of its own) dropped from 4 m
            SurfacePatchPreset(s, V(8, 0, z), names[i], V(4, 0.4f, 3.5f));
            Piece(s, names[i] + " ball", V(8, 4.0f, z), Vec3::One * 0.7f, Color::FromBytes(235, 90, 90), true, true, 1.0f);
        }
        Label(s, "LEFT: a plain crate on a ramp of each material (slides or stays).  RIGHT: a ball dropped on a pad of each material.", V(0, 9, 0));
        WriteScene(s, root / "physics_materials" / "physics_materials.okayscene");
    }
    {   // ---- physics_upgrade
        Scene s; Basics(s, "Physics upgrade", V(0, 1, 14));
        Label(s, "1  TOWER of ten crates stands and sleeps", V(-14, 12, 0));
        for (int i = 0; i < 10; ++i) Piece(s, "Tower crate", V(-14, 0.5f + i, 0), Vec3::One, Color::FromBytes((unsigned char)(170 + (i % 2) * 40), 120, 80), false, true, 1.0f);
        Label(s, "2  DOMINOES: the ball rolls down and topples the line", V(0, 5, -6));
        auto doms = DominoPackPreset(s, V(-4, 0, -6), 16, 0.6f);
        GameObject* ramp = s.CreateGameObject("Domino ramp"); ramp->transform->localPosition = V(-8.2f, 0.55f, -6); ramp->transform->localRotation = Quat::Euler(0, 0, -20);
        ramp->transform->localScale = V(4, 0.2f, 1.5f); { auto* mr = ramp->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(1.0f); mr->color = Color::FromBytes(150, 150, 160); }
        ramp->AddComponent<BoxCollider3D>()->size = Vec3::One; ramp->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        Piece(s, "Domino ball", V(-9.3f, 1.8f, -6), Vec3::One * 0.8f, Color::FromBytes(240, 80, 80), true, true, 4.0f);
        Label(s, "3  WRECKING BALL swings into a wall", V(20, 14, 0));
        {   // chain hung from (18, 12, 0), displaced 50 degrees so it swings into the wall at x = 24
            const Vec3 hook = V(24, 12, 0); const float len = 7.0f; const float a = 55.0f * 3.14159265f / 180.0f;
            GameObject* prev = nullptr; const int links = 6;
            for (int i = 0; i <= links; ++i) {
                const float d = len * (float)(i + 1) / (float)(links + 1) ;
                const bool big = i == links;
                GameObject* g = Piece(s, big ? "Wrecking Ball" : "Chain link " + std::to_string(i), hook + V(-std::sin(a) * (big ? len + 0.9f : d), -std::cos(a) * (big ? len + 0.9f : d), 0),
                                      big ? Vec3::One * 1.8f : Vec3::One * 0.3f, Color::FromBytes(70, 70, 80), true, true, big ? 50.0f : 0.5f);
                auto* j = g->AddComponent<Joint3D>(); j->mode = (int)Joint3D::Mode::Distance; j->autoConfigure = true;
                if (prev) j->connectedBody = prev->name; else j->anchor = hook;
                prev = g;
            }
            for (int y = 0; y < 5; ++y) for (int z = -1; z <= 1; ++z) Piece(s, "Wall crate", V(25.0f, 0.5f + y, z * 1.05f), Vec3::One, Color::FromBytes(190, 150, 100), false, true, 1.0f);
        }
        Label(s, "4  RAMPS: ice, wood, rubber hold or let go the same crate", V(40, 5, 0));
        const char* mats[3] = {"Ice", "Wood", "Rubber"};
        for (int i = 0; i < 3; ++i) {
            const float z = (float)(i - 1) * 4.0f;
            GameObject* ramp2 = s.CreateGameObject(UniqueName(s, std::string(mats[i]) + " Ramp")); ramp2->transform->localPosition = V(40, 1.0f, z); ramp2->transform->localRotation = Quat::Euler(0, 0, 20);
            ramp2->transform->localScale = V(6, 0.4f, 3); { auto* mr = ramp2->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(1.0f); mr->color = Color::FromBytes(150, 150, 160); }
            ramp2->AddComponent<BoxCollider3D>()->size = Vec3::One; ramp2->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static; AssignPhysicsMaterial(*ramp2, mats[i]);
            const float a = 20.0f * 3.14159265f / 180.0f; const Vec3 up{-std::sin(a), std::cos(a), 0}, dir{std::cos(a), std::sin(a), 0};
            GameObject* c = Piece(s, std::string(mats[i]) + " crate", V(40, 1.0f, z) + dir * 1.0f + up * 0.7f, Vec3::One * 0.8f, Color::FromBytes(235, 190, 90), false, true, 1.0f);
            c->transform->localRotation = Quat::Euler(0, 0, 20);
        }
        Label(s, "5  MOVING PLATFORM: walk onto it with the character", V(58, 6, 0));
        { GameObject* p = MovingPlatformPreset(s, V(54, 0.3f, 0)); p->GetComponent<MovingPlatform>()->offset2 = V(10, 0, 0); p->GetComponent<MovingPlatform>()->speed = 1.5f; }
        WriteScene(s, root / "physics" / "physics_upgrade.okayscene");
    }

    {   // ---- physics_engine: the Engine quality (our own engine core, docs/physics/physics_core.md)
        Scene s; Basics(s, "Physics engine", V(0, 1, 20));
        s.physics3D().quality = PhysicsQuality::Engine;
        Label(s, "ENGINE QUALITY: our own physics engine core (Project Settings > Physics > Physics quality)", V(0, 14, 0));
        Label(s, "1  TOWER of twelve crates and a PYRAMID of 36: they stand and fall asleep", V(-30, 13, 0));
        for (int i = 0; i < 12; ++i) Piece(s, "Tower crate", V(-34, 0.5f + i, 0), Vec3::One, Color::FromBytes((unsigned char)(170 + (i % 2) * 40), 120, 80), false, true, 1.0f);
        for (int row = 0; row < 8; ++row)
            for (int i = 0; i < 8 - row; ++i)
                Piece(s, "Pyramid crate", V(-30.0f + (float)i * 1.02f + (float)row * 0.51f, 0.5f + (float)row, 0), Vec3::One, Color::FromBytes(200, (unsigned char)(120 + row * 12), 90), false, true, 1.0f);
        Label(s, "2  CHAINS of limbs on cone-twist joints (a ragdoll's joints): they swing and stay attached", V(-6, 13, 0));
        for (int c = 0; c < 3; ++c) {
            std::string prev;
            for (int i = 0; i < 6; ++i) {
                GameObject* g = s.CreateGameObject(UniqueName(s, "Limb"));
                g->transform->localPosition = V(-10.0f + (float)c * 3.0f + (float)(i + 1) * 0.6f, 10.0f, (float)c);
                g->transform->localRotation = Quat::Euler(0, 0, 90);
                auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(1.0f); mr->color = Color::FromBytes(90, 160, 230);
                g->transform->localScale = V(0.15f, 0.6f, 0.15f);
                auto* cc = g->AddComponent<CapsuleCollider3D>(); cc->radius = 0.5f; cc->height = 1.0f;
                auto* rb = g->AddComponent<Rigidbody3D>(); rb->freezeRotation = false; rb->mass = 2.0f;
                auto* j = g->AddComponent<Joint3D>();
                j->mode = (int)Joint3D::Mode::ConeTwist;
                j->connectedBody = prev;
                j->anchor = V(-10.0f + (float)c * 3.0f + 0.3f + (float)i * 0.6f, 10.0f, (float)c);   // the near end of this limb
                j->axis = V(1, 0, 0);
                j->swingLimit1 = j->swingLimit2 = 35.0f;
                j->useLimits = true; j->minAngle = -20; j->maxAngle = 20;
                j->useDrive = c == 2; j->driveFrequency = 2.0f; j->driveMaxTorque = 60.0f;   // the third chain is powered: it holds itself out
                prev = g->name;
            }
        }
        Label(s, "3  POWERED ARM: a ball joint with a drive holds a 5 kg arm level", V(8, 9, 0));
        {
            GameObject* post = Piece(s, "Arm post", V(8, 2.5f, 0), V(0.4f, 5, 0.4f), Color::FromBytes(120, 120, 130));
            (void)post;
            GameObject* arm = s.CreateGameObject("Powered arm");
            arm->transform->localPosition = V(9.0f, 5.2f, 0);
            arm->transform->localScale = V(2.0f, 0.2f, 0.2f);
            auto* mr = arm->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(1.0f); mr->color = Color::FromBytes(240, 200, 80);
            arm->AddComponent<BoxCollider3D>()->size = Vec3::One;
            auto* rb = arm->AddComponent<Rigidbody3D>(); rb->freezeRotation = false; rb->mass = 5.0f;
            auto* j = arm->AddComponent<Joint3D>();
            j->mode = (int)Joint3D::Mode::Ball; j->anchor = V(8.0f, 5.2f, 0);
            j->useDrive = true; j->driveFrequency = 3.0f; j->driveDampingRatio = 1.0f; j->driveMaxTorque = 300.0f;
        }
        Label(s, "4  FAST SHOTS: 60 m/s balls never pass the 4 cm wall", V(22, 9, 0));
        {
            GameObject* wall = Piece(s, "Thin wall", V(30, 3, 0), V(0.04f, 6, 8), Color::FromBytes(200, 200, 220));
            (void)wall;
            for (int i = 0; i < 6; ++i) {
                GameObject* b = Piece(s, "Shot", V(16, 1.0f + (float)i * 0.8f, -2.0f + (float)(i % 3) * 2.0f), Vec3::One * 0.15f, Color::FromBytes(255, 80, 80), true, true, 0.2f);
                b->GetComponent<Rigidbody3D>()->velocity = V(60, 2, 0);
            }
        }
        Label(s, "5  SHAPES: capsule, cylinder, cone, hull and compound bodies settle", V(40, 6, 0));
        for (int i = 0; i < 5; ++i) {
            GameObject* g = s.CreateGameObject(UniqueName(s, "Shape"));
            g->transform->localPosition = V(36.0f + (float)i * 2.0f, 3.0f, 0);
            g->transform->localRotation = Quat::Euler(10.0f * (float)i, 20.0f, 15.0f);
            auto* mr = g->AddComponent<MeshRenderer>(); mr->color = Color::FromBytes(120, 220, 140);
            switch (i) {
                case 0: { mr->mesh = Mesh::Capsule(0.3f, 1.2f); auto* c = g->AddComponent<CapsuleCollider3D>(); c->radius = 0.3f; c->height = 1.2f; break; }
                case 1: { mr->mesh = Mesh::Cylinder(0.4f, 0.6f); auto* c = g->AddComponent<CylinderCollider3D>(); c->radius = 0.4f; c->height = 0.6f; break; }
                case 2: { mr->mesh = Mesh::Cube(0.8f); auto* c = g->AddComponent<ConeCollider3D>(); c->radius = 0.5f; c->height = 0.8f; break; }
                case 3: { mr->mesh = Mesh::Cube(0.8f); auto* c = g->AddComponent<ConvexHullCollider3D>(); c->SetFromMesh(Mesh::Cube(0.8f)); break; }
                default: { mr->mesh = Mesh::Cube(0.8f); auto* c = g->AddComponent<CompoundCollider3D>(); c->AddBox({0, 0, 0}, {1.0f, 0.2f, 1.0f}); c->AddSphere({0, 0.3f, 0}, 0.25f); break; }
            }
            auto* rb = g->AddComponent<Rigidbody3D>(); rb->freezeRotation = false;
        }
        WriteScene(s, root / "physics_engine" / "physics_engine.okayscene");
    }

    {   // ---- physics_engine_5k: 5,000 bodies poured into a pit (Engine quality)
        Scene s; Basics(s, "Physics engine 5k", V(0, 1, 40), false);
        s.physics3D().quality = PhysicsQuality::Engine;
        GameObject* cam = s.CreateGameObject("Camera");
        cam->transform->localPosition = V(0, 30, 45);
        cam->transform->localRotation = Quat::Euler(30, 180, 0);
        cam->AddComponent<Camera>();
        Label(s, "5,000 BODIES in the Engine physics quality (islands solved on every core)", V(0, 40, 0));
        for (int w = 0; w < 4; ++w) {   // the pit walls
            const float a = (float)w * 1.5707963f;
            Piece(s, "Pit wall", V(14.0f * std::sin(a), 4.0f, 14.0f * std::cos(a)), V(w % 2 ? 1.0f : 29.0f, 8.0f, w % 2 ? 29.0f : 1.0f), Color::FromBytes(110, 110, 120));
        }
        for (int i = 0; i < 5000; ++i) {
            const int layer = i / 400, cell = i % 400;
            Piece(s, i % 2 ? "Ball" : "Crate", V(-11.4f + (float)(cell % 20) * 1.2f, 2.0f + (float)layer * 1.2f, -11.4f + (float)(cell / 20) * 1.2f),
                  Vec3::One * 0.8f, Color::FromBytes((unsigned char)(100 + (i * 37) % 155), (unsigned char)(100 + (i * 91) % 155), 200), i % 2 == 1, true, 1.0f);
        }
        WriteScene(s, root / "physics_engine" / "physics_engine_5k.okayscene");
    }
    return 0;
}
