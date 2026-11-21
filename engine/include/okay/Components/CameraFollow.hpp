#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/CinemachineBrain.hpp"
#include "okay/Math/Vec3.hpp"
#include <algorithm>
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
        if (!std::isfinite(dt)) return;
        // One owner for the camera: a camera brain with a live shot on the same object already places it every
        // frame, so this follow steps aside (and starts fresh when the brain lets go).
        if (auto* b = gameObject->GetComponent<CinemachineBrain>(); b && b->enabled && b->LiveCamera()) { m_started = false; return; }
        GameObject* target = gameObject->scene()->Find(targetName);
        if (!target || !target->transform) return;
        const Vec3 tp = target->transform->Position();
        if (!std::isfinite(tp.x) || !std::isfinite(tp.y) || !std::isfinite(tp.z)) return;   // a broken target never drags the camera

        const Vec3 desired = tp + offset;
        const Vec3 now = transform->Position();   // world space: a camera under a parent works too
        Vec3 next = desired;
        // First frame (or a scene that has not moved us yet): start on the target, do not fly in from the origin.
        if (m_started && smoothing > 0.0f && std::isfinite(smoothing)) {
            // Frame-rate independent exponential smoothing toward the target.
            const float t = 1.0f - std::exp(-smoothing * std::max(0.0f, dt));
            next = Vec3::Lerp(now, desired, t);
        }
        m_started = true;
        transform->SetPosition(next);
    }

private:
    bool m_started = false;
};

} // namespace okay
