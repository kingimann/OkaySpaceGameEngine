#pragma once
// ---------------------------------------------------------------------------
// FootIK — plant a biped's feet on the ground (Unity's foot IK). Each frame it
// raycasts down under each foot, and if the ground is above where the animation
// put the foot, it solves a two-bone IK (hip -> knee -> foot) so the foot sits on
// the surface and the knee bends naturally — no more feet floating over slopes or
// sinking into steps. Blendable via `weight`.
//
// Assign the leg-bone Transforms (hips/knee/foot for each leg). Bone lengths are
// learned from the rig on the first solve. Knees bend toward the body's forward.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/TwoBoneIK.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

class FootIK : public Component {
public:
    // Left leg chain.
    Transform* leftHip = nullptr;  Transform* leftKnee = nullptr;  Transform* leftFoot = nullptr;
    // Right leg chain.
    Transform* rightHip = nullptr; Transform* rightKnee = nullptr; Transform* rightFoot = nullptr;

    float weight     = 1.0f;   ///< 0 = off (pure animation), 1 = fully planted
    float footOffset = 0.05f;  ///< ankle height to keep above the ground surface
    bool  useRaycast = true;   ///< raycast the scene for ground; else use groundY
    float groundY    = 0.0f;   ///< fallback ground height when not raycasting
    float maxRayUp   = 0.6f;   ///< how far above the foot to start the ray
    float maxRayDown = 0.8f;   ///< how far below the foot the ground may be

    void Update(float) override {
        if (weight <= 0.0f) return;
        if (!m_init) { Learn(); m_init = true; }
        Vec3 pole = (transform ? transform->Rotation() : Quat::Identity) * Vec3::Forward;
        Scene* s = GetScene();
        SolveLeg(leftHip,  leftKnee,  leftFoot,  m_lUp, m_lLo, pole, s);
        SolveLeg(rightHip, rightKnee, rightFoot, m_rUp, m_rLo, pole, s);
    }

private:
    void Learn() {
        if (leftHip && leftKnee && leftFoot) {
            m_lUp = (leftKnee->Position() - leftHip->Position()).Magnitude();
            m_lLo = (leftFoot->Position() - leftKnee->Position()).Magnitude();
        }
        if (rightHip && rightKnee && rightFoot) {
            m_rUp = (rightKnee->Position() - rightHip->Position()).Magnitude();
            m_rLo = (rightFoot->Position() - rightKnee->Position()).Magnitude();
        }
    }

    float GroundAt(Scene* s, const Vec3& foot) {
        if (useRaycast && s) {
            Vec3 origin{foot.x, foot.y + maxRayUp, foot.z};
            RaycastHit3D hit = s->physics3D().Raycast(*s, origin, Vec3{0, -1, 0},
                                                      maxRayUp + maxRayDown, gameObject);
            if (hit.hit) return hit.point.y;
            return foot.y - 1e6f;       // nothing under the foot -> don't lift
        }
        return groundY;
    }

    void SolveLeg(Transform* hip, Transform* knee, Transform* foot,
                  float upLen, float loLen, const Vec3& pole, Scene* s) {
        if (!hip || !knee || !foot || upLen <= 0.0f || loLen <= 0.0f) return;
        Vec3 animFoot = foot->Position();
        float g = GroundAt(s, animFoot);
        float targetY = g + footOffset;
        // Only lift the foot UP to meet ground; never push it below its animated pose,
        // and ignore ground that's out of reach below.
        if (targetY <= animFoot.y || g < animFoot.y - maxRayDown) return;

        Vec3 target{animFoot.x, targetY, animFoot.z};
        Vec3 mid, end;
        SolveTwoBoneIK(hip->Position(), upLen, loLen, target, pole, mid, end);
        knee->SetPosition(Vec3::Lerp(knee->Position(), mid, weight));
        foot->SetPosition(Vec3::Lerp(foot->Position(), end, weight));
    }

    bool  m_init = false;
    float m_lUp = 0, m_lLo = 0, m_rUp = 0, m_rLo = 0;
};

} // namespace okay
