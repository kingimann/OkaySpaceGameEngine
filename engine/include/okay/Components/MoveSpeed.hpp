#pragma once
// ---------------------------------------------------------------------------
// MoveSpeedScale — what everything on a body multiplies its movement speed by:
// carrying a heavy load (ProceduralObjectHolding::SpeedMultiplier). The player
// controllers multiply their walk / run / crouch / prone speed by it where they
// pick the speed (inside Simulate for the ones that run server-side), so a
// server and a predicting client move the body at the same speed. One helper
// for every slow-down, so the controllers don't each grow their own.
// ---------------------------------------------------------------------------
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"

namespace okay {

/// 1 = full speed. `body` is the object the controller is on.
inline float MoveSpeedScale(const GameObject* body) {
    float k = 1.0f;
    if (body)
        if (auto* h = body->GetComponent<ProceduralObjectHolding>()) k *= h->SpeedMultiplier();
    return k;
}

} // namespace okay
