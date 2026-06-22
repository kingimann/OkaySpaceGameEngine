#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Input/Input.hpp"
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

    /// How the follow offset is interpreted (Cinemachine's Binding Mode):
    ///  - WorldSpace:    offset is fixed in world axes (a static framing).
    ///  - LockToTarget:  offset rotates with the follow target — an orbital/3rd-person
    ///                   chase cam that stays behind the target as it turns.
    enum class BindingMode { WorldSpace, LockToTarget };
    BindingMode bindingMode = BindingMode::WorldSpace;

    /// Name of the GameObject to position relative to (the body). Empty = stay put.
    std::string follow;
    /// Name of the GameObject to aim at (the aim). Empty = keep follow's facing.
    std::string lookAt;
    /// World-space offset from the follow target (e.g. behind & above for 3rd person).
    Vec3 followOffset{0.0f, 3.0f, -8.0f};
    /// Extra offset added to the LookAt target before aiming (e.g. aim at the head,
    /// not the feet). Cinemachine's "Tracked Object Offset".
    Vec3 lookAtOffset{0.0f, 0.0f, 0.0f};
    /// Aim dead zone (degrees): the camera only re-aims once the target drifts beyond
    /// this angle from screen center, so small target motion doesn't jitter the shot
    /// (a simplified Cinemachine Composer dead zone). 0 = always track.
    float aimDeadZone = 0.0f;

    /// Easing rates (higher = snappier; <= 0 = instant). Frame-rate independent.
    float positionDamping = 2.0f;
    float rotationDamping  = 6.0f;

    /// Lens: the field of view (deg) the brain applies to the real camera when live.
    float fieldOfView = 60.0f;

    /// Handheld noise: peak positional shake (world units) and its speed.
    float shakeAmplitude = 0.0f;
    float shakeFrequency = 1.0f;
    /// How fast a one-shot impulse (AddImpulse) decays (per second). Higher = snappier.
    float impulseDecay = 3.0f;

    /// Fire a transient camera shake — a "Cinemachine Impulse". Call it on an event
    /// (explosion, landing, hit) and the live camera kicks, then settles. Additive.
    void AddImpulse(float strength) { m_impulse += strength; }

    /// FreeLook (Cinemachine's orbital rig): orbit the Follow target on a sphere,
    /// driven by yaw/pitch. When on, this overrides the body offset and aim — the
    /// camera always frames the target. Great for 3rd-person player cameras.
    bool freeLook = false;
    float orbitYaw = 0.0f;       ///< horizontal orbit angle (deg)
    float orbitPitch = 15.0f;    ///< vertical orbit angle (deg), clamped below
    float orbitRadius = 8.0f;    ///< distance from the target
    float orbitHeight = 1.0f;    ///< height of the orbit pivot above the target
    float orbitMinPitch = -20.0f, orbitMaxPitch = 70.0f;
    /// Drive yaw/pitch from the mouse. orbitButton = mouse button to hold (-1 = always);
    /// mouseSensitivity scales the look speed.
    bool orbitInput = true;
    int  orbitButton = 1;        ///< 1 = right mouse button
    float mouseSensitivity = 0.2f;

    /// Advance the solved pose one step toward the targets. Driven by the brain so
    /// every vcam solves exactly once per frame, in a defined order.
    void Solve(float dt) {
        Scene* s = GetScene();
        if (!s || !transform) return;

        GameObject* f = follow.empty() ? nullptr : s->Find(follow);
        GameObject* l = lookAt.empty() ? nullptr : s->Find(lookAt);

        // FreeLook orbit rig: position is a point on a sphere around the target pivot.
        bool freeLookActive = freeLook && f && f->transform;
        Vec3 flPivot{0.0f, 0.0f, 0.0f};

        Vec3 desiredPos = transform->Position();
        if (freeLookActive) {
            if (orbitInput) {
                Vec2 m = Input::MousePosition();
                if (!m_mouseInit) { m_lastMouse = m; m_mouseInit = true; }
                Vec2 d = m - m_lastMouse;
                m_lastMouse = m;
                bool active = (orbitButton < 0) || Input::GetMouseButton(orbitButton);
                if (active) {
                    orbitYaw   += d.x * mouseSensitivity;
                    orbitPitch -= d.y * mouseSensitivity;
                }
            }
            orbitPitch = Mathf::Clamp(orbitPitch, orbitMinPitch, orbitMaxPitch);
            flPivot = f->transform->Position() + Vec3{0.0f, orbitHeight, 0.0f};
            Quat orot = Quat::Euler(orbitPitch, orbitYaw, 0.0f);
            desiredPos = flPivot + orot * Vec3{0.0f, 0.0f, -orbitRadius};
        } else if (f && f->transform) {
            // LockToTarget orbits the offset with the target's heading (chase cam);
            // WorldSpace keeps it axis-aligned (a fixed framing).
            Vec3 worldOffset = (bindingMode == BindingMode::LockToTarget)
                                   ? (f->transform->Rotation() * followOffset)
                                   : followOffset;
            desiredPos = f->transform->Position() + worldOffset;
        }
        if (!m_init) m_pos = desiredPos;
        else {
            float tp = (positionDamping <= 0.0f) ? 1.0f : (1.0f - std::exp(-positionDamping * dt));
            m_pos = Vec3::Lerp(m_pos, desiredPos, tp);
        }

        // Aim from the (damped) body position so the shot stays framed while easing.
        Quat desiredRot = m_rot;
        if (freeLookActive) {
            Vec3 dir = flPivot - m_pos;
            if (dir.SqrMagnitude() > 1e-8f) desiredRot = Quat::LookRotation(dir);
        } else if (l && l->transform) {
            Vec3 dir = (l->transform->Position() + lookAtOffset) - m_pos;
            if (dir.SqrMagnitude() > 1e-8f) {
                // Composer dead zone: keep the current aim while the target stays within
                // aimDeadZone degrees of screen center; only re-aim past it.
                bool reaim = true;
                if (m_init && aimDeadZone > 0.0f) {
                    Vec3 fwd = m_rot * Vec3::Forward;
                    float c = Vec3::Dot(fwd.Normalized(), dir.Normalized());
                    if (c > std::cos(aimDeadZone * Mathf::Deg2Rad)) reaim = false;
                }
                if (reaim) desiredRot = Quat::LookRotation(dir);
            }
        } else if (f && f->transform) {
            desiredRot = f->transform->Rotation();
        }
        if (!m_init) m_rot = desiredRot;
        else {
            float tr = (rotationDamping <= 0.0f) ? 1.0f : (1.0f - std::exp(-rotationDamping * dt));
            m_rot = Quat::Slerp(m_rot, desiredRot, tr);
        }
        m_init = true;

        // Handheld shake (steady) + impulse (one-shot, decaying): smooth gradient
        // noise, decorrelated per axis. Impulse rides a faster carrier so a single
        // kick reads as a sharp jolt even with no steady shake.
        Vec3 shake{0.0f, 0.0f, 0.0f};
        if (shakeAmplitude > 1e-5f) {
            m_noiseT += dt * shakeFrequency;
            shake += shakeAmplitude * Vec3{Noise(m_noiseT, 0.0f), Noise(m_noiseT, 17.3f), Noise(m_noiseT, 41.7f)};
        }
        if (m_impulse > 1e-5f) {
            m_impulseT += dt * 12.0f;
            shake += m_impulse * Vec3{Noise(m_impulseT, 3.1f), Noise(m_impulseT, 23.7f), Noise(m_impulseT, 57.2f)};
            m_impulse *= std::exp(-impulseDecay * dt);
            if (m_impulse < 1e-4f) m_impulse = 0.0f;
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
    float m_impulse = 0.0f;   // current one-shot impulse amplitude (decays)
    float m_impulseT = 0.0f;
    Vec2 m_lastMouse{0.0f, 0.0f};
    bool m_mouseInit = false;

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
