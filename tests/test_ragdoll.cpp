#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// Ragdoll: a Character goes limp, lands on the ground in one piece, stands back
// up, dies away from the hit, and flinches on a hit it survives.
namespace {
struct Rig {
    Scene s{"rag"};
    GameObject* body = nullptr;
    Character* ch = nullptr;
    Ragdoll* rag = nullptr;
    HealthStat* hp = nullptr;
    explicit Rig(bool ground = true, bool health = false) {
        if (ground) {
            auto* g = s.CreateGameObject("Ground");
            g->transform->localPosition = {0, -0.5f, 0};
            g->AddComponent<BoxCollider3D>()->size = {40, 1, 40};
        }
        body = s.CreateGameObject("Body");
        ch = body->AddComponent<Character>();
        ch->Apply();
        if (health) hp = body->AddComponent<HealthStat>();
        rag = body->AddComponent<Ragdoll>();
        s.Start();
        s.Update(1.0f / 60.0f);
    }
    void Run(float seconds) { for (float t = 0; t < seconds; t += 1.0f / 60.0f) s.Update(1.0f / 60.0f); }
    float MinY() const { float m = 1e9f; for (int i = 0; i < rag->ParticleCount(); ++i) m = std::fmin(m, rag->ParticlePosition(i).y); return m; }
    float MaxY() const { float m = -1e9f; for (int i = 0; i < rag->ParticleCount(); ++i) m = std::fmax(m, rag->ParticlePosition(i).y); return m; }
    float Len(int a, int b) const {
        return Vec3::Distance(rag->ParticlePosition(rag->BoneParticle(a)), rag->ParticlePosition(rag->BoneParticle(b)));
    }
};
}

int main() {
    RUN_SUITE("ragdoll");
    using namespace humanoid;

    // ---- Collapse onto the ground in one piece ----
    {
        Rig r;
        const float standTop = r.MaxY();
        const float thigh = r.Len(LThigh, LShin), arm = r.Len(RUpperArm, RForeArm), spine = r.Len(Hips, Torso);
        CHECK(r.rag->ParticleCount() == 20);
        CHECK(standTop > 1.0f);
        Quat rest = r.ch->Part(LThigh)->transform->localRotation;
        r.rag->Hit(r.rag->ParticlePosition(r.rag->BoneParticle(Torso)), {0, 0, 1.5f});
        CHECK(r.rag->Activate());
        CHECK(r.rag->IsActive());
        CHECK(!r.ch->enabled);                                  // animation switched off
        r.Run(4.0f);
        CHECK(r.MinY() > -0.05f);                              // nothing sank through the floor
        CHECK(r.MaxY() < standTop * 0.6f);                     // lying down
        CHECK(r.rag->IsSettled(0.2f));
        CHECK(std::fabs(r.Len(LThigh, LShin) - thigh) < 0.05f * thigh);
        CHECK(std::fabs(r.Len(RUpperArm, RForeArm) - arm) < 0.05f * arm);
        CHECK(std::fabs(r.Len(Hips, Torso) - spine) < 0.05f * spine);
        // The bones follow the particles.
        CHECK(Vec3::Distance(r.ch->Part(Head)->transform->Position(),
                             r.rag->ParticlePosition(r.rag->BoneParticle(Head))) < 1e-3f);

        r.rag->Deactivate();
        CHECK(!r.rag->IsActive());
        CHECK(r.ch->enabled);
        const Quat now = r.ch->Part(LThigh)->transform->localRotation;
        CHECK(std::fabs(now.x - rest.x) + std::fabs(now.y - rest.y) + std::fabs(now.z - rest.z) + std::fabs(now.w - rest.w) < 1e-4f);
    }

    // ---- With no ground the body just falls ----
    {
        Rig r(false);
        const float y0 = r.MaxY();
        r.rag->Activate();
        r.Run(1.0f);
        CHECK(r.MaxY() < y0 - 3.0f);                           // ~4.9 m in one second
    }

    // ---- Death goes limp, away from the hit; revive stands back up ----
    {
        Rig r(true, true);
        const float x0 = r.rag->ParticlePosition(r.rag->BoneParticle(Hips)).x;
        r.hp->Damage(1000.0f, Vec3{-3.0f, 1.0f, 0.0f});        // shot from the -X side
        r.Run(2.0f);
        CHECK(r.hp->IsDead());
        CHECK(r.rag->IsActive());
        CHECK(r.rag->ParticlePosition(r.rag->BoneParticle(Head)).x > x0 + 0.2f);
        r.hp->Revive();
        r.Run(0.1f);
        CHECK(!r.rag->IsActive());
        CHECK(r.ch->enabled);
    }

    // ---- A hit it survives: flinch, then back to the animation ----
    {
        Rig r(true, true);
        r.hp->Damage(10.0f, Vec3{0.0f, 1.5f, -3.0f});
        r.Run(1.0f / 60.0f);
        CHECK(!r.rag->IsActive());
        CHECK(r.rag->IsReacting());
        r.Run(1.0f);
        CHECK(!r.rag->IsReacting());
        CHECK(r.ch->enabled);
    }
    TEST_MAIN_RESULT();
}
