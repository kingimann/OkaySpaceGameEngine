#include "okay/Components/LookAtIK.hpp"
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/HumanoidRetarget.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Scene/Scene.hpp"

namespace okay {

namespace {

ModelAnimator* FindModelAnimator(GameObject* host) {
    if (!host) return nullptr;
    if (ModelAnimator* ma = host->GetComponent<ModelAnimator>()) return ma;
    Scene* sc = host->scene();
    if (!sc) return nullptr;
    for (const auto& up : sc->Objects()) {
        if (!up || up.get() == host || !up->IsSelfOrDescendantOf(host)) continue;
        if (ModelAnimator* ma = up->GetComponent<ModelAnimator>()) return ma;
    }
    return nullptr;
}

bool ClipReplacedThePose(Character* ch) {
    if (!ch || !ch->gameObject) return false;
    // No retarget: the built-in mesh already wears HeadYaw. Adding it again
    // on a bone chain would turn the head twice.
    if (!ch->gameObject->GetComponent<HumanoidRetarget>()) return false;
    ModelAnimator* ma = FindModelAnimator(ch->gameObject);
    return ma && ma->ClipOverridesPose();
}

} // namespace

void LookAtIK::ApplyCharacterGaze(float weightNow) {
    if (weightNow <= 0.0f || chain.empty()) return;
    Character* ch = nullptr;
    for (Transform* t = transform; t; t = t->Parent())
        if (t->gameObject)
            if ((ch = t->gameObject->GetComponent<Character>())) break;
    if (!ch || !ClipReplacedThePose(ch)) return;
    const float yaw = ch->HeadYaw();
    const float pitch = ch->HeadPitch();
    if (std::fabs(yaw) < 0.05f && std::fabs(pitch) < 0.05f) return;
    Transform* head = chain.back();
    if (!head) return;
    Transform* neck = nullptr;
    for (Transform* t = head->Parent(); t; t = t->Parent()) {
        if (t->gameObject && humanoid::IsNeckName(t->gameObject->name)) { neck = t; break; }
        if (!chain.empty() && t == chain.front()) break;
    }
    const Vec3 right = ch->gameObject->transform
        ? ch->gameObject->transform->Rotation() * Vec3{1.0f, 0.0f, 0.0f}
        : Vec3{1.0f, 0.0f, 0.0f};
    // HeadYaw is already the eased offset the controllers publish: positive
    // yaw about world up, then pitch about the body's right, matches a camera
    // that looks to the body's right and up.
    const Quat yawQ = Quat::AngleAxis(yaw, Vec3::Up);
    const Quat extra = (Quat::AngleAxis(pitch, yawQ * right) * yawQ).Normalized();
    const float w = weightNow < 0.0f ? 0.0f : (weightNow > 1.0f ? 1.0f : weightNow);
    // The head's world rotation is taken BEFORE the neck write. Setting the head
    // to the full gaze afterwards leaves the neck with its share and the skull
    // on the look, even if a parent write does not refresh a cached child.
    const Quat head0 = head->Rotation();
    if (neck) {
        m_restore.Touch(neck);
        neck->SetRotation((Quat::Slerp(Quat::Identity, extra, 0.45f * w) * neck->Rotation()).Normalized());
    }
    m_restore.Touch(head);
    head->SetRotation((Quat::Slerp(Quat::Identity, extra, w) * head0).Normalized());
    m_restore.Commit();
}

} // namespace okay
