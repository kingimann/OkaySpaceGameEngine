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
//   * LookSmoother — optional look smoothing (of the view's angles).
//   * FollowPivot / SpringArm / ClaimMainCamera — the orbit cameras' follow
//                 point, wall collision, and sole ownership of the main camera.
//   * FindInTree<T> — a component on the object or any descendant.
// ---------------------------------------------------------------------------
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/CinemachineBrain.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/Scene.hpp"
#include <cstdint>
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
        Vec3 velocity{0, 0, 0};   ///< world velocity: sent as moveX / moveZ in the body's frame (2D blend trees)
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
        if (owner && owner->transform) {
            Vec3 local = owner->transform->InverseTransformDirection(f.velocity);
            m_sm->SetFloat("moveX", local.x);
            m_sm->SetFloat("moveZ", local.z);
        }
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

/// Look smoothing: eases the angles the view shows toward the look angles
/// (yaw the short way round), frame-rate independent, so the total turn is
/// exactly the mouse's and uneven frame times don't weight it unevenly.
/// `smoothing` is a time in seconds; 0 = the look angles as they are.
struct LookSmoother {
    void Step(float targetYaw, float targetPitch, float smoothing, float dt) {
        if (smoothing <= 0.0f || !m_have) { m_yaw = targetYaw; m_pitch = targetPitch; m_have = true; return; }
        const float t = 1.0f - std::exp(-dt / std::max(1e-4f, smoothing));
        m_yaw += std::remainder(targetYaw - m_yaw, 360.0f) * t;
        m_pitch += (targetPitch - m_pitch) * t;
    }
    float Yaw() const { return m_yaw; }
    float Pitch() const { return m_pitch; }
private:
    float m_yaw = 0.0f, m_pitch = 0.0f;
    bool m_have = false;
};

/// 1 - e^(-k dt): the share of the way an exponential ease covers this frame
/// (frame-rate independent; k <= 0 = all the way at once).
inline float EaseFactor(float k, float dt) { return k > 0.0f ? 1.0f - std::exp(-k * dt) : 1.0f; }

/// The point an orbit camera looks at and turns around: the player's head,
/// smoothed. Horizontal and vertical ease on their own (a slower vertical soaks
/// up steps, landings and bob), the lag never exceeds `maxLag`, and a jump of
/// more than 2 m in one frame (respawn, teleport) snaps instead of swooping.
/// The camera is then placed around this point with the current yaw / pitch /
/// distance, so its position and rotation always agree.
struct FollowPivot {
    Vec3 pos{0.0f, 0.0f, 0.0f};
    const Vec3& Step(const Vec3& target, float dampXZ, float dampY, float maxLag, float dt) {
        const Vec3 moved = target - m_last;
        m_last = target;
        if (!m_have || moved.Magnitude() > 2.0f) { pos = target; m_have = true; return pos; }
        const float h = EaseFactor(dampXZ, dt), v = EaseFactor(dampY, dt);
        pos.x += (target.x - pos.x) * h;
        pos.z += (target.z - pos.z) * h;
        pos.y += (target.y - pos.y) * v;
        const Vec3 lag = target - pos;
        const float l = lag.Magnitude();
        if (maxLag > 0.0f && l > maxLag) pos = target - lag * (maxLag / l);
        return pos;
    }
    void Reset() { m_have = false; }
private:
    Vec3 m_last{0.0f, 0.0f, 0.0f};
    bool m_have = false;
};

/// Camera collision for an orbit camera: how far the arm may reach from the
/// pivot along `dir` this frame. A sphere (not a thin ray, which flickers on
/// edges, poles and railings) is swept against solid colliders only (triggers
/// never pull the camera in), skipping the player's whole hierarchy. Blocked:
/// snap in, so nothing ever clips, unless the change is tiny (under a quarter of
/// the skin), which eases in so 1-2 cm of edge flicker doesn't pump. Clear: glide
/// back out.
struct SpringArm {
    float Step(Scene& sc, GameObject* self, const Vec3& from, const Vec3& dir, float want,
               float radius, float skin, std::uint32_t mask, float pullIn, float recover, float dt) {
        float allow = want;
        {
            Physics3D::IgnoreHierarchy own(self);
            const RaycastHit3D hit = radius > 0.0f
                ? sc.physics3D().SphereCast(sc, from, dir, radius, want + skin, self, mask, /*queryTriggers=*/false)
                : sc.physics3D().Raycast(sc, from, dir, want + skin, self, mask, /*queryTriggers=*/false);
            if (hit.hit) allow = std::max(0.0f, hit.distance - skin);
        }
        if (m_len < 0.0f) m_len = allow;                                  // first frame: the right length at once
        else if (allow < m_len - skin * 0.25f) m_len = allow;             // blocked: in at once
        else if (allow < m_len) m_len += (allow - m_len) * EaseFactor(pullIn, dt);    // a hair closer: ease
        else m_len += (allow - m_len) * EaseFactor(recover, dt);         // clear: glide back out
        return m_len;
    }
    void Reset() { m_len = -1.0f; }
private:
    float m_len = -1.0f;
};

/// An orbit controller places the main camera itself. A CameraFollow on that
/// camera, or a virtual-camera brain with a live virtual camera, would fight it
/// every frame: switch them off, saying so once.
inline void ClaimMainCamera(Camera* cam, const char* controller) {
    if (!cam || !cam->gameObject) return;
    GameObject* go = cam->gameObject;
    if (auto* f = go->GetComponent<CameraFollow>(); f && f->enabled) {
        f->enabled = false;
        OKAY_WARN(controller, " drives the main camera '", go->name, "': its Camera Follow was turned off "
                  "(the two would fight). Remove one of them.");
    }
    if (auto* b = go->GetComponent<CinemachineBrain>(); b && b->enabled)
        if (Scene* sc = go->scene()) {
            bool live = false;
            for (VirtualCamera* v : sc->FindObjectsOfType<VirtualCamera>())
                live = live || (v->enabled && v->gameObject && v->gameObject->active);
            if (live) {
                b->enabled = false;
                OKAY_WARN(controller, " drives the main camera '", go->name, "': its camera brain was turned "
                          "off (the two would fight). Remove one of them, or give the player another camera.");
            }
        }
}

} // namespace okay
