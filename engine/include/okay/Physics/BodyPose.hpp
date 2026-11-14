#pragma once
// Moving a rigidbody in world space. An un-parented body stores its world pose in
// localPosition / localRotation, and the solvers write those fields directly. A body
// under a parent must take the same world delta without the parent's rotation or
// scale turning or stretching the step. Un-parented, MoveBodyWorld is
// `localPosition += delta` and SpinBodyWorld is the old local-rotation integrate.
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"

namespace okay {

/// Shift `t` by a world-space delta. No parent: `localPosition += worldDelta`.
inline void MoveBodyWorld(Transform* t, const Vec3& worldDelta) {
    if (!t) return;
    if (!t->Parent()) t->localPosition += worldDelta;
    else if (worldDelta.SqrMagnitude() > 0.0f) t->SetPosition(t->Position() + worldDelta);
}

/// Integrate angular velocity (radians per second, world axes) for one step.
/// No parent: the same quaternion integrate the 3D solver used to write into localRotation.
inline void SpinBodyWorld(Transform* t, const Vec3& angularVelocity, float dt) {
    if (!t || angularVelocity.SqrMagnitude() <= 1e-12f) return;
    const bool parented = t->Parent() != nullptr;
    Quat q = parented ? t->Rotation() : t->localRotation;
    Quat wq{angularVelocity.x, angularVelocity.y, angularVelocity.z, 0.0f};
    Quat dq = wq * q;
    const float h = 0.5f * dt;
    Quat next{q.x + dq.x * h, q.y + dq.y * h, q.z + dq.z * h, q.w + dq.w * h};
    next = next.Normalized();
    if (!parented) t->localRotation = next;
    else t->SetRotation(next);
}

/// The parent pose last physics frame, so leaving that parent can keep its speed.
struct ParentSample {
    Transform* tracked = nullptr;
    Vec3 pos;
    Quat rot = Quat::Identity;
    bool have = false;
};

/// World linear velocity to add because `body` just left `sample.tracked`, and the
/// parent's spin in radians per second (zero when it did not leave). Updates
/// `sample` to the parent `body` has now. A parent that did not change contributes
/// nothing: the hierarchy already carries the body.
inline Vec3 ParentDetachVelocity(ParentSample& sample, Transform* parent, const Vec3& bodyPos,
                                 float frameDt, Vec3& angularOut) {
    angularOut = Vec3::Zero;
    Vec3 add = Vec3::Zero;
    if (sample.tracked && sample.tracked != parent && sample.have && frameDt > 1e-8f) {
        Transform* old = sample.tracked;
        if (old->gameObject) {
            const Vec3 originVel = (old->Position() - sample.pos) * (1.0f / frameDt);
            Quat dq = (old->Rotation() * sample.rot.Conjugate()).Normalized();
            float deg = 0.0f;
            Vec3 axis{1.0f, 0.0f, 0.0f};
            dq.ToAngleAxis(deg, axis);
            if (deg > 180.0f) deg -= 360.0f;
            angularOut = axis * (deg * Mathf::Deg2Rad / frameDt);
            add = originVel + Vec3::Cross(angularOut, bodyPos - old->Position());
        }
    }
    if (parent && parent->gameObject && !parent->gameObject->IsDestroyed()) {
        sample.tracked = parent;
        sample.pos = parent->Position();
        sample.rot = parent->Rotation();
        sample.have = true;
    } else {
        sample.tracked = nullptr;
        sample.have = false;
        sample.rot = Quat::Identity;
    }
    return add;
}

} // namespace okay
