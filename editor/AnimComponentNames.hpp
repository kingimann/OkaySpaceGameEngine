#pragma once
// The names the editor shows for the two animation components (#422 finding 8). Two different classes were both
// shown as "Animator":
//   - AnimStateMachine, the state graph (states, transitions, parameters, layers). It keeps the name "Animator": the
//     Animator window, the scripting API type "Animator" and the visual-script "Animator" nodes all mean this one.
//   - okay::Animator, a simple player for one keyframe clip that moves its own object. It is shown as "Clip Player".
// Only the shown names change. The class names and the scene file token ("animator") stay as they are, so every saved
// scene loads exactly as before. No ImGui here, so tests can include it.
#include <cstring>

namespace okay::editor::vsed {

inline constexpr const char* kAnimGraphTitle = "Animator";
inline constexpr const char* kClipPlayerTitle = "Clip Player";

/// Which component a visual-script component id (from a node's requirements, see VisualScriptNodeDocs.hpp) means.
enum class VsAnimComponent { None, Graph, ClipPlayer };

/// "Animator" is the scripting API's name for the state graph (SetTrigger, SetBool, Play, CrossFade ...), so a node
/// that needs an "Animator" needs an AnimStateMachine. The clip player is asked for as "ClipPlayer".
inline VsAnimComponent VsAnimComponentFor(const char* id) {
    if (!id) return VsAnimComponent::None;
    if (std::strcmp(id, "Animator") == 0 || std::strcmp(id, "AnimStateMachine") == 0) return VsAnimComponent::Graph;
    if (std::strcmp(id, "ClipPlayer") == 0 || std::strcmp(id, "Clip Player") == 0) return VsAnimComponent::ClipPlayer;
    return VsAnimComponent::None;
}

} // namespace okay::editor::vsed
