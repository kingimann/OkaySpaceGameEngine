#pragma once
// The water example scenes (examples/water/) and the test scenes use the same builders, so what the owner opens is what the
// tests rendered. Everything is made from our own blocks (cubes, spheres) and the Terrain and Water components.
// docs/rendering/water_visuals.md.
#include <Okay.hpp>
#include "okay/Components/Water.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/Buoyancy.hpp"
#include <cmath>
#include <string>

namespace watersc {
using namespace okay;

inline GameObject* Block(Scene& s, const char* name, Vec3 p, Vec3 sc, Color c) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = p;
    g->transform->localScale = sc;
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(1.0f);
    mr->color = c;
    return g;
}
inline GameObject* Rock(Scene& s, const char* name, Vec3 p, float r) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = p;
    g->transform->localScale = {r * 1.3f, r, r * 1.1f};
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Sphere(1.0f, 8, 12);
    mr->color = Color::FromBytes(112, 108, 102);
    return g;
}
inline GameObject* Crate(Scene& s, const char* name, Vec3 p, float mass = 0.4f) {
    GameObject* g = Block(s, name, p, {1, 1, 1}, Color::FromBytes(176, 124, 66));
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    auto* rb = g->AddComponent<Rigidbody3D>();
    rb->mass = mass; rb->allowSleep = false;
    g->AddComponent<Buoyancy>();
    return g;
}
inline void SunAndCamera(Scene& s, Vec3 eye, Vec3 target) {
    GameObject* sun = s.CreateGameObject("Sun");
    sun->transform->localRotation = Quat::Euler(38.0f, -35.0f, 0.0f);
    sun->AddComponent<Light>();
    GameObject* cam = s.CreateGameObject("Main Camera");
    auto* c = cam->AddComponent<Camera>();
    c->main = true; c->farClip = 600.0f;
    cam->transform->localPosition = eye;
    cam->transform->LookAt(target, {0, 1, 0});
    s.mainCamera = c;
}
inline void Note(Scene& s, const std::string& text) {
    GameObject* g = s.CreateGameObject("What to look for");
    auto* t = g->AddComponent<TextRenderer>();
    t->text = text;
    t->screenSpace = true; t->anchor = UIAnchor::StretchHorizontal;
    t->screenPos = {12, 12}; t->size = {420, 120}; t->vcenter = false; t->wrap = true;
    t->pixelSize = 2.0f; t->outline = true;
}
/// A bowl-shaped bed: height(x, z) in local metres, written into a Terrain of `size` metres.
template <class F>
inline Terrain* Ground(Scene& s, float size, int res, F height, Color col = Color::FromBytes(150, 138, 104)) {
    GameObject* g = s.CreateGameObject("Ground");
    auto* t = g->AddComponent<Terrain>();
    t->size = size; t->Resize(res); t->autoColor = false; t->color = col;
    const float cell = size / res;
    for (int z = 0; z <= res; ++z)
        for (int x = 0; x <= res; ++x) t->SetHeight(x, z, height(x * cell - size * 0.5f, z * cell - size * 0.5f));
    t->Apply();
    return t;
}
inline Water* MakeWater(Scene& s, int preset, float size, int resolution, float depth, Vec3 at = {0, 0, 0}) {
    GameObject* g = s.CreateGameObject("Water");
    g->transform->localPosition = at;
    auto* w = g->AddComponent<Water>();
    w->size = size; w->resolution = resolution; w->depth = depth;
    w->ApplyPreset(preset);
    w->Apply();
    return w;
}

