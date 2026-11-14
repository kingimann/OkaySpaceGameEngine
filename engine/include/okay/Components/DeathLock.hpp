#pragma once
// One line for a movement controller: a dead body does not read move input.
// HealthStat owns the flag when it is present; a script die() with no health
// sets the Animator's `dead` parameter instead.
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Physics/Rigidbody3D.hpp"

namespace okay {

inline bool DeathLocksInput(GameObject* go) {
    if (!go) return false;
    // Health is the authority when the body has it. A leftover animator `dead`
    // flag must not freeze a living (or revived) character. A script die() with
    // no health component sets that flag instead, and only the base layer counts.
    if (auto* hp = go->GetComponent<HealthStat>())
        return hp->IsDead();
    for (AnimStateMachine* sm : go->GetComponents<AnimStateMachine>())
        if (sm && sm->layer == 0 && sm->GetBool("dead")) return true;
    return false;
}

/// Drop the walk velocity a dead body was carrying. Vertical speed stays so a
/// body in the air still lands; the controller is no longer steering.
inline void StopDeadSlide(GameObject* go) {
    if (!go) return;
    if (auto* rb = go->GetComponent<Rigidbody3D>()) {
        rb->velocity.x = 0.0f;
        rb->velocity.z = 0.0f;
    }
}

} // namespace okay
