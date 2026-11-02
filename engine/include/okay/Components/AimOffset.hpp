#pragma once
// ---------------------------------------------------------------------------
// Aim Offset — pitch and yaw added on the spine, chest, and upper arms so a
// character aims where the camera looks, or at a target object. Each bone has
// its own weight. Recoil kicks the pitch and eases back. This runs after item
// grips and does not change that code: it only turns these bones.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/IKRestore.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace okay {

class AimOffset : public Component {
public:
    int ExecutionOrder() const override { return 102; }   // after grips (90) and full-body IK (100)

    bool useCamera = true;
    std::string targetName;
    Transform* target = nullptr;
    float weight = 1.0f;
    float pitchClamp = 40.0f;
    float yawClamp = 70.0f;
    float spineWeight = 0.35f;
    float chestWeight = 0.7f;
    float armWeight = 0.85f;
    /// Degrees added to the pitch per recoil(1).
    float kick = 8.0f;
    /// How fast the kick eases back (per second). Higher is snappier.
    float recovery = 10.0f;

    void Recoil(float strength) {
        if (strength < 0.0f) strength = 0.0f;
        m_recoil += kick * strength;
    }
    float RecoilPitch() const { return m_recoil; }

    void Start() override {
        if (!target && gameObject && gameObject->scene() && !targetName.empty())
            if (GameObject* g = gameObject->scene()->Find(targetName)) target = g->transform;
        Gather();
    }

    void Update(float) override { m_restore.Restore(); }

    void LateUpdate(float dt) override { Solve(dt); }

    void Preview(float dt) { Solve(dt); }

private:
    struct Bone { Transform* t = nullptr; float w = 0.0f; };
    std::vector<Bone> m_bones;
    Transform* m_aimFrom = nullptr;   ///< chest, else the head: pitch is measured from here
    IKRestore m_restore;
    float m_recoil = 0.0f;
    bool m_gathered = false;

    void Gather() {
        m_bones.clear();
        m_aimFrom = nullptr;
        m_gathered = true;
        if (!gameObject || !gameObject->scene()) return;
        std::vector<Transform*> spines, chests, arms;
        Transform* head = nullptr;
        for (const auto& up : gameObject->scene()->Objects()) {
            if (!up || !up->transform || !up->IsSelfOrDescendantOf(gameObject)) continue;
            const int id = FullBodyIK::Classify(up->name);
            if (id == FullBodyIK::Spine) spines.push_back(up->transform);
            else if (id == FullBodyIK::Chest) chests.push_back(up->transform);
            else if (id == FullBodyIK::LUpperArm || id == FullBodyIK::RUpperArm) arms.push_back(up->transform);
            else if (id == FullBodyIK::Head) head = up->transform;
        }
        // Several spine links used to each take the full spine weight, so the
        // chest turned by the sum (and the arms by that plus the arm weight).
        // Split the spine share, then keep spine + chest from stacking past 1.
        const float spineSum = std::fmax(0.0f, spineWeight);
        const float chestSum = std::fmax(0.0f, chestWeight);
        const float chain = spineSum + chestSum;
        const float scale = chain > 1.0f ? 1.0f / chain : 1.0f;
        if (!spines.empty() && spineSum > 0.0f) {
            const float w = spineSum * scale / (float)spines.size();
            for (Transform* t : spines) m_bones.push_back({t, w});
        }
        if (!chests.empty() && chestSum > 0.0f) {
            const float w = chestSum * scale / (float)chests.size();
            for (Transform* t : chests) m_bones.push_back({t, w});
        }
        const float inherited = std::fmin(1.0f, chain);
        const float extra = std::fmax(0.0f, armWeight - inherited);
        for (Transform* t : arms) if (extra > 0.0f) m_bones.push_back({t, extra});
        m_aimFrom = !chests.empty() ? chests.back() : head;
        if (!m_aimFrom && !spines.empty()) m_aimFrom = spines.back();
        if (!m_aimFrom) m_aimFrom = head;
    }

    Transform* CameraTransform() const {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc || !sc->mainCamera || !sc->mainCamera->transform) return nullptr;
        return sc->mainCamera->transform;
    }

    void Solve(float dt) {
        m_restore.Restore();
        if (!m_gathered) Gather();
        if (recovery > 1e-4f)
            m_recoil += (0.0f - m_recoil) * (1.0f - std::exp(-recovery * dt));
        if (!transform || weight <= 0.0f || m_bones.empty()) { m_restore.Commit(); return; }

        if (!target && !targetName.empty() && gameObject->scene())
            if (GameObject* g = gameObject->scene()->Find(targetName)) target = g->transform;

        const Vec3 facing = transform->Rotation() * Vec3{0, 0, -1};
        const Vec3 right = transform->Rotation() * Vec3{1, 0, 0};
        const Vec3 from = m_aimFrom ? m_aimFrom->Position() : transform->Position();
        Vec3 aim = from + facing * 8.0f;
        if (target) aim = target->Position();
        else if (useCamera) {
            if (Transform* cam = CameraTransform()) {
                const Vec3 dir = cam->Rotation() * Vec3{0, 0, -1};
                aim = cam->Position() + dir * 12.0f;
            }
        }
        Vec3 to = aim - from;
        if (to.SqrMagnitude() < 1e-8f) { m_restore.Commit(); return; }
        const float yaw = Mathf::Clamp(std::atan2(Vec3::Dot(to, right), Vec3::Dot(to, facing)) * Mathf::Rad2Deg,
                                        -std::fabs(yawClamp), std::fabs(yawClamp));
        const float flat = std::sqrt(to.x * to.x + to.z * to.z);
        float pitch = Mathf::Clamp(std::atan2(to.y, flat) * Mathf::Rad2Deg, -std::fabs(pitchClamp), std::fabs(pitchClamp));
        pitch = Mathf::Clamp(pitch - m_recoil, -89.0f, 89.0f);

        const float wAll = Mathf::Clamp01(weight);
        for (const Bone& b : m_bones) {
            if (!b.t) continue;
            m_restore.Touch(b.t);
            const float w = wAll * b.w;
            if (w <= 0.0f) continue;
            const Quat add = Quat::AngleAxis(yaw * w, Vec3::Up) * Quat::AngleAxis(pitch * w, right);
            b.t->SetRotation((add * b.t->Rotation()).Normalized());
        }
        m_restore.Commit();
    }
};

} // namespace okay