/// Calm Lake: a bowl of ground, a wooden dock, a rock, a rowing target and floating crates.
inline void BuildLake(Scene& s) {
    Ground(s, 120.0f, 48, [](float x, float z) {
        const float r = std::sqrt(x * x + z * z);
        return -4.0f + 6.5f * std::pow(std::min(1.0f, r / 44.0f), 2.2f) + 0.35f * std::sin(x * 0.21f) * std::cos(z * 0.17f);
    });
    MakeWater(s, 1, 120.0f, 64, 6.0f)->lod = true;
    for (int i = 0; i < 8; ++i)      // the dock: planks out over the water and a few posts
        Block(s, "Dock Plank", {-30.0f + i * 1.0f, 0.55f, -2.0f}, {0.95f, 0.18f, 3.0f}, Color::FromBytes(150, 108, 66));
    for (int i = 0; i < 4; ++i) {
        Block(s, "Dock Post", {-30.0f + i * 2.4f, -0.4f, -3.3f}, {0.3f, 2.0f, 0.3f}, Color::FromBytes(104, 76, 48));
        Block(s, "Dock Post", {-30.0f + i * 2.4f, -0.4f, -0.7f}, {0.3f, 2.0f, 0.3f}, Color::FromBytes(104, 76, 48));
    }
    Rock(s, "Rock", {8.0f, -0.2f, 10.0f}, 2.2f);
    Rock(s, "Small Rock", {13.0f, -0.4f, 6.0f}, 1.0f);
    GameObject* tgt = Block(s, "Rowing Target", {-8.0f, 0.4f, 8.0f}, {2.2f, 0.25f, 2.2f}, Color::FromBytes(210, 60, 52));
    tgt->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    auto* rb = tgt->AddComponent<Rigidbody3D>(); rb->mass = 0.5f; rb->allowSleep = false;
    tgt->AddComponent<Buoyancy>();
    Crate(s, "Crate 1", {-4.0f, 0.5f, -6.0f}); Crate(s, "Crate 2", {-2.0f, 0.5f, -7.5f}); Crate(s, "Crate 3", {3.0f, 0.5f, -5.0f});
    SunAndCamera(s, {-36.0f, 3.2f, -12.0f}, {0.0f, 0.0f, 2.0f});
    Note(s, "CALM LAKE. Look at the water: it is never flat. Small ripples drift in two directions, the colour goes from turquoise at "
            "the shore to deep blue, the sky is mirrored in it. Drop a crate in (select it and press Play, then move it up) to see rings.");
}

/// Ocean: travelling waves with sharp crests and foam; the sea follows the camera (no edge). Change Sea State in the Inspector.
inline void BuildOcean(Scene& s) {
    Water* w = MakeWater(s, 3, 200.0f, 64, 30.0f);
    w->SetSeaState(3);
    for (int i = 0; i < 6; ++i)
        Crate(s, "Floating Crate", {-6.0f + i * 3.0f, 1.0f, 6.0f - (i % 2) * 5.0f}, 0.35f);
    SunAndCamera(s, {0.0f, 4.5f, -22.0f}, {0.0f, 0.0f, 10.0f});
    Note(s, "OCEAN. Waves roll in with sharp crests and white caps. Select Water and move Sea State: Calm, Light, Rough, Storm. "
            "The crates ride the same waves (they use the same wave maths).");
}

/// River: a channel with banks, a current that carries ripples and crates downstream.
inline void BuildRiver(Scene& s) {
    Ground(s, 100.0f, 64, [](float x, float z) {
        const float bank = std::fabs(z + 3.0f * std::sin(x * 0.07f));
        return -2.2f + 4.0f * std::pow(std::min(1.0f, bank / 10.0f), 1.6f);
    }, Color::FromBytes(120, 128, 82));
    Water* w = MakeWater(s, 5, 100.0f, 64, 3.0f);
    w->current = {2.5f, 0.0f};
    Rock(s, "Rock", {-4.0f, 0.0f, -3.0f}, 1.4f);
    Rock(s, "Rock", {12.0f, 0.0f, 2.0f}, 1.0f);
    for (int i = 0; i < 4; ++i) Crate(s, "Crate", {-30.0f + i * 4.0f, 0.5f, -1.5f + (i % 2) * 3.0f}, 0.35f);
    SunAndCamera(s, {-38.0f, 6.0f, -14.0f}, {0.0f, 0.0f, 0.0f});
    Note(s, "RIVER. The water flows to the right (Current 2.5 m/s). Ripples and foam travel downstream and the crates drift with it. "
            "Foam gathers where the water is shallow near the banks and the rocks.");
}

/// Shore: a beach that slopes into the sea, so foam surges along the waterline and the water goes from clear to deep.
inline void BuildShore(Scene& s) {
    Ground(s, 160.0f, 64, [](float x, float z) {
        return -7.0f + 0.16f * (x + 80.0f) + 0.25f * std::sin(z * 0.15f) + 0.12f * std::sin(x * 0.4f + z * 0.3f);
    }, Color::FromBytes(206, 190, 150));
    Water* w = MakeWater(s, 7, 160.0f, 64, 0.0f);
    w->SetSeaState(2); w->waveHeight = 0.5f; w->shoreFoamWidth = 2.0f; w->foamAmount = 0.6f;
    Rock(s, "Shore Rock", {-24.0f, 0.0f, 8.0f}, 2.0f);
    Rock(s, "Shore Rock", {-30.0f, 0.5f, -10.0f}, 1.6f);
    SunAndCamera(s, {-72.0f, 4.0f, -16.0f}, {-32.0f, 0.0f, 4.0f});
    Note(s, "SHORE. The water is clear and turquoise near the sand and goes deep blue further out. A white foam line surges "
            "along the waterline and around the rocks.");
}

} // namespace watersc
