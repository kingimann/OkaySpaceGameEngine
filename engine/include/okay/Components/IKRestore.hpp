#pragma once
// ---------------------------------------------------------------------------
// IKRestore — an IK solver's corrections never pile up. A solver Touch()es each
// bone before it writes it (the animated local pose is saved) and Commit()s at
// the end of its solve (what it wrote is saved). Next frame Restore() puts the
// animated pose back wherever nothing rewrote the bone since: a rotation-only
// clip never rewrites positions, and a still rig rewrites nothing. Kept in LOCAL
// space, so moving the body doesn't hide that a bone still holds a correction.
// ---------------------------------------------------------------------------
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <vector>

namespace okay {

struct IKRestore {
    /// Note a bone's animated pose before this frame first moves it.
    void Touch(Transform* t) {
        if (!t) return;
        for (const Saved& s : m_saved) if (s.t == t) return;
        m_saved.push_back({t, t->localPosition, t->localPosition, t->localRotation, t->localRotation});
    }
    /// After the solve: remember what was written.
    void Commit() {
        for (Saved& s : m_saved) { s.pos = s.t->localPosition; s.rot = s.t->localRotation; }
    }
    /// Before the solve: take last frame's writes back off where they still stand.
    void Restore() {
        for (const Saved& s : m_saved) {
            const Vec3& p = s.t->localPosition; const Quat& q = s.t->localRotation;
            if (p.x == s.pos.x && p.y == s.pos.y && p.z == s.pos.z) s.t->localPosition = s.prePos;
            if (q.x == s.rot.x && q.y == s.rot.y && q.z == s.rot.z && q.w == s.rot.w) s.t->localRotation = s.preRot;
        }
        m_saved.clear();
    }
    /// Forget the bones (they were reassigned or destroyed) without touching them.
    void Clear() { m_saved.clear(); }

private:
    struct Saved { Transform* t; Vec3 prePos, pos; Quat preRot, rot; };
    std::vector<Saved> m_saved;
};

} // namespace okay
