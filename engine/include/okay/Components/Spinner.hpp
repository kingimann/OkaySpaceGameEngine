#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include <cmath>

namespace okay {

/// Rotates its GameObject at a constant angular velocity (degrees/second around
/// each axis). Great for spinning pickups, planets, or 3D showcase objects.
class Spinner : public Behaviour {
public:
    /// Degrees per second. The vector is an axis of spin and its length the speed: a turn about
    /// several axes at once is one rotation about that axis, so the result does not depend on how
    /// the time is cut into frames.
    Vec3 angularVelocity{0.0f, 0.0f, 90.0f};
    /// Self (the default, and what it always did) spins around the object's own axes, Local around
    /// the parent's axes, World around the scene's axes.
    Space space = Space::Self;

    void Update(float dt) override {
        if (!transform) return;
        if (!std::isfinite(dt) || !std::isfinite(angularVelocity.x) || !std::isfinite(angularVelocity.y) ||
            !std::isfinite(angularVelocity.z)) return;   // never let a NaN into the rotation
        const float speed = angularVelocity.Magnitude();
        if (speed <= 0.0f || !std::isfinite(speed)) return;
        transform->Rotate(angularVelocity * (1.0f / speed), speed * dt, space);
    }
};

} // namespace okay
