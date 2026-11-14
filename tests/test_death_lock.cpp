// DeathLock: a dead body does not read move input, and the walk velocity is dropped.
// HealthStat owns the flag when the body has one. Otherwise the base animator's
// "dead" parameter does.
#include "test_framework.hpp"
#include "okay/Components/DeathLock.hpp"

using namespace okay;

int main() {
    RUN_SUITE("death_lock");

    CHECK(!DeathLocksInput(nullptr));
    StopDeadSlide(nullptr);

    {
        Scene s("empty");
        CHECK(!DeathLocksInput(s.CreateGameObject("Walker")));
    }

    // No health: the base layer's dead flag locks input. An upper layer does not.
    {
        Scene s("anim");
        GameObject* go = s.CreateGameObject("Corpse");
        auto* sm = go->AddComponent<AnimStateMachine>();
        CHECK(sm->layer == 0);
        CHECK(!DeathLocksInput(go));
        sm->SetBool("dead", true);
        CHECK(DeathLocksInput(go));
        sm->SetBool("dead", false);
        CHECK(!DeathLocksInput(go));

        GameObject* upper = s.CreateGameObject("Upper");
        auto* layer = upper->AddComponent<AnimStateMachine>();
        layer->layer = 1;
        layer->SetBool("dead", true);
        CHECK(!DeathLocksInput(upper));                    // only layer 0 counts
    }

    // Health present: it owns the flag. A leftover animator "dead" does not freeze a living body.
    {
        Scene s("health");
        GameObject* go = s.CreateGameObject("Hero");
        auto* hp = go->AddComponent<HealthStat>();
        hp->playDeathAnim = false;
        auto* sm = go->AddComponent<AnimStateMachine>();
        sm->SetBool("dead", true);
        CHECK(!hp->IsDead());
        CHECK(!DeathLocksInput(go));                       // alive health wins over the animator

        hp->dead = true;
        sm->SetBool("dead", false);
        CHECK(DeathLocksInput(go));

        hp->dead = false;                                  // revived, animator flag put back by a late graph tick
        sm->SetBool("dead", true);
        CHECK(!DeathLocksInput(go));

        hp->KillFromAction(false, {});
        CHECK(hp->IsDead());
        CHECK(DeathLocksInput(go));
    }

    // Horizontal velocity is cleared. The fall is kept. No rigidbody is fine.
    {
        Scene s("slide");
        GameObject* go = s.CreateGameObject("Body");
        StopDeadSlide(go);
        auto* rb = go->AddComponent<Rigidbody3D>();
        rb->velocity = Vec3{3.0f, -4.0f, 5.0f};
        StopDeadSlide(go);
        CHECK_NEAR(rb->velocity.x, 0.0f, 1e-5f);
        CHECK_NEAR(rb->velocity.y, -4.0f, 1e-5f);
        CHECK_NEAR(rb->velocity.z, 0.0f, 1e-5f);
    }

    TEST_MAIN_RESULT();
}
