#pragma once
// Camera collision: the spring arm an orbit camera (the third-person controllers
// and the camera rigs) uses to stay out of walls. It lived in ControllerFeel.hpp;
// it moved here so the virtual-camera rigs share the same code (no second copy).
#include "okay/Components/CameraMath.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cstdint>

namespace okay {

/// Camera collision for an orbit camera: how far the arm may reach from the
/// pivot along `dir` this frame. A sphere (not a thin ray, which flickers on
/// edges, poles and railings) is swept against solid colliders only (triggers
/// never pull the camera in), skipping the player's whole hierarchy. Blocked:
/// snap in, so nothing ever clips, unless the change is tiny (under a quarter of
/// the skin), which eases in so 1-2 cm of edge flicker doesn't pump. Clear: glide
/// back out.
struct SpringArm {
    float Step(Scene& sc, GameObject* self, const Vec3& from, const Vec3& dir, float want,
               float radius, float skin, std::uint32_t mask, float pullIn, float recover, float dt, bool snapIn = true) {
        float allow = want;
        {
            Physics3D::IgnoreHierarchy own(self);
            const RaycastHit3D hit = radius > 0.0f
                ? sc.physics3D().SphereCast(sc, from, dir, radius, want + skin, self, mask, /*queryTriggers=*/false)
                : sc.physics3D().Raycast(sc, from, dir, want + skin, self, mask, /*queryTriggers=*/false);
            if (hit.hit) allow = std::max(0.0f, hit.distance - skin);
        }
        if (m_len < 0.0f) m_len = allow;                                  // first frame: the right length at once
        else if (snapIn && allow < m_len - skin * 0.25f) m_len = allow;   // blocked: in at once, never left inside (snapIn=false eases in instead)
        else if (allow < m_len) m_len += (allow - m_len) * EaseFactor(pullIn, dt);    // a hair closer: ease
        else m_len += (allow - m_len) * EaseFactor(recover, dt);         // clear: glide back out
        return m_len;
    }
    void Reset() { m_len = -1.0f; }
    /// The reach used last frame (< 0 before the first step).
    float Length() const { return m_len; }
private:
    float m_len = -1.0f;
};

} // namespace okay
