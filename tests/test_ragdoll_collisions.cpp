#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Math/Geometry.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

using namespace okay;
using namespace okay::dyn;

// The ragdoll's limbs are real bodies in the scene's physics: they are stopped by dynamic bodies and push them
// (and are pushed back), they do not pass through each other, and a body dropped on loose boxes ends up lying
// on them. Each test here failed while the limbs only met the world through rays (docs/gameplay/active_ragdoll.md).
namespace {
enum { Hips = 0, Torso, Head, LUpperArm, LForeArm, LHand, RUpperArm, RForeArm, RHand,
       LThigh, LShin, LFoot, RThigh, RShin, RFoot };

struct World {
    Scene sc{"ragdoll collisions"};
    GameObject* body = nullptr;
    ActiveRagdoll* ar = nullptr;
    std::vector<GameObject*> crates;

    explicit World(Vec3 at = Vec3::Zero) {
        GameObject* g = sc.CreateGameObject("Ground");
        g->transform->localPosition = {0, -0.5f, 0};
        g->AddComponent<BoxCollider3D>()->size = {40, 1, 40};
        body = sc.CreateGameObject("Body");
        body->transform->localPosition = at;
        body->AddComponent<Character>()->Apply();
        ar = body->AddComponent<ActiveRagdoll>();
    }
    GameObject* Crate(Vec3 centre, float size, float mass) {
        GameObject* c = sc.CreateGameObject("Crate");
        c->transform->localPosition = centre;
        c->AddComponent<BoxCollider3D>()->size = {size, size, size};
        auto* rb = c->AddComponent<Rigidbody3D>();
        rb->mass = mass;
        crates.push_back(c);
        return c;
    }
    void Run(float seconds, const std::function<void()>& each = nullptr) {
        for (float e = 0.0f; e < seconds - 1e-5f; e += 1.0f / 60.0f) {
            sc.Update(1.0f / 60.0f);
            if (each) each();
        }
    }
    // The two ends of a limb's capsule (its long axis is the body's +Y).
    void Ends(int bone, Vec3& a, Vec3& b) const {
        const BodyState s = ar->Core().Body(bone);
        const Vec3 ax = s.rotation * Vec3::Up * ar->Core().BodyHalfLength(bone);
        a = s.position - ax;
        b = s.position + ax;
    }
    // How far the surfaces of two limbs are apart (negative = they overlap).
    float Gap(int b1, int b2) const {
        Vec3 a1, e1, a2, e2, c1, c2;
        Ends(b1, a1, e1);
        Ends(b2, a2, e2);
        return std::sqrt(Geom::SegmentSegment(a1, e1, a2, e2, c1, c2)) - ar->Core().BodyRadius(b1) - ar->Core().BodyRadius(b2);
    }
    // How deep the centre line of a limb is inside a crate (0 = outside), metres.
    float DepthInCrate(int bone, const GameObject* crate, float size) const {
        Vec3 a, b;
        Ends(bone, a, b);
        float deepest = 0.0f;
        for (int i = 0; i <= 4; ++i) {
            const Vec3 w = Vec3::Lerp(a, b, (float)i / 4.0f);
            const Vec3 l = crate->transform->Rotation().Inverse() * (w - crate->transform->Position());
            const float in = std::fmin(size * 0.5f - std::fabs(l.x), std::fmin(size * 0.5f - std::fabs(l.y), size * 0.5f - std::fabs(l.z)));
            deepest = std::fmax(deepest, in);
        }
        return deepest;
    }
    float Speed() const {
        float m = 0.0f;
        for (int b = 0; b < kBones; ++b) m = std::fmax(m, ar->Core().Body(b).velocity.Magnitude());
        return m;
    }
};
} // namespace

