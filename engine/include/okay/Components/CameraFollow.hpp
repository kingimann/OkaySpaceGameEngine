#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include <cmath>
#include <string>

namespace okay {

/// Smoothly follows a target GameObject — the standard 2D/3D chase camera. Put
/// it on the camera, set `targetName` to the object to track, and it eases the
/// transform toward the target's position plus `offset` each frame.
class CameraFollow : public Behaviour {
public:
    std::string targetName;            // GameObject to follow (resolved by name)
    Vec3 offset{0.0f, 0.0f, 0.0f};     // position relative to the target
    float smoothing = 5.0f;            // higher = snappier; <= 0 = instant snap

    void LateUpdate(float dt) override {
        if (!transform || targetName.empty() || !gameObject || !gameObject->scene()) return;
        GameObject* target = gameObject->scene()->Find(targetName);
        if (!target || !target->transform) return;

        Vec3 desired = target->transform->Position() + offset;
        if (smoothing <= 0.0f) {
            transform->localPosition = desired;
        } else {
            // Frame-rate independent exponential smoothing toward the target.
            float t = 1.0f - std::exp(-smoothing * dt);
            transform->localPosition = Vec3::Lerp(transform->Position(), desired, t);
        }
    }
};

} // namespace okay
