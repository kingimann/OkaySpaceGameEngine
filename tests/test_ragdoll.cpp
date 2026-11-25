#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/RagdollPhysics.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace okay;

// The one Ragdoll component (Components/Ragdoll.hpp) in a scene, on the physics core: a limp body falls and settles
// on the floor in one piece, no limb ends up inside the world, the joints stay inside their limits, death and hits
// work, and scenes saved with the two old components (the Verlet `ragdoll` record and the `activeragdoll` record)
// load into it. (The active-ragdoll behaviour, balance and get-up, is in test_active_ragdoll; limbs against dynamic
// bodies and each other in test_ragdoll_collisions.)
namespace {
using namespace humanoid;

struct Rig {
    Scene s{"rag"};
    GameObject* body = nullptr;
    Character* ch = nullptr;
    Ragdoll* rag = nullptr;
    HealthStat* hp = nullptr;
    explicit Rig(bool ground = true, bool health = false, const Vec3& at = Vec3::Zero) {
        if (ground) {
            auto* g = s.CreateGameObject("Ground");
            g->transform->localPosition = {0, -0.5f, 0};
            g->AddComponent<BoxCollider3D>()->size = {40, 1, 40};
        }
        body = s.CreateGameObject("Body");
        body->transform->localPosition = at;
        ch = body->AddComponent<Character>();
        ch->Apply();
        if (health) hp = body->AddComponent<HealthStat>();
        rag = body->AddComponent<Ragdoll>();
        s.Start();
        s.Update(1.0f / 60.0f);
    }
    void Run(float seconds) { for (float t = 0; t < seconds - 1e-5f; t += 1.0f / 60.0f) s.Update(1.0f / 60.0f); }
    dyn::BodyState B(int bone) const { return rag->Core().Body(bone); }
    float MinY() const { float m = 1e9f; for (int b = 0; b < BoneCount; ++b) m = std::fmin(m, B(b).position.y); return m; }
    float MaxY() const { float m = -1e9f; for (int b = 0; b < BoneCount; ++b) m = std::fmax(m, B(b).position.y); return m; }
    float MaxGap() { float g = 0; auto* c = rag->Core().Bodies(); for (int j = 0; j < c->JointCount(); ++j) g = std::fmax(g, c->JointGap(j)); return g; }
    float MaxLimit() { float g = 0; auto* c = rag->Core().Bodies(); for (int j = 0; j < c->JointCount(); ++j) g = std::fmax(g, c->JointLimitExcess(j)); return g; }
};

Ragdoll* OnlyRagdoll(Scene& s, const char* name) {
    GameObject* go = s.Find(name);
    if (!go) return nullptr;
    auto all = go->GetComponents<Ragdoll>();
    return all.size() == 1 ? all[0] : nullptr;
}

const char* kHead = "okayscene 3\nname \"old\"\ngameobject 0 \"Body\"\n  active 1\n  parent -1\n"
                    "  transform 0 0 0 0 0 0 1 1 1 1\n";
const char* kVerlet = "  ragdoll grav=9.81 damp=0.02 fric=0.6 radius=0.06 iters=8 maxSpeed=30 onDeath=0 deathPush=2.5 hits=1"
                      " hitPush=1.5 recover=0.35\n";
const char* kActive = "  activeragdoll mode=\"1 Powered\" mass=82 strength=1.5 limpOnDeath=true reactToDamage=false\n";
} // namespace

