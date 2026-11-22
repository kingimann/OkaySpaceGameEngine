#pragma once
// Ready-made physics objects: each function puts one finished toy into a scene (mesh, collider, body, the toy component) and
// returns it, so GameObject > 3D Object > Physics Toys, the example scenes and tests all build them the same way.
// Written from this engine's own scene API; nothing outside code.
#include "okay/Components/Joint3D.hpp"
#include "okay/Components/NoCode.hpp"
#include "okay/Components/PhysicsToys.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <vector>

namespace okay {
namespace toypresets {

inline std::string UniqueName(Scene& scene, const std::string& base) {
    if (!scene.Find(base)) return base;
    for (int i = 2; i < 100000; ++i) { std::string n = base + " " + std::to_string(i); if (!scene.Find(n)) return n; }
    return base;
}

/// A visible block or ball with a collider. `dynamicBody` gives it a Rigidbody 3D (else it is a static piece of the world).
inline GameObject* Piece(Scene& scene, const std::string& name, const Vec3& pos, const Vec3& scale, const Color& color,
                         bool ball = false, bool dynamicBody = false, float mass = 1.0f) {
    GameObject* g = scene.CreateGameObject(UniqueName(scene, name));
    g->transform->localPosition = pos; g->transform->localScale = scale;
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = ball ? Mesh::Sphere(0.5f) : Mesh::Cube(1.0f);
    mr->color = color;
    if (ball) g->AddComponent<SphereCollider3D>()->radius = 0.5f; else g->AddComponent<BoxCollider3D>()->size = Vec3::One;
    auto* rb = g->AddComponent<Rigidbody3D>();
    rb->mass = mass;
    if (dynamicBody) rb->freezeRotation = false; else rb->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}

inline GameObject* Ground(Scene& scene, const Vec3& size = {60, 1, 60}, const Color& c = Color::FromBytes(150, 154, 150)) {
    return Piece(scene, "Ground", {0, -0.5f, 0}, size, c);
}
inline GameObject* ConveyorBeltPreset(Scene& s, const Vec3& at) {
    GameObject* g = Piece(s, "Conveyor Belt", at + Vec3{0, 0.15f, 0}, {3, 0.3f, 8}, Color::FromBytes(60, 60, 70));
    g->AddComponent<ConveyorBelt>()->speed = 2.0f;
    return g;
}
inline GameObject* JumpPadPreset(Scene& s, const Vec3& at) {
    GameObject* g = Piece(s, "Jump Pad", at + Vec3{0, 0.15f, 0}, {2, 0.3f, 2}, Color::FromBytes(60, 200, 90));
    g->AddComponent<JumpPad>()->force = 14.0f;
    return g;
}
inline GameObject* ForceFieldPreset(Scene& s, const Vec3& at, int mode = ForceField::Attract) {
    static const char* names[] = {"Attractor", "Repeller", "Gravity Well", "Whirlpool"};
    static const Color cols[] = {Color::FromBytes(90, 140, 255), Color::FromBytes(255, 120, 90), Color::FromBytes(180, 90, 255), Color::FromBytes(80, 220, 220)};
    const int m = std::max(0, std::min(3, mode));
    GameObject* g = s.CreateGameObject(UniqueName(s, names[m]));
    g->transform->localPosition = at; g->transform->localScale = {0.8f, 0.8f, 0.8f};
    auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Sphere(0.5f); mr->color = cols[m];
    auto* f = g->AddComponent<ForceField>(); f->mode = m; f->radius = 7.0f; f->strength = m == ForceField::GravityWell ? 60.0f : 20.0f;
    return g;
}
inline GameObject* GravityZonePreset(Scene& s, const Vec3& at, const Vec3& gravity = {0, -1.6f, 0}) {
    GameObject* g = s.CreateGameObject(UniqueName(s, gravity.SqrMagnitude() < 1e-6f ? "Zero-G Zone" : "Moon Gravity Zone"));
    g->transform->localPosition = at;
    auto* z = g->AddComponent<GravityZone>(); z->size = {8, 8, 8}; z->gravity = gravity;
    return g;
}
inline GameObject* TurntablePreset(Scene& s, const Vec3& at) {
    GameObject* g = s.CreateGameObject(UniqueName(s, "Turntable"));
    g->transform->localPosition = at + Vec3{0, 0.15f, 0}; g->transform->localScale = {6, 0.3f, 6};
    auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cylinder(0.5f, 1.0f); mr->color = Color::FromBytes(220, 170, 60);
    g->AddComponent<BoxCollider3D>()->size = Vec3::One;    // (a box round the disc keeps the turn simple and steady)
    auto* rb = g->AddComponent<Rigidbody3D>(); rb->bodyType = Rigidbody3D::BodyType::Kinematic; rb->freezeRotation = false;
    g->AddComponent<Turntable>()->degreesPerSecond = 30.0f;
    return g;
}
inline GameObject* MovingPlatformPreset(Scene& s, const Vec3& at) {
    GameObject* g = Piece(s, "Moving Platform", at + Vec3{0, 0.15f, 0}, {3, 0.3f, 3}, Color::FromBytes(90, 150, 220));
    g->GetComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Kinematic;
    g->AddComponent<MovingPlatform>();
    return g;
}
inline GameObject* BombPreset(Scene& s, const Vec3& at) {
    GameObject* g = Piece(s, "Bomb", at, {0.7f, 0.7f, 0.7f}, Color::FromBytes(40, 40, 40), true, true, 5.0f);
    auto* e = g->AddComponent<Explosion>(); e->onStart = true; e->delay = 3.0f; e->destroyAfter = true; e->force = 30.0f; e->radius = 8.0f;
    return g;
}
inline GameObject* BreakableCratePreset(Scene& s, const Vec3& at) {
    GameObject* g = Piece(s, "Breakable Crate", at, Vec3::One, Color::FromBytes(180, 120, 70), false, true, 4.0f);
    g->AddComponent<Breakable>();
    return g;
}
/// A slab of ground that feels like `material` (Ice, Sticky, Trampoline, Mud...). Colour follows the material.
inline GameObject* SurfacePatchPreset(Scene& s, const Vec3& at, const std::string& material, const Vec3& size = {6, 0.4f, 6}) {
    Color c = Color::FromBytes(170, 170, 170);
    if (material == "Ice" || material == "Slippery") c = Color::FromBytes(170, 220, 245);
    else if (material == "Rubber" || material == "Bouncy Ball") c = Color::FromBytes(40, 40, 45);
    else if (material == "Trampoline") c = Color::FromBytes(235, 90, 120);
    else if (material == "Mud") c = Color::FromBytes(110, 80, 50);
    else if (material == "Sand") c = Color::FromBytes(225, 205, 140);
    else if (material == "Sticky") c = Color::FromBytes(120, 200, 80);
    else if (material == "Metal") c = Color::FromBytes(140, 145, 155);
    else if (material == "Wood") c = Color::FromBytes(160, 110, 60);
    else if (material == "Stone") c = Color::FromBytes(120, 120, 115);
    else if (material == "Glass") c = Color::FromBytes(150, 210, 220);
    else if (material == "Cloth") c = Color::FromBytes(200, 150, 190);
    GameObject* g = Piece(s, material + " Patch", at + Vec3{0, size.y * 0.5f, 0}, size, c);
    AssignPhysicsMaterial(*g, material);
    return g;
}
/// A weight hanging from a point on a rigid rod: swings for ever (a little energy is lost each swing).
inline GameObject* PendulumPreset(Scene& s, const Vec3& at, float length = 4.0f) {
    GameObject* g = Piece(s, "Pendulum", at + Vec3{length * 0.7f, -length * 0.7f, 0}, {1, 1, 1}, Color::FromBytes(200, 60, 60), true, true, 5.0f);
    auto* j = g->AddComponent<Joint3D>(); j->mode = (int)Joint3D::Mode::Distance; j->anchor = at; j->autoConfigure = true;
    return g;
}
/// A chain of linked balls hanging from a point. Returns the last link.
inline GameObject* RopePreset(Scene& s, const Vec3& at, int links = 8, float spacing = 0.6f) {
    GameObject* prev = nullptr;
    const std::string base = UniqueName(s, "Rope link");
    for (int i = 0; i < links; ++i) {
        GameObject* g = Piece(s, base + " " + std::to_string(i), at + Vec3{0.0f, -spacing * (float)(i + 1), 0.0f}, {0.35f, 0.35f, 0.35f}, Color::FromBytes(180, 150, 90), true, true, 0.5f);
        auto* j = g->AddComponent<Joint3D>(); j->mode = (int)Joint3D::Mode::Distance; j->autoConfigure = true;
        if (prev) j->connectedBody = prev->name; else j->anchor = at;
        prev = g;
    }
    return prev;
}
/// A heavy ball (50 kg) on a chain, ready to swing into a wall.
inline GameObject* WreckingBallPreset(Scene& s, const Vec3& at, int links = 6) {
    GameObject* last = RopePreset(s, at, links, 0.7f);
    GameObject* ball = Piece(s, "Wrecking Ball", last->transform->Position() + Vec3{0, -0.9f, 0}, {1.6f, 1.6f, 1.6f}, Color::FromBytes(60, 60, 70), true, true, 50.0f);
    auto* j = ball->AddComponent<Joint3D>(); j->mode = (int)Joint3D::Mode::Distance; j->autoConfigure = true; j->connectedBody = last->name;
    return ball;
}
/// A row of dominoes (tall thin boxes) standing in a line along +x; push the first one.
inline std::vector<GameObject*> DominoPackPreset(Scene& s, const Vec3& at, int count = 14, float spacing = 0.6f) {
    std::vector<GameObject*> out;
    for (int i = 0; i < count; ++i)
        out.push_back(Piece(s, "Domino", at + Vec3{spacing * (float)i, 0.6f, 0}, {0.15f, 1.2f, 0.7f}, Color::FromBytes((unsigned char)(80 + 12 * (i % 10)), 110, 200), false, true, 0.5f));
    return out;
}
/// A door that swings on a hinge: a slab on a hinge joint with stops (0 to 100 degrees).
inline GameObject* DoorPreset(Scene& s, const Vec3& at) {
    GameObject* g = Piece(s, "Door", at + Vec3{0.6f, 1.0f, 0}, {1.2f, 2.0f, 0.12f}, Color::FromBytes(150, 100, 60), false, true, 8.0f);
    auto* j = g->AddComponent<Joint3D>(); j->mode = (int)Joint3D::Mode::Hinge; j->anchor = at + Vec3{0, 1.0f, 0}; j->axis = {0, 1, 0};
    j->useLimits = true; j->minAngle = 0.0f; j->maxAngle = 100.0f;
    return g;
}

} // namespace toypresets
} // namespace okay
