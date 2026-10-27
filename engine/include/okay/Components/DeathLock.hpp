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
    if (auto* hp = go->GetComponent<HealthStat>())
        if (hp->IsDead()) return true;
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