int main() {
    // ---- 1. A falling body meets a dynamic crate: the crate is pushed, and it pushes back ----
    {
        // The blocky Character faces -Z. A light crate stands in front of it; the body is thrown at it.
        float moved[2] = {0, 0}, deepest[2] = {0, 0}, crateSpeed[2] = {0, 0}, torsoZ[2] = {0, 0};
        const float masses[2] = {4.0f, 400.0f};
        for (int k = 0; k < 2; ++k) {
            World w;
            const float size = 0.6f;
            GameObject* crate = w.Crate({0, size * 0.5f, -0.75f}, size, masses[k]);
            w.sc.Start();
            w.Run(0.3f);
            CHECK(w.ar->Ready());
            const Vec3 start = crate->transform->Position();
            w.ar->Push(Vec3{0, 0, -160.0f});
            w.ar->Trigger("die");
            w.Run(4.0f, [&] {
                for (int b = 0; b < kBones; ++b) deepest[k] = std::fmax(deepest[k], w.DepthInCrate(b, crate, size));
                crateSpeed[k] = std::fmax(crateSpeed[k], crate->GetComponent<Rigidbody3D>()->velocity.Magnitude());
            });
            moved[k] = Vec3::Distance(start, crate->transform->Position());
            torsoZ[k] = w.ar->Core().Body(Torso).position.z;
            std::cout << "  crate " << masses[k] << " kg: moved " << moved[k] << " m, top speed " << crateSpeed[k]
                      << " m/s, deepest limb centre line inside it " << deepest[k] << " m, torso ends at z " << torsoZ[k] << "\n";
        }
        CHECK(moved[0] > 0.10f);                 // the light crate is shoved away
        CHECK(crateSpeed[0] > 0.3f);
        CHECK(crateSpeed[1] > 0.005f);           // the heavy one feels the hit too
        CHECK(moved[1] < moved[0]);              // but gives way less: it pushes back
        CHECK(deepest[0] < 0.06f);               // no limb ends up inside a crate
        CHECK(deepest[1] < 0.06f);
        CHECK(torsoZ[1] > -0.75f);               // the heavy crate stopped the body: the chest never got past its middle
    }

    // ---- 2. A leg does not pass through the other leg or through the torso ----
    {
        World w;
        w.sc.Start();
        w.Run(0.3f);
        w.ar->Trigger("die");
        w.Run(3.0f);                             // lying limp on the ground
        CHECK(w.ar->State() == CharState::Dead);
        dyn::RagdollPhysics* phys = w.ar->Core().Physics();
        // Sweep the left leg sideways into and across the right leg, again and again.
        float legGap = 1e9f, torsoGap = 1e9f;
        for (int i = 0; i < 150; ++i) {
            const BodyState l = phys->State(LShin), r = phys->State(RShin);
            Vec3 d = r.position - l.position;
            if (d.Magnitude() < 1e-4f) d = Vec3::Right;
            d = d.Normalized();
            phys->ApplyImpulse(LShin, d * 1.2f, l.position);
            phys->ApplyImpulse(LFoot, d * 0.4f, phys->State(LFoot).position);
            w.Run(1.0f / 60.0f);
            legGap = std::fmin(legGap, std::fmin(w.Gap(LShin, RShin), std::fmin(w.Gap(LShin, RThigh), w.Gap(LThigh, RShin))));
        }
        // Fold the left leg up into the chest.
        for (int i = 0; i < 150; ++i) {
            const BodyState l = phys->State(LShin), t = phys->State(Torso);
            Vec3 d = t.position - l.position;
            if (d.Magnitude() < 1e-4f) d = Vec3::Up;
            d = d.Normalized();
            phys->ApplyImpulse(LShin, d * 1.5f, l.position);
            w.Run(1.0f / 60.0f);
            torsoGap = std::fmin(torsoGap, std::fmin(w.Gap(LShin, Torso), w.Gap(LShin, Hips)));
        }
        std::cout << "  self collision: smallest gap leg to leg " << legGap << " m, shin to torso or pelvis " << torsoGap << " m\n";
        CHECK(legGap > -0.03f);                  // (the shins are 0.07 m thick: passing through reads -0.12)
        CHECK(torsoGap > -0.04f);
    }

    // ---- 3. Dropped on a pile of loose boxes, the body comes to rest on them ----
    {
        World w({0, 1.6f, 0});
        const float size = 0.4f;
        std::vector<Vec3> start;
        for (int x = -1; x <= 1; ++x)
            for (int z = -1; z <= 1; ++z) w.Crate({(float)x * 0.42f, size * 0.5f, (float)z * 0.42f}, size, 6.0f);
        for (int x = 0; x <= 1; ++x)
            for (int z = 0; z <= 1; ++z) w.Crate({((float)x - 0.5f) * 0.44f, size * 1.5f, ((float)z - 0.5f) * 0.44f}, size, 6.0f);
        for (GameObject* c : w.crates) start.push_back(c->transform->Position());
        w.sc.Start();
        w.Run(0.1f);
        CHECK(w.ar->Ready());
        w.ar->Trigger("die");
        float deepest = 0.0f;
        w.Run(7.0f, [&] {
            for (int b = 0; b < kBones; ++b)
                for (GameObject* c : w.crates) deepest = std::fmax(deepest, w.DepthInCrate(b, c, size));
        });
        float boxMoved = 0.0f, highest = -1e9f, lowest = 1e9f;
        for (std::size_t i = 0; i < w.crates.size(); ++i) boxMoved = std::fmax(boxMoved, Vec3::Distance(start[i], w.crates[i]->transform->Position()));
        for (int b = 0; b < kBones; ++b) {
            const float y = w.ar->Core().Body(b).position.y;
            highest = std::fmax(highest, y);
            lowest = std::fmin(lowest, y);
        }
        std::cout << "  pile: body speed at the end " << w.Speed() << " m/s, highest part " << highest << " m, lowest " << lowest
                  << " m, a box moved " << boxMoved << " m, deepest limb centre line inside a box " << deepest << " m\n";
        CHECK(w.Speed() < 0.5f);                 // at rest
        CHECK(highest > size);                   // lying on the boxes, not on the floor between them
        CHECK(lowest > 0.0f);                    // nothing under the floor
        CHECK(boxMoved > 0.01f);                 // the boxes felt its weight
        CHECK(deepest < 0.08f);                  // and it lies on them, not in them
    }

    TEST_MAIN_RESULT();
}
