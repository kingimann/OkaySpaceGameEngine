#pragma once
// ---------------------------------------------------------------------------
// RootMotion — drive the body from the animation's root movement (Unity's "Apply
// Root Motion"). Each frame it reads how far the animated `rootNode` bone moved,
// applies that to the GameObject's world position, and re-centers the bone so the
// mesh doesn't double-move. Answers the classic question "does the animation drive
// the physics, or the physics drive the animation?" with an explicit mode.
//
// Source-agnostic: works with any animation that moves `rootNode` (imported clip,
// AnimClip on an Animator, a hand-driven bone). Assign `rootNode` to the bone the
// locomotion clip translates (usually the hips/root).
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include <string>

namespace okay {

class RootMotion : public Component {
public:
    enum class Mode {
        AnimDrivesMotion,   ///< the clip's root movement moves the body (default)
        PhysicsDrivesAnim,  ///< the body moves itself; root motion is neutralized (cosmetic)
        Disabled
    };

    /// The animated bone whose translation is the root motion (e.g. the hips).
    Transform* rootNode = nullptr;
    std::string rootNodeName;    ///< editor/serialized: resolve `rootNode` by object name at Start
    int   mode = (int)Mode::AnimDrivesMotion;
    bool  lockHeight = true;     ///< keep root motion on the ground plane (ignore Y)
    bool  applyToRigidbody = false;  ///< write the motion as Rigidbody3D velocity (anim -> physics)
    float loopThreshold = 0.5f;  ///< ignore root jumps bigger than this (clip-loop seam)

    void Start() override {
        if (!rootNode && !rootNodeName.empty())
            if (Scene* s = GetScene())
                if (GameObject* g = s->Find(rootNodeName)) rootNode = g->transform;
    }

    void Update(float dt) override {
        if (!rootNode || mode == (int)Mode::Disabled || dt <= 0.0f) return;
        Vec3 raw = rootNode->localPosition;
        if (!m_have) { m_prev = raw; m_have = true; return; }

        Vec3 delta = raw - m_prev;
        m_prev = raw;
        if (lockHeight) delta.y = 0.0f;
        // Skip the big backward jump when a looping clip wraps its root back to start.
        if (delta.Magnitude() > loopThreshold) { Recenter(raw); return; }

        if (mode == (int)Mode::AnimDrivesMotion) {
            // Map the bone-local motion through the body's facing, then move the body.
            Vec3 world = (transform ? transform->Rotation() : Quat::Identity) * delta;
            if (lockHeight) world.y = 0.0f;
            if (applyToRigidbody) {
                if (auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr) {
                    rb->velocity.x = world.x / dt;
                    rb->velocity.z = world.z / dt;       // leave Y to gravity/jump
                } else if (transform) {
                    transform->SetPosition(transform->Position() + world);
                }
            } else if (transform) {
                transform->SetPosition(transform->Position() + world);
            }
        }
        // Both AnimDrivesMotion and PhysicsDrivesAnim re-center the bone so the mesh
        // stays under the body instead of sliding away from it.
        Recenter(raw);
    }

private:
    void Recenter(const Vec3& raw) {
        // Pin the bone's planar position back to its first-frame base (keep Y so a
        // crouch/jump pose still reads), and measure the next delta from there.
        if (!rootNode) return;
        if (!m_basedSet) { m_base = raw; m_basedSet = true; }
        Vec3 pinned = lockHeight ? Vec3{m_base.x, raw.y, m_base.z} : m_base;
        rootNode->localPosition = pinned;
        m_prev = pinned;
    }

    bool m_have = false, m_basedSet = false;
    Vec3 m_prev{0, 0, 0}, m_base{0, 0, 0};
};

} // namespace okay
