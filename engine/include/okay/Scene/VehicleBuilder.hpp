#pragma once
// Builds ready-to-drive vehicles out of cubes and cylinders (no assets needed): the body, cabin, wheels that roll and
// steer, lights, a driver seat and a passenger seat, and a VehicleController tuned by a preset.
// Written from scratch for this engine. Header only.
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleSeat.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Render/Mesh.hpp"
#include <string>

namespace okay {

namespace VehicleBuilder {

/// Size and look of one kind of vehicle (metres). Used by Make().
struct Shape {
    const char* name;        ///< also the preset name
    float length, width, bodyH;   ///< body slab
    float cabinLen, cabinH;       ///< glass cabin on top (0 = none: buggy)
    float cabinBack;              ///< 0 = cabin centred, 1 = cabin at the back (truck: bed behind)
    float wheelR;
    float cabinZ;                 ///< centre of the cabin along the length, as a share of the length (-0.5 .. 0.5)
};

inline const std::vector<Shape>& Shapes() {
    static const std::vector<Shape> s = {
        {"Sedan",       4.4f, 1.8f, 0.55f, 2.3f, 0.55f, 0.0f, 0.34f, -0.05f},
        {"Sports Car",  4.2f, 1.9f, 0.45f, 1.6f, 0.42f, 0.0f, 0.34f, -0.08f},
        {"Muscle Car",  4.8f, 1.95f, 0.55f, 1.9f, 0.5f, 0.0f, 0.36f, -0.1f},
        {"Pickup",      5.2f, 2.0f, 0.75f, 1.6f, 0.65f, 0.0f, 0.42f, 0.14f},
        {"Heavy Truck", 7.5f, 2.5f, 1.1f, 2.0f, 1.3f, 0.0f, 0.55f, 0.3f},
        {"Bus",         9.5f, 2.5f, 1.3f, 8.6f, 1.2f, 0.0f, 0.5f, 0.0f},
        {"Buggy",       3.4f, 1.9f, 0.35f, 0.0f, 0.0f, 0.0f, 0.42f, 0.0f},
        {"Rally Car",   4.0f, 1.85f, 0.55f, 2.0f, 0.5f, 0.0f, 0.34f, -0.05f},
        {"Drift Car",   4.4f, 1.8f, 0.5f, 1.9f, 0.45f, 0.0f, 0.33f, -0.05f},
    };
    return s;
}

inline const Shape* FindShape(const std::string& n) { for (const Shape& s : Shapes()) if (n == s.name) return &s; return nullptr; }

/// Make a vehicle of type `type` (a preset name) at `pos`, facing +Z turned by `yawDeg`. The car root is unscaled so
/// everything below it is in metres. Returns null for an unknown type. `chaseCamera` lets the vehicle move the scene's
/// camera while it is driven. `seats` adds the driver seat (and a passenger seat) so a walking player can get in with E.
inline GameObject* Make(Scene& scene, const std::string& type, Vec3 pos, float yawDeg, Color paint, bool seats = true, bool chaseCamera = false) {
    const Shape* sh = FindShape(type);
    if (!sh) return nullptr;
    auto rgb = [](int r, int g, int b, int a = 255) { return Color::FromBytes(r, g, b, a); };
    GameObject* car = scene.CreateGameObject(type);
    car->transform->localPosition = pos + Vec3{0, sh->wheelR, 0};
    car->transform->localRotation = Quat::Euler(0, yawDeg, 0);
    Transform* ct = car->transform;
    car->AddComponent<Rigidbody3D>();
    const float hl = sh->length * 0.5f, hw = sh->width * 0.5f;
    const float bodyTop = sh->bodyH - sh->wheelR * 0.4f;
    car->AddComponent<BoxCollider3D>()->size = {sh->width, sh->bodyH, sh->length};

    auto block = [&](const char* name, Vec3 p, Vec3 size, Color col) {
        GameObject* o = scene.CreateGameObject(name);
        o->transform->SetParent(ct, false);
        o->transform->localPosition = p; o->transform->localScale = size;
        auto* mr = o->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(); mr->color = col;
        return o;
    };
    const Color dark = rgb(38, 38, 42), glass = rgb(150, 205, 235, 70);
    block("Car Body", {0, bodyTop * 0.5f - sh->wheelR * 0.3f, 0}, {sh->width, sh->bodyH * 0.6f, sh->length}, paint);
    block("Bumper Front", {0, 0.0f, hl}, {sh->width + 0.04f, sh->bodyH * 0.35f, 0.12f}, dark);
    block("Bumper Rear", {0, 0.0f, -hl}, {sh->width + 0.04f, sh->bodyH * 0.35f, 0.12f}, dark);
    const float top = bodyTop * 0.5f - sh->wheelR * 0.3f + sh->bodyH * 0.3f;
    if (sh->cabinLen > 0.0f) {
        const float cz = sh->cabinZ * sh->length;
        block("Cabin", {0, top + sh->cabinH * 0.5f, cz}, {sh->width * 0.92f, sh->cabinH, sh->cabinLen}, glass);
        block("Roof", {0, top + sh->cabinH, cz}, {sh->width * 0.94f, 0.06f, sh->cabinLen + 0.04f}, paint);
        if (type == "Pickup") block("Bed", {0, top + 0.1f, -hl * 0.55f}, {sh->width * 0.95f, 0.25f, sh->length * 0.38f}, dark);
    } else {
        block("Roll Cage", {0, top + 0.5f, 0}, {sh->width * 0.9f, 0.08f, 0.08f}, dark);   // the buggy is open
        block("Seat Block", {0, top + 0.2f, -0.2f}, {0.9f, 0.4f, 0.5f}, rgb(70, 74, 86));
    }
    const Color lamp = rgb(250, 240, 190), tail = rgb(220, 30, 30);
    for (int side = -1; side <= 1; side += 2) {
        GameObject* h = block(side < 0 ? "Headlight Left" : "Headlight Right", {side * hw * 0.65f, top - 0.1f, hl + 0.01f}, {0.34f, 0.13f, 0.06f}, lamp);
        h->GetComponent<MeshRenderer>()->emissive = rgb(120, 110, 70);
        GameObject* t = block(side < 0 ? "Taillight Left" : "Taillight Right", {side * hw * 0.7f, top - 0.1f, -hl - 0.01f}, {0.3f, 0.12f, 0.06f}, tail);
        t->GetComponent<MeshRenderer>()->emissive = rgb(110, 10, 10);
    }
    // Wheels: "Wheel ..." empty at the axle (turns for steering), "Tire" child cylinder (rolls). VehicleController animates them.
    const char* wname[4] = {"Wheel Front Left", "Wheel Front Right", "Wheel Rear Left", "Wheel Rear Right"};
    const float wx[4] = {-1, 1, -1, 1}, wzs[4] = {1, 1, -1, -1};
    const float axle = sh->length * 0.5f - sh->wheelR * 1.9f;
    for (int i = 0; i < 4; ++i) {
        GameObject* w = scene.CreateGameObject(wname[i]);
        w->transform->SetParent(ct, false);
        w->transform->localPosition = {wx[i] * (hw - 0.1f), 0.0f, wzs[i] * axle};
        GameObject* tire = scene.CreateGameObject("Tire");
        tire->transform->SetParent(w->transform, false);
        tire->transform->localScale = {sh->wheelR * 2.0f, 0.25f + sh->wheelR * 0.2f, sh->wheelR * 2.0f};
        tire->transform->localRotation = Quat::Euler(0.0f, 0.0f, 90.0f);
        auto* tmr = tire->AddComponent<MeshRenderer>(); tmr->mesh = Mesh::Cylinder(); tmr->color = rgb(24, 24, 27);
        GameObject* rim = scene.CreateGameObject("Rim");
        rim->transform->SetParent(tire->transform, false);
        rim->transform->localScale = {0.62f, 1.06f, 0.62f};
        auto* rmr = rim->AddComponent<MeshRenderer>(); rmr->mesh = Mesh::Cylinder(); rmr->color = rgb(190, 192, 198);
        GameObject* bar = scene.CreateGameObject("Rim Bar");
        bar->transform->SetParent(tire->transform, false);
        bar->transform->localScale = {0.95f, 1.1f, 0.14f};
        auto* bmr = bar->AddComponent<MeshRenderer>(); bmr->mesh = Mesh::Cube(); bmr->color = rgb(70, 72, 80);
    }

    auto* vc = car->AddComponent<VehicleController>();
    vc->ApplyPreset(type);
    vc->wheelBase = axle * 2.0f;
    vc->trackWidth = hw * 2.0f - 0.2f;
    vc->followCamera = chaseCamera;
    if (seats) {
        const float sz = sh->cabinLen > 0.0f ? sh->cabinZ * sh->length + sh->cabinLen * 0.15f : -0.2f;
        const float sy = top - sh->wheelR * 0.2f;
        auto* ds = car->AddComponent<VehicleSeat>();     // the -X front seat: the car's right side seen from behind
        ds->sitPoint = {-hw * 0.5f, sy, sz};
        ds->exitPoint = {-hw - 0.8f, -0.3f, sz};
        ds->useRadius = 2.6f; ds->prompt = "Drive";
        GameObject* pg = scene.CreateGameObject("Passenger Seat");
        pg->transform->SetParent(ct, false);
        auto* ps = pg->AddComponent<VehicleSeat>();
        ps->driver = false; ps->sitPoint = {hw * 0.5f, sy, sz}; ps->exitPoint = {hw + 0.8f, -0.3f, sz};
        ps->useRadius = 2.6f; ps->prompt = "Ride";
    }
    return car;
}

} // namespace VehicleBuilder

} // namespace okay
