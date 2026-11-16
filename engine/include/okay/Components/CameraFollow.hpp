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
///
/// Everything is in world space: the camera may sit under a parent (a rig, a
/// vehicle) and still ends at target + offset. A target position that is not a
/// finite number (NaN, infinity) is skipped for that frame, and a camera whose own
/// position went non-finite snaps back to the target, so one bad frame never
/// leaves the camera broken.
class CameraFollow : public Behaviour {
public:
    std::string targetName;            // GameObject to follow (resolved by name)
    Vec3 offset{0.0f, 0.0f, 0.0f};     // position relative to the target
    float smoothing = 5.0f;            // higher = snappier; <= 0 = instant snap

    void LateUpdate(float dt) override {
        if (!transform || targetName.empty() || !gameObject || !gameObject->scene()) return;
        GameObject* target = gameObject->scene()->Find(targetName);
        if (!target || !target->transform) return;

        const Vec3 desired = target->transform->Position() + offset;
        if (!Finite(desired)) return;   // a bad target frame: keep the camera where it is
        const Vec3 current = transform->Position();
        Vec3 next = desired;
        if (smoothing > 0.0f && Finite(current)) {
            // Frame-rate independent exponential smoothing toward the target.
            const float t = 1.0f - std::exp(-smoothing * dt);
            next = Vec3::Lerp(current, desired, t);
            if (!Finite(next)) next = desired;
        }
        // World position through the parent (writing localPosition put a world value
        // into the parent's space, so a parented camera drifted away for ever).
        transform->SetPosition(next);
    }

private:
    static bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
};

} // namespace okay
