// RagdollShared - see the header.
#include "okay/Components/RagdollShared.hpp"
#include "okay/Components/AimIK.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/ChainIK.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/DeathLock.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/FreeRoamController.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/HumanoidRetarget.hpp"
#include "okay/Components/LimbIK.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/Platformer25DController.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Net/PlayerInput.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"

namespace okay {
namespace ragdoll {

namespace {
template <typename T> bool Is(const Component* c) { return dynamic_cast<const T*>(c) != nullptr; }

void CollectTree(Transform* t, std::vector<Transform*>& out) {
    if (!t) return;
    out.push_back(t);
    for (Transform* c : t->Children()) CollectTree(c, out);
}
} // namespace

unsigned DriverKind(const Component* c) {
    if (!c) return 0;
    if (Is<Character>(c) || Is<Animator>(c) || Is<ModelAnimator>(c) || Is<AnimStateMachine>(c) || Is<AimIK>(c)
        || Is<ChainIK>(c) || Is<FootIK>(c) || Is<FullBodyIK>(c) || Is<LimbIK>(c) || Is<LookAtIK>(c)
        || Is<HumanoidRetarget>(c))
        return kAnimation;
    if (Is<IPlayerSim>(c) || Is<NPCController>(c) || Is<ClickToMoveController>(c) || Is<FreeRoamController>(c)
        || Is<Platformer25DController>(c) || Is<ThirdPersonShooterController>(c) || Is<TopDownController>(c)
        || Is<TopDownShooterController>(c) || Is<Rigidbody3D>(c))
        return kMovement;
    return 0;
}

void SwitchOff(GameObject* root, const Component* self, unsigned kinds, bool wholeTree, std::vector<Component*>& switched) {
    if (!root || !root->transform) return;
    std::vector<Transform*> tree;
    if (wholeTree) CollectTree(root->transform, tree);
    else tree.push_back(root->transform);
    for (Transform* t : tree) {
        if (!t->gameObject) continue;
        for (Component* c : t->gameObject->GetComponents<Component>()) {
            if (c == self || !c->enabled || !(DriverKind(c) & kinds)) continue;
            if (auto* rb = dynamic_cast<Rigidbody3D*>(c)) rb->velocity = Vec3::Zero;
            c->enabled = false;
            switched.push_back(c);
        }
    }
}

void SwitchBackOn(std::vector<Component*>& switched) {
    for (Component* c : switched) c->enabled = true;
    switched.clear();
}

bool CastIgnoringSelf(GameObject* self, const Vec3& from, const Vec3& to, Vec3& point, Vec3& normal, GameObject** hitObject) {
    Scene* sc = self ? self->scene() : nullptr;
    if (!sc) return false;
    Vec3 d = to - from;
    const float len = d.Magnitude();
    if (!(len > 1e-6f) || !std::isfinite(len)) return false;
    d = d * (1.0f / len);
    Physics3D::IgnoreHierarchy own(self);
    const RaycastHit3D h = sc->physics3D().Raycast(*sc, from, d, len, nullptr, Physics3D::kAllLayers, false);
    if (!h.hit) return false;
    point = h.point;
    normal = h.normal;
    if (hitObject) *hitObject = h.gameObject;
    return true;
}

bool FindBones(GameObject* go, Transform* out[humanoid::BoneCount]) {
    using namespace humanoid;
    for (int b = 0; b < BoneCount; ++b) out[b] = nullptr;
    if (!go) return false;
    if (auto* ch = go->GetComponent<Character>()) {
        // The baked body is one mesh: split it into its part rig to move the bones.
        if (!ch->driveExternal && !ch->PartsBuilt()) { ch->separateParts = true; ch->BuildParts(); }
        for (int b = 0; b < BoneCount; ++b)
            if (GameObject* g = ch->Part(b)) out[b] = g->transform;
    }
    if (!out[Hips] || !out[Torso]) {
        BoneMap map = MapHumanoidBones(go);
        if (!map.humanoid) return false;
        for (int b = 0; b < BoneCount; ++b) out[b] = map.bone[b] ? map.bone[b]->transform : nullptr;
    }
    for (int b = 0; b < BoneCount; ++b) if (!out[b]) return false;
    return true;
}

void HealthWatch::Begin(GameObject* go) {
    wasDead = DeathLocksInput(go);
    auto* hp = go ? go->GetComponent<HealthStat>() : nullptr;
    lastHealth = hp ? hp->health : -1.0f;
}

HealthWatch::Event HealthWatch::Poll(GameObject* go, float* damage, Vec3* from, bool* hasFrom) {
    if (!go) return None;
    // HealthStat, or a script die() on a body without one.
    auto* hp = go->GetComponent<HealthStat>();
    const bool dead = DeathLocksInput(go);
    Event e = None;
    if (dead && !wasDead) e = Died;
    else if (!dead && wasDead) e = Revived;
    else if (hp && !dead && lastHealth >= 0.0f && hp->health < lastHealth - 1e-4f) e = Hurt;
    if (damage) *damage = (hp && lastHealth >= 0.0f) ? lastHealth - hp->health : 0.0f;
    bool has = false;
    Vec3 f;
    if (hp) has = hp->LastHitFrom(f);
    if (from && has) *from = f;
    if (hasFrom) *hasFrom = has;
    wasDead = dead;
    lastHealth = hp ? hp->health : -1.0f;
    return e;
}

} // namespace ragdoll
} // namespace okay
