#pragma once
// The names the editor shows for the two animation components (#422 finding 8). Two different classes were both
// shown as "Animator":
//   - AnimStateMachine, the state graph (states, transitions, parameters, layers). It keeps the name "Animator": the
//     Animator window, the scripting API type "Animator" and the visual-script "Animator" nodes all mean this one.
//   - okay::Animator, a simple player for one keyframe clip that moves its own object. It is shown as "Clip Player".
// Only the shown names change. The class names and the scene file token ("animator") stay as they are, so every saved
// scene loads exactly as before. No ImGui here, so tests can include it.
#include "okay/Components/AnimControl.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cstring>

namespace okay::editor::vsed {

inline constexpr const char* kAnimGraphTitle = "Animator";
inline constexpr const char* kClipPlayerTitle = "Clip Player";

/// Which component a visual-script component id (from a node's requirements, see VisualScriptNodeDocs.hpp) means.
///   Graph         "Animator": the scripting API's name for the state graph (SetTrigger, SetBool, Play, CrossFade ...);
///                 its binding (EngineApi.cpp) finds an AnimStateMachine on the object or under it.
///   GraphNearby   "AnimStateMachine": the Anim* nodes (Anim Trigger, Anim Set Float/Bool, Anim State) find one on the
///                 object, a child or a parent (FindAnimComponent, AnimControl.hpp).
///   ClipPlayer    "ClipPlayer": the simple keyframe Animator class, on the object.
///   Character / ModelAnimator   Play Clip plays through either (PlayClipFrom), on the object, a child or a parent.
enum class VsAnimComponent { None, Graph, GraphNearby, ClipPlayer, Character, ModelAnimator };

inline VsAnimComponent VsAnimComponentFor(const char* id) {
    if (!id) return VsAnimComponent::None;
    if (std::strcmp(id, "Animator") == 0) return VsAnimComponent::Graph;
    if (std::strcmp(id, "AnimStateMachine") == 0) return VsAnimComponent::GraphNearby;
    if (std::strcmp(id, "ClipPlayer") == 0 || std::strcmp(id, "Clip Player") == 0) return VsAnimComponent::ClipPlayer;
    if (std::strcmp(id, "Character") == 0) return VsAnimComponent::Character;
    if (std::strcmp(id, "ModelAnimator") == 0) return VsAnimComponent::ModelAnimator;
    return VsAnimComponent::None;
}

/// True when `go` has what nodes asking for `c` use, searched where those nodes search at run time.
inline bool VsAnimHas(GameObject* go, VsAnimComponent c) {
    if (!go) return false;
    switch (c) {
        case VsAnimComponent::Graph:
            if (go->GetComponent<AnimStateMachine>()) return true;
            if (go->scene())
                for (const auto& up : go->scene()->Objects())
                    if (up && up->IsSelfOrDescendantOf(go) && up->GetComponent<AnimStateMachine>()) return true;
            return false;
        case VsAnimComponent::GraphNearby:   return FindAnimComponent<AnimStateMachine>(go) != nullptr;
        case VsAnimComponent::ClipPlayer:    return go->GetComponent<Animator>() != nullptr;
        case VsAnimComponent::Character:     return FindAnimComponent<Character>(go) != nullptr;
        case VsAnimComponent::ModelAnimator: return FindAnimComponent<ModelAnimator>(go) != nullptr;
        default: return false;
    }
}

/// The fix-it button: add the component `c` stands for to `go`. A Character added for Play Clip plays its built-in
/// animations at once (Idle, Walk, Run, Jump, Wave, Point ...); a clip of your own is added to it in the Inspector.
inline bool VsAnimAdd(GameObject* go, VsAnimComponent c) {
    if (!go) return false;
    switch (c) {
        case VsAnimComponent::Graph:
        case VsAnimComponent::GraphNearby:   return go->AddComponent<AnimStateMachine>() != nullptr;
        case VsAnimComponent::ClipPlayer:    return go->AddComponent<Animator>() != nullptr;
        case VsAnimComponent::Character:     return go->AddComponent<Character>() != nullptr;
        case VsAnimComponent::ModelAnimator: return go->AddComponent<ModelAnimator>() != nullptr;
        default: return false;
    }
}

} // namespace okay::editor::vsed
