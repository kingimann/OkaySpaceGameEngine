#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <string>
#include "okay/Scene/ComponentSerializer.hpp"

using namespace okay;
#define BASE_OFF_DEAD "Character NPCController "   // recorded on the Ragdoll before the shared helper (2026-11-22)
#define BASE_SUM_DEAD 3.809640
#define BASE_OFF_ALIVE ""
#define BASE_SUM_HIT 32.764613

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
// What a Ragdoll switched off, by type, in component order (for the baseline below).
std::string OffList(GameObject* root) {
    std::string out;
    std::vector<Transform*> todo{root->transform};
    while (!todo.empty()) {
        Transform* t = todo.back(); todo.pop_back();
        for (Component* c : t->gameObject->GetComponents<Component>())
            if (!c->enabled) out += ComponentSerializerRegistry::TypeKey(*c) + " ";
        for (Transform* k : t->Children()) todo.push_back(k);
    }
    return out;
}
double Sum(const Ragdoll* r) {
    double s = 0.0;
    for (int i = 0; i < r->ParticleCount(); ++i) { const Vec3 p = r->ParticlePosition(i); s += p.x + 2.0 * p.y + 3.0 * p.z; }
    return s;
}
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

    // ---- Baseline (recorded on the Ragdoll before the shared helper): death, revive and a hit give
    //      the same particles and switch off the same components ----
    {
        Rig r(true, true);
        r.body->AddComponent<NPCController>()->behavior = (int)NPCController::Behavior::Idle;
        r.Run(0.2f);
        r.hp->Damage(1000.0f, Vec3{-3.0f, 1.0f, 0.5f});
        r.Run(1.0f);
        const std::string offDead = OffList(r.body);
        const double sumDead = Sum(r.rag);
        r.hp->Revive();
        r.Run(0.1f);
        const std::string offAlive = OffList(r.body);
        r.hp->Damage(10.0f, Vec3{0.5f, 1.4f, -3.0f});
        r.Run(0.2f);
        const double sumHit = Sum(r.rag);
        std::printf("  baseline: dead off [%s] sum %.6f | revived off [%s] | hit sum %.6f reacting %d\n",
                    offDead.c_str(), sumDead, offAlive.c_str(), sumHit, (int)r.rag->IsReacting());
        CHECK(offDead == BASE_OFF_DEAD);
        CHECK(std::fabs(sumDead - BASE_SUM_DEAD) < 1e-3);
        CHECK(offAlive == BASE_OFF_ALIVE);
        CHECK(std::fabs(sumHit - BASE_SUM_HIT) < 1e-3);
    }

    // ---- The one intended change: every movement controller is off while limp (one list shared with
    //      ActiveRagdoll). Before, a Click To Move controller kept steering the root of a limp body. ----
    {
        Rig r(true, true);
        auto* ctm = r.body->AddComponent<ClickToMoveController>();
        r.Run(0.2f);
        r.hp->Damage(1000.0f, Vec3{-3.0f, 1.0f, 0.5f});
        r.Run(0.2f);
        CHECK(r.rag->IsActive());
        CHECK(!ctm->enabled);
        r.hp->Revive();
        r.Run(0.1f);
        CHECK(ctm->enabled);
    }
    TEST_MAIN_RESULT();
}
