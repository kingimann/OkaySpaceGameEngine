#pragma once
// ---------------------------------------------------------------------------
// Shared "game feel" pieces for the first- and third-person controllers:
//   * AnimLink  — feeds an AnimStateMachine on the player (or its model) the
//                 usual locomotion parameters: speed, verticalSpeed, grounded,
//                 crouch, prone, sprint, slide, aim, plus jump / land triggers.
//                 Declare whichever you use in the Animator; the rest are ignored.
//   * FovKick   — eases a camera's field of view toward base + sprint boost, or a
//                 zoom FOV while aiming.
//   * Landing   — detects touchdown and how hard it was.
//   * LookSmoother — optional mouse smoothing.
//   * FindInTree<T> — a component on the object or any descendant.
// ---------------------------------------------------------------------------
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include <cmath>
#include <functional>

namespace okay {

template <class T>
inline T* FindInTree(GameObject* go, int depth = 0) {
    if (!go || depth > 6) return nullptr;
    if (auto* c = go->GetComponent<T>()) return c;
    if (go->transform)
        for (Transform* ch : go->transform->Children())
            if (ch && ch->gameObject) if (auto* c = FindInTree<T>(ch->gameObject, depth + 1)) return c;
    return nullptr;
}

/// Locomotion -> AnimStateMachine parameters.
struct AnimLink {
    struct Frame {
        float speed = 0.0f, verticalSpeed = 0.0f;
        bool grounded = true, crouch = false, prone = false, sprint = false, slide = false, aim = false;
        bool jumped = false, landed = false;
    };
    void Apply(GameObject* owner, const Frame& f) {
        if (!m_sm || m_owner != owner) { m_owner = owner; m_sm = FindInTree<AnimStateMachine>(owner); }
        if (!m_sm) return;
        m_sm->SetFloat("speed", f.speed);
        m_sm->SetFloat("verticalSpeed", f.verticalSpeed);
        m_sm->SetBool("grounded", f.grounded);
        m_sm->SetBool("crouch", f.crouch);
        m_sm->SetBool("prone", f.prone);
        m_sm->SetBool("sprint", f.sprint);
        m_sm->SetBool("slide", f.slide);
        m_sm->SetBool("aim", f.aim);
        if (f.jumped) m_sm->SetTrigger("jump");
        if (f.landed) m_sm->SetTrigger("land");
    }
    AnimStateMachine* machine() const { return m_sm; }
private:
    GameObject* m_owner = nullptr;
    AnimStateMachine* m_sm = nullptr;
};

/// Eased field of view: base (captured from the camera) + sprint boost, or a zoom FOV.
struct FovKick {
    void Update(Camera* cam, bool sprinting, float boost, bool zoom, float zoomFov, float lerp, float dt) {
        if (!cam) return;
        if (m_cam != cam) { m_cam = cam; m_base = cam->fieldOfView; m_cur = m_base; }
        // Someone else changed the FOV (a script, the inspector): adopt it as the new base.
        if (std::fabs(cam->fieldOfView - m_cur) > 0.01f) { m_base = cam->fieldOfView - (m_cur - m_base); m_cur = cam->fieldOfView; }
        float want = zoom ? zoomFov : m_base + (sprinting ? boost : 0.0f);
        float t = lerp > 0.0f ? 1.0f - std::exp(-lerp * dt) : 1.0f;
        m_cur += (want - m_cur) * t;
        cam->fieldOfView = m_cur;
    }
    float zoomAmount() const { return m_base > 1e-3f ? m_cur / m_base : 1.0f; }
private:
    Camera* m_cam = nullptr;
    float m_base = 60.0f, m_cur = 60.0f;
};

/// Touchdown detection: Step returns true on the frame the player lands and
/// reports the downward speed it hit the ground with.
struct Landing {
    bool Step(bool grounded, float vy, float& impact) {
        bool landed = grounded && !m_wasGrounded && m_airTime > 0.08f;
        impact = landed ? -m_minVy : 0.0f;
        if (grounded) { m_airTime = 0.0f; m_minVy = 0.0f; }
        else { m_airTime += m_dt; if (vy < m_minVy) m_minVy = vy; }
        m_wasGrounded = grounded;
        return landed;
    }
    void SetDt(float dt) { m_dt = dt; }
private:
    bool m_wasGrounded = true;
    float m_airTime = 0.0f, m_minVy = 0.0f, m_dt = 0.016f;
};

/// Exponential smoothing for mouse deltas (0 = raw).
struct LookSmoother {
    void Step(float& dx, float& dy, float smoothing, float dt) {
        if (smoothing <= 0.0f) { m_x = dx; m_y = dy; return; }
        float t = 1.0f - std::exp(-dt / std::max(1e-4f, smoothing));
        m_x += (dx - m_x) * t; m_y += (dy - m_y) * t;
        dx = m_x; dy = m_y;
    }
private:
    float m_x = 0.0f, m_y = 0.0f;
};

} // namespace okay
