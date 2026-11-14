#pragma once
// ---------------------------------------------------------------------------
// IKRestore — an IK solver's corrections never pile up. A solver Touch()es each
// bone before it writes it (the animated local pose is saved) and Commit()s at
// the end of its solve (what it wrote is saved). Next frame Restore() puts the
// animated pose back wherever nothing rewrote the bone since: a rotation-only
// clip never rewrites positions, and a still rig rewrites nothing. Kept in LOCAL
// space, so moving the body doesn't hide that a bone still holds a correction.
// Solvers call Restore() in Update too: an animator's crossfade and layers (its
// LateUpdate) blend from what's on the bone, so for a channel the clip doesn't
// key, last frame's correction must already be off by then. The call at the
// start of the solve is then a no-op.
// ---------------------------------------------------------------------------
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <cmath>
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
            if (Same(s.t->localPosition, s.pos)) s.t->localPosition = s.prePos;
            if (Same(s.t->localRotation, s.rot)) s.t->localRotation = s.preRot;
        }
        m_saved.clear();
    }
    /// Forget the bones (they were reassigned or destroyed) without touching them.
    void Clear() { m_saved.clear(); }

private:
    struct Saved { Transform* t; Vec3 prePos, pos; Quat preRot, rot; };
    // "Still what we wrote", to float rounding: an animator that writes a bone's own
    // value back (renormalized, no track on that channel) hasn't re-posed it.
    static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
    static bool Same(const Vec3& a, const Vec3& b) {
        const float eps = 1e-6f * (1.0f + std::fabs(b.x) + std::fabs(b.y) + std::fabs(b.z));
        return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
    }
    static bool Same(const Quat& a, const Quat& b) {
        return Near(a.x, b.x, 1e-6f) && Near(a.y, b.y, 1e-6f) && Near(a.z, b.z, 1e-6f) && Near(a.w, b.w, 1e-6f);
    }
    std::vector<Saved> m_saved;
};

} // namespace okay
