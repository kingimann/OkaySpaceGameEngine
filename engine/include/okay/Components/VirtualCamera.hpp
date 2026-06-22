#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <cmath>
#include <string>

namespace okay {

/// A Cinemachine-style virtual camera. Unlike a real Camera it never renders;
/// instead it describes a *shot* — where to stand (Follow), what to aim at
/// (LookAt), the lens (field of view) and how smoothly to ease there (damping),
/// plus optional handheld shake. A CinemachineBrain on the real (main) camera
/// picks the highest-`priority` enabled virtual camera and blends the real
/// camera toward its solved pose. Author several and switch shots by changing
/// priorities — exactly like Unity's Cinemachine.
class VirtualCamera : public Behaviour {
public:
    /// Higher wins. The brain makes the highest-priority enabled vcam "live".
    int priority = 10;

    /// Name of the GameObject to position relative to (the body). Empty = stay put.
    std::string follow;
    /// Name of the GameObject to aim at (the aim). Empty = keep follow's facing.
    std::string lookAt;
    /// World-space offset from the follow target (e.g. behind & above for 3rd person).
    Vec3 followOffset{0.0f, 3.0f, -8.0f};

    /// Easing rates (higher = snappier; <= 0 = instant). Frame-rate independent.
    float positionDamping = 2.0f;
    float rotationDamping  = 6.0f;

    /// Lens: the field of view (deg) the brain applies to the real camera when live.
    float fieldOfView = 60.0f;

    /// Handheld noise: peak positional shake (world units) and its speed.
    float shakeAmplitude = 0.0f;
    float shakeFrequency = 1.0f;

    /// Advance the solved pose one step toward the targets. Driven by the brain so
    /// every vcam solves exactly once per frame, in a defined order.
    void Solve(float dt) {
        Scene* s = GetScene();
        if (!s || !transform) return;

        GameObject* f = follow.empty() ? nullptr : s->Find(follow);
        GameObject* l = lookAt.empty() ? nullptr : s->Find(lookAt);

        Vec3 desiredPos = (f && f->transform) ? (f->transform->Position() + followOffset)
                                              : transform->Position();
        if (!m_init) m_pos = desiredPos;
        else {
            float tp = (positionDamping <= 0.0f) ? 1.0f : (1.0f - std::exp(-positionDamping * dt));
            m_pos = Vec3::Lerp(m_pos, desiredPos, tp);
        }

        // Aim from the (damped) body position so the shot stays framed while easing.
        Quat desiredRot = m_rot;
        if (l && l->transform) {
            Vec3 dir = l->transform->Position() - m_pos;
            if (dir.SqrMagnitude() > 1e-8f) desiredRot = Quat::LookRotation(dir);
        } else if (f && f->transform) {
            desiredRot = f->transform->Rotation();
        }
        if (!m_init) m_rot = desiredRot;
        else {
            float tr = (rotationDamping <= 0.0f) ? 1.0f : (1.0f - std::exp(-rotationDamping * dt));
            m_rot = Quat::Slerp(m_rot, desiredRot, tr);
        }
        m_init = true;

        // Handheld shake: smooth gradient noise, decorrelated per axis.
        Vec3 shake{0.0f, 0.0f, 0.0f};
        if (shakeAmplitude > 1e-5f) {
            m_noiseT += dt * shakeFrequency;
            shake = {Noise(m_noiseT, 0.0f), Noise(m_noiseT, 17.3f), Noise(m_noiseT, 41.7f)};
            shake *= shakeAmplitude;
        }
        m_solvedPos = m_pos + shake;

        // Mirror the solved pose onto our own transform so the vcam GameObject sits
        // where the shot is (handy for gizmos / scene-view debugging at runtime).
        transform->SetPosition(m_solvedPos);
        if (!transform->Parent()) transform->localRotation = m_rot;
    }

    const Vec3& SolvedPosition() const { return m_solvedPos; }
    const Quat& SolvedRotation() const { return m_rot; }

private:
    bool m_init = false;
    Vec3 m_pos{0.0f, 0.0f, 0.0f};
    Quat m_rot = Quat::Identity;
    Vec3 m_solvedPos{0.0f, 0.0f, 0.0f};
    float m_noiseT = 0.0f;

    /// Smooth 1-D value noise in [-1,1] (interpolated hash). Good enough for shake.
    static float Noise(float t, float seed) {
        t += seed;
        float fl = std::floor(t);
        int i = (int)fl;
        float fr = t - fl;
        float u = fr * fr * (3.0f - 2.0f * fr);   // smoothstep
        auto h = [](int n) {
            n = (n << 13) ^ n;
            int m = (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff;
            return 1.0f - (float)m / 1073741824.0f;   // [-1,1]
        };
        return h(i) * (1.0f - u) + h(i + 1) * u;
    }
};

} // namespace okay
