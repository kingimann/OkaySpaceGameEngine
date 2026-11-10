#pragma once
// Shared third-person orbit placement. The third-person controller and the
// view toggle both call this, so the spring arm, pivot and aim stay one piece
// of code. With the same inputs it matches the controller's own camera.
#include "okay/Components/ControllerFeel.hpp"
#include "okay/Components/Stance.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <cmath>

namespace okay {

struct OrbitPose {
    Vec3 position;
    Quat rotation;
};

/// One frame of the over-the-shoulder orbit. Steps the pivot, aim blend, look
/// smoothing, spring arm and landing-shake decay the controller already owned.
/// `applyShake` adds the landing bump; the view blend turns that off so the
/// transition itself doesn't shake.
inline OrbitPose PlaceThirdPersonOrbit(
    Transform* transform, GameObject* self, Scene* sc,
    float yaw, float pitch,
    float distance, float aimDistance, float shoulder, float aimShoulder,
    float cameraHeight, float stanceLevel, float crouchDrop, float proneDrop, float stanceLerp,
    float& stanceDrop, bool aiming, float aimLerp, float& aimT, float& shake,
    LookSmoother& smooth, float smoothing,
    FollowPivot& pivot, float dampXZ, float dampY, float maxLag,
    bool collision, SpringArm& arm, float radius, float skin, std::uint32_t mask,
    float pullIn, float recover, bool applyShake, float dt) {
    float drop = StanceValue(stanceLevel, 0.0f, crouchDrop, proneDrop);
    float lt = stanceLerp > 0.0f ? (1.0f - std::exp(-stanceLerp * dt)) : 1.0f;
    stanceDrop += (drop - stanceDrop) * lt;

    Vec3 head = transform->Position();
    head.y += cameraHeight - stanceDrop;
    const Vec3 target = pivot.Step(head, dampXZ, dampY, maxLag, dt);

    float at = aimLerp > 0.0f ? (1.0f - std::exp(-aimLerp * dt)) : 1.0f;
    aimT += ((aiming ? 1.0f : 0.0f) - aimT) * at;
    const float useDistance = distance + (aimDistance - distance) * aimT;
    const float useShoulder = shoulder + (aimShoulder - shoulder) * aimT;
    shake *= 1.0f - EaseFactor(8.0f, dt);

    smooth.Step(yaw, pitch, smoothing, dt);
    const float orbitYaw = smooth.Yaw(), orbitPitch = smooth.Pitch();

    float pr = orbitPitch * Mathf::Deg2Rad;
    Vec3 behind = Quat::Euler(0, orbitYaw, 0) * Vec3{0, 0, 1};
    float cp = Mathf::Cos(pr);
    Vec3 offset{behind.x * cp, Mathf::Sin(pr), behind.z * cp};
    Vec3 camPos = target + offset * useDistance;
    if (useShoulder != 0.0f)
        camPos = camPos + (Quat::Euler(0, orbitYaw, 0) * Vec3::Right) * useShoulder;

    if (collision && sc) {
        Vec3 d = camPos - target;
        float dist = d.Magnitude();
        if (dist > 1e-4f) {
            Vec3 dn = d * (1.0f / dist);
            camPos = target + dn * arm.Step(*sc, self, target, dn, dist, radius, skin, mask,
                                            pullIn, recover, dt);
        }
    }

    // A flat floor is easy for a sphere cast to miss, and a low pitch at full
    // distance puts the lens under that floor, looking up at its underside.
    // Slide back along the arm until the lens is above the feet. Lifting it
    // straight up would leave the look aimed through the slab.
    {
        const float clearance = std::max(0.40f, skin);
        const float minY = transform->Position().y + clearance;
        Vec3 armV = camPos - target;
        const float dist = armV.Magnitude();
        if (dist > 1e-3f && camPos.y < minY) {
            const Vec3 dn = armV * (1.0f / dist);
            if (dn.y < -1e-4f) {
                float allow = (minY - target.y) / dn.y;
                if (allow < 0.25f) allow = 0.25f;
                if (allow < dist) camPos = target + dn * allow;
            }
            if (camPos.y < minY) camPos.y = minY;
        }
    }
    if (applyShake) camPos.y -= shake;

    OrbitPose pose;
    pose.position = camPos;
    pose.rotation = Quat::Euler(-orbitPitch, orbitYaw, 0.0f);
    return pose;
}

} // namespace okay
