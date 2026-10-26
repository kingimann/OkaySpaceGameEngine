#pragma once
// ---------------------------------------------------------------------------
// IKMath — the small helpers every IK solver shares, so each one bends, blends
// and guards the same way:
//
//   * Finite / SafeNormalize: a NaN or infinite goal never reaches a bone;
//   * AimBone: the world rotation that turns a bone so its child lands on a
//     goal direction, keeping the bone's roll (what FromToRotation does);
//   * BlendWorldRotation: weight a solved rotation against the animated one
//     (rotations blend; positions never do, so bones keep their length);
//   * IKPole: one pole convention for every solver. The result is always a world
//     DIRECTION from the limb's root, whichever way it was asked for.
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Scene/Transform.hpp"
#include <cmath>

namespace okay {

inline bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline bool Finite(const Quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }

/// `v` as a unit vector, or `fallback` when `v` is too short or not finite.
inline Vec3 SafeNormalize(const Vec3& v, const Vec3& fallback) {
    const float m2 = v.SqrMagnitude();
    if (!std::isfinite(m2) || m2 < 1e-12f) return fallback;
    return v * (1.0f / std::sqrt(m2));
}

/// True when `t` sits somewhere below `ancestor` in the hierarchy.
inline bool IsUnder(const Transform* t, const Transform* ancestor) {
    for (const Transform* p = t ? t->Parent() : nullptr; p; p = p->Parent()) if (p == ancestor) return true;
    return false;
}

/// The world rotation that turns `bone` so the child now at `childNow` ends up on
/// the direction of `childGoal` (both world points). The bone's roll is kept, as
/// FromToRotation keeps it. A degenerate pair leaves the bone's current rotation.
inline Quat AimBone(const Transform* bone, const Vec3& childNow, const Vec3& childGoal) {
    const Quat cur = bone->Rotation();
    const Vec3 at = bone->Position();
    const Vec3 a = childNow - at, b = childGoal - at;
    if (!Finite(a) || !Finite(b) || a.SqrMagnitude() < 1e-12f || b.SqrMagnitude() < 1e-12f) return cur;
    const Quat q = (Quat::FromToRotation(a.Normalized(), b.Normalized()) * cur).Normalized();
    return Finite(q) ? q : cur;
}

/// Give `t` the blend of two world rotations: `from` at w = 0 (the animated pose),
/// `to` at w = 1 (the solve). Blending rotations keeps every bone's length.
inline void BlendWorldRotation(Transform* t, const Quat& from, const Quat& to, float w) {
    if (!t) return;
    t->SetRotation(w >= 1.0f ? to : (w <= 0.0f ? from : Quat::Slerp(from, to, w)));
}

/// Where a joint bends toward. The numbers match LimbIK's saved `poleMode`
/// (Auto 0, Character 1, World 2); Object is "there's a pole object".
enum class IKPoleMode { Auto = 0, Character = 1, World = 2, Object = 3 };

/// One pole helper for every solver. Call Direction() each frame with the limb's
/// root, mid and end; it returns the world direction (from the root) the mid
/// joint should bend toward.
///
/// Auto keeps the animation's own bend plane while the limb has one. As the limb
/// straightens (the mid joint's offset from the root->end line drops from 3% to
/// 0.5% of the limb's length) the direction eases over to the character-space
/// fallback, so nothing flips the frame the limb passes through straight: the
/// last good bend direction stands in while the limb is too straight to read.
/// The helper keeps that one direction per limb, so hold one per limb.
struct IKPole {
    static constexpr float kStraight = 0.005f;   ///< below this bend, the animated plane can't be read
    static constexpr float kBent     = 0.03f;    ///< above this bend, it's used as is

    Vec3 Direction(const Vec3& root, const Vec3& mid, const Vec3& end, const Quat& character,
                   IKPoleMode mode, const Vec3& dir, const Transform* poleObject) {
        if (poleObject) {
            const Vec3 d = poleObject->Position() - root;
            if (Finite(d) && d.SqrMagnitude() > 1e-12f) return d;
        }
        if (mode == IKPoleMode::World) return dir;
        const Vec3 fallback = character * dir;
        if (mode != IKPoleMode::Auto) return fallback;

        const Vec3 ax = end - root, up = mid - root;
        const float ax2 = ax.SqrMagnitude();
        const float len = up.Magnitude() + (end - mid).Magnitude();
        if (!Finite(ax) || !Finite(up) || ax2 < 1e-12f || !(len > 1e-6f)) return m_hasLast ? m_last : fallback;
        const Vec3 bend = up - ax * (Vec3::Dot(up, ax) / ax2);
        const float bent = bend.Magnitude() / len;
        float t = (bent - kStraight) / (kBent - kStraight);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        if (t > 0.0f) { m_last = bend.Normalized(); m_hasLast = true; }
        if (t >= 1.0f) return m_last;
        const Vec3 animated = m_hasLast ? m_last : SafeNormalize(fallback, Vec3::Forward);
        const Vec3 fb = SafeNormalize(fallback, animated);
        return SafeNormalize(animated * t + fb * (1.0f - t), animated);
    }

    /// Forget the remembered bend (the bones were reassigned).
    void Reset() { m_hasLast = false; }

private:
    Vec3 m_last = Vec3::Zero;
    bool m_hasLast = false;
};

} // namespace okay
