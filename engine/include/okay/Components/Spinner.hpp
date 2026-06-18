#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"

namespace okay {

/// Rotates its GameObject at a constant angular velocity (degrees/second around
/// each axis). Great for spinning pickups, planets, or 3D showcase objects.
class Spinner : public Behaviour {
public:
    Vec3 angularVelocity{0.0f, 0.0f, 90.0f}; // degrees per second

    void Update(float dt) override {
        if (transform) transform->Rotate(angularVelocity * dt);
    }
};

} // namespace okay
