#include "okay/Animation/PlayerSetup.hpp"
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"

namespace okay {

namespace {

bool HasBaseAnimator(GameObject* player) {
    if (!player) return false;
    for (AnimStateMachine* sm : player->GetComponents<AnimStateMachine>())
        if (sm->layer == 0) return true;
    return false;
}

} // namespace

PlayerAnimStatus InspectPlayerAnimation(Scene& scene, GameObject* player) {
    PlayerAnimStatus s;
    if (!player) return s;
    GameObject* model = FindCharacterModelRoot(scene, player);
    if (!model) return s;
    s.hasModel = true;
    s.model = model->name;
    s.bones = CountHumanoidBones(scene, model);
    FootIK probe;
    s.legs = probe.DetectHumanoidBones(scene, model) >= 6;
    const humanoid::BoneMap bm = humanoid::MapHumanoidBones(model);
    s.head = bm.bone[humanoid::Head] != nullptr;
    s.footIk = player->GetComponent<FootIK>() != nullptr;
    s.lookAt = player->GetComponent<LookAtIK>() != nullptr;
    s.animator = HasBaseAnimator(player);
    return s;
}

std::string WirePlayerAnimation(Scene& scene, GameObject* player) {
    if (!player) return "No player to set up.";
    GameObject* model = FindCharacterModelRoot(scene, player);
    if (!model) {
        return "No character model on '" + player->name +
               "'. Use Set Character Model on this controller, or right-click a model file and choose "
               "Make Playable Character. A new player starts in third person; V switches to the eyes. "
               "Player Controller, View, changes that.";
    }

    const std::string fix = FixCharacterAnimations(scene, player);
    const PlayerAnimStatus st = InspectPlayerAnimation(scene, player);
    // Components that were already there stay. Only add what is missing, and
    // never rewrite a weight or a chain the user set.
    bool addedFoot = false, addedLook = false, addedAnim = false;
    if (st.bones >= 8 && st.legs && !player->GetComponent<FootIK>()) {
        FootIK* fik = player->AddComponent<FootIK>();
        fik->useRaycast = true;
        fik->weight = 1.0f;
        fik->adjustPelvis = true;
        fik->plantDown = true;
        fik->alignToGround = true;
        fik->maxPelvisShift = 0.35f;
        fik->orient = true;
        fik->hipLimit = IKJointLimit::Hip();
        fik->kneeLimit = IKJointLimit::Knee();
        fik->DetectHumanoidBones(scene, model);
        for (const auto& up : scene.Objects()) {
            if (!up->IsSelfOrDescendantOf(model)) continue;
            if (up->name == fik->leftFootName && up->transform)
                fik->footUpAxis = (up->transform->Rotation().Inverse() * Vec3::Up).Normalized();
            else if (up->name == fik->rightFootName && up->transform)
                fik->footUpAxisR = (up->transform->Rotation().Inverse() * Vec3::Up).Normalized();
        }
        fik->Start();
        addedFoot = true;
    }
    if (st.bones >= 8 && st.head && !player->GetComponent<LookAtIK>()) {
        const humanoid::BoneMap bm = humanoid::MapHumanoidBones(model);
        LookAtIK* look = player->AddComponent<LookAtIK>();
        look->weight = 1.0f;
        look->ikActive = true;
        look->aimAtPoint = false;
        look->chainNames.clear();
        look->chain.clear();
        if (bm.bone[humanoid::Torso]) look->chainNames.push_back(bm.bone[humanoid::Torso]->name);
        if (bm.bone[humanoid::Head]) look->chainNames.push_back(bm.bone[humanoid::Head]->name);
        look->Start();
        addedLook = true;
    }
    if (st.bones >= 8 && player->GetComponent<Character>() && !HasBaseAnimator(player)) {
        HumanoidLibrary::EnsureAnimator(player);
        addedAnim = true;
    }

    std::string msg = fix;
    msg += " Rig " + std::to_string(st.bones) + "/" + std::to_string(humanoid::BoneCount) + ".";
    if (st.bones < 8) {
        msg += " Not enough humanoid bones for built-in walk, foot planting or look-at (need 8). "
               "Right-click the model and choose Map Humanoid Bones or Rig Model (Humanoid).";
        return msg;
    }
    if (addedFoot || addedLook || addedAnim) {
        msg += " Added";
        bool any = false;
        auto add = [&](bool on, const char* name) {
            if (!on) return;
            msg += any ? ", " : " ";
            msg += name;
            any = true;
        };
        add(addedFoot, "Foot IK");
        add(addedLook, "Look-At");
        add(addedAnim, "the default Animator");
        msg += ".";
    }
    std::string kept;
    if (!addedFoot && st.legs) kept += "Foot IK";
    if (!addedLook && st.head) kept += kept.empty() ? "Look-At" : ", Look-At";
    if (!addedAnim && st.animator) kept += kept.empty() ? "the Animator" : ", the Animator";
    if (!kept.empty())
        msg += " Left " + kept + " as they were (weights, chains and the state graph are unchanged).";
    if (!st.legs) msg += " No two-legged chain, so Foot IK was not added.";
    if (!st.head) msg += " No head bone, so Look-At was not added.";
    return msg;
}

} // namespace okay
