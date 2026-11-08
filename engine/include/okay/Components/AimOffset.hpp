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
#include "okay/Components/Character.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/IKRestore.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Physics/Physics3D.hpp"
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
        ResolveTarget();
        Gather();
    }

    void Update(float) override { m_restore.Restore(); }

    void LateUpdate(float dt) override { Solve(dt); }

    void Preview(float dt) { Solve(dt); }

    /// Edit mode turned the component off: put the bones back.
    void ReleasePreview() {
        m_restore.Restore();
        m_bones.clear();
        m_aimFrom = nullptr;
        m_gathered = false;
    }

    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        if (target && target->gameObject && dying.count(target->gameObject)) target = nullptr;
        auto going = [&](Transform* t) {
            return t && t->gameObject && dying.count(t->gameObject);
        };
        bool bone = going(m_aimFrom);
        for (const Bone& b : m_bones) if (going(b.t)) bone = true;
        if (bone) {
            m_restore.Clear();
            m_bones.clear();
            m_aimFrom = nullptr;
            m_gathered = false;
        }
    }

private:
    struct Bone { Transform* t = nullptr; float w = 0.0f; };
    std::vector<Bone> m_bones;
    Transform* m_aimFrom = nullptr;   ///< chest, else the head: pitch is measured from here
    IKRestore m_restore;
    float m_recoil = 0.0f;
    bool m_gathered = false;
    float m_gotSpine = -1.0f, m_gotChest = -1.0f, m_gotArm = -1.0f;

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
        m_gotSpine = spineWeight;
        m_gotChest = chestWeight;
        m_gotArm = armWeight;
    }

    void ResolveTarget() {
        if (!gameObject || !gameObject->scene()) return;
        if (target && target->gameObject && !targetName.empty() && target->gameObject->name != targetName)
            target = nullptr;
        if (!target && !targetName.empty())
            if (GameObject* g = gameObject->scene()->Find(targetName)) target = g->transform;
    }

    Transform* CameraTransform() const {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc || !sc->mainCamera || !sc->mainCamera->transform) return nullptr;
        return sc->mainCamera->transform;
    }

    /// Orbit pitch the third-person camera rests at (degrees, looking down is
    /// positive). 0 when this is not a third-person orbit.
    float RestOrbitPitch() const {
        auto read = [](GameObject* g, float& out) -> bool {
            if (!g) return false;
            if (auto* tp = g->GetComponent<ThirdPersonController>()) { out = tp->restPitch(); return true; }
            if (auto* sh = g->GetComponent<ThirdPersonShooterController>()) { out = sh->restPitch(); return true; }
            return false;
        };
        auto walk = [&](Transform* t, float& out) -> bool {
            for (; t; t = t->Parent())
                if (t->gameObject && read(t->gameObject, out)) return true;
            return false;
        };
        float rest = 0.0f;
        if (walk(transform, rest)) return rest;
        if (Transform* cam = CameraTransform())
            if (walk(cam, rest)) return rest;
        return 0.0f;
    }

    /// The player the camera should not hit: this object, or the ancestor that
    /// owns the third-person controller / character.
    GameObject* AimSelf() const {
        GameObject* self = gameObject;
        for (Transform* t = transform; t; t = t->Parent()) {
            if (!t->gameObject) continue;
            if (t->gameObject->GetComponent<ThirdPersonController>() ||
                t->gameObject->GetComponent<ThirdPersonShooterController>() ||
                t->gameObject->GetComponent<Character>())
                self = t->gameObject;
        }
        return self;
    }

    void Solve(float dt) {
        m_restore.Restore();
        // Bone weights are read when the bones are gathered. A later edit in
        // the inspector (or a rig that appears after Start) has to gather again.
        if (!m_gathered || m_bones.empty() ||
            m_gotSpine != spineWeight || m_gotChest != chestWeight || m_gotArm != armWeight)
            Gather();
        if (recovery > 1e-4f)
            m_recoil += (0.0f - m_recoil) * (1.0f - std::exp(-recovery * dt));
        if (!transform || weight <= 0.0f || m_bones.empty()) { m_restore.Commit(); return; }

        ResolveTarget();

        // The body's facing, not this object's local -Z. An imported Mixamo
        // model is yawed 180 so its mesh faces -Z, and its local +X is then the
        // body's left: pitching about that axis bows the chest forward.
        Vec3 facing, right;
        BodyAxes(facing, right);
        const Vec3 from = m_aimFrom ? m_aimFrom->Position() : transform->Position();
        Vec3 aim = from + facing * 8.0f;
        bool haveCamPitch = false;
        float camRelPitch = 0.0f;
        Vec3 camDir{0, 0, -1};
        if (target) aim = target->Position();
        else if (useCamera) {
            if (Transform* cam = CameraTransform()) {
                Vec3 dir = cam->Rotation() * Vec3{0, 0, -1};
                const float dlen = std::sqrt(dir.SqrMagnitude());
                if (dlen > 1e-6f) {
                    dir = dir * (1.0f / dlen);
                    camDir = dir;
                    // Crosshair: a hit along the view, otherwise a point far enough
                    // that the shoulder offset does not tilt the aim.
                    const float reach = 80.0f;
                    aim = cam->Position() + dir * reach;
                    if (Scene* sc = gameObject->scene()) {
                        Physics3D::IgnoreHierarchy own(AimSelf());
                        const RaycastHit3D hit = sc->physics3D().Raycast(*sc, cam->Position(), dir, reach);
                        if (hit.hit) aim = hit.point;
                    }
                    // A third-person orbit rests looking down. Pitch is how far the
                    // view has left that rest, so a level look does not bend the chest.
                    const float flatD = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                    const float down = -std::atan2(dir.y, std::max(flatD, 1e-4f)) * Mathf::Rad2Deg;
                    camRelPitch = RestOrbitPitch() - down;
                    haveCamPitch = true;
                }
            }
        }
        Vec3 to = aim - from;
        if (haveCamPitch) {
            // The crosshair heading is the camera's forward. A hit on the
            // ground a step ahead sits off to the shoulder side and yawed the
            // chest while the body was still looking ahead. Pitch stays the
            // angle off the resting orbit, above.
            Vec3 heading{camDir.x, 0.0f, camDir.z};
            if (heading.SqrMagnitude() > 1e-8f) to = heading.Normalized();
        }
        if (to.SqrMagnitude() < 1e-8f) { m_restore.Commit(); return; }
        const float yaw = Mathf::Clamp(std::atan2(Vec3::Dot(to, right), Vec3::Dot(to, facing)) * Mathf::Rad2Deg,
                                        -std::fabs(yawClamp), std::fabs(yawClamp));
        const float flat = std::sqrt(to.x * to.x + to.z * to.z);
        float pitch = haveCamPitch ? camRelPitch : std::atan2(to.y, flat) * Mathf::Rad2Deg;
        pitch = Mathf::Clamp(pitch, -std::fabs(pitchClamp), std::fabs(pitchClamp));
        pitch = Mathf::Clamp(pitch + m_recoil, -89.0f, 89.0f);

        const float wAll = Mathf::Clamp01(weight);
        for (const Bone& b : m_bones) {
            if (!b.t) continue;
            m_restore.Touch(b.t);
            const float w = wAll * b.w;
            if (w <= 0.0f) continue;
            // World yaw and pitch, written in this bone's parent space.
            // Positive pitch is about the body's right: the head goes back and up.
            // Recoil is degrees added to that pitch, not a scale on the aim vector.
            Transform* parent = b.t->Parent();
            const Quat parentRot = parent ? parent->Rotation() : Quat::Identity;
            Vec3 upAxis = parentRot.Inverse() * Vec3::Up;
            Vec3 rightAxis = parentRot.Inverse() * right;
            if (upAxis.SqrMagnitude() < 1e-8f) upAxis = Vec3::Up;
            if (rightAxis.SqrMagnitude() < 1e-8f) rightAxis = Vec3{1, 0, 0};
            b.t->localRotation = (Quat::AngleAxis(yaw * w, upAxis) *
                                  Quat::AngleAxis(pitch * w, rightAxis) *
                                  b.t->localRotation).Normalized();
        }
        m_restore.Commit();
    }

    /// Visual forward (-Z) and right of the character that owns this aim.
    void BodyAxes(Vec3& facing, Vec3& right) const {
        const Character* ch = nullptr;
        for (Transform* t = transform; t && !ch; t = t->Parent())
            if (t->gameObject) ch = t->gameObject->GetComponent<Character>();
        const Transform* src = (ch && ch->transform) ? ch->transform : transform;
        facing = src ? src->Rotation() * Vec3{0, 0, -1} : Vec3{0, 0, -1};
        facing.y = 0.0f;
        if (facing.SqrMagnitude() < 1e-6f) facing = Vec3{0, 0, -1};
        else facing = facing.Normalized();
        right = Vec3{-facing.z, 0.0f, facing.x};
        if (right.SqrMagnitude() < 1e-6f) right = Vec3{1, 0, 0};
        else right = right.Normalized();
    }
};

} // namespace okay
