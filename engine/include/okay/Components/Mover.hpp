#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include <cmath>

namespace okay {

/// Moves its GameObject at a constant velocity every frame. A no-scripting way
/// to give projectiles, scrolling backgrounds, or drifting objects motion.
class Mover : public Behaviour {
public:
    /// Distance per second, read in `space`.
    Vec3 velocity{0.0f, 0.0f, 0.0f};
    /// Local (the default, and what it always did) moves in the parent's frame, which is the
    /// world frame for an object with no parent; World moves in world units per second whatever the
    /// parent does; Self moves along the object's own axes (a bullet that flies where it points),
    /// in world units per second.
    Space space = Space::Local;

    void Update(float dt) override {
        if (!transform) return;
        // A NaN or infinite velocity or frame time must not reach the position: it never comes back.
        if (!std::isfinite(dt) || !std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z)) return;
        transform->Translate(velocity * dt, space);
    }
};

} // namespace okay
