#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"

namespace okay {

/// Moves its GameObject at a constant velocity every frame. A no-scripting way
/// to give projectiles, scrolling backgrounds, or drifting objects motion.
class Mover : public Behaviour {
public:
    Vec3 velocity{0.0f, 0.0f, 0.0f}; // world units per second

    void Update(float dt) override {
        if (transform) transform->Translate(velocity * dt);
    }
};

} // namespace okay
