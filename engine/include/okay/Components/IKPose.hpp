#pragma once
// ---------------------------------------------------------------------------
// IKPose — pose a limb by its end: move a hand / foot (any bone) toward a world
// target and the two bones above it TURN to reach it (shoulder -> elbow -> hand,
// hip -> knee -> foot). Bones keep their lengths and the mesh skinned to them
// bends naturally; the current bend direction is kept (an elbow that bends
// forward keeps bending forward). Used by the editor's IK posing tool.
// ---------------------------------------------------------------------------
#include "okay/Scene/Transform.hpp"
#include "okay/Math/TwoBoneIK.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"

namespace okay {

/// Turn `end`'s parent and grandparent so `end` lands on `target` (or as close as
/// the limb reaches). `bendHint` (world direction) is used when the limb is
/// straight and has no bend of its own. Returns false if `end` has no grandparent.
inline bool PoseTwoBoneChain(Transform* end, const Vec3& target, const Vec3& bendHint = Vec3::Forward) {
    if (!end || !end->Parent() || !end->Parent()->Parent()) return false;
    Transform* mid = end->Parent();
    Transform* root = mid->Parent();
    Vec3 r = root->Position(), m = mid->Position(), e = end->Position();
    float up = (m - r).Magnitude(), lo = (e - m).Magnitude();
    if (up < 1e-6f || lo < 1e-6f) return false;
    // Keep the limb's current bend direction.
    Vec3 axis = e - r;
    float al = axis.Magnitude();
    Vec3 pole = bendHint;
    if (al > 1e-6f) {
        Vec3 n = axis * (1.0f / al);
        Vec3 off = (m - r) - n * Vec3::Dot(m - r, n);
        if (off.SqrMagnitude() > 1e-6f * up * up) pole = off.Normalized();
    }
    Vec3 nm, ne;
    SolveTwoBoneIK(r, up, lo, target, pole, nm, ne);
    Vec3 a = m - r, b = nm - r;
    if (a.SqrMagnitude() > 1e-12f && b.SqrMagnitude() > 1e-12f)
        root->SetRotation((Quat::FromToRotation(a, b) * root->Rotation()).Normalized());
    Vec3 cm = mid->Position(), ce = end->Position();
    Vec3 c = ce - cm, d = ne - cm;
    if (c.SqrMagnitude() > 1e-12f && d.SqrMagnitude() > 1e-12f)
        mid->SetRotation((Quat::FromToRotation(c, d) * mid->Rotation()).Normalized());
    return true;
}

} // namespace okay