int main() {
    RUN_SUITE("ragdoll");

    // ---- Limp: falls onto the floor in one piece and settles there ----
    {
        Rig r;
        const float standTop = r.MaxY();
        CHECK(r.rag->Ready());
        CHECK(standTop > 1.0f);
        CHECK(!r.rag->IsActive());
        CHECK(r.rag->Activate());
        r.rag->PushAt(r.B(Torso).position, Vec3{0, 0, 20.0f});
        CHECK(r.rag->IsActive());
        float lowest = 1e9f, worstGap = 0.0f, worstLimit = 0.0f;
        for (int i = 0; i < 240; ++i) {
            r.Run(1.0f / 60.0f);
            lowest = std::fmin(lowest, r.MinY());
            worstGap = std::fmax(worstGap, r.MaxGap());
            worstLimit = std::fmax(worstLimit, r.MaxLimit());
        }
        std::printf("  limp fall: top %.3f -> %.3f, lowest part centre %.3f, worst joint gap %.4f m, worst limit excess %.3f rad"
                    " (at rest %.3f), settled %d\n", standTop, r.MaxY(), lowest, worstGap, worstLimit, r.MaxLimit(),
                    (int)r.rag->IsSettled(0.2f));
        CHECK(lowest > 0.0f);                                   // every part's centre stays above the floor
        CHECK(r.MaxY() < standTop * 0.5f);                     // lying down
        CHECK(r.rag->IsSettled(0.2f));                         // and still
        CHECK(worstGap < 0.03f);                               // the joints hold together
        CHECK(worstLimit < 0.2f);                              // and inside their limits (a little give while landing)
        CHECK(r.MaxLimit() < 0.05f);                           // at rest: inside
        r.rag->Deactivate();
        CHECK(!r.rag->IsActive());
        CHECK(r.rag->State() == dyn::CharState::Animated);
    }

    // ---- With no ground the body just falls ----
    {
        Rig r(false);
        const float y0 = r.B(Hips).position.y;
        r.rag->Activate();
        r.Run(1.0f);
        std::printf("  free fall: pelvis dropped %.2f m in 1 s\n", y0 - r.B(Hips).position.y);
        CHECK(r.B(Hips).position.y < y0 - 3.0f);              // ~4.9 m in one second
    }

    // ---- Limbs do not pass through the world: dropped limp across a table, nothing ends up inside it or the floor ----
    {
        Rig r(true, false, Vec3{0, 1.2f, 0});
        auto* table = r.s.CreateGameObject("Table");
        table->transform->localPosition = {0, 0.4f, 0.3f};
        table->AddComponent<BoxCollider3D>()->size = {1.6f, 0.8f, 0.8f};
        r.Run(1.0f / 60.0f);
        r.rag->Activate();
        r.rag->PushAt(r.B(Torso).position, Vec3{0, 0, 15.0f});
        float deepest = 0.0f, lowest = 1e9f;
        const Vec3 lo{-0.8f, 0.0f, -0.1f}, hi{0.8f, 0.8f, 0.7f};
        for (int i = 0; i < 300; ++i) {
            r.Run(1.0f / 60.0f);
            for (int b = 0; b < BoneCount; ++b) {
                const Vec3 p = r.B(b).position;
                lowest = std::fmin(lowest, p.y);
                if (p.x > lo.x && p.x < hi.x && p.y > lo.y && p.y < hi.y && p.z > lo.z && p.z < hi.z) {
                    const float in = std::fmin(std::fmin(p.x - lo.x, hi.x - p.x),
                                               std::fmin(std::fmin(p.y - lo.y, hi.y - p.y), std::fmin(p.z - lo.z, hi.z - p.z)));
                    deepest = std::fmax(deepest, in);
                }
            }
        }
        std::printf("  table drop: deepest part centre inside the table %.3f m, lowest part centre %.3f, highest at rest %.3f\n",
                    deepest, lowest, r.MaxY());
        CHECK(deepest < 0.02f);
        CHECK(lowest > 0.0f);
        CHECK(r.MaxY() > 0.8f);                                // something of it lies on the table
    }

    // ---- Death goes limp, away from the hit; revive gets back up ----
    {
        Rig r(true, true);
        const float x0 = r.B(Hips).position.x;
        r.hp->Damage(1000.0f, Vec3{-3.0f, 1.0f, 0.0f});        // shot from the -X side
        r.Run(2.0f);
        CHECK(r.hp->IsDead());
        CHECK(r.rag->IsActive());
        CHECK(r.rag->State() == dyn::CharState::Dead);
        std::printf("  death: head moved %.2f m away from the shot\n", r.B(Head).position.x - x0);
        CHECK(r.B(Head).position.x > x0 + 0.2f);
        r.hp->Revive();
        r.Run(0.1f);
        CHECK(r.rag->State() != dyn::CharState::Dead);          // getting up
        r.Run(8.0f);
        std::printf("  revive: state %s after 8 s\n", r.rag->StateName());
        CHECK(r.rag->State() == dyn::CharState::Animated);
    }

    // ---- A hit it survives: it reacts, then is back in the animation ----
    {
        Rig r(true, true);
        r.hp->Damage(10.0f, Vec3{0.0f, 1.5f, -3.0f});
        r.Run(2.0f / 60.0f);
        CHECK(r.rag->IsPhysical());
        CHECK(!r.rag->IsActive());                              // not limp, not down
        r.Run(4.0f);
        std::printf("  hit: state %s after 4 s\n", r.rag->StateName());
        CHECK(r.rag->State() == dyn::CharState::Animated);
    }

    // ---- Old scenes: both old records load into the one Ragdoll ----
    {
        // The Verlet Ragdoll's `ragdoll` record: limp-on-death and hit reactions carry over, animated until it dies.
        Scene a{"a"};
        std::string err;
        CHECK(SceneSerializer::Deserialize(a, std::string(kHead) + kVerlet + "end\n", &err));
        Ragdoll* ra = OnlyRagdoll(a, "Body");
        CHECK(ra != nullptr);
        if (ra) {
            CHECK(!ra->limpOnDeath);                            // onDeath=0
            CHECK(ra->reactToDamage);                           // hits=1
            CHECK(ra->mode == 0 && !ra->stumble);
        }
        // The ActiveRagdoll's `activeragdoll` record.
        Scene b{"b"};
        CHECK(SceneSerializer::Deserialize(b, std::string(kHead) + kActive + "end\n", &err));
        Ragdoll* rb = OnlyRagdoll(b, "Body");
        CHECK(rb != nullptr);
        if (rb) CHECK(rb->mode == 1 && std::fabs(rb->mass - 82.0f) < 1e-4f && std::fabs(rb->strength - 1.5f) < 1e-4f && !rb->reactToDamage);
        // Both on one object (the ActiveRagdoll used to take over): one Ragdoll, with the ActiveRagdoll's settings.
        Scene c{"c"};
        CHECK(SceneSerializer::Deserialize(c, std::string(kHead) + kVerlet + kActive + "  compoff \"ActiveRagdoll\" 0\nend\n", &err));
        Ragdoll* rc = OnlyRagdoll(c, "Body");
        CHECK(rc != nullptr);
        if (rc) {
            CHECK(rc->mode == 1 && rc->limpOnDeath && !rc->reactToDamage);
            CHECK(!rc->fromLegacyRecord);
            CHECK(!rc->enabled);                                // `compoff "ActiveRagdoll"` names the Ragdoll now
        }
        // Saved again: one `activeragdoll` record, no `ragdoll` record; and it loads back the same.
        const std::string text = SceneSerializer::Serialize(c);
        CHECK(text.find("  activeragdoll ") != std::string::npos);
        CHECK(text.find("\n  ragdoll ") == std::string::npos);
        Scene d{"d"};
        CHECK(SceneSerializer::Deserialize(d, text, &err));
        Ragdoll* rd = OnlyRagdoll(d, "Body");
        CHECK(rd && rd->mode == 1 && std::fabs(rd->mass - 82.0f) < 1e-4f && !rd->enabled);
        // Two Ragdolls on one object saved today stay two (only an old `ragdoll` record merges).
        Scene e{"e"};
        GameObject* two = e.CreateGameObject("Two");
        two->AddComponent<Ragdoll>()->mass = 50.0f;
        two->AddComponent<Ragdoll>()->mass = 60.0f;
        Scene f{"f"};
        CHECK(SceneSerializer::Deserialize(f, SceneSerializer::Serialize(e), &err));
        GameObject* back = f.Find("Two");
        CHECK(back && back->GetComponents<Ragdoll>().size() == 2);
        // The old type name still finds it.
        auto info = PropertyRegistry::FindByName("ActiveRagdoll");
        CHECK(info && info->name == "Ragdoll");
    }
    TEST_MAIN_RESULT();
}
