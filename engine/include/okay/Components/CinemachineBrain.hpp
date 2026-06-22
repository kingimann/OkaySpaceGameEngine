#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/VirtualCamera.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <algorithm>
#include <vector>

namespace okay {

/// The Cinemachine "brain": lives on the real (main) camera and, each frame,
/// drives that camera from the live virtual camera. It solves every
/// VirtualCamera, makes the highest-`priority` enabled one live, and eases the
/// real camera's pose and field of view toward it over `blendTime` whenever the
/// live vcam changes — giving smooth, designer-controlled shot transitions.
class CinemachineBrain : public Behaviour {
public:
    /// Seconds to ease between virtual cameras when the live one changes (0 = cut).
    float blendTime = 1.0f;

    void LateUpdate(float dt) override {
        Scene* s = GetScene();
        if (!s || !transform || !gameObject) return;

        // Solve all vcams once (defined order) and pick the live one by priority.
        std::vector<VirtualCamera*> vcams = s->FindObjectsOfType<VirtualCamera>();
        VirtualCamera* live = nullptr;
        for (VirtualCamera* v : vcams) {
            if (!v->enabled || !v->gameObject || !v->gameObject->active) continue;
            v->Solve(dt);
            if (!live || v->priority > live->priority) live = v;
        }
        if (!live) return;

        Camera* cam = gameObject->GetComponent<Camera>();

        // A new live vcam starts a fresh blend from the camera's current pose.
        if (live != m_active) {
            m_fromPos = transform->Position();
            m_fromRot = transform->Rotation();
            m_fromFov = cam ? cam->fieldOfView : 60.0f;
            m_active  = live;
            m_blend   = (blendTime <= 0.0f) ? 1.0f : 0.0f;
        }
        if (m_blend < 1.0f) m_blend = std::min(1.0f, m_blend + dt / std::max(blendTime, 1e-4f));

        // Ease the real camera toward the live vcam's solved pose + lens.
        transform->SetPosition(Vec3::Lerp(m_fromPos, live->SolvedPosition(), m_blend));
        if (!transform->Parent())
            transform->localRotation = Quat::Slerp(m_fromRot, live->SolvedRotation(), m_blend);
        if (cam)
            cam->fieldOfView = m_fromFov + (live->fieldOfView - m_fromFov) * m_blend;
    }

    /// The virtual camera currently driving the brain (read-only; runtime).
    VirtualCamera* LiveCamera() const { return m_active; }

private:
    VirtualCamera* m_active = nullptr;
    Vec3 m_fromPos{0.0f, 0.0f, 0.0f};
    Quat m_fromRot = Quat::Identity;
    float m_fromFov = 60.0f;
    float m_blend = 1.0f;
};

} // namespace okay
