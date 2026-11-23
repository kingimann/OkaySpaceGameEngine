#pragma once
// Shared physics test scenes (tests only).
//  - BuildMixedBodies: a floor and `bodies` mixed bodies (box, sphere, capsule, cylinder, compound) in a grid of layers,
//    each with its own small start speed. Used by jobs_adoption (Classic / Improved, serial against parallel) and
//    physics_engine_mode (Engine quality, determinism and timing).
//  - PutBytes: append a value's raw bytes (bit-for-bit comparisons).
#include <Okay.hpp>
#include "okay/Physics/ColliderShapes3D.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace okaytest {

template <class T> void PutBytes(std::vector<unsigned char>& out, const T& v) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

/// `freeRotation` unticks Freeze Rotation on every body (the bodies turn).
inline void BuildMixedBodies(okay::Scene& s, int bodies, okay::PhysicsQuality quality, bool freeRotation = false) {
    using namespace okay;
    s.physics3D().quality = quality;
    GameObject* floor = s.CreateGameObject("Floor");
    floor->transform->localPosition = {0, -0.5f, 0};
    floor->AddComponent<BoxCollider3D>()->size = {200, 1, 200};
    floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    const int side = (int)std::ceil(std::sqrt((double)bodies / 4.0));
    for (int i = 0; i < bodies; ++i) {
        GameObject* g = s.CreateGameObject("B" + std::to_string(i));
        const int layer = i / (side * side), cell = i % (side * side);
        const float x = (float)(cell % side) * 1.05f - side * 0.5f, z = (float)(cell / side) * 1.05f - side * 0.5f;
        g->transform->localPosition = {x + 0.07f * (float)(i % 3), 0.6f + (float)layer * 1.1f, z};
        g->transform->localRotation = Quat::Euler(0.0f, (float)(i * 13 % 90), (float)(i * 7 % 20));
        switch (i % 5) {
            case 0: g->AddComponent<BoxCollider3D>()->size = {0.9f, 0.9f, 0.9f}; break;
            case 1: g->AddComponent<SphereCollider3D>()->radius = 0.45f; break;
            case 2: { auto* c = g->AddComponent<CapsuleCollider3D>(); c->radius = 0.3f; c->height = 1.0f; break; }
            case 3: { auto* c = g->AddComponent<CylinderCollider3D>(); c->radius = 0.4f; c->height = 0.8f; break; }
            default: {   // a compound (bounds done on the calling thread: it caches its parsed parts)
                auto* c = g->AddComponent<CompoundCollider3D>();
                c->AddBox({0, 0, 0}, {0.8f, 0.3f, 0.8f});
                c->AddSphere({0, 0.3f, 0}, 0.25f);
                break;
            }
        }
        auto* rb = g->AddComponent<Rigidbody3D>();
        rb->velocity = {0.1f * (float)(i % 7) - 0.3f, 0.0f, 0.05f * (float)(i % 5)};
        if (freeRotation) rb->freezeRotation = false;
    }
}

} // namespace okaytest
