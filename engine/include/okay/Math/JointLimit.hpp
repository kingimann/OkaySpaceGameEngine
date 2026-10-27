#pragma once
// ---------------------------------------------------------------------------
// IKJointLimit — how far an IK solver may turn one bone away from its rest pose:
// a Hinge (elbow, knee) bends about one axis and barely rolls; a Ball (shoulder,
// hip, spine) swings inside a cone and rolls within a twist range.
//
// Angles are measured from the bone's REST local rotation (the pose it had when
// the solver resolved its bones), and the axes live in the bone's own frame:
// the twist axis points at the child, the hinge axis is the normal of the rest
// bend plane. Nothing here is a shared constant, so the same numbers behave the
// same on the left and right side of a mirrored rig.
// ---------------------------------------------------------------------------
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/IKMath.hpp"
#include <cmath>

namespace okay {

struct IKJointLimit {
    enum class Type { None = 0, Hinge = 1, Ball = 2 };
    Type  type = Type::None;
    float swingMax = 180.0f;                       ///< Ball: cone half-angle from rest (degrees)
    float hingeMin = -180.0f, hingeMax = 180.0f;   ///< Hinge: bend about the hinge axis (degrees from rest)
    float twistMin = -180.0f, twistMax = 180.0f;   ///< roll about the twist axis (degrees from rest)
    bool  autoAxes = true;                         ///< axes from the rest pose (below); off: the fields
    Vec3  twistAxis = Vec3::Up;                    ///< bone-local
    Vec3  hingeAxis = Vec3::Right;                 ///< bone-local

    bool Active() const { return type != Type::None; }

    // Humanoid presets.
    static IKJointLimit Elbow()    { return Hinge(0.0f, 150.0f, 10.0f); }
    static IKJointLimit Knee()     { return Hinge(0.0f, 150.0f, 5.0f); }
    static IKJointLimit Shoulder() { return Ball(100.0f, 60.0f); }
    static IKJointLimit Hip()      { return Ball(80.0f, 30.0f); }
    static IKJointLimit Spine()    { return Ball(20.0f, 15.0f); }
    static IKJointLimit Neck()     { return Ball(40.0f, 40.0f); }
    static IKJointLimit Hinge(float bendMin, float bendMax, float twist) {
        IKJointLimit l; l.type = Type::Hinge; l.hingeMin = bendMin; l.hingeMax = bendMax; l.twistMin = -twist; l.twistMax = twist; return l;
    }
    static IKJointLimit Ball(float swing, float twist) {
        IKJointLimit l; l.type = Type::Ball; l.swingMax = swing; l.twistMin = -twist; l.twistMax = twist; return l;
    }
};

/// The rest frame one bone's limit measures from. Capture() when the bones
/// resolve (and again whenever they change); the hinge axis of a bone whose
/// rest pose is straight comes from the first solved bend plane instead.
struct IKJointFrame {
    Quat restLocal = Quat::Identity;
    Vec3 twistAxis = Vec3::Up;      ///< bone-local: toward the child
    Vec3 hingeAxis = Vec3::Right;   ///< bone-local: normal of the rest bend plane
    const Transform* bone = nullptr;
    bool hasHinge = false;

    /// `child` is the bone's next joint; `grandchild` (may be null) the one after,
    /// which gives the rest bend plane.
    void Capture(const Transform* b, const Transform* child, const Transform* grandchild) {
        bone = b; hasHinge = false;
        if (!b) return;
        restLocal = b->localRotation;
        const Quat toLocal = b->Rotation().Inverse();
        if (child) twistAxis = SafeNormalize(toLocal * (child->Position() - b->Position()), Vec3::Up);
        if (child && grandchild)
            SetHingeFromBend(b->Position(), child->Position(), grandchild->Position());
    }
    /// Hinge axis from a bend plane in WORLD space (rest or first solved).
    void SetHingeFromBend(const Vec3& root, const Vec3& mid, const Vec3& end) {
        if (!bone) return;
        const Vec3 n = Vec3::Cross(mid - root, end - mid);
        const float l2 = (mid - root).SqrMagnitude() * (end - mid).SqrMagnitude();
        if (!(l2 > 0.0f) || n.SqrMagnitude() < 1e-6f * l2) return;   // straight: no plane yet
        hingeAxis = SafeNormalize(bone->Rotation().Inverse() * n, hingeAxis);
        hasHinge = true;
    }
    bool CapturedFor(const Transform* b) const { return bone && bone == b; }
};

/// Signed angle (degrees) of the twist component of `q` about `axis`, and that
/// twist. `q` must have w >= 0 (the short way round).
inline Quat TwistAbout(const Quat& q, const Vec3& axis, float& degrees) {
    const Vec3 v{q.x, q.y, q.z};
    const float d = Vec3::Dot(v, axis);
    Quat t{axis.x * d, axis.y * d, axis.z * d, q.w};
    const float m = t.Magnitude();
    if (m < 1e-6f) { degrees = 180.0f; return Quat::AngleAxis(180.0f, axis); }   // a half turn: the sign is anyone's
    t = {t.x / m, t.y / m, t.z / m, t.w / m};
    degrees = 2.0f * std::atan2(d, q.w) * Mathf::Rad2Deg;
    return t;
}

/// Clamp a bone's local rotation to `limit` around its rest frame. `bendFree`
/// leaves a Hinge's bend alone (a two-bone solve already chose it) and only
/// clamps the twist. Anything not finite comes back as the rest rotation.
inline Quat ApplyLimit(const Quat& localRot, const IKJointFrame& frame, const IKJointLimit& limit, bool bendFree = false) {
    if (!limit.Active()) return localRot;
    if (!Finite(localRot)) return frame.restLocal;
    const Vec3 tAxis = SafeNormalize(limit.autoAxes ? frame.twistAxis : limit.twistAxis, Vec3::Up);
    const Vec3 hAxis = SafeNormalize(limit.autoAxes ? frame.hingeAxis : limit.hingeAxis, Vec3::Right);
    Quat delta = (frame.restLocal.Inverse() * localRot).Normalized();
    if (delta.w < 0.0f) delta = {-delta.x, -delta.y, -delta.z, -delta.w};

    // delta = swing * twist: the roll about the child axis first, then whatever
    // moves that axis.
    float twistDeg = 0.0f;
    const Quat twist = TwistAbout(delta, tAxis, twistDeg);
    Quat swing = (delta * twist.Inverse()).Normalized();
    if (swing.w < 0.0f) swing = {-swing.x, -swing.y, -swing.z, -swing.w};
    const float twistClamped = Mathf::Clamp(twistDeg, limit.twistMin, limit.twistMax);

    if (limit.type == IKJointLimit::Type::Hinge) {
        if (!bendFree) {
            // A hinge only bends about its axis: keep that part of the swing,
            // within the bend range, and drop the rest.
            float bend = 0.0f;
            TwistAbout(swing, hAxis, bend);
            swing = Quat::AngleAxis(Mathf::Clamp(bend, limit.hingeMin, limit.hingeMax), hAxis);
        }
    } else {
        const Vec3 sv{swing.x, swing.y, swing.z};
        const float sl = sv.Magnitude();
        if (sl > 1e-6f) {
            const float swingDeg = 2.0f * std::atan2(sl, swing.w) * Mathf::Rad2Deg;
            if (swingDeg > limit.swingMax) swing = Quat::AngleAxis(limit.swingMax, sv * (1.0f / sl));
        }
    }
    const Quat out = (frame.restLocal * swing * Quat::AngleAxis(twistClamped, tAxis)).Normalized();
    return Finite(out) ? out : frame.restLocal;
}

} // namespace okay
